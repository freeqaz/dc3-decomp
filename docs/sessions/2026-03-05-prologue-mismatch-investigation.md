# Prologue Mismatch Investigation (2026-03-05)

## Summary

Investigated prologue mismatches across the codebase — functions where target and decomp use different `__savegprlr_N` counts, meaning different callee-saved register allocation. Found ~56/500 scanned functions have prologue mismatches (~11%).

## Prologue Mismatch Taxonomy

### 1. Parameter Live Range (Fixable)

In `::Load` functions using `LOAD_REVS(bs)`, the macro creates `BinStreamRev d(bs, revs)`. If `bs` is used after the macro (e.g., `SuperClass::Load(bs)`), the compiler keeps `bs` alive in a callee-saved register. Replacing `bs` with `d.stream` kills the parameter's live range, freeing a register.

**~135 Load functions** have `bs` usage after `LOAD_REVS` and are candidates.

### 2. BinStreamRev Chain (Fixable)

Consecutive `d >> a; d >> b;` statements can be merged into `d >> a >> b;`. This matches the target's return-value caching in a callee-saved register. Merging adjacent statements eliminates redundant loads of `d`.

### 3. Dead Register (Unfixable)

Some targets waste a callee-saved register on dead address computation. Example: `MeterDisplay::Copy` has `addi r28, r31, 0x68` that is never used — the compiler computed a member address and then decided not to use it. No source-level fix exists.

## Worked Example: FlowAnimate::Load (96% → 99%)

**Before** (prologue mismatch: `__savegprlr_25` vs `__savegprlr_26`):
```cpp
void FlowAnimate::Load(BinStream &bs) {
    LOAD_REVS(bs);          // creates BinStreamRev d(bs, revs)
    FlowNode::Load(bs);     // bs still alive → callee-saved register
    d >> mBlend >> mWait;
    ...
}
```

**After** (prologue matched):
```cpp
void FlowAnimate::Load(BinStream &bs) {
    LOAD_REVS(bs);
    FlowNode::Load(d.stream);  // d.stream replaces bs → bs dies
    d >> mBlend >> mWait;
    ...
}
```

The `d.stream` member is a reference to the same `BinStream`, so semantics are identical. Killing the `bs` live range frees one callee-saved register, matching the target's prologue.

## BinStreamRev Chain Merging

**Before**:
```cpp
d >> mBlend;
d >> mWait;
d >> mDelay;
```

**After**:
```cpp
d >> mBlend >> mWait >> mDelay;
```

`operator>>` returns `BinStreamRev&`, so chaining is valid. The chained form caches the return value in a callee-saved register across the chain, matching how the target was compiled.

## MeterDisplay::Copy — Dead Register Analysis (Unfixable)

Target has `addi r28, r31, 0x68` (computes `&this->mSomeField`) that is never read or stored. The compiler allocated a callee-saved register for an address it then discarded. No source transformation can produce this — it's a compiler optimization artifact.

## Statistics

| Metric | Count |
|--------|-------|
| Functions scanned | 500 |
| Prologue mismatches | 56 (11%) |
| d.stream candidates | ~135 |
| Fixable (param live range) | ~40-50% of mismatches |
| Unfixable (dead register) | ~30-40% of mismatches |

## Candidate Units

- `system/flow/*` — FlowCommand, FlowIf, FlowAnimate (FlowAnimate already fixed)
- `system/hamobj/*` — MeterDisplay (unfixable dead reg)
- `system/char/*` — CharBones, CharEyes, CharLipSync, etc.
- `system/rndobj/*` — RndText, RndMat, etc.
- `system/ui/*` — UILabel, UIList, etc.

## Permuter Pattern

Created `scripts/permuter/patterns/parameter_live_range.py` to automate:
1. **bs→d.stream substitution** — finds `bs` identifiers after `LOAD_REVS` and replaces with `d.stream`
2. **BinStreamRev chain merging** — merges consecutive `d >> x; d >> y;` into `d >> x >> y;`

Priority: 0.9 when `has_prologue_mismatch` and `gpr_save_delta < 0`, 0.7 for any Load function with bs/d usage.
