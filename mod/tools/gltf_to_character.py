"""Turns a rigged character (glTF .gltf/.glb with a skin: skeleton + vertex weights, y up, facing +z) into a
Typing of the Dead EX model pack whose mesh bends with the game's animation, using the model's own joints and
weights.

    python gltf_to_character.py <model.glb> <TODEX folder> <pack> <kind> [<kind> ...] [--name "Shown name"]

Bones are matched to the game's 15 body parts by name (Mixamo, Blender/Rigify, Biped/GTA, VRM and similar names:
hips/pelvis, spine/chest, neck/head, upper arm, forearm, hand, thigh, calf, foot); fingers, toes, twist and other
helper bones follow their parent. Left and right come from where the bones are. FBX models: import them into
Blender and export as glTF 2.0 (.glb). Then tick the pack under "Model packs" in todmod_config.exe.
"""
import argparse
import base64
import io
import json
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from PIL import Image

import model_formats
import texture_formats
from obj_to_character import (ARMS, CHEST, CHILD, HEAD, HIPS, LEGS, emit_skinned, kind_skeleton, power_of_two,
                              vertex_normals, write_names)
from to_tod import rotation_between, scale_matrix, sub
from pose import multiply

COMPONENTS = {5120: 'b', 5121: 'B', 5122: 'h', 5123: 'H', 5125: 'I', 5126: 'f'}
WIDTHS = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}
NORMALISE = {5121: 255.0, 5123: 65535.0}
MAX_INFLUENCES = 4
ATLAS_CELL = 512
ROLE_WORDS = [
    ('fore', ('forearm', 'lowerarm', 'lower_arm', 'elbow', 'ellbow')),
    ('hand', ('hand', 'finger', 'thumb', 'index', 'middle', 'ring', 'pinky', 'little', 'wrist', 'palm')),
    ('upper', ('upperarm', 'upper_arm', 'arm')),
    ('thigh', ('thigh', 'upleg', 'upperleg', 'upper_leg')),
    ('foot', ('foot', 'toe', 'ankle', 'heel', 'ball')),
    ('shin', ('calf', 'shin', 'lowerleg', 'lower_leg', 'knee', 'leg')),
    ('head', ('head', 'neck', 'jaw', 'eye', 'brow', 'hair', 'nose', 'ear', 'tongue', 'teeth')),
    ('chest', ('spine', 'chest', 'torso', 'clavicle', 'shoulder', 'collar', 'breast', 'body', 'ribcage', 'back')),
    ('hips', ('hips', 'hip', 'pelvis', 'root', 'waist')),
]
LIMB_PARTS = {'upper': 0, 'fore': 1, 'hand': 2, 'thigh': 0, 'shin': 1, 'foot': 2}


class Gltf:
    def __init__(self, path):
        self.folder = os.path.dirname(path)
        data = open(path, 'rb').read()
        self.binary = None
        if data[:4] == b'glTF':
            offset = 12
            while offset < len(data):
                length, kind = struct.unpack_from('<I4s', data, offset)
                chunk = data[offset + 8:offset + 8 + length]
                if kind == b'JSON':
                    self.json = json.loads(chunk.decode('utf-8'))
                elif kind == b'BIN\x00':
                    self.binary = chunk
                offset += 8 + length
        else:
            self.json = json.loads(data.decode('utf-8'))
        self.buffers = [self.load_buffer(b) for b in self.json.get('buffers', [])]

    def load_uri(self, uri):
        if uri.startswith('data:'):
            return base64.b64decode(uri.split(',', 1)[1])
        return open(os.path.join(self.folder, uri.replace('%20', ' ')), 'rb').read()

    def load_buffer(self, buffer):
        return self.load_uri(buffer['uri']) if 'uri' in buffer else self.binary

    def view_bytes(self, index):
        view = self.json['bufferViews'][index]
        start = view.get('byteOffset', 0)
        return self.buffers[view['buffer']][start:start + view['byteLength']], view.get('byteStride')

    def accessor(self, index):
        accessor = self.json['accessors'][index]
        width = WIDTHS[accessor['type']]
        code = COMPONENTS[accessor['componentType']]
        size = struct.calcsize('<' + code)
        count = accessor['count']
        if 'bufferView' not in accessor:
            return [tuple([0] * width) for _ in range(count)]
        data, stride = self.view_bytes(accessor['bufferView'])
        start = accessor.get('byteOffset', 0)
        stride = stride or size * width
        scale = NORMALISE.get(accessor['componentType']) if accessor.get('normalized') else None
        values = []
        for i in range(count):
            item = struct.unpack_from('<%d%s' % (width, code), data, start + i * stride)
            values.append(tuple(v / scale for v in item) if scale else item)
        return values

    def image(self, index):
        image = self.json['images'][index]
        if 'uri' in image:
            raw = self.load_uri(image['uri'])
        else:
            raw, _ = self.view_bytes(image['bufferView'])
        return Image.open(io.BytesIO(raw)).convert('RGB')


def matrix_of(node):
    if 'matrix' in node:
        m = node['matrix']
        return [[m[c * 4 + r] for c in range(4)] for r in range(4)]
    tx, ty, tz = node.get('translation', (0, 0, 0))
    x, y, z, w = node.get('rotation', (0, 0, 0, 1))
    sx, sy, sz = node.get('scale', (1, 1, 1))
    rotation = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
    return [[rotation[r][0] * sx, rotation[r][1] * sy, rotation[r][2] * sz, (tx, ty, tz)[r]] for r in range(3)] + [[0, 0, 0, 1]]


def mat_mul(a, b):
    return [[sum(a[r][k] * b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]


def mat_point(m, p):
    return tuple(m[r][0] * p[0] + m[r][1] * p[1] + m[r][2] * p[2] + m[r][3] for r in range(3))


def mat_direction(m, d):
    return tuple(m[r][0] * d[0] + m[r][1] * d[1] + m[r][2] * d[2] for r in range(3))


def world_matrices(gltf):
    nodes = gltf.json['nodes']
    parents = {child: index for index, node in enumerate(nodes) for child in node.get('children', [])}
    cache = {}

    def world(index):
        if index not in cache:
            local = matrix_of(nodes[index])
            cache[index] = mat_mul(world(parents[index]), local) if index in parents else local
        return cache[index]
    return [world(i) for i in range(len(nodes))], parents


def bone_role(name):
    clean = name.lower()
    for prefix in ('mixamorig:', 'mixamorig_', 'def-', 'org-', 'bip01 ', 'bip001 ', 'b_', 'j_bip_c_', 'j_bip_l_', 'j_bip_r_'):
        if clean.startswith(prefix):
            clean = clean[len(prefix):]
    clean = clean.replace(' ', '').replace('.', '_')
    if clean.startswith('armature'):
        return None
    for role, words in ROLE_WORDS:
        if any(word in clean for word in words):
            return role
    return None


def map_bones(gltf, skin, joint_positions, parents):
    """{joint index in the skin: game part}."""
    nodes = gltf.json['nodes']
    joints = skin['joints']
    node_to_joint = {node: i for i, node in enumerate(joints)}
    roles = {i: bone_role(nodes[node].get('name', '')) for i, node in enumerate(joints)}
    parts = {}

    def part_of(i, depth=0):
        if i in parts:
            return parts[i]
        role = roles[i]
        if role is None or depth > 64:
            parent = parents.get(joints[i])
            part = part_of(node_to_joint[parent], depth + 1) if parent in node_to_joint else HIPS
        elif role in LIMB_PARTS:
            side = -1 if joint_positions[i][0] < 0 else 1
            part = (ARMS if role in ('upper', 'fore', 'hand') else LEGS)[side][LIMB_PARTS[role]]
        else:
            part = {'head': HEAD, 'chest': CHEST, 'hips': HIPS}[role]
        parts[i] = part
        return part
    for i in range(len(joints)):
        part_of(i)
    return parts


def part_joint_positions(skin_joints, parts, joint_positions, parents, node_to_joint):
    """{part: position of its bone nearest the skeleton's root}."""
    def depth(node):
        steps = 0
        while node in parents and steps < 256:
            node = parents[node]
            steps += 1
        return steps
    best = {}
    for i, node in enumerate(skin_joints):
        d = depth(node)
        if parts[i] not in best or d < best[parts[i]][0]:
            best[parts[i]] = (d, joint_positions[i])
    return {part: position for part, (d, position) in best.items()}


def build_matrices(tops, tod):
    missing = [p for p in (HEAD, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15) if p not in tops]
    if missing:
        raise SystemExit('the skeleton has no bones for game parts %s; check the bone names' % missing)
    source_height = tops[HEAD][1] - tops[12][1]
    scale = (tod[HEAD][1][1] - tod[12][1][1]) / source_height
    uniform = [[scale, 0, 0], [0, scale, 0], [0, 0, scale]]
    matrices, rotations = {}, {}
    for part, child in CHILD.items():
        source_bone = sub(tops[child], tops[part])
        target_bone = sub(tod[child][1], tod[part][1])
        source_length = sum(c * c for c in source_bone) ** 0.5
        target_length = sum(c * c for c in target_bone) ** 0.5
        if source_length < 1e-9:
            raise SystemExit('bones of parts %d and %d sit on the same point' % (part, child))
        rotations[part] = rotation_between(source_bone, target_bone)
        matrices[part] = (multiply(scale_matrix(target_bone, target_length / source_length, scale), rotations[part]), tops[part])
    for hand, fore in ((5, 4), (8, 7), (12, 11), (15, 14)):
        matrices[hand] = ([[c * scale for c in row] for row in rotations[fore]], tops[hand])
    shoulders = tuple((tops[3][k] + tops[6][k]) / 2 for k in range(3))
    hips = tuple((tops[10][k] + tops[13][k]) / 2 for k in range(3))
    chest_pivot = tuple(shoulders[k] - (tod[3][1][k] + tod[6][1][k]) / 2 / scale + tod[CHEST][1][k] / scale for k in range(3))
    hips_pivot = tuple(hips[k] - (tod[10][1][k] + tod[13][1][k]) / 2 / scale + tod[HIPS][1][k] / scale for k in range(3))
    chest_pivot = (0.0, chest_pivot[1], shoulders[2])
    hips_pivot = (0.0, hips_pivot[1], hips[2])
    matrices[CHEST] = (uniform, chest_pivot)
    matrices[HIPS] = (uniform, hips_pivot)
    matrices[HEAD] = (uniform, tops[HEAD])
    return matrices, scale


def material_images(gltf, material_index):
    if material_index is None:
        return None, (200, 200, 200)
    material = gltf.json['materials'][material_index]
    pbr = material.get('pbrMetallicRoughness', {})
    colour = tuple(int(c * 255) for c in pbr.get('baseColorFactor', (1, 1, 1, 1))[:3])
    texture = pbr.get('baseColorTexture')
    if texture is None:
        return None, colour
    source = gltf.json['textures'][texture['index']].get('source')
    return source, colour


def convert(args):
    gltf = Gltf(args.model)
    skins = gltf.json.get('skins', [])
    if not skins:
        raise SystemExit('%s has no skin (skeleton with vertex weights); use obj_to_character.py for unrigged models' % args.model)
    skin = skins[0]
    worlds, parents = world_matrices(gltf)
    inverse_bind = gltf.accessor(skin['inverseBindMatrices']) if 'inverseBindMatrices' in skin else None
    joint_positions = []
    for i, node in enumerate(skin['joints']):
        if inverse_bind:
            m = inverse_bind[i]
            ibm = [[m[c * 4 + r] for c in range(4)] for r in range(4)]
            position = invert_affine_translation(ibm)
        else:
            position = tuple(worlds[node][r][3] for r in range(3))
        joint_positions.append((-position[0], position[1], position[2]))
    node_to_joint = {node: i for i, node in enumerate(skin['joints'])}
    parts = map_bones(gltf, skin, joint_positions, parents)
    tops = part_joint_positions(skin['joints'], parts, joint_positions, parents, node_to_joint)

    game, uvs, weights, normals, faces, cells = [], [], [], [], [], []
    image_sources = []
    for node_index, node in enumerate(gltf.json['nodes']):
        if 'mesh' not in node or node.get('skin') != 0:
            continue
        for primitive in gltf.json['meshes'][node['mesh']]['primitives']:
            if primitive.get('mode', 4) != 4:
                continue
            attributes = primitive['attributes']
            positions = gltf.accessor(attributes['POSITION'])
            vertex_normals_in = gltf.accessor(attributes['NORMAL']) if 'NORMAL' in attributes else None
            texcoords = gltf.accessor(attributes['TEXCOORD_0']) if 'TEXCOORD_0' in attributes else [(0.5, 0.5)] * len(positions)
            influences = []
            for set_index in range(2):
                key_j, key_w = 'JOINTS_%d' % set_index, 'WEIGHTS_%d' % set_index
                if key_j in attributes and key_w in attributes:
                    influences.append((gltf.accessor(attributes[key_j]), gltf.accessor(attributes[key_w])))
            if not influences:
                continue
            source, colour = material_images(gltf, primitive.get('material'))
            cell_key = ('image', source) if source is not None else ('colour', colour)
            if cell_key not in image_sources:
                image_sources.append(cell_key)
            cell = image_sources.index(cell_key)
            base = len(game)
            for v, p in enumerate(positions):
                game.append((-p[0], p[1], p[2]))
                if vertex_normals_in:
                    n = vertex_normals_in[v]
                    normals.append((-n[0], n[1], n[2]))
                u, w = texcoords[v]
                uvs.append((u, w))
                cells.append(cell)
                by_part = {}
                for joint_list, weight_list in influences:
                    for joint, weight in zip(joint_list[v], weight_list[v]):
                        if weight > 0:
                            part = parts[int(joint)]
                            by_part[part] = by_part.get(part, 0.0) + weight
                ranked = sorted(by_part.items(), key=lambda item: -item[1])[:MAX_INFLUENCES]
                total = sum(w for _, w in ranked) or 1.0
                weights.append([(part, w / total) for part, w in ranked] or [(CHEST, 1.0)])
            indices = gltf.accessor(primitive['indices']) if 'indices' in primitive else [(i,) for i in range(len(positions))]
            flat = [i[0] for i in indices]
            for t in range(0, len(flat) - 2, 3):
                a, b, c = (base + flat[t + k] for k in range(3))
                faces.append(((a, a), (b, b), (c, c)))
    if not faces:
        raise SystemExit('no skinned triangles found in %s' % args.model)
    if len(normals) != len(game):
        normals = vertex_normals(game, faces)

    texture, uvs = build_atlas(gltf, image_sources, cells, uvs)
    tables = model_formats.ModelTables(os.path.join(args.todex, 'Table.dll'))
    pol_hab = texture_formats.read_hab(os.path.join(args.todex, 'pol.HAB'))
    names = []
    for kind in args.kinds:
        tod = kind_skeleton(tables, kind)
        matrices, scale = build_matrices(tops, tod)
        emit_skinned(args, tables, pol_hab, kind, tod, os.path.basename(args.model), faces, game, uvs, normals, weights,
                     matrices, texture)
        names.append((kind, args.name or os.path.splitext(os.path.basename(args.model))[0]))
    write_names(args, names)
    mapped = {}
    for joint, part in parts.items():
        mapped.setdefault(part, []).append(gltf.json['nodes'][skin['joints'][joint]].get('name', '#%d' % joint))
    for part in sorted(mapped):
        print('  part %2d <- %s' % (part, ', '.join(mapped[part][:8]) + (' ...' if len(mapped[part]) > 8 else '')))


def invert_affine_translation(m):
    """Translation of the inverse of an affine 4x4 (the joint's position in bind space)."""
    a = [row[:3] for row in m[:3]]
    det = (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
           a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]))
    inv = [[(a[(c + 1) % 3][(r + 1) % 3] * a[(c + 2) % 3][(r + 2) % 3] - a[(c + 1) % 3][(r + 2) % 3] * a[(c + 2) % 3][(r + 1) % 3]) / det
            for c in range(3)] for r in range(3)]
    t = [m[r][3] for r in range(3)]
    return tuple(-sum(inv[r][k] * t[k] for k in range(3)) for r in range(3))


def build_atlas(gltf, sources, cells, uvs):
    """One texture for the whole model: the material images (and flat colours) side by side; UVs moved into
    their cell (v counted from the bottom, as in OBJ files)."""
    columns = 1
    while columns * columns < len(sources):
        columns *= 2
    rows = 1
    while columns * rows < len(sources):
        rows *= 2
    atlas = Image.new('RGB', (columns * ATLAS_CELL, rows * ATLAS_CELL), (128, 128, 128))
    for i, (kind, value) in enumerate(sources):
        cell = gltf.image(value).resize((ATLAS_CELL, ATLAS_CELL), Image.LANCZOS) if kind == 'image' else Image.new('RGB', (ATLAS_CELL, ATLAS_CELL), value)
        atlas.paste(cell, ((i % columns) * ATLAS_CELL, (i // columns) * ATLAS_CELL))
    moved = []
    for (u, v_top), cell in zip(uvs, cells):
        u = u - math.floor(u) if not 0 <= u <= 1 else u
        v_top = v_top - math.floor(v_top) if not 0 <= v_top <= 1 else v_top
        x = ((cell % columns) + min(max(u, 0.001), 0.999)) / columns
        y = ((cell // columns) + min(max(v_top, 0.001), 0.999)) / rows
        moved.append((x, 1.0 - y))
    return power_of_two(atlas), moved


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('model')
    parser.add_argument('todex')
    parser.add_argument('pack')
    parser.add_argument('kinds', type=int, nargs='+')
    parser.add_argument('--name')
    convert(parser.parse_args())


if __name__ == '__main__':
    main()
