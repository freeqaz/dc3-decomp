# Session: HamCharacter::GetPropShowing Register Swap Investigation

**Date**: 2026-02-05
**Function**: `HamCharacter::GetPropShowing`
**Symbol**: `?GetPropShowing@HamCharacter@@IAA_NH@Z`
**Final Status**: `at_limit` @ 94.8%
**Pattern**: REGISTER_SWAP (r10 ↔ r11)

## Problem Summary

After fixing an earlier signed/unsigned comparison issue, the function reached 94.8% with a persistent register allocation difference:

| Target | Our Code |
|--------|----------|
| `lwz r10, 0xc, r11` | `lwz r11, 0xc, r11` |
| `cmplwi cr6, r10, 0x0` | `cmplwi cr6, r11, 0x0` |
| `clrrwi r11, r10, 0` | (missing) |

The target loads the pointer to **r10**, compares it, then copies r10→r11 via `clrrwi`. Our code loads directly to **r11** and reuses it, saving 4 bytes but not matching.

## Final Implementation (94.8%)

```cpp
bool HamCharacter::GetPropShowing(int prop) {
    RndDrawable *d;
    return mShowableProps.size() > prop && (d = mShowableProps[prop]) && d->Showing();
}
```

## Approaches Tried

### Variable Declaration Reordering

| Approach | Result | Notes |
|----------|--------|-------|
| `int sz; RndDrawable *d;` with inline size assignment | 94.8% | No change |
| Extra temp variable `RndDrawable *tmp = 0; (void)tmp;` | 94.8% | No change |
| Two pointer variables `d` and `e` with intermediate copy | 94.8% | No change |
| Reversed declaration order | 94.8% | No change (tried in prior session) |

### Control Flow Changes

| Approach | Result | Notes |
|----------|--------|-------|
| Explicit if-statement for size check only | 81.9% | Worse - control flow diff (ble vs bgt) |
| Explicit if-statement with stored result | 50.2% | Much worse - BOOL_MASK detected |
| Ternary expression | 85.0% | Worse (tried in prior session) |

### Type/Qualifier Changes

| Approach | Result | Notes |
|----------|--------|-------|
| `const RndDrawable *d` | 94.8% | No change |
| `RndDrawable *volatile d` | 87.1% | Worse - forced stack spill (stw/lwz) |
| `ObjPtr<RndDrawable> d` | 94.8% | Build failed (needs owner) |
| `Hmx::Object *d` with cast | 94.8% | Build failed |
| `RndDrawable *const &p = d` | 94.8% | Build failed |

### Expression Variations

| Approach | Result | Notes |
|----------|--------|-------|
| `(d = ...) != 0` explicit null check | 94.8% | No change |
| `!!(d = ...)` double negation | 94.8% | No change |
| `d = 0` initialization | 94.8% | No change (tried in prior session) |

## Why This Pattern is Unfixable

The `clrrwi r11, r10, 0` instruction is a no-op copy (clear bits, but clearing 0 bits). The compiler chose to:
1. Load the ObjPtr result to r10
2. Compare r10 against zero
3. Copy r10 to r11 before the method call

Our compiler instead:
1. Loads directly to r11
2. Compares r11 against zero
3. Uses r11 directly for the method call

This is purely an internal register allocation decision. The compiler's graph coloring algorithm made different choices, and there's no source-level construct that can influence this specific allocation.

Per `docs/decomp/patterns/unfixable-compiler.md`:
> **REGISTER_SWAP Prevalence:** 607 functions tagged (most common pattern)
> **Typical Gap:** 1-3% (avg 92.3%)
> **Success Rate:** 30% for variable reordering attempts

With 12+ attempts and no improvement, this falls within the expected 70% failure rate.

## Future Paths to Fix Register Allocation Issues

### 1. Compiler Flag Investigation

The original build may have used specific optimization flags that affect register allocation:
- `-fno-schedule-insns` / `-fschedule-insns`
- `-frename-registers` / `-fno-rename-registers`
- `-fira-algorithm=` (priority vs CB)

**Action**: Compare our build flags against known MSVC Xbox 360 defaults.

### 2. Inline Assembly Hints

For critical functions, inline assembly could force specific register usage:
```cpp
register RndDrawable *d __asm__("r10");
```

**Caveat**: This is non-portable and may cause issues with other functions.

### 3. Permuter Tool

The decomp.me permuter can automatically try thousands of source variations to find matches. For register swap issues:
- Configure permuter with the function
- Let it try variable reorderings, type changes, expression rewrites
- May find obscure patterns humans wouldn't try

**Action**: Set up permuter for functions stuck at 90-98%.

### 4. Understanding the ObjPtrVec::operator[] Inlining

The issue stems from how `mShowableProps[prop]` inlines. The `ObjPtrVec::operator[]` returns:
```cpp
T1 *operator[](int idx) { return mNodes[idx].Obj(); }
```

Which calls `Node::Obj()`:
```cpp
T1 *Obj() const { return mObject; }
```

The chain of inlined loads may affect register selection. Investigating whether a wrapper function or explicit iterator usage changes allocation could help.

### 5. Struct Padding/Alignment

Sometimes register allocation is affected by struct layout. If `DrawPtrVec` or its `Node` type had different padding, the compiler might allocate differently.

**Action**: Verify struct sizes match DWARF/RB2 info exactly.

### 6. Function Attribute Experiments

Try function attributes that might affect codegen:
- `__declspec(noinline)` on helper functions
- `#pragma optimize("", off/on)` around the function
- `__forceinline` on the operator[]

### 7. Accept and Document

For functions at 94%+ with only register differences:
- The logic is correct
- The code is readable
- Mark as `at_limit` and move on

The 5% loss from register allocation is acceptable technical debt.

## Lessons Learned

1. **Inline assignment is robust**: The `(d = expr)` pattern consistently produces good codegen
2. **Explicit if-statements can backfire**: Breaking up short-circuit logic often triggers BOOL_MASK
3. **volatile is counterproductive**: Forces stack spills, making things worse
4. **Type changes rarely help**: const, different pointer types don't affect register allocation
5. **Know when to stop**: After 10+ attempts with no improvement, accept the limit

## References

- `docs/decomp/patterns/unfixable-compiler.md#register-allocation`
- `docs/decomp/patterns/fixable-bool-mask.md`
- Prior session: `docs/sessions/2026-02-04-characterTest-ctor-regression.md`
