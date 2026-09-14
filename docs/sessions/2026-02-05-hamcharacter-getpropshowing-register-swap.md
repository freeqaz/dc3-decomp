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

## 2026-09-14 addendum (wave 7, lane w7-k): two new refutations, and the caller that pays for them

`HamCharacter::GetPropShowing` is not just a 2324-byte-free 95.2% row. It is
inlined **four times** into `HamCharacter::SyncProperty`
(`?SyncProperty@HamCharacter@@UAA_NAAVDataNode@@PAVDataArray@@HW4PropOp@@@Z`,
97.935 canonical, 2324 B) at the four `SYNC_PROP_SET(prop_N_showing, ...)`
lines, and the *same single instruction* accounts for the whole residual there.

### What the target does at each of the four sites

```
lwz     r10, 0xN(r11)
cmplwi  cr6, r10, 0x0
beq     cr6, 0x334c        ; shared "false" tail
clrrwi  r3, r10, 0         ; re-materialise the pointer into r3
b       0x3834             ; shared Showing() tail
```

Our build loads straight into `r3` and then **cross-jumps** the null test, so
all four sites share one physical copy. That is 12 instructions / 48 bytes of
`delete` rows ([386-388], [440-443], [495-497], and the fourth site), plus
`[340] delete clrlwi r11, r11, 24` from the neighbouring `crew_card_showing`
getter. `name_check` is otherwise clean: every callee name and every
function-local-static scope ordinal is already right.

So it is one root cause and two functions: produce the `clrrwi` in
`GetPropShowing` and SyncProperty's 12 deletes are expected to resolve with it.

### Refutation 13 -- repeated subscript (95.2% -> 92.4%, REGRESSION)

Motivated by line 123 of the same file, which *does* use the repeated-read
spelling (`mCrewCardMesh && mCrewCardMesh->Showing()`):

```cpp
return mShowableProps.size() > prop && mShowableProps[prop]
    && mShowableProps[prop]->Showing();
```

New row: `[11] replace: cmplwi cr6, r10, 0x0  vs  cmpwi cr6, r11, 0x0`.
**A bare subscript expression in boolean context emits a SIGNED compare**;
only the `(d = ...)` assignment form yields the unsigned `cmplwi` the target
has. This is the same lever wave 7 recorded as "an `ObjPtr<T>` test is
`cmpwi`, a raw `T*` test is `cmplwi`", seen from the other side: the
assignment materialises a raw `RndDrawable *`, the bare subscript is tested
as the `ObjPtr` expression it came from.

### Refutation 14 -- hybrid: assignment for the test, subscript for the call (95.2% -> 92.4%)

```cpp
RndDrawable *d;
auto _tmp0 = mShowableProps.size();
return _tmp0 > prop && (d = mShowableProps[prop])
    && mShowableProps[prop]->Showing();
```

Identical 3-row set to refutation 13 (`[10] lwz [reg:r10->r11]`,
`[11] replace cmplwi/cmpwi`, `[13] delete clrrwi r11, r10, 0`). Keeping the
assignment form for the *test* does not rescue the signedness once a second
subscript exists in the expression -- MSVC re-reads through the `ObjPtr` and
the signed compare comes back. Both variants reverted.

### Standing

The committed body stays the best known spelling:

```cpp
bool HamCharacter::GetPropShowing(int prop) {
    RndDrawable *d;
    auto _tmp0 = mShowableProps.size();
    return _tmp0 > prop && (d = mShowableProps[prop]) && d->Showing();
}
```

14 refuted variants. The `clrrwi` is a pointer re-materialisation our side
never needs because our value is already in `r3`; nothing in source reach has
produced a spelling that both keeps the unsigned test and forces the extra
copy. Anyone attacking `SyncProperty` should attack `GetPropShowing` -- and
should know that the two obvious remaining spellings are now refuted too.
