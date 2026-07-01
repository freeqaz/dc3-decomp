# Native Camera Correctness Fixes — 2026-07-01

Two native-port rendering regressions, investigated and being fixed via a tiered
multi-agent (ultracode) pipeline.

## The two bugs

| # | Symptom | Status | Doc |
|---|---------|--------|-----|
| 1 | **No menu/UI text renders** on native (and web) — bars/icons/venue render, all glyph text is missing | **ROOT-SIGNATURE FOUND: all glyph quads zero-height (1B) + ui.cam framing (1A)** | [06-decisive-experiments.md](06-decisive-experiments.md) |
| 2 | **Camera flips chaotically once a song is loaded** (gameplay venue) | **REPRODUCED + LOCALIZED: camera roll continuum inside CamShot evaluation, nondeterministic** | [06-decisive-experiments.md](06-decisive-experiments.md) |

Both are **DC3-side** and **engine-version-independent** (verified: an `engine@pin`
build reproduces bug 1 identically — the shared engine is not the cause).

## Docs

- [01-root-cause-menu-text.md](01-root-cause-menu-text.md) — full evidence chain + fix design for bug 1.
- [02-camera-flip-repro.md](02-camera-flip-repro.md) — repro procedure + hypotheses for bug 2 (to be filled by discovery).
- [03-diagnostic-toolkit.md](03-diagnostic-toolkit.md) — how to run/screenshot/instrument the native port headless, plus the environment gotchas that will otherwise waste hours.
- [04-workflow-tiers.md](04-workflow-tiers.md) — the Planning → Implementation → Review → Refinement+Landing pipeline and how each tier is run.
- [05-plan.md](05-plan.md) — Tier-1 consolidated plan (partially superseded — see its status section).
- [06-decisive-experiments.md](06-decisive-experiments.md) — **CANONICAL**: gate results, confirmed/refuted hypotheses, repro numbers, suspect commits. Read this first.
- 07-bug2-camera-fix.md / 08-bug1-text-fix.md — written by the Tier-2 implementation lanes (camera worktree `dc3-camerafix`, text worktree `dc3-textfix`).

## Hard constraints (read before touching anything)

- **GPU access needs the sandbox skipped.** Any command that runs `dc3-native`
  (Vulkan) must use `dangerouslyDisableSandbox: true` in its Bash call. This
  applies to subagents too — they are **not** GPU-blocked, they just have to skip
  the sandbox. See [03](03-diagnostic-toolkit.md).
- **Never edit shared main-repo source for A/B tests.** Concurrent agents build the
  main repo. Use a git worktree (`scripts/setup_worktree.sh` for DC3; plain
  `git worktree add --detach` for the engine).
- **Runtime-verify every native fix visually.** Static review + green unit suite is
  NOT sufficient for camera/projection/UI-layout changes — capture screenshots and
  confirm the actual pixels.
- CLAUDE.md rules apply: no `git stash`/`git revert` in the main repo; **no
  `Co-Authored-By` lines** in commits.
