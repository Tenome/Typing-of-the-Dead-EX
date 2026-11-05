# Typing of the Dead EX (JP) — reverse-engineering notes

Build analysed: `Tod.exe` SHA-1 `7b983d6cd0294d5f2f8cf5152f5645eb6e52c04c` (identical to `Tod.exe.bak`, unmodified).
`HglCore.dll.orig` SHA-1 `58697d36…` is the original; the live `HglCore.dll` has resolution slot 1 changed to 1920x1080.
Image bases: `Tod.exe` 0x00400000, Hgl*/Table/Common DLLs 0x10000000. Addresses below are Ghidra VAs.

Ghidra export: `re\run_ghidra_export.bat` → `re\export\<module>\{decompiled.c,functions.tsv,data_refs.tsv,imports.tsv,exports.tsv}`.

## Module map

| Module | Role |
|---|---|
| `Tod.exe` | Game logic. Imports HglCore (39), HglNa (139), Table (66), Common (2, by ordinal), DINPUT8, GDI+ |
| `HglCore.dll` | Engine core: window, DirectDraw7 + D3DX (D3D7-era), HAB file streaming, sound, frame pacing, INI (`hlProfile`) |
| `HglNa.dll` | Higher-level engine API (sprites/matrices/text), imports IMM32 (IME) |
| `HglHab.dll` | HAB archive reader (`hlOpenHab`, `hlLoadHabItem`, …) |
| `Table.dll` | Pure data: `WordFileNameTbl` (527 items), `WordKidsFileNameTbl` (430), `kanji_offset`, `sprfixlist_*`, texture/motion tables |
| `Common.dll` | 2 exports: ord 1 `GetUserFolderPath`, ord 2 `bIsWindowsVersionOK`. **Proxy-DLL target for the mod loader.** |
| `Configurater.exe` | MFC 7.1 settings dialog writing `Default.ini` `[HGLCORE]` |

HglCore ordinals used by Tod.exe for files: 18 `HabFileCloseItem`, 19 `HabFileGetItemSize`, 20 `HabFileInit`,
22 `HabFileOpenItem`, 23 `HabFileReadItem`, 24 `HabFileSeekItem`, 26 `HabFileWait`, 28 `SetHabFileList`.
HAB bank 6 = `word.Hab`; the other word bank = `wordkids.Hab`.

## Dictionary pipeline (confirmed from code)

- `FUN_004cb3a0(index)` — word file loader. Picks `WordFileNameTbl[index]` (bank 6) or
  `WordKidsFileNameTbl[index]`, formats `"%s.bin"`, reads the whole item into the **fixed buffer
  `DAT_007808f8`, 0x96000 bytes (600 KB)**. Index remap quirks: 0x154→0x155, 0x17e→0x17f.
- `FUN_004cb530` — relocates the display pointers (offset → absolute) and counts records into `DAT_008168fc`.
- `FUN_004cb890(kind, i)` — returns record `i` (kind 2 = loaded word file; kinds 1/3/4/5 = built-in tables,
  e.g. `0x00504f78` (27 records), `0x00505dd0` (28)).
- Record = 0x88 bytes: `u32 group`, `u32 display_ptr`, `u16 reading_ids[64]` (0-terminated, ≤63 units);
  terminator `group == 0xFFFFFFFF`. Display text is cp932 in a pool after the records.
- Typing is judged on `reading_ids` through the romaji table in `Tod.exe` (`hab_tool.py romaji-table`).

### Word loader details (confirmed from disassembly)

- `FUN_004cb3a0`, `FUN_004cb530`, `FUN_004cb820`, `FUN_004cb890` are plain `__cdecl` (stack args, `ret`).
  `FUN_004cb3a0` returns 0; it reloads only when the final index differs from the cache `DAT_00547e84`
  (u16, stored *after* the 0x154/0x17e remap). Bank in `DAT_007808ec` (6 = word.Hab).
- Name tables are data imports: `[0x004d74f8]` → `WordFileNameTbl`, `[0x004d7588]` → `WordKidsFileNameTbl`
  (`char*[]`, names without `.bin`).
- After a reload `DAT_008168f8 = 1`; `FUN_004cb530` then turns display offsets into pointers, sets
  `DAT_00816900 = 0x7808f8` and the count `DAT_008168fc`. It stops at `group(u16) == 0xFFFF` or `reading[0] == 0`.
  These globals are touched only by 0x004cb3a0–0x004cb930 (and 0x0048db13 writes `DAT_00816904`, which forces
  index 0x20e).
- `FUN_004cb890(2, i)` checks a **built-in per-word override table** before the buffer: `FUN_004cb320`
  (register args ESI = item index, EDX = word index) scans 0x8c-byte entries `{u16 item, u16 word, record}` at
  0x00514070 (main) / 0x00514a48 (kids), terminated by 0xFFFF (errata for specific Japanese words).
- **Alternative word lists `data<N>\`**: `FUN_0047b250` also loads `<exe dir>\data<N>\<item>.bin` for N = 1..
  (0x108-byte records = 0x88 record + 0x80 inline display). `FUN_0047b2d0` picks variant `DAT_0075837c` (0 = HAB,
  1/2 = data1/data2) at random using percentages at 0x00526e00 (forced to 1 in one mode). When the variant has
  words, `FUN_004cb820`/`FUN_004cb890` kind 2 return them instead of the HAB buffer. The install has `data1\`
  (139 items).

### todmod dictionary packs (implemented, 0.2.0)

- Hooks: `FUN_004cb3a0` (call original, then load `dict\<pack>\<item>.txt`, kids: `dict\<pack>\kids\<item>.txt`,
  first pack in `todmod.ini [dictionary] packs=` wins), `FUN_004cb820` / `FUN_004cb890` kind 2 (return the pack's
  count/records from our own heap buffer). A pack item therefore also bypasses the errata table and `data<N>`.
- Format: UTF-8, `group<TAB>display[<TAB>reading]`, `#` comments; reading → unit ids via
  `mod/tools/reading_units.tsv` (half-width/katakana/lower case normalised; `<id>` escapes; diacritics folded).
- Offline check (`mod/tests/verify_pack.c`): packs made from `hab_tool words-export-all` rebuild **all 527 items of
  the live word.HAB and of word.HAB_O byte-identically**. wordkids: 375/430 identical; every difference is unit
  213 (９) which `hab_tool`'s inferred kana table exports as ０ — an export bug, not a loader bug.
- Verified in game (2026-10-26): hooks install (`MH_OK`); `demo` pack items S000L010 (395 words) and S000L020
  (745 words) loaded from text, ASCII words displayed full-width and were typable. Only the tutorial was played,
  so the in-game `verify` comparison against word.HAB was never exercised (only `demo` items loaded).

### Word record `group` and scoring (confirmed from code, 2026-10-26)

- The record's first u32 is two u16s: low = **difficulty** (copied to `word+0x9e`), high = special-mode index
  (`word+0x9c`; non-zero on 182 of 49 406 JP records, used only by a special mode in `FUN_00420bf0`: table
  `0x004db648[high]`, low byte 1/2 → timers 180/480, high byte of low = trigger count). Normal lists: high = 0.
- Picker `FUN_004c6dd0`: random index (`FUN_004c6b90`, avoids the last 16 picks), avoids words whose first typing
  unit equals that of a word already on screen (list `0x007807c4`); some modes filter by characters
  (`FUN_004c6d60`, `0x00780790`). Built-in lists at 0x00513140… are used in some modes instead of the file.
- Word finished (`FUN_0040c620`): `ratio = frames(word+0xa0+player*2) / (difficulty * 0.01)`, ×1.1 if
  difficulty < 20, ×0.8 if ≥ 61; grade 0–5 by ratio < 120/180/240/300/360/else (grade 6 for special cases).
  Score (`FUN_00486810`): base `FUN_004867b0(difficulty)` = 10 (<21), 12 (<31), 15 (<51), 17 (<71), 20 (<101),
  25 (<121), 30 (≤150), 35 (>150) **plus** speed bonus `0x0052c7af[grade+1]` = 45/35/20/15/10/5/0.
  Per-player totals at `0x00a18736 + player*0x210` (per grade counts at `+0x0e`), `0x00a18758`.
- **Difficulty is a deterministic function of the reading**: in word.HAB_O only 1 of 30 981 distinct readings has
  two values. ≈ 4 × keystrokes: per-key least-squares fit (first spelling) gives letters ≈ 4 (A/S/H/T ≈ 3, P/Q ≈ 6,
  V ≈ 8), digits ≈ 10, symbols ≈ 12, space ≈ 2 → 83.5 % exact; per-typing-unit weights → 87.6 % exact, mean
  error 0.2. So new word lists can get original-equivalent scoring by computing difficulty from the reading.

### Difficulty model and easy packs (implemented, 0.6.0)

- **Model** (`mod/tools/fit_difficulty.py` → `src/unit_costs.c`, `difficulty_model.json`): difficulty = Σ integer
  key costs over the first spelling of each typing unit (A/S/T/H 3, most letters 4, C/M/N/X/Z 5, P/Q 6, V 8,
  space 2, digits 9–11, symbols 12–14), a small っ (unit 203) costs the consonant it doubles, plus corrections for
  18 rare units (THI/DHI/TSA/ヴ… whose original spelling choice differed). Reproduces **99.92 %** of the 30 801
  distinct word.HAB_O readings. The English version uses the same weights (`word_tool.py KEY_WEIGHTS`).
- **Loader**: a line may be `word`, `word<TAB>reading`, or `difficulty<TAB>word[<TAB>reading]` (`auto` allowed);
  missing difficulty is computed by `word_difficulty()`. Letters without decomposition are transliterated
  (ß→SS, Æ→AE, Ø→O, Ł→L, Þ→TH, Œ→OE, Đ/Ð→D, ı→I, ŋ→NG …). Check: word.HAB_O as `word<TAB>reading` only →
  504/527 lists byte-identical (rest differ by 1–2 difficulty on a few words); old format still 527/527.
- **List structure** (`mod/tools/gen_pack_template.py` → `config/pack_template.c`, names + slot difficulties
  only): 957 lists (527 main, 430 kids). **S000 = main game pool** (20 lists, 42 819 of 49 406 words, short→long).
  Quiz lists (first record is a question typed `？`, unit 286): all SE01, 98 SE08, 1 SE00; SE06 = special mode
  (high-half groups) + quiz. 525 lists keep the game's words, 432 are fillable.
- **Pack builder** (`config/pack_builder.c`, configurator button "New pack from word list…", CLI
  `tests/build_pack_cli.c`): reads a UTF-8 list (word[TAB reading]), drops untypable/>24-glyph/duplicate lines,
  computes difficulty with the loader's code, and for each fillable list slot picks an unused-in-list word within
  ±max(2, d/8) difficulty (widening as needed), least-used first, random ties. 385-line German sample → 432 lists
  in 0.4 s; loaded in game (S000L010, 674 words, no warnings).
- **English** (`mod/tools/english_to_pack.py`): English files are XOR 0x41, 12-byte records {group, display ptr,
  typed ptr}, typed = unit bytes from 162 (`0-9A-Za-z\.,?!_[]-% '$#@.,-"&/():*`). Same family/number assignment as
  transplant_tool; quiz question first (12, `?`); >24-char lines dropped in S000, other lists then left to the
  game. 204 lists (S000 13, SE00 9, SE01 100, SE02/03 1, SE05 76, SE07 4); matches the live transplant except an
  equivalent comma unit. Installed as `TODEX\dict\english`.
- Verified by the user in gameplay (2026-10-26): configurator button built `german_sample` (S000L010 674 words,
  S000L020 2 612 words loaded), German words shown and typable; English pack works.
- Self-test without the user: set `Tod.ini Active=true` (back up first) so the attract demo runs unfocused; the
  first list (S000L010) loads after ~60 s; close with `close_game.ps1`; restore Tod.ini.

### Half-width Latin text (implemented, 0.7.0)

- Prompt layout (`FUN_004c84f0`, `FUN_004c66b0`): when `DAT_00a1b6b2 == 1` (set to 1 at init, `FUN_0044fed0`)
  every 2-byte cell advances `size*0.75`; otherwise a proportional mode uses tables at 0x00547c60 (advance) and
  0x00547d50 (left offset) for codes 0x8260–0x829A (caps 0.9, lowercase 0.8, i/j/l 0.6 + 0.2 offset) and 0.4 for
  everything else; total width `FUN_004c6540`. Not used by todmod.
- `word+0x50` holds a **pointer to the record's display string** (`FUN_004021c0`), and every cell is rasterised via
  `FUN_00456140(display + 2*i, texture, mode)` (cdecl), which copies the 2 bytes and calls `FUN_00455ff0`.
- todmod (`[text] half_width=1`, default): a display made only of characters < U+2E80 (after LCMAP_HALFWIDTH) is
  stored as ⌈n/2⌉ cells of code 0xF9FC (reserved from the token range) plus the text in a parallel UTF-16 pool.
  Hook `FUN_00456140` maps the cell pointer back to (word, cell) via the pack's display pool; the
  `FUN_00455ff0` hook renders the two characters with `half_width_font` (default Consolas, capitals 20 px,
  uniformly squeezed to 12 px per character) into the carrier glyph. 24 cells now hold 48 Latin characters.
  Offline preview correct (Latin, Greek, Cyrillic, ß, Ł); verified by the user in gameplay (2026-10-26): narrow
  English text looks good and is typable.
- English converter limit raised to 48 characters → 420 lists (all S000, SE04 boss lines, SE06); only SE08/SE09
  stay with the game's words.

### Master TSV packs, full English coverage, coverage check (implemented, 0.9.0)

- List kinds (`mod/tools/word_lists.tsv`, from `gen_pack_template.py`): main 236 normal / 200 quiz / 91 special,
  kids 196 / 172 / 62. Quiz lists = question (difficulty 12, reading ？) + answers; SE01 and SE08 quizzes have 19
  records, SE00 lists 3, SE06 (special) 4. English quizzes (ZE01, 98 files, 19 records) list correct answers first
  and wrong ones after (e.g. "Something that will help ease a cold?": Chicken soup … Warmth, then Exorcism …),
  same layout as the Japanese quizzes. English has no SE08/SE09 family and no Kids' Mode bank.
- Loader: a pack may contain `words.tsv` (main) / `kids.tsv` (Kids' Mode), columns
  list(.bin optional)<TAB>difficulty(empty/auto ok)<TAB>display<TAB>reading[<TAB>ignored…] — the hab_tool export
  layout (`all.tsv`). Read once per pack, list lines extracted on demand (~7 ms for S000L010). Per-list `.txt`
  files override the TSV for their list. Check: hab_tool export of word.HAB_O as words.tsv → 527/527 lists
  byte-identical.
- `english_to_pack.py` now covers every list: main 420 English lists + 98 English quizzes (SE08) + 9 by difficulty
  (SE08 non-quiz, SE09); kids 321 English lists + 81 quizzes + 28 by difficulty (S000). No CJK characters left.
- Configurator: builder writes words.tsv/kids.tsv (removes its old generated per-list files); "Check coverage"
  lists, per bank and family, word lists that would fall back to the game's words for the current "In use" order.
- Built-in word tables in Tod.exe (record kinds of `FUN_004cb890` other than 2; not replaceable by packs yet):
  kind 1 (`[0x547e78]` → 0x502cf0, 40) and kind 6 (0x5079f8, 89) = kana drills (か, き …); kinds 3/4/5
  (0x504f78 ×27, 0x505dd0 ×28, 0x506cb0 ×25) = staff names (kanji, kanji, kana) for the credits; default
  (0x501fa8 ×25) = letters Ａ…; picker extras 0x513140/0x513250/0x513718/0x5138b0/0x513ad0 = letter, digit and
  symbol drills. No quiz text. A boss-stage "Japanese quiz" report turned out to be a session started without the
  English pack saved (configurator changes apply only after Save / Save and play).
- `[debug] log_word_sources=1` logs, once each, every word the game takes from word.HAB or a built-in table
  instead of a pack (source, list, index, group, text) — for finding leftover Japanese.
- `[text] half_width=0` now means "narrow only for Latin lines longer than 24 cells" (so long English lines still
  fit); 1 = narrow for all Latin/Greek/Cyrillic words.

### Built-in word tables and keyboard layout (implemented, 0.10.0)

- Built-in tables (record = u32 group, display pointer, u16 reading[64]; 0x88 bytes). Kind selected by
  `FUN_004c6c60` from enemy flags (+0x194: 0x10 → kind 0 letters, 0x20 → kind 1 kana, 0xc0/0x100 → kind 6),
  kana input mode ('K' at 0x547c1c) turns kind 0 into 1; default kind `DAT_007808cc` (0/1/2) from `FUN_004c97c0`;
  kinds 3–5 come from `DAT_0051cce0` (= 3 in the file) in `FUN_00420f50`.
  - default 0x501fa8 ×25 letters Ａ…Ｚ (no Ｏ), group 0x0100xxxx;
  - kind 1 0x502cf0 ×40 single kana, group 0x0200xxxx (pointer `[0x547e78]`, count byte 0x547e7c);
  - kind 6 0x5079f8 ×89: 40 kana, 25 letters, ！！ ！？ ３Ｋ ９時 ＤＨ ＩＱ ＯＬ ＯＮ, 16 two-kana words;
  - kinds 3/4/5 staff names: 0x504f78 ×27 (kanji, high16 = stage/flags), 0x505dd0 ×27 + terminator (kanji),
    0x506cb0 ×24 + terminator (kana); out of range → zero record 0x501f20, which ends sequential walks;
  - drill array 0x513140 ×28 used directly by `FUN_004c6dd0` (modes 0xa19598 = 7 or 10): Ｆ Ｊ, １–９ ０,
    「 」 ￥ ？ ！ ％ 。 、, ぁ ぃ ぅ ぇ ぉ ゃ ゅ ょ. Picker addresses 0x513250/0x513718/0x5138b0/0x513ad0 are
    slots 2/11/14/18 of this array.
- todmod overwrites the records in place (VirtualProtect) on the first word lookup (after the font is loaded) from
  pack lists `builtin_letters`, `builtin_kana`, `builtin_short_words`, `builtin_drill`, `builtin_staff_stage`,
  `builtin_staff_kanji`, `builtin_staff_kana` in `words.tsv`; slot for slot (repeated if the list is shorter),
  high16 of the group kept, difficulty = original × cost(new) / cost(original). Confirmed in game (log, attract demo).
  `english_to_pack.py` writes English versions (two-letter words for kana, ASCII symbols, staff names romanised
  from the user's Tod.exe); coverage check reports them.
- Keys: `FUN_00456700(key, modifiers)` maps a USB HID usage code (a = 0x04 … z = 0x1d, 1 = 0x1e … 0 = 0x27,
  space 0x2c, symbols 0x2d–0x38, JP ro 0x87, yen 0x89) through JP106 tables 0x4efe08 (plain) / 0x4eff08 (Shift,
  modifiers & 0x22). US ' (usage 0x34) gave ':' and Shift+7 gave "'". todmod hooks it and returns the character of the
  active Windows layout (MapVirtualKeyEx + ToUnicodeEx, accents folded to ASCII) for printable results;
  control results (Shift+E/Z/0 = 18/…) untouched. `[input] keyboard_layout=japanese` keeps the built-in table.
- Key codes come from `FUN_0049adc0`: DirectInput state (0x77a260, 256 bytes) → usage via table 0x77a628, filled
  once by `FUN_0049b290` with JP DirectInput codes (DIK_CIRCUMFLEX 0x90 → 0x2e, DIK_AT 0x91 → 0x2f, DIK_COLON
  0x92 → 0x34, 0x1a → 0x30, 0x1b → 0x32). On a non-Japanese Windows layout DIK = scancode, so ' (0x28), = (0x0d),
  ` (0x29), \ (0x2b) had no usage and [ ] were one key off: the US ' key never reached the judge (user report,
  0.10.0). todmod hooks `FUN_0049b290` and writes the positional scancode → usage map after it (skipped when the
  Windows layout is Japanese or `[input] keyboard_layout=japanese`).

### Case-sensitive typing (implemented, 0.11.0)

- Gameplay judge: `FUN_004cb5a0` (romaji mode 'A') / `FUN_004cb710` (kana mode) → `FUN_004026f0(state, char)`
  (cdecl; romaji → `FUN_00402230` with EAX = state, DL = char). The character is `toupper`ed first, so case is
  lost. `state[0]` = word context (`[0]` display, `[1]` = &record->reading, +8 romaji text, +0x49 index),
  `state[0x21]` = pointer to the current reading unit, `state[0x23]` = typed buffer. Return 0x10 = miss (key
  stats `0x548088[key] += 0x14`), 0x40 = unit done, 0x80 = word done. On a miss without Shift, `FUN_004cb5a0`
  retries with the shifted character if Shift was down on the previous key (`DAT_00816908`, early-release
  tolerance). `param_3 == 1/2` paths feed the expected character (demo / assisted).
- `FUN_00402c20` is the text-entry box (Home/Delete/End usages), not gameplay.
- todmod: packs record each unit's case from the reading text (letter units only; transliterations inherit the
  source letter's case; kana and symbols any). `[typing] case_sensitive=1` hooks the judge: when the judged
  letter is the key just pressed and its case differs from the word's, the judge gets a different letter, so the
  game counts a normal miss. Only pack words (incl. built-in tables) carry case. `case_scoring=1` adds
  `uppercase_points` (default 4, about one key; shifted digits cost the model +3/+4) per capital to the difficulty.
- Correction (0.11.1): the typing state holds its own copy of the reading at `state + 4` (64 units, `state[1..0x20]`)
  and `state[0x21]` points into that copy, not into the record (0.11.0 never matched, so nothing was checked).
  `FUN_00401600` splits compound units in the copy as they are typed, so the copy index is mapped back to the
  record by walking both (a mismatch = split, skip one extra copy unit).
- The attract demo types by simulated keys through the same path (without Shift), so only keys physically down
  (`GetAsyncKeyState`) are case-checked. Shift+E / Shift+Z come out of the JP table as 0x12 / 0x13 (callers turn
  them into E / Z): counted as capitals.
- Confirmed in game 2026-10-27 with `mod/tests/make_case_test_pack.py` (`dict\case_test`) and `[debug] log_typing=1`:
  wrong-case keys logged as "miss (wrong case)" (result 10), right-case keys judged normally.
  User confirmed 2026-10-27: case-sensitive typing and the capital scoring work in play (case_test pack).

### Textures and texture packs (implemented, 0.12.0)

- Texture tables: Table.dll exports `TexFileNameTbl` (char*[bank]), `TexHeaderTbl` (header*[bank]), `BankTexMax`
  (short[bank]); bank 0 unused, 531 banks. Header = {u16 width, u16 height, u8 pixel format (0 ARGB1555,
  1 RGB565, 2 ARGB4444), u8 data format (1 twiddled, 3 VQ, 5 4-bit palette twiddled, 9 rectangle,
  13 twiddled rectangle), u16 pad, u32 offset in the decompressed bank, u32 id}; ended by width 0.
  Rows are stored bottom-up.
- Loading: 2D banks via `FUN_00499850` (PRS → buffer, then HglNa ordinal 167 `nlScrollLoadTexture(?, index,
  headers, buffer, ?)` per texture, buffer freed after); 3D banks via the `PbkTbl` path and
  `nlObjPutLoadTexture`. Both create textures in HglNa `FUN_10006ab0` (cdecl `texture *(header *, pixels *)`;
  handles data formats 1/3/5/9/…). todmod hooks it: header pointer → bank/index (sorted header ranges), then
  `textures\<pack>\<bank>\<NNN>*.png` (GDI+) encoded as a rectangle in the header's pixel format and passed with
  a copied header (data format 9). Same size only for now (2D texture sizes are also stored by
  nlScrollLoadTexture for UVs). Confirmed in game 2026-10-27 (scr_tod_msel, scr_tod_ssel, scr_etc_1 replaced). User confirmed: English mode select, chapter select and mission captions
  show correctly.
- English version (Tod_e.exe, timestamp 3B5B8887): same header layout; bank names at 0x4F0108, header pointers
  at 0x5872C8 (index 1 = common.bin), 548 banks. Of 548 shared banks only 71 are byte-identical; most 2D
  screens keep the same layout (same texture sizes) with different encodings (English: VQ, twiddled and 4-bit
  palette; EX: mostly rectangle). Translated textures: mission captions (scr_tod_etc3, sirei*), tutorial
  (scr_tod_tutr), mode/chapter select (msel, ssel), VS CPU, results, boss6 #0 ("Weakpoint Unknown"). The English
  version left some Japanese (item names in scr_item_*, "Enterキーで選択"). 3D differences are art changes
  (signs, effects, green blood), not text; tod_les_blaster/notepc show Dreamcast hardware.
- Not converted yet: 50 banks with a different layout (scr_title, scr_tod_option, scr_tod_dialog, scr_tod_time,
  lessons, ext*, boss menus …; would need per-texture matching) and 1366 English 4-bit palette textures (palette
  location unknown; scr_common, scr_adver, scr_endstaff, item names).

### Larger textures, English matching and palettes, music (implemented, 0.13.0)

- Larger replacements: the creator sizes the D3D surface from the header, and nlScrollLoadTexture keeps the
  original size for 2D UVs, so a copied header with 2x/4x/8x width/height draws the sharper texture in place.
  Confirmed 2026-10-27 by screenshot (scr_tod_tutr at 1024x256 instead of 512x128).
- Screenshots without the user: `mod/tools/capture_game.ps1` (Tod.ini Window/Active temporarily true, PrintWindow
  of the game window; no other screen content). Attract sequence: HOD1_ADV intro, tutorial demo with ST1.
- English matching for different layouts: Needleman-Wunsch over the texture lists (only same-size pairs,
  score 0.2 + correlation of 32x32 premultiplied luminance/alpha signatures, pairs below 0.3 dropped). Reviewed by
  eye: scr_title (different art), scr_boss/scr_tod_res_boss (Dreamcast credits), ext01 and most of ext02/04,
  les_fukidasi #5, org_p2 #6, training counters and Dreamcast hardware/VMU screens are excluded.
- English 4-bit palettes: 111 ARGB1555 palettes of 16 entries at file offset 0x113630 of Tod_e.exe (0x113630 to
  0x114410). One palette per bank (found by matching textures whose picture equals EX): subtitles 4, item cards
  52, lessons 24, training 20, les_hand 25, item list 36, ranking 50. Translated 4-bit banks use: item cards 52,
  scr_tod_les_03/les_com 24, scr_training_sele 17 (chosen for legibility), scr_tod_org_p2 17. The texture to
  palette link itself is in the code (header pad field is 0). English pack: 188 textures in 57 banks.
- Music: Tod.exe calls HglCore `wave_play(const wchar_t *path, int loop)` (export 128, cdecl) with
  L"Sound/bgm/<TRACK>.WMA" (track names at 0x53b0f8); wave_play builds a DirectShow graph (CLSID_FilterGraph,
  IGraphBuilder::RenderFile, IMediaControl, IBasicAudio) and the game loops via wave_status/wave_replay. todmod
  hooks it and passes another file: WAV/MP3 directly (DirectShow decodes them), OGG/FLAC decoded once to
  `music\.cache\<source>_<TRACK>.wav` (stb_vorbis 1.22, dr_flac 0.13). Confirmed 2026-10-27: WAV, MP3 and FLAC
  played in game (intro and attract stage); OGG and compressed FLAC decoded (ffmpeg-made test files).
  OGG confirmed playing in Arcade stage 1 (2026-10-27, unattended run).

### Unattended gameplay, English mode (2026-10-27)

- Reaching gameplay: Enter repeatedly from launch → Arcade mode stage 1 (user tip). `mod/tools/capture_game.ps1`
  presses keys only when the game window is in front (AttachThreadInput + SetForegroundWindow, checked with
  GetForegroundWindow); `-EnterFrom/-EnterUntil/-EnterEvery`, `-TypeFrom/-TypeUntil/-TypeEvery` (key A).
- `[debug] autotype=1`: in the judge hook a physically pressed letter is replaced by the word's next romaji
  character (`word + 8 + word[0x49]`), so holding/repeating any letter clears stages. Stage 1 incl. boss cleared in
  ~4 min; results then wait in name entry (A types あ there, hiragana mode by default).
- EX voice files are byte-for-byte the English dub (SOUND\VOICE vs English sound\voice, correlation 1.00), so
  cutscene subtitles are Japanese images over English speech.
- Game language flag `DAT_00a1b6b2` (short): set to 1 (Japanese) in `FUN_0044fed0` at 0x44ff0f (next to
  `DAT_00a1b6b0`); read by the subtitle task `FUN_00425f80` (1 → Japanese subtitle image from 0x51cfd0 table,
  else English text from 0x40-byte records at 0x51eea8/+0x44 drawn by `FUN_00425140` with the Latin subtitle
  font scr_jimaku_e), prompt layout (`FUN_004c84f0`, `FUN_004c66b0`), `FUN_004753a0` (table 0x52afc4[lang][mode])
  and others. EX still contains the English subtitle strings ("Harry and Amy are coming to back us up.").
  todmod `[text] language=english` hooks `FUN_0044fdc0` (void, applies settings right after the flag is set) and
  sets the flag to 0. Confirmed by screenshot: English subtitles ("Thanks, you can get out side that way."),
  no Japanese boss caption, results and boss fight normal; the reading line under a word shows the word itself.
- Still Japanese in English mode: "をゲットした" (scr_common, item get), name entry "F1:ひらがな入力 / F2:カタカナ入力 /
  F3:全角英数入力" (scr_tod_dialog; the English version has the same Japanese). The cutscene caption
  "Enterキーでキャンセル" (scr_common #23, 512x32 ARGB4444, orange outlined) is replaced by a generated
  "Press Enter to skip" (Arial Bold, 2x) in the English pack; the timer counter "残り n 体" (scr_tod_time #17/#18)
  by the English "Rest" (#33) + empty. In-stage cutscene letterbox bars of stage 1 span the full width.
- 0.14.0 confirmed by screenshot (attract demo cutscene, 1920x1080): "Press Enter to skip" drawn crisp at top
  right (1024x64 PNG loaded into the 512x32 slot), English subtitle "James..., I tried...". The language flag
  hook installs from `[text] language=english`; the configurator checkbox reads it back.

### 3D models (confirmed from code, implemented 0.15.0)

- Tables (Table.dll exports): `PbkTbl[model id]` (u16) = bank (0x102 = none), `PolFileNameTbl[bank]` = pol.HAB file,
  `PatNoTbl[bank]` → short list of the bank's model ids, `BankPatMax[bank]` = count; the bank's textures are tex.HAB
  bank with the same index (`TexFileNameTbl`/`TexHeaderTbl[bank]`). 355 banks, 10131 models, 1.45 M triangles.
- Model table in Tod.exe `0x009f3ac0 + id*16`: {buffer, object, short prev, short next, u16 flags (0x8000 loaded,
  bits 1–3 state, bit 0 textured)}. Load queue `0x0077ec38` (64 × 16 bytes), handlers `0x5470c0`
  (0/1 single model → jump table `0x52cbc4`, 2 free single `0x48b940`, 3 whole bank → `0x52cbe4`, 4 free bank
  `0x48bf90`). Single model: `0x48b620` open + read offset table, `0x48b700` read the model's block
  (`PatNoTbl` position i: {u32 start (bit 31 = no textures), u32 object offset}, block ends at entry i+1),
  `0x48b890` read textures, `0x48c5c0` finish (link, HglNa ord 117 `nlObjPutLoadTexture_hod2(object, headers,
  data)`, `FUN_0048c120(id)` per-model fix-ups). Whole bank: decompressed (`FUN_00451930`), `0x48c680` finishes
  one model per call (id = `*(short *)*0x007586ec`; object = buffer + object offset). Pointers in objects are
  relative, so objects can be copied anywhere.
- Object format (drawn by HglNa `FUN_10003920`/`FUN_100034a0`…, FVF 0x112): u32 kind (always 1), u32 flags
  (bit 0x10 = don't draw vertices), float sphere x, y, z, r; then commands: 0 = end; bit 31 = material chunk,
  0x50-byte header (+0x10 sphere, +0x20 texture index in the bank or −1, +0x24 specular power exponent,
  +0x30 diffuse rgb, +0x40 specular rgb, +0x4c byte size of its strips); otherwise a strip {u32 flags, u32 count}
  of count vertices (flag 8: triangle list, count × 3 vertices; low byte ≥ 0 sets render state via
  `FUN_10002800`). A vertex is 8 floats x y z nx ny nz u v with the low bit of x set; with flag 0x20 a vertex
  may instead be {u32 even tag, s32 byte offset from its end} referring to an earlier vertex. All 10131 models
  parse; all 43806 textured chunks index within their bank (`mod/tools/model_formats.py`).
- todmod `model_hooks.c`: hooks `0x48c5c0`/`0x48c680` (remember the id being finished; afterwards replace
  untextured models) and HglNa+0x6f40 (`nlObjPutLoadTexture_hod2`: swap in the replacement before its texture
  indices are registered, so `nlObjPutFreeTexture` later releases the right textures). Replacements:
  `models\<pack>\[<bank>\]<id>.obj|.bin`; OBJ → one chunk per material (header copied from chunk `m<k>`,
  texture `_t<n>`), triangle lists of ≤ 1000, flat normals if missing, spheres recomputed; z and v flipped.
  `[models] dump=1` writes `models\dump\<bank>\<id>.bin/.obj/.mtl` of each loaded model; `[debug] log_models=1`
  → `todmod_models.log` (TSV). Verified 2026-10-27: C round trip bin → OBJ → object reproduces all 148
  triangles of model 8735 and its sphere; in game 1122 models dumped in the attract demo, the 156 komono_1
  props replaced by 3x-stretched OBJs (books, chandelier, chair visibly stretched, textures intact), no crash.
- Not covered: `FUN_0048c1e0` (bank 1 / common models loaded at start through another path).
- Waist belts: `FUN_00493f50(part, vertices)` (called per body part by `FUN_0047aae0`) builds a belt from
  `BeltVtxDb[kind]` (+4 → per part: model id, belt data with byte offsets into the model object) and moves those
  object vertices at run time. With a replaced object the offsets point into unrelated bytes, so todmod 0.20.0 hooks
  it: for a replaced model it clears the part's belt slot (`*(character+0x40) + 4 + part*8`) and returns 0.
- Texture packs (0.20.0): replacements may have any power-of-two size; a model pack's `textures\<pack>` folder is
  used as a texture pack automatically. Model banks share texture ids (e.g. 7227 in znsam2 and znsam2b): a texture
  created by an earlier bank is reused, so a model pack's atlas needs the `_id<id>` file name.

### Packs mixing, keyboard layouts, texture log (implemented, 0.15.0)

- `[dictionary] mix=1`: each list's text from every active pack is concatenated and loaded as one list. Word
  picking (`FUN_004c6b90`, inside `FUN_004c6dd0`) draws random indices over the whole count with a 16-entry
  recent-history ring and filters by difficulty, so order and size do not matter. Built-in tables (fixed slots)
  take evenly spaced words from the mixed list. Verified: S000L010 → "mixed: case_test + english".
- `[input] keyboard_layout=qwerty|dvorak|colemak|workman`: characters come from built-in tables by scancode
  position (rows 0x02/0x10/0x1E/0x2C, plus ` \ space) instead of the Windows layout; Shift+E/Z control codes
  (0x12/0x13) are remapped when the layout puts something else there. Tables checked offline
  (`mod/tests/layout_tables.c`); in game only the log line was checked.
- `[debug] log_textures=1` writes `todmod_textures.log` (TSV: time, bank, index, texture id, size, formats,
  replacement).

### Model textures by texture id (implemented 0.15.1)

- Header +0xc is a global texture id; HglNa caches created textures per id (`DAT_11214228[id]`, ref counts in
  `DAT_11214238`), and banks share ids (e.g. 1670 in all b6boss banks), so a texture is created once, from the
  first bank that loads it. Texture packs now also match `*_id<n>*.png` / `id<n>.png` anywhere in the pack
  (≤ 3 folders deep) by header id, after the bank\index match of the same pack. model_tool exports and the
  in-game .mtl dumps name textures `NNN_id<id>.png`.
- Verified 2026-10-27: flat pack `textures\modeltest\` with the 8 komono_1 textures (VQ 128x128 originals)
  channel-swapped at 256x256 — all 8 replaced by id in the attract demo; the crates by the well render lavender
  with the sharper texture.

### Cutscene skipping (implemented 0.16.0)

- Skippable cutscenes run the skip-caption task `0x4261a0` (not found by Ghidra; found via the only `push 0x294`,
  sprite 0x294 → texture 2007 through the sprite→texture table `0x536e20`; sprites are queued by `FUN_004992f0`
  into the list `0x758be0` and drawn by `FUN_004993b0`). Each frame: if `0xa18fa8` == 0 the task ends; if byte
  `0xa3e808` (skip request) is set it clears it, sets `0x9f0adc` = `0xa3e7a4` = 1, calls `FUN_0049d2c0` and turns
  into `0x4250f0`; otherwise it queues the caption on 45 of every 60 frames. The request is set by the player task
  `FUN_00486fa0` when `0xa18fa8` ≠ 0, `0xa3e724` == 0 and Start (8 / 0x80000 for P2) is newly pressed.
- todmod hooks the task and sets `0xa3e808` after the task has run 30 frames (earlier requests are lost while the
  cutscene starts), not in title/demo modes (`0xa3e308` 3/5/9/10/11). Verified 2026-10-27 in Arcade (mode 6): the
  stage-1 intro and the agent scene are skipped (gameplay at 30–60 s instead of cutscenes); the well scene has no
  caption task (unskippable in the original too).

### Item effects and cheats (implemented 0.16.0)

- Item effect switch `FUN_004570c0` (item number in EDI) sets bits in `0x9f0be0`: 5 → 2 (Balloon Head), 6 → 4,
  0xb → 8 (+2 to `0x9f0be4`), 0xc → 0x10, 0xd → 0x20 (+ `FUN_00413120(1)`), 0xe → 0x40 (zombie suit, clears 0x80),
  0xf → 0x80 (elderly suit, clears 0x40); 7–10 change `0xa3e9a2` by −2/−4/−6/+2; 0x11–0x15 go to the inventory.
  Bits 4/8 with 0x100 steer word choice (`FUN_004c6dd0`, `FUN_004c9a40`); 0x20 changes a sound (`FUN_00486fa0`).
- Bit 2: `FUN_004390c0` creates each head with scale 2.0 (1.8 for character kind 0xe). The attract demo
  (`FUN_0049e8a0`) turns it on 1 run in 10.
- Bits 0x40/0x80: model loads `FUN_0049ccc0`/`FUN_0049ce30` replace player banks 0xc4 player1 / 0xc5 player2 with
  0x92 znsam2 / 0x93 znsam2b (0x40) or 0xbf hito_oyaji / 0xac hito_baba (0x80); `FUN_0047b3f0`, `FUN_0047ea70`,
  `FUN_0047dcd0`, `FUN_00494590` also read them.
- todmod `cheat_hooks.c` ORs the chosen bits into `0x9f0be0` after every `FUN_00456650` (per-frame input update).
  Verified 2026-10-27: big heads on zombies and the agent; with the zombie suit no player1/player2 model loads,
  znsam2 (73) and znsam2b (37) load instead.

- Suit side effect 1 (fixed): zombie code loads znsam2/znsam2b (146/147) itself; with the suit the player banks
  196/197 map onto them, and the player free after a cutscene freed the zombies' models → invisible zombies.
  todmod skips free requests for 196/197 while a suit cheat is on (`FUN_0049ce30` hook). `[debug] log_banks=1`
  logs every bank load/free request.
- Suit side effect 2 (fixed): in the stage-1 opening car cutscene the suited player was invisible. The suit
  remaps character kinds '9'/':' (players) to 7/8 (zombie) or 0x34/0x21 (elderly) in `FUN_0047b3f0`/`FUN_0047ea70`,
  but the car scene's player entry is not remapped and still uses the real player1/player2 banks, which the suit
  never loads (196/197 → 146/147). todmod now also requests the real 196/197 (suit bits cleared for that one
  request) before the suited request. Verified 2026-10-27: James visible in the car (normal clothes in that scene).
- "Balloon Head Meat" is the Balloon Head item (card: "Everyone's heads grow"); picking it up sets the same bit.
  Head scaling is per frame in part-draw callbacks not found by Ghidra (0x4397c2, 0x43a3f0, 0x4466f0: ×2.0 for
  part 2; 0x4783d4, 0x4784d6, 0x485dfb: ×1.5) plus `FUN_004390c0` for flying severed heads.

### Characters and player swaps (implemented 0.17.0)

- Character kind k: `BodyInitTbl[k]` (Table.dll, 88 kinds 0–87) points to the root of a body tree; the nodes of
  kind k lie in memory just before its root. Node: +0 u32 model id (0 = none), +4 float x, y, z offset from the
  parent, +0x10 u32 flags, +0x14 s16 part id, +0x16 s16 child count, +0x18 child pointers (`FUN_004381c0` walks
  it). Humanoids have 16 nodes (root + 15 parts). `mod/tools/character_kinds.py` lists kinds; 0 Harold,
  1–25 zombies, 26–31 animals/bats, 32–55 civilians, 56 detective girl, 57/58 players 1/2 (banks 0xc4/0xc5),
  59–61 player3/4/gold, 62 Hou, 63 Logan, 64 detective, 65–70 story characters, 71–85 bosses, 86–87 extras.
  (0.17.0 read kind k's banks from the bytes after root k−1, which happen to be kind k's nodes; 0.18.0 walks the
  tree.) Gaps at the waist between parts are filled at run time from `BeltVtxDb`.
- Player kinds are set in three places: script characters `FUN_0047ea70` (kind = script code '9'/':' = 57/58 at
  `*(obj+0x1394)`, stored at +0x1f8; suits remap to 7/8 or 0x34/0x21), event characters `FUN_0047b3f0` (code at
  entry+4 → obj+0x60), gameplay players `FUN_0048b200` (kinds from the short table `0x52c800`, stride 4 bytes).
  Motion banks come from separate tables (`0x4fc904`, `MbkTbl`), not from the kind.
- Built-in costume swap (modes 5/6 of `0xa3e248` only): `FUN_004b9990` sets `0xa3e860` (code 0x39+n),
  `0xa3e864` (bank 0xc4+n), `0xa3e868` kind, `0xa3e86c` bank, `0xa3e874` held-prop model from the 12-byte table
  `0x500eb0` (9 entries: kinds 32, 60, 52, 59, 58, 58, 63, 63, 59). Suit keyboard models: +0x108 = 0x1d69/0x1d6a
  (`FUN_00494590`).
- todmod `character_hooks.c` (`[cheats] player1_character`, `player2_character`): patches `0x52c800`, rewrites the
  script code to the chosen kind around `FUN_0047ea70`, overrides `FUN_0047b3f0`'s result, and (through the bank
  hooks in `cheat_hooks.c`) requests the chosen kind's banks with each player bank and never frees them.
  Verified 2026-10-27: kind 1 (znassb) as player 1 in the car and agent scenes; kinds 21, 24, 27, 62, 73, 76 run
  without crashes; 76 (boss6) and 24 (zslman) visible in the car scene, 73 (boss3, 27 parts) and 27 (frog) not.

- Configurator "Characters" tab (0.18.0, `config/character_page.c`, `config/game_models.c`): loads Table.dll from
  the game folder, reads pol.HAB/tex.HAB (HAB0 index, PRS), places each body node's model at the summed offsets,
  decodes textures (rect, twiddled, VQ; 1555/565/4444) and rasterises a textured, lit front view (turnable).
  ~10 ms per character. `todmod_config.exe /characters [kind]` opens the tab. Settings: `player1_character` /
  `player2_character` = kind 0–87, −1 = normal. Verified in game: kind 0 (Harold) drives the car in the opening.
- Player equipment (0.19.0): the Dreamcast, battery pack, keyboard and cable are not separate models but material
  chunks inside the player torso meshes (model 5687 player 1, 5707 player 2), with textures 5363–5366,
  5512–5596, 5752–5756 (body textures 847/848, 733/734, 3312, 4518/4528 are not equipment). Keyboard props
  0x1d68 etc. in tod_etc2 (bank 341) are other scenes' props; player-bank models 5456–5470 are lip-sync heads.
  todmod (`[cheats] keep_equipment=1`) copies the real torso once, collapses its non-equipment chunks to a point,
  and draws the copy with the same `nlObjPut*` call right after a swapped player's torso (part id 1 under the
  root). Skipped for kinds 57–61. Verified in game: kind 13 shows battery, Dreamcast and keyboard in the car scene.

### Randomizer (0.22.0)

- Design follows the MIT-licensed HOTD2 rebuild github.com/GitFist/hotd2-re (`src/game/world_randomizer.cpp`,
  `notes/agents/randomizer.md`): stable per-run choices from a seed, appearance-only character swaps, voice lines
  keyed by their subtitle so text and audio match. No code was copied; its addresses are HOTD2's, not Tod.exe's.
- Looks: Table.dll `BodyInitTbl`, `BeltVtxDb` and `ShadowTbl` (pointer per kind, 88 kinds) are permuted once at
  start within pools of 16-node kinds: zombies 0–20, 22, 24, 25 and people 32–56, 62–64, 66, 67 (kind 68 is the stage 1 boss, removed 0.26.0; players 57–61 and
  bosses untouched; `mix` shuffles both pools together). Behaviour, words and strength stay with the kind; damaged
  part models come from the kind's own tables (a shot-off piece can show the original look). Banks: when the game
  loads bank B, the banks of the bodies now shown for B's kinds are requested too and never freed
  (`cheat_hooks` bank hooks). Verified (seed 12345): stage 1 zombies and cutscene people swapped, no crash.
- Voices: event table `0x51e8f0`–`0x51eea8` (244 events × 3 variants of subtitle ids, `FUN_00426060`); a subtitle id
  carries its voice (`0x51cfd0 + id*16`, +0xc sound, kind 2 = voice) and its text (`0x524a28`), so permuting the 333
  voiced ids in the table changes line, voice and subtitle together. Verified: car-scene lines replaced.
- Items: `FUN_004570c0` (item number in EDI) hooked through a naked wrapper; items 5–15 get a random id 5–15
  (collectibles 0x11–0x15 and other items unchanged). Hook installs; a pickup was not yet seen in a test.
- Not done: extra enemies per encounter (actor slots, word assignment), enemy weapons (tied to kinds).

### Words in file order (0.20.0)

- Picker `FUN_004c6dd0(word)` (cdecl, from `FUN_004c75a0`): its random draws call `FUN_004cb890(kind, index)` from
  return addresses 0x4c71c8, 0x4c7228, 0x4c7276 (after `FUN_004c6b90`) and 0x4c733e (fallback loop); scripted
  paths (0x4c6ec6, 0x4c6ef9, 0x4c6fd1) and the built-in tables (kinds 0/1/3–6, e.g. stage 1's two-letter words
  from builtin_short_words) are not these. The picker always uses the last record it fetched.
- todmod `[dictionary] in_order=1`: loads the top pack's `sequence.txt` (else its words.tsv without the list
  column) as one more word pack; `get_word_record_hook` returns the current sequence record for kind 2 at those
  return addresses unless the game's own record is shorter than `in_order_min_length` (4) reading units; a hook
  on the picker advances the position when a sequence record was handed out, saved in
  `dict\<pack>\sequence_position.txt`. Letter cases / half-width text also look in the sequence pack.
  Verified in game: autotype run reached sequence lines 10 and 11 ("To die to sleep", "No more") in order.
- The configurator's pack builder writes `sequence.txt` (usable lines in source order) before de-duplicating.

### Typing input details (0.19.0)

- Word-box task `FUN_004ca960` cancels the current word when the newest key byte `0x7579c5 + player*0x2c` is
  0x29 (Esc usage). todmod `[input] backspace_cancels=1` turns 0x2A (Backspace) into 0x29 for that call only, so
  name entry (`FUN_00402c20`, 0x2A = delete) is unchanged. Hook install confirmed; cancel not yet seen on screen.
- Typing unit 302 = full-width space (spelling " "); the difficulty model costs it 2, so spaces are typed and
  scored in the original. `[typing] type_spaces=0` removes unit 302 from pack readings after the difficulty is
  computed (score unchanged).

### Music loop points (0.19.0)

- HglCore `wave_play`/`wave_stop` (the only BGM imports of Tod.exe) build a DirectShow graph and loop by seeking to
  0 on EC_COMPLETE (0x8609). todmod plays WAV (and OGG/FLAC decoded to WAV) itself through waveOut
  (`src/music_player.c`, 4 × 8192-frame buffers), wrapping LOOPEND → LOOPSTART; MP3 stays on DirectShow.
  Tags: OGG/FLAC comments, WAV `smpl` chunk or text. Verified by ear/log with a tagged test tone.

### Subtitles (0.19.0)

- English records: 366 × 0x40 bytes at 0x51eea8 (record 366 has 0xffff at +0x3e: end) (+0 x offset, +4 text, +0x3e timing); drawn by
  `FUN_00425140(float x, float y=392, char *text)` (cdecl, only caller 0x426052 in the subtitle task
  `FUN_00425f80`). Subtitle font sheet table 0x4db778: A–Z a–z 0–9 space ! ' , - . ? ~ only.
- Subtitle task `FUN_00425f80(task)`: +0x34 subtitle id, +0x36 timer (decremented each call; 0 → `Ordinal_12`
  ends the task, as do `DAT_009f0adc` / `DAT_00a3e7a4`, both read as 32-bit), +0x38 step; language flag 1 →
  sprite image from 0x51cfd0 (`FUN_00499010`), else text.
- todmod `[text] subtitles=<name>` (`subtitles\<name>.tsv`): hooks `FUN_00425140` (Latin lines through the game
  font, `|` = second line stacked 22 px up) and the task: in Japanese mode the task runs with the flag cleared;
  if the line is not in the file, the task fields are restored and it runs again with the flag set (game image).
  Lines with characters the font lacks are drawn by GDI on the render target (`g_lpD3DDevice` → GetRenderTarget
  (vtable 9) → surface GetDC (17)/ReleaseDC (26)) in a HglCore `VSync` hook before the present, centred, top at
  392/480 of the height, Meiryo bold (`subtitle_font`). Verified in game (1920×1080, both modes): user's
  Japanese.tsv lines 2 and 4 shown horizontally at the bottom; the vertical images are gone. Fallback to the
  game image for a missing line not yet seen on screen. `[text] subtitle_windows_font=1` sends every text line
  (file lines and the game's English records) through the GDI overlay; verified (game line "I've already taken
  care of G." in Meiryo, Arcade opening). GDI enumerates Japanese fonts by localized name on this system
  (Meiryo → メイリオ); the configurator resolves a face through GetTextFaceW before selecting it.
- Attract intro (2026-10-27): subtitle ids 392–400 (image table 0x51cfd0 entries 1307–1315) map in the id→record
  table 0x524a28 (4 shorts per id: record per step, −1 ends) to record 364 (empty) in EX, but to records 366–374
  in the English Tod_e.exe (table 0x59bc70, records at 0x595eb0, same 0x40 layout and indices 0–365): "The Curien
  Mansion Incident..." … "AMS agents "Rogan" and "G" went to solve it.". EX's table ends at 366, so the lines were
  dropped. todmod numbers them 366–374 in subtitle files; the task hook remembers the task's id (+0x34) and the
  draw hook maps record 364 drawn for ids 392–400 to those lines. `tools/english_subtitles.py` writes
  `subtitles\english.tsv` from Tod_e.exe; in English mode it is loaded first as a base. Verified in the attract
  demo (1920×1080): intro lines shown at the bottom.
- HglNa 2D: `nlScrPutCore(sprite, slot)` looks the texture up in `DAT_11214228[slot]` (sizes `DAT_11214230`,
  refcounts `DAT_11214238`, `nlInitCommonTexture` sets the table size); translucent sprites are queued and drawn at
  `nlEndScene`, so swapping a slot during the call is not enough. Not used.

### Cutscene skipping: earlier attempts

- Pads: `FUN_00456650` (per frame) clears `0xa3e780` (held) and builds, via `FUN_004562e0`, DC-style buttons from
  the keyboard report: Enter (usage 0x28) = 8 (Start), ')' = 2, O/P/Q/R = 0x80/0x40/0x20/0x10; player 2 << 16;
  `0xa3e788` = newly changed. Report from `FUN_0049afd0` (DirectInput GetDeviceState → `0x77a260`, buffered
  data → `0x77a1c0`). `FUN_00456290(player)` = newest key usage, `FUN_00456260` = modifiers.
- Game mode `0xa3e308`: 6 during Arcade (cutscenes and play); 3/5/9/10/11 title/attract (Start there → title).
- The skip caption is texture id 2007 (scr_common #23), drawn with HglNa `nlScrPutCore(sprite, texture id)`.
- Attempt 1 (pulse Start bit 8 into `0xa3e780/788` after `FUN_00456650` while the caption is drawn): caption
  appeared in Arcade 4 times and went away, but the stage-1 cutscene kept playing — the skip does not read the
  pad Start bit (or not that way). The cutscene skip code itself is not found yet (not the logo screens
  `FUN_004c5520/5670`, not `FUN_004102a0` title/demo, not `FUN_004806d0` countdown).
- Attempt 2: set DIK_RETURN (0x1C) in `0x77a260` after `FUN_0049afd0` on alternate frames while the caption is
  drawn: no effect either (cutscene played through). Replaced by the skip-request hook above.

## Online network mode (research 2026-10-27)

- EX shipped with network versus: the original Configurater has a `[ネットワーク]` page (matchup server handle,
  login, password, connection info display, update rate 10/15/25 per second for ≤1.5 Mbps, faster lines and LAN)
  and Tod.exe still contains the lobby-chat and network settings strings (0x4fd2e0…).
- The shipping Tod.exe (2 June 2011, Ver0.500) has no network code: 13 import descriptors, none for
  WININET/ws2_32/dplay, no LoadLibrary/GetProcAddress import; "WININET.dll" and "ole32.dll" are unreferenced name
  strings, no URLs. The netplay code was compiled out; the matchup server is gone anyway.
- Local 2P exists (RfuDrv.dll: `RfuOpenKeyboard/GetKeyData/EnableKey/DisableKey` for a second keyboard). A new
  online mode would have to be written from scratch, e.g. lockstep over our own connection that feeds the remote
  player's keys into the 2P keyboard path and synchronises `nlRand` (HglNa ord 148) seeds.

### Lockstep proof (0.23.0, two copies on one PC)

- Design after hotd2-re's MIT `notes/agents/netplay.md` (inputs only, input delay, per-frame state hash); no code
  copied. `src/online_hooks.c`, `[online]`, role per copy from the environment variable `TODMOD_ONLINE`.
- Second copy: entry `FUN_004cf8b0` → `FUN_004cf7b0` creates `Global\HicGameLibMutex…` (prefix `PTR_DAT_00548000`)
  and quits if it exists; todmod's `CreateMutexA` hook appends `_todmod_<role>` online.
- Input: `FUN_004755b0` (one logic frame) → `FUN_0049b090` = `FUN_0049afd0` (DirectInput; zeroed when the window is
  not in front) + `FUN_0049adc0` (7-byte report: modifiers + 6 usages; device 0 at `0x77a1ac`, device 1 at
  `0x77a1b3`) + `FUN_004bb3f0(1)` (RFU 2nd keyboard → device 1, only with RfuDrv hardware). Player p reads device
  `0x4efde0[*(player+4)]` = {0, 1}. Buffered events `0x77a1c0` are not read by game logic. todmod hooks
  `FUN_0049b090`: own report for frame f+delay goes to the peer; frame f gets host → device 0, joiner → device 1.
- Determinism: loading is synchronous (HglCore `HabFileIsBusy`/`HabFileWait` ord 21/26 return 0), task queue
  `FUN_0049cbe0` runs ≤ 4 jobs per frame, `nlRand`/`nlRandomSeed` (HglNa 0x49a0/0x49b0 → MSVCR71 rand/srand) are
  seeded from the frame counter `0xa18fac`; `time()` only in the registry/licence check `FUN_004cef80`.
- Desync found and fixed: todmod's own `input_hooks` checked `GetAsyncKeyState` for letter case (autotype/case
  typing); the joiner ran the frame later and saw the key up → extra `nlRand` from `FUN_004c7820` (enemy hit
  reaction). Online, the key is taken as pressed. Randomizer seed 0 → fixed seed online.
- Verified 2026-10-28: host + joiner windowed, Enter into Arcade, autotype through stage 1 → 7200+ frames compared,
  0 desyncs (frame counter, nlRand count/value, mode/pads/items hash), ~58 frames/s, same screen on both.
- Diagnostics: `[online] log_hashes=1` (`todmod_online_<role>.tsv`), `log_rand_callers=1` (`todmod_rand_<role>.tsv`,
  return address per nlRand call; diff the two files to find the first divergent caller).
- Open: the joiner's keys only reach the game while its window is in front (DirectInput foreground); two people on
  one PC need RFU or a later network build. Different keyboard layouts on two PCs would translate keys differently
  (`[input]` should be identical or translated on the sender). No resync/rollback; a stall of 10 s ends online play.

### Two-PC online play (0.24.0, tested on one PC only)

- `online_hooks.c` protocol 2: 16-byte header (magic `TODL`, version, kind, role 1/2, room hash, boot id); input
  packets carry a 6-part settings fingerprint (`online_fingerprint.c`: version, word packs + dict files + word.HAB,
  typing/layout, rules, Tod.ini GameSpec/sound/[その他], model packs), send time + echo for the ping, the last 16
  frames of reports and the frame check. Delay auto = ceil(ping/2 / 16.7 ms) + 1, min 2, after a 0.7 s ping phase.
- Save: `FUN_00475130` (startup task) → `FUN_004c50e0` → `FUN_004c4b10` reads `<0x547ef8>user.bin` (0x22ae8 bytes)
  into `0xa1b760`, then decrypts/checks it; `FUN_004c4bc0` writes it. The joiner's load hook fetches the host's file
  in 1 KB chunks (host reads it from disk on request) and its save hook skips writing. Load runs before the first
  `FUN_0049b090`, so the joiner connects inside the load hook.
- Background running: HglCore export `hlSetActiveMode` (ord 86, set from Tod.ini Active by WinMain) is forced to 1.
- Status in the window title (`EnumThreadWindows` of the game thread). Waiting pumps `PeekMessage(PM_NOREMOVE)` so
  Windows does not mark the window as hung; Esc while waiting → offline.
- `mod/tools/online_relay.py`: forwards packets between role 1 and 2 of a room (no NAT punching).
- Verified 2026-10-28 on one PC: direct (0 desyncs, 3600+ frames), simulated 40 ms + 0–15 ms jitter + 3 % loss
  (ping 104 ms → delay 5, 5400 frames, 0 desyncs, ~58.7 fps), relay on localhost (0 desyncs), joiner's own window
  pressing Enter → player 2 joins on both screens. Save transfer 142056 bytes in 0.26–0.93 s.

### Mutators, weapons, borderless, statistics, replays (0.25.0)

- Lives: player block `0xa18620 + p*0x210` (stride 0x108 shorts): +2 state (`0xa18622`), **+6 life** (`0xa18626`,
  3 at start, found with `[debug] memory_watch=9f0000-a43000` → `todmod_memory_watch.tsv`), `0xa18764 + p*0x210`
  hits taken. Damage `FUN_004872b0(player, knock_back, direction)` (cdecl; life−1, grace timer `0xa1b730[p]` = 90
  frames (int32), word level drops). Never-0 clamp unless `0xa3e588/0xa3e58c` == 2.
- Word level `0xa3e9a2` (s16 0–15): picks the word difficulty, clamped by `FUN_004c8210`/`FUN_00457260`; items
  7–10 change it; −2 per hit (seen 6→4→2→0).
- Typing miss: word task `FUN_004c9dd0` plays sound 0x5517a9 (P1) / 0x5617a9 (P2) via `FUN_0049c380(id)` (also
  `FUN_004a8a40`). Judge `FUN_004cb5a0(state, player, mode)` / `FUN_004cb710` (cdecl, 3 args; mode −1 = player
  typing, 1/2 = demo/assisted; 710 jumps to 5a0 in romaji mode 'A' `0x547c1c`); result 0x10 miss, 0x40 unit,
  0x80 word done.
- `FUN_004755b0` = one game update (todmod `frame_hooks.c` callbacks around it). `[mutators] speed` scales
  `[timing] update_rate` (150 % = 90 updates/s, measured 89.4–89.9 frames/s).
- Weapons: zombie banks hold weapon models — znonoo 631 axe, 632, 633 pitchfork, 634 hammer, 635 mace; znonoopa
  636–638; znchain chainsaw frames 8072–8082. Throwers (kinds 1, 0x13, 0x14) swap the weapon hand model
  (`obj+0x4e0`/`+0x690`) for an empty hand and create the projectile in `FUN_0044a590` (object 0x13f8 bytes, task
  `FUN_00449c20`, model `+0x13f4` = hand model − base + 0x27b, kind `+0x1fc` = 5, `+0x133c/+0x1340` = 7, own word
  task). `FUN_00421f80(obj)` registers objects every frame; todmod picks one loaded weapon per projectile there.
  Model drawn by id `FUN_0048b400` (skips unloaded: slot flags `0x9f3acc + id*16` sign bit = loaded).
- Borderless: HglCore `hlbIsProfile("HGLCORE","Window")` forced true, the window (created later than
  `hlInitSystem`) gets WS_POPUP and the monitor rect in the create-window hook; DPI-aware, render size = screen.
- Statistics: judge hooks above; romaji text `word+8`, index `word+0x49` (expected key read before the call).
- Replays: `determinism.c` owns read-input, nlRand/seed and save load/write hooks for online and replays. File
  `TODRPLY1`: 92-byte header (version, todmod version, settings fingerprint, randomizer seed, save size, frame
  count, check interval/count), raw user.bin, RLE runs of 14-byte frames (device reports 0 and 1), checks
  (frame + 4-part hash every 60 frames). Verified: 4962-frame recording with randomizer seed 0 played back with
  0 drift warnings and an identical frame at 80 s.
- Not done: random enemy counts (encounter scripts and kill accounting not mapped).

### Enemy counts and weapon users (0.26.0)

- Stage-script spawn ops (table `0x5264d0`, also `0x51cce8`): `0x4218d0` (flat records: type, flags, pos xyz, rot xyz,
  +0x22 hp, +0x24 kind, +0x25 behaviour), `0x421bb0` and `0x421c70` (if `0x9f0adc` == 0) call `FUN_00421ad0(record)`
  per entry (+0x24 params: kind, variant, behaviour, ...; the enemy keeps `obj+0x1394` = params pointer). Lists are
  `DAT_00a195a0`-relative: pointer + 1, records, −1. Stage 1 zombies come through `0x421bb0` (type 48).
  Type 48 = `FUN_004474a0` (zombie create): counters `0xa18618`/`0xa18fb0` (short) +1 unless behaviour 0x1f.
  todmod `enemy_counts.c` runs the original op on a list with copied records (params copied too), offset < 1 unit.
  Verified: 25 extra zombies in stage 1, typed down, no stall (`[debug] log_enemies=1` logs counters).
- **Stall cause found 2026-10-28:** the 0.22.0 look shuffle gave the axe throwers (kind 19) another kind's body;
  throwers then stand at their spawn point and the Santa Lucia window zombie is never triggered (stall at the
  bridge). Same with looks off: passes. Kinds 1, 2, 19, 20 (and kinds with weapon banks) keep their own look. The wall jumpers are kinds 13
  (znkagex, masked ninja) and 25 (zstin, monkey face; spawn type 49): copies of them loop (they jump between
  walls forever), so they are never copied. Kind 25 with another body soft-locks the alley (seen again 2026-10-28), so it
  also keeps its own look; kind 13 may still be swapped. 2026-10-29: the stage 2 assassin jumpers 1 (znassb) and
  22 (zsass) soft-locked too (user); both keep their look and are never copied.

### Pause menu (0.27.0)

- Game modes `0xa3e308` seen in order: 15 boot, 11 and 3 title, 4 main menu (Arcade/Original/Kids/Options/Exit),
  6 gameplay (Mode Select Training/Story, cutscenes and play). A mode change is requested by writing the mode to
  `0x757c58` (−1 = none); `FUN_00475250` applies it at the start of the next game frame (resets tasks, sound).
- `FUN_00454d40` dispatches per mode each frame; `0x9f3a84` is the mode's step. Gameplay steps: jump table
  `0x4efd30`; step 0 `FUN_00454310` starts a new game: stage `0xa19598` (short) = 0 for game types 0/1/5/6/7, then
  `FUN_004804b0` loads that stage's event file (`0x52c230[stage]`). Overwriting the stage in a hook on `0x4804b0`
  during step 0 starts that chapter (verified: forced stage 1 started chapter 2).
- Pause: Esc → one frame's draw calls are captured (frame_interp interceptors); while paused the game frame is
  skipped and the capture replayed before HglNa ord 209 (nlWaitVSync) presents. Verified 2026-10-29: pause,
  resume, exit to main menu, restart.
- Mode Select (Training/Story) is part of chapter 1's script (`FUN_004517d0`: pointer = `0x9c62c0[stage][area
  0xa18fb2][entry 0xa3e578]`, entry 0 for stage 0). Starting stage 0 at entry 1 to skip it broke the chapter
  (invisible models, zombies not spawning or attacking: entry 0 does more set-up), so it is not used.
- Restart must reset player states like the game's own retry (`FUN_00454fd0`: players in state 6 → 0, then
  `FUN_004753c0(6)` = mode request 6): player state `0xa18622 + p*0x210` 0 = joining. Left at 5 (playing), Mode
  Select ignores every Enter. todmod sets every player not in state 9 to 0, then presses Enter (SendInput, every
  0.5 s from 0.75 s, until `0xa18fa8` "skippable cutscene" is set; Story stays selected). Writing Enter into the
  device report did not work. Verified 2026-10-29: Mode Select passed after 4 presses, opening, zombies visible,
  attacking.

### Narrow text, Staff Credits, chainsaw loop (0.28.0)

- Narrow (half-width) cells broke in English mode: the word box (`FUN_004c84f0(layout, word)`) in English mode
  (`0xa1b6b2` != 1) advances each cell by a per-code table (`0x547c60`/`0x547d50`, codes 0x8260-0x829A) and 0.4 for
  every other code, so the narrow cell 0xF9FC (a two-letter pair) got 0.4 of a cell and overlapped. todmod draws
  a word whose display starts with 0xF9FC with the flag set to Japanese for that call (0.75 per cell, the layout
  narrow mode was made for); the typing line then shows capitals. Verified 2026-10-29 ("All the news from
  Baghdad", 25 letters).
- Staff Credits = game type 7: `FUN_004789c0` turns player codes 0x39/0x3a into kinds 0x56/0x57 (tod_ext05
  dancers). The player character swap (`[cheats] player1_character`) rewrote those codes and the gameplay kinds
  table `0x52c800`; it now leaves game type 7 alone (table restored while the type is 7). Not verified in game.
- Sound effects: `FUN_0049c380(id)` → table `0x542488` (0x34 bytes: id, file under SOUND/SE/) → HglNa ord 212
  `nlsndLoadPlay(path, loop, channel, id)`; loop = id in `0x5467f8` (paired off ids at `0x5468b0` call
  `nlsndSystemControl(0, 0x300a0)` = stop looped channels; 0x2a0/0x3a0 pause/resume BGM). Strength's chainsaw
  0x4d17a9 = COMMON2/CHAIN_SAW_22.WAV (22050 Hz stereo, 13162 frames) looped whole, start-up included. todmod
  plays it itself (waveOut: intro frames 0-9542 once, 9542-13162 with WHDR_BEGINLOOP|ENDLOOP, infinite) and stops it
  on 0x300a0/0x1100a0/0x100a0 or outside gameplay; loop points from the HOTD2 rebuild 1.5 (Dreamcast SND.AFS).
  Verified with `[debug] sound_loop_test=1` (game's own call on and off).

### Blood colour and audio library (0.29.0)

- tex.HAB has `scr_blood_red.bin` and `scr_blood_green.bin` (39 textures each, identical headers and ids 273+);
  this game loads the green bank. `[display] blood=red` points Table.dll `TexFileNameTbl[green]` at the red file
  name before any load (bank 0 is an unused empty slot). Verified 2026-10-30: red pools and wounds in stage 1.
- Voice lines: sound record `0x51cfd0 + id*16` (+0xc sound, kind 2 = voice, low 12 bits = index into the voice
  names `0x53b1a4 + i*0x24`, files under SOUND/VOICE/), text records from `0x524a28 + id*8` (4 shorts, −1 ends;
  records = subtitle lines 0-365). Both player takes of a line (e.g. ST1/01_J and ST1/01_GA) share the record.
  The configurator "Audio" tab (`config/audio_page.c`, MCI) lists SOUND/BGM (named), VOICE (with subtitles) and SE.

- 0.29.1: "Player 1 as" + randomizer looks: the chosen kind's body was shuffled to another kind's, whose banks the
  character swap does not request, so the player was invisible (any shuffled kind, e.g. 0, 32, 45, 46, 56, 62;
  players 57-61 are never shuffled). The randomizer now keeps the chosen kinds' looks.

### Soak test: wordless copies (0.30.0)

- Full-run randomizer test (seed 777, 2-3 copies): copies of the library zombies (kind 8, behaviour 0x0d, params
  `08 00 0d 0f`) left a zombie without a word that kept attacking until game over; the same seed with 1 copy
  passed. Word tasks (`FUN_004ca960`) only take a word in certain enemy states (`FUN_004c75a0` → `FUN_004c6dd0`,
  one per frame, `0x7808e0`). todmod now copies only behaviours seen to work: 0x01, 0x12 (walkers) and 0x21
  (axe throwers); scripted entrances (0x0d, 0x0e, ...) keep one zombie.

### Infinite lives, quiz autotype, full run (0.32.0)

- `[cheats] infinite_lives`: hook on the damage function `FUN_004872b0`; life raised to 2 before the call if it is
  1, restored after (knock-back, flash and grace time still happen).
- Quiz lists (SE01/SE08, 19 records): question, then 8 correct answers (records 1-8), then 10 wrong ones. A word's
  reading pointer gives its record in the active pack (`dictionary_quiz_answer`); autotype now refuses wrong answers.
- Full run (seed 777, all randomizer options, 2-3 copies, infinite lives, autotype, skip_cutscenes; chapters 5 and 6
  started with `[debug] pause_restart_stage=4/5`): all six chapters and bosses cleared, quizzes answered. Two more
  look-shuffle locks found and fixed: kind 18 (znele) creates a kind 9 child (`FUN_004474a0`) — with swapped looks the
  children had no words; kind 24 (zslman, type-49 spawner like 25) with another body got up again after every kill.
  Kinds 9, 18, 24 now keep their look (with 1, 2, 19, 20, 22, 25).
- Ending credits (task `0x4207f0`) stalled at "PRODUCER" with no staff name (also Staff Credits mode). Cause: in the
  staff tables (kinds 3/4/5) the record's group is not a difficulty: high16 = title index, low16 = `0x0001` normal
  name or `0xNN02` = last of a title group of NN names (e.g. 0x10102 PRODUCER 1 name, 0x90402 4 names). todmod's
  built-in replacement scaled low16 as a difficulty, so no group ever ended. 0.32.1 keeps the original group for
  staff tables. Verified (chapter 6 via restart, English pack): every title, English names typed, END, name entry.
  Bisect: game names + no skip + no Enter = works; English names alone = stall; with the fix = works.
  `pause_restart_stage=6` is not the ending but a 180-second timed mode in a mansion.

### Soak test result and glTF import (0.31.0)

- Seed 777, all randomizer options, 2-3 copies (walkers and throwers only), autotype: chapters 1 and 2 cleared
  (5100 and 6016 pts), chapter 3 reached its boss; game over there because autotype cannot answer the boss quiz
  (harness limit, not a lock). Harness notes: the name entry after each chapter needs Up (YES) then Enter; Enter
  presses every 10 s from 45 s get through continues; the title screen after a game over ignores them.
- `tools/gltf_to_character.py`: glTF/GLB skins (JOINTS_0/1, WEIGHTS_0/1) → the same .skin output; bones mapped by
  name to the 15 parts (topmost bone of each part = its joint), per-vertex weights summed per part (≤ 4).
  Verified with a Mixamo-named test rig built from Tommy: bends in the car opening.

### Skinned models (0.30.0)

- Characters draw each body part with `FUN_0048b400(model id)` / `FUN_0048b460` under the node's matrix (HglNa
  `nlGetMatrix`, row vectors, translation in elements 12-14) after `nlSetScaleFactor(A+0x116c)` (ord 178; civilians
  0.9); `nlObjPut` (ord 110) copies the matrix into its draw record (sort key = matrix element 14).
  `nlObjPutLoadTexture_hod2` rewrites each chunk's texture word in place (handle | 0x40000000), so copies of a
  loaded object draw with its textures.
- todmod `skinned_models.c`: `.skin` files in model packs (kind, carrier = chest model, part of every model id
  incl. variants, per object corner up to 4 (part, weight, part-local position, normal)). Part draws of a skinned
  kind are captured instead of drawn; once every part of the character is in, a fresh copy of the carrier object
  (ring of 32) gets the bent vertices in the chest's frame (divided by the scale) and is drawn with `nlObjPut`
  through Tod.exe's import slot (so frame interpolation and the pause freeze record it) under the chest matrix.
  Verified 2026-10-30: Tommy (player 1, kind 57) sits in the car and steps out as one mesh, no seams.

## Prompt text rendering (confirmed)

- `FUN_00455dc0` loads `font.bin` (bank 6) into a 0x138000 buffer `DAT_00757940`.
- `FUN_00455ff0(sjis, texId, mode)` rasterises **one glyph**: 24x24, 2 bpp, looked up through
  `kanji_offset[sjis - 0x8140]` for codes 0x8140–0xEAA4, into a 32x32 16-bit texture (`DAT_00757140`).
  Anything that is not a double-byte cp932 code fails.
- `FUN_004c5830` / `FUN_004c59a0` build a prompt: string at `obj+0x50`, per-glyph texture ids at `obj+0xac`
  (max 64 glyphs), width = `strlen/2`; kana-mode (`DAT_00547c1c == 'K'`) adds a reading line at `obj+0x1ac`.
  Glyph texture ids come from a 127-slot ring 0x1e24–0x1ea2.
- Details (disassembly): `FUN_00456140(str, texId, mode)` copies one 2-byte code + NUL and calls `FUN_00455ff0`,
  which always draws exactly one glyph at (2,2) of the cleared 32x32 buffer `0x757140`. Glyph data =
  `font.bin + kanji_offset[code-0x8140]*32 + 16`, 24 rows x 6 bytes, 2 bpp, MSB-first. Stride 160 bytes; 19174 of
  the codes share offset 0 (blank = full-width space). `FUN_00455e40(src, y)` (x in ESI) writes A1R5G5B5:
  level 1 → grey 21/31, **level 2 → grey 10/31 (darker than 1; font.bin still orders levels by coverage)**,
  level 3 → white; every lit pixel sets the alpha bit of its 8 neighbours (black 1 px outline). Upload via
  `FUN_0048c7d0(texId, 0x526e18, 0x800, 0x757140)`; mode 2 zeroes `0x526e68`. Return 0 = ok, -1 = failed
  (callers store the negative texture id and retry in `FUN_004c59a0`).
- Prompt layout (`FUN_004c5830`) only walks the display as 2-byte pairs: no lead-byte range checks.
- Consequences (original game): no single-byte text; only JIS X 0208 characters (no é, ñ, ü, ß, Cyrillic is present, Polish/Czech
  diacritics are not); fixed full-width advance.

### todmod Unicode glyphs (implemented, 0.3.0)

- Pack loader encodes characters with no font.bin glyph as tokens 0xF040–0xF9FC (1880, allocated per codepoint
  for the session, `glyph_tokens.c`). If a word needs any token, all its characters below U+2E80 (except space)
  become tokens too, so Latin text in one word has one consistent look.
- Hook `FUN_00455ff0`: for a token, GDI renders the codepoint (`todmod.ini [text] font/weight/cap_height`; font
  linking covers e.g. Hangul) into 2 bpp, swaps it into the blank glyph slot of 0x8140 in the font.bin buffer,
  calls the original with 0x8140, restores the slot. Outline/colour/upload stay native.
- Native full-width Latin metrics (font.bin, 24×24 cell): capitals/digits rows 2–21 (20 px, baseline below row 21),
  lowercase x-height rows 7–21, ascenders to row 2, descenders squeezed into rows 22–23, glyphs centred, strokes
  ~2 px (regular weight).
- Matching raster (0.5.2, `glyph_raster.c`): em chosen from the 'H' black box (`GetGlyphOutlineW` at a 100 px
  probe) so capitals are `cap_height` (default 20, Arial → 28 px em); drawn at a fixed baseline in a 96×64 canvas,
  centred on ink, then area-resampled into the cell with the baseline on row 22: the part above the baseline is
  squeezed only if taller than 22 px (accented capitals), descenders squeezed into 2 px like the native font.
  Default weight 400. Verified in game (2026-10-26): Unicode demo words match the native letter size.
  Known: 2 px descenders make e.g. þ resemble b (same limit as the native font).
- Offline preview: `mod/tests/render_glyphs.c` + `preview_glyphs.py` (pass `--font word.bin font.bin --table
  Table.dll --codes …` for a native reference row).
- Verified in game (2026-10-26): hook installs (`MH_OK`); `unicode_demo` S000L010 (395) and S000L020 (745) loaded
  with no parse warnings; words such as café, Straße, Łódź, Čeština, 한국어 rendered correctly with the native
  outline and were typable (accent folding and explicit readings both work).

## Renderer and timing (confirmed)

- `FUN_10002180` (HglCore) reads `Resolution` (0–15) from the INI, looks up the table at `0x10013258`
  (8-byte entries), and sets 2D scale `x = W/640` (`0x10019374`), `y = H/480` (`0x10019370`) → 2D is authored
  at 640x480 and stretched.
- `FUN_0048b370` (Tod.exe) sets the 3D projection: `Ordinal_142(id, aspect = 4/3 (imm 0x3FAAAAAB at 0x0048B3A9),
  near 0.5, far 8000)`, half-FOV from `tan(0.35866)`.
- Resolution details (disassembly): `FUN_10002180(a, b, fullscreen)` is cdecl, called only from `hlInitSystem`;
  writes W `0x10019420`, H `0x10019424`, bpp `0x1001942c`, scales `0x10019374`/`0x10019370`. W/H are then read by
  `hlInitSystem` (MoveWindow to W×H *window* size), the window creation `FUN_100041d0`, display-mode enumeration
  `FUN_10002810` and `D3DXInit` (D3DX context W×H). Overriding the globals right after `FUN_10002180` is enough.
- **All 2D goes through `D3DDrawPrimitiveStripTLV`** (HglCore ord 9, cdecl `(float *v, count)`, 8 floats per vertex,
  FVF 0x1c4): it multiplies x/y in place by the scales and draws. Tod.exe calls it at 17 sites; HglNa once.
  `D3DDrawPrimitiveStrip/List` are 3D (FVF 0x112).
- 3D projection: HglNa `nlPerspective` (ord 142, cdecl `(int angle, float aspect, float near, float far)`):
  m[0] = cot(half)/aspect, m[5] = cot(half) → **fixed vertical FOV**, so aspect = W/H gives Hor+. Only caller is
  Tod.exe `FUN_0048b370` (aspect 4/3). Tod.exe projects 3D→640×480 HUD space with `240/tan(0.35866)` around
  (320,240); with 2D scale s = H/480 and x offset (W−640s)/2 this matches the Hor+ 3D image exactly, so word prompts
  stay on zombies outside the 4:3 area.

### todmod display (implemented, 0.4.0)

- `todmod.ini [display] width/height` (0 = Configurater `Resolution`), `widescreen=1` (Hor+ 3D, 2D uniform scale
  centred) or 0 (original stretch). Hooks: `FUN_10002180` (override W/H, recompute scales), `D3DDrawPrimitiveStripTLV`
  (adds the offset in 640-space before the original scales), `nlPerspective` (4/3 → W/H).
- Verified in game (2026-10-26, 1280×720 fullscreen per Tod.ini `Window=false`, dgVoodoo still present): hooks `MH_OK`, 2D scale 1.5 × 1.5,
  offset 160 px; 3D fills 16:9 without stretching, 2D is centred 4:3, word boxes stay on zombies (tutorial played,
  S000L010/L020 loaded). Not yet tested: fullscreen, other resolutions, `widescreen=0`.
- Fixed in 0.5.1: cutscene letterbox and windowed client size (see "Widescreen polish").

- Frame pacing: HglCore busy-waits on QueryPerformanceCounter to `1000/rate` ms (`hlSetRefreshRate`,
  `0x100180a0`), with frame skipping (`hlDelayCount`, "mabiki" modes). Game logic advances per frame.

## Implementation plan

1. **Mod loader**: our `Common.dll` (exports ordinals 1/2, forwards to `Common_orig.dll`) loads `todmod.dll`,
   which installs hooks (MinHook). Built here with zig cc (x86-windows). Settings in `todmod.ini`.
2. **Dictionaries**: hook `FUN_004cb3a0` to load `dict\<pack>\<item>.txt` (UTF-8 TSV) and build records in our
   own buffer (no 600 KB / 63-unit limits); fall back to `word.HAB` per item. Packs selectable per family
   (mix and match).
3. **Any-language text**: hook `FUN_00455ff0` to rasterise glyphs with GDI from Unicode. Characters outside
   cp932 are mapped to 2-byte tokens in the user-defined area (0xF040–0xF9FC) so the prompt layout code keeps
   working. Later: proportional/half-width advance.
4. **Typing**: map each display letter to reading ids; accented letters accept the base key (é→e) or a
   per-language rule table.
5. **Widescreen / resolution**: arbitrary `Width`/`Height` instead of the 15-entry table; projection aspect from
   the window; 2D scale x = y with a centring offset (2D draw path still to locate).
6. **Framerate**: logic is per-frame, so >60 needs fixed-step logic + render interpolation (research). First
   deliverable: accurate 60 pacing, optional uncapped menus.
7. **Renderer without dgVoodoo**: D3D7/DirectDraw. Evaluate native D3D7 on Windows 10/11 with our fixes;
   fallback: open-source DDrawCompat, or a D3D7→D3D9 translation layer (large).
8. **Configurator**: new standalone configurator (replaces the MFC one) writing `Default.ini` and `todmod.ini`:
   resolution, aspect, fps, window mode, dictionary packs per family, language/font.
9. **In-game dictionary selection**: after 2, locate the mode/menu code.

## Typing judge (confirmed)

- Unit index `DAT_0051b378[unit_id]` → entry `{u8 flags (low nibble = spelling count; 0x80/0x40/0x20 class bits),
  …, char** spellings}`; entries live at 0x0051a0a4–0x0051b36c (302 units, see `romaji_table.json`).
- `FUN_00402fb0(typed, len, …)` scans unit ids and returns the one whose spelling equals the upper-cased typed
  keys; a doubled A–Z consonant returns 0xCB (small っ). 0xFFFF = no match.
- Accessors `FUN_00402ee0/2f00/2f20` (class bits), `FUN_00402f40` (count), `FUN_00402f60` (spelling i),
  `FUN_00402f80` (component unit of a compound). The state machine that consumes them is 0x00401420–0x00403190.
- Input is ASCII letters (`toupper`), so Latin-script languages work by mapping each display letter to an existing
  letter unit; non-Latin layouts need a key→unit table. New unit ids (> 302) require redirecting the 7 accessor
  references from `DAT_0051b378` to a larger table.

### Widescreen polish (implemented, 0.5.1)

- **Cutscene letterbox = 3D model 2421**, drawn with `nlObjPut` at model-view translation (0, 0.35, −1), identity
  axes, i.e. fixed to the camera and sized slightly over the 4:3 frustum (≈ −9…650 in 640-space under Hor+). Found
  with `log_objects=1` (logs each distinct object once with its id from Tod.exe's model table
  `0x009f3ac4 + id*16` → object pointer; `+0xc` flags). todmod hooks `nlObjPut`/`Trnsl`/`ML`/`MLTrnsl`/
  `CheapEnvMap` and, for ids in `[display] widen_models` (default 2421), scales matrix row 0 by
  (W/H)/(4/3) around the draw (`nlGetMatrix`/`nlLoadMatrix`). Verified in the opening cutscene (2026-10-26):
  bars span the full 16:9 width. Other cutscenes not yet checked for other bar models.
- 2D side: only two callers of `D3DDrawPrimitiveStripTLV`: Tod.exe `FUN_00403b30` (centred image quad) and HglNa
  `FUN_10003110` (all `nlScrPut` sprites: centred, scaled, rotated textured quads). todmod stretches any 2D quad
  whose x span is exactly 0…640 (±2) to the full screen width (`stretch_full_width_2d=1`); not yet seen to trigger.
  `log_wide_2d=1` logs full-width and top/bottom-edge 2D quads. The intro logo animation uses 512×64 sprites that
  overshoot 0…640 while zooming (correctly left alone).
- **Windowed size**: the window is created in `FUN_100041d0` (after `hlInitSystem`, whose MoveWindow is skipped
  because the window does not exist yet) with legacy `SM_CXFRAME/SM_CYFRAME/SM_CYCAPTION` sizing, and HglCore's
  window procedure (`0x100043a0`) answers `WM_GETMINMAXINFO` with the same legacy sizes → client was 1280×717.
  todmod hooks `FUN_100041d0`, subclasses the window to raise the min/max limits, then corrects the size by the
  measured client deficit → client exactly W×H (verified 1280×720 windowed).
- Quitting: Tod.exe's window procedure (`0x004cf4e0`) ignores WM_CLOSE unless Alt+F4 (WM_SYSKEYDOWN F4) set
  `0x00547ef0`, which then shows a Yes/No quit box — Alt+F4 is the game's only quit path.

## Frame pacing and game loop (confirmed from code, step 5 research)

- Tod.exe `0x004cf960` calls `hlMain(hInst, …, init 0x4cf1e0, post-init 0x4cf360, frame 0x4cf0a0, shutdown 0x4cf840,
  0x4cf4e0)`. HglCore `FUN_100042d0` is a PeekMessage loop that calls the frame callback whenever idle.
- Post-init calls `hlSetRefreshRate(60)` (ord 100, `0x004cf3d6`) and `hlSetMaxMabiki(6)` (ord 96, `0x004cf342`).
  Defaults: rate 60 → frame time `0x100180a0` = 16.667 ms, mabiki mode 1 = {3, 0.2, 10.0}.
- Frame callback `FUN_004cf0a0` → `FUN_004755b0`: one pass of all game tasks — frame counters (`0x00a18fac`,
  `0x00a19560`, `0x009f0bc4`) +1, update and draw calls interleaved, projection set each frame. **Game speed is
  "per frame"**; only 2 `GetTickCount` uses in Tod.exe (not gameplay timing).
- End of frame: HglNa calls HglCore `VSync` (ord 29): if `hlDelayCount == 0` it presents (D3DX vtbl +0x4c), then
  `CreateHighResolutionIdle(16.667)` **busy-waits on QPC** to the frame time. Running late adds to an error term;
  past the threshold it sets `hlDelayCount` (≤ 6) and the next frames **skip Present but still run logic**
  ("mabiki" = frame skipping). `hlResetMabiki/PauseMabiki/RestoreMabiki` (ord 74/64/75) manage it during loading.
- Consequences: (1) logic is locked to 60 Hz; >60 fps needs render interpolation of every animated object, camera
  and 2D element, or a higher logic rate with every per-frame constant rescaled — both very large. (2) The busy-wait
  burns a full core. (3) On a 120/144 Hz display with vsync forced (dgVoodoo `ForceVerticalSync = true`), Present
  blocks to the display's vblank and the 16.667 ms wait is measured from the previous frame, so pacing can judder.

### Framerate options (recommendation)

1. **Smooth, efficient 60 (small)**: hook HglCore `CreateHighResolutionIdle` (ord 3, cdecl `(float ms)`) with a
   high-resolution waitable timer + short final spin; keep the mabiki logic. Cuts CPU from a full core and fixes
   pacing; optional "present without vsync" when the display is not 60 Hz.
2. **>60 fps (very large)**: needs render-time interpolation between logic ticks for the camera, every model,
   particle and 2D element, or splitting `FUN_004755b0` into update/draw and replaying draws with interpolated state.
   Not worth it before everything else; external frame generation is the practical route for high refresh rates.
3. Raising the tick rate (`hlSetRefreshRate(n)`) would just speed the game up (per-frame logic) — only useful as a
   deliberate "game speed" option.

### todmod frame limiter (implemented, 0.5.0)

- HglCore's spin loop in `CreateHighResolutionIdle` calls `*(void (__cdecl **)(float remaining_ms))0x10019530` on
  every iteration if non-null. That global is only read (no writer in any module; it sits after the 4-byte
  `g_PrimaryPixelFormat` enum at `0x1001952c`). todmod stores a callback there that waits on a high-resolution
  waitable timer until 1.5 ms before the deadline; the original loop spins the rest. Mabiki logic untouched.
- `todmod.ini [timing] limiter=timer|spin`, `stats=1` hooks `VSync` (ord 29) and logs frames/s, longest frame and
  process CPU every 10 s. Configurator checkbox "Low-CPU frame limiter".
- Measured in game (2026-10-26, native D3D7, 1920×1080, ~1 min each; logs `TODEX\todmod_run1_spin.log`,
  `todmod_run2_timer.log`), steady-state intervals:
  spin 60.00 fps, longest 16.7 ms, **CPU 100% of one core**; timer 59.91–60.59 fps, longest 16.7–26 ms,
  **CPU 16–20%**. Both runs show hitches of 25–130 ms while loading; one 6.9 s gap in the timer run (game
  inactive or loading — no frames are drawn then).
- todmod logs `todmod: game closed normally` on process exit; a missing line means a crash.

### Frame interpolation (implemented, 0.8.0)

- Import profile (`[debug] profile_imports=1`, IAT stubs counting Tod.exe→HglNa/HglCore calls), attract-demo gameplay
  per frame: nlObjPut ~136, nlObjPutTrnsl ~36, nlScrPutCore ~4, state setters ~70 (light direction, layer, fade,
  ignore-Z, ambience…), once: nlPerspective, fog, light mode/view-trans, ClearScreen, nlBeginScene/EndScene/WaitVSync;
  matrix maths (Push/Pop/Rotate/Translate ~1500) is pure CPU. HglNa 178/102/67/140… are aliases of an empty stub
  (0x10006140); 148 = nlRand. Tod never calls D3DDrawPrimitiveStripTLV in normal frames (only FUN_00403b30).
- HglNa queue: `FUN_10003920` draws opaque objects **immediately** (`nlObjPut` flags ≥ 0); only sign-bit entries
  (`nlObjPutTrnsl`) and sorted sprites go to the arena (`DAT_1000d184`, reset by `nlBeginScene`/`FUN_10002eb0`;
  pointers `DAT_1000d170`, count `DAT_100248e0`; drawn by `FUN_100041b0` in `nlEndScene`). So a frame cannot be
  replayed from the queue alone.
- todmod `frame_interp.c`: redirects 33 of Tod.exe's import slots (puts, nlScrPutCore, Begin/EndScene, ClearScreen,
  D3DSetTexture, StripTLV, 26 state setters, nlWaitVSync) to recorders. From the first nlWaitVSync on, each game
  update is recorded (args, copied pointer data: sprite 0x38 bytes, light vector 12, TLV strips; current matrix per
  call); draws/scene calls are held, setters also run. At nlWaitVSync the recording is replayed: in-between frames
  while another display refresh fits before the next update (time-based; refresh from EnumDisplaySettings), each
  blended to its display time (matrices element-wise, sprite x/y/scale, strip x/y; matched by function + key +
  occurrence; cuts > 60 units / 0.6 rotation not blended), presented via D3DX UpdateFrame (vtbl slot 19); then the
  final replay and the game's own present. Skipped while `hlDelayCount` ≠ 0 (engine frame skip).
- Recording must not start before the game loop: `nlGetMatrix` during init (before the matrix stack exists)
  faults and the game's handler exits silently.
- Measured on the 60 Hz display: replay costs ~0.4 ms; every present blocks to vblank even with
  D3DX_UPDATE_NOVSYNC (native Win10 fullscreen DirectDraw), so > 60 presents/s are impossible on 60 Hz.
  `frame_interpolation=1` at 60 updates/s: 0 in-between frames, 60.00 frames/s (no regression).
  `update_rate=30` test (hook hlSetRefreshRate): 1.00 in-between frame per update, 60 frames/s presented, game at
  half speed — for judging interpolation smoothness on a 60 Hz monitor. Verified by the user (2026-10-26): smooth,
  no flicker or glitches. Not yet tried on a real > 60 Hz monitor. `present_vsync=0` patches HglCore
  `0x10002ef2` (push 0 → push 1 in VSync); no effect on native DirectDraw.

### Community patch (0.8.0)

- `mod/package.py` → `mod/dist/TODEX_Modern_Community_Patch_<ver>/` + zip (164 KB): README, install/uninstall.bat
  (run in the game folder; keep the original as Common_orig.dll), todmod_config.exe, todmod\Common.dll, guide,
  MinHook licence, tools\english_to_pack.py + word_lists.txt. No game files, packs, dgVoodoo. Install/uninstall
  tested in a scratch folder.
- `dllmain.c` build check: PE timestamp + SizeOfImage of Tod.exe 4DE72EF7/643000, HglCore 42E6DBE3/20000,
  HglNa 42E9E173/121B000, Table 4DE72ED8/C4000; any mismatch → todmod stays inactive (proxy still forwards).
- Generated tables compiled into the DLL/configurator are derived from the game (typing-unit spellings, list
  names, slot difficulties; no word text).

### Toolchain (Rich headers)

- Tod.exe: C++ compiler 13.10.3077 (VS .NET 2003) ×180 objects, C ×21, MASM, linker 7.10; 1 879 functions,
  ~604 KB of code. HglCore/HglNa: same toolchain (HglCore also 13.10.4035 objects).

## Renderer and dgVoodoo (step 5 research)

- HglCore imports only `DirectDrawEnumerateExA` and `DirectDrawCreateEx` from DDRAW.DLL; D3DX7 is statically linked
  (`D3DXInit` creates a D3DX context → DirectDraw7 + Direct3D7 device). No D3D8/9 use: the D3D8.dll/D3D9.dll in TODEX
  are unused dgVoodoo files.
- dgVoodoo 2.87 (`dgVoodoo.conf`): `Resolution = max_fhd` (internal render size forced up to Full HD),
  `Antialiasing = 8x`, `ForceVerticalSync = true`, `VideoCard = geforce_9800_gt`, VRAM 1024, `FPSLimit = 0`.
- **Native test (2026-10-26)**: with `DDraw.dll`/`D3DImm.dll` parked by `mod/tools/dgvoodoo_disable.bat`, the game
  loads `C:\Windows\SYSTEM32\DDRAW.dll` + `D3DIM700.DLL` + `dxgi.dll` + the NVIDIA user-mode driver, opens fullscreen
  with a 1920×1080 client, shows no error dialog and reaches the tutorial word load (todmod hooks all `MH_OK`).
  User played natively (2026-10-26): looked fine; dgVoodoo left disabled since then.
- What dgVoodoo adds that native D3D7 lacks: MSAA (modern drivers ignore D3D7 antialias render states), forced
  vsync, internal upscaling (unneeded now that todmod renders at native size), 16-bit dithering, robust
  fullscreen/alt-tab. Options: native D3D7 + todmod (pacing/vsync done ourselves), optional bundled DDrawCompat
  (open source) for compatibility fixes, or a own D3D7→D3D9/11 wrapper (large; only route to AA without dgVoodoo).
- Toggle: `mod/tools/dgvoodoo_disable.bat` / `dgvoodoo_enable.bat` (moves only DDraw.dll and D3DImm.dll to
  `TODEX\dgVoodoo_disabled\` and back).

## Settings files (confirmed)

- **The game reads `%APPDATA%\Sega\TODEX\Tod.ini`, not `Default.ini`.** Tod.exe (`~0x004cf1e0`) calls Common ord 1
  `GetUserFolderPath("Sega", "TODEX", out)` (CSIDL_APPDATA, creates the folders), copies `Default.ini` to
  `<out>Tod.ini` only if missing (`CopyFileA(..., TRUE)`), and passes the path to HglCore ord 98 (profile file,
  global `0x1001d748`; `hlProfile` = `GetPrivateProfileIntA`). Configurater.exe does the same and edits Tod.ini.
- Tod.ini is cp932 with Japanese section/key names (`[ワードボックス]` colours, `[ネットワーク]`, `[その他]`).
  `[HGLCORE]` keys: DispVersion, GameSpec 0–4, Window, Resolution 0–14 (table at HglCore `0x10013258`), Bpp,
  Sound/BGM/SE/Voice, Active, Transparency, Filter 0–2, SmallTexture, Fog, WindowPosX/Y. Booleans are `true`/`false`.
- Live Tod.ini at the time of the 0.4.0 test: `Window=false`, `Resolution=10`, `Bpp=32` → the 1280×720 widescreen
  test was fullscreen (via dgVoodoo), not windowed.

### todmod configurator (implemented, 0.4.0)

- `mod/config/` → `build/todmod_config.exe` (Win32 dialog, zig cc + .rc, PerMonitorV2 manifest), installed to
  `TODEX\todmod_config.exe`. Edits `[HGLCORE]` in Tod.ini and `todmod.ini` through `ini_file.c`, a line editor that
  keeps every other byte (round trip of the real Tod.ini and Default.ini is byte-identical; edits change only
  their own lines). Writes `Resolution` only when the size is in the original table; the size itself goes to
  `todmod.ini [display]`. Word packs: available folders in `dict\` ↔ ordered `packs=` list.
- Backups made before first use: `%APPDATA%\Sega\TODEX\Tod.ini.orig`, `TODEX\todmod.ini.bak`.
- Verified (2026-10-26): user saved and used Save and play; diff vs backups shows only `Tod.ini Resolution=10→7`
  and `todmod.ini [display] 1280×720→1920×1080`; Japanese sections untouched; game log shows 1920×1080, 2D scale
  2.25, offset 240 px. `Resolution=7` (800×600) is left from an intermediate save: a size outside the original
  table keeps whatever index was there, which only matters if todmod is removed.

### Volume, score display, voice packs, kana word boxes (0.33.0)

- The game has only on/off switches for music/effects/voice (Tod.ini `[HGLCORE] Music/Effect/Voice`). todmod adds
  levels (`[sound] music_volume/effects_volume/voice_volume`, 0-100, dB = 20·log10(p/100), hundredths):
  - Effects/voices: hook HglCore `hlSoundPlay(channel)` (ord 118) and call `hlSetSoundVolume(channel, cdB)`
    (ord 103) first; channel 0x10 = voice, others = effects. Verified 2026-11-04: log "channel 17 volume -444"
    (60 %), "channel 16 volume 0".
  - Game music: after HglCore `wave_play`, QueryInterface the DirectShow graph (`DAT_1001d8e0`) for IBasicAudio
    and `put_Volume` (vtable 7). Verified: S_OK with 30 % (−1046). Pack music (own waveOut player) and the
    Dreamcast chainsaw loop scale their 16-bit samples instead.
- Score: per-player running total = int `0x00a18758 + player*0x210` (every score change adds to it; the shorts
  at `0x00a18736 + player*0x210 + n*2` are per-category, clamped ≥ 0). `[display] show_score=1` draws it top
  right with GDI in gameplay (mode 6) for players whose state `0x00a18622 + p*0x210` ≠ 9. Verified on screen;
  not compared with the end-of-chapter tally.
- The VSync GDI overlay (subtitles) is now shared: `overlay_add(fn)` in `subtitle_hooks.c` (MinHook cannot hook
  VSync twice). `fn(NULL, 0, 0)` asks whether it wants to draw.
- Voices and effects both load through HglNa `nlsndLoadPlay(path, loop, channel, id)` with paths
  `Sound/Voice/<name>` (Kids: `Sound/Voice/Kids/`) and `Sound/SE/...`. `[sound] voice_packs` redirects the path to
  `voices\<pack>\<path below Sound>` (or below `Voice/`); OGG/FLAC decoded to `voices\.cache\`. The redirect runs
  inside the sound-loops hook when that is installed. Verified: SE and OGG voices of stage 1 redirected.
- Kana/kanji word boxes in English mode: English mode advances every code outside fullwidth Latin by 0.4 cell, so
  kana/kanji overlapped (Typing Space Harrier pack). The word box now uses the Japanese layout when the display
  holds kana (0x829F+), katakana/Greek/Cyrillic leads 0x83/0x84, kanji (lead ≥ 0x88) or a GDI token for a CJK code
  point. Verified: "法王 / HOUOU" drawn cleanly next to "ESC" in English layout.

### Blood colours, score vs result screen, subtitle timing (0.34.0)

- Blood banks hold rectangle textures (data format 9) in ARGB4444/ARGB1555/RGB565 (39 textures, ids 273+).
  `[display] blood=<colour>` loads the red bank (name swap as before) and, for a tint, `create_texture` copies
  the pixels with shade = R − min(G,B) times the tint plus the white highlight min(G,B); alpha kept. rainbow = hue
  47° × texture index. A texture pack PNG still wins. Verified 2026-11-04: purple pools; rainbow splatters and
  wounds in several colours.
- Score check (chapter 1, autotype, all A/B ranks): overlay 3007 at the end of play = Base Points 1547 + rank
  points (Rank A 1120 + Rank B 340). The result screen then adds Accuracy Points (1200 at 100 %) and Rescued
  Citizens (150 each) to the same int while it counts (overlay 3157 mid-count); Ranking 4657 = 3007 + 1200 + 450.
  The game stays in mode 6 on the result screen.
- Subtitle timing (from `FUN_00426060` and `FUN_00425f80`): a voice event plays the voice (`FUN_0049c380`) and
  creates the subtitle task in the same call, so text and voice always start together; there is no delay field.
  The line's display time is the short at `0x51cfd2 + id*16` (frames, counted down at +0x36). A line has up to 4
  English text parts (`0x524a28 + id*8`); part n+1 takes over when the remaining frames drop below part n's
  record +0x3e. Japanese mode shows one image for the whole time. `mod/tools/subtitle_timing.py` lists the
  344 voiced lines with display time vs WAV length: 3 end before the voice (75 ST2/54_JA, 82 ST2/60_TV_A, 90
  COM/67_YNG_M), 60 stay ≥ 0.75 s longer.

### Subtitle timing (0.35.0)

- `src/subtitle_timing.c` (shared with the configurator) reads the line table (`0x51cfd0`, duration +2, sound +0xc,
  voice names `0x53b1a4`), part table `0x524a28` and record switch values (+0x3e). Game timing per line: part 0
  starts at 0, part k+1 at duration − switch(part k), last part ends at the duration.
- Subtitle files may add `start`/`end` columns (seconds after the voice) per record. At startup todmod computes
  each line's timing (overrides, then `[text] subtitles_cover_voice=1` stretches the last part to the voice length
  read from the WAV header, through `voice_packs_redirect`) and writes the new total into the duration table
  (made writable), so the task is created with it. The task hook: elapsed = total − timer; outside every part's
  window (delay, gap) it only decrements the timer and returns (the task still ends on scene end/skip); while
  showing it writes switch = total − start(next part) into the current part's record for the call and restores it.
  `install_subtitle_hooks` now runs after `install_voice_packs`.
- Verified 2026-11-04 (English mode): 59 lines retimed (57 stretched to their voice); test file record 2
  start 0.5/end 1.5 → line 3 hidden frames 0-29, shown at 30, total 90; record 4 start 3.0 → line 7 part 2 at
  frame 180 (game 60), part 3 at the game's 290.

### Extras: FOV, reading line, photo mode, paintball, speech, mute, achievements (0.36.0)

- FOV: `nlPerspective` angle is in 1/65536 turns (game 0x1d3b = 41.1°, half 0.35866 rad). `FUN_0048b370` (only
  caller) then stores the HUD projection `0x00a3e994 = 240/tan(half)`; it is read by every 3D→screen helper, so
  todmod hooks `FUN_0048b370` and rewrites it for `[display] fov`. Word boxes stay on zombies. The camera-attached
  letterbox (widen_models) is scaled by tan(new/2)/tan(game/2) in x/y, translation included. Verified at 60°.
- Reading line: `FUN_004c8370` (Japanese layout) and `FUN_004c83d0` (English layout) draw the romaji line of a
  word box (word+0x98 = letter count, +0x99 typed, +0x58 letters, +0x50 display). Skipped when count > 1 and a
  display exists. Note the typing state word pointer (`typing_stats`) = draw word + 0x50.
- Photo mode: the frozen frame replays the recording (`frame_interp`). HUD pieces are recorded `nlObjPut*` calls
  with screen-space matrices (each 3×3 row has ≤ 1 non-zero, all |m| ≤ 0.5, tz > −20: word boxes, letters,
  health bar, PERFECT counter) plus `nlScrPutCore` sprites (1P lives) — skipped in photo replay; layers ≥ 0x0E
  skipped too. Layer 8 = normal 3D. A very close zombie's waist joint shows a gap in any frozen replay.
- Paintball: blood textures loaded white (tint 245) and the Direct3D7 device `SetTexture` (vtable 35) is hooked;
  when stage 0 binds a blood surface (found as the pointer with the DirectDrawSurface7 vtable at word 2 of the
  HglNa texture object), stage 1 = MODULATE(CURRENT, TFACTOR) with `D3DRENDERSTATE_TEXTUREFACTOR` (60) = a colour
  picked on every BLOOD*/SIBUKI* sound; restored for other textures. Verified: pools green/blue/pink.
- Sound events: `voice_packs.c` now hosts observers and redirects for every `nlsndLoadPlay` path (typewriter
  clack for `Sound/SE/.../GUN*`, paintball colour). Effects are played as `(path, loop, 0xffffffff, id)`.
- Speech: SAPI ISpVoice slots Speak 20, SetRate 28, SetVolume 30 (19/27/29 crashed the game — check vtables in
  zig's mingw headers). Mute: WASAPI default render endpoint → IAudioSessionManager::GetSimpleAudioVolume →
  SetMute (slot 5) for the process session. Verified muted/unmuted with focus changes.
- Achievements (`src/achievements.c`, list in `src/achievement_list.h`, `stats\achievements.tsv`), off with
  autotype (`[debug] achievements_test=1` writes `achievements_test.tsv`). Autotype games no longer write stats.

## Open questions

- 2D sprite draw path for HUD offsets.
- Where `Tod.ini` is read (`sprintf("%s%s", userFolder, "Tod.ini")` at ~0x004…; per-user save/config).
