r"""Make a todmod texture pack from the English Typing of the Dead: every texture the English version changed
(translated menus, captions, signs) is written as a PNG that replaces the Japanese texture in the game.

Only 2D screen banks (scr_*, tod_*) and a few 3D textures with text are used: other 3D differences between the
versions are art changes (signs, effects, blood colour), not translations. The bank and texture lists below
leave out screens checked by eye where the English version has different art or credits, not a translation.
Composed textures put an English texture on a canvas of the EX texture's size where EX kept Japanese that the
English version replaced by another texture (the "残り n 体" counter becomes "Rest n"). Where the versions lay a screen out differently, textures are paired in order by size and image similarity.
4-bit palette textures take their palette from a block in Tod_e.exe (one palette per bank, chosen by matching
against EX); banks without a known palette are skipped.

usage: python english_textures.py "The Typing of The Dead" "EX Installation\TODEX" "EX Installation\TODEX\textures\english"
"""
import os
import sys

import numpy
from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import texture_formats as tf  # noqa: E402

ENGLISH_EXE_TIMESTAMP = 0x3B5B8887
ENGLISH_NAMES_VA = 0x4F0108
ENGLISH_HEADERS_VA = 0x5872C8
ENGLISH_PALETTES_FILE_OFFSET = 0x113630
ENGLISH_PALETTE_COUNT = 111
BANK_PALETTES = {"scr_tod_les_03.bin": 24, "scr_tod_les_com.bin": 24, "scr_training_sele.bin": 17, "scr_tod_org_p2.bin": 17}
ITEM_CARD_PREFIX, ITEM_CARD_PALETTE = "scr_tod_itm_", 52
VISIBLE_DIFFERENCE = 48
CHANGED_SHARE = 0.01
MATCH_BONUS = 0.2
MINIMUM_SIMILARITY = 0.3
NEUTRAL_SIMILARITY = 0.0
SKIPPED_BANKS = {
    "scr_blood_green.bin", "scr_blood_red.bin", "tod_les_blaster.bin", "tod_les_notepc.bin",
    "scr_title.bin", "scr_boss.bin", "scr_tod_res_boss.bin", "scr_tod_ext01.bin", "scr_trn_byo.bin",
    "scr_training_game.bin", "tod_boxetc.bin", "tod_etc2.bin", "scr_vmc.bin", "scr_vmc_cant.bin", "scr_vmc_ctrl.bin",
}
ONLY_TEXTURES = {"scr_tod_ext02.bin": {46}, "scr_tod_ext04.bin": {7, 8, 9}}
GENERATED_SCALE = 2
GENERATED_FONT = "arialbd.ttf"
GENERATED_TEXTURES = {("scr_common.bin", 23): ("Press Enter to skip", (240, 168, 96), 19, 24)}
COMPOSED_TEXTURES = {("scr_tod_time.bin", 17): 33, ("scr_tod_time.bin", 18): None}
SKIPPED_TEXTURES = {("scr_tod_les_fukidasi.bin", 5), ("scr_tod_org_p2.bin", 6)}
TEXT_BANK_PREFIXES = ("scr_", "tod_")
EXTRA_TEXTURES = {("boss6.bin", 0)}


def english_palettes(exe_path):
    blob = open(exe_path, "rb").read()
    return [tf.pixels_to_rgba(numpy.frombuffer(blob, dtype="<u2", count=16, offset=ENGLISH_PALETTES_FILE_OFFSET + 32 * i), 0)
            for i in range(ENGLISH_PALETTE_COUNT)]


def bank_palette(name, palettes):
    """The 4-bit palette the English version uses for this bank's textures (found by matching against EX art)."""
    if name.startswith(ITEM_CARD_PREFIX):
        return palettes[ITEM_CARD_PALETTE]
    return palettes[BANK_PALETTES[name]] if name in BANK_PALETTES else None


def english_texture_table(exe_path):
    executable = tf.PortableExecutable(exe_path)
    if executable.timestamp != ENGLISH_EXE_TIMESTAMP:
        sys.exit(f"{exe_path} is not the supported English build")
    return tf.read_texture_table(executable, ENGLISH_NAMES_VA, ENGLISH_HEADERS_VA)


def premultiplied(rgba):
    rgba = rgba.astype(numpy.int32)
    return numpy.concatenate([rgba[..., :3] * rgba[..., 3:] // 255, rgba[..., 3:]], axis=-1)


def differs(japanese, english):
    difference = numpy.abs(premultiplied(japanese) - premultiplied(english)).max(axis=-1)
    return (difference > VISIBLE_DIFFERENCE).mean() > CHANGED_SHARE


def signature(rgba):
    if rgba is None:
        return None
    small = numpy.asarray(Image.fromarray(rgba, "RGBA").resize((32, 32), Image.BILINEAR)).astype(float)
    luminance = small[..., :3].mean(axis=-1) * small[..., 3] / 255
    return numpy.concatenate([luminance.ravel(), small[..., 3].ravel() * 0.5])


def similarity(first, second):
    if first is None or second is None:
        return NEUTRAL_SIMILARITY
    first, second = first - first.mean(), second - second.mean()
    norms = numpy.linalg.norm(first) * numpy.linalg.norm(second)
    if norms == 0:
        return 1.0 if numpy.allclose(first, second) else 0.0
    return float(first @ second / norms)


def aligned_pairs(japanese_layout, english_layout, japanese_images, english_images):
    """Pairs (japanese index, english index) of same-size textures, in order, maximising image similarity."""
    if [t[:2] for t in japanese_layout] == [t[:2] for t in english_layout]:
        return list(zip(range(len(japanese_layout)), range(len(english_layout))))
    japanese_signatures = [signature(image) for image in japanese_images]
    english_signatures = [signature(image) for image in english_images]
    rows, columns = len(japanese_layout), len(english_layout)
    score = numpy.zeros((rows + 1, columns + 1))
    step = numpy.zeros((rows + 1, columns + 1), dtype=numpy.int8)
    step[1:, 0], step[0, 1:] = 1, 2
    for i in range(1, rows + 1):
        for j in range(1, columns + 1):
            best, move = (score[i - 1][j], 1) if score[i - 1][j] >= score[i][j - 1] else (score[i][j - 1], 2)
            if japanese_layout[i - 1][:2] == english_layout[j - 1][:2]:
                matched = score[i - 1][j - 1] + MATCH_BONUS + similarity(japanese_signatures[i - 1], english_signatures[j - 1])
                if matched > best:
                    best, move = matched, 3
            score[i][j], step[i][j] = best, move
    pairs, i, j = [], rows, columns
    while i > 0 or j > 0:
        if step[i][j] == 3:
            if similarity(japanese_signatures[i - 1], english_signatures[j - 1]) >= MINIMUM_SIMILARITY:
                pairs.append((i - 1, j - 1))
            i, j = i - 1, j - 1
        elif step[i][j] == 1:
            i -= 1
        else:
            j -= 1
    return pairs[::-1]


def decode_all(layout, raw, palette=None):
    return [tf.decode_texture(raw[t[4]:], *t[:4], palette=palette) for t in layout]


def convert_bank(name, japanese_layout, english_layout, japanese_raw, english_raw, out_dir, palette):
    japanese_images = decode_all(japanese_layout, japanese_raw)
    english_images = decode_all(english_layout, english_raw, palette)
    written = palette = 0
    for japanese_index, english_index in aligned_pairs(japanese_layout, english_layout, japanese_images, english_images):
        if not name.startswith(TEXT_BANK_PREFIXES) and (name, japanese_index) not in EXTRA_TEXTURES:
            continue
        if (name, japanese_index) in SKIPPED_TEXTURES or japanese_index not in ONLY_TEXTURES.get(name, {japanese_index}):
            continue
        english, japanese = english_images[english_index], japanese_images[japanese_index]
        if english is None:
            palette += 1
            continue
        if japanese is not None and not differs(japanese, english):
            continue
        folder = os.path.join(out_dir, name[:-4])
        os.makedirs(folder, exist_ok=True)
        Image.fromarray(english, "RGBA").save(os.path.join(folder, f"{japanese_index:03d}.png"))
        written += 1
    for (bank, japanese_index), (text, colour, left, size) in GENERATED_TEXTURES.items():
        if bank == name:
            write_generated_texture(name, japanese_layout[japanese_index], japanese_index, text, colour, left, size, out_dir)
            written += 1
    for (bank, japanese_index), english_index in COMPOSED_TEXTURES.items():
        if bank == name:
            write_composed_texture(name, japanese_layout[japanese_index], japanese_index, english_images, english_index, out_dir)
            written += 1
    return written, palette


def write_generated_texture(name, japanese_texture, japanese_index, text, colour, left, size, out_dir):
    """English text drawn in the style of the EX caption it replaces (outlined, left-aligned), at twice the size."""
    width, height = (side * GENERATED_SCALE for side in japanese_texture[:2])
    canvas = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    font = ImageFont.truetype(GENERATED_FONT, size * GENERATED_SCALE)
    draw = ImageDraw.Draw(canvas)
    top = (height - (font.getbbox(text)[3] - font.getbbox(text)[1])) // 2 - font.getbbox(text)[1]
    draw.text((left * GENERATED_SCALE, top), text, font=font, fill=colour + (255,),
              stroke_width=2 * GENERATED_SCALE, stroke_fill=(16, 12, 8, 255))
    folder = os.path.join(out_dir, name[:-4])
    os.makedirs(folder, exist_ok=True)
    canvas.save(os.path.join(folder, f"{japanese_index:03d}.png"))


def write_composed_texture(name, japanese_texture, japanese_index, english_images, english_index, out_dir):
    """An English texture placed on a transparent canvas of the EX texture's size (None: left empty)."""
    width, height = japanese_texture[:2]
    canvas = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    if english_index is not None:
        part = Image.fromarray(english_images[english_index], "RGBA")
        canvas.paste(part, (0, (height - part.height) // 2))
    folder = os.path.join(out_dir, name[:-4])
    os.makedirs(folder, exist_ok=True)
    canvas.save(os.path.join(folder, f"{japanese_index:03d}.png"))


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    english_dir, japanese_dir, out_dir = sys.argv[1:]
    english_table = english_texture_table(os.path.join(english_dir, "Tod_e.exe"))
    palettes = english_palettes(os.path.join(english_dir, "Tod_e.exe"))
    japanese_table = tf.japanese_texture_table(os.path.join(japanese_dir, "Table.dll"))
    japanese_banks = tf.read_hab(os.path.join(japanese_dir, "tex.HAB"))
    english_tex = os.path.join(english_dir, "tex")
    english_files = {name.lower(): name for name in os.listdir(english_tex)}
    totals = {"textures": 0, "banks": 0, "palette": 0}
    for name in sorted(set(japanese_table) & set(english_table)):
        if name in SKIPPED_BANKS or name not in japanese_banks or name.lower() not in english_files:
            continue
        if not name.startswith(TEXT_BANK_PREFIXES) and not any(bank == name for bank, _ in EXTRA_TEXTURES):
            continue
        english_raw = tf.bank_bytes(open(os.path.join(english_tex, english_files[name.lower()]), "rb").read())
        japanese_raw = tf.bank_bytes(japanese_banks[name])
        written, palette = convert_bank(name, japanese_table[name], english_table[name], japanese_raw, english_raw, out_dir,
                                        bank_palette(name, palettes))
        totals["palette"] += palette
        if written:
            totals["textures"] += written
            totals["banks"] += 1
            print(f"  {name}: {written} textures")
    print(f"wrote {totals['textures']} textures in {totals['banks']} banks to {out_dir}")
    if totals["palette"]:
        print(f"left out {totals['palette']} English 4-bit palette textures without a known palette (most show the same picture as EX)")


if __name__ == "__main__":
    main()
