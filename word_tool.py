import argparse
import hashlib
import os
import string
import struct
import sys

DEFAULT_XOR_KEY = 0x41
RECORD_SIZE = 12
TERMINATOR_GROUP = 0xFFFFFFFF
FIRST_UNIT_ID = 162
UNIT_CHARACTERS = string.digits + string.ascii_uppercase + string.ascii_lowercase + "\\.,?!_[]-% '$#@.,-\"&/():*"
NON_WORD_FILES = {"font.bin"}
KEY_WEIGHTS = {
    " ": 2, "A": 3, "B": 4, "C": 5, "D": 4, "E": 4, "F": 4, "G": 4, "H": 3, "I": 4, "J": 4, "K": 4, "L": 4,
    "M": 5, "N": 5, "O": 4, "P": 6, "Q": 6, "R": 4, "S": 3, "T": 3, "U": 4, "V": 5, "W": 4, "X": 5, "Y": 4,
    "Z": 5, "0": 10, "1": 9, "2": 10, "3": 10, "4": 11, "5": 12, "6": 12, "7": 11, "8": 11, "9": 10,
    "!": 12, '"': 12, "#": 14, "$": 14, "%": 14, "&": 13, "'": 8, "(": 12, ")": 12, "*": 14, ",": 10,
    "-": 11, ".": 10, "/": 10, ":": 12, "?": 12, "@": 13,
}
AUTO_GROUP_WORDS = ("", "auto")


def build_unit_tables():
    encode = {character: FIRST_UNIT_ID + index for index, character in enumerate(UNIT_CHARACTERS)}
    decode = {
        FIRST_UNIT_ID + index: character
        for index, character in enumerate(UNIT_CHARACTERS)
        if encode[character] == FIRST_UNIT_ID + index
    }
    return encode, decode


ENCODE_UNIT, DECODE_UNIT = build_unit_tables()


def xor_bytes(data, key):
    return bytes(byte ^ key for byte in data)


def align_up(value, alignment):
    return (value + alignment - 1) & ~(alignment - 1)


def read_cstring(data, offset):
    return data[offset:data.index(b"\0", offset)]


def parse_decoded(data):
    words = []
    index = 0
    while True:
        group, display_at, typed_at = struct.unpack_from("<III", data, index * RECORD_SIZE)
        if group == TERMINATOR_GROUP:
            return words
        words.append(
            {
                "group": group,
                "display": read_cstring(data, display_at),
                "typed": list(read_cstring(data, typed_at)),
            }
        )
        index += 1


def parse_word_file(blob):
    for key in (DEFAULT_XOR_KEY, 0):
        try:
            words = parse_decoded(xor_bytes(blob, key))
        except (struct.error, ValueError):
            continue
        if words and all((word["group"] & 0xFFFF) < 10000 and (word["group"] >> 16) < 16 for word in words):
            return words, key
    raise ValueError("not a recognised word file")


def pool_slot(content):
    return content + b"\0" + b"\xff" * (align_up(len(content) + 1, 4) - len(content) - 1)


def serialize_word_file(words, key):
    pool_start = (len(words) + 1) * RECORD_SIZE
    records = bytearray()
    pool = bytearray()
    pointer_by_content = {}

    def place(content):
        if content not in pointer_by_content:
            pointer_by_content[content] = pool_start + len(pool)
            pool.extend(pool_slot(content))
        return pointer_by_content[content]

    for word in words:
        display_at = place(word["display"])
        typed_at = place(bytes(word["typed"]))
        records += struct.pack("<III", word["group"], display_at, typed_at)
    records += struct.pack("<III", TERMINATOR_GROUP, 0, 0)
    return xor_bytes(bytes(records + pool.rstrip(b"\xff")), key)


def typed_ids_to_text(typed_ids):
    return "".join(DECODE_UNIT.get(unit, f"<{unit}>") for unit in typed_ids)


def typed_text_to_ids(text):
    ids = []
    position = 0
    while position < len(text):
        if text[position] == "<" and ">" in text[position:]:
            close = text.index(">", position)
            token = text[position + 1:close]
            if token.isdigit() and int(token) < 256:
                ids.append(int(token))
                position = close + 1
                continue
        character = text[position]
        if character not in ENCODE_UNIT:
            raise ValueError(f"no typing ID known for character {character!r}")
        ids.append(ENCODE_UNIT[character])
        position += 1
    return ids


def typing_score(typed_ids):
    total = 0
    for unit in typed_ids:
        character = DECODE_UNIT.get(unit)
        if character is None or character.upper() not in KEY_WEIGHTS:
            raise ValueError(f"no difficulty weight known for typing ID {unit}; give an explicit group")
        total += KEY_WEIGHTS[character.upper()]
    return total


def display_to_bytes(text):
    try:
        return text.encode("latin-1")
    except UnicodeEncodeError as error:
        raise ValueError(f"display text must be Latin-1: {error.object[error.start]!r}") from None


def list_word_paths(location):
    if os.path.isfile(location):
        return [location]
    names = sorted(os.listdir(location))
    return [
        os.path.join(location, name)
        for name in names
        if name.lower().endswith(".bin") and name.lower() not in NON_WORD_FILES
    ]


def command_export(args):
    exported = 0
    skipped = []
    first_seen = {}
    with open(args.tsv, "w", encoding="utf-8", newline="\n") as handle:
        for path in list_word_paths(args.location):
            blob = open(path, "rb").read()
            try:
                words, _ = parse_word_file(blob)
            except ValueError:
                skipped.append(os.path.basename(path))
                continue
            name = os.path.basename(path)
            digest = hashlib.md5(blob).hexdigest()
            if digest in first_seen:
                print(f"note: {name} is byte-identical to {first_seen[digest]}")
            first_seen.setdefault(digest, name)
            for word in words:
                display = word["display"].decode("latin-1")
                typed = typed_ids_to_text(word["typed"])
                handle.write(f"{name}\t{word['group']}\t{display}\t{typed}\n")
                exported += 1
    print(f"exported {exported} words")
    if skipped:
        print(f"skipped (not word files): {', '.join(skipped)}")


def read_tsv(path):
    words_by_file = {}
    with open(path, encoding="utf-8") as handle:
        for line_number, line in enumerate(handle, 1):
            line = line.rstrip("\n")
            if not line:
                continue
            fields = line.split("\t")
            if len(fields) < 3:
                sys.exit(f"line {line_number}: expected file, group, display[, typed]")
            name, group, display = fields[0], fields[1].strip(), fields[2]
            typed_text = fields[3] if len(fields) > 3 and fields[3] else display
            try:
                typed = typed_text_to_ids(typed_text)
                word = {
                    "group": typing_score(typed) if group.lower() in AUTO_GROUP_WORDS else int(group),
                    "display": display_to_bytes(display),
                    "typed": typed,
                }
            except ValueError as error:
                sys.exit(f"line {line_number}: {error}")
            words_by_file.setdefault(name, []).append(word)
    return words_by_file


def warn_about_groups_outside_original_range(name, original_words, new_words):
    low = min(word["group"] for word in original_words)
    high = max(word["group"] for word in original_words)
    outside = [word for word in new_words if not low <= word["group"] <= high]
    if outside:
        examples = ", ".join(f"{word['display'].decode('latin-1')!r} ({word['group']})" for word in outside[:3])
        print(f"warning: {name}: {len(outside)} words have a group outside this file's original range {low}-{high}: {examples}")


def command_import(args):
    words_by_file = read_tsv(args.tsv)
    for name, words in words_by_file.items():
        path = os.path.join(args.directory, name)
        key = DEFAULT_XOR_KEY
        if os.path.exists(path):
            original_words, key = parse_word_file(open(path, "rb").read())
            warn_about_groups_outside_original_range(name, original_words, words)
        with open(path, "wb") as handle:
            handle.write(serialize_word_file(words, key))
    print(f"wrote {len(words_by_file)} files")


def command_check(args):
    for location in args.paths:
        for path in list_word_paths(location):
            try:
                words, _ = parse_word_file(open(path, "rb").read())
            except ValueError:
                continue
            differing = []
            for word in words:
                try:
                    expected = typing_score(word["typed"])
                except ValueError:
                    continue
                if expected != word["group"]:
                    differing.append((word, expected))
            print(f"{os.path.basename(path)}: {len(differing)} of {len(words)} groups differ from the computed score")
            if args.list:
                for word, expected in differing:
                    print(f"  group {word['group']} vs score {expected}: {word['display'].decode('latin-1')}")


def command_verify(args):
    for location in args.paths:
        for path in list_word_paths(location):
            original = open(path, "rb").read()
            try:
                words, key = parse_word_file(original)
            except ValueError:
                print(f"{path}: not a word file")
                continue
            same = serialize_word_file(words, key) == original
            print(f"{path}: {len(words)} words, xor {key:#04x}, {'round-trip OK' if same else 'MISMATCH'}")


def build_argument_parser():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)

    export = commands.add_parser("export")
    export.add_argument("location", help="a word file or a folder of them")
    export.add_argument("tsv")
    export.set_defaults(handler=command_export)

    import_ = commands.add_parser("import")
    import_.add_argument("tsv")
    import_.add_argument("directory")
    import_.set_defaults(handler=command_import)

    check = commands.add_parser("check")
    check.add_argument("paths", nargs="+")
    check.add_argument("--list", action="store_true")
    check.set_defaults(handler=command_check)

    verify = commands.add_parser("verify")
    verify.add_argument("paths", nargs="+")
    verify.set_defaults(handler=command_verify)
    return parser


def main():
    args = build_argument_parser().parse_args()
    args.handler(args)


if __name__ == "__main__":
    main()
