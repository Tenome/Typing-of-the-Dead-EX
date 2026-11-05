"""HAB0 archives, PRS decompression and Tod texture decoding (adapted from hab_tool.py)."""
import struct

import numpy

HAB_MAGIC = b"HAB0"
HAB_HEADER_SIZE = 0x20
HAB_INDEX_ENTRY_SIZE = 16
PIXEL_FORMAT_NAMES = {0: "argb1555", 1: "rgb565", 2: "argb4444"}
DATA_FORMAT_TWIDDLED_SQUARE = 1
DATA_FORMAT_VQ = 3
DATA_FORMAT_PALETTE4 = 5
DATA_FORMAT_RECTANGLE = 9
DATA_FORMAT_TWIDDLED_RECTANGLE = 13
VQ_CODEBOOK_BYTES = 2048


def read_hab(path):
    blob = open(path, "rb").read()
    if blob[:4] != HAB_MAGIC:
        raise ValueError(f"{path} is not a HAB0 archive")
    _, _, names_size, _ = struct.unpack_from("<4I", blob, 0x10)
    count = struct.unpack_from("<I", blob, 0x14)[0] & 0xFFFF
    names_start = HAB_HEADER_SIZE + count * HAB_INDEX_ENTRY_SIZE
    data_base = names_start + names_size
    items = {}
    for i in range(count):
        name_offset, offset, size, _ = struct.unpack_from("<4I", blob, HAB_HEADER_SIZE + i * HAB_INDEX_ENTRY_SIZE)
        name_at = names_start + name_offset
        name = blob[name_at:blob.index(b"\0", name_at)].decode("cp932")
        items[name] = blob[data_base + offset:data_base + offset + size]
    return items


def prs_decompress(data, start=0):
    output = bytearray()
    position, control, bits_left = start, 0, 0

    def next_bit():
        nonlocal control, bits_left, position
        if bits_left == 0:
            control, position, bits_left = data[position], position + 1, 8
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
            if length == 0:
                length = data[position] + 1
                position += 1
            else:
                length += 2
            distance = 0x2000 - (packed >> 3)
        else:
            length = ((next_bit() << 1) | next_bit()) + 2
            distance = 0x100 - data[position]
            position += 1
        source = len(output) - distance
        if source < 0:
            raise ValueError("invalid PRS back-reference")
        for offset in range(length):
            output.append(output[source + offset])
    return bytes(output)


def bank_bytes(content):
    """A texture bank file is either PRS-compressed (u32 size + stream) or stored as is."""
    if len(content) >= 8:
        declared = struct.unpack_from("<I", content, 0)[0]
        try:
            raw = prs_decompress(content, 4)
            if len(raw) == declared:
                return raw
        except (ValueError, IndexError):
            pass
    return content


def spread_bits(values, bit_count):
    spread = numpy.zeros_like(values)
    for bit in range(bit_count):
        spread |= ((values >> bit) & 1) << (2 * bit)
    return spread


def twiddle_index_map(width, height):
    rows, columns = numpy.mgrid[0:height, 0:width].astype(numpy.int64)
    side = min(width, height)
    bit_count = int(side).bit_length() - 1
    inner = (spread_bits(columns % side, bit_count) << 1) | spread_bits(rows % side, bit_count)
    if width > height:
        return inner + (columns // side) * side * side
    if height > width:
        return inner + (rows // side) * side * side
    return inner


def texture_byte_size(width, height, data_format):
    if data_format == DATA_FORMAT_VQ:
        return VQ_CODEBOOK_BYTES + width * height // 4
    if data_format == DATA_FORMAT_PALETTE4:
        return width * height // 2
    return width * height * 2


def pixels_to_rgba(values, pixel_format):
    values = values.astype(numpy.uint32)
    name = PIXEL_FORMAT_NAMES[pixel_format]
    if name == "argb4444":
        channels = [((values >> s) & 0xF) * 17 for s in (8, 4, 0, 12)]
    elif name == "rgb565":
        channels = [((values >> 11) & 0x1F) * 255 // 31, ((values >> 5) & 0x3F) * 255 // 63, (values & 0x1F) * 255 // 31,
                    numpy.full(values.shape, 255)]
    else:
        channels = [((values >> s) & 0x1F) * 255 // 31 for s in (10, 5, 0)] + [((values >> 15) & 1) * 255]
    return numpy.stack(channels, axis=-1).astype(numpy.uint8)


def decode_texture(data, width, height, pixel_format, data_format, palette=None):
    """RGBA image (top row first); 4-bit palette textures need palette (16 RGBA rows), else None."""
    if data_format == DATA_FORMAT_PALETTE4:
        if palette is None:
            return None
        packed = numpy.frombuffer(data[:width * height // 2], dtype=numpy.uint8)
        nibbles = numpy.stack([packed & 15, packed >> 4], axis=-1).reshape(-1)
        return palette[nibbles[twiddle_index_map(width, height)]].reshape(height, width, 4)[::-1]
    if data_format == DATA_FORMAT_RECTANGLE:
        values = numpy.frombuffer(data[:width * height * 2], dtype="<u2").reshape(height, width)
    elif data_format in (DATA_FORMAT_TWIDDLED_SQUARE, DATA_FORMAT_TWIDDLED_RECTANGLE):
        values = numpy.frombuffer(data[:width * height * 2], dtype="<u2")[twiddle_index_map(width, height)]
    elif data_format == DATA_FORMAT_VQ:
        codebook = numpy.frombuffer(data[:VQ_CODEBOOK_BYTES], dtype="<u2").reshape(256, 4)
        block_width, block_height = width // 2, height // 2
        indices = numpy.frombuffer(data[VQ_CODEBOOK_BYTES:VQ_CODEBOOK_BYTES + block_width * block_height], dtype=numpy.uint8)
        blocks = codebook[indices[twiddle_index_map(block_width, block_height)]]
        values = numpy.zeros((height, width), dtype=numpy.uint16)
        values[0::2, 0::2], values[1::2, 0::2] = blocks[..., 0], blocks[..., 1]
        values[0::2, 1::2], values[1::2, 1::2] = blocks[..., 2], blocks[..., 3]
    else:
        raise ValueError(f"unsupported data format {data_format}")
    return pixels_to_rgba(values[::-1], pixel_format)


class PortableExecutable:
    def __init__(self, path):
        self.blob = open(path, "rb").read()
        pe = struct.unpack_from("<I", self.blob, 0x3C)[0]
        self.timestamp = struct.unpack_from("<I", self.blob, pe + 8)[0]
        self.image_base = struct.unpack_from("<I", self.blob, pe + 52)[0]
        count = struct.unpack_from("<H", self.blob, pe + 6)[0]
        optional_size = struct.unpack_from("<H", self.blob, pe + 20)[0]
        self.sections = [struct.unpack_from("<8sIIII", self.blob, pe + 24 + optional_size + i * 40) for i in range(count)]
        self.export_rva = struct.unpack_from("<I", self.blob, pe + 24 + 96)[0]

    def offset(self, va):
        rva = va - self.image_base
        for _, virtual_size, virtual_address, raw_size, raw_offset in self.sections:
            if virtual_address <= rva < virtual_address + raw_size:
                return rva - virtual_address + raw_offset
        return None

    def u32(self, va):
        return struct.unpack_from("<I", self.blob, self.offset(va))[0]

    def string(self, va):
        start = self.offset(va)
        return self.blob[start:self.blob.index(b"\0", start)].decode("cp932")

    def exports(self):
        table = self.offset(self.image_base + self.export_rva)
        count, functions, names, ordinals = struct.unpack_from("<IIII", self.blob, table + 24)
        result = {}
        for i in range(count):
            name = self.string(self.image_base + struct.unpack_from("<I", self.blob, self.offset(self.image_base + names) + 4 * i)[0])
            ordinal = struct.unpack_from("<H", self.blob, self.offset(self.image_base + ordinals) + 2 * i)[0]
            result[name] = self.image_base + struct.unpack_from("<I", self.blob, self.offset(self.image_base + functions) + 4 * ordinal)[0]
        return result


def read_texture_table(executable, names_va, headers_va):
    """{bank name: [[width, height, pixel format, data format, offset, id], ...]}; bank 0 is unused."""
    banks = {}
    bank = 1
    while True:
        name_pointer, header_pointer = executable.u32(names_va + 4 * bank), executable.u32(headers_va + 4 * bank)
        if not name_pointer or executable.offset(name_pointer) is None or executable.offset(header_pointer) is None:
            break
        start, textures = executable.offset(header_pointer), []
        while True:
            width, height, pixel_format, data_format, _, offset, texture_id = struct.unpack_from(
                "<HHBBHII", executable.blob, start + 16 * len(textures))
            if width == 0:
                break
            textures.append([width, height, pixel_format, data_format, offset, texture_id])
        banks[executable.string(name_pointer)] = textures
        bank += 1
    return banks


def japanese_texture_table(table_dll):
    executable = PortableExecutable(table_dll)
    exports = executable.exports()
    return read_texture_table(executable, exports["TexFileNameTbl"], exports["TexHeaderTbl"])
