"""Reads The House of the Dead (PC) VMC texture files: PIFF/VVTP with one BODY per 256x256 ARGB1555 page."""
import struct

PAGE_HEADER = 0x40


def read_pages(path):
    """Returns {page number: (width, height, raw 16-bit pixels, kind)}."""
    data = open(path, 'rb').read()
    if data[:4] != b'PIFF' or data[8:12] != b'VVTP':
        raise ValueError(path + ' is not a VMC file')
    pages = {}
    offset = 12
    while offset + 8 <= len(data):
        tag = data[offset:offset + 4]
        size = struct.unpack_from('<I', data, offset + 4)[0]
        if tag == b'BODY':
            header = data[offset + 8:offset + 8 + PAGE_HEADER]
            number, width, height, kind, bits = struct.unpack_from('<IHHHH', header, 4)
            pixels = data[offset + 8 + PAGE_HEADER:offset + 8 + PAGE_HEADER + width * height * bits // 8]
            pages[number] = (width, height, pixels, kind)
        offset += 8 + size + (size & 1)
    return pages


def page_rgba(width, height, pixels):
    """ARGB1555 -> RGBA bytes."""
    out = bytearray(width * height * 4)
    for i, value in enumerate(struct.unpack('<%dH' % (width * height), pixels)):
        red, green, blue = (value >> 10) & 31, (value >> 5) & 31, value & 31
        out[i * 4:i * 4 + 4] = bytes(((red * 255 + 15) // 31, (green * 255 + 15) // 31, (blue * 255 + 15) // 31,
                                      255 if value & 0x8000 else 0))
    return bytes(out)
