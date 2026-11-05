"""Writes subtitles\\english.tsv from your English Typing of the Dead (Tod_e.exe).

The English version's subtitle table has nine lines more than EX: the attract intro ("The Curien Mansion
Incident..."). todmod shows them, and uses english.tsv as the base for English mode.

    python tools\\english_subtitles.py "<English game>\\Tod_e.exe" "<EX game>\\Tod.exe" "<EX game>\\subtitles\\english.tsv"
"""
import struct
import sys

RECORD_SIZE = 0x40
TEXT_OFFSET = 4
TEXT_SIZE = 0x38
GAME_LINES = 366
INTRO_LINES = 9
EX_TEXT_VA = 0x0051EEAC
FIRST_KNOWN_LINE = 2
FIRST_KNOWN_TEXT = b"We're meeting G over there."


def section_offset(data, va):
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    count = struct.unpack_from('<H', data, pe + 6)[0]
    optional = struct.unpack_from('<H', data, pe + 20)[0]
    base = struct.unpack_from('<I', data, pe + 24 + 28)[0]
    for i in range(count):
        header = pe + 24 + optional + 40 * i
        size, address, raw_size, raw = struct.unpack_from('<IIII', data, header + 8)
        if base + address <= va < base + address + max(size, raw_size):
            return raw + va - base - address
    raise SystemExit('address %#x not in file' % va)


def text_at(data, offset):
    return data[offset:offset + TEXT_SIZE].split(b'\0')[0].decode('cp1252')


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    english = open(sys.argv[1], 'rb').read()
    ex = open(sys.argv[2], 'rb').read()
    known = english.find(FIRST_KNOWN_TEXT + b'\0')
    if known < 0:
        raise SystemExit('subtitle table not found in ' + sys.argv[1])
    english_text = known - FIRST_KNOWN_LINE * RECORD_SIZE
    ex_text = section_offset(ex, EX_TEXT_VA)
    if text_at(ex, ex_text + FIRST_KNOWN_LINE * RECORD_SIZE) != FIRST_KNOWN_TEXT.decode():
        raise SystemExit('unexpected Tod.exe build: ' + sys.argv[2])
    rows = []
    for line in range(GAME_LINES + INTRO_LINES):
        text = text_at(english, english_text + line * RECORD_SIZE)
        if line < GAME_LINES:
            original = text_at(ex, ex_text + line * RECORD_SIZE)
        else:
            original = '[Attract intro %d]' % (line - GAME_LINES + 1)
        if text and original:
            rows.append('%d\t%s\t%s' % (line, original, text))
    intro = [row for row in rows if '[Attract intro' in row]
    if len(intro) != INTRO_LINES:
        raise SystemExit('found %d of %d intro lines; is this the English Tod_e.exe?' % (len(intro), INTRO_LINES))
    with open(sys.argv[3], 'w', encoding='utf-8', newline='\n') as out:
        out.write('# English subtitles from the English Typing of the Dead (english_subtitles.py).\n')
        out.write('# Columns: line number, original, text. Lines 366-374 are the attract intro.\n')
        out.write('\n'.join(rows) + '\n')
    print('%s: %d lines (%d attract intro)' % (sys.argv[3], len(rows), len(intro)))


main()
