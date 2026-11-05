"""Exports every part of a House of the Dead RBH file as OBJ + MTL (textures from the extracted VMC pages),
and draws a contact sheet of all parts.

    python export_parts.py <file.rbh> <texture.vmc> <out folder>
"""
import math
import os.path as _path
import sys as _sys
_sys.path.insert(0, _path.dirname(_path.abspath(__file__)))
import os
import sys

from PIL import Image, ImageDraw

from rbh import Rbh, Part, part_offsets
from vmc import read_pages, page_rgba

PAGE_SIZE = 256.0
THUMB = 128


def save_pages(vmc_path, folder):
    pages = read_pages(vmc_path)
    images = {}
    os.makedirs(folder, exist_ok=True)
    for number, (width, height, pixels, _kind) in pages.items():
        image = Image.frombytes('RGBA', (width, height), page_rgba(width, height, pixels)).convert('RGB')
        image.save(os.path.join(folder, 'page%03d.png' % number))
        images[number] = image
    return images


def write_obj(part, rbh, path, name):
    lines = ['mtllib %s.mtl' % name, 'o %s' % name]
    lines += ['v %.6f %.6f %.6f' % v for v in part.vertices]
    materials = {}
    uv_count = 0
    body = []
    for indices, page, extra in part.faces:
        if page is not None:
            material = 'page%03d' % page
            materials[material] = ('map', page)
            for u, v in extra:
                lines.append('vt %.6f %.6f' % (u / PAGE_SIZE, 1.0 - v / PAGE_SIZE))
            corners = ['%d/%d' % (index + 1, uv_count + k + 1) for k, index in enumerate(indices)]
            uv_count += len(indices)
        else:
            material = 'colour%d' % extra
            materials[material] = ('colour', part.colour(rbh, extra))
            corners = ['%d' % (index + 1) for index in indices]
        body.append((material, 'f ' + ' '.join(reversed(corners))))
    current = None
    for material, face in body:
        if material != current:
            lines.append('usemtl ' + material)
            current = material
        lines.append(face)
    with open(path, 'w') as out:
        out.write('\n'.join(lines) + '\n')
    mtl = []
    for material, (kind, value) in sorted(materials.items()):
        mtl.append('newmtl ' + material)
        if kind == 'map':
            mtl += ['Kd 1 1 1', 'map_Kd ../pages/page%03d.png' % value]
        else:
            mtl.append('Kd %.3f %.3f %.3f' % value)
    with open(path[:-4] + '.mtl', 'w') as out:
        out.write('\n'.join(mtl) + '\n')


def face_colour(part, rbh, face, pages):
    indices, page, extra = face
    if page is None:
        return tuple(int(c * 255) for c in part.colour(rbh, extra))
    image = pages.get(page)
    if image is None:
        return (255, 0, 255)
    u = sum(uv[0] for uv in extra) / len(extra)
    v = sum(uv[1] for uv in extra) / len(extra)
    return image.getpixel((min(255, max(0, int(u))), min(255, max(0, int(v)))))


def thumbnail(part, rbh, pages, label):
    image = Image.new('RGB', (THUMB, THUMB), (40, 40, 48))
    draw = ImageDraw.Draw(image)
    if part.vertices:
        xs = [v[0] for v in part.vertices]
        ys = [v[1] for v in part.vertices]
        zs = [v[2] for v in part.vertices]
        centre = ((max(xs) + min(xs)) / 2, (max(ys) + min(ys)) / 2, (max(zs) + min(zs)) / 2)
        extent = max(max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs), 1e-6)
        angle = math.radians(30)

        def project(v):
            x, y, z = (v[0] - centre[0], v[1] - centre[1], v[2] - centre[2])
            rx = x * math.cos(angle) - z * math.sin(angle)
            rz = x * math.sin(angle) + z * math.cos(angle)
            scale = (THUMB - 16) / extent
            return THUMB / 2 + rx * scale, THUMB / 2 - y * scale, rz

        projected = [project(v) for v in part.vertices]
        order = sorted(part.faces, key=lambda f: -sum(projected[i][2] for i in f[0]) / len(f[0]))
        for face in order:
            draw.polygon([projected[i][:2] for i in face[0]], fill=face_colour(part, rbh, face, pages))
    draw.text((2, 2), label, fill=(255, 255, 0))
    return image


def main():
    rbh_path, vmc_path, out = sys.argv[1:4]
    name = os.path.splitext(os.path.basename(rbh_path))[0].lower()
    pages = save_pages(vmc_path, os.path.join(out, 'pages'))
    rbh = Rbh(rbh_path)
    folder = os.path.join(out, name)
    os.makedirs(folder, exist_ok=True)
    offsets = sorted(set(offset for _, offset in part_offsets(rbh)))
    thumbs = []
    for offset in offsets:
        try:
            part = Part(rbh, offset)
        except Exception:
            continue
        part_name = '%s_%06x' % (name, offset)
        write_obj(part, rbh, os.path.join(folder, part_name + '.obj'), part_name)
        thumbs.append(thumbnail(part, rbh, pages, '%06x' % offset))
    columns = 16
    sheet = Image.new('RGB', (columns * THUMB, math.ceil(len(thumbs) / columns) * THUMB))
    for i, thumb in enumerate(thumbs):
        sheet.paste(thumb, ((i % columns) * THUMB, (i // columns) * THUMB))
    sheet.save(os.path.join(out, name + '_sheet.png'))
    print('%s: %d parts' % (name, len(thumbs)))


main()
