"""Helpers to read thotd.exe by virtual address (Watcom LE-style PE with BEGTEXT/DGROUP)."""
import struct

EXE = None
SECTIONS = []


def load(path):
    global EXE, SECTIONS
    EXE = open(path, 'rb').read()
    pe = struct.unpack_from('<I', EXE, 0x3C)[0]
    count = struct.unpack_from('<H', EXE, pe + 6)[0]
    optional = struct.unpack_from('<H', EXE, pe + 20)[0]
    base = struct.unpack_from('<I', EXE, pe + 24 + 28)[0]
    SECTIONS = []
    for i in range(count):
        header = pe + 24 + optional + 40 * i
        _size, address, raw_size, raw = struct.unpack_from('<IIII', EXE, header + 8)
        SECTIONS.append((base + address, raw, raw_size))


def file_to_va(offset):
    for va, raw, size in SECTIONS:
        if raw <= offset < raw + size:
            return va + offset - raw
    return None


def va_to_file(va):
    for start, raw, size in SECTIONS:
        if start <= va < start + size:
            return raw + va - start
    return None


def read(va, size):
    offset = va_to_file(va)
    return EXE[offset:offset + size]


def words(va, count):
    """Each dword shown as int and, if plausible, float."""
    out = []
    for i in range(count):
        value = struct.unpack_from('<I', read(va + 4 * i, 4))[0]
        as_float = struct.unpack('<f', struct.pack('<I', value))[0]
        if value and 1e-4 < abs(as_float) < 1e5:
            out.append('%.4g' % as_float)
        else:
            out.append(str(value if value < 0x80000000 else value - 0x100000000))
    return out
