"""Textured front/side/back render of an OBJ with MTL map_Kd textures (checks exported bodies).

    python render_obj.py <model.obj> <out.png> [size]
"""
import math
import os
import sys

from PIL import Image, ImageDraw


def load(path):
    folder = os.path.dirname(path)
    positions, uvs, triangles, materials = [], [], [], {}
    current = None
    for line in open(path):
        fields = line.split()
        if not fields:
            continue
        if fields[0] == 'mtllib':
            name = None
            for mtl_line in open(os.path.join(folder, fields[1])):
                parts = mtl_line.split()
                if parts and parts[0] == 'newmtl':
                    name = parts[1]
                elif parts and parts[0] == 'map_Kd':
                    materials[name] = Image.open(os.path.join(folder, parts[1])).convert('RGB')
                elif parts and parts[0] == 'Kd' and name not in materials:
                    materials[name] = tuple(int(float(c) * 255) for c in parts[1:4])
        elif fields[0] == 'v':
            positions.append(tuple(map(float, fields[1:4])))
        elif fields[0] == 'vt':
            uvs.append(tuple(map(float, fields[1:3])))
        elif fields[0] == 'usemtl':
            current = fields[1]
        elif fields[0] == 'f':
            corners = [tuple(int(i) - 1 if i else -1 for i in (c.split('/') + [''])[:2]) for c in fields[1:]]
            for k in range(1, len(corners) - 1):
                triangles.append(([corners[0], corners[k], corners[k + 1]], current))
    return positions, uvs, triangles, materials


def render(path, out, size=320):
    positions, uvs, triangles, materials = load(path)
    image = Image.new('RGB', (size * 3, size), (40, 40, 48))
    pixels = image.load()
    low_y = min(p[1] for p in positions)
    high_y = max(p[1] for p in positions)
    centre_x = (min(p[0] for p in positions) + max(p[0] for p in positions)) / 2
    centre_z = (min(p[2] for p in positions) + max(p[2] for p in positions)) / 2
    scale = (size - 20) / max(high_y - low_y, 1e-6)
    for view, yaw in enumerate((0.0, math.pi / 2, math.pi)):
        depth = [[-1e30] * size for _ in range(size)]
        for corners, material in triangles:
            screen = []
            for vertex, _ in corners:
                x, y, z = positions[vertex]
                x, z = x - centre_x, z - centre_z
                rx = x * math.cos(yaw) - z * math.sin(yaw)
                rz = x * math.sin(yaw) + z * math.cos(yaw)
                screen.append((size / 2 + rx * scale, size - 10 - (y - low_y) * scale, rz))
            (x0, y0, z0), (x1, y1, z1), (x2, y2, z2) = screen
            area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
            if abs(area) < 1e-9:
                continue
            texture = materials.get(material)
            for py in range(max(0, int(min(y0, y1, y2))), min(size - 1, int(max(y0, y1, y2))) + 1):
                for px in range(max(0, int(min(x0, x1, x2))), min(size - 1, int(max(x0, x1, x2))) + 1):
                    w0 = ((x1 - px) * (y2 - py) - (x2 - px) * (y1 - py)) / area
                    w1 = ((x2 - px) * (y0 - py) - (x0 - px) * (y2 - py)) / area
                    w2 = 1 - w0 - w1
                    if w0 < 0 or w1 < 0 or w2 < 0:
                        continue
                    z = w0 * z0 + w1 * z1 + w2 * z2
                    if z <= depth[py][px]:
                        continue
                    depth[py][px] = z
                    if isinstance(texture, Image.Image) and corners[0][1] >= 0:
                        u = sum(w * uvs[c[1]][0] for w, c in zip((w0, w1, w2), corners))
                        v = sum(w * uvs[c[1]][1] for w, c in zip((w0, w1, w2), corners))
                        colour = texture.getpixel((min(texture.width - 1, max(0, int(u * texture.width))),
                                                   min(texture.height - 1, max(0, int((1 - v) * texture.height)))))
                    else:
                        colour = texture if isinstance(texture, tuple) else (200, 0, 200)
                    pixels[view * size + px, py] = colour
    ImageDraw.Draw(image).text((4, 4), os.path.basename(path) + ': camera at +z, +x, -z', fill=(255, 255, 0))
    image.save(out)


if __name__ == '__main__':
    render(sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 320)
