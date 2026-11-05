r"""Write a word pack for testing case-sensitive typing: every normal and quiz word list (main game and Kids' Mode)
and the built-in tables get mixed-case English words.

usage: python make_case_test_pack.py "...\TODEX\dict\case_test"
"""
import itertools
import os
import sys

TOOLS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools")
LIST_STRUCTURE = os.path.join(TOOLS, "word_lists.tsv")
WORDS = ("Apple bAnana Cat zomBie HOUSE eBay iPod McDonald PlayStation Tokyo NEW_York Dead aXe "
         "Gun Run Shoot Bite Fear Night Dark ROOM Door Key Blood Bone Grave Moon").split()
QUIZ = [("Which is spelt Right?", ["Tokyo", "Paris", "tokyo"]), ("Pick the Capital", ["London", "Berlin", "ROME"])]
BUILTINS = {
    "builtin_letters": list("ABCDEFGHIJKLMNPQRSTUWVXYZ"),
    "builtin_kana": "Ab bA Cd dC Ef fE Gh hG Ij jI Kl lK Mn nM Op pO Qr rQ St tS Uv vU Wx xW Yz zY "
                    "Am An As At Be By Do Go He Hi If In".split(),
    "builtin_short_words": "Ab bA Cd dC Ef fE Gh hG Ij jI Kl lK Mn nM Op pO Qr rQ St tS".split(),
    "builtin_drill": ["F", "J"] + list("1234567890") + list("'\"$?!%.,-:;()&@/"),
    "builtin_staff_stage": ["Staff Name"] * 27,
    "builtin_staff_kanji": ["Another Name"] * 27,
    "builtin_staff_kana": ["Third Name"] * 24,
}
HEADER = "# case test pack (make_case_test_pack.py): list<TAB>difficulty<TAB>display<TAB>reading\n"


def read_lists():
    for line in open(LIST_STRUCTURE, encoding="utf-8"):
        if line.startswith("#") or not line.strip():
            continue
        bank, name, kind, slots = line.rstrip("\n").split("\t")
        yield bank, name, kind, len([s for s in slots.split(",") if s])


def list_rows(name, kind, slot_count, words):
    if kind == "quiz":
        question, answers = QUIZ[hash(name) % len(QUIZ)]
        return [f"{name}\t12\t{question}\t?\n"] + [f"{name}\t\t{a}\n" for a in answers[: slot_count - 1]]
    return [f"{name}\t\t{next(words).replace('_', ' ')}\n" for _ in range(slot_count)]


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    pack_dir = sys.argv[1]
    os.makedirs(pack_dir, exist_ok=True)
    words = itertools.cycle(WORDS)
    files = {"main": [HEADER], "kids": [HEADER]}
    for bank, name, kind, slot_count in read_lists():
        if kind in ("normal", "quiz"):
            files[bank] += list_rows(name, kind, slot_count, words)
    files["main"] += [f"{name}\t\t{word}\n" for name, entries in BUILTINS.items() for word in entries]
    for bank, file_name in (("main", "words.tsv"), ("kids", "kids.tsv")):
        with open(os.path.join(pack_dir, file_name), "w", encoding="utf-8", newline="\n") as handle:
            handle.writelines(files[bank])
    print(f"wrote words.tsv and kids.tsv to {pack_dir}")


if __name__ == "__main__":
    main()
