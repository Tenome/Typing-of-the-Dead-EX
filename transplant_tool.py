import argparse
import collections
import json
import os
import re
import sys

import hab_tool
import word_tool

FAMILY_PAIRS = {
    "S000": "Z000",
    "SE00": "ZE00",
    "SE01": "ZE01",
    "SE02": "ZE02",
    "SE03": "ZE03",
    "SE04": "ZE04",
    "SE05": "ZE05",
    "SE06": "ZE06",
    "SE07": "ZE07",
}
QUIZ_FAMILIES = {"SE01", "SE06", "SE08"}
WORD_BANK_FAMILY = "S000"
QUESTION_GROUP = 12
QUESTION_KEY = "?"
DEFAULT_MAX_DISPLAY = 24
FIRST_JAPANESE_SYMBOL_ID = 266
FULLWIDTH_EXCEPTIONS = {'"': "\u201d"}


def file_number(name):
    return int(re.search(r"L(\d+)\.bin$", name).group(1))


def family_of(name):
    return name[:4]


def to_fullwidth_display(text):
    pieces = []
    for character in text:
        if character in FULLWIDTH_EXCEPTIONS:
            pieces.append(FULLWIDTH_EXCEPTIONS[character])
        else:
            pieces.append(hab_tool.to_fullwidth(character))
    return "".join(pieces)


def list_names_by_family(names):
    families = collections.defaultdict(list)
    for name in names:
        families[family_of(name)].append(name)
    for members in families.values():
        members.sort(key=file_number)
    return families


def japanese_symbol_ids(japanese_files):
    table = hab_tool.load_romaji_table()
    usage = collections.Counter()
    for path in japanese_files:
        for word in hab_tool.parse_word_file(open(path, "rb").read()):
            usage.update(unit for unit in word["reading_ids"] if unit >= FIRST_JAPANESE_SYMBOL_ID)
    best = {}
    for unit_id, entry in table.items():
        if unit_id < FIRST_JAPANESE_SYMBOL_ID:
            continue
        key = entry["spellings"][0]
        if len(key) != 1 or not key.isascii() or key.isalnum():
            continue
        if key not in best or usage[unit_id] > usage[best[key]]:
            best[key] = unit_id
    return best


def english_character_of(unit):
    index = unit - word_tool.FIRST_UNIT_ID
    if not 0 <= index < len(word_tool.UNIT_CHARACTERS):
        raise ValueError(f"unknown English typing ID {unit}")
    return word_tool.UNIT_CHARACTERS[index]


def japanese_reading_text(english_units, symbol_ids):
    pieces = []
    for unit in english_units:
        character = english_character_of(unit)
        if character.isalnum() or character == " ":
            pieces.append(character)
        elif character in symbol_ids:
            pieces.append(f"<{symbol_ids[character]}>")
        else:
            raise ValueError(f"no Japanese typing ID for {character!r}")
    return "".join(pieces)


def convert_english_file(path, family, symbol_ids, halfwidth):
    words, _ = word_tool.parse_word_file(open(path, "rb").read())
    converted = []
    for index, word in enumerate(words):
        display = word["display"].decode("latin-1")
        is_question = family in QUIZ_FAMILIES and index == 0
        if is_question:
            group = QUESTION_GROUP
            reading = f"<{symbol_ids[QUESTION_KEY]}>"
        else:
            group = word["group"]
            reading = japanese_reading_text(word["typed"], symbol_ids)
        shown = display if halfwidth else to_fullwidth_display(display)
        converted.append({"group": group, "display": shown, "reading": reading, "length": len(display)})
    return converted


def assign_english_files(japanese_names, english_names):
    english_by_number = {file_number(name): name for name in english_names}
    assignment = {}
    used = set()
    for name in japanese_names:
        match = english_by_number.get(file_number(name))
        if match:
            assignment[name] = (match, "same id")
            used.add(match)
    spare = [name for name in english_names if name not in used]
    pool = spare or list(english_names)
    cursor = 0
    for name in japanese_names:
        if name in assignment or not english_names:
            continue
        if cursor < len(spare):
            assignment[name] = (spare[cursor], "unused english file")
        else:
            assignment[name] = (english_names[cursor % len(english_names)], "reused english file")
        cursor += 1
    return assignment


def command_convert(args):
    japanese_files = hab_tool.list_word_file_names(args.japanese_dir)
    japanese_paths = [os.path.join(args.japanese_dir, name) for name in japanese_files]
    symbol_ids = japanese_symbol_ids(japanese_paths)
    english_names = [
        os.path.basename(path)
        for path in word_tool.list_word_paths(args.english_dir)
        if re.search(r"L\d+\.bin$", path)
    ]
    english_families = list_names_by_family(english_names)
    japanese_families = list_names_by_family(japanese_files)

    rows = []
    summary = collections.defaultdict(collections.Counter)
    skipped_files = []
    for japanese_family, members in sorted(japanese_families.items()):
        english_family = FAMILY_PAIRS.get(japanese_family)
        if english_family is None or english_family not in english_families:
            summary[japanese_family]["kept japanese (no english family)"] += len(members)
            continue
        assignment = assign_english_files(members, english_families[english_family])
        for japanese_name in members:
            english_name, how = assignment[japanese_name]
            converted = convert_english_file(
                os.path.join(args.english_dir, english_name), japanese_family, symbol_ids, args.halfwidth
            )
            too_long = [word for word in converted if word["length"] > args.max_display > 0]
            if too_long and japanese_family == WORD_BANK_FAMILY:
                converted = [word for word in converted if word not in too_long]
                summary[japanese_family][f"words dropped as too long"] += len(too_long)
            elif too_long:
                summary[japanese_family]["files kept japanese (line too long)"] += 1
                skipped_files.append((japanese_name, english_name, max(word["length"] for word in too_long)))
                continue
            summary[japanese_family][f"files replaced ({how})"] += 1
            for word in converted:
                rows.append((japanese_name, word["group"], word["display"], word["reading"]))
    with open(args.tsv, "w", encoding="utf-8", newline="\n") as handle:
        for name, group, display, reading in rows:
            handle.write(f"{name}\t{group}\t{display}\t{reading}\n")
    print(f"wrote {len(rows)} lines to {args.tsv}")
    for family in sorted(summary):
        print(f"  {family}: " + "; ".join(f"{count} {label}" for label, count in summary[family].items()))
    if skipped_files and args.verbose:
        for japanese_name, english_name, longest in skipped_files:
            print(f"  skipped {japanese_name} <- {english_name}: longest line {longest} characters")


def build_argument_parser():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    convert = commands.add_parser("convert")
    convert.add_argument("english_dir")
    convert.add_argument("japanese_dir")
    convert.add_argument("tsv")
    convert.add_argument("--max-display", type=int, default=DEFAULT_MAX_DISPLAY)
    convert.add_argument("--halfwidth", action="store_true")
    convert.add_argument("--verbose", action="store_true")
    convert.set_defaults(handler=command_convert)
    return parser


def main():
    args = build_argument_parser().parse_args()
    args.handler(args)


if __name__ == "__main__":
    main()
