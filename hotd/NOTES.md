# The House of the Dead (PC, 1998) — model formats

Game folder: `The House Of The Dead I\` (read-only). Working copy of the exe: `hotd\re\bin\thotd.exe`; Ghidra export
in `hotd\re\export\thotd.exe\` (`hotd\re\run_ghidra_export.bat`). Watcom-built (BEGTEXT/DGROUP sections, register
calling convention: the decompiler shows `in_EAX`, `in_AX`).

## Files
| File | Contents |
|---|---|
| `art\stage1\creature\creat1.rbh`, `creat2.rbh` | Meshes of all characters, zombies and bosses (body parts), plus a few whole figures. |
| `art\stage1\creature\texture.vmc` (`bigger.vmc`, same layout) | 414 texture pages 256×256 ARGB1555. Pages 1–330 2D (menus, captions, fonts), 512–595 3D. |
| `art\stageN\world\worldN.rbh` | Level geometry (same RBH format, not examined further). |
| `cam\allstage\camall.rbh`, `evt\allstage\evtall.rbh`, `sprite\spr.rbh`, `sound\*.rbh` | Camera paths, events, sprites, sounds (RBH containers). |
| `thotd.exe` (8.8 MB) | Skeletons and all motions are compiled into its data. |

## RBH container (`hotd/tools/rbh.py`)
`PIFF` size `RBHF`, then chunks (tag, u32 size, data, padded to 2): `RBHH` header, `BODY` × 3 (sections 0, 1, 2;
section 2 is empty, a runtime buffer), `RELC` × n: u32 0, u32 target section, then offsets in section 0 holding
pointers into the target section (relative to its start).

Creature section 0 starts with an id table: entries (u16 id, u16 0xFFFF, u32 pointer → part header), ended by an
entry whose second u16 is not 0xFFFF. Ids are global object numbers (also used by the exe's skeletons).

Part header (32 bytes; it follows its own face list in memory):
`+0` → face list (section 0), `+4` → vertices (section 1, float xyz × count), `+8` → colour table (BGRA, for
untextured faces), `+0xc` → section 2 (runtime), `+0x10` vertex count, `+0x14` face count, `+0x18` float radius,
`+0x1c` u16 group count incl. terminator, u16 0xFFFF.

Face list: groups of (u16 count, u16 kind, u16 bytes + 2, u16 words), then `count` faces; a group with kind 0xFFFF
ends the list. `kind & 7`: 4 textured quad (44 bytes: 4 × u16 vertex, u32 texture page, 4 × (u, v) float pixels),
2 textured triangle (36: 3 × u16, u32 page at +6, 6 floats at +12), 1 flat quad (10: 4 × u16, u16 colour index),
0 flat triangle (8). High bits are render flags (not decoded). UVs are pixels in the 256×256 page.

## VMC textures (`hotd/tools/vmc.py`)
`PIFF` size `VVTP`, then one `BODY` per page: 0x40-byte header (u32 0x40, u32 page number, u16 width, u16 height,
u16 kind (3 for 2D pages, 0 for 3D pages), u16 bits 16, masks A 0x8000 R 0x7C00 G 0x03E0 B 0x001F) and the pixels.
A face's texture id is the page number.

## Skeletons and motions in thotd.exe (`hotd/tools/skeletons.py`, `pose.py`)
- Body table `0x5ef8f8`: 94 pointers to root nodes; joint counts `0x5efa70` (94 dwords); motion table `0x5efbe8`
  (674 pointers); frame counts `0x5f0670` (674).
- Node: dwords id (0 = joint without mesh), x, y, z offset from the parent, flag, joint index, child count,
  child node pointers, 0. Bones point along local +X.
- Motion frame: 3 root floats, then per joint 3 × s16 angles a0, a1, a2 (0x10000 = 360°), padded to 4 bytes:
  `(joints * 6 + 15) & ~3` bytes. Motion data sizes equal frames × frame size, which gives each motion's joint count;
  motions are grouped by creature family (e.g. 467–662 = 18-joint zombies, 211–299 = 20-joint humans).
- Joint walk `FUN_00476bc0`: translate by (x, y, −z), then rotate Z by −a2 (`FUN_00475244`), Y by a1
  (`FUN_0047513c`), X by a0 (`FUN_004753d0`). `FUN_00477718` picks the frame (`motion table[motion] + frame × size`).
- The game's Y axis points down; exports are turned 180° about X (y, z negated) so figures stand upright.

## Extraction (`hotd\extract\`, local only — game assets, do not publish)
- `python hotd\tools\export_parts.py <creatN.rbh> <texture.vmc> hotd\extract` → `pages\pageNNN.png`,
  `creatN\creatN_<offset>.obj/.mtl` (every part, own coordinates), `creatN_sheet.png`.
- `python hotd\tools\assemble.py hotd\re\bin\thotd.exe "<game>\art\stage1\creature" hotd\extract` →
  `bodies\bodyNN.obj/.mtl` (94 bodies posed with a standing frame), `bodies.tsv` (joints, motion, part ids),
  `bodies_sheet.png`. Standing frame: upright frames near the median height, narrowest. Bodies 5, 10, 12, 44, 52,
  66, 75 have no parts in the creature files (other files, not found yet).
- Whole single-mesh figures in creat2 (agents, Sophie, scientists): parts `0x02984c`–`0x0359c0`.

## Import into Typing of the Dead (done, todmod 0.20.0; `hotd/tools/to_tod.py`, `make_zombie_pack.py`)
- HOTD 18-joint bodies use TOD's own numbering: joint 1 torso, 2 head, 3–5 / 6–8 arms (upper, fore, hand), 9 hips,
  10–12 / 14–16 legs (thigh, shin, foot), 13 / 17 toes; TOD humanoid parts are 1, 2, 3–5, 6–8, 9, 10–12, 13–15 (same
  offsets: head 4.46, hips 0.92, shoulders 2.33). TOD rest pose: zero rotations, arms and legs straight down, faces
  +z (toes +z), part 3 at x −2.29.
- HOTD game coordinates → TOD: (x, −y, −z) (y down → up), and sides swap: TOD part 3–5 ← HOTD 6–8, 6–8 ← 3–5,
  10–12 ← 14–16 (+17 toe), 13–15 ← 10–12 (+13 toe).
- Per TOD part: rotate the posed HOTD mesh so its bone (joint → child joint) points along the TOD bone, stretch it
  along the bone to the TOD bone length (uniform scale = TOD/HOTD neck-to-ankle height across), move its joint to
  the TOD node. Torso and hips: frame alignment (up = joint → neck / legs, side = shoulder / hip line). Head, hands
  and feet: their parent's rotation (the head must follow the torso; aligning it to world up tilted faces).
- Textures: all HOTD pages and flat colours (8×8 swatches) of the body in one power-of-two atlas, put into the TOD
  bank texture slot shared with the fewest other banks, saved as `<index>_id<id>.png` (ids are shared between
  banks, so the id match is needed). UVs: v counts from the PNG top (todmod flips v in OBJs and rows in PNGs).
- TOD materials: each converted model uses chunk `m<k>` of its original: textured, white, without word0 bit
  0x02000000 (the head's chunk 0 has it and drew the head black), most vertices.
- Gameplay zombies draw more than the 15 BodyInitTbl models: head animation frames (znsam2 8974–9003, same vertex
  count as the head), damaged parts, and extra pieces (8966, a hair/hat piece). The converter writes the HOTD part
  for every bank model within 1.0 (bounding-box distance) of a part or with the part's vertex count within 3.0, and a
  hidden stand-in (one zero-area triangle) for other models within 4.0 and for TOD parts the HOTD body lacks.
- Verified in game 2026-10-27 (stage 1, 1920×1080): body 24 / 84 / 28 zombies walk and animate with TOD's motions,
  textures right, faces forward; no crash. Belts are skipped for replaced models (`re/NOTES.md`).
- `hotd_zombies`: TOD kinds 1–20, 22, 24, 25 ← HOTD bodies 24, 84, 57, 62, 36, 35, 28, 29, 30, 53, 61, 73, 82, 83,
  86, 45, 47, 11, 12, 16, 32, 31, 33.

## Heads, humans and the configurator (todmod 0.21.0)
- HOTD head meshes face the opposite way to what the motion frames give (in every motion, e.g. body 28: red mask
  at −z, jacket lapels at +z in the upright frame; the game replaces head matrices at run time, joint walk
  `FUN_00476bc0` look-at branch). Fix: turn the head half round about its neck axis (neck → mesh centre), in
  `assemble.place` and the converter; the converter then stands the head axis upright and keeps the chest's facing.
  Verified in game: kind 7 (body 28) zombies show the red mask to the camera.
- Roles from the tree (`to_tod.skeleton_roles`): root children = chest (taller subtree: head leaf + two arm chains)
  and hips (leg chains of ≥ 3 joints; shorter chains such as the humans' coat tails 2/3 ride on the hips). Forward
  = where the toes point (foot → toe joint), right = forward × up; works for 18-joint zombies (same mapping as
  before) and 20-joint humans (1 hips, 2/3 coat tails, 4–7 / 8–11 legs, 12 chest, 13 head, 14–16 / 17–19 arms).
- `hotd_humans` (`make_human_pack.py`): 31 TOD kinds (civilians 32–55, Sophie-like 56/68, Hou/Logan 62/63/66/67,
  detective 64) ← HOTD bodies 2, 13, 15 (agents/men in suits), 14, 34/67 (Sophie), 17/19–22/49 (scientists),
  23/25–27 (women), 60/78 (robed figure). TOD's own player kinds 57/58 are left alone. `characters.tsv` names them.
- Configurator Characters tab: `game_models_set_packs()` (the Model packs box) — a node draws `models\<pack>\
  [<bank>\]<id>.obj` (z and the material slot as todmod reads them, slot texture from `textures\<pack>\<bank>\
  NNN*.png` via GDI+, v from the PNG top) instead of the pol.HAB object; labels come from `characters.tsv`.
- One test run stopped logging after Hou's models loaded (someone typed during it; not reproduced in three runs
  through the same cutscenes). `FUN_0047c740` blends head frames 0x1b0f–0x1b57 / 0x1d5–0x1fa into 0x1b0e / 0x1d4
  in lockstep (komono_4 props, not characters).

## Next (not done)
- HOTD bosses and 16-joint creatures; damaged parts from HOTD's own red variants.
- Typing game conversion: plan in `hotd/TYPING_PLAN.md` (2026-10-28). Entry point: thotd.exe imports only
  `gblGetButtonStatus`, `gblGetCursorPosition`, `gblSetScreenMode` from the light-gun driver `gamebios.dll`, so a
  proxy gamebios.dll can aim and fire (input device "Lightgun 1").
