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

### Sweep diminishing returns in 90-95% band

90-95% AT_LIMIT band sweep (335 functions) yielded **1 improvement in 25
functions processed** (+0.09%) before manual port work overtook it. Most
mismatches in this band are prologue/regalloc divergences that the permuter's
source transformations can't shift. Wave-2 funded the conclusion: at sub-95%,
manual upstream-port (or audit-style root-cause finds) beats automated permuter.

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

3. **Skip the 90-95% permuter sweep** — confirmed low hit rate (~4%, tiny
   gains). The remaining at-limit functions in this band are dominated by
   regalloc/prologue divergences. Direct manual work has 10-100× better ROI.

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
