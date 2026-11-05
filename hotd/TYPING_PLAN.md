# The Typing of the Dead 1 — House of the Dead (PC, 1998) typing conversion plan

Goal: play The House of the Dead with the keyboard the way Typing of the Dead plays HOTD2. Each enemy shows a word;
every correct letter is a shot at it, and finishing the word kills it. Words come from todmod word packs.

## What we already have
- **Engine knowledge:** formats, skeletons and motions of `thotd.exe` (`hotd/NOTES.md`). There's a Ghidra export
  (`hotd/re/export/thotd.exe/`, 1590 functions). The exe is Watcom-built, using register calling conventions.
- **todmod code to reuse:**
  - word packs: `word_pack.c`, difficulty, reading units, kana → romaji;
  - Unicode text drawn with GDI (`glyph_raster.c`);
  - keyboard-layout handling;
  - stats, replays and the configurator framework;
  - test harness: `capture_game.ps1`, autotype, close scripts.
- **The way in, found 2026-10-28:** thotd.exe imports exactly three functions from the light-gun driver
  `gamebios.dll`: `gblGetButtonStatus`, `gblGetCursorPosition` and `gblSetScreenMode`. With the input device set
  to "Lightgun 1", a replacement `gamebios.dll` (like todmod's `Common.dll` proxy) controls where the gun points
  and when it fires, with no patching of thotd.exe. The game also has a built-in "Auto Reload" option.

## Ground rules
- `The House Of The Dead I\` stays read-only. Work in a copy, `hotd\game\` (174 MB; 226 GB free), and install the
  proxy there. The original folder is only a reference.
- The rules carried over from TOD still apply:
  - hooks over byte patches;
  - backups and reversible installs;
  - no game files or decompiled code are published;
  - confirmed findings go into `hotd/NOTES.md`;
  - in-game checks use the stall rule (30 s with no prompt, or endless typing, means stuck).

## Milestones

### M0 Baseline (small)
- Copy the game to `hotd\game\`. Run thotd.exe there on this PC (`-d3d`, windowed if it allows, 1 CPU like
  `batch.bat`), with "Lightgun 1" chosen as the input device.
- Write a pass-through `gamebios.dll`: the three exports, plus a log of every call (arguments and results, timing)
  to `hotd_typing.log`. A Watcom caller may expect particular registers to be preserved, so check the calling
  convention from the call sites.
- Done when the game runs to stage 1 with the proxy in place and the log shows how the game polls the gun.

### M1 Enemies on screen (research)
- Find the actor/enemy list: active enemies, kind, hit points, position, alive/dying state. Two leads:
  - the motion and body tables in NOTES lead to the per-actor animation state;
  - the HOTD2 rebuild (`H:\Github\Tenome\hotd2-re`, same studio a year later) shows the likely layout and naming.
- Find the camera and projection (world → screen), and keep civilians and agents apart from enemies.
- Debug check: draw a box at each enemy's projected head position, and log enemies per area.
- Done when boxes follow every zombie through the first area in a capture run.

### M2 Shooting by program (small once M1 is done)
- Through the proxy, aim the gun at an enemy's head or weak spot and pull the trigger for one frame per shot.
  Reloading stays automatic.
- Debug key: "shoot the nearest enemy".
- Done when a scripted run clears stage 1's first area with no mouse.

### M3 Typing core (the heart of it)
- **Words:** each enemy gets a word when it appears. Length follows enemy kind and hit points, so fast or weak
  enemies get short words and big ones long words. Words come from packs (`words.tsv` list names chosen per
  kind/stage, or a plain word list).
- **Keyboard:** read through a window-proc or DirectInput hook, using todmod's layout logic. TOD rules apply:
  first letter picks the target, Backspace/Enter cancels, a wrong key is a miss.
- **Shots:** each correct letter fires one shot at the target, and completing the word kills it. If the shots run
  out before the word does, extra hits are applied, or damage is written directly once M1 knows hit points.
- **Never typeable:** civilians and anything that must not be shot.
- Done when stage 1 can be played start to end by typing (autotype test run plus a human play-test).

### M4 Word boxes and look
- Draw TOD-style word boxes: the word, the typed part, and the next key. Use DirectDraw back-buffer GDI or a D3D
  overlay, whichever M0 shows the renderer allows, with todmod's glyph code (any language, kana readings).
- Show a crosshair or flash on each hit. The original HUD stays.

### M5 Special cases
- **Bosses:** a word on the weak point; a phase change gives a new word. Kinds: Chariot, Hangedman, Hermit,
  Magician.
- **Items and doors:** crates, lanterns and doors that hide items or open routes get optional item words, like
  TOD's.
- **Mid-scene events:** thrown axes, bats and other fast attackers get short words; scripted rescues work as in
  TOD. In both, a civilian's attacker is the target, never the civilian.
- **Hard to type:** jumpers and anything else too fast to type gets single letters (TOD's small-enemy rule).

### M6 Options and release
- **Settings:** `hotd_typing.ini` [dictionary]/[typing]/[display], via a configurator page or a small HOTD
  configurator built from the same code.
- **Extras:** stats and replays are possible later. Replays need determinism research like TOD's.
- **Release:** a package containing only our files (proxy DLL, configurator, guide), installed into the user's own
  copy.

### Later (separate)
Widescreen and resolution for HOTD, modern Windows fixes, HOTD's own zombies in TOD (bosses and 16-joint
creatures, from `hotd/NOTES.md` "Next").

## Risks and open questions
- **Running at all:** does the 1998 D3D path run on Windows 10 here, and windowed? The user has run it before
  (`_inmm.log` 2026-10-27). If D3D fails, the software renderer or a DirectDraw wrapper are fallbacks; it will be
  known after M0.
- **Lightgun mode:** some games require calibration or a gun "present" status from `gblGetButtonStatus`, so M0's
  log decides what the proxy must answer.
- **Watcom code:** register-based calls and no symbols make M1 slower than TOD's work. Hooks need naked wrappers.
- **Hit test:** the hit test is HOTD's own, so a shot can miss an enemy behind scenery. Aim at the point the game
  marks as the hit point once it is found. Writing damage directly avoids misses but skips hit reactions, so prefer
  real shots.
- **Size:** M1 and M3 are the bulk; M0, M2 and M4 are small to medium.

## First steps
1. M0: copy the game, then write and install the logging `gamebios.dll` proxy.
2. Launch the game with "Lightgun 1" and record the gun calls through the title and stage 1 start.
3. Start M1 from the HOTD2 rebuild's enemy notes and HOTD's motion/body tables.
