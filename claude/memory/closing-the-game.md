---
name: closing-the-game
description: How to close Tod.exe during tests without leaving the quit confirmation box for the user
metadata:
  node_type: memory
  type: feedback
  originSessionId: 895f1349-b290-4eaf-8720-335612d489b5
  modified: 2026-10-26T22:21:15.447Z
---

When Claude launches and closes the game (Tod.exe) for a test, it must also answer the game's "quit?" Yes/No box;
use `mod\tools\close_game.ps1` (Alt+F4 + auto-Yes). Plain WM_CLOSE / CloseMainWindow leaves the box open.

**Why:** the user had been clicking those confirmation boxes manually for Claude's test runs and asked Claude to
account for it (2026-10-26).

**How to apply:** for user play-tests, let the user close the game themselves; for Claude's own launch/measure/close
runs, finish with `close_game.ps1` and check that `todmod.log` ends with "game closed normally".
