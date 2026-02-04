## Session: January 23, 2026 (Character & Math Focus)

### Summary

Parallel subagent session targeting **system/char Load functions** (99%+ matches) and **math function diagnosis**. Using objdiff CLI instruction-level diffs and RB3 reference.

**Note:** Render functions (Mat, Shader, Group) being worked on in separate thread.

### Planned Targets

#### system/char Load Functions (99%+ - Quick Wins)

These Load functions are 1-2 instructions away from 100%:

| Function | Unit | Match % | Size | Status |
|----------|------|---------|------|--------|
| `CharLipSync::FindLipSyncForSound` | CharLipSync | 99.90% | 192 | Planned |
| `CharCollide::Load` | CharCollide | 99.86% | 804 | Planned |
| `CharInterest::Load` | CharInterest | 99.85% | 724 | Planned |
| `Waypoint::Load` | Waypoint | 99.82% | 608 | Planned |
| `CharPosConstraint::Load` | CharPosConstraint | 99.81% | 580 | Planned |
| `CharIKHead::Load` | CharIKHead | 99.79% | 544 | Planned |
| `CharBonesBlender::Load` | CharBonesBlender | 99.79% | 540 | Planned |
| `Character::PreLoad` | Character | 99.79% | 536 | Planned |
| `CharIKFingers::Load` | CharIKFingers | 99.79% | 536 | Planned |
| `CharSleeve::Load` | CharSleeve | 99.77% | 496 | Planned |

#### Math Functions (Need Diagnosis)

| Function | File | Match % | Size | Status |
|----------|------|---------|------|--------|
| `RatioToDb` | Decibels.cpp | 90.8% | ~100 | Planned - needs objdiff diagnosis |
| `InterpTangent` | Key.cpp | 82.9% | ~150 | Planned - needs objdiff diagnosis |

### Resources

- **objdiff CLI:** `~/code/milohax/objdiff/target/release/objdiff-cli`
- **RB3 Reference:** `~/code/milohax/rb3/src/system/char/`
- **Diagnosis command:**
  ```bash
  objdiff-cli diff -p . "FUNCTION_NAME" -f json --include-instructions \
    | jq '[.instructions[] | select(.match_type | test("diff|replace"))][:15]'
  ```

### Session Progress

12 parallel agents deployed to diagnose system/char Load functions and math functions.

---

### Functions Fixed This Session

| File | Function | Before | After | Fix Applied |
|------|----------|--------|-------|-------------|
| CharLipSync.cpp | `FindLipSyncForSound` | 99.90% | **100%** ✓ | Changed `unsigned int ext` → `int ext`, `ext > 0` → `ext >= 0` |
| Decibels.cpp | `RatioToDb` | 76.6% | **90.7%** | Inverted ternary: `(ratio <= 0.0f) ? -96.0f : ...` |
| CharBonesBlender.cpp | `CharBonesBlender::Load` | 99.79% | 99.79% | Changed `d >>` to `bs >>` (matches RB3) |

### Functions at Compiler/Linker Limit (Confirmed via objdiff CLI)

**Root Cause:** The `ASSERT_REVS` macro creates a static `gRevs` array. The linker assigns generic labels (`lbl_XXXXXXXX`) instead of the mangled C++ symbol (`?gRevs@...`). This affects ALL Load functions using `ASSERT_REVS`.

| File | Function | Match % | Diagnosis |
|------|----------|---------|-----------|
| CharCollide.cpp | `CharCollide::Load` | 99.86% | Linker symbols + merged Read3FloatStruct |
| CharInterest.cpp | `CharInterest::Load` | 99.22% | Linker symbols + argument evaluation order |
| Waypoint.cpp | `Waypoint::Load` | 99.82% | Argument evaluation order in ASSERT_REVS |
| CharPosConstraint.cpp | `CharPosConstraint::Load` | 99.81% | Linker symbols + register allocation |
| CharIKHead.cpp | `CharIKHead::Load` | 99.79% | Linker symbols + merged Read3FloatStruct |
| CharSleeve.cpp | `CharSleeve::Load` | 99.77% | Linker symbols |
| CharForeTwist.cpp | `CharForeTwist::Load` | 99.76% | Linker symbols |
| CharUpperTwist.cpp | `CharUpperTwist::Load` | 99.74% | Linker symbols |

### Functions Needing Further Investigation

| File | Function | Match % | Notes |
|------|----------|---------|-------|
| Key.cpp | `InterpTangent` | 82.9% | Fused multiply-add vs separate ops, "regswaps" - difficult |
| Decibels.cpp | `RatioToDb` | 90.7% | `__FILE__` path differences block remaining 9% - build system issue |

### Key Discovery: ASSERT_REVS Linker Limit

All system/char Load functions using `ASSERT_REVS(rev1, rev2)` have the same unfixable pattern:
1. Static `gRevs[4]` array gets linker label instead of symbol name
2. Argument evaluation order differs in MILO_FAIL calls
3. These show as `diff_arg` in objdiff (unfixable at source level)

**Implication:** ~20+ Load functions at 99%+ are at their practical limit.

---

### Recommended Next Targets

Based on brainstorm analysis, these are the best next targets (avoiding linker-blocked functions):

#### Quick Wins (<100 bytes at 98%+)

| Function | File | Match | Size | Notes |
|----------|------|-------|------|-------|
| `String::operator==(FixedString)` | Str.cpp | 99.1% | 56b | Likely same fix for both |
| `String::operator==(Symbol)` | Str.cpp | 99.3% | 56b | Likely same fix for both |
| `complex::operator*` | complex.cpp | 99.3% | 60b | Synth already has 100%s |
| `Box::Clamp` | Geo.cpp | 99.5% | 160b | system/math, RB3 compatible |
| `RndMeshDeform::BoneDesc::operator=` | MeshDeform.cpp | 99.1% | 92b | Related Copy() at 99.5% |

#### High-ROI Clusters

| Area | Functions 95%+ | Notes |
|------|----------------|-------|
| **system/flow** | ~15 | Fix one → pattern for many. Start with `FlowSwitch::ActivateTransitionCases` (99.4%) |
| **system/math** | ~8 | Pure math, RB3 reference available |
| **rndobj Copy()** | ~12 | Similar patterns across all |

#### Categories to Avoid

- system/char Load functions using `ASSERT_REVS` (linker limit)
- Functions at 99.95%+ (linker artifacts, 1-2 bytes)
- STL template instantiations (compiler-generated)

---

### Session Complete

**Summary:** 12 parallel agents diagnosed system/char Load functions. Major discovery: ALL 99%+ Load functions are blocked by `ASSERT_REVS` linker limits. One function fixed to 100% (`FindLipSyncForSound`), one improved (`RatioToDb` 76%→90%). Clear next targets identified in system/utl and system/flow.
