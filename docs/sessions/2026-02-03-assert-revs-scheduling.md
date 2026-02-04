# ASSERT_REVS Scheduling Mismatch — 98.6% Cap on ~146 Load Functions

## Summary

Every `::Load` function that uses the `ASSERT_REVS` macro is capped at 98.6% due to a 2-instruction scheduling difference in the second `MILO_FAIL` call. The compiler emits 3 independent `addi` instructions in a different order than the target binary. After 17 attempts across 3 rounds, this is confirmed as unfixable compiler instruction scheduling.

## The Code

The `ASSERT_REVS` macro (from `src/system/obj/Object.h:862`):
```cpp
#define ASSERT_REVS(rev1, rev2)                                                    \
    static const unsigned short gRevs[4] = { rev1, 0, rev2, 0 };                  \
    if (d.rev > rev1) {                                                            \
        MILO_FAIL(                                                                 \
            "%s can't load new %s version %d > %d",                                \
            PathName(this), ClassName(), d.rev, gRevs[0]                           \
        );                                                                         \
    }                                                                              \
    if (d.altRev > rev2) {                                                         \
        MILO_FAIL(                                                                 \
            "%s can't load new %s alt version %d > %d",                            \
            PathName(this), ClassName(), d.altRev, gRevs[2]                        \
        );                                                                         \
    }
```

`MILO_FAIL(...)` expands to `TheDebugFailer << MakeString(...)`, which calls the template `MakeString<const char*, Symbol, int, unsigned short>`. The second MILO_FAIL sets up arguments for this call — specifically three `addi` instructions that load reference addresses into r4, r6, and r7.

## The Assembly Diff

The mismatch is at instructions 55-57 in the second MILO_FAIL block. All other differences are just `diff_arg` (symbol name/address differences that are equivalent):

```
Index | Target (original binary)                  | Ours (compiled)
------|-------------------------------------------|----------------------------------
  55  | addi r7, r27, 0x4     ← &gRevs[2]        | addi r4, r1, 0x54   ← &PathName result
  56  | addi r4, r1, 0x54     ← &PathName result  | addi r6, r1, 0x64   ← &d.altRev
  57  | addi r6, r1, 0x64     ← &d.altRev         | addi r7, r27, 0x4   ← &gRevs[2]
```

The target binary schedules `r7` (the static array reference `&gRevs[2]`) first, then `r4` and `r6` (stack-relative addresses). Our compiler does the reverse: `r4`, `r6`, then `r7`. All three instructions are independent — they write to different registers with no data dependencies between them. The order is purely a compiler scheduling decision.

Notably, the **first** MILO_FAIL (for `d.rev > rev1`) does NOT have this problem — only the second one (for `d.altRev > rev2`).

## Full objdiff Instruction Diff

Test function: `Fader::Load` (`?Load@Fader@@UAAXAAVBinStream@@@Z`)

| Index | Target | Base | Match |
|------:|--------|------|-------|
| 9 | `lis r11, TheDebug` | `lis r11, TheDebug` | diff_arg |
| 10 | `lis r10, lbl_820B78B0` | `lis r10, gRevs` | diff_arg |
| 12 | `addi r28, r11, TheDebug` | `addi r28, r11, TheDebug` | diff_arg |
| 13 | `addi r27, r10, lbl_820B78B0` | `addi r27, r10, gRevs` | diff_arg |
| 30 | `lis r11, "%s can't load..."` | `lis r11, "%s can't load..."` | diff_arg |
| 32 | `addi r3, r11, "%s can't load..."` | `addi r3, r11, "%s can't load..."` | diff_arg |
| 33 | `mr r7, r27` | `mr r7, r27` | diff_arg |
| 38 | `mr r3, r28` | `mr r3, r28` | diff_arg |
| 52 | `lis r11, "%s can't load...alt..."` | `lis r11, "%s can't load...alt..."` | diff_arg |
| 54 | `addi r3, r11, "%s can't load...alt..."` | `addi r3, r11, "%s can't load...alt..."` | diff_arg |
| **55** | **`addi r7, r27, 0x4`** | **`addi r4, r1, 0x54`** | **replace** |
| 56 | `addi r4, r1, 0x54` | `addi r6, r1, 0x64` | diff_arg |
| **57** | **`addi r6, r1, 0x64`** | **`addi r7, r27, 0x4`** | **replace** |
| 60 | `mr r3, r28` | `mr r3, r28` | diff_arg |

## Attempts (17 total, all failed)

| # | Round | Approach | Result |
|---|-------|----------|--------|
| 1 | 1 | `#pragma optimize("t", on)` | 74% — destructive, changed overall optimization |
| 2 | 1 | `#pragma optimize("y", off)` | 98.6% — no effect |
| 3 | 1 | `#pragma optimize("g", off)` | 28% — destructive |
| 4 | 1 | Per-file `/Ot` compiler flag | 74% — destructive |
| 5 | 1 | `*(const unsigned short *)((const char *)gRevs + 4)` pointer arithmetic | 98.6% — no effect |
| 6 | 1 | `const unsigned short *_gAltRevP = &gRevs[2]` local pointer | 98.6% — optimized away |
| 7 | 1 | `const unsigned short * volatile _gAltRevP` | 96% — added extra stw/lwz |
| 8 | 2 | `const unsigned short &_altMax` reference before `if` | 98.6% — optimized away |
| 9 | 2 | `__forceinline` identity wrapper function | 98.6% — optimized away |
| 10 | 2 | Non-static `gRevs` array | 92.9% — added init overhead |
| 11 | 2 | Condition `d.altRev > gRevs[2]` instead of `> rev2` | 98.6% — constant-folded |
| 12 | 2 | `__pragma(auto_inline(off))` | Build error |
| 13 | 2 | `*(gRevs + 2)` pointer form | 98.6% — identical codegen |
| 14 | 2 | `(int)d.altRev` cast | 98.6% — already int |
| 15 | 2 | `unsigned short _altRevMax = gRevs[2]` local copy | 92.4% — extra bytes |
| 16 | 3 | Explicit template instantiation of MakeString in .cpp | 98.6% — no effect on call sites |
| 17 | 3 | `extern template` declaration in MakeString.h | 93.4% — changed to real call, worse |

## Why It's Unfixable (So Far)

The three `addi` instructions set up `const T&` reference addresses for MakeString template parameters. They target different registers with no data dependencies — the compiler is free to emit them in any order. The target binary's compiler chose `r7, r4, r6`; ours chooses `r4, r6, r7`. Every attempt to influence this ordering either:

- Has no effect (compiler optimizes away the indirection)
- Makes things worse (adds extra instructions or changes the call mechanism)
- Is destructive (changes optimization level globally)

The MakeString call target must remain `bl MakeString<const char*, Symbol, int, unsigned short>` — any approach that changes the callee (wrapper functions, FormatString directly) would produce a completely different `bl` target.

## Impact

~146 `::Load` functions use `ASSERT_REVS` and are affected. Most have `rev2 = 0`, meaning the second `if` branch is dead code (never taken), but the compiler still generates it with this scheduling difference.

## Open Question

Is there any MSVC-specific technique (inline assembly hint, `__assume`, memory barrier, or pragma) that could influence the instruction scheduler's ordering of independent `addi` instructions at a call site? The compiler is MSVC 16.00 (Xbox 360 / PowerPC target).
