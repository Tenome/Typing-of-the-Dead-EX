---
name: decomp-skills
description: "User installed lemiur's decompilation skills (orchestrator, matcher, wall-identifier, helper-recovery, permute, decomp-code-rules, native-platform-porting) for the Tod.exe byte-matching decomp"
metadata:
  node_type: memory
  type: reference
  originSessionId: 895f1349-b290-4eaf-8720-335612d489b5
  modified: 2026-10-27T13:10:01.832Z
---

Source: https://github.com/lemiur/Game-Decompilation-Skill-For-Claude, installed in `C:\Users\Michael\.claude\skills\` (2026-10-27).

- `orchestrator` starts a campaign (parallel matcher workers in git worktrees, fixed merge procedure); `matcher` is the per-function loop; `wall-identifier` diagnoses plateaus; `helper-recovery` restores inlined helpers; `permute` only for proven register/schedule residue; `decomp-code-rules` = reader test, fakematch catalogue; `native-platform-porting` after 100 %.
- They need a project `AGENTS.md` filled from `matcher/references/AGENTS-template.md` (project adapter verbs build/match/diff/report/gate/rules, toolchain, annotation format). `re\decomp\` has no AGENTS.md yet; the orchestrator needs git (the project folder is not a git repo yet).
- README asks: request code-quality passes intermittently; the project rule "don't publish decompiled code" still applies here (the README's "open source it" ask conflicts — follow the user).

Related: [[autonomous-mode]]
