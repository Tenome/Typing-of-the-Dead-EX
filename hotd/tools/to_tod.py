"""Converts a House of the Dead body into a Typing of the Dead EX model pack + texture pack.

    python to_tod.py <thotd.exe> <HOTD creature folder> <TODEX folder> <body> <kind> [<kind> ...] [--motion M]
                     [--slot N] [--name PACK]

For each kind, writes TODEX\\models\\<pack>\\<bank>\\<model id>.obj (one per body part) and
TODEX\\textures\\<pack>\\<bank>\\<slot>.png (an atlas of the HOTD texture pages and flat colours, in a texture slot
that only this bank uses). Each HOTD part is re-oriented from its standing pose onto the TOD rest
pose (arms and legs straight down), stretched along the bone to the TOD bone length, so TOD's animations move it.
"""
import argparse
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..'))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'mod', 'tools'))

from PIL import Image

import character_kinds
import exe_view
import model_formats
import texture_formats
from assemble import load_parts, standing_pose
from pose import MOTION_COUNT, apply, body_roots, joint_count, motion_frame, motion_joint_count, multiply, posed_nodes
from skeletons import find_nodes
from vmc import read_pages, page_rgba

TOD_CHEST, TOD_HEAD, TOD_HIPS = 1, 2, 9
TOD_ARMS = ((3, 4, 5), (6, 7, 8))
TOD_LEGS = ((10, 11, 12), (13, 14, 15))
UP = (0.0, 1.0, 0.0)
BONE_CHILD = {3: 4, 4: 5, 6: 7, 7: 8, 10: 11, 11: 12, 13: 14, 14: 15}
INHERIT = {2: 1, 5: 4, 8: 7, 12: 11, 15: 14}
PAGE = 256
VARIANT_DISTANCE = 1.0
FRAME_DISTANCE = 3.0
HIDE_DISTANCE = 4.0
SPECIAL_BLEND_FLAG = 0x02000000
SWATCH = 8
GAME_TO_TOD = [[1, 0, 0], [0, -1, 0], [0, 0, -1]]


def sub(a, b):
    return tuple(a[k] - b[k] for k in range(3))


def length(v):
    return math.sqrt(sum(c * c for c in v))


def normalise(v):
    n = length(v) or 1.0
    return tuple(c / n for c in v)


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return sum(a[k] * b[k] for k in range(3))


def rotation_between(a, b):
    """Smallest rotation taking direction a to direction b."""
    a, b = normalise(a), normalise(b)
    axis = cross(a, b)
    s, c = length(axis), dot(a, b)
    if s < 1e-8:
        if c > 0:
            return [[1, 0, 0], [0, 1, 0], [0, 0, 1]]
        helper = (1, 0, 0) if abs(a[0]) < 0.9 else (0, 1, 0)
        axis, s, c = normalise(cross(a, helper)), 0.0, -1.0
        angle = math.pi
    else:
        axis = normalise(axis)
        angle = math.atan2(s, c)
    x, y, z = axis
    cs, sn, t = math.cos(angle), math.sin(angle), 1 - math.cos(angle)
    return [[t * x * x + cs, t * x * y - sn * z, t * x * z + sn * y],
            [t * x * y + sn * z, t * y * y + cs, t * y * z - sn * x],
            [t * x * z - sn * y, t * y * z + sn * x, t * z * z + cs]]


def frame_rotation(up_from, side_from, up_to, side_to):
    """Rotation taking the frame (up_from, side_from) onto (up_to, side_to)."""
    def basis(up, side):
        u = normalise(up)
        f = normalise(cross(side, u))
        s = cross(u, f)
        return [s, u, f]
    source, target = basis(up_from, side_from), basis(up_to, side_to)
    return [[sum(target[k][i] * source[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def scale_matrix(direction, along, across):
    d = normalise(direction)
    return [[across * (i == j) + (along - across) * d[i] * d[j] for j in range(3)] for i in range(3)]


def hotd_joints(body, motion, nodes, parts):
    """{joint index: (node, world rotation, world position)} in TOD axes."""
    joints = joint_count(body)
    _, angles = motion_frame(motion, 0, joints)
    result = {}
    for node, rotation, position in posed_nodes(body_roots()[body], nodes, angles):
        result[node['index']] = (node, multiply(GAME_TO_TOD, rotation), apply(GAME_TO_TOD, position))
    return result


def axis_rotation(axis, angle):
    x, y, z = normalise(axis)
    c, s, t = math.cos(angle), math.sin(angle), 1 - math.cos(angle)
    return [[t * x * x + c, t * x * y - s * z, t * x * z + s * y],
            [t * x * y + s * z, t * y * y + c, t * y * z - s * x],
            [t * x * z - s * y, t * y * z + s * x, t * z * z + c]]


def skeleton_roles(joints):
    """(TOD part -> HOTD joint, HOTD joint -> TOD part) from the tree: the root's two children are the chest (the
    taller subtree: head leaf and two arm chains) and the hips (two leg chains, plus short extra pieces such as coat
    tails, which ride on the hips). Forward is where the toes point; right = forward x up (TOD: -x)."""
    index_of = {node['va']: index for index, (node, _, _) in joints.items()}
    children = {index: [index_of[c] for c in node['children'] if c in index_of] for index, (node, _, _) in joints.items()}
    position = {index: joints[index][2] for index in joints}

    def subtree(index):
        found = [index]
        for child in children[index]:
            found += subtree(child)
        return found

    def chain(index):
        found = [index]
        while children[found[-1]]:
            found.append(children[found[-1]][0])
        return found

    root = min(joints)
    if len(children[root]) != 2:
        raise SystemExit('unexpected skeleton: the root has %d children' % len(children[root]))
    chest, hips = sorted(children[root], key=lambda i: -max(position[j][1] for j in subtree(i)))
    leaves = [c for c in children[chest] if not children[c]]
    arms = [chain(c) for c in children[chest] if children[c]]
    legs = [chain(c) for c in children[hips] if len(chain(c)) >= 3]
    extras = [j for c in children[hips] if len(chain(c)) < 3 for j in subtree(c)]
    if not leaves or len(arms) != 2 or len(legs) != 2:
        raise SystemExit('unexpected skeleton: %d head(s), %d arm(s), %d leg(s)' % (len(leaves), len(arms), len(legs)))
    head = max(leaves, key=lambda i: position[i][1])
    toes = [sub(position[leg[3]], position[leg[2]]) for leg in legs if len(leg) >= 4]
    forward = normalise((sum(t[0] for t in toes), 0.0, sum(t[2] for t in toes))) if toes else (0.0, 0.0, 1.0)
    right = cross(forward, UP)
    part_joint = {TOD_CHEST: chest, TOD_HEAD: head, TOD_HIPS: hips}
    joint_part = {chest: TOD_CHEST, head: TOD_HEAD, hips: TOD_HIPS}
    joint_part.update({j: TOD_HIPS for j in extras})
    for chains, slots in ((arms, TOD_ARMS), (legs, TOD_LEGS)):
        for joints_of_chain, parts in zip(sorted(chains, key=lambda c: -dot(position[c[0]], right)), slots):
            for k, joint in enumerate(joints_of_chain):
                part = parts[min(k, len(parts) - 1)]
                joint_part[joint] = part
                if k < len(parts):
                    part_joint[part] = joint
    for leaf in leaves:
        joint_part.setdefault(leaf, TOD_HEAD)
    return part_joint, joint_part


def tod_skeleton(tables, kind):
    return {part: (model, position) for part, model, position in character_kinds.body_parts(tables, kind) if part > 0}


def part_transforms(joints, tod, part_joint, head_centre):
    """{tod part: (matrix, hotd pivot)} mapping posed HOTD world positions to TOD part-local positions. The head
    follows the chest, turned half round about its neck: HOTD head meshes face away from what the motions give
    (the game turns heads at run time)."""
    hp = {part: joints[joint][2] for part, joint in part_joint.items()}
    tp = {part: tod[part][1] for part in tod}
    hotd_height = hp[2][1] - (hp[12][1] + hp[15][1]) / 2
    tod_height = tp[2][1] - (tp[12][1] + tp[15][1]) / 2
    scale = tod_height / hotd_height
    rotations, transforms = {}, {}
    rotations[1] = frame_rotation(sub(hp[2], hp[1]), sub(hp[3], hp[6]), sub(tp[2], tp[1]), sub(tp[3], tp[6]))
    hotd_legs = tuple((hp[10][k] + hp[13][k]) / 2 for k in range(3))
    tod_legs = tuple((tp[10][k] + tp[13][k]) / 2 for k in range(3))
    rotations[9] = frame_rotation(sub(hp[9], hotd_legs), sub(hp[10], hp[13]), sub(tp[9], tod_legs), sub(tp[10], tp[13]))
    for part, child in BONE_CHILD.items():
        rotations[part] = rotation_between(sub(hp[child], hp[part]), sub(tp[child], tp[part]))
    for part, parent in INHERIT.items():
        rotations[part] = rotations[parent]
    head_axis = sub(head_centre, hp[TOD_HEAD])
    turned = multiply(rotations[TOD_CHEST], axis_rotation(head_axis, math.pi))
    rotations[TOD_HEAD] = multiply(rotation_between(apply(turned, head_axis), UP), turned)
    for part in tod:
        if part not in rotations:
            continue
        matrix = [[scale * rotations[part][i][j] for j in range(3)] for i in range(3)]
        if part in BONE_CHILD:
            child = BONE_CHILD[part]
            tod_bone = sub(tp[child], tp[part])
            hotd_bone = length(sub(hp[child], hp[part]))
            matrix = multiply(scale_matrix(tod_bone, length(tod_bone) / hotd_bone, scale), rotations[part])
        transforms[part] = (matrix, hp[part])
    return transforms


class Atlas:
    """Packs the used HOTD pages and flat colours into one power-of-two image. UVs count v from the top of the
    PNG, which is what the game shows once todmod loads the OBJ (v flipped) and the PNG (rows flipped)."""

    def __init__(self, pages, page_numbers, colours):
        self.page_numbers = sorted(page_numbers)
        self.colours = sorted(colours)
        cells = len(self.page_numbers) + (1 if self.colours else 0)
        columns = 1
        while columns * columns < cells:
            columns *= 2
        rows = 1
        while columns * rows < cells:
            rows *= 2
        self.size = (columns * PAGE, rows * PAGE)
        self.image = Image.new('RGB', self.size, (128, 128, 128))
        self.origin = {}
        for i, number in enumerate(self.page_numbers):
            width, height, pixels, _ = pages[number]
            origin = ((i % columns) * PAGE, (i // columns) * PAGE)
            self.image.paste(Image.frombytes('RGBA', (width, height), page_rgba(width, height, pixels)).convert('RGB'), origin)
            self.origin[number] = origin
        swatch_cell = len(self.page_numbers)
        self.swatch_origin = ((swatch_cell % columns) * PAGE, (swatch_cell // columns) * PAGE)
        per_row = PAGE // SWATCH
        self.swatch = {}
        for i, colour in enumerate(self.colours):
            x = self.swatch_origin[0] + (i % per_row) * SWATCH
            y = self.swatch_origin[1] + (i // per_row) * SWATCH
            self.image.paste(colour, (x, y, x + SWATCH, y + SWATCH))
            self.swatch[colour] = (x + SWATCH / 2, y + SWATCH / 2)

    def uv(self, page, u, v):
        x, y = self.origin[page]
        u = min(max(u, 0.5), PAGE - 0.5)
        v = min(max(v, 0.5), PAGE - 0.5)
        return ((x + u) / self.size[0], (y + v) / self.size[1])

    def colour_uv(self, colour):
        x, y = self.swatch[colour]
        return (x / self.size[0], y / self.size[1])


def flat_colour(rbh, part, index):
    return tuple(int(c * 255) for c in part.colour(rbh, index))


def atlas_texture_slot(tables, bank):
    """A texture of the bank shared with as few other banks as possible (the largest among those)."""
    users = {}
    for name in tables.texture_table:
        for header in tables.bank_textures(name):
            users.setdefault(header[5], set()).add(name)
    textures = list(enumerate(tables.bank_textures(bank + '.bin')))
    if not textures:
        raise SystemExit('bank %s has no textures' % bank)
    return min(textures, key=lambda item: (len(users[item[1][5]]), -item[1][0] * item[1][1]))[0]


def template_chunk(model):
    """The chunk whose material the converted part copies: textured, white, without the special blend flag,
    with the most vertices."""
    def score(item):
        index, chunk = item
        if chunk['header'] is None:
            return (0, 0, 0, 0)
        word0 = struct.unpack_from('<I', chunk['header'], 0)[0]
        diffuse = struct.unpack_from('<3f', chunk['header'], 0x30)
        vertices = sum(len(strip['vertices']) for strip in chunk['strips'])
        return (chunk['texture'] != model_formats.NO_TEXTURE, not word0 & SPECIAL_BLEND_FLAG, min(diffuse) > 0.9, vertices)
    return max(enumerate(model['chunks']), key=score)[0]


def bank_model_boxes(pol_hab, tables, bank):
    """{model id: (low corner, high corner, template chunk, vertex count)} of every model in the TOD bank."""
    bank_number = next(number for number, name, _ in tables.banks() if name == bank + '.bin')
    model_ids = tables.bank_model_ids(bank_number)
    boxes = {}
    for model_id, data, _ in model_formats.bank_objects(texture_formats.bank_bytes(pol_hab[bank + '.bin']), model_ids):
        model = model_formats.parse_object(data)
        points = [v[:3] for chunk in model['chunks'] for strip in chunk['strips'] for v in strip['vertices']]
        if points:
            boxes[model_id] = (tuple(min(p[k] for p in points) for k in range(3)),
                               tuple(max(p[k] for p in points) for k in range(3)), template_chunk(model), len(points))
    return boxes


def part_variants(boxes, base_models):
    """({base model id: [model ids of the same part]}, [model ids to hide]) by bounding-box distance: close models
    and animation frames (same vertex count) are variants; other nearby pieces (hair, hats) are hidden."""
    variants = {model: [] for model in base_models}
    hidden = []
    for model, (low, high, _, vertices) in boxes.items():
        if model in base_models:
            continue
        def distance(base):
            base_low, base_high = boxes[base][:2]
            return sum(abs(low[k] - base_low[k]) + abs(high[k] - base_high[k]) for k in range(3))
        nearest = min((base for base in base_models if base in boxes), key=distance)
        gap = distance(nearest)
        if gap <= VARIANT_DISTANCE or (vertices == boxes[nearest][3] and gap <= FRAME_DISTANCE):
            variants[nearest].append(model)
        elif gap <= HIDE_DISTANCE:
            hidden.append(model)
    return variants, sorted(hidden)


CACHE = {}


def cached(key, load):
    if key not in CACHE:
        CACHE[key] = load()
    return CACHE[key]


def hotd_data(args):
    def load():
        exe_view.load(args.exe)
        return load_parts(args.creatures), find_nodes(), read_pages(args.vmc)
    return cached(('hotd', args.exe, args.creatures), load)


def hotd_body(args):
    """Posed HOTD meshes: [(tod part, rbh, part, world vertices)], motion."""
    parts, nodes, _ = hotd_data(args)
    body = args.body
    joints_needed = joint_count(body)
    motion = args.motion
    if motion is None:
        candidates = [m for m in range(MOTION_COUNT) if motion_joint_count(m) == joints_needed]
        motion, _ = standing_pose(body_roots()[body], nodes, parts, joints_needed, candidates)
    joints = hotd_joints(body, motion, nodes, parts)
    part_joint, joint_part = skeleton_roles(joints)
    pieces = []
    for index, (node, rotation, position) in joints.items():
        if node['id'] not in parts or index not in joint_part:
            continue
        rbh, part = parts[node['id']]
        world = [tuple(position[k] + r for k, r in enumerate(apply(rotation, vertex))) for vertex in part.vertices]
        pieces.append((joint_part[index], rbh, part, world))
    return joints, pieces, motion, part_joint


def write_part(path, header, pieces, atlas, chunk, slot):
    lines = [header, 'usemtl m%d_t%d' % (chunk, slot)]
    uv_lines, face_lines = [], []
    vertex_base, uv_base = 0, 0
    for rbh, part, local in pieces:
        lines += ['v %.6f %.6f %.6f' % (x, y, -z) for x, y, z in local]
        for indices, page, extra in part.faces:
            if page is None:
                corner_uvs = [atlas.colour_uv(flat_colour(rbh, part, extra))] * len(indices)
            else:
                corner_uvs = [atlas.uv(page, u, v) for u, v in extra]
            uv_lines += ['vt %.6f %.6f' % uv for uv in corner_uvs]
            corners = ['%d/%d' % (vertex_base + i + 1, uv_base + k + 1) for k, i in enumerate(indices)]
            for k in range(1, len(corners) - 1):
                face_lines.append('f %s %s %s' % (corners[0], corners[k], corners[k + 1]))
            uv_base += len(indices)
        vertex_base += len(local)
    with open(path, 'w', newline='\n') as out:
        out.write('\n'.join(lines + uv_lines + face_lines) + '\n')


def write_hidden(path, header, chunk, slot):
    """A stand-in with one zero-area triangle, for TOD pieces the converted body has no counterpart for."""
    with open(path, 'w', newline='\n') as out:
        out.write('\n'.join([header, 'usemtl m%d_t%d' % (chunk, slot), 'v 0 0 0', 'vt 0 0', 'f 1/1 1/1 1/1']) + '\n')


def convert(args):
    joints, pieces, motion, part_joint = hotd_body(args)
    head_points = [v for target, _, _, world in pieces if target == TOD_HEAD for v in world]
    if head_points:
        head_centre = tuple(sum(p[k] for p in head_points) / len(head_points) for k in range(3))
    else:
        neck = joints[part_joint[TOD_HEAD]][2]
        head_centre = (neck[0], neck[1] + 1.0, neck[2])
    page_numbers = {page for _, _, part, _ in pieces for _, page, _ in part.faces if page is not None}
    colours = {flat_colour(rbh, part, extra) for _, rbh, part, _ in pieces for _, page, extra in part.faces if page is None}
    atlas = Atlas(hotd_data(args)[2], page_numbers, colours)
    tables = cached(('tables', args.todex), lambda: model_formats.ModelTables(os.path.join(args.todex, 'Table.dll')))
    pack = args.name or 'hotd_body%02d' % args.body
    pol_hab = cached(('pol', args.todex), lambda: texture_formats.read_hab(os.path.join(args.todex, 'pol.HAB')))
    for kind in args.kinds:
        tod = tod_skeleton(tables, kind)
        transforms = part_transforms(joints, tod, part_joint, head_centre)
        by_part = {}
        for target, rbh, part, world in pieces:
            matrix, pivot = transforms[target]
            by_part.setdefault(target, []).append((rbh, part, [apply(matrix, sub(v, pivot)) for v in world]))
        banks = set()
        for target, items in sorted(by_part.items()):
            model_id = tod[target][0]
            bank = tables.bank_name(tables.u16(tables.exports['PbkTbl'] + 2 * model_id))[:-4]
            banks.add(bank)
        if len(banks) != 1:
            raise SystemExit('kind %d uses several banks (%s)' % (kind, ', '.join(sorted(banks))))
        bank = banks.pop()
        slot = args.slot if args.slot is not None else atlas_texture_slot(tables, bank)
        folder = os.path.join(args.todex, 'models', pack, bank)
        os.makedirs(folder, exist_ok=True)
        boxes = bank_model_boxes(pol_hab, tables, bank)
        base_models = [tod[target][0] for target in by_part]
        missing = [tod[part][0] for part in tod if part not in by_part and tod[part][0] in boxes]
        variants, hidden = part_variants(boxes, base_models + missing)
        model_count = 0
        for target, items in sorted(by_part.items()):
            for model_id in [tod[target][0]] + variants[tod[target][0]]:
                write_part(os.path.join(folder, '%d.obj' % model_id),
                           '# House of the Dead body %d -> Typing of the Dead kind %d part %d' % (args.body, kind, target),
                           items, atlas, boxes[model_id][2] if model_id in boxes else 0, slot)
                model_count += 1
        hidden = sorted(set(hidden) | {model for base in missing for model in [base] + variants[base]})
        for model_id in hidden:
            write_hidden(os.path.join(folder, '%d.obj' % model_id),
                         '# Typing of the Dead kind %d piece without a House of the Dead counterpart (hidden)' % kind,
                         boxes[model_id][2], slot)
        texture_folder = os.path.join(args.todex, 'textures', pack, bank)
        os.makedirs(texture_folder, exist_ok=True)
        texture_id = tables.bank_textures(bank + '.bin')[slot][5]
        atlas.image.save(os.path.join(texture_folder, '%03d_id%d.png' % (slot, texture_id)))
        print('%s: body %d (motion %d) -> kind %d, %d models (%d parts and their variants), %d hidden, in %s, atlas '
              '%dx%d in texture slot %d' % (pack, args.body, motion, kind, model_count, len(by_part), len(hidden), bank,
                                            atlas.size[0], atlas.size[1], slot))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('exe')
    parser.add_argument('creatures')
    parser.add_argument('todex')
    parser.add_argument('body', type=int)
    parser.add_argument('kinds', type=int, nargs='+')
    parser.add_argument('--motion', type=int)
    parser.add_argument('--slot', type=int)
    parser.add_argument('--name')
    args = parser.parse_args()
    args.vmc = os.path.join(args.creatures, 'texture.vmc')
    convert(args)


if __name__ == '__main__':
    main()
