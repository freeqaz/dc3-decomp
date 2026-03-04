# Session: Verdict Pipeline Fix + Skeleton/Bone Audit

**Date**: 2026-03-04

## Summary

Fixed the verdict pipeline that was producing thousands of false COMPLETE verdicts. Root cause: objdiff's `report generate` returns `fuzzy_match_percent: 100.0` for unimplemented stubs (base_size=0 divide-by-zero guard), and `ingest_report.py` was setting verdicts from this unreliable data.

## Root Cause Analysis

### The base_size=0 Bug in Report Generation

In `objdiff-core/src/bindings/report.rs` line 249:
```rust
if self.total_code == 0 {
    self.fuzzy_match_percent = 100.0;
}
```

When a function has no decomp code (stub), `total_code == 0`, and the report sets match% to 100%. This is correct for unit-level aggregation (empty units shouldn't penalize overall progress) but wrong for per-function verdicts.

The `objdiff diff` command (used by `sync_objdiff.py` and the MCP orchestrator) computes match% correctly using the **target** function size as denominator. Only `report generate` has this bug.

### Pipeline Flow (Before Fix)

```
ninja report.json        → fuzzy_match_percent: 100.0 for stubs (BUG)
  ↓
ingest_report.py         → verdict = "COMPLETE" for 100% match (propagates bug)
  ↓
sync_objdiff.py          → never downgrades COMPLETE (preserves bad verdict)
  ↓
DB: 21,796 false COMPLETEs
```

### Scale of the Problem

| Category | Count |
|----------|-------|
| False COMPLETE (match% < 100) | 20,515 |
| False COMPLETE (never checked, NULL %) | 1,281 |
| Genuine COMPLETE (100% match) | 9,943 |
| AT_LIMIT | 2,469 |

## Fixes Applied

### 1. `scripts/orchestrator/database.py` — `ingest_report()`

**Change**: Removed all verdict-setting logic from report ingestion. `ingest_report.py` now only updates metadata (symbol, demangled name, unit, size). It no longer sets `current_percent` or `verdict` from report.json data since that data is unreliable.

**Rationale**: report.json match% is unreliable (base_size=0 bug). Verdicts should only come from:
- `sync_objdiff.py` — runs actual objdiff diffs, reliable match%
- Agents/humans — via `report_result` MCP tool

### 2. `scripts/sync_objdiff.py` — Added verdict downgrade logic

**Change**: Added downgrade path for false verdicts:
- `COMPLETE → NULL` when objdiff shows match% < 100%
- Tracks `function_meta` (verdict, best_percent) from DB to make downgrade decisions

**New stats in output**:
- `Demoted COMPLETE: N (-> NULL)`
- `Demoted AT_LIMIT: N (-> NULL)`

### 3. DB Reset

Reset 21,796 false COMPLETE verdicts to NULL:
```sql
UPDATE functions SET verdict = NULL
WHERE verdict = 'COMPLETE' AND (current_percent IS NULL OR current_percent < 100.0);
```

Then ran `sync_objdiff.py --all -j16` to re-scan all functions with correct logic.

## Skeleton/Bone Audit

### HamSkeletonConverter Work (from previous session)

| Function | Match% | Status |
|----------|--------|--------|
| `SetLeg` | 77.8% | AT_LIMIT (NaN check, Plane::Set, RotateTowards, IK chain) |
| `Set` | 66.7% | AT_LIMIT (504 instructions, coordinate transform + full skeleton processing) |

### False COMPLETEs in Skeleton/Bone/Gesture Units

Top units with false COMPLETEs (functions marked COMPLETE but actually < 100%):

| Unit | False COMPLETEs |
|------|----------------|
| CharBoneDir | 26 |
| NavigationSkeletonDir | 21 |
| SkeletonViz | 20 |
| SkeletonDir | 20 |
| DepthBuffer3D | 14 |
| StreamRecorder | 13 |
| StreamRenderer | 12 |
| SkeletonClip | 12 |

Notable stubs (0%, fully unimplemented):
- `DepthBuffer3D::DrawShowing` — 5,188 bytes, 1,297 instructions
- `GestureMgr::Handle` — 4,516 bytes (actually 98.8%, MakeString noise)
- `StreamRenderer::DrawToTexture` — 3,400 bytes
- `StreamRenderer::SyncProperty` — 3,604 bytes

### Verdict System Design

| Verdict | Meaning | Set By |
|---------|---------|--------|
| `COMPLETE` | Done, 100% match | `sync_objdiff.py` (auto-promote at 100%) |
| `AT_LIMIT` | Agent gave up, may be revisitable | Agent `report_result` |
| `NULL` | Workable, not yet decided | Default / demoted |

Key rule: **sync_objdiff.py is the source of truth** for COMPLETE. Agents set AT_LIMIT. Both can be demoted if conditions change.

## Results After Fix

### sync_objdiff.py --all Results

| Metric | Count |
|--------|-------|
| Scanned | 32,208 |
| Matched | 27,686 |
| Not found | 1,243 |
| Unimplemented (stubs) | 3,279 |
| Promoted to COMPLETE | 5,627 |
| Demoted | 0 (already reset) |

### Final DB State (after improved detection + auto AT_LIMIT)

| Verdict | Count |
|---------|-------|
| AT_LIMIT | 17,098 |
| COMPLETE (100% match) | 8,383 |
| NULL (workable) | 6,532 |

Workable breakdown:
- 4,806 at 90-99% (have some fixable mismatches mixed in)
- 1,263 unchecked
- 406 at 50-89%
- 54 at 1-49%
- 3 at 0% (stubs)

### objdiff Analysis Improvements (Phase 1)

Extended `detect_address_relocation_noise` in `objdiff-core`:
1. **`bl`/`b` with same symbol** — branch calls to same function at different addresses
2. **`lwz`/`stw`/etc. with same symbol** — memory ops with same symbol relocation
3. **`lbl_XXXXXXXX` vs proper symbol** — target .obj raw labels vs decomp named symbols
4. **Same-name `lis`/`addi` with identical args** — relocation-only differences where text args match

Also added verdict logic: when ALL mismatches are attributed to unfixable patterns (0 unattributed), auto-classify as AT_LIMIT.

**Impact**: Address relocation detection: 5,265 → 20,140 (+283%). Auto AT_LIMIT: 15,135 functions.

### objdiff Analysis Improvements (Phase 2)

Investigated remaining 4,806 workable functions at 90-99% with unattributed mismatches. Found 6 types of undetected patterns:

#### New detections added:

1. **CRT save/restore suffix** (`__savegprlr` vs `__savegprlr_14`) — Different entry points in the CRT fall-through save/restore chain. Added `is_crt_save_restore_diff()` function with regex matching `__(save|rest)(gpr|fpr|vmx)(lr)?(_\d+)?$`.

2. **Any-opcode address relocation** — Changed default arm from no-op to check `has_same_symbol_reloc()` for ANY opcode (not just lis/addi/bl/stw). Catches `mr`, `cmpw`, etc. with symbol relocs.

3. **Identical args text** — When `diff_arg` has identical text on both sides (relocation address differs but symbol text matches), detect as address relocation. Added to `bl`/`b`, `lis`, `addi`, and default arms.

4. **ICF const-qualifier merging** — MSVC mangling `@@QAA` (non-const) vs `@@QBA` (const). Added regex normalization in `has_same_symbol_reloc()` Case 3.

5. **ICF template merging** — Extended `detect_linker_merged` to detect different instantiations of the same template (e.g., `ObjRefConcrete<RndDrawable>` vs `ObjRefConcrete<RndTransformable>`). Uses `msvc_template_base()` extraction.

6. **ICF cross-function merging** — Any `bl` diff_arg where both sides are MSVC-mangled (`?`-prefixed) but different symbols → ICF of unrelated functions with identical machine code.

#### Fixability reclassifications:

- **ScopeCounterMismatch**: `LikelyFixable` → `Unfixable` (scope counters depend on function ordering in TU, not controllable)
- **PrologueMismatch**: `MaybeFixable` → `Unfixable` (compiler decides callee-saved register count)
- **`lis` always counted**: Any `diff_arg` on `lis` is address relocation (PowerPC `lis` is always address loading)

## Results After Phase 2

| Verdict | Count (Phase 1) | Count (Phase 2) | Delta |
|---------|---------------:|----------------:|------:|
| AT_LIMIT | 17,098 | 19,791 | +2,693 |
| COMPLETE | 8,383 | 8,382 | -1 |
| NULL (workable) | 6,532 | 3,840 | -2,692 |

Workable breakdown after Phase 2:
- 2,114 at 90-99%
- 406 at 50-89%
- 45 at 1-49%
- 12 stubs (0%)
- 1,263 unchecked

## Permuter: const_overload Pattern

Added `scripts/permuter/patterns/const_overload.py` — tries adding/removing `const` on local variable declarations to change which method overload the compiler selects.

**Note**: For ICF-merged const/non-const overloads, changing const doesn't help because both versions have identical machine code. But the pattern is valuable for non-ICF cases where const genuinely changes code generation.

## Files Modified

- `scripts/orchestrator/database.py` — Removed verdict logic from `ingest_report()`
- `scripts/sync_objdiff.py` — Added verdict downgrade logic, auto AT_LIMIT from objdiff verdict, tracks function_meta
- `../objdiff/objdiff-cli/src/cmd/analysis.rs` — Extended address relocation detection (Phase 1 + Phase 2), ICF template/cross-function detection, CRT save/restore detection, fixability reclassifications
- `scripts/permuter/patterns/const_overload.py` — New permuter pattern for const qualifier variations
- `scripts/permuter/patterns/__init__.py` — Register const_overload pattern
- `decomp.db` — Reset 21,796 false COMPLETEs, re-synced with `--all` twice
