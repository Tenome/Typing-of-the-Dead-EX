r"""Typing Space Harrier word banks -> todmod word list.

  python tsh_to_word_list.py <TSH WORD folder> <out word list.txt>

Reads WORD\word_NN.dat from the user's own copy of Typing Space Harrier (plain or obfuscated, see
re_tsh/tools/tsh_words.py), and writes a UTF-8 list "display<TAB>reading" with the kana readings the game
types. Feed it to "New pack from word list..." in todmod_config.exe (or tests/build_pack_cli).
Single-letter banks and one-entry placeholder banks are skipped; duplicates are removed.
"""
import os
import sys
import unicodedata

KEY = b"God's in His Heaven, all's right with the world."
MIN_BANK_ENTRIES = 30


def decode_bank(data):
    if data[:2] != b"\r\n":
        data = bytes(c ^ KEY[i % len(KEY)] ^ 0xA7 for i, c in enumerate(data))
    return data.decode("cp932", errors="replace")


def bank_entries(text):
    for line in text.split("\r\n"):
        parts = line.split("\t")
        if len(parts) != 2:
            continue
        reading, display = (unicodedata.normalize("NFKC", p).strip() for p in parts)
        if reading and display:
            yield display, reading


def main(word_folder, out_path):
    seen, lines, banks = set(), [], 0
    for name in sorted(os.listdir(word_folder)):
        if not (name.lower().startswith("word_") and name.lower().endswith(".dat")):
            continue
        entries = list(bank_entries(decode_bank(open(os.path.join(word_folder, name), "rb").read())))
        if len(entries) < MIN_BANK_ENTRIES or all(len(r) == 1 for _, r in entries):
            continue
        banks += 1
        for display, reading in entries:
            if (display, reading) in seen:
                continue
            seen.add((display, reading))
            lines.append(display if display == reading else f"{display}\t{reading}")
    with open(out_path, "w", encoding="utf-8", newline="\n") as out:
        out.write("# Typing Space Harrier word banks (display<TAB>kana reading)\n")
        out.write("\n".join(lines) + "\n")
    print(f"{len(lines)} words from {banks} banks -> {out_path}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
