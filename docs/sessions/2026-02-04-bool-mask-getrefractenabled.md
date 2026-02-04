# Session: Bool Mask Pattern - GetRefractEnabled

**Date**: 2026-02-04
**Function**: `RndMat::GetRefractEnabled`
**Symbol**: `?GetRefractEnabled@RndMat@@QAA_N_N@Z`
**Result**: 96.6% (at_limit)

## Problem

The function had a bool mask pattern where the target uses r11 as an intermediate register before masking to r3:

```
Target:                          Ours:
li r11, 0x1                      li r3, 0x1
...                              ...
li r11, 0x0                      li r3, 0x0
clrlwi r3, r11, 24               (missing)
```

The pattern doc says "delete" direction (target has clrlwi, we don't) is the "fixable" direction, but this case proved unfixable.

## m2c Decompilation

The m2c output showed a goto pattern with explicit variable:
```c
u8 var_r11;
if (...conditions...) {
    var_r11 = 1;
} else {
    goto block_8;
}
block_8:
    var_r11 = 0;
return var_r11;
```

## Approaches Tried

| Approach | Result | Notes |
|----------|--------|-------|
| Explicit `!= 0` / `!= NULL` comparisons | 96.6% | No change |
| Local `bool` variable with goto | 96.6% | Compiler optimizes away intermediate |
| Local `u8` variable with goto | 94.1% | Generated clrlwi but added subic/subfe conversion |
| Local `char` variable with goto | 92.6% | Generated extsb (sign-extend) instead of clrlwi |
| `(bool)` cast on return | 96.6% | No effect |
| Ternary `? true : false` | 96.6% | No effect |
| Direct condition return | 93.9% | Changed control flow structure |
| Restructured early returns | 82.7% | Completely different codegen |
| `.Ptr()` explicit member access | 89.6% | Changed comparison instruction |
| `(bool)1` / `(bool)0` literals | 96.6% | No effect |

## Key Insight

The **u8 + goto** approach was closest (94.1%) and successfully generated the `clrlwi` instruction:

```cpp
bool RndMat::GetRefractEnabled(bool b) {
    u8 ret;
    if (mRefractEnabled == 1 && mRefractStrength > 0.0f) {
        RndTex *tex = mRefractNormalMap ? mRefractNormalMap : mNormalMap;
        if (tex && (b || TheRnd.GetCurrentFrameTex(false))) {
            ret = 1;
        } else {
            goto fail;
        }
    } else {
    fail:
        ret = 0;
    }
    return ret;
}
```

However, the implicit u8→bool conversion on return added extra instructions:
```
clrlwi r11, r11, 24    (we generate this - good!)
subic r10, r11, 0x1    (extra - bad)
subfe r3, r10, r11     (extra - bad)
```

The target does `clrlwi r3, r11, 24` (directly to r3), we do `clrlwi r11, r11, 24` then convert.

## Why It's Unfixable

The core issue is **register allocation**, not source-level semantics:

1. **Target compiler**: Allocated r11 for the bool result, then used clrlwi to mask+move to r3 in one instruction
2. **Our compiler**: Allocates r3 directly for the bool result, skipping the intermediate

This is a compiler optimization decision that cannot be influenced by source code changes. The target compiler's choice to use r11 as an intermediate is arbitrary from a correctness standpoint.

## RB3 Reference

RB3's implementation differs - it doesn't have the mNormalMap fallback:
```cpp
bool RndMat::GetRefractEnabled(bool b) {
    bool ret = false;
    if (mRefractEnabled == 1 && mRefractStrength > 0.0f && mRefractNormalMap) {
        if (b || TheRnd->GetCurrentFrameTex(false)) {
            ret = true;
        }
    }
    return ret;
}
```

This suggests DC3 added the fallback logic, potentially with different compiler settings.

## Final Implementation

Kept the clean, readable version at 96.6%:
```cpp
bool RndMat::GetRefractEnabled(bool b) {
    if (mRefractEnabled == 1 && mRefractStrength > 0.0f) {
        RndTex *tex = mRefractNormalMap ? mRefractNormalMap : mNormalMap;
        if (tex && (b || TheRnd.GetCurrentFrameTex(false))) {
            return true;
        }
    }
    return false;
}
```

## Lessons Learned

1. **Bool mask "delete" direction isn't always fixable** - despite the pattern doc suggesting it should be, register allocation differences can make it impossible

2. **u8 variable approach generates clrlwi** - but may add conversion instructions when returning as bool

3. **The gap is small (3.4%)** - not worth uglifying code with gotos for marginal improvement

4. **Check RB3 for differences** - implementation changes between games may explain compiler behavior differences
