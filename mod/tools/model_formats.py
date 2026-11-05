"""Tod 3D models (pol.HAB): bank tables from Table.dll and the object format drawn by HglNa.

Object: u32 kind, u32 flags, float bounding sphere x, y, z, radius, then commands:
  0                  end
  bit 31 set         material chunk: 0x50-byte header (texture index at +0x20, data byte size at +0x4c),
                     followed by its strip commands
  otherwise          strip: u32 flags, u32 count, then count vertices (count * 3 for a triangle list, flag 8);
                     a vertex is 8 floats x y z nx ny nz u v (low bit of x set), or, with flag 0x20, a
                     reference: u32 tag (low bit clear), u32 byte offset from the end of the reference
"""
import struct

import texture_formats as tf

CHUNK_HEADER_SIZE = 0x50
CHUNK_TEXTURE_OFFSET = 0x20
CHUNK_SIZE_OFFSET = 0x4C
OBJECT_HEADER_SIZE = 0x18
VERTEX_FLOATS = 8
VERTEX_SIZE = VERTEX_FLOATS * 4
STRIP_LIST_FLAG = 0x08
STRIP_REFERENCES_FLAG = 0x20
NO_TEXTURE = 0xFFFFFFFF
NO_BANK = 0x102


class ModelTables:
    def __init__(self, table_dll):
        self.executable = tf.PortableExecutable(table_dll)
        exports = self.executable.exports()
        self.exports = exports
        self.texture_table = tf.read_texture_table(self.executable, exports["TexFileNameTbl"], exports["TexHeaderTbl"])

    def u16(self, va):
        return struct.unpack_from("<H", self.executable.blob, self.executable.offset(va))[0]

    def s16(self, va):
        return struct.unpack_from("<h", self.executable.blob, self.executable.offset(va))[0]

    def bank_name(self, bank):
        pointer = self.executable.u32(self.exports["PolFileNameTbl"] + 4 * bank)
        if not pointer or self.executable.offset(pointer) is None:
            return None
        return self.executable.string(pointer)

    def bank_model_ids(self, bank):
        count = self.u16(self.exports["BankPatMax"] + 2 * bank)
        pointer = self.executable.u32(self.exports["PatNoTbl"] + 4 * bank)
        if not count or not pointer or self.executable.offset(pointer) is None:
            return []
        return [self.s16(pointer + 2 * i) for i in range(count)]

    def banks(self):
        bank = 1
        while True:
            name = self.bank_name(bank)
            if name is None:
                return
            yield bank, name, self.bank_model_ids(bank)
            bank += 1

    def bank_textures(self, bank_name):
        return self.texture_table.get(bank_name, [])


def bank_objects(raw, model_ids):
    """[(model id, object bytes from the object start to the end of its block, has own textures)]"""
    entries = [struct.unpack_from("<2I", raw, 8 * i) for i in range(len(model_ids) + 1)]
    objects = []
    for i, model_id in enumerate(model_ids):
        start, object_offset = entries[i]
        end = entries[i + 1][0] & 0x7FFFFFFF or len(raw)
        textured = not (start >> 31)
        objects.append((model_id, raw[object_offset:end], textured))
    return objects


def read_vertex(data, position):
    return struct.unpack_from("<8f", data, position)


def parse_object(data):
    """{'kind', 'flags', 'sphere', 'chunks': [{'header', 'texture', 'strips': [{'flags', 'list', 'vertices'}]}]}"""
    kind, flags = struct.unpack_from("<2I", data, 0)
    sphere = struct.unpack_from("<4f", data, 8)
    model = {"kind": kind, "flags": flags, "sphere": sphere, "chunks": []}
    position = OBJECT_HEADER_SIZE
    chunk = None
    while position + 4 <= len(data):
        command = struct.unpack_from("<I", data, position)[0]
        if command == 0:
            break
        if command >> 31:
            header = data[position:position + CHUNK_HEADER_SIZE]
            texture = struct.unpack_from("<I", header, CHUNK_TEXTURE_OFFSET)[0]
            chunk = {"header": header, "texture": texture, "strips": []}
            model["chunks"].append(chunk)
            position += CHUNK_HEADER_SIZE
            continue
        count = struct.unpack_from("<I", data, position + 4)[0]
        position += 8
        is_list = bool(command & STRIP_LIST_FLAG)
        vertex_count = count * 3 if is_list else count
        vertices = []
        for _ in range(vertex_count):
            tag = struct.unpack_from("<I", data, position)[0]
            if command & STRIP_REFERENCES_FLAG and not tag & 1:
                relative = struct.unpack_from("<i", data, position + 4)[0]
                vertices.append(read_vertex(data, position + 8 + relative))
                position += 8
            else:
                vertices.append(read_vertex(data, position))
                position += VERTEX_SIZE
        if chunk is None:
            chunk = {"header": None, "texture": NO_TEXTURE, "strips": []}
            model["chunks"].append(chunk)
        chunk["strips"].append({"flags": command, "list": is_list, "vertices": vertices})
    model["size"] = position + 4
    return model


def strip_triangles(strip):
    vertices = strip["vertices"]
    if strip["list"]:
        return [(vertices[i], vertices[i + 1], vertices[i + 2]) for i in range(0, len(vertices) - 2, 3)]
    triangles = []
    for i in range(len(vertices) - 2):
        a, b, c = vertices[i], vertices[i + 1], vertices[i + 2]
        triangles.append((a, b, c) if i % 2 == 0 else (b, a, c))
    return triangles


def material_name(chunk_index, texture):
    return f"m{chunk_index}_t{texture}" if texture != NO_TEXTURE else f"m{chunk_index}_none"


def write_obj(model, obj_path, mtl_name, texture_files):
    """Writes a right-handed OBJ (z negated). texture_files: {texture index: png file name}."""
    lines = [f"# Typing of the Dead EX model, kind {model['kind']}, flags 0x{model['flags']:x}",
             f"mtllib {mtl_name}"]
    positions, normals, uvs, faces = [], [], [], []
    index = {}

    def vertex_key(vertex):
        if vertex not in index:
            x, y, z, nx, ny, nz, u, v = vertex
            positions.append((x, y, -z))
            normals.append((nx, ny, -nz))
            uvs.append((u, 1.0 - v))
            index[vertex] = len(positions)
        return index[vertex]

    for chunk_index, chunk in enumerate(model["chunks"]):
        faces.append(f"usemtl {material_name(chunk_index, chunk['texture'])}")
        for strip in chunk["strips"]:
            for a, b, c in strip_triangles(strip):
                if a[:3] == b[:3] or b[:3] == c[:3] or a[:3] == c[:3]:
                    continue
                ia, ib, ic = vertex_key(a), vertex_key(b), vertex_key(c)
                faces.append(f"f {ia}/{ia}/{ia} {ib}/{ib}/{ib} {ic}/{ic}/{ic}")
    lines += [f"v {x:.6f} {y:.6f} {z:.6f}" for x, y, z in positions]
    lines += [f"vt {u:.6f} {v:.6f}" for u, v in uvs]
    lines += [f"vn {x:.6f} {y:.6f} {z:.6f}" for x, y, z in normals]
    lines += faces
    with open(obj_path, "w", newline="\n") as out:
        out.write("\n".join(lines) + "\n")

    mtl_lines = []
    for chunk_index, chunk in enumerate(model["chunks"]):
        mtl_lines.append(f"newmtl {material_name(chunk_index, chunk['texture'])}")
        if chunk["header"]:
            floats = struct.unpack_from("<20f", chunk["header"])
            mtl_lines.append("Kd {:.4f} {:.4f} {:.4f}".format(*floats[12:15]))
            mtl_lines.append("Ks {:.4f} {:.4f} {:.4f}".format(*floats[16:19]))
        if chunk["texture"] in texture_files:
            mtl_lines.append(f"map_Kd {texture_files[chunk['texture']]}")
        mtl_lines.append("")
    with open(obj_path[:-4] + ".mtl", "w", newline="\n") as out:
        out.write("\n".join(mtl_lines))
    return len(positions), sum(1 for f in faces if f.startswith("f "))
