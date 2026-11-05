"""Assemble the Modern Community Patch from the built mod (run build.sh first).

Writes dist/TODEX_Modern_Community_Patch_<version>/ and a .zip of it. Only the mod's own files go in:
no game files, word packs, dgVoodoo or extracted data.
"""
import os
import re
import shutil
import zipfile

MOD = os.path.dirname(os.path.abspath(__file__))
TEXT_SUFFIXES = (".bat", ".txt")

FILES = [
    ("patch/README.txt", "README.txt"),
    ("patch/install.bat", "install.bat"),
    ("patch/uninstall.bat", "uninstall.bat"),
    ("build/todmod_config.exe", "todmod_config.exe"),
    ("tools/play_two_copies.bat", "play_two_copies.bat"),
    ("docs/online_play.txt", "todmod/online_play.txt"),
    ("docs/gameplay_options.txt", "todmod/gameplay_options.txt"),
    ("tools/online_relay.py", "tools/online_relay.py"),
    ("build/Common.dll", "todmod/Common.dll"),
    ("docs/word_packs.txt", "todmod/word_packs.txt"),
    ("docs/texture_packs.txt", "todmod/texture_packs.txt"),
    ("docs/music_packs.txt", "todmod/music_packs.txt"),
    ("docs/model_packs.txt", "todmod/model_packs.txt"),
    ("docs/voice_packs.txt", "todmod/voice_packs.txt"),
    ("docs/subtitles.txt", "todmod/subtitles.txt"),
    ("third_party/README.txt", "todmod/third_party.txt"),
    ("third_party/minhook/LICENSE.txt", "todmod/MinHook-LICENSE.txt"),
    ("tools/english_to_pack.py", "tools/english_to_pack.py"),
    ("tools/obj_to_character.py", "tools/obj_to_character.py"),
    ("tools/gltf_to_character.py", "tools/gltf_to_character.py"),
    ("tools/tsh_to_word_list.py", "tools/tsh_to_word_list.py"),
    ("tools/subtitle_timing.py", "tools/subtitle_timing.py"),
    ("tools/word_lists.tsv", "tools/word_lists.tsv"),
    ("tools/reading_units.tsv", "tools/reading_units.tsv"),
    ("tools/english_textures.py", "tools/english_textures.py"),
    ("tools/english_subtitles.py", "tools/english_subtitles.py"),
    ("tools/texture_formats.py", "tools/texture_formats.py"),
    ("tools/model_formats.py", "tools/model_formats.py"),
    ("tools/model_tool.py", "tools/model_tool.py"),
    ("tools/character_kinds.py", "tools/character_kinds.py"),
    ("../hotd/tools/rbh.py", "tools/hotd/rbh.py"),
    ("../hotd/tools/vmc.py", "tools/hotd/vmc.py"),
    ("../hotd/tools/exe_view.py", "tools/hotd/exe_view.py"),
    ("../hotd/tools/skeletons.py", "tools/hotd/skeletons.py"),
    ("../hotd/tools/pose.py", "tools/hotd/pose.py"),
    ("../hotd/tools/assemble.py", "tools/hotd/assemble.py"),
    ("../hotd/tools/export_parts.py", "tools/hotd/export_parts.py"),
    ("../hotd/tools/to_tod.py", "tools/hotd/to_tod.py"),
    ("../hotd/tools/make_zombie_pack.py", "tools/hotd/make_zombie_pack.py"),
    ("../hotd/tools/make_human_pack.py", "tools/hotd/make_human_pack.py"),
    ("../hotd/tools/render_obj.py", "tools/hotd/render_obj.py"),
    ("../hotd/tools/preview_tod.py", "tools/hotd/preview_tod.py"),
]


def version():
    text = open(os.path.join(MOD, "src", "dllmain.c"), encoding="utf-8").read()
    return re.search(r'TODMOD_VERSION "([^"]+)"', text).group(1)


def copy_file(source, target):
    os.makedirs(os.path.dirname(target), exist_ok=True)
    if target.endswith(TEXT_SUFFIXES):
        text = open(source, encoding="utf-8").read().replace("\r\n", "\n").replace("\n", "\r\n")
        with open(target, "w", encoding="utf-8", newline="") as handle:
            handle.write(text)
    else:
        shutil.copyfile(source, target)


def main():
    name = f"TODEX_Modern_Community_Patch_{version()}"
    root = os.path.join(MOD, "dist", name)
    if os.path.isdir(root):
        shutil.rmtree(root)
    for source, target in FILES:
        copy_file(os.path.join(MOD, source), os.path.join(root, target))
    archive = root + ".zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as handle:
        for folder, _, names in os.walk(root):
            for file_name in sorted(names):
                path = os.path.join(folder, file_name)
                handle.write(path, os.path.relpath(path, root))
    print(f"{archive} ({os.path.getsize(archive) // 1024} KB)")


if __name__ == "__main__":
    main()
