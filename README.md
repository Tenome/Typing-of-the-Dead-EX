# The Typing of the Dead EX – Modern Community Patch (todmod)

An unofficial, fan-made mod for the **Japanese PC release of The Typing of the Dead EX**. It loads as a small proxy
`Common.dll`, changes the game only through hooks, and leaves every game file untouched. `uninstall.bat` removes it.

You need your own copy of the game. This repository contains no game files.

## Features

- **Word packs in any language:** mix several packs, keep the original scoring rules, build a pack from a plain
  word list, type a text in order, or replace the game's built-in drills.
- **Modern display:** any resolution, Hor+ widescreen, borderless window, a field-of-view slider and a low-CPU
  frame limiter. It runs on native DirectDraw/D3D7, so dgVoodoo isn't needed.
- **English mode and subtitles:** use the game's own English subtitles, or edit your own subtitle files in any
  language, with timing, size and position.
- **Packs:** replace textures (PNG, up to 8×), music (WAV/MP3/OGG/FLAC with loop points), voices and sound effects,
  and 3D models (OBJ/glTF, including skinned characters). An importer is built into the configurator.
- **Gameplay options:** pause menu, mutators, a randomizer (looks, voices, items, weapons, enemy counts) and
  cheats (big heads, infinite lives, play as any of 88 characters).
- **Statistics and extras:** typing statistics, achievements, replays, a practice pack built from your misses,
  a live WPM display, photo mode, volume sliders, blood colours, text-to-speech and a typewriter gun.
- **Online play (experimental):** two PCs play Arcade together in lockstep.

The full list of features and settings is in [mod/README.md](mod/README.md), and the player guide is
[mod/patch/README.txt](mod/patch/README.txt).

## Install (players)

1. Download the latest `TODEX_Modern_Community_Patch_*.zip` from the
   [Releases](https://github.com/Tenome/Typing-of-the-Dead-EX/releases) page.
2. Extract everything into the game folder, the one that contains `Tod.exe`.
3. Run `install.bat`, then `todmod_config.exe`. Choose your settings and click "Save and play".

Supported build: `Tod.exe` dated 2 June 2011 ("Ver0.500"). With any other build the patch stays inactive and the
game runs unchanged; `todmod.log` explains why.

## Build from source

```
git clone --recursive https://github.com/Tenome/Typing-of-the-Dead-EX
pip install ziglang
bash mod/build.sh            # builds mod/build/Common.dll and mod/build/todmod_config.exe
python mod/package.py        # makes the release zip in mod/dist/
```

`mod/install.bat` installs a development build into a game folder; edit the `GAME` path at its top first.

## Repository layout

| Path | Contents |
|---|---|
| `mod/src/` | The mod (hooks, packs, overlays, online play). |
| `mod/config/` | `todmod_config.exe`, the configurator. |
| `mod/tools/` | Pack makers, converters (OBJ/glTF to characters, English textures and subtitles from the English release) and test helpers. |
| `mod/docs/`, `mod/patch/` | Player guides shipped with the patch. |
| `re/NOTES.md` | Every confirmed reverse-engineering finding about Tod.exe and its DLLs. |
| `hotd/` | Notes and tools that turn The House of the Dead (PC) characters into model packs. |
| `CLAUDE.md`, `claude/` | Instructions and memory for continuing the project with Claude Code. |

## Continuing with Claude Code

This project was built with Claude Code. `CLAUDE.md`, `re/NOTES.md` and `claude/memory/` hold everything a new
session needs. See [claude/README.md](claude/README.md) to install the memory and pick up the work.

## Credits and licence

The mod is MIT-licensed ([LICENSE](LICENSE)). It uses MinHook (BSD 2-clause), stb_vorbis (public domain/MIT) and
dr_flac (public domain/MIT-0); see [mod/third_party/README.txt](mod/third_party/README.txt).
The Typing of the Dead is a trademark of SEGA. This project is not affiliated with SEGA.
