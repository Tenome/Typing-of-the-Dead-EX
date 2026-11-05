"""Draw 2-bpp glyphs the way Tod.exe does (grey levels + 1 px black outline) into a PNG for inspection.

usage: python preview_glyphs.py out.png --rendered glyphs.bin [--font font.bin --table Table.dll --codes 8260,8281]
"""
import argparse
import struct
import zlib

SIZE, CELL, SCALE = 24, 28, 3
GLYPH_BYTES = SIZE * SIZE // 4
LEVEL_GREY = [None, 173, 82, 255]
BACKGROUND = (70, 90, 130)


def kanji_offsets(table_path):
    data = open(table_path, "rb").read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    count = struct.unpack_from("<H", data, pe + 6)[0]
    optional = struct.unpack_from("<H", data, pe + 20)[0]
    base = struct.unpack_from("<I", data, pe + 52)[0]
    rva = 0x10096010 - base
    for i in range(count):
        _, vsize, va, rsize, raw = struct.unpack_from("<8sIIII", data, pe + 24 + optional + i * 40)
        if va <= rva < va + max(vsize, rsize):
            return struct.unpack_from(f"<{0xEAA4 - 0x8140 + 1}H", data, rva - va + raw)
    raise ValueError("kanji_offset not found")


def font_glyphs(font_path, table_path, codes):
    font, offsets = open(font_path, "rb").read(), kanji_offsets(table_path)
    for code in codes:
        start = offsets[code - 0x8140] * 32 + 16
        yield font[start:start + GLYPH_BYTES]


def levels(glyph):
    return [[(glyph[(y * SIZE + x) // 4] >> (6 - 2 * (x % 4))) & 3 for x in range(SIZE)] for y in range(SIZE)]


def draw(rows_of_glyphs):
    width = max(len(row) for row in rows_of_glyphs) * CELL
    height = len(rows_of_glyphs) * CELL
    image = [[BACKGROUND] * width for _ in range(height)]
    for row_index, glyphs in enumerate(rows_of_glyphs):
        for column, glyph in enumerate(glyphs):
            grid, ox, oy = levels(glyph), column * CELL + 2, row_index * CELL + 2
            for y in range(SIZE):
                for x in range(SIZE):
                    if grid[y][x]:
                        for dy in (-1, 0, 1):
                            for dx in (-1, 0, 1):
                                if image[oy + y + dy][ox + x + dx] == BACKGROUND:
                                    image[oy + y + dy][ox + x + dx] = (0, 0, 0)
            for y in range(SIZE):
                for x in range(SIZE):
                    if grid[y][x]:
                        grey = LEVEL_GREY[grid[y][x]]
                        image[oy + y][ox + x] = (grey, grey, grey)
    return [[pixel for pixel in row for _ in range(SCALE)] for row in image for _ in range(SCALE)]


def write_png(path, image):
    raw = b"".join(b"\0" + bytes(channel for pixel in row for channel in pixel) for row in image)
    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))
    header = struct.pack(">IIBBBBB", len(image[0]), len(image), 8, 2, 0, 0, 0)
    with open(path, "wb") as handle:
        handle.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("png")
    parser.add_argument("--rendered", required=True)
    parser.add_argument("--font")
    parser.add_argument("--table")
    parser.add_argument("--codes", default="")
    args = parser.parse_args()
    rendered = open(args.rendered, "rb").read()
    rows = [[rendered[i:i + GLYPH_BYTES] for i in range(start, min(start + 16 * GLYPH_BYTES, len(rendered)), GLYPH_BYTES)]
            for start in range(0, len(rendered), 16 * GLYPH_BYTES)]
    if args.font and args.codes:
        codes = [int(code, 16) for code in args.codes.split(",")]
        rows.insert(0, list(font_glyphs(args.font, args.table, codes)))
    write_png(args.png, draw(rows))


if __name__ == "__main__":
    main()
