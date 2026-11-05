# Typing of the Dead EX (Japanese PC) modernisation project

Personal learning project. Working folder: `E:\Claude Typing of the Dead\` (stay inside it).

## Goals
1. Swappable dictionaries in any language; mix-and-match word packs; later, pick packs in-game.
2. Widescreen, arbitrary resolutions, custom framerates, no dgVoodoo dependency.
3. All options exposed in a user-friendly configurator.

## Folder layout
| Path | Contents |
|---|---|
| `EX Installation\TODEX\` | Japanese game (target). Live install; `todmod` is installed here. |
| `The Typing of The Dead\` | English version (reference only; its `word\` folder has English word files). |
| `ghidra_12.2_PUBLIC\` | Ghidra. Headless: `support\analyzeHeadless.bat`. |
| `ghidra_12.1.4_PUBLIC\` | Ghidra 12.1.4, the version REA's Ghidra provider accepts (used only by REA). |
| `tools\node\` | Portable Node 24.22.1 (REA needs ≥ 24.22.1; system Node is 24.15). |
| `tools\rea\` | REA 6.8.0 installed locally; registered in `C:\Users\Michael\.claude.json` as MCP server `rea` (`tools\node\node.exe …\rea.mjs mcp`, env `GHIDRA_INSTALL_DIR`, `JAVA_HOME`=Adoptium JDK 25). REA skill in `~\.claude\skills\reverse-engineer-anything`. Backup of the config: `.claude.json.before_rea_20261026_1900`. npm cannot reach the registry from the sandbox (EACCES), so REA was copied from the npx cache. |
| `tools\downloads\` | Verified downloads (Node, Ghidra 12.1.4 zips). |
| `C:\Program Files (x86)\Microsoft Visual Studio .NET 2003\Vc7\bin\` | User-installed VS .NET 2003: cl/c1/c1xx/c2 **13.10.6030 (SP1)**, link/ml 7.10.6030. Game was built with 13.10.**3077** (RTM), so SP1 may not byte-match. |
| `C:\Program Files (x86)\Microsoft Visual C++ Toolkit 2003\bin\` | User-installed Toolkit 2003: cl 13.10.3052, link 7.10.3052 (no ml). Decompilation work in `re\decomp\` (see `COMPILER_PLAN.md`). |
| `re\` | Reverse engineering. `bin\` = pristine copies of the originals, `project\` = Ghidra project, `export\` = text dumps, `scripts\ExportAll.java`, `run_ghidra_export.bat`, **`NOTES.md` (all confirmed findings — read first)**. |
| `mod\` | The mod (`todmod`). `src\` hook sources, `config\` configurator, `tests\`, `tools\`, `build\Common.dll` + `build\todmod_config.exe`, `install.bat`, `uninstall.bat`, `build.sh`. |
| `The House Of The Dead I\` | The House of the Dead PC (1998), read-only. Second project: extract its models for TOD, later a typing conversion. |
| `hotd\` | HOTD work: `NOTES.md` (formats: RBH meshes, VMC textures, skeletons/motions in thotd.exe — read first), `tools\` (Python extractors), `re\` (Ghidra export of thotd.exe), `extract\` (local exports, do not publish). |
| `H:\Games\The House - Typing of the Dead COLLECTION\` | Earlier tools: `hab_tool.py` (HAB0 archive list/extract/pack, PRS, textures, word files, romaji table), `word_tool.py` (English word format), `transplant_tool.py` + `transplant.bat` (English words → Japanese format). Read-only reference unless asked. |

## Current state
- Ghidra export of Tod.exe, HglCore, HglNa, HglHab, Table, Common, RfuDrv, Configurater is in `re\export\`.
- `todmod` 0.18.0: proxy `Common.dll` (exports ordinals 1/2 forwarded to `Common_orig.dll`), logs to
  `TODEX\todmod.log` (ends with "game closed normally" unless it crashed). Steps 1–5 done and confirmed in game:
  dictionary packs (`todmod.ini [dictionary]`), Unicode glyphs via GDI (`[text]`), resolution + Hor+ widescreen
  with centred 4:3 2D (`[display]`, tested fullscreen 1280×720 and 1920×1080, windowed 1280×720), configurator
  `TODEX\todmod_config.exe` (`mod/config/`), timer-based frame limiter (`[timing]`, CPU 100% → ~18% of a core).
  0.5.1: cutscene letterbox (3D model 2421) widened to 16:9, windowed client area exactly W×H.
  Open: other cutscenes may use other bar models (`[display] widen_models`).
  0.5.2: GDI glyphs sized like the native letters (`[text] cap_height=20`, weight 400).
  0.6.0: automatic difficulty/scoring (99.9 % of original values), simple pack lines (`word[TAB reading]`),
  configurator "New pack from word list...", English pack (`TODEX\dict\english`, `mod/tools/english_to_pack.py`),
  user guide `mod/docs/word_packs.txt` (installed as `dict\README.txt`).
  0.9.0: packs as `words.tsv`/`kids.tsv` (hab_tool export layout), English pack covers all 957 lists incl. quizzes,
  configurator "Check coverage"; `half_width=0` = narrow only when needed.
  0.10.0: keys follow the Windows keyboard layout (`[input]`), Tod.exe built-in word tables (drills, single
  letters/kana, staff names) replaceable by `builtin_*` pack lists; English pack and coverage check include them.
  0.11.0: optional case-sensitive typing for pack words (`[typing] case_sensitive`, `case_scoring`,
  `uppercase_points=4`), configurator "Typing" group.
  0.12.0: texture packs (`TODEX\textures\<pack>\<bank>\<index>.png`, `[textures] packs`, `[debug] log_textures`),
  English 2D texture pack from the English version (`mod/tools/english_textures.py`, 89 textures).
  0.13.0: larger replacement textures (2x/4x/8x), English pack 188 textures (layout matching, 4-bit palettes),
  music packs (`[music] packs`, `music\<pack>\<TRACK>.wav|mp3|ogg|flac`; OGG/FLAC decoded to `music\.cache`),
  `mod/tools/capture_game.ps1` (windowed screenshots via PrintWindow).
  0.14.0: English mode (`[text] language=english`: game language flag 0x00a1b6b2 → English subtitles from the
  game's own text), `[debug] autotype=1` (any letter types the next letter), capture_game.ps1 sends keys
  (Enter repeatedly reaches Arcade gameplay; keys only when the game window is in front). English pack 191
  textures incl. "Rest" counter and a generated "Press Enter to skip" (2x).
  0.15.0: 3D model packs (`[models] packs`, `models\<pack>\[<bank>\]<id>.obj|.bin`), in-game model dump
  (`[models] dump=1` → `models\dump\`), `mod/tools/model_tool.py` (export pol.HAB as OBJ/MTL/PNG/.bin, preview),
  `[dictionary] mix=1` (mix all packs), `[input] keyboard_layout=qwerty|dvorak|colemak|workman`, texture and model
  logs in their own TSV files (`todmod_textures.log`, `todmod_models.log`). Online network mode: code compiled out
  of the shipping Tod.exe (NOTES "Online network mode"). `re\decompile_at.bat <program> <out> <hex>...` decompiles
  (and creates) functions Ghidra missed, read-only.
  0.15.1: texture packs also match `*_id<texture id>.png` anywhere in a pack (model textures shared across banks);
  `png\` (project root) is a full tex.HAB export named `NNN_id<id>.png`. Online play deferred (user: maybe P2P).
  0.16.0: `[gameplay] skip_cutscenes` (sets the game's skip request in the caption task 0x4261a0), `[cheats]
  big_heads` and `suit=zombie|elderly` (item effect bits in 0x9f0be0). User wants next: a model selector tab with
  player-model previews (play as zombies/other characters) and more silly cheats.
  0.17.0: `[cheats] player1_character/player2_character` = any of 88 character kinds (configurator "Player 1/2 as");
  `mod/tools/character_kinds.py`.
  0.18.0: configurator "Characters" tab with textured previews rendered from the user's game files
  (`config/game_models.c`, `config/character_page.c`; `todmod_config.exe /characters [kind]`); kinds are 0–87,
  −1 = normal; `config/character_list.h` generated by `mod/tools/gen_character_list.py`.
  Unattended in-game checks are possible: `capture_game.ps1 -EnterFrom 6 -EnterUntil 250 -TypeFrom 25 -TypeUntil 250`
  with `[debug] autotype=1` clears stage 1 (~4 min) and stops at name entry.
  0.19.0: music loop points (`LOOPSTART`/`LOOPEND`, own waveOut player `src/music_player.c`), subtitle files
  (`[text] subtitles`, configurator "Subtitles" tab, `subtitles\<name>.tsv`; non-Latin lines drawn with GDI at the
  bottom in a VSync hook, works in Japanese mode too), pack `description.txt` shown in the configurator,
  `[text] subtitle_windows_font` (all subtitles in `subtitle_font`), attract-intro English lines (subtitle
  lines 366–374, `tools/english_subtitles.py` → `TODEX\subtitles\english.tsv`, base in English mode), `[input] backspace_cancels`, `[typing] type_spaces`, `[cheats] keep_equipment` (Dreamcast/keyboard on swapped
  players). The user's own `TODEX\subtitles\Japanese.tsv` (194 lines) is theirs — don't overwrite it.
  0.20.0: House of the Dead zombies in TOD (`hotd/tools/to_tod.py`, `make_zombie_pack.py` → model pack
  `TODEX\models\hotd_zombies` + `textures\hotd_zombies`, see `hotd/NOTES.md`), waist-belt guard for replaced models,
  power-of-two texture replacements, model packs bring `textures\<pack>`, configurator model-pack drop-down,
  `[dictionary] in_order` (top pack's `sequence.txt` lines in order, short words stay random, position saved).
  0.21.0: HOTD heads turned the right way, `hotd_humans` pack (`make_human_pack.py`), Characters tab draws and
  names model packs (`characters.tsv`).
  0.22.0: randomizer (`src/randomizer.c`, configurator "Randomizer" tab, `[randomizer]`): character looks (permuted
  `BodyInitTbl`/`BeltVtxDb`/`ShadowTbl`), voice lines (event→subtitle table), item effects. Design reference:
  `H:\Github\Tenome\hotd2-re\` (MIT, HOTD2 C++ rebuild; user granted access).
  0.23.0: online lockstep proof (`src/online_hooks.c`, `[online]`, `TODMOD_ONLINE=host|join`, `play_two_copies.bat`):
  two copies on one PC stay in sync (0 desyncs over 7200 frames of Arcade). `capture_game.ps1 -Online` runs host +
  joiner; `close_game.ps1` closes every copy. NOTES "Lockstep proof".
  0.24.0: two-PC online play (untested between real PCs; user has one PC): relay `tools/online_relay.py`, settings
  fingerprint, host's save for the joiner, auto input delay, background running, configurator "Online" tab,
  `docs/online_play.txt`, test conditions `[online] test_lag_ms/test_jitter_ms/test_loss_percent`,
  `capture_game.ps1 -JoinKeys`. NOTES "Two-PC online play".
  0.25.0: mutators (`src/mutators.c`, `src/word_mutators.c`, `[mutators]`), randomizer thrown weapons, borderless
  window (`[display] borderless`), typing statistics (`src/typing_stats.c`, `stats\`), replays (`src/replay.c`,
  `replays\`, `TODMOD_REPLAY`), shared `src/determinism.c` and `src/frame_hooks.c`, `[debug] memory_watch`;
  configurator tabs Mutators/Stats/Replays; `docs/gameplay_options.txt`; `capture_game.ps1 -Keys` takes any letter.
  NOTES "Mutators, weapons, borderless, statistics, replays".
  0.26.0: enemy counts (`src/enemy_counts.c`, `[randomizer] enemy_min/enemy_max`, spawn ops 0x4218d0/0x421bb0/0x421c70),
  `human_zombies`, `[debug] log_enemies`; fixes: axe throwers 1/19/20, chainsaw 2 and monkey jumper 25 keep their look
  (else stalls), kind 68 = stage 1 boss (out of the people pool), jumpers 13/25 and assassins 1/22 never copied; assassin 22 keeps its look too (stage 2 soft-lock); configurator
  Characters list hides the "may be invisible" kinds. TSH word pack tool
  `tools/tsh_to_word_list.py` (pack `TODEX\dict\typing_space_harrier`, local only). NOTES "Enemy counts and weapon users".
  0.27.0: pause menu (`src/pause_menu.c`, Esc, `[gameplay] pause_menu`): skips the game frame (`frame_hooks` pause
  handler) and replays a captured frame (`frame_interp` capture mode, one frame recorded on Esc), menu drawn with GDI
  on the render target; Exit = mode request 4 (`0x757c58`), Restart = request mode 6 and overwrite stage `0xa19598`
  in a `0x4804b0` hook at step 0; restart resets player states to 0 (like the game retry) and presses Enter through Mode Select (script entry 1 broke the chapter). Modes: 15 boot, 11/3 title, 4 main menu, 6 gameplay. NOTES "Pause menu".
  0.28.0: narrow text fixed in English mode (word box drawn with the Japanese layout), Staff Credits leaves
  player swaps alone, Strength chainsaw Dreamcast loop (`src/sound_loops.c`, `[sound] dreamcast_loops`), pause
  menu pauses music and loops, `tools/obj_to_character.py` (single-mesh OBJ → character pack; user's
  `models\GTA` Tommy on kind 46 and 57 = player 1). NOTES "Narrow text, Staff Credits, chainsaw loop".
  0.29.0: `[display] blood=red` (`src/blood.c`, Settings "Red blood"), configurator "Audio" tab (tab 8, `/audio`):
  music, voice lines with subtitles, effects; Play/Pause/Stop. NOTES "Blood colour and audio library".
  0.29.1: randomizer leaves the "Player 1/2 as" kinds out of the look shuffle (a shuffled chosen kind showed another
  body whose models were not loaded for the player: invisible in every scene).
  0.30.0: skinned (bending) imported characters: `src/skinned_models.c`, `.skin` files from
  `tools/obj_to_character.py` (default; `--rigid` for the old cut). NOTES "Skinned models".
  0.31.0: copies only for walkers (behaviour 0x01/0x12) and axe throwers (0x21); `tools/gltf_to_character.py` (rigged
  glTF/GLB → skinned pack). Soak test: chapters 1-2 clear with all randomizer options (NOTES "Soak test result").
  0.32.0: `[cheats] infinite_lives` (Settings "Infinite lives"), autotype answers quizzes correctly (records 1-8 of a
  19-record quiz list), kinds 9/18/24 keep their look. Full run with all randomizer options cleared all six chapters
  (5 and 6 via `[debug] pause_restart_stage`). 0.32.1: staff-name tables keep their group values (credits groups); ending credits fixed.
  0.33.0: volume sliders (`src/volume.c`, `[sound] music_volume/effects_volume/voice_volume`), score overlay
  (`src/score_overlay.c`, `[display] show_score`; shared VSync overlay `overlay_add`), voice packs
  (`src/voice_packs.c`, `voices\<pack>\`, `[sound] voice_packs`), kana/kanji word boxes use the Japanese layout in
  English mode, configurator 600 DLU wide (word packs in a third column, scrollable description) with
  "Import a character model" (runs `TODEX\tools\*_to_character.py`; install.bat copies the converters). Player 1 =
  kind 57. NOTES "Volume, score display, voice packs, kana word boxes".
  0.34.0: blood colour drop-down (`[display] blood=green|red|purple|gold|blue|grey|black|white|pink|orange|cyan|
  yellow|rainbow`, tint in `create_texture`), score overlay = in-play word score (matches the result screen; NOTES
  "Blood colours, score vs result screen, subtitle timing"), `tools/subtitle_timing.py`.
  0.35.0: subtitle timing (`src/subtitle_timing.c`, `start`/`end` columns in subtitle files, Subtitles tab
  Start/End/Play voice, `[text] subtitles_cover_voice=1` default); NOTES "Subtitle timing".
  0.36.0: Extras group on the Settings tab (live WPM `[display] show_typing`, `[fun] speak_words`/`typewriter_gun`/
  `photo_mode`, `[sound] mute_in_background`, `[typing] hide_reading`, `[display] fov`), `blood=paintball`,
  subtitle size/position, achievements + practice pack (Stats tab). New sources: speech.c, background_mute.c,
  paintball.c, typewriter.c, achievements.c. NOTES "Extras: FOV, reading line, photo mode, ...".
  `capture_game.ps1 -Keys` also takes F12. Autotype games don't write stats\.
  Git: repo initialised 2026-11-04 (whitelist `.gitignore`, MinHook submodule, `claude/` = memory + resume guide);
  nothing committed here. **Public repo (2026-11-05): `H:\Github\Tenome\Typing of the Dead EX\`** (remote
  Tenome/Typing-of-the-Dead-EX, branch `master`, MIT): a copy of this folder's whitelisted files minus `re_tsh\`.
  After changes here, copy the changed files there, commit and push; releases carry the `mod/dist` zip.
  Keep `claude/memory/` in step with the memory folder when memories change.
  Test harness: the game cancels the word on Enter, so `capture_game.ps1` Enter presses during gameplay cancel typing.
  Run `capture_game.ps1` from the PowerShell tool (launching it from bash hung). The Arcade car opening needs 7 Enter
  presses (`-EnterFrom 5 -EnterUntil 14 -EnterEvery 1.5`): chapter title ~17 s, car opening ~20-33 s, gameplay ~38 s.
  0.8.0: frame interpolation (`[timing] frame_interpolation`, `update_rate`), build check, community patch
  (`mod/package.py` → `mod/dist/`). Interpolation verified in half-speed test; untested on > 60 Hz hardware.
  0.7.0: half-width Latin text (`[text] half_width=1`): two characters per cell, 48-character lines; English pack 420 lists.
  Test packs in `TODEX\dict\`: `german_test` (from `mod/tests/wordlists/german_sample.txt`), `english`,
  `unicode_demo` (S000L010/L020), `demo` (ASCII words for S000*/SE01*),
  `live_check` (text copy of live word.HAB). Offline tests: `mod/tests/verify_pack.c`, `render_glyphs.c` +
  `preview_glyphs.py`, `ini_roundtrip.c`.
- **dgVoodoo is currently disabled** (DDraw.dll/D3DImm.dll parked in `TODEX\dgVoodoo_disabled\`); the game runs on
  native DirectDraw/D3D7. Toggle: `mod/tools/dgvoodoo_disable.bat` / `dgvoodoo_enable.bat`.
- **Game settings live in `%APPDATA%\Sega\TODEX\Tod.ini`** (cp932; `Default.ini` is only the template copied when it
  is missing). Backup: `Tod.ini.orig` next to it.
- The user plays and closes the game (Alt+F4 is the only quit path, followed by a Yes/No quit box); Claude launches
  it for tests and reads `todmod.log` afterwards. When Claude closes the game itself, use
  `mod\tools\close_game.ps1` (sends Alt+F4 and answers the quit box) — never leave the box for the user to click.
  Diagnostics in `todmod.ini [display]`: `log_wide_2d`, `log_objects`.
  Unattended self-test: back up `Tod.ini`, set `Active=true`, launch; the attract demo loads S000L010 after ~60 s
  (enough to check the loader); real gameplay needs Enter presses through DirectInput, so gameplay checks are
  the user's. Restore `Tod.ini` afterwards.
- Build needs local zig caches (set by `build.sh`); `python3` may be missing on Windows, `build.sh` falls back to `python`.
- Live `TODEX\word.HAB` is an English transplant; `word.HAB_O` is the original Japanese.
  Live `HglCore.dll` has resolution slot 1 = 1920x1080 (original is `HglCore.dll.orig`); no longer needed with
  `[display] width/height`.

## Next steps (in order)
1. ~~Dictionary loader hook~~ (done, 0.2.0).
2. ~~Unicode glyph hook~~ (done, 0.3.0).
3. ~~Resolution/aspect hooks and 2D centring~~ (done, 0.4.0).
4. ~~New configurator~~ (done, writes Tod.ini `[HGLCORE]` and `todmod.ini`).
5. ~~Framerate and dgVoodoo research~~ (done, 0.5.0; see NOTES "Frame pacing" / "Renderer and dgVoodoo"): logic is
   fixed 60 Hz per frame, so >60 fps needs render interpolation (deferred); native D3D7 works.

## Candidate next steps (not yet agreed)
- Decided 2026-10-26: word pack selection stays in the configurator (no in-game picker for now).
- Check later cutscenes for letterbox bars using other models (needs the user to play further; `log_objects=1`).
- Anti-aliasing without dgVoodoo (D3D7 wrapper) — large.

## Rules
- Never modify files in `re\bin\`; analyse those copies, not the live install.
- Before changing any file in `TODEX\`, keep a backup (`.orig`) and make the change reversible by a script.
- Prefer hooks in `todmod` over byte-patching `Tod.exe`/DLLs.
- **ASLR/relocation:** `Tod.exe` loads at 0x00400000, but the DLLs relocate (HglCore was seen at 0x001E0000,
  not its 0x10000000 preferred base). Compute DLL addresses as `GetModuleHandle(dll) + (ghidra_va - 0x10000000)`.
- Verify findings in game (log to `todmod.log`) before building on them; record confirmed facts in `re\NOTES.md`.
- Code style: minimal comments; descriptive function names.
- Don't publish game files.

## Build
- `mod\build.sh` cross-compiles with zig (`pip install ziglang`), target `x86-windows-gnu`; needs MinHook in
  `mod\third_party\minhook` (`git clone https://github.com/TsudaKageyu/minhook`). On Windows, run the same zig
  command from PowerShell, or switch to MSVC (x86) if installed.
- Install/test loop: `mod\install.bat`, run `TODEX\Tod.exe`, read `TODEX\todmod.log`, `mod\uninstall.bat` to revert.

## Typing Space Harrier (second target)
`Typing Space Harrier (2002) [SEGA]\tsh.exe`, work in `re_tsh\` (read `re_tsh\NOTES.md` first). Built with **VC6**; the user's VC6 at `E:\Microsoft Visual Studio` (access granted) is the exact build. Matching decomp workflow:
`re_tsh\decomp\README.md`. Word banks are solved: plain `\r\n`-prefixed cp932 `reading<TAB>display` files
load without obfuscation (`re_tsh\tools\tsh_words.py`).
- **Decomp repo (2026-11-04):** the TSH matching decompilation now lives in its own git repo, `H:\Github\Tenome\Typing Space Harrier Decompilation\`
  (remote `Tenome/Typing-Space-Harrier-Decompilation`, branch `master`, not pushed yet; read its `README.md` and `AGENTS.md`).
  New decomp work goes there (build: `python tools/build_all.py` with `VC6_DIR=E:\Microsoft Visual Studio` and `orig/tsh.exe`);
  `re_tsh\decomp\` here is the old working tree and is no longer updated. Parallel workers use `git worktree` under `build\wt\`.
- **Stats (2026-11-05):** after each batch of merged decomp work, regenerate `docs/PROJECT_STATS.md` in the decomp repo with
  `python tools/usage_report.py` (time span, active time, tokens by day and by main/subagents) and commit it with the progress report.
