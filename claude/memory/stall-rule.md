---
name: stall-rule
description: "In game tests, no typing prompt for 30 s outside a cutscene means the game is stuck (a bug)"
metadata:
  node_type: memory
  type: feedback
  originSessionId: 39bf59fa-b85d-4813-b053-f4a0e68b2499
  modified: 2026-10-29T02:18:59.603Z
---

When testing TOD in game: if not in a cutscene and no typing prompt appears within 30 seconds, treat it as stuck due to a bug.

**Why:** the user saw extra-zombie copies soft-lock stage 1 at the Santa Lucia wall (window zombie never came) while my capture kept running.
**How to apply:** check screenshots at short intervals during gameplay tests; a gap of 30 s with no word box (outside cutscenes) = failure, investigate instead of reporting success. See [[reaching-gameplay]].

Also: if autotype keeps typing far more words than a scene should need (e.g. the same enemies over and over), that is a stuck loop too: close the game (close_game.ps1) right away and read the log/screenshots for where it looped. Use `[debug] log_enemies=1` to see which spawn records were live.
