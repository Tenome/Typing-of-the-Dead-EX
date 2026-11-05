---
name: visible-test-targets
description: "When testing texture/model replacements in game, pick assets that are on screen almost always (zombies), not props"
metadata:
  node_type: memory
  type: feedback
  originSessionId: 895f1349-b290-4eaf-8720-335612d489b5
  modified: 2026-10-27T13:48:11.060Z
---

For in-game replacement tests (textures, models), choose a target that is on nearly every screen — the zombies (e.g. banks like znsam2, znonoo, znnick in todmod_models.log / todmod_textures.log) — instead of props such as crates that may not appear in the demo.

**Why:** a crate test (2026-10-27) needed several runs and zoomed crops to find one crate; the user pointed out zombies are visible almost everywhere.

**How to apply:** check which zombie banks load in the attract demo/gameplay (log_models / log_textures), replace their textures or models with something obvious, then screenshot. Related: [[reaching-gameplay]]
