# Port-Harvest Workflow (shared mechanics for all 3 streams)

This is the proven pipeline from the 2026-05-30 session (3 waves, ~73 functions to
exact 100%, net 0 regressions). All three work-stream docs in this directory reference it.

## Core principle
The **target binary is the oracle** (objdiff confirms every change). og-dc3-decomp
(`../og-dc3-decomp`, same compiler cl.exe v16.00.11886 + same target 373307D9) is a
**proven source to copy from**, NOT an oracle. Porting og-dc3's exact source matches the
target because register allocation is driven by *declaration order* — og-dc3 carries the
original's structural DNA. (Compiler-flag fixes are a dead end — proven; see
`docs/plans/compiler-instrumentation.md`.)

## Refresh candidate data (do this first each session)
og-dc3 and our tree both move. Rebuild og-dc3's report, then re-run the differ:
```bash
cd ../og-dc3-decomp && python3 configure.py && ninja           # refresh og-dc3 report.json (~min)
cd ../dc3-decomp
python3 scripts/analysis/og_dc3_port_candidates.py --min-gain 3 --json /tmp/claude/cand.json
```
Signal: **og% > our%** ⟹ og-dc3's source differs in a helpful way → port it. (Same
compiler/target, so a better match can only come from different—better—source.)

## Worktree workflow (one per agent, isolated)
- Create: `scripts/setup_worktree.sh /home/free/code/milohax/wt/<name> <branch>` (~50s, primes ninja, symlinks toolchain). Put worktrees under `/home/free/code/milohax/wt/`.
- **zsh gotcha:** do NOT `set -- $var` in a loop — zsh doesn't word-split unquoted vars; you get malformed paths. Use explicit per-line `mk <name> <branch>` calls in a bash script.
- **Sandbox:** worktrees live outside the repo; if the sandbox is on, creation fails with "Read-only file system" — toggle with `/sandbox` or run with sandbox disabled.
- Create N worktrees in ONE background bash script (serial inside), then launch agents when done.
- Cleanup after merge: `git worktree remove --force <path>; git worktree prune; git branch -D <branch>`.

## Subagent workflow
- **Concurrency:** ≤6 agents at once. One **unit per agent** (shared og-dc3 file → one build context).
- **Model:** Opus for big/complex/math/regalloc-heavy units; Sonnet for near-done mechanical ports. If Sonnet stalls below target, relaunch the unit with Opus.
- **Per-function recipe (in the prompt):**
  1. Baseline: `mcp__orchestrator__run_objdiff` symbol=<sym> **`project_dir="<worktree>"`** (ALWAYS pass project_dir or you test the wrong tree).
  2. Diff our function vs og-dc3's; port og-dc3's EXACT implementation — statement order, **local declaration order**, expressions, casts, helper calls.
  3. Adapt to our headers/types where they differ; preserve semantics + the codegen-determining structure.
  4. Re-run run_objdiff; iterate to 100%. Deeper analysis: `run_diff_inspect` mode=diagnose/mismatches/stack-layout.
- **Expected outcome:** ~75-85% of partial-ports reach 100%. Residuals are register-allocation / FPR / scheduling floors (accept them; they're the genuine ceiling).

## Regression guardrails (learned the hard way)
- **NEVER reorder struct/class MEMBERS in a header without a full-unit regression sweep.** A CamShot member reorder fixed 2 functions but **broke 61**. Member reorders cascade to every function using the class. Additive inline getters/setters are safe (no layout change); member reorders are NOT.
- **Porting can add `static` consts/strings that shift TU-global symbol layout** and nick an *adjacent* function (esp. anonymous-namespace siblings, e.g. HolmesClient::CheckReads). After porting, batch-measure the WHOLE unit; accept small at-limit sibling nicks, but if a *complete* function broke, fix it.
- **HX_NATIVE-guarded edits don't affect the decomp build** (it compiles the non-native path) — safe.
- **Check header blast-radius before merging a header change:** `grep -rl "<Header>.h\|<Type>" src/` — if used beyond the unit, the build+sync gate must show 0 complete-functions broken.
- Don't alter `MILO_ASSERT(...)` / `OBJ_MEM_OVERLOAD` macro contents unless verified. Human-readable names; keep members protected/private (getters for external access).

## Merge & validate (orchestrator does this, serially)
- Agents commit **only src files** on their branch (never venv/, function_analysis/). Verify with `git -C <wt> show --stat`.
- Independently confirm each win: `mcp__orchestrator__run_objdiff project_dir=<main repo>` after merge.
- Merge: `git -C <wt> rebase main && git merge --ff-only <branch>` (rebase first since main moves between merges; different files → clean).
- **Regression gate (after each wave):** `python3 scripts/sync_match_percent.py --build --promote` — full rebuild + promote. Read "Improvements / Regressions / Promoted". 0 regressions ideal; **a regression that was a complete (100%) function is the only kind worth reverting/fixing** — fix by porting the affected sibling/unit (e.g. mapping0 after codec.h).
- **Concurrent agents work on main**: NEVER `git add -A`/`git add .` (you'll sweep their uncommitted work); `git add <specific files>` only. NEVER `git stash` in the main repo. `decomp.db` is gitignored shared state — no commit needed.

## Honest expectation (fuzzy% vs completion)
Partial-ports (90-99% → 100%) are huge for **COMPLETE-count / AT_LIMIT→COMPLETE** but move
**fuzzy% only modestly** (each recovers few bytes). Stub-ports (0% → 100%) recover full
function size — the real fuzzy% lever (Stream 2). 3 waves took engine fuzzy 93.76→93.88%
while landing 73 functions. Set expectations accordingly.

## Key references
- Worklists: `docs/plans/port-harvest/stream{1,2}-*.json` (regenerate with the differ each session)
- Differ tool: `scripts/analysis/og_dc3_port_candidates.py`
- Re-triage worklist + classification: `docs/sessions/2026-05-30-atlimit-retriage-{worklist.md,classified.json}`
- Memory: `reference_prize_map_signals` (the full strategy + every lesson)
- Compiler RE (why regalloc floors are floors): `docs/plans/compiler-instrumentation.md`
- Confirmed at-limit floors are tagged in decomp.db `verdict_reason` (`CONFIRMED at-limit 2026-05-30…`) — skip them.
