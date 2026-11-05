"""Lists every voiced subtitle line of Tod.exe: how long the subtitle is shown, how long its voice file plays, and
the frame (counted down from the end) at which each English text part takes over.
Usage: subtitle_timing.py <TODEX folder>"""
import os
import struct
import sys
import wave

TODEX = sys.argv[1] if len(sys.argv) > 1 else '.'
EXE = os.path.join(TODEX, 'Tod.exe')
VOICE = os.path.join(TODEX, 'SOUND', 'VOICE')
data = open(EXE, 'rb').read()
pe = struct.unpack_from('<I', data, 0x3c)[0]
sections = struct.unpack_from('<H', data, pe + 6)[0]
opt = struct.unpack_from('<H', data, pe + 20)[0]
base = 0x400000
table = []
for i in range(sections):
    o = pe + 24 + opt + i * 40
    vsize, va, rsize, raw = struct.unpack_from('<IIII', data, o + 8)
    table.append((base + va, max(vsize, rsize), raw))

def off(addr):
    for va, size, raw in table:
        if va <= addr < va + size:
            return addr - va + raw
    raise ValueError(hex(addr))

def u16(a): return struct.unpack_from('<H', data, off(a))[0]
def s16(a): return struct.unpack_from('<h', data, off(a))[0]
def u32(a): return struct.unpack_from('<I', data, off(a))[0]
def cstr(a):
    o = off(a); e = data.index(b'\0', o); return data[o:e].decode('cp932', 'replace')

rows = []
for line in range(400):
    rec = 0x51cfd0 + line * 16
    sound = u32(rec + 0xc)
    frames = s16(rec + 2)
    if sound >> 28 != 2:
        continue
    name = cstr(0x53b1a4 + (sound & 0xfff) * 0x24)
    path = os.path.join(VOICE, name.replace('/', '\\'))
    try:
        with wave.open(path) as w:
            seconds = w.getnframes() / w.getframerate()
    except Exception as e:
        seconds = None
    parts = []
    for step in range(4):
        r = s16(0x524a28 + (line * 4 + step) * 2)
        if r < 0: break
        parts.append((r, s16(0x51eea8 + r * 0x40 + 0x3e), cstr(0x51eea8 + r * 0x40 + 4)))
    rows.append((line, name, frames, seconds, parts))

for line, name, frames, seconds, parts in rows:
    shown = frames / 60
    diff = None if seconds is None else shown - seconds
    flag = '' if diff is None or abs(diff) < 0.75 else ('  LONG' if diff > 0 else '  SHORT')
    print(f'{line:3d} {name:24s} shown {shown:5.2f}s voice {seconds if seconds is None else round(seconds,2)}{flag} parts={[(p[0], p[1]) for p in parts]}')
print(len(rows), 'voiced lines')
