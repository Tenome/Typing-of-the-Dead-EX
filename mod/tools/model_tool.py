"""Exports Typing of the Dead EX 3D models (pol.HAB) as OBJ + MTL + PNG textures plus the original object bytes.

  python model_tool.py export <game folder> <out folder> [bank name ...]
      writes <out>/<bank>/<model id>.obj/.mtl/.bin and <bank>/textures/NNN.png; models.tsv lists every model
  python model_tool.py list <game folder>
      prints bank, model id and size of every model
  python model_tool.py preview <obj file> <png file>
      renders a quick shaded preview (front and side view) to check an exported or edited OBJ

The .obj is right-handed (z negated against the game); todmod converts it back when it loads a replacement.
Needs Python 3 with numpy and Pillow.
"""
import os
import sys

import numpy
from PIL import Image

import model_formats as mf
import texture_formats as tf


def export_textures(bank_name, layout, tex_hab, out_dir):
    if bank_name not in tex_hab:
        return {}
    raw = tf.bank_bytes(tex_hab[bank_name])
    os.makedirs(out_dir, exist_ok=True)
    files = {}
    for index, (width, height, pixel_format, data_format, offset, texture_id) in enumerate(layout):
        try:
            rgba = tf.decode_texture(raw[offset:], width, height, pixel_format, data_format)
        except (ValueError, IndexError):
            rgba = None
        if rgba is None:
            continue
        name = f"{index:03d}_id{texture_id}.png"
        Image.fromarray(rgba, "RGBA").save(os.path.join(out_dir, name))
        files[index] = f"textures/{name}"
    return files


def export(game_folder, out_folder, only_banks):
    tables = mf.ModelTables(os.path.join(game_folder, "Table.dll"))
    pol_hab = tf.read_hab(os.path.join(game_folder, "pol.HAB"))
    tex_hab = tf.read_hab(os.path.join(game_folder, "tex.HAB"))
    os.makedirs(out_folder, exist_ok=True)
    rows = ["bank\tmodel_id\tvertices\ttriangles\tmaterials\ttextures"]
    for bank, bank_name, model_ids in tables.banks():
        folder_name = bank_name[:-4] if bank_name.lower().endswith(".bin") else bank_name
        if only_banks and bank_name not in only_banks and folder_name not in only_banks:
            continue
        if bank_name not in pol_hab or not model_ids:
            continue
        bank_dir = os.path.join(out_folder, folder_name)
        os.makedirs(bank_dir, exist_ok=True)
        texture_files = export_textures(bank_name, tables.bank_textures(bank_name), tex_hab,
                                        os.path.join(bank_dir, "textures"))
        raw = tf.bank_bytes(pol_hab[bank_name])
        for model_id, data, _ in mf.bank_objects(raw, model_ids):
            model = mf.parse_object(data)
            with open(os.path.join(bank_dir, f"{model_id}.bin"), "wb") as out:
                out.write(data[:model["size"]])
            vertices, triangles = mf.write_obj(model, os.path.join(bank_dir, f"{model_id}.obj"), f"{model_id}.mtl",
                                               texture_files)
            textures = sorted({c["texture"] for c in model["chunks"] if c["texture"] != mf.NO_TEXTURE})
            rows.append(f"{folder_name}\t{model_id}\t{vertices}\t{triangles}\t{len(model['chunks'])}\t"
                        + ",".join(map(str, textures)))
        print(f"{folder_name}: {len(model_ids)} models")
    with open(os.path.join(out_folder, "models.tsv"), "w", newline="\n") as out:
        out.write("\n".join(rows) + "\n")


def list_models(game_folder):
    tables = mf.ModelTables(os.path.join(game_folder, "Table.dll"))
    pol_hab = tf.read_hab(os.path.join(game_folder, "pol.HAB"))
    for bank, bank_name, model_ids in tables.banks():
        if bank_name not in pol_hab:
            continue
        for model_id, data, _ in mf.bank_objects(tf.bank_bytes(pol_hab[bank_name]), model_ids):
            print(f"{bank_name}\t{model_id}\t{len(data)}")


def read_obj(path):
    positions, faces = [], []
    for line in open(path, encoding="utf-8", errors="replace"):
        parts = line.split()
        if not parts:
            continue
        if parts[0] == "v":
            positions.append([float(x) for x in parts[1:4]])
        elif parts[0] == "f":
            corners = [int(p.split("/")[0]) for p in parts[1:]]
            corners = [c - 1 if c > 0 else len(positions) + c for c in corners]
            for i in range(1, len(corners) - 1):
                faces.append((corners[0], corners[i], corners[i + 1]))
    return numpy.array(positions, dtype=numpy.float64), numpy.array(faces, dtype=numpy.int64)


def render_view(positions, faces, rotation, size):
    points = positions @ rotation.T
    low, high = points.min(axis=0), points.max(axis=0)
    scale = (size - 8) / max(high[0] - low[0], high[1] - low[1], 1e-6)
    screen_x = (points[:, 0] - (low[0] + high[0]) / 2) * scale + size / 2
    screen_y = size / 2 - (points[:, 1] - (low[1] + high[1]) / 2) * scale
    depth = numpy.full((size, size), -numpy.inf)
    image = numpy.full((size, size), 32, dtype=numpy.uint8)
    light = numpy.array([0.3, 0.5, 0.8])
    light /= numpy.linalg.norm(light)
    for a, b, c in faces:
        normal = numpy.cross(points[b] - points[a], points[c] - points[a])
        length = numpy.linalg.norm(normal)
        if length == 0:
            continue
        shade = int(60 + 190 * abs(normal @ light) / length)
        xs, ys = screen_x[[a, b, c]], screen_y[[a, b, c]]
        x0, x1 = max(int(xs.min()), 0), min(int(xs.max()) + 1, size)
        y0, y1 = max(int(ys.min()), 0), min(int(ys.max()) + 1, size)
        if x0 >= x1 or y0 >= y1:
            continue
        grid_x, grid_y = numpy.meshgrid(numpy.arange(x0, x1) + 0.5, numpy.arange(y0, y1) + 0.5)
        denominator = (ys[1] - ys[2]) * (xs[0] - xs[2]) + (xs[2] - xs[1]) * (ys[0] - ys[2])
        if abs(denominator) < 1e-12:
            continue
        w0 = ((ys[1] - ys[2]) * (grid_x - xs[2]) + (xs[2] - xs[1]) * (grid_y - ys[2])) / denominator
        w1 = ((ys[2] - ys[0]) * (grid_x - xs[2]) + (xs[0] - xs[2]) * (grid_y - ys[2])) / denominator
        w2 = 1 - w0 - w1
        inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
        z = w0 * points[a, 2] + w1 * points[b, 2] + w2 * points[c, 2]
        region = depth[y0:y1, x0:x1]
        closer = inside & (z > region)
        region[closer] = z[closer]
        image[y0:y1, x0:x1][closer] = shade
    return image


def preview(obj_path, png_path, size=320):
    positions, faces = read_obj(obj_path)
    front = numpy.eye(3)
    angle = numpy.radians(90)
    side = numpy.array([[numpy.cos(angle), 0, numpy.sin(angle)], [0, 1, 0], [-numpy.sin(angle), 0, numpy.cos(angle)]])
    views = [render_view(positions, faces, rotation, size) for rotation in (front, side)]
    Image.fromarray(numpy.hstack(views), "L").save(png_path)
    print(f"{obj_path}: {len(positions)} vertices, {len(faces)} triangles -> {png_path}")


def main():
    if len(sys.argv) >= 4 and sys.argv[1] == "export":
        export(sys.argv[2], sys.argv[3], set(sys.argv[4:]))
    elif len(sys.argv) == 3 and sys.argv[1] == "list":
        list_models(sys.argv[2])
    elif len(sys.argv) == 4 and sys.argv[1] == "preview":
        preview(sys.argv[2], sys.argv[3])
    else:
        print(__doc__)
        sys.exit(1)


if __name__ == "__main__":
    main()
