# Continuing this project with Claude Code

This project was built with Claude Code. Everything a new Claude session needs to pick up the work is in the
repository:

| File | What it is |
|---|---|
| `CLAUDE.md` (repo root) | Project instructions: goals, folder layout, version history, rules, build steps. Claude Code reads it automatically. |
| `re/NOTES.md` | Every confirmed reverse-engineering finding about Tod.exe and its DLLs (addresses, structures, how each hook works). |
| `hotd/NOTES.md`, `re_tsh/NOTES.md` | The same for The House of the Dead and Typing Space Harrier. |
| `claude/memory/` | Claude's working memory: test routines (reaching gameplay, closing the game, the stall rule) and preferences learned while working. |

## Start a session

1. Clone the repository and install your own copy of the game (see "Game files" below).
2. Install the memory into your Claude Code profile:
   `powershell -File claude\install_memory.ps1`
   (it copies `claude/memory/*.md` to `%USERPROFILE%\.claude\projects\<this folder>\memory\`).
3. Open the folder in Claude Code and ask it to continue, e.g. "Read CLAUDE.md and re/NOTES.md, then continue with the
   next steps."

CLAUDE.md uses the original author's paths: the working folder `E:\Claude Typing of the Dead\` (this repository
is a copy of its public part) and the game in `EX Installation\TODEX\` inside it. If yours differ, update the
folder layout table first, or tell Claude where things are. Folders that CLAUDE.md mentions but this repository
leaves out (the Ghidra project and exports in `re\`, the games, `re_tsh\`) are local: regenerate the exports with
`re\run_ghidra_export.bat` from your own copy of the game. The Typing Space Harrier work has its own repository,
Tenome/Typing-Space-Harrier-Decompilation.

## Why the session transcripts are not included

Claude Code keeps full transcripts in `%USERPROFILE%\.claude\projects\...\*.jsonl`. They are not in the repository:
the main one is about 250 MB (screenshots), over GitHub's 100 MB file limit, and transcripts quote decompiled game
code, which the project rules keep private. CLAUDE.md, the NOTES files and the memory are the condensed record of
those sessions; a new session that reads them can continue where the last one stopped.

## Game files

Nothing from the games is in this repository. You need your own copies of Typing of the Dead EX (Japanese PC), and
for the side projects The Typing of the Dead (English PC), The House of the Dead (PC) and Typing Space Harrier.
Ghidra exports (`re/export/`) and the Ghidra project are regenerated with `re/run_ghidra_export.bat`.

## Tools Claude used

- zig (`pip install ziglang`) to build `mod/`; MinHook is a git submodule (`git submodule update --init`).
- Python 3 with Pillow and numpy for the tools.
- Ghidra 12.x for the exports; REA (an MCP reverse-engineering server) optionally.
- ffmpeg for audio tests (optional).
