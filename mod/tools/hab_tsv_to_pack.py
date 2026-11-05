"""Convert a hab_tool.py words-export(-all) TSV into a todmod dictionary pack folder.

usage: python hab_tsv_to_pack.py words.tsv "TODEX/dict/<pack>" [--kids]
"""
import argparse
import os
from collections import defaultdict

HEADER = "# group<TAB>display<TAB>reading (reading optional: defaults to display)\n"


def read_items(tsv_path):
    items = defaultdict(list)
    with open(tsv_path, encoding="utf-8") as handle:
        for line in handle:
            fields = line.rstrip("\r\n").split("\t")
            if len(fields) < 4:
                continue
            item, group, display, reading = fields[:4]
            items[os.path.splitext(item)[0]].append((group, display, reading))
    return items


def write_pack(items, pack_dir):
    os.makedirs(pack_dir, exist_ok=True)
    for item, words in items.items():
        with open(os.path.join(pack_dir, item + ".txt"), "w", encoding="utf-8", newline="\n") as handle:
            handle.write(HEADER)
            handle.writelines(f"{group}\t{display}\t{reading}\n" for group, display, reading in words)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("tsv")
    parser.add_argument("pack_dir")
    parser.add_argument("--kids", action="store_true", help="write into the pack's kids subfolder")
    args = parser.parse_args()
    items = read_items(args.tsv)
    target = os.path.join(args.pack_dir, "kids") if args.kids else args.pack_dir
    write_pack(items, target)
    print(f"wrote {len(items)} items, {sum(map(len, items.values()))} words to {target}")


if __name__ == "__main__":
    main()
