"""Assembles the 94 bodies of thotd.exe from the creature parts, posed with a motion frame, and writes one OBJ
per body plus a contact sheet.

    python assemble.py <thotd.exe> <creature folder> <out folder>
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from PIL import Image, ImageDraw

import exe_view
from pose import MOTION_COUNT, apply, body_roots, joint_count, motion_frame, motion_joint_count, posed_nodes
from rbh import Rbh, Part, part_offsets
from skeletons import find_nodes

PAGE_SIZE = 256.0
THUMB = 192


def load_parts(folder):
    parts = {}
    for name in ('creat1', 'creat2'):
        rbh = Rbh(os.path.join(folder, name + '.rbh'))
        for ident, offset in part_offsets(rbh):
            if ident in (0, 0xFFFF) or ident in parts:
                continue
            try:
                parts[ident] = (rbh, Part(rbh, offset))
            except Exception:
                pass
    return parts


def load_pages(folder):
    return {int(name[4:7]): Image.open(os.path.join(folder, name)).convert('RGB')
            for name in os.listdir(folder) if name.startswith('page')}


def game_to_upright(v):
    """The game's Y axis points down: turn 180 degrees about X."""
    return (v[0], -v[1], -v[2])


def head_node(posed):
    """The head: the highest leaf under the root's taller child (the chest)."""
    by_va = {node['va']: (node, upright) for node, upright in posed}

    def subtree(va):
        node = by_va[va][0]
        return [va] + [v for child in node['children'] if child in by_va for v in subtree(child)]

    root = posed[0][0]
    if len(root['children']) < 2:
        return None
    chest = max(root['children'], key=lambda va: max(by_va[v][1][1] for v in subtree(va)) if va in by_va else -1e9)
    leaves = [c for c in by_va[chest][0]['children'] if c in by_va and not by_va[c][0]['children']]
    return max(leaves, key=lambda va: by_va[va][1][1]) if leaves else None


def turn_about(points, pivot, axis, angle):
    length = math.sqrt(sum(c * c for c in axis)) or 1.0
    x, y, z = (c / length for c in axis)
    c, s, t = math.cos(angle), math.sin(angle), 1 - math.cos(angle)
    m = [[t * x * x + c, t * x * y - s * z, t * x * z + s * y],
         [t * x * y + s * z, t * y * y + c, t * y * z - s * x],
         [t * x * z - s * y, t * y * z + s * x, t * z * z + c]]
    return [tuple(pivot[i] + sum(m[i][k] * (p[k] - pivot[k]) for k in range(3)) for i in range(3)) for p in points]


def place(root, nodes, parts, angles, order='zyx', negate_z=True):
    """[(rbh, part, world vertices, part id)]. Heads are turned half round about the neck: HOTD head meshes face
    away from what the motions give (the game turns heads at run time)."""
    posed = posed_nodes(root, nodes, angles, order, negate_z=negate_z)
    head = head_node([(node, game_to_upright(position)) for node, _, position in posed])
    placed = []
    for node, rotation, position in posed:
        if node['id'] not in parts:
            continue
        rbh, part = parts[node['id']]
        world = [game_to_upright(tuple(position[k] + r[k] for k in range(3))) for r in (apply(rotation, v) for v in part.vertices)]
        if node['va'] == head and world:
            neck = game_to_upright(position)
            centre = tuple(sum(p[k] for p in world) / len(world) for k in range(3))
            world = turn_about(world, neck, tuple(centre[k] - neck[k] for k in range(3)), math.pi)
        placed.append((rbh, part, world, node['id']))
    return placed


def write_obj(placed, path, name):
    lines = ['mtllib %s.mtl' % name]
    materials = {}
    vertex_base = 0
    uv_base = 0
    for rbh, part, world, ident in placed:
        lines.append('g part%d' % ident)
        lines += ['v %.6f %.6f %.6f' % v for v in world]
        current = None
        for indices, page, extra in part.faces:
            if page is not None:
                material = 'page%03d' % page
                materials[material] = ('map', page)
                for u, v in extra:
                    lines.append('vt %.6f %.6f' % (u / PAGE_SIZE, 1.0 - v / PAGE_SIZE))
                corners = ['%d/%d' % (vertex_base + i + 1, uv_base + k + 1) for k, i in enumerate(indices)]
                uv_base += len(indices)
            else:
                colour = part.colour(rbh, extra)
                material = 'colour_%02x%02x%02x' % tuple(int(c * 255) for c in colour)
                materials[material] = ('colour', colour)
                corners = ['%d' % (vertex_base + i + 1) for i in indices]
            if material != current:
                lines.append('usemtl ' + material)
                current = material
            lines.append('f ' + ' '.join(reversed(corners)))
        vertex_base += len(world)
    with open(path, 'w') as out:
        out.write('\n'.join(lines) + '\n')
    mtl = []
    for material, (kind, value) in sorted(materials.items()):
        mtl.append('newmtl ' + material)
        mtl += ['Kd 1 1 1', 'map_Kd ../pages/page%03d.png' % value] if kind == 'map' else ['Kd %.3f %.3f %.3f' % value]
    with open(path[:-4] + '.mtl', 'w') as out:
        out.write('\n'.join(mtl) + '\n')


def render(placed, pages, label, yaw=30, flip=True):
    image = Image.new('RGB', (THUMB, THUMB), (40, 40, 48))
    draw = ImageDraw.Draw(image)
    points = [v for _, _, world, _ in placed for v in world]
    if not points:
        return image
    low = [min(c[k] for c in points) for k in range(3)]
    high = [max(c[k] for c in points) for k in range(3)]
    centre = [(low[k] + high[k]) / 2 for k in range(3)]
    extent = max(high[k] - low[k] for k in range(3)) or 1.0
    angle = math.radians(yaw)
    up = -1 if flip else 1
    polygons = []
    for rbh, part, world, _ in placed:
        for indices, page, extra in part.faces:
            projected = []
            depth = 0.0
            for i in indices:
                x, y, z = (world[i][k] - centre[k] for k in range(3))
                rx = x * math.cos(angle) - z * math.sin(angle)
                rz = x * math.sin(angle) + z * math.cos(angle)
                scale = (THUMB - 16) / extent
                projected.append((THUMB / 2 + rx * scale, THUMB / 2 + up * y * scale))
                depth += rz
            if page is None:
                colour = tuple(int(c * 255) for c in part.colour(rbh, extra))
            else:
                page_image = pages.get(page)
                u = sum(uv[0] for uv in extra) / len(extra)
                v = sum(uv[1] for uv in extra) / len(extra)
                colour = page_image.getpixel((min(255, max(0, int(u))), min(255, max(0, int(v))))) if page_image else (255, 0, 255)
            polygons.append((depth / len(indices), projected, colour))
    for _, projected, colour in sorted(polygons, key=lambda p: -p[0]):
        draw.polygon(projected, fill=colour)
    draw.text((2, 2), label, fill=(255, 255, 0))
    return image


def render_posed(root, nodes, parts, pages, angles, order, label, negate_z=True):
    return render(place(root, nodes, parts, angles, order, negate_z), pages, label)


UPRIGHT_RATIO = 1.8


def extents(placed):
    points = [v for _, _, world, _ in placed for v in world]
    if not points:
        return (0.0, 0.0, 0.0)
    return tuple(max(p[k] for p in points) - min(p[k] for p in points) for k in range(3))


def standing_pose(root, nodes, parts, joints, motions):
    """A standing frame: among the matching motions' first frames that are upright (height >= 1.8 x width
    and depth) and within 7 % of their median height (arms not raised, not crouching), the narrowest one. Returns (motion, placed)."""
    candidates = []
    for motion in motions:
        _, angles = motion_frame(motion, 0, joints)
        placed = place(root, nodes, parts, angles)
        width, height, depth = extents(placed)
        candidates.append((height / (max(width, depth) + 1e-6), height, width + depth, motion, placed))
    if not candidates:
        return None, place(root, nodes, parts, [(0, 0, 0)] * joints)
    upright = [c for c in candidates if c[0] >= UPRIGHT_RATIO] or candidates
    typical = sorted(c[1] for c in upright)[len(upright) // 2]
    standing = [c for c in upright if abs(c[1] - typical) <= 0.07 * typical] or upright
    best = min(standing, key=lambda c: c[2])
    return best[3], best[4]


def main():
    exe_path, creature_folder, out = sys.argv[1:4]
    exe_view.load(exe_path)
    parts = load_parts(creature_folder)
    nodes = find_nodes()
    pages = load_pages(os.path.join(out, 'pages'))
    folder = os.path.join(out, 'bodies')
    os.makedirs(folder, exist_ok=True)
    motions_by_joints = {}
    for motion in range(MOTION_COUNT):
        motions_by_joints.setdefault(motion_joint_count(motion), []).append(motion)
    thumbs = []
    index = []
    for body, root in enumerate(body_roots()):
        joints = joint_count(body)
        motion, placed = standing_pose(root, nodes, parts, joints, motions_by_joints.get(joints, []))
        name = 'body%02d' % body
        write_obj(placed, os.path.join(folder, name + '.obj'), name)
        thumbs.append(render(placed, pages, '%d j%d m%s' % (body, joints, motion)))
        index.append('%s\t%d joints\tmotion %s\tparts %s' % (name, joints, motion, ' '.join(str(p[3]) for p in placed)))
    with open(os.path.join(out, 'bodies.tsv'), 'w') as listing:
        listing.write('\n'.join(index) + '\n')
    columns = 10
    sheet = Image.new('RGB', (columns * THUMB, math.ceil(len(thumbs) / columns) * THUMB))
    for i, thumb in enumerate(thumbs):
        sheet.paste(thumb, ((i % columns) * THUMB, (i // columns) * THUMB))
    sheet.save(os.path.join(out, 'bodies_sheet.png'))
    print('%d bodies' % len(thumbs))


if __name__ == '__main__':
    main()
