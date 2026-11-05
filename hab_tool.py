import argparse
import glob
import json
import os
import struct
import sys

HAB_MAGIC = b"HAB0"
HAB_HEADER_SIZE = 0x20
HAB_INDEX_ENTRY_SIZE = 16
HAB_NAME_SIZE = 16
HAB_DATA_ALIGNMENT = 0x800
HAB_FILE_ALIGNMENT = 0x1000

WORD_RECORD_SIZE = 0x88
WORD_MAX_READING_UNITS = 64
WORD_TERMINATOR = 0xFFFFFFFF
SMALL_KANA = set("ゃゅょ")
LOWERCASE_ID_FIRST = 240
LOWERCASE_ID_LAST = 265
SMALL_KANA_BY_ID = {193: "ぁ", 194: "ぇ", 195: "ぃ", 196: "ぉ", 197: "ぅ", 198: "ゃ", 199: "ょ", 200: "ゅ"}
SMALL_TSU_ID = 203
HATSUON_ID = 202
ROMAJI_TABLE_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "romaji_table.json")


def align_up(value, alignment):
    return (value + alignment - 1) & ~(alignment - 1)


def read_hab_index(blob):
    if blob[:4] != HAB_MAGIC:
        raise ValueError("not a HAB0 archive")
    version, variant, total_size = struct.unpack_from("<HHQ", blob, 4)
    field_a, field_b, names_size, data_size = struct.unpack_from("<4I", blob, 0x10)
    count = field_b & 0xFFFF
    names_start = HAB_HEADER_SIZE + count * HAB_INDEX_ENTRY_SIZE
    data_base = names_start + names_size
    entries = []
    for i in range(count):
        name_offset, offset, size, _ = struct.unpack_from(
            "<4I", blob, HAB_HEADER_SIZE + i * HAB_INDEX_ENTRY_SIZE
        )
        name_at = names_start + name_offset
        name = blob[name_at:blob.index(b"\0", name_at)].decode("cp932")
        entries.append({"name": name, "offset": data_base + offset, "size": size})
    header = {
        "version": version,
        "variant": variant,
        "field_a": field_a,
        "field_b": field_b,
    }
    return header, entries, data_base, total_size


PRS_MAX_LONG_DISTANCE = 0x1FFF
PRS_MAX_SHORT_DISTANCE = 256
PRS_MAX_LENGTH = 256
PRS_HASH_CHAIN_LIMIT = 48


def prs_decompress(data, start=0):
    output = bytearray()
    position = start
    control = 0
    bits_left = 0

    def next_bit():
        nonlocal control, bits_left, position
        if bits_left == 0:
            control = data[position]
            position += 1
            bits_left = 8
        bit = control & 1
        control >>= 1
        bits_left -= 1
        return bit

    while True:
        if next_bit():
            output.append(data[position])
            position += 1
            continue
        if next_bit():
            packed = data[position] | (data[position + 1] << 8)
            position += 2
            if packed == 0:
                break
            length = packed & 7
            distance_field = packed >> 3
            if length == 0:
                length = data[position] + 1
                position += 1
            else:
                length += 2
            distance = 0x2000 - distance_field
        else:
            length = (next_bit() << 1)
            length |= next_bit()
            length += 2
            distance = 0x100 - data[position]
            position += 1
        source = len(output) - distance
        if source < 0:
            raise ValueError("invalid PRS back-reference")
        for offset in range(length):
            output.append(output[source + offset])
    return bytes(output), position


class PrsBitWriter:
    def __init__(self):
        self.output = bytearray()
        self.control_index = None
        self.bits_used = 8

    def write_bit(self, bit):
        if self.bits_used == 8:
            self.control_index = len(self.output)
            self.output.append(0)
            self.bits_used = 0
        if bit:
            self.output[self.control_index] |= 1 << self.bits_used
        self.bits_used += 1


def find_longest_match(data, position, chains):
    if position + 3 > len(data):
        return 0, 0
    key = data[position:position + 3]
    best_length = 0
    best_distance = 0
    limit = min(PRS_MAX_LENGTH, len(data) - position)
    candidates = chains.get(key, ())
    checked = 0
    for candidate in reversed(candidates):
        distance = position - candidate
        if distance > PRS_MAX_LONG_DISTANCE:
            break
        length = 3
        while length < limit and data[candidate + length] == data[position + length]:
            length += 1
        if length > best_length:
            best_length, best_distance = length, distance
            if length == limit:
                break
        checked += 1
        if checked >= PRS_HASH_CHAIN_LIMIT:
            break
    return best_length, best_distance


def prs_compress(data):
    writer = PrsBitWriter()
    chains = {}
    position = 0

    def register(upto):
        for index in range(register.next_index, min(upto, len(data) - 2)):
            chains.setdefault(data[index:index + 3], []).append(index)
        register.next_index = max(register.next_index, upto)

    register.next_index = 0
    while position < len(data):
        register(position)
        length, distance = find_longest_match(data, position, chains)
        short_match = 0
        if position >= 1:
            for candidate_distance in range(1, min(PRS_MAX_SHORT_DISTANCE, position) + 1):
                run = 0
                while (
                    run < 5
                    and position + run < len(data)
                    and data[position - candidate_distance + run] == data[position + run]
                ):
                    run += 1
                if run >= 2 and run > short_match:
                    short_match, short_distance = run, candidate_distance
                    if run == 5:
                        break
        if short_match >= 2 and short_match >= length - 0 and short_match <= 5 and length <= 5:
            writer.write_bit(0)
            writer.write_bit(0)
            writer.write_bit((short_match - 2) >> 1 & 1)
            writer.write_bit((short_match - 2) & 1)
            writer.output.append(0x100 - short_distance & 0xFF)
            position += short_match
        elif length >= 3:
            writer.write_bit(0)
            writer.write_bit(1)
            distance_field = 0x2000 - distance
            if length <= 9:
                packed = (distance_field << 3) | (length - 2)
                writer.output += struct.pack("<H", packed)
            else:
                packed = distance_field << 3
                writer.output += struct.pack("<H", packed) + bytes([length - 1])
            position += length
        else:
            writer.write_bit(1)
            writer.output.append(data[position])
            position += 1
    writer.write_bit(0)
    writer.write_bit(1)
    writer.output += b"\0\0"
    return bytes(writer.output)


def classify_texture_file(content):
    if len(content) == 0:
        return "empty", None
    if len(content) >= 8:
        declared = struct.unpack_from("<I", content, 0)[0]
        try:
            raw, used = prs_decompress(content, 4)
        except (ValueError, IndexError):
            return "stored", None
        if len(raw) == declared:
            return "prs", raw
    return "stored", None


def command_tex_unpack(args):
    manifest_path = os.path.join(args.input_dir, "manifest.json")
    os.makedirs(args.output_dir, exist_ok=True)
    kinds = {}
    for name in sorted(os.listdir(args.input_dir)):
        if name == "manifest.json":
            continue
        content = open(os.path.join(args.input_dir, name), "rb").read()
        kind, raw = classify_texture_file(content)
        kinds[name] = kind
        if kind == "prs":
            open(os.path.join(args.output_dir, name + ".raw"), "wb").write(raw)
        elif kind == "stored":
            open(os.path.join(args.output_dir, name + ".raw"), "wb").write(content)
    with open(os.path.join(args.output_dir, "tex_kinds.json"), "w") as handle:
        json.dump(kinds, handle, indent=1)
    counts = {kind: list(kinds.values()).count(kind) for kind in set(kinds.values())}
    print(f"unpacked: {counts}")


TEXTURE_FORMATS = ("argb4444", "rgb565", "argb1555")


def require_imaging():
    try:
        import numpy
        from PIL import Image
    except ImportError:
        sys.exit("this command needs: pip install numpy pillow")
    return numpy, Image


def tile_gather_index(numpy, width, height, tile_size, layout):
    rows, columns = numpy.mgrid[0:height, 0:width]
    if layout == "linear":
        return rows * width + columns
    source_rows = height - 1 - rows if layout == "tiled-bottom-up" else rows
    tiles_down = height // tile_size
    tile_number = (columns // tile_size) * tiles_down + (source_rows // tile_size)
    inner = (source_rows % tile_size) * tile_size + (columns % tile_size)
    return tile_number * tile_size * tile_size + inner


def pixels_to_rgba(numpy, values, texture_format):
    values = values.astype(numpy.uint32)
    if texture_format == "argb4444":
        alpha = ((values >> 12) & 0xF) * 17
        red = ((values >> 8) & 0xF) * 17
        green = ((values >> 4) & 0xF) * 17
        blue = (values & 0xF) * 17
    elif texture_format == "rgb565":
        alpha = numpy.full(values.shape, 255)
        red = ((values >> 11) & 0x1F) * 255 // 31
        green = ((values >> 5) & 0x3F) * 255 // 63
        blue = (values & 0x1F) * 255 // 31
    else:
        alpha = ((values >> 15) & 1) * 255
        red = ((values >> 10) & 0x1F) * 255 // 31
        green = ((values >> 5) & 0x1F) * 255 // 31
        blue = (values & 0x1F) * 255 // 31
    return numpy.stack([red, green, blue, alpha], axis=-1).astype(numpy.uint8)


def rgba_to_pixels(numpy, rgba, texture_format):
    red, green, blue, alpha = (rgba[..., i].astype(numpy.uint32) for i in range(4))
    if texture_format == "argb4444":
        quantize = lambda channel: (channel * 15 + 127) // 255
        return ((quantize(alpha) << 12) | (quantize(red) << 8) | (quantize(green) << 4) | quantize(blue)).astype(numpy.uint16)
    if texture_format == "rgb565":
        return (((red * 31 + 127) // 255 << 11) | ((green * 63 + 127) // 255 << 5) | ((blue * 31 + 127) // 255)).astype(numpy.uint16)
    return (
        ((alpha >= 128).astype(numpy.uint32) << 15)
        | ((red * 31 + 127) // 255 << 10)
        | ((green * 31 + 127) // 255 << 5)
        | ((blue * 31 + 127) // 255)
    ).astype(numpy.uint16)


PIXEL_FORMAT_NAMES = {0: "argb1555", 1: "rgb565", 2: "argb4444"}
DATA_FORMAT_RECTANGLE = 9
DATA_FORMAT_TWIDDLED_SQUARE = 1
DATA_FORMAT_TWIDDLED_RECTANGLE = 13
DATA_FORMAT_VQ = 3
VQ_CODEBOOK_BYTES = 2048
TEXTURE_TABLE_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "texture_table.json")


def read_pe_exports(blob):
    image_base, sections = read_pe_layout(blob)

    def to_offset(rva):
        for virtual_address, size, raw_offset in sections:
            if virtual_address <= rva < virtual_address + size:
                return raw_offset + rva - virtual_address
        return None

    pe = struct.unpack_from("<I", blob, 0x3C)[0]
    export_rva = struct.unpack_from("<I", blob, pe + 24 + 96)[0]
    table = to_offset(export_rva)
    count, functions_rva, names_rva, ordinals_rva = struct.unpack_from("<IIII", blob, table + 24)
    exports = {}
    for i in range(count):
        name_offset = to_offset(struct.unpack_from("<I", blob, to_offset(names_rva) + 4 * i)[0])
        name = blob[name_offset:blob.index(b"\0", name_offset)].decode("ascii")
        ordinal = struct.unpack_from("<H", blob, to_offset(ordinals_rva) + 2 * i)[0]
        exports[name] = struct.unpack_from("<I", blob, to_offset(functions_rva) + 4 * ordinal)[0]
    return image_base, to_offset, exports


def extract_texture_table(table_blob):
    image_base, to_offset, exports = read_pe_exports(table_blob)

    def read_u32_at_va(address):
        return struct.unpack_from("<I", table_blob, to_offset(address - image_base))[0]

    def read_name_at_va(address):
        start = to_offset(address - image_base)
        return table_blob[start:table_blob.index(b"\0", start)].decode("cp932")

    names_va = image_base + exports["TexFileNameTbl"]
    headers_va = image_base + exports["TexHeaderTbl"]
    banks = {}
    bank = 1
    while True:
        name_pointer = read_u32_at_va(names_va + 4 * bank)
        if not name_pointer or to_offset(name_pointer - image_base) is None:
            break
        header_start = to_offset(read_u32_at_va(headers_va + 4 * bank) - image_base)
        textures = []
        while True:
            width, height, pixel_format, data_format, _, offset, texture_id = struct.unpack_from(
                "<HHBBHII", table_blob, header_start + 16 * len(textures)
            )
            if width == 0:
                break
            textures.append([width, height, pixel_format, data_format, offset, texture_id])
        banks[read_name_at_va(name_pointer)] = textures
        bank += 1
    return banks


def load_texture_table():
    if not os.path.exists(TEXTURE_TABLE_FILE):
        sys.exit("run: python hab_tool.py tex-table Table.dll   (once)")
    with open(TEXTURE_TABLE_FILE, encoding="utf-8") as handle:
        return json.load(handle)


def command_tex_table(args):
    banks = extract_texture_table(open(args.table_dll, "rb").read())
    with open(TEXTURE_TABLE_FILE, "w", encoding="utf-8") as handle:
        json.dump(banks, handle)
    textures = sum(len(items) for items in banks.values())
    print(f"extracted {len(banks)} banks, {textures} textures to {TEXTURE_TABLE_FILE}")


def spread_bits(numpy, values, bit_count):
    spread = numpy.zeros_like(values)
    for bit in range(bit_count):
        spread |= ((values >> bit) & 1) << (2 * bit)
    return spread


def twiddle_index_map(numpy, width, height):
    rows, columns = numpy.mgrid[0:height, 0:width].astype(numpy.int64)
    side = min(width, height)
    bit_count = int(side).bit_length() - 1
    inner = (spread_bits(numpy, columns % side, bit_count) << 1) | spread_bits(numpy, rows % side, bit_count)
    if width > height:
        return inner + (columns // side) * side * side
    if height > width:
        return inner + (rows // side) * side * side
    return inner


def texture_byte_size(width, height, data_format):
    if data_format == DATA_FORMAT_VQ:
        return VQ_CODEBOOK_BYTES + (width * height) // 4
    return width * height * 2


def decode_texture_values(numpy, data, width, height, data_format):
    if data_format == DATA_FORMAT_RECTANGLE:
        values = numpy.frombuffer(data[:width * height * 2], dtype="<u2").reshape(height, width)
    elif data_format in (DATA_FORMAT_TWIDDLED_SQUARE, DATA_FORMAT_TWIDDLED_RECTANGLE):
        stream = numpy.frombuffer(data[:width * height * 2], dtype="<u2")
        values = stream[twiddle_index_map(numpy, width, height)]
    elif data_format == DATA_FORMAT_VQ:
        codebook = numpy.frombuffer(data[:VQ_CODEBOOK_BYTES], dtype="<u2").reshape(256, 4)
        block_width, block_height = width // 2, height // 2
        indices = numpy.frombuffer(
            data[VQ_CODEBOOK_BYTES:VQ_CODEBOOK_BYTES + block_width * block_height], dtype=numpy.uint8
        )
        blocks = codebook[indices[twiddle_index_map(numpy, block_width, block_height)]]
        values = numpy.zeros((height, width), dtype=numpy.uint16)
        values[0::2, 0::2] = blocks[..., 0]
        values[1::2, 0::2] = blocks[..., 1]
        values[0::2, 1::2] = blocks[..., 2]
        values[1::2, 1::2] = blocks[..., 3]
    else:
        raise ValueError(f"unsupported data format {data_format}")
    return values[::-1]


def cluster_blocks(numpy, vectors, cluster_count=256, iterations=14):
    unique = numpy.unique(vectors, axis=0)
    if len(unique) <= cluster_count:
        return unique.astype(numpy.float32)
    generator = numpy.random.default_rng(1234)
    centers = unique[generator.choice(len(unique), cluster_count, replace=False)].astype(numpy.float32)
    data = vectors.astype(numpy.float32)
    for _ in range(iterations):
        assignment = nearest_center(numpy, data, centers)
        for index in range(cluster_count):
            members = data[assignment == index]
            if len(members):
                centers[index] = members.mean(axis=0)
    return centers


def nearest_center(numpy, data, centers):
    result = numpy.empty(len(data), dtype=numpy.int64)
    center_norms = (centers ** 2).sum(axis=1)
    for start in range(0, len(data), 8192):
        chunk = data[start:start + 8192]
        distances = center_norms[None, :] - 2.0 * chunk @ centers.T
        result[start:start + 8192] = distances.argmin(axis=1)
    return result


def encode_texture_values(numpy, values_upright, width, height, data_format, pixel_format):
    values = values_upright[::-1]
    if data_format == DATA_FORMAT_RECTANGLE:
        return numpy.ascontiguousarray(values, dtype="<u2").tobytes()
    if data_format in (DATA_FORMAT_TWIDDLED_SQUARE, DATA_FORMAT_TWIDDLED_RECTANGLE):
        stream = numpy.zeros(width * height, dtype="<u2")
        stream[twiddle_index_map(numpy, width, height).reshape(-1)] = values.reshape(-1)
        return stream.tobytes()
    if data_format == DATA_FORMAT_VQ:
        name = PIXEL_FORMAT_NAMES[pixel_format]
        rgba = pixels_to_rgba(numpy, values, name).astype(numpy.int32)
        block_height, block_width = height // 2, width // 2
        corners = [rgba[0::2, 0::2], rgba[1::2, 0::2], rgba[0::2, 1::2], rgba[1::2, 1::2]]
        vectors = numpy.concatenate(corners, axis=-1).reshape(block_height * block_width, 16)
        centers = cluster_blocks(numpy, vectors)
        rounded = numpy.clip(numpy.rint(centers), 0, 255).astype(numpy.uint8)
        codebook_pixels = rgba_to_pixels(numpy, rounded.reshape(-1, 4), name).reshape(-1, 4)
        padded = numpy.zeros((256, 4), dtype="<u2")
        padded[:len(codebook_pixels)] = codebook_pixels
        assignment = nearest_center(numpy, vectors.astype(numpy.float32), centers).reshape(block_height, block_width)
        index_stream = numpy.zeros(block_width * block_height, dtype=numpy.uint8)
        index_stream[twiddle_index_map(numpy, block_width, block_height).reshape(-1)] = assignment.reshape(-1)
        return padded.astype("<u2").tobytes() + index_stream.tobytes()
    raise ValueError(f"unsupported data format {data_format}")


def load_bank_raw(raw_dir, bank_name):
    path = os.path.join(raw_dir, bank_name + ".raw")
    return path, (bytearray(open(path, "rb").read()) if os.path.exists(path) else None)


def select_banks(table, requested):
    if requested:
        missing = [name for name in requested if name not in table]
        if missing:
            sys.exit(f"unknown banks: {missing}")
        return requested
    return [name for name, textures in table.items() if textures]


def make_contact_sheet(numpy, Image, images, labels):
    from PIL import ImageDraw
    cell, columns = 132, 8
    rows = (len(images) + columns - 1) // columns
    sheet = Image.new("RGB", (columns * cell, rows * (cell + 12)), (24, 24, 24))
    draw = ImageDraw.Draw(sheet)
    for position, (image, label) in enumerate(zip(images, labels)):
        scale = min(128 / image.width, 128 / image.height, 1.0 if max(image.size) > 128 else 4.0)
        resized = image.resize((max(1, int(image.width * scale)), max(1, int(image.height * scale))))
        background = Image.new("RGBA", resized.size, (70, 70, 120, 255))
        background.alpha_composite(resized.convert("RGBA"))
        x, y = (position % columns) * cell + 2, (position // columns) * (cell + 12) + 12
        sheet.paste(background.convert("RGB"), (x, y))
        draw.text((x, y - 11), label, fill=(255, 255, 0))
    return sheet


def command_tex_export(args):
    numpy, Image = require_imaging()
    table = load_texture_table()
    for bank_name in select_banks(table, args.bank):
        _, raw = load_bank_raw(args.raw_dir, bank_name)
        if raw is None:
            continue
        folder = os.path.join(args.png_dir, bank_name.replace(".bin", ""))
        os.makedirs(folder, exist_ok=True)
        images, labels = [], []
        for index, (width, height, pixel_format, data_format, offset, texture_id) in enumerate(table[bank_name]):
            size = texture_byte_size(width, height, data_format)
            values = decode_texture_values(numpy, bytes(raw[offset:offset + size]), width, height, data_format)
            rgba = pixels_to_rgba(numpy, values, PIXEL_FORMAT_NAMES[pixel_format])
            image = Image.fromarray(rgba, "RGBA")
            image.save(os.path.join(folder, f"{index:03d}_id{texture_id}.png"))
            images.append(image)
            labels.append(f"#{index} {width}x{height}")
        make_contact_sheet(numpy, Image, images, labels).save(os.path.join(folder, "_sheet.png"))
        print(f"{bank_name}: {len(images)} textures -> {folder}")


def command_tex_import(args):
    numpy, Image = require_imaging()
    table = load_texture_table()
    total_changed = 0
    for bank_name in select_banks(table, args.bank):
        folder = os.path.join(args.png_dir, bank_name.replace(".bin", ""))
        raw_path, raw = load_bank_raw(args.raw_dir, bank_name)
        if raw is None or not os.path.isdir(folder):
            continue
        changed = 0
        for index, (width, height, pixel_format, data_format, offset, texture_id) in enumerate(table[bank_name]):
            png_path = os.path.join(folder, f"{index:03d}_id{texture_id}.png")
            if not os.path.exists(png_path):
                continue
            image = Image.open(png_path).convert("RGBA")
            if image.size != (width, height):
                sys.exit(f"{png_path}: is {image.size[0]}x{image.size[1]}, must be {width}x{height}")
            name = PIXEL_FORMAT_NAMES[pixel_format]
            new_values = rgba_to_pixels(numpy, numpy.array(image), name)
            size = texture_byte_size(width, height, data_format)
            current = decode_texture_values(numpy, bytes(raw[offset:offset + size]), width, height, data_format)
            if name == "rgb565":
                same = numpy.array_equal(new_values, current)
            else:
                same = numpy.array_equal(new_values, current)
            if same:
                continue
            raw[offset:offset + size] = encode_texture_values(numpy, new_values, width, height, data_format, pixel_format)
            changed += 1
        if changed:
            open(raw_path, "wb").write(bytes(raw))
        total_changed += changed
        print(f"{bank_name}: {changed} textures updated")
    print(f"total updated textures: {total_changed}")


def command_tex_pack(args):
    with open(os.path.join(args.raw_dir, "tex_kinds.json")) as handle:
        kinds = json.load(handle)
    changed = 0
    for name, kind in kinds.items():
        target = os.path.join(args.output_dir, name)
        if kind == "empty":
            continue
        raw = open(os.path.join(args.raw_dir, name + ".raw"), "rb").read()
        if kind == "stored":
            if open(target, "rb").read() != raw:
                open(target, "wb").write(raw)
                changed += 1
            continue
        original = open(target, "rb").read()
        original_kind, original_raw = classify_texture_file(original)
        if original_kind == "prs" and original_raw == raw:
            continue
        packed = struct.pack("<I", len(raw)) + prs_compress(raw)
        verify, _ = prs_decompress(packed, 4)
        if verify != raw:
            sys.exit(f"compression verification failed for {name}")
        padded = packed
        open(target, "wb").write(padded)
        changed += 1
    print(f"repacked {changed} changed files into {args.output_dir}")


def command_hab_list(args):
    blob = open(args.archive, "rb").read()
    _, entries, data_base, total_size = read_hab_index(blob)
    print(f"declared size {total_size:#x}, actual {len(blob):#x}, data base {data_base:#x}")
    for entry in entries:
        complete = entry["offset"] + entry["size"] <= len(blob)
        flag = "" if complete else "  [TRUNCATED]"
        print(f"{entry['name']:<20} offset={entry['offset']:#010x} size={entry['size']:#09x}{flag}")


def command_hab_extract(args):
    blob = open(args.archive, "rb").read()
    header, entries, data_base, _ = read_hab_index(blob)
    os.makedirs(args.output_dir, exist_ok=True)
    skipped = []
    for entry in entries:
        end = entry["offset"] + entry["size"]
        if end > len(blob):
            skipped.append(entry["name"])
            continue
        with open(os.path.join(args.output_dir, entry["name"]), "wb") as handle:
            handle.write(blob[entry["offset"]:end])
    manifest = {"header": header, "order": [entry["name"] for entry in entries]}
    with open(os.path.join(args.output_dir, "manifest.json"), "w") as handle:
        json.dump(manifest, handle, indent=2)
    print(f"extracted {len(entries) - len(skipped)} of {len(entries)} files")
    if skipped:
        print(f"skipped {len(skipped)} truncated files")


def build_hab(header, named_files):
    count = len(named_files)
    index_size = count * HAB_INDEX_ENTRY_SIZE
    names = bytearray()
    name_offsets = []
    for name, _ in named_files:
        name_offsets.append(len(names))
        encoded = name.encode("cp932") + b"\0"
        names += encoded.ljust(align_up(len(encoded), HAB_NAME_SIZE), b"\0")
    data_base = HAB_HEADER_SIZE + index_size + len(names)
    first_data = align_up(data_base, HAB_DATA_ALIGNMENT)
    position = first_data
    index = bytearray()
    placements = []
    for i, (name, content) in enumerate(named_files):
        index += struct.pack("<4I", name_offsets[i], position - data_base, len(content), 0)
        placements.append((position, content))
        position = align_up(position + len(content), HAB_DATA_ALIGNMENT)
    last_end = placements[-1][0] + len(placements[-1][1])
    total_size = align_up(last_end, HAB_FILE_ALIGNMENT)
    out = bytearray(total_size)
    out[0:4] = HAB_MAGIC
    struct.pack_into("<HHQ", out, 4, header["version"], header["variant"], total_size)
    field_b = (header["field_b"] & 0xFFFF0000) | count
    struct.pack_into("<4I", out, 0x10, header["field_a"], field_b, len(names), total_size - data_base)
    out[HAB_HEADER_SIZE:HAB_HEADER_SIZE + index_size] = index
    out[HAB_HEADER_SIZE + index_size:data_base] = names
    for at, content in placements:
        out[at:at + len(content)] = content
    return bytes(out)


def command_hab_pack(args):
    with open(os.path.join(args.input_dir, "manifest.json")) as handle:
        manifest = json.load(handle)
    named_files = []
    for name in manifest["order"]:
        with open(os.path.join(args.input_dir, name), "rb") as handle:
            named_files.append((name, handle.read()))
    with open(args.archive, "wb") as handle:
        handle.write(build_hab(manifest["header"], named_files))
    print(f"wrote {args.archive}")


def split_reading_units(text):
    units = []
    for char in text:
        if char in SMALL_KANA and units:
            units[-1] += char
        else:
            units.append(char)
    return units


def parse_word_file(blob):
    words = []
    index = 0
    while True:
        base = index * WORD_RECORD_SIZE
        group, pointer = struct.unpack_from("<II", blob, base)
        if group == WORD_TERMINATOR:
            break
        reading_ids = struct.unpack_from(f"<{WORD_MAX_READING_UNITS}H", blob, base + 8)
        unit_count = reading_ids.index(0) if 0 in reading_ids else WORD_MAX_READING_UNITS
        text_end = blob.index(b"\0", pointer)
        words.append(
            {
                "group": group,
                "display": blob[pointer:text_end].decode("cp932"),
                "reading_ids": list(reading_ids[:unit_count]),
            }
        )
        index += 1
    return words


def serialize_word_file(words):
    records = bytearray()
    pool = bytearray()
    pool_start = (len(words) + 1) * WORD_RECORD_SIZE
    pointer_by_display = {}
    for position, word in enumerate(words):
        ids = word["reading_ids"]
        if len(ids) >= WORD_MAX_READING_UNITS:
            raise ValueError(f"reading too long: {word['display']}")
        padded_ids = ids + [0] * (WORD_MAX_READING_UNITS - len(ids))
        if word["display"] in pointer_by_display:
            pointer = pointer_by_display[word["display"]]
        else:
            pointer = pool_start + len(pool)
            encoded = word["display"].encode("cp932")
            slot = encoded + b"\0\xff"
            pool += slot + b"\xff" * (align_up(len(slot), 4) - len(slot))
        pointer_by_display[word["display"]] = pointer
        records += struct.pack("<II", word["group"], pointer)
        records += struct.pack(f"<{WORD_MAX_READING_UNITS}H", *padded_ids)
    terminator = struct.pack("<I", WORD_TERMINATOR) + b"\0" * (WORD_RECORD_SIZE - 4)
    return bytes(records + terminator + pool.rstrip(b"\xff"))


def read_pe_layout(blob):
    pe = struct.unpack_from("<I", blob, 0x3C)[0]
    section_count = struct.unpack_from("<H", blob, pe + 6)[0]
    optional_size = struct.unpack_from("<H", blob, pe + 20)[0]
    image_base = struct.unpack_from("<I", blob, pe + 24 + 28)[0]
    sections = []
    for i in range(section_count):
        at = pe + 24 + optional_size + i * 40
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from("<4I", blob, at + 8)
        sections.append((virtual_address, max(virtual_size, raw_size), raw_offset))
    return image_base, sections


def extract_romaji_table(exe_blob):
    image_base, sections = read_pe_layout(exe_blob)

    def to_file_offset(address):
        rva = address - image_base
        for virtual_address, size, raw_offset in sections:
            if virtual_address <= rva < virtual_address + size:
                return raw_offset + rva - virtual_address
        return None

    def read_u32(offset):
        return struct.unpack_from("<I", exe_blob, offset)[0]

    def read_string(address):
        offset = to_file_offset(address)
        if offset is None:
            return None
        end = exe_blob.index(b"\0", offset)
        raw = exe_blob[offset:end]
        if raw and all(32 <= byte < 127 for byte in raw):
            return raw.decode("ascii")
        return None

    def read_spellings(offset):
        spellings = []
        while True:
            text = read_string(read_u32(offset))
            if text is None:
                return spellings, offset
            spellings.append(text)
            offset += 4

    def read_entry(offset):
        spellings, after = read_spellings(offset)
        count_and_flags = read_u32(after)
        self_pointer = read_u32(after + 4)
        valid = (
            bool(spellings)
            and count_and_flags & 0xF == len(spellings)
            and self_pointer == image_base + offset
        )
        return spellings, count_and_flags, after + 8, valid

    anchor = None
    for offset in range(0, len(exe_blob) - 12, 4):
        if read_u32(offset + 8) != image_base + offset or read_u32(offset + 4) != 1:
            continue
        if read_string(read_u32(offset)) == "A":
            anchor = offset
            break
    if anchor is None:
        raise ValueError("romaji table not found")

    table = {}
    offset, unit_id = anchor, 123
    while True:
        spellings, flags, after, valid = read_entry(offset)
        if not valid:
            raw_text = None
            text_offset = to_file_offset(read_u32(offset))
            if text_offset is None or read_u32(offset + 4) != 1 or read_u32(offset + 8) != image_base + offset:
                break
            end = exe_blob.index(b"\0", text_offset)
            spellings = [exe_blob[text_offset:end].decode("cp932")]
            flags, after = 1, offset + 12
        table[unit_id] = {"spellings": spellings, "flags": flags >> 4, "parts": None}
        offset, unit_id = after, unit_id + 1

    end, unit_id = anchor, 122
    while True:
        count = read_u32(end - 8) & 0xF
        start = read_u32(end - 4) - image_base
        file_start = to_file_offset(read_u32(end - 4))
        if file_start is None or count < 1 or not file_start < end:
            break
        spellings = [read_string(read_u32(file_start + 4 * i)) for i in range(count)]
        if None in spellings:
            break
        after = file_start + 4 * count
        parts = None
        entry_start = file_start
        if read_u32(after) == image_base + file_start - 4:
            parts = list(struct.unpack_from("<HH", exe_blob, file_start - 4))
            entry_start = file_start - 4
        table[unit_id] = {"spellings": spellings, "flags": read_u32(end - 8) >> 4, "parts": parts}
        end, unit_id = entry_start, unit_id - 1
    return dict(sorted(table.items()))


def load_romaji_table():
    if not os.path.exists(ROMAJI_TABLE_FILE):
        return {}
    with open(ROMAJI_TABLE_FILE, encoding="utf-8") as handle:
        return {int(key): value for key, value in json.load(handle).items()}


def derive_combined_kana(romaji_table, id_to_kana):
    derived = {}
    for unit_id, entry in romaji_table.items():
        parts = entry["parts"]
        if not parts or unit_id in id_to_kana:
            continue
        base = id_to_kana.get(parts[0])
        small = SMALL_KANA_BY_ID.get(parts[1])
        candidate = base + small if base and small else None
        taken = set(id_to_kana.values()) | set(derived.values())
        if candidate and small in SMALL_KANA and len(base) == 1 and candidate not in taken:
            derived[unit_id] = candidate
    return derived


def romaji_preview(reading_ids, romaji_table):
    primaries = [
        romaji_table[i]["spellings"][0] if i in romaji_table else "?" for i in reading_ids
    ]
    pieces = []
    for position, unit_id in enumerate(reading_ids):
        following = primaries[position + 1] if position + 1 < len(primaries) else ""
        if unit_id == SMALL_TSU_ID and following[:1].isalpha() and following[0] not in "AIUEON":
            pieces.append(following[0])
        elif unit_id == HATSUON_ID:
            pieces.append("N" if following[:1] and following[0] not in "AIUEONY" else "NN")
        else:
            pieces.append(primaries[position])
    return "".join(pieces)


def command_romaji_table(args):
    table = extract_romaji_table(open(args.exe, "rb").read())
    with open(ROMAJI_TABLE_FILE, "w", encoding="utf-8") as handle:
        json.dump({str(key): value for key, value in table.items()}, handle, ensure_ascii=False)
    print(f"extracted {len(table)} entries (IDs {min(table)}-{max(table)}) to {ROMAJI_TABLE_FILE}")


def is_pure_kana(text):
    return all("ぁ" <= char <= "ん" or char == "ー" for char in text)


def learn_kana_table(word_lists):
    id_to_kana = {}
    for words in word_lists:
        for word in words:
            if not is_pure_kana(word["display"]):
                continue
            units = split_reading_units(word["display"])
            if len(units) != len(word["reading_ids"]):
                continue
            for unit, unit_id in zip(units, word["reading_ids"]):
                id_to_kana.setdefault(unit_id, unit)
    return id_to_kana


def is_cjk_or_kana(char):
    return "\u3040" <= char <= "\u30ff" or "\u4e00" <= char <= "\u9fff"


def to_fullwidth(char):
    if char == " ":
        return "\u3000"
    if char == "'":
        return "\u2019"
    if "!" <= char <= "~":
        return chr(ord(char) + 0xFEE0)
    return char


def learn_symbol_table(word_lists):
    votes = {}
    for words in word_lists:
        for word in words:
            display = word["display"]
            if any(is_cjk_or_kana(char) for char in display):
                continue
            if len(display) != len(word["reading_ids"]):
                continue
            for char, unit_id in zip(display, word["reading_ids"]):
                votes.setdefault(unit_id, {}).setdefault(char, 0)
                votes[unit_id][char] += 1
    table = {}
    for unit_id, counts in votes.items():
        if LOWERCASE_ID_FIRST <= unit_id <= LOWERCASE_ID_LAST:
            continue
        best = sorted(counts.items(), key=lambda item: (-item[1], item[0]))
        candidates = [char for char, _ in best]
        uppercase_first = sorted(candidates, key=lambda char: (char.islower(), char))
        table[unit_id] = uppercase_first[0]
    return table


def learn_unit_table(word_lists):
    word_lists = list(word_lists)
    table = learn_symbol_table(word_lists)
    table.update(learn_kana_table(word_lists))
    table.update(derive_combined_kana(load_romaji_table(), table))
    return table


def to_fullwidth_text(text):
    return "".join(to_fullwidth(char) for char in text)


def reading_ids_to_text(reading_ids, id_to_kana):
    return "".join(id_to_kana.get(i, f"<{i}>") for i in reading_ids)


def reading_text_to_ids(text, kana_to_id):
    ids = []
    position = 0
    while position < len(text):
        if text[position] == "<":
            close = text.index(">", position)
            ids.append(int(text[position + 1:close]))
            position = close + 1
            continue
        unit = to_fullwidth(text[position])
        if "ａ" <= unit <= "ｚ":
            unit = chr(ord(unit) - 0x20)
        if position + 1 < len(text) and text[position + 1] in SMALL_KANA:
            unit = text[position:position + 2]
            position += 1
        position += 1
        if unit not in kana_to_id:
            raise ValueError(f"no reading ID known for kana '{unit}'")
        ids.append(kana_to_id[unit])
    return ids


def expand_reference_paths(patterns, word_file):
    if not patterns:
        patterns = [os.path.join(os.path.dirname(word_file), "S*.bin")]
    paths = sorted({path for pattern in patterns for path in glob.glob(pattern)})
    return paths or [word_file]


def load_all_word_files(paths):
    return [parse_word_file(open(path, "rb").read()) for path in paths]


def command_words_export(args):
    reference_paths = expand_reference_paths(args.reference, args.word_file)
    table = learn_unit_table(load_all_word_files(reference_paths))
    words = parse_word_file(open(args.word_file, "rb").read())
    with open(args.tsv, "w", encoding="utf-8", newline="\n") as handle:
        for word in words:
            reading = reading_ids_to_text(word["reading_ids"], table)
            handle.write(f"{word['group']}\t{word['display']}\t{reading}\n")
    print(f"exported {len(words)} words; {len(table)} kana units known")


def command_words_import(args):
    reference_paths = expand_reference_paths(args.reference, args.word_file)
    table = learn_unit_table(load_all_word_files(reference_paths))
    kana_to_id = {kana: unit_id for unit_id, kana in table.items()}
    words = []
    with open(args.tsv, encoding="utf-8") as handle:
        for line_number, line in enumerate(handle, 1):
            line = line.rstrip("\n")
            if not line:
                continue
            fields = line.split("\t")
            group, display = fields[0], fields[1]
            reading = fields[2] if len(fields) > 2 and fields[2] else display
            if not args.halfwidth_display:
                display = to_fullwidth_text(display)
            try:
                ids = reading_text_to_ids(reading, kana_to_id)
            except ValueError as error:
                sys.exit(f"line {line_number}: {error}")
            words.append({"group": int(group), "display": display, "reading_ids": ids})
    with open(args.output, "wb") as handle:
        handle.write(serialize_word_file(words))
    print(f"wrote {len(words)} words to {args.output}")


def list_word_file_names(directory):
    names = sorted(os.listdir(directory))
    return [name for name in names if name.lower().endswith(".bin") and name != "font.bin"]


def command_words_export_all(args):
    names = list_word_file_names(args.directory)
    parsed = {name: parse_word_file(open(os.path.join(args.directory, name), "rb").read()) for name in names}
    table = learn_unit_table(parsed.values())
    romaji_table = load_romaji_table()
    total = 0
    with open(args.tsv, "w", encoding="utf-8", newline="\n") as handle:
        for name in names:
            for word in parsed[name]:
                reading = reading_ids_to_text(word["reading_ids"], table)
                line = f"{name}\t{word['group']}\t{word['display']}\t{reading}"
                if romaji_table:
                    line += f"\t{romaji_preview(word['reading_ids'], romaji_table)}"
                handle.write(line + "\n")
                total += 1
    print(f"exported {total} words from {len(names)} files")


def command_words_import_all(args):
    names = list_word_file_names(args.directory)
    table = learn_unit_table(
        parse_word_file(open(os.path.join(args.directory, name), "rb").read()) for name in names
    )
    kana_to_id = {kana: unit_id for unit_id, kana in table.items()}
    words_by_file = {}
    with open(args.tsv, encoding="utf-8") as handle:
        for line_number, line in enumerate(handle, 1):
            line = line.rstrip("\n")
            if not line:
                continue
            fields = line.split("\t")
            name, group, display = fields[0], fields[1], fields[2]
            reading = fields[3] if len(fields) > 3 and fields[3] else display
            if not args.halfwidth_display:
                display = to_fullwidth_text(display)
            try:
                ids = reading_text_to_ids(reading, kana_to_id)
            except ValueError as error:
                sys.exit(f"line {line_number}: {error}")
            words_by_file.setdefault(name, []).append(
                {"group": int(group), "display": display, "reading_ids": ids}
            )
    unknown = [name for name in words_by_file if name not in names]
    if unknown:
        sys.exit(f"unknown files in TSV: {unknown[:5]}")
    for name, words in words_by_file.items():
        with open(os.path.join(args.directory, name), "wb") as handle:
            handle.write(serialize_word_file(words))
    print(f"wrote {len(words_by_file)} files")


def command_words_verify(args):
    for path in args.word_files:
        original = open(path, "rb").read()
        rebuilt = serialize_word_file(parse_word_file(original))
        print(f"{path}: {'round-trip OK' if rebuilt == original else 'MISMATCH'}")


def build_argument_parser():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)

    hab_list = commands.add_parser("list")
    hab_list.add_argument("archive")
    hab_list.set_defaults(handler=command_hab_list)

    hab_extract = commands.add_parser("extract")
    hab_extract.add_argument("archive")
    hab_extract.add_argument("output_dir")
    hab_extract.set_defaults(handler=command_hab_extract)

    hab_pack = commands.add_parser("pack")
    hab_pack.add_argument("input_dir")
    hab_pack.add_argument("archive")
    hab_pack.set_defaults(handler=command_hab_pack)

    words_export = commands.add_parser("words-export")
    words_export.add_argument("word_file")
    words_export.add_argument("tsv")
    words_export.add_argument("--reference", nargs="*")
    words_export.set_defaults(handler=command_words_export)

    words_import = commands.add_parser("words-import")
    words_import.add_argument("tsv")
    words_import.add_argument("word_file")
    words_import.add_argument("output")
    words_import.add_argument("--reference", nargs="*")
    words_import.add_argument("--halfwidth-display", action="store_true")
    words_import.set_defaults(handler=command_words_import)

    tex_unpack = commands.add_parser("tex-unpack")
    tex_unpack.add_argument("input_dir")
    tex_unpack.add_argument("output_dir")
    tex_unpack.set_defaults(handler=command_tex_unpack)

    tex_table = commands.add_parser("tex-table")
    tex_table.add_argument("table_dll")
    tex_table.set_defaults(handler=command_tex_table)

    tex_export = commands.add_parser("tex-export")
    tex_export.add_argument("raw_dir")
    tex_export.add_argument("png_dir")
    tex_export.add_argument("--bank", nargs="*")
    tex_export.set_defaults(handler=command_tex_export)

    tex_import = commands.add_parser("tex-import")
    tex_import.add_argument("png_dir")
    tex_import.add_argument("raw_dir")
    tex_import.add_argument("--bank", nargs="*")
    tex_import.set_defaults(handler=command_tex_import)

    tex_pack = commands.add_parser("tex-pack")
    tex_pack.add_argument("raw_dir")
    tex_pack.add_argument("output_dir")
    tex_pack.set_defaults(handler=command_tex_pack)

    romaji_command = commands.add_parser("romaji-table")
    romaji_command.add_argument("exe")
    romaji_command.set_defaults(handler=command_romaji_table)

    words_export_all = commands.add_parser("words-export-all")
    words_export_all.add_argument("directory")
    words_export_all.add_argument("tsv")
    words_export_all.set_defaults(handler=command_words_export_all)

    words_import_all = commands.add_parser("words-import-all")
    words_import_all.add_argument("tsv")
    words_import_all.add_argument("directory")
    words_import_all.add_argument("--halfwidth-display", action="store_true")
    words_import_all.set_defaults(handler=command_words_import_all)

    words_verify = commands.add_parser("words-verify")
    words_verify.add_argument("word_files", nargs="+")
    words_verify.set_defaults(handler=command_words_verify)

    return parser


def main():
    args = build_argument_parser().parse_args()
    args.handler(args)


if __name__ == "__main__":
    main()