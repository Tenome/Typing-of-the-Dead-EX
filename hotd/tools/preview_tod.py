"""Draws a converted model pack on its TOD skeleton (front and side, rest pose) and reports face winding.

    python preview_tod.py <TODEX folder> <pack> <kind> <out.png>
"""
import glob
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'mod', 'tools'))

from PIL import Image, ImageDraw

import character_kinds
import model_formats

SIZE = 360


def read_obj(path):
    positions, uvs, faces = [], [], []
    for line in open(path):
        fields = line.split()
        if not fields:
            continue
        if fields[0] == 'v':
            x, y, z = map(float, fields[1:4])
            positions.append((x, y, -z))
        elif fields[0] == 'vt':
            uvs.append(tuple(map(float, fields[1:3])))
        elif fields[0] == 'f':
            faces.append([tuple(int(i) - 1 for i in corner.split('/')[:2]) for corner in fields[1:]])
    return positions, uvs, faces


def main():
    todex, pack, kind, out = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
    tables = model_formats.ModelTables(os.path.join(todex, 'Table.dll'))
    skeleton = {model: position for part, model, position in character_kinds.body_parts(tables, kind) if model}
    bank_folders = {os.path.basename(os.path.dirname(path)) for path in glob.glob(os.path.join(todex, 'models', pack, '*', '*.obj'))
                    if int(os.path.basename(path)[:-4]) in skeleton}
    atlas_path = glob.glob(os.path.join(todex, 'textures', pack, sorted(bank_folders)[0], '*.png'))[0]
    atlas = Image.open(atlas_path).convert('RGB')
    triangles = []
    outward = total = 0
    for path in glob.glob(os.path.join(todex, 'models', pack, '*', '*.obj')):
        model = int(os.path.basename(path)[:-4])
        if model not in skeleton:
            continue
        origin = skeleton[model]
        positions, uvs, faces = read_obj(path)
        centre = [sum(p[k] for p in positions) / len(positions) for k in range(3)]
        for face in faces:
            a, b, c = (positions[i] for i, _ in face)
            ab = [b[k] - a[k] for k in range(3)]
            ac = [c[k] - a[k] for k in range(3)]
            normal = (ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0])
            middle = [(a[k] + b[k] + c[k]) / 3 for k in range(3)]
            # OBJ z was negated on reading, so outward in OBJ space means inward here
            outward += sum(normal[k] * (middle[k] - centre[k]) for k in range(3)) < 0
            total += 1
            corner_uvs = [uvs[t] for _, t in face]
            triangles.append((([tuple(origin[k] + p[k] for k in range(3)) for p in (a, b, c)], corner_uvs), None))
    image = Image.new('RGB', (SIZE * 2, SIZE), (40, 40, 48))
    pixels = image.load()
    texture = atlas.load()
    points = [p for t, _ in triangles for p in t[0]]
    low = [min(p[k] for p in points) for k in range(3)]
    high = [max(p[k] for p in points) for k in range(3)]
    scale = (SIZE - 20) / max(high[1] - low[1], 1e-6)
    for view, yaw in enumerate((0.0, math.pi / 2)):
        depth = [[-1e30] * SIZE for _ in range(SIZE)]
        for (tri, uv), _ in triangles:
            screen = []
            for x, y, z in tri:
                rx = x * math.cos(yaw) + z * math.sin(yaw)
                rz = -x * math.sin(yaw) + z * math.cos(yaw)
                screen.append((SIZE / 2 + rx * scale, SIZE - 10 - (y - low[1]) * scale, rz))
            (x0, y0, z0), (x1, y1, z1), (x2, y2, z2) = screen
            area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
            if abs(area) < 1e-9:
                continue
            for py in range(max(0, int(min(y0, y1, y2))), min(SIZE - 1, int(max(y0, y1, y2))) + 1):
                for px in range(max(0, int(min(x0, x1, x2))), min(SIZE - 1, int(max(x0, x1, x2))) + 1):
                    cx, cy = px + 0.5, py + 0.5
                    w0 = ((x1 - cx) * (y2 - cy) - (x2 - cx) * (y1 - cy)) / area
                    w1 = ((x2 - cx) * (y0 - cy) - (x0 - cx) * (y2 - cy)) / area
                    w2 = 1 - w0 - w1
                    if w0 < 0 or w1 < 0 or w2 < 0:
                        continue
                    z = w0 * z0 + w1 * z1 + w2 * z2
                    if z <= depth[py][px]:
                        continue
                    depth[py][px] = z
                    u = w0 * uv[0][0] + w1 * uv[1][0] + w2 * uv[2][0]
                    v = w0 * uv[0][1] + w1 * uv[1][1] + w2 * uv[2][1]
                    tx = min(atlas.width - 1, max(0, int(u * atlas.width)))
                    ty = min(atlas.height - 1, max(0, int(v * atlas.height)))
                    pixels[view * SIZE + px, py] = texture[tx, ty]
    draw = ImageDraw.Draw(image)
    draw.text((4, 4), '%s on kind %d: front (camera +z) and side; outward faces %.0f%%' % (pack, kind, 100.0 * outward / max(total, 1)),
              fill=(255, 255, 0))
    image.save(out)


main()
