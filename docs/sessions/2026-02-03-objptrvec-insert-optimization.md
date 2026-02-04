# ObjPtrVec::insert Optimization Session (2026-02-03)

## Summary

Improved `ObjPtrVec::insert` from **82.6% → 96.2%** match by discovering the original code's return value pattern and fixing the branch direction.

## Key Discovery: Return Value Pattern

The original code does NOT compute `begin() + idx` for the return value. It always returns `it.it` (the original iterator parameter) in both paths:

```cpp
// BEFORE (82.6%)
if (obj != 0 || mListMode != kObjListNoNull) {
    int idx = it.it ? (it.it - mNodes.begin()) : 0;
    Node newNode(this);
    mNodes.insert(mNodes.begin() + idx, 1, newNode);
    iterator result = begin() + idx;
    Set(result, obj);
    return result;
}
return iterator(const_cast<typename std::vector<Node>::iterator>(it.it));

// AFTER (96.2%)
if (obj != 0 || mListMode != kObjListNoNull) {
    int idx = it.it ? (it.it - mNodes.begin()) : 0;
    Node newNode(this);
    mNodes.insert(mNodes.begin() + idx, 1, newNode);
    Set(begin() + idx, obj);
}
return iterator(const_cast<typename std::vector<Node>::iterator>(it.it));
```

Evidence from assembly: target register r26 (holding `it.it`) is stored to the sret pointer at the end, and r26 is never updated in the body path. The `begin() + idx` is computed only into r4 for the `Set()` call argument and is never saved.

This works because after `vector::insert` at position idx without reallocation, the original pointer `it.it` still points to the newly inserted element (elements shift right). With reallocation, the pointer is stale — this may be a known acceptable risk in the original codebase, or reallocation may not occur in practice.

## Branch Direction Fix (82.6% → 88.6%)

Intermediate step: using a single-return pattern with pre-initialized result variable fixed the `beq` vs `bne` branch direction at index 12:

```cpp
// Single return pattern
iterator result(const_cast<...>(it.it));
if (obj != 0 || mListMode != kObjListNoNull) {
    // ... body ...
    result = begin() + idx;
    Set(result, obj);
}
return result;
```

However, this added extra instructions because the sret pointer was written to eagerly (during `iterator result(...)` initialization) and again during `result = begin() + idx`.

## Final result eliminates both issues

Removing the separate `result` variable and always returning `it.it` means:
- Single return path → compiler generates `beq` to skip body (matches target)
- No double-write to sret → no extra instructions
- Same code size (228 bytes)

## Remaining Mismatches at 96.2%

| Index | Type | Description | Fixable? |
|-------|------|-------------|----------|
| 21, 24 | diff_arg | vtable relocation hi16/lo16 | Cosmetic |
| 29→33 | scheduling | `add r4, r28, r11` moved 4 positions | Maybe |
| 44 | diff_arg | `merged_82849A90` vs actual `Set` call | No (ICF) |

The merged Set call is unfixable (ICF). The instruction scheduling and vtable relocations are minor. This function is effectively at its limit.

## Regression Check

| Function | Before | After | Status |
|----------|--------|-------|--------|
| push_back (Spotlight) | 100% | 100% | OK |
| operator= (Spotlight) | 82.5% | 82.5% | OK |
| SyncKeyframeTargets | 30.8% | 30.8% | OK (separate issue) |

## Experiments Tried

1. **Nested if** (`if (obj==0) { if (mode==NoNull) return; }`) — compiler optimized back to same `||` pattern, no change
2. **`&&` with early return** — same branch direction issue as `||`, 82.6%
3. **Single return with pre-initialized result** — fixed branch direction (88.6%) but added extra sret writes
4. **Raw vector iterator variable** — 76.8%, worse due to different codegen
5. **Extract `pos = begin() + idx` before Node ctor** — 84.7%, changed instruction scheduling badly
6. **Always return `it.it`, compute `begin()+idx` only for Set arg** — 96.2%, matches target pattern

## Lesson Learned

When the assembly shows a register holding a parameter value is never updated in the body of an if-block but is used at the shared return, the original code returns the parameter directly rather than a computed value. Read the register flow carefully — if rN = param at entry and `stw rN, 0(sret)` at the end with no intervening write to rN, the function returns the param unchanged.

## Patterns Documented

Both patterns from this session have been added to the pattern reference:

- **Single Return for Branch Direction** → [fixable-control-flow.md](../decomp/patterns/fixable-control-flow.md#single-return-for-branch-direction) (beq vs bne fix, +6%)
- **sret Return Value Tracing** → [fixable-declarations.md](../decomp/patterns/fixable-declarations.md#sret-return-value-tracing) (return parameter directly, +7.6%)

## Next Steps

Remaining work candidates in the LightPreset unit:

| Function | Current | Verdict | Notes |
|----------|---------|---------|-------|
| `ObjPtrVec<Spotlight>::operator=` | 82.5% | MAYBE_FIXABLE | Register swap r24↔r26 + linker merged |
| `FillSpotPresetData` | 99.6% | NEEDS_INVESTIGATION | Linker merged + 5 unattributed mismatches |
| `ObjPtrVec<Spotlight>::insert` | 96.2% | AT_LIMIT | ICF merged Set call, minor scheduling |
