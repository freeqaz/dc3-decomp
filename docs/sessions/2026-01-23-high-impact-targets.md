# DC3 Decomp - High Impact Targets Session

**Date:** 2026-01-23
**Starting Progress:** 45.31% matched (21,275 / 46,958 functions)

## Analysis Summary

Used extended objdiff-cli to identify high-impact areas for substantial % improvements.

---

## Tier 1: Quick Wins (99%+, small fixes)

Non-LINKER functions with highest match percentages - likely single-instruction fixes.

| Function | Match % | Size | Unit | Notes |
|----------|---------|------|------|-------|
| `HamScrollBehavior::ScrollUp` | 99.88% | 172b | system/hamobj/HamScrollBehavior | |
| `HamScrollBehavior::ScrollDown` | 99.88% | 172b | system/hamobj/HamScrollBehavior | |
| `FastInvert` | 99.84% | 248b | system/math/mtx | Math, likely operand order |
| `Box::Contains(Triangle)` | 99.82% | 224b | system/math/Geo | Comparison pattern |
| `MemResizeElem` | 99.82% | 224b | system/utl/MemMgr | |
| `MoveDir::PreLoad` | 99.84% | 696b | system/hamobj/MoveDir | Branch/if-else |
| `CharInterest::Load` | 99.85% | 724b | system/char/CharInterest | Branch/if-else |

### Investigation Commands
```bash
~/code/milohax/objdiff/target/release/objdiff-cli diff -p . "HamScrollBehavior::ScrollUp" -f markdown --verdict --include-instructions
~/code/milohax/objdiff/target/release/objdiff-cli diff -p . "FastInvert" -f markdown --verdict --include-instructions
```

---

## Tier 2: Systematic Batch Fix - ByteGrinder

**21 functions** in `system/synth/ByteGrinder` at 98-99% with identical patterns.
Fix one = potentially fix all.

| Function | Match % | Size |
|----------|---------|------|
| op0 | 99.6% | 100b |
| op6 | 99.6% | 104b |
| op41-op47 | 99.3% | 116b |
| op48-op62 | 98.9% | 120b |
| op51 | 99.0% | 120b |

### Investigation Command
```bash
~/code/milohax/objdiff/target/release/objdiff-cli diff -p . "op0@@YA?AVDataNode" -f markdown --verdict --include-instructions
```

---

## Tier 3: High-Volume Load Functions (LINKER-blocked)

These are at 99.8%+ but have `LINKER_MERGED` pattern - likely not fixable with code changes alone.

| Function | Match % | Size | Unit |
|----------|---------|------|------|
| `WorldCrowd::Load` | 99.86% | 1508b | system/world/Crowd |
| `CharCollide::Load` | 99.86% | 804b | system/char/CharCollide |
| `WorldDir::PreLoad` | 99.85% | 748b | system/world/Dir |
| `RndCam::Load` | 99.85% | 732b | system/rndobj/Cam |
| `RndDrawable::Load` | 99.85% | 724b | system/rndobj/Draw |

**Skip these** unless we find a workaround for linker-merged calls.

---

## Tier 4: Harder but High Impact (80-90%)

Larger functions with more work required but significant byte impact.

| Unit | Functions | Likely Fixable | Total Size |
|------|-----------|----------------|------------|
| `lazer/meta_ham/SongSortNode` | 4 | 4 | 3252b |
| `system/oggvorbis/mapping0` | 1 | 1 | 2684b |
| `system/utl/MemMgr` | 4 | 4 | 2596b |
| `system/ui/UI` | 2 | 1 | 2432b |
| `system/synth/ByteGrinder` | 18 | 18 | 2296b |
| `system/oggvorbis/mdct` | 6 | 4 | 2168b |

### Key Functions in this Tier
- `SongSortNode::Text` - 89.0%, 1916b
- `mapping0_forward` - 85.4%, 2684b
- `MemInit` - 85.4%, 1988b
- `UIManager::Init` - 87.7%, 2240b

---

## High Impact Units by Total Bytes

Units with most potential improvement (90-99.9% range):

| Unit | Functions | Total Bytes | Notes |
|------|-----------|-------------|-------|
| system/rndobj/Part | 2 | 7536b | Needs investigation |
| system/world/Spotlight | 3 | 5752b | 2 maybe fixable |
| system/net/curl/lib/http | 4 | 5548b | 3 likely |
| system/hamobj/HamDirector | 8 | 5076b | Mixed |
| system/net/curl/lib/multi | 4 | 4872b | 3 likely |
| system/net/curl/lib/cookie | 6 | 4816b | 4 likely |
| system/rndobj/Mesh | 4 | 4600b | Mixed |

---

## Recommended Priority Order

1. **ByteGrinder** - Systematic fix opportunity, 21 functions
2. **HamScrollBehavior::ScrollUp/Down** - 99.88%, clean target
3. **FastInvert** - Math function, likely operand ordering
4. **Box::Contains** - 99.82%, geometry comparison
5. **MoveDir::PreLoad** - 99.84%, larger impact (696b)

---

## Fix Patterns Reference (from previous session)

| Pattern | Example | Effect |
|---------|---------|--------|
| Comparison style | `< 2` → `<= 1` | Changes cmpwi immediate + branch |
| Boolean inversion | `if (x)` → `if (!x)` | Swaps branch conditions |
| Operation order | `a * b * c` → `a * (b * c)` | Changes fmuls/fadds operand order |
| max() macro | `(a < b ? b : a)` → `(a > b ? a : b)` | Different comparison direction |
| Variable order | Declaration order in constructors | Register allocation |

---

## Session Log

### Completed
- [ ] (none yet)

### In Progress
- [ ] (none yet)

### Blocked
- Load functions with LINKER_MERGED pattern
