# DataReadString Bug Fix - 2026-03-05

## Summary

Fixed a critical bug in `DataReadString(const char *c)` in `src/system/obj/DataFile.cpp` that was passing the address of the pointer variable instead of the pointer value itself. This caused the function to read from a 4-byte stack slot rather than the actual string data.

**Result**: 94.3% → 100% match (4 mismatches resolved)

## The Problem

The function was initially at 94.3% match with 4 instruction mismatches:

```cpp
DataArray *DataReadString(const char *c) {
    BufStream stream(&c, strlen(c), true);
    return DataReadStream(&stream);
}
```

### Objdiff Mismatches

1. **[6] replace**: `mr r4, r3` (target) vs `stw r3, 0xb4(r31)` (ours)
   - Target kept strlen result in register; we spilled to stack
2. **[12] diff_arg**: `subf` with register r4 (target) vs r3 (ours)
3. **[15] diff_arg**: `addi` with register r3 (target) vs r4 (ours), +100 offset shift
4. **[17] insert**: extra `addi r3, r31, 0x50` in our code

### Root Cause

The bug: `&c` passes the **address of the pointer variable** (a 4-byte stack slot) to `BufStream`, not the **string data pointer**. The `BufStream` constructor expects `void *buffer` pointing to the actual data, not a pointer-to-pointer.

This forced the compiler to:
1. Store parameter `c` to a specific stack slot (to have an address for `&c`)
2. Spill the `strlen(c)` result to stack (register needed for address computation)
3. Generate extra `addi` to compute stack addresses
4. Reload values from stack in subsequent instructions

Result: register spill + stack offset differences that looked like a typical compiler regswap, but was actually a semantic bug.

## The Fix

```cpp
DataArray *DataReadString(const char *c) {
    BufStream stream((void *)c, strlen(c), true);
    return DataReadStream(&stream);
}
```

Change `&c` to `(void *)c` to pass the actual string data pointer directly.

This lets the compiler:
1. Keep `c` in a register (no need to create a stack address)
2. Pass `strlen(c)` result directly via `mr r4, r3` (register move)
3. No spilled values or extra address computations
4. No stack offset differences

**Result**: 100% match (all 4 mismatches resolved)

## Why It Looked Like a Regswap

The initial diagnosis seemed like a typical register allocation issue:
- Stack spill differences
- Register name swaps (`r4` vs `r3`)
- Extra instruction (`addi`)
- Offset shift (+100 = 0x64 hex)

These are usually unfixable regswaps or stack layout artifacts of the compiler's internal heuristics. However, the key tell was:

**`mr` vs `stw` is a different operation class**, not just a register name change. A regswap would use the same operation with different registers. When the target uses `mr` (register-to-register) and we generate `stw` (register-to-memory), it indicates a **semantic difference in value usage**, not just scheduling.

## Pattern Recognition

### "Pointer-to-Pointer vs Direct Pointer"

When decompiling functions that pass data to stream/buffer constructors, watch for this pattern:

**Red flags**:
- Function passes `&variable` to a stream constructor where `variable` is already a pointer
- Objdiff shows `stw` (store) where target has `mr` (move)
- Stack offset differences in subsequent instructions
- Extra `addi` instructions for address computation

**Detection heuristic**:
- `BufStream stream(&param, ...)` where `param` is `void*`, `char*`, etc.
- `MemStream stream(&param, ...)` with similar pointer parameter
- The `&` operator on a pointer parameter creates pointer-to-pointer (usually wrong)
- Compare with RB3 reference: always `(void*)ptr` not `&ptr` for buffer constructors

### Broader Lesson

Not every register spill + stack offset mismatch is an unfixable compiler artifact. The difference between `mr` and `stw` is fundamental:

- **Regswap**: `mr r4, r3` vs `mr r3, r4` — same operation, register names differ
- **Semantic bug**: `mr r4, r3` vs `stw r3, 0xb4(r31)` — fundamentally different operations

When you see the second pattern in a stream/buffer context, check whether the code is passing `&pointer` instead of `pointer`.

## Files Modified

- `src/system/obj/DataFile.cpp` — Changed `&c` to `(void *)c` in `DataReadString`

## References

- RB3 reference: confirms `(void*)ptr` pattern for all BufStream/MemStream constructors
- Related functions: any function using `BufStream(buffer, size, ...)` or similar constructors
