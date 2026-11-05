---
name: reaching-gameplay
description: "How to get from game launch into the Arcade car opening and gameplay by pressing Enter (7 presses, not 4)"
metadata:
  node_type: memory
  type: reference
  originSessionId: 39bf59fa-b85d-4813-b053-f4a0e68b2499
  modified: 2026-10-27T18:54:36.538Z
---

Tod.exe starts with a cutscene. Pressing ENTER repeatedly skips through it and the menus (ARCADE -> CHAPTER
SELECT) into Arcade mode: the car-driving opening cutscene, then gameplay. (User, 2026-10-27.)

Four presses (`-EnterFrom 5 -EnterUntil 10 -EnterEvery 1.5`) stop at the menu. The user said to add three more:
`-EnterFrom 5 -EnterUntil 14 -EnterEvery 1.5` (7 presses) gives "The First Chapter" at ~17 s, the car opening
~20-33 s (subtitles), and gameplay (zombies with words) by ~38 s; verified 2026-10-27. "PRESS ENTER KEY /
CREDIT(S)" is the player-2 join prompt and shows in Arcade too, so it does not mean the attract demo.

**How to apply:** for unattended in-game checks, launch with `mod\tools\capture_game.ps1` from the PowerShell tool.
Send keys only while the game window is the foreground window (never type into another app). Close with
`close_game.ps1` ([[closing-the-game]]).
