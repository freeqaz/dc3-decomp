# og-dc3-decomp upstream-port lane + audit Wave 2 (2026-05-27)

## Summary

Continuation of the 2026-05-27 permuter sweep session. After verifying the
two "perfect" claims from the prior segment (PhysicalAlloc, RndGraph::DrawAll
both 100% normalized), opened the **og-dc3-decomp upstream-port lane** — a
previously-unknown 834-file parallel DC3 decomp tree under
`/home/free/code/milohax/og-dc3-decomp/`. Same compiler/target as DC3,
making it a first-class port source (RB3 is MetroWerks/Wii, often diverges).

Combined with the concurrent agent's **audit Wave 2** (6 more functions to 100%
via the metric-audit candidates list), this session moved ~10 functions
significantly, with 5 reaching 100% and 4 going from AT_LIMIT to near-100%.

## Commits this segment

| Hash | Subject | Lane |
|------|---------|------|
| `56fc812f` | `rndobj/TexBlender: restructure DrawShowing to nested form (88.6%->91.6%)` | og-dc3 port (subagent) |
| `11a04efc` | `rndobj/TexRenderer: split mirror loop + static var rename (90.6%->91.5%)` | RB3 hybrid port (subagent) |
| `8f457ca1` | `rndobj/EventTrigger: port Cleanup from og-dc3 (89.3%->98.1%)` | og-dc3 port (subagent) |
| `0711f1f6` | `rndobj/Font: UpdateChars to 100% (90.5%->100%)` | iterative regalloc surgery (subagent) |
| `a10eec2a` | `decomp: audit fixes wave 2 — 6 more functions to 100%` | audit Wave 2 (concurrent agent) |
| `20ec2664` | `docs: audit handoff — log wave 1/2 dispositions` | audit doc maintenance |
| `da63128c` | `strategy_db: fix diagnosis_category always written as 'unknown'` | permuter infrastructure (concurrent) |

## Wins by lane

### og-dc3 port lane (new this session)
- **RndTexBlender::DrawShowing**: 88.6% → 91.6% (+3.0%). RB3 was an empty stub;
  og-dc3 had a nested-if CFG that DC3's chained early-returns didn't match.
- **EventTrigger::Cleanup**: 89.3% → 98.1% (+8.8%). Verbatim port from og-dc3
  with FOREACH macros, simplified iterator triplet, branch reorder.

### Mixed-lane (og-dc3 stub + iterative regalloc surgery)
- **RndFont::UpdateChars**: 90.5% → 100% (+9.5%). og-dc3 stub + RB3 single-page
  variant both unsuitable. Solved by 3-step iterative regalloc surgery:
  1. Hoist `Vector2 pos(0,0)` out of loop → +0.7%
  2. Drop posX/posY scalars, use pos.x/pos.y directly → +1.5%
  3. Reorder page-rollover branch with `bmap` caching → +7.3% to 100%

### RB3 port lane (legacy)
- **RndTexRenderer::DrawToTexture**: 90.6% → 91.5% (+0.9%). Hit the regalloc
  ceiling (r23↔r24 swap on `this` cascades through; FPR window mismatch
  f14-f31 vs f26-f31).

### Audit Wave 2 (concurrent agent)
- **RndCam::UpdateLocal** 99.95% → 100% — wrong field accesses (`m.z.x` → `m.y.z`)
- **Splash::{Suspend,Resume,EndSplasher,UpdateThread}** 99.0-99.5% → 100% —
  vtable slot order bug in NgRnd base class (Suspend/Resume swapped)
- **UIListLabel::CreateElement** 99.5% → 100% — Copy() third arg was
  kCopyDeep, target expected kCopyShallow

## Verified from prior segment

The two "perfect" claims from `docs/sessions/2026-05-27-permuter-sweep-95-99-band.md`
verified with `mcp__orchestrator__run_objdiff`:
- **PhysicalAlloc**: 100% normalized (99.4% raw) ✓
- **RndGraph::DrawAll**: 100% normalized (95.9% raw) ✓

The build manifest issue from the prior segment turned out to be **the
concurrent permuter-dev session continuously touching `scripts/permuter/*.py`
files** — configure.py kept re-running and never stabilizing. Bypassed by
using `mcp__orchestrator__run_objdiff` directly (function-granular build).

## New knowledge captured

### og-dc3-decomp as a first-class upstream source

Saved as `reference_og_dc3_decomp.md` in auto-memory. Key points:
- Path: `/home/free/code/milohax/og-dc3-decomp/`
- 834 .cpp files, same compiler/target as DC3
- Use when RB3 stubs the function or has structural drift (different inheritance,
  different compiler)
- og-dc3 itself may have stubs (RndFont::UpdateChars body was empty in og-dc3),
  so verify before porting

### When upstream port fails: 3-step iterative regalloc surgery

RndFont::UpdateChars demonstrated the recovery flow when both upstreams fall
through:
1. **Hoist temporaries out of loops** to stabilize stack slot allocation
2. **Drop scalar locals** that mirror struct fields — use struct access directly
3. **Reorder branches** + cache values across calls to eliminate redundant
   loads

Each step verified with `run_objdiff`. Subagent used `run_diff_inspect mode="mismatches"`
between steps to pick the next cluster.

### 90-95% band sweep — full results (committed `6e9b495a`)

The 90-95% AT_LIMIT sweep (335 functions) ran to completion: **37 improved,
283 no-change, 15 errors, +48.7% total delta**. Landed 32 of the 37 (commit
`6e9b495a`); the others were churn (+0.01-0.05) in already-committed files or
TexRenderer's +0.04 on top of its earlier commit.

**Caution learned: don't judge a sweep mid-run.** When checked at 25/335 the
sweep showed only 1 win (+0.09%) — the early alphabetical-ish functions were
low-yield. The back half held the big wins:
- HamDirector::LoadCrew         90.7 → 99.2 (+8.4)
- PoseFatalities::UpdateClipDriver 93.7 → 99.2 (+5.6)
- PartyModeMgr::ResetModes       92.7 → 98.2 (+5.5)
- MetagameRank::AwardForRankUp   94.9 → 98.9 (+4.0)
- HamNavList::DrawShowing        90.7 → 94.5 (+3.9)
- SpotlightDrawer::DrawWorld     91.3 → 94.3 (+3.0)

The 90-95% band is **worth sweeping** (≈11% hit rate, +48.7% total). The
mid-run pessimism in the prior draft of this note was wrong. Run the full
sweep, don't extrapolate from the first 10%.

**Harvest safety (autonomous):** the working tree mixed sweep output with
concurrent-agent work. Separated by: (1) cross-referencing the sweep's JSON
`improvements` list against `git status`, (2) confirming no `.permuter.lock`
files and no sweep-win file modified in the last 3 min, (3) `run_objdiff`
spot-checks that on-disk % == sweep's claimed final %, (4) grepping staged
additions for comments/structs/asserts (none → pure mechanical transforms).
Committed only the 32 verified files by explicit path; `git add -A` would have
bundled Flow.cpp, audit Wave 3, and permuter-dev scripts.

## What to do next

1. **Continue og-dc3 upstream-port wave**. Remaining 80-95% candidates from
   `scripts/at_limit_rb3_candidates.py --min-percent 80 --max-percent 95
   --min-size 300`:
   - `WorldCrowd::WorldCrowd` (86.8%, sz=836)
   - `AnimTask::AnimTask` (84.8%, sz=816) — but has OFFSET_SWAP suggesting
     ObjPtr internal field issue, possibly engine-wide
   - `StorePreviewMgr::Poll` (88.9%, sz=796)
   - `RndWind::SelfGetWind` (84.6%, sz=720)
   - `CharClipDriver::CharClipDriver` (93.0%, sz=632)

2. **Audit Wave 3.** ~70 candidates remain from
   `docs/sessions/2026-05-26-objdiff-metric-audit-dc3.md`. Wave 1+2 fixed 10.
   Agents 4 (AccomplishmentProgress), 6 (Spotlight class), 11 (HamVisDir),
   12 (DataFlex), 13 (Profile::GetPadNum sweep) are unstarted.

3. **Run the sub-90% permuter sweep** (task #15). The 90-95% sweep landed
   +48.7% (32 wins) — the band is productive, contrary to the mid-run guess.
   Command: `venv/bin/python -m scripts.permuter.batch_auto --target workable
   --include-at-limit --min-pct 80 --max-pct 90 --limit 0 --json 2>&1 | tee
   cl_temp_files/permuter/sweep_80_90.log`. Run to completion before judging.

4. **Mark stale DB stats.** `CheckBSPTree` was logged as 89.3% in the function
   DB but is actually 99% AT_LIMIT (regswap, unfixable). The DB has more stale
   entries — `scripts/orchestrator/sync_decomp_db.py` (or equivalent) should
   refresh from objdiff measurements.

## Commands for next session

```bash
# Verify any newly-claimed perfects
mcp__orchestrator__run_objdiff symbol=<symbol> unit=<unit> \
    project_dir=/home/free/code/milohax/dc3-decomp

# Continue upstream-port wave (set up parallel subagents)
venv/bin/python scripts/at_limit_rb3_candidates.py --min-percent 80 \
    --max-percent 95 --min-size 300 --limit 20

# Check og-dc3 has the function before porting
ls /home/free/code/milohax/og-dc3-decomp/src/system/<subsys>/<File>.cpp
diff -u <our> <og-dc3> | head -80

# Resume the 90-95% sweep (low priority)
venv/bin/python -m scripts.permuter.batch_auto --target workable \
    --include-at-limit --min-pct 90 --max-pct 95 --limit 0 --json \
    --resume cl_temp_files/permuter/sweep_90_95_logs/

# Refresh progress
venv/bin/python scripts/get_progress.py
```

## Reference docs

- `docs/sessions/2026-05-27-permuter-sweep-95-99-band.md` — prior segment
- `docs/sessions/2026-05-26-objdiff-metric-audit-dc3.md` — audit doc with
  ~70 remaining candidates
- `docs/decomp/UPSTREAM_PORT_WORKFLOW.md` — full workflow doc
- `docs/decomp/patterns/INDEX.md` — pattern catalog

## Working tree state at session end

Many concurrent agents working in parallel — working tree has ~24 modified
files from:
- Audit Wave 2 (Cam.cpp, Rnd.h, MoggClip.{cpp,h}, UIListLabel.cpp, Splash, ...)
- Permuter engine development (scripts/permuter/*.py, new patterns)
- My subagent work (TexRenderer.cpp, TexBlender.cpp, Font.cpp, EventTrigger.cpp — all committed)

Don't touch the uncommitted files — they belong to other agents.
