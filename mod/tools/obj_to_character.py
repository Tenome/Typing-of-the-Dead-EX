"""Turns a single-mesh character model (one OBJ, one texture, standing in a T-pose or A-pose, y up, facing +z)
into a Typing of the Dead EX model pack for a character kind, so the game's animations move it.

    python obj_to_character.py <model.obj> <TODEX folder> <pack> <kind> [<kind> ...] [--name "Shown name"]

The mesh is cut into the game's 15 body parts (head, chest, hips, upper arms, forearms, hands, thighs, shins,
feet) from its proportions; each piece is turned onto the game's rest pose (arms down) and stretched to the
game's bone lengths. Writes models\\<pack>\\<bank>\\<model id>.obj, textures\\<pack>\\<bank>\\<slot>_id<id>.png and
models\\<pack>\\characters.tsv. Then tick the pack under "Model packs" in todmod_config.exe; the Characters tab
shows it, and "Play as this" plays as it.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
for folder in (os.path.join(HERE, 'hotd'), os.path.join(HERE, '..', '..', 'hotd', 'tools')):
    if os.path.isdir(folder):
        sys.path.insert(0, folder)
sys.path.insert(0, HERE)

from PIL import Image

import character_kinds
import model_formats
import texture_formats
from to_tod import (atlas_texture_slot, bank_model_boxes, part_variants, rotation_between, scale_matrix, sub,
                    write_hidden)
from pose import apply, multiply

HEAD, CHEST, HIPS = 2, 1, 9
ARMS = {-1: (3, 4, 5), 1: (6, 7, 8)}
LEGS = {-1: (10, 11, 12), 1: (13, 14, 15)}
CHILD = {3: 4, 4: 5, 6: 7, 7: 8, 10: 11, 11: 12, 13: 14, 14: 15}
ARM_ROW_WIDTH = 0.55
ELBOW_ALONG_ARM, WRIST_ALONG_ARM = 0.40, 0.76
HEAD_HEIGHT, ANKLE_HEIGHT, HIP_ABOVE_CROTCH, DEFAULT_CROTCH = 0.13, 0.045, 0.035, 0.47
ROW = 0.01
BLEND = 0.03
NEWLINE = chr(10)


def read_obj(path):
    vertices, uvs, faces, texture = [], [], [], None
    folder = os.path.dirname(path)
    for line in open(path, encoding='utf-8', errors='replace'):
        parts = line.split()
        if not parts:
            continue
        if parts[0] == 'v':
            vertices.append(tuple(float(c) for c in parts[1:4]))
        elif parts[0] == 'vt':
            uvs.append((float(parts[1]), float(parts[2]) if len(parts) > 2 else 0.0))
        elif parts[0] == 'f':
            corners = []
            for corner in parts[1:]:
                fields = corner.split('/')
                vertex = int(fields[0])
                uv = int(fields[1]) if len(fields) > 1 and fields[1] else 0
                corners.append((vertex - 1 if vertex > 0 else len(vertices) + vertex,
                                uv - 1 if uv > 0 else (len(uvs) + uv if uv else None)))
            for k in range(1, len(corners) - 1):
                faces.append((corners[0], corners[k], corners[k + 1]))
        elif parts[0] == 'mtllib' and texture is None:
            mtl = os.path.join(folder, line.split(None, 1)[1].strip())
            if os.path.exists(mtl):
                for mline in open(mtl, encoding='utf-8', errors='replace'):
                    if mline.strip().lower().startswith('map_kd'):
                        texture = os.path.join(folder, mline.split(None, 1)[1].strip())
    return vertices, uvs, faces, texture


def rows(points, low, high):
    return [p for p in points if low <= p[1] < high]


def measure(points, triangles):
    """Joint positions of the source mesh (game orientation: x mirrored, so the character's left is -x)."""
    floor = min(p[1] for p in points)
    top = max(p[1] for p in points)
    height = top - floor
    reach = max(abs(p[0]) for p in points)
    arm_rows = [y for y in frange(floor, top, ROW) if max((abs(p[0]) for p in rows(points, y, y + ROW)), default=0) > ARM_ROW_WIDTH * reach]
    if not arm_rows:
        raise SystemExit('no outstretched arms found: the model must stand in a T-pose or A-pose')
    shoulder_y = (min(arm_rows) + max(arm_rows)) / 2
    torso_rows = rows(points, shoulder_y - 0.12 * height, shoulder_y - 0.06 * height)
    shoulder_x = max(abs(p[0]) for p in torso_rows) if torso_rows else 0.1 * height
    crotch_y = floor + DEFAULT_CROTCH * height
    for y in frange(floor + 0.3 * height, floor + 0.6 * height, ROW):
        if any(min(p[1] for p in tri) <= y <= max(p[1] for p in tri) and min(p[0] for p in tri) <= 0 <= max(p[0] for p in tri)
               for tri in triangles):
            crotch_y = y
            break
    legs = rows(points, floor, crotch_y)
    leg_x = sum(abs(p[0]) for p in legs) / len(legs) if legs else 0.05 * height
    hip_y = crotch_y + HIP_ABOVE_CROTCH * height
    ankle_y = floor + ANKLE_HEIGHT * height
    neck_y = top - HEAD_HEIGHT * height
    arm_length = reach - shoulder_x
    joints = {'shoulder_y': shoulder_y, 'shoulder_x': shoulder_x, 'reach': reach,
              'elbow_x': shoulder_x + ELBOW_ALONG_ARM * arm_length, 'wrist_x': shoulder_x + WRIST_ALONG_ARM * arm_length,
              'neck_y': neck_y, 'waist_y': (shoulder_y + hip_y) / 2 - 0.05 * height, 'crotch_y': crotch_y,
              'hip_y': hip_y, 'knee_y': (hip_y + ankle_y) / 2, 'ankle_y': ankle_y, 'leg_x': leg_x,
              'floor': floor, 'top': top}
    return joints


def frange(start, stop, step):
    value = start
    while value < stop:
        yield value
        value += step


def classify(point, j):
    x, y, _ = point
    side = -1 if x < 0 else 1
    if y > j['neck_y'] and abs(x) < j['shoulder_x']:
        return HEAD
    if abs(x) > j['shoulder_x'] and y > j['waist_y']:
        if abs(x) < j['elbow_x']:
            return ARMS[side][0]
        return ARMS[side][1] if abs(x) < j['wrist_x'] else ARMS[side][2]
    if y < j['crotch_y']:
        if y > j['knee_y']:
            return LEGS[side][0]
        return LEGS[side][1] if y > j['ankle_y'] else LEGS[side][2]
    return CHEST if y > j['waist_y'] else HIPS


def source_joints(j):
    """{part: (joint position, child joint position or None)} in the source mesh."""
    out = {HEAD: ((0, j['neck_y'], 0), None), CHEST: ((0, j['waist_y'], 0), None), HIPS: ((0, j['waist_y'], 0), None)}
    for side, (upper, fore, hand) in ARMS.items():
        y = j['shoulder_y']
        out[upper] = ((side * j['shoulder_x'], y, 0), (side * j['elbow_x'], y, 0))
        out[fore] = ((side * j['elbow_x'], y, 0), (side * j['wrist_x'], y, 0))
        out[hand] = ((side * j['wrist_x'], y, 0), None)
    for side, (thigh, shin, foot) in LEGS.items():
        x = side * j['leg_x']
        out[thigh] = ((x, j['hip_y'], 0), (x, j['knee_y'], 0))
        out[shin] = ((x, j['knee_y'], 0), (x, j['ankle_y'], 0))
        out[foot] = ((x, j['ankle_y'], 0), None)
    return out


def part_matrices(j, tod, scale):
    """{part: (matrix, source pivot)}: source mesh positions -> game part-local positions."""
    joints = source_joints(j)
    uniform = [[scale, 0, 0], [0, scale, 0], [0, 0, scale]]
    matrices, rotations = {}, {}
    for part, (joint, child) in joints.items():
        if part not in tod or child is None or CHILD.get(part) not in tod:
            continue
        source_bone = sub(child, joint)
        target_bone = sub(tod[CHILD[part]][1], tod[part][1])
        source_length = sum(c * c for c in source_bone) ** 0.5
        target_length = sum(c * c for c in target_bone) ** 0.5
        rotations[part] = rotation_between(source_bone, target_bone)
        stretch = scale_matrix(target_bone, target_length / source_length, scale)
        matrices[part] = (multiply(stretch, rotations[part]), joint)
    for part, (joint, child) in joints.items():
        if part not in tod or part in matrices:
            continue
        parent = {5: 4, 8: 7, 12: 11, 15: 14}.get(part)
        if parent in rotations:
            matrices[part] = ([[c * scale for c in row] for row in rotations[parent]], joint)
        else:
            matrices[part] = (uniform, joint)
    return matrices


def torso_offsets(j, tod, scale):
    """Chest, hips and head keep the source's shape; put their joints where the game's nodes are relative to the
    shoulders, so the arms meet the torso."""
    shoulder_gap = tod[3][1][1] - tod[CHEST][1][1]
    return {CHEST: (0, j['shoulder_y'] - shoulder_gap / scale, 0),
            HIPS: (0, j['shoulder_y'] - (tod[3][1][1] - tod[HIPS][1][1]) / scale, 0),
            HEAD: (0, j['shoulder_y'] - (tod[3][1][1] - tod[HEAD][1][1]) / scale, 0)}


def blend_joints(j):
    """(parent part, child part, joint point, axis towards the child, side) for every joint of the cut."""
    joints = [(CHEST, HEAD, (0, j['neck_y'], 0), (0, 1, 0), 0), (HIPS, CHEST, (0, j['waist_y'], 0), (0, 1, 0), 0)]
    for side, (upper, fore, hand) in ARMS.items():
        joints += [(CHEST, upper, (side * j['shoulder_x'], 0, 0), (side, 0, 0), side),
                   (upper, fore, (side * j['elbow_x'], 0, 0), (side, 0, 0), side),
                   (fore, hand, (side * j['wrist_x'], 0, 0), (side, 0, 0), side)]
    for side, (thigh, shin, foot) in LEGS.items():
        joints += [(HIPS, thigh, (0, j['crotch_y'], 0), (0, -1, 0), side),
                   (thigh, shin, (0, j['knee_y'], 0), (0, -1, 0), side),
                   (shin, foot, (0, j['ankle_y'], 0), (0, -1, 0), side)]
    return joints


def vertex_weights(point, j, joints, width):
    """[(part, weight)]: the part the point belongs to, blended with its neighbour within width of a joint."""
    primary = classify(point, j)
    best = None
    for parent, child, joint, axis, side in joints:
        if primary not in (parent, child) or (side and (point[0] < 0) != (side < 0)):
            continue
        along = sum((point[k] - joint[k]) * axis[k] for k in range(3))
        if abs(along) < width and (best is None or abs(along) < abs(best[2])):
            best = (parent, child, along)
    if best is None:
        return [(primary, 1.0)]
    parent, child, along = best
    s = min(max(0.5 + along / (2 * width), 0.0), 1.0)
    child_weight = s * s * (3 - 2 * s)
    return [(part, w) for part, w in ((parent, 1 - child_weight), (child, child_weight)) if w > 0.001]


def vertex_normals(game, faces):
    normals = [[0.0, 0.0, 0.0] for _ in game]
    for face in faces:
        a, b, c = (game[v] for v, _ in face)
        u, w = sub(b, a), sub(c, a)
        n = (u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0])
        for v, _ in face:
            for k in range(3):
                normals[v][k] += n[k]
    out = []
    for n in normals:
        length = sum(c * c for c in n) ** 0.5 or 1.0
        out.append(tuple(c / length for c in n))
    return out


def unit(v):
    length = sum(c * c for c in v) ** 0.5 or 1.0
    return tuple(c / length for c in v)


def write_skinned(source_name, folder, kind, tod, faces, game, uvs, normals, weights, matrices, variants, hidden, boxes, slot):
    """One carrier model (the chest) with the whole mesh, hidden stand-ins for every other part model, and a
    .skin file: per face corner the parts it follows, with weights and part-local position and normal."""
    carrier = tod[CHEST][0]
    chest_origin = tod[CHEST][1]
    rest = []
    for v, point in enumerate(game):
        position = [0.0, 0.0, 0.0]
        for part, w in weights[v]:
            matrix, pivot = matrices[part]
            local = apply(matrix, sub(point, pivot))
            offset = sub(tod[part][1], chest_origin)
            for k in range(3):
                position[k] += w * (local[k] + offset[k])
        rest.append(tuple(position))
    write_piece(os.path.join(folder, '%d.obj' % carrier), '# %s -> kind %d, skinned mesh carried by the chest' %
                (source_name, kind), faces, game, uvs, None, None, boxes[carrier][2], slot, rest)
    part_models = {}
    for part in tod:
        for model_id in [tod[part][0]] + variants.get(tod[part][0], []):
            part_models[model_id] = part
    for model_id in sorted(set(part_models) | set(hidden)):
        if model_id != carrier and model_id in boxes:
            write_hidden(os.path.join(folder, '%d.obj' % model_id), '# kind %d part drawn by the skinned mesh' % kind,
                         boxes[model_id][2], slot)
    lines = ['todmod skin 1', 'kind %d' % kind, 'carrier %d' % carrier]
    lines += ['model %d %d' % (model_id, part) for model_id, part in sorted(part_models.items())]
    lines.append('corners %d' % (3 * len(faces)))
    for face in faces:
        for v, _ in face:
            fields = ['c', str(len(weights[v]))]
            for part, w in weights[v]:
                matrix, pivot = matrices[part]
                position = apply(matrix, sub(game[v], pivot))
                normal = unit(apply(matrix, normals[v]))
                fields += [str(part), '%.5f' % w] + ['%.5f' % c for c in position] + ['%.5f' % c for c in normal]
            lines.append(' '.join(fields))
    with open(os.path.join(folder, '%d.skin' % carrier), 'w', newline=NEWLINE) as out:
        out.write(NEWLINE.join(lines) + NEWLINE)
    blended = sum(1 for w in weights if len(w) > 1)
    return len(part_models), blended


def kind_skeleton(tables, kind):
    tod = {part: (model, position) for part, model, position in character_kinds.body_parts(tables, kind) if part > 0}
    if not all(part in tod for part in (HEAD, CHEST, HIPS, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15)):
        raise SystemExit('kind %d is not a 16-part humanoid' % kind)
    return tod


def kind_bank(tables, tod, kind):
    banks = {tables.bank_name(tables.u16(tables.exports['PbkTbl'] + 2 * tod[p][0]))[:-4] for p in tod}
    if len(banks) != 1:
        raise SystemExit('kind %d uses several banks (%s)' % (kind, ', '.join(sorted(banks))))
    return banks.pop()


def power_of_two(texture):
    side = 1
    while side < max(texture.size):
        side *= 2
    return texture.convert('RGB').resize((side, side), Image.LANCZOS)


def save_texture(args, tables, bank, slot, texture):
    texture_folder = os.path.join(args.todex, 'textures', args.pack, bank)
    os.makedirs(texture_folder, exist_ok=True)
    texture_id = tables.bank_textures(bank + '.bin')[slot][5]
    texture.save(os.path.join(texture_folder, '%03d_id%d.png' % (slot, texture_id)))


def emit_skinned(args, tables, pol_hab, kind, tod, source_name, faces, game, uvs, normals, weights, matrices, texture):
    """Writes the skinned carrier, stand-ins, .skin file and texture of one kind; returns the bank."""
    bank = kind_bank(tables, tod, kind)
    slot = atlas_texture_slot(tables, bank)
    folder = os.path.join(args.todex, 'models', args.pack, bank)
    os.makedirs(folder, exist_ok=True)
    for stale in os.listdir(folder):
        if stale.endswith('.skin'):
            os.remove(os.path.join(folder, stale))
    boxes = bank_model_boxes(pol_hab, tables, bank)
    variants, hidden = part_variants(boxes, [tod[p][0] for p in tod if tod[p][0] in boxes])
    models, blended = write_skinned(source_name, folder, kind, tod, faces, game, uvs, normals, weights, matrices,
                                    variants, hidden, boxes, slot)
    save_texture(args, tables, bank, slot, texture)
    print('%s: %s -> kind %d (%s), skinned: %d part models follow one mesh, %d of %d vertices blend across joints, '
          'texture slot %d' % (args.pack, source_name, kind, bank, models, blended, len(game), slot))
    return bank


def write_names(args, names):
    with open(os.path.join(args.todex, 'models', args.pack, 'characters.tsv'), 'w', encoding='utf-8', newline=NEWLINE) as out:
        out.write('# kind' + chr(9) + 'name shown on the Characters tab' + NEWLINE)
        out.writelines('%d%s%s%s' % (kind, chr(9), name, NEWLINE) for kind, name in names)


def convert(args):
    vertices, uvs, faces, texture_path = read_obj(args.obj)
    if not faces:
        raise SystemExit('no faces in %s' % args.obj)
    game = [(-x, y, z) for x, y, z in vertices]
    j = measure(game, [[game[v] for v, _ in face] for face in faces])
    tables = model_formats.ModelTables(os.path.join(args.todex, 'Table.dll'))
    pol_hab = texture_formats.read_hab(os.path.join(args.todex, 'pol.HAB'))
    texture = Image.open(texture_path).convert('RGB') if texture_path and os.path.exists(texture_path) else Image.new('RGB', (8, 8), (180, 180, 180))
    side = 1
    while side < max(texture.size):
        side *= 2
    texture = texture.resize((side, side), Image.LANCZOS)
    names = []
    for kind in args.kinds:
        tod = {part: (model, position) for part, model, position in character_kinds.body_parts(tables, kind) if part > 0}
        if not all(part in tod for part in (HEAD, CHEST, HIPS, 3, 6, 10, 13)):
            raise SystemExit('kind %d is not a 16-part humanoid' % kind)
        scale = (tod[HEAD][1][1] - tod[12][1][1]) / (j['neck_y'] - j['ankle_y'])
        matrices = part_matrices(j, tod, scale)
        for part, pivot in torso_offsets(j, tod, scale).items():
            matrices[part] = (matrices[part][0], pivot)
        pieces = {}
        for face in faces:
            centre = tuple(sum(game[v][k] for v, _ in face) / 3 for k in range(3))
            pieces.setdefault(classify(centre, j), []).append(face)
        banks = {tables.bank_name(tables.u16(tables.exports['PbkTbl'] + 2 * tod[p][0]))[:-4] for p in tod}
        if len(banks) != 1:
            raise SystemExit('kind %d uses several banks (%s)' % (kind, ', '.join(sorted(banks))))
        bank = banks.pop()
        slot = atlas_texture_slot(tables, bank)
        folder = os.path.join(args.todex, 'models', args.pack, bank)
        os.makedirs(folder, exist_ok=True)
        boxes = bank_model_boxes(pol_hab, tables, bank)
        base_models = [tod[p][0] for p in pieces if p in tod]
        missing = [tod[p][0] for p in tod if p not in pieces and tod[p][0] in boxes]
        variants, hidden = part_variants(boxes, base_models + missing)
        if args.skinned:
            height = j['top'] - j['floor']
            joints = blend_joints(j)
            weights = [vertex_weights(point, j, joints, BLEND * height) for point in game]
            emit_skinned(args, tables, pol_hab, kind, tod, os.path.basename(args.obj), faces, game, uvs,
                         vertex_normals(game, faces), weights, matrices, texture)
            names.append((kind, args.name or os.path.splitext(os.path.basename(args.obj))[0]))
            continue
        written = 0
        for part, part_faces in ([] if args.skinned else sorted(pieces.items())):
            if part not in tod:
                continue
            matrix, pivot = matrices[part]
            for model_id in [tod[part][0]] + variants[tod[part][0]]:
                chunk = boxes[model_id][2] if model_id in boxes else 0
                write_piece(os.path.join(folder, '%d.obj' % model_id), '# %s -> kind %d part %d' % (os.path.basename(args.obj), kind, part),
                            part_faces, game, uvs, matrix, pivot, chunk, slot)
                written += 1
        for model_id in ([] if args.skinned else sorted(set(hidden) | {m for base in missing for m in [base] + variants[base]})):
            write_hidden(os.path.join(folder, '%d.obj' % model_id), '# kind %d piece not in the source model (hidden)' % kind,
                         boxes[model_id][2], slot)
        texture_folder = os.path.join(args.todex, 'textures', args.pack, bank)
        os.makedirs(texture_folder, exist_ok=True)
        texture_id = tables.bank_textures(bank + '.bin')[slot][5]
        texture.save(os.path.join(texture_folder, '%03d_id%d.png' % (slot, texture_id)))
        names.append((kind, args.name or os.path.splitext(os.path.basename(args.obj))[0]))
        if not args.skinned:
            print('%s: %s -> kind %d (%s), %d part models, texture slot %d, scale %.2f' %
                  (args.pack, os.path.basename(args.obj), kind, bank, written, slot, scale))
    with open(os.path.join(args.todex, 'models', args.pack, 'characters.tsv'), 'w', encoding='utf-8', newline='\n') as out:
        out.write('# kind\tname shown on the Characters tab\n')
        out.writelines('%d\t%s\n' % item for item in names)


def write_piece(path, header, faces, game, uvs, matrix, pivot, chunk, slot, positions=None):
    lines = [header, 'usemtl m%d_t%d' % (chunk, slot)]
    used = sorted({v for face in faces for v, _ in face})
    index = {v: i + 1 for i, v in enumerate(used)}
    for v in used:
        x, y, z = positions[v] if positions else apply(matrix, sub(game[v], pivot))
        lines.append('v %.6f %.6f %.6f' % (x, y, -z))
    uv_index = {}
    uv_lines, face_lines = [], []
    for face in faces:
        corners = []
        for v, t in face:
            u, w = uvs[t] if t is not None and t < len(uvs) else (0.0, 0.0)
            key = (round(u, 6), round(1.0 - w, 6))
            if key not in uv_index:
                uv_index[key] = len(uv_index) + 1
                uv_lines.append('vt %.6f %.6f' % key)
            corners.append('%d/%d' % (index[v], uv_index[key]))
        face_lines.append('f %s %s %s' % tuple(corners))
    with open(path, 'w', newline='\n') as out:
        out.write('\n'.join(lines + uv_lines + face_lines) + '\n')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('obj')
    parser.add_argument('todex')
    parser.add_argument('pack')
    parser.add_argument('kinds', type=int, nargs='+')
    parser.add_argument('--name')
    parser.add_argument('--rigid', dest='skinned', action='store_false',
                        help='cut into rigid body parts instead of one skinned mesh (needs no todmod support)')
    convert(parser.parse_args())


if __name__ == '__main__':
    main()
