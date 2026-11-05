# todmod

A mod for the Japanese PC version of *The Typing of the Dead EX*. It loads as a proxy `Common.dll` and changes
the game through hooks only; no game file is patched. Personal learning project.

## Features

| Area | What todmod does | Settings (`todmod.ini`) |
|---|---|---|
| Word lists | Loads UTF-8 word packs from `dict\<pack>\` (`description.txt` is shown in the configurator; `words.tsv` + `kids.tsv` for Kids' Mode, or one `.txt` per list), any language, several packs mixed by priority; anything missing comes from the game's `word.HAB` ("Check coverage" in the configurator lists those). | `[dictionary] packs=` |
| Mixing | Optionally combines every active pack's words for each list (otherwise the top pack with a list wins). | `[dictionary] mix=1` |
| File order | Optionally the top pack's lines come in file order (`sequence.txt`, written by "New pack from word list"), so a play or script can be typed from start to finish; built-in letter/short-word slots and words under 4 keys stay as they are; the position is saved. | `[dictionary] in_order, in_order_min_length` |
| Built-in words | Letter, kana and symbol drills and staff names stored in Tod.exe itself are replaced by `builtin_*` lists of a pack. | `words.tsv` lists |
| Capitals | Optional case-sensitive typing for pack words; capitals can count as harder for scoring. | `[typing] case_sensitive, case_scoring, uppercase_points` |
| Spaces | Spaces are typed and scored (cost 2) like the originals; optionally skipped for pack words without changing the score. | `[typing] type_spaces` |
| Textures | PNG texture packs replace any game texture (2D and 3D, also at 2x/4x/8x resolution) without touching tex.HAB; `tools\english_textures.py` makes an English menu pack from the English version. | `[textures] packs`, `[debug] log_textures` |
| Models | OBJ (or original-format .bin) model packs replace any 3D model when it loads (with the textures in `textures\<pack>`); loaded models can be dumped as OBJ/MTL + original bytes; `tools\model_tool.py` exports all of pol.HAB with textures. | `[models] packs, dump`, `[debug] log_models` |
| House of the Dead | `tools\hotd\` converts The House of the Dead (PC) characters from your own copy into model packs: `hotd_zombies` (every human zombie becomes a HOTD zombie) and `hotd_humans` (civilians and story characters become the agents, Sophie, scientists), animated by this game; the Characters tab shows and previews them. | model packs `hotd_zombies`, `hotd_humans` |
| Music | WAV, MP3, OGG or FLAC files replace the BGM tracks (music packs or files next to the .wma); `LOOPSTART`/`LOOPEND` tags (or a WAV `smpl` chunk) loop a section. | `[music] packs` |
| Subtitles | Subtitle files (`subtitles\<name>.tsv`, edited on the configurator's Subtitles tab) replace cutscene subtitles in either language mode; Latin lines use the game font, other scripts (Japanese...) are drawn with a Windows font at the bottom; lines a file leaves out keep the game's own; optionally every subtitle (also the game's English text) uses the Windows font; the attract intro's English lines come back with `tools\english_subtitles.py`. | `[text] subtitles, subtitle_font, subtitle_windows_font` |
| English mode | Switches the game's language flag to English: cutscene subtitles come from the English text still in Tod.exe (the voices are the English dub). | `[text] language=english` |
| Mutators | Most lives (1/2), typos cost a life, word difficulty always easiest/hardest, reversed/scrambled pack words, upper/lower/random case, game speed 50–200 %. Configurator "Mutators" tab. | `[mutators] max_lives, typos_hurt, word_level, words, letter_case, speed` |
| Borderless window | Frameless window covering the screen (render size = screen size unless set). | `[display] borderless` |
| Typing statistics | Per game and player: words, keys, typos, accuracy, words per minute, most missed letters, hardest words; `stats\history.tsv`, a report per game, `practice_words.txt` for a practice pack. Configurator "Stats" tab. | `[stats] enabled` |
| Replays | Every game recorded (keys of both players, save, randomizer seed) to `replays\`; watch from the configurator "Replays" tab or `TODMOD_REPLAY=<file>`; drift check every second. | `[replay] record, keep, play` |
| Online (experimental) | Two PCs play Arcade together in lockstep: only key reports cross (UDP), host = player 1, joiner = player 2; per-frame desync check; input delay from the ping; settings check refuses mismatched setups; the joiner plays on the host's save; direct, VPN or relay (`tools/online_relay.py`). Configurator "Online" tab; guide `docs/online_play.txt`. Tested on one PC only (`play_two_copies.bat`). | `[online] mode, peer, port, relay, room, input_delay`; `TODMOD_ONLINE=host\|join` |
| Randomizer | Each game: zombie types look like other zombie types, civilians/story characters like other people (or mixed), scene voice lines are shuffled with matching subtitles, picked-up items get a random effect, thrown weapons become other weapons, each zombie appears 0-4 times, zombie types drawn as people; seed shown in todmod.log and repeatable. Configurator "Randomizer" tab. | `[randomizer] enabled, seed, enemies, people, mix, voices, items` |
| Cutscenes | Optionally skips every skippable cutscene (those showing the skip caption) by pressing Enter for you; the attract demo is left alone. | `[gameplay] skip_cutscenes=1` |
| Cheats | Big heads for everyone (the Balloon Head item, always on); players always in the zombie or old man/woman skin suit; each player shown as any of the game's 88 characters (zombies, civilians, story characters, bosses), still carrying the Dreamcast, battery pack and keyboard. | `[cheats] big_heads, suit=none/zombie/elderly, player1_character, player2_character, keep_equipment` |
| Debug | `autotype=1`: any letter key types the next letter (unattended testing); `log_typing`, `log_word_sources`; `log_textures`/`log_models` write tab-separated `todmod_textures.log`/`todmod_models.log`. | `[debug]` |
| Keyboard | Typed characters follow the Windows keyboard layout (US ' and " work; QWERTZ/AZERTY too) instead of the game's fixed Japanese layout, or a chosen US QWERTY/Dvorak/Colemak/Workman layout by key position; Backspace cancels the word like Esc. | `[input] keyboard_layout=windows/qwerty/dvorak/colemak/workman/japanese, backspace_cancels` |
| Scoring | Computes each word's difficulty from what is typed, matching the original data for 99.9 % of words, so new words score like the originals. | automatic |
| Characters | Draws characters the game font lacks (é, ł, ß, Greek, Cyrillic, Hangul…) with Windows fonts in the game's style; Latin text can be narrow (two characters per cell, 48-character lines). | `[text] font, weight, cap_height, half_width, half_width_font` |
| Display | Any resolution, widescreen with a wider 3D view and centred 4:3 menus, cutscene letterbox widened, exact window size. | `[display] width, height, widescreen, widen_models` |
| Timing | Frame limiter that sleeps instead of spinning (one CPU core at 100 % → about 18 %); optional blended in-between frames on monitors faster than 60 Hz (each game update is recorded and redrawn with interpolated positions). | `[timing] limiter, frame_interpolation, update_rate, stats` |
| Renderer | Runs on Windows' own DirectDraw/Direct3D 7 without dgVoodoo. | `tools\dgvoodoo_enable.bat` / `_disable.bat` |

All settings, plus the game's own options (stored in `%APPDATA%\Sega\TODEX\Tod.ini`), are in
**`todmod_config.exe`**, which can also turn a plain word list into a pack ("New pack from word list…").
See `docs\word_packs.txt` (installed as `dict\README.txt`) for writing word lists.

## Install / remove

- `build.sh` builds `build\Common.dll` and `build\todmod_config.exe` (zig cc, x86-windows-gnu; MinHook in
  `third_party\minhook`).
- `install.bat` keeps the original `Common.dll` as `Common_orig.dll` and copies the mod, configurator and guide.
- `uninstall.bat` restores the original `Common.dll`; `todmod.ini` and `dict\` are kept.
- The game writes `todmod.log` next to `Tod.exe`; it ends with "game closed normally" unless the game crashed.

## Tools (`tools\`)

| Tool | Purpose |
|---|---|
| `english_to_pack.py` | Converts the English version's `word\` folder into a pack. |
| `english_subtitles.py` | Writes `subtitles\english.tsv` from the English version's `Tod_e.exe`, including the attract-intro lines EX lacks (English mode uses it as the base). |
| `hab_tsv_to_pack.py` | Converts a `hab_tool.py words-export-all` TSV into a pack. |
| `fit_difficulty.py` | Refits the difficulty model from the original word files (`src\unit_costs.c`). |
| `gen_pack_template.py` | Regenerates the word-list structure used by the pack builder (`config\pack_template.c`). |
| `gen_reading_units.py` | Regenerates the typing-unit table (`src\reading_units.c`). |
| `model_tool.py` | Exports pol.HAB models as OBJ/MTL/PNG + original bytes; previews an OBJ. |
| `capture_game.ps1` | Starts the game windowed, presses keys, saves screenshots of the game window (tests). |
| `close_game.ps1` | Closes the game like Alt+F4 and answers its quit box (for scripted tests). |
| `dgvoodoo_enable.bat` / `dgvoodoo_disable.bat` | Move dgVoodoo's `DDraw.dll`/`D3DImm.dll` back into or out of the game folder. |

Tests in `tests\` (`verify_pack.c`, `build_pack_cli.c`, `render_glyphs.c` + `preview_glyphs.py`,
`ini_roundtrip.c`) run offline. Reverse-engineering notes are in `..\re\NOTES.md`.

No game files, extracted assets or decompiled code are part of this mod. Some generated tables
(`src\unit_costs.c`, `src\reading_units.c`, `config\pack_template.c`, `tools\romaji_table.json`) are derived
from the user's own copy of the game (typing spellings, list names and difficulty numbers, no word text); keep
the project private.
