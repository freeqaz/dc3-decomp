# GetBadgeInfo Investigation - NOT AT_LIMIT (Needs Investigation)

**Date**: 2026-02-04
**Function**: `GetBadgeInfo` (free function)
**File**: `src/lazer/net_ham/ChallengeSystemJobs.cpp:230-268`
**Symbol**: `?GetBadgeInfo@@YAXAAVJsonConverter@@PBVJsonObject@@AAV?$map@VString@@VChallengeBadgeInfo@@U?$less@VString@@@stlpmtx_std@@V?$StlNodeAlloc@U?$pair@$$CBVString@@VChallengeBadgeInfo@@@stlpmtx_std@@@4@@stlpmtx_std@@@Z`

## Summary

Earlier notes described this function as AT_LIMIT at 98.6%. That is **not** accurate. As of the current report (2026-02-04), `objdiff-cli report function` shows **99.545%** and `objdiff-cli diff --analyze --verdict` classifies it as **NEEDS_INVESTIGATION**. This doc is updated to reflect that, and to outline concrete mismatch causes to investigate.

## Final Code (Best Match)

```cpp
} else {
    ChallengeBadgeInfo value = { 0 };
    value.mMedalCounts[kBadgeGold] = dlcGold + hmxGold;
    value.mMedalCounts[kBadgeSilver] = dlcSilver + hmxSilver;
    value.mMedalCounts[kBadgeBronze] = dlcBronze + hmxBronze;
    badgeInfos[gamerTag] = value;
}
```

## Approaches Tried

| Approach | Result | Notes |
|----------|--------|-------|
| `ChallengeBadgeInfo value = { 0 }; ... badgeInfos[key] = value;` | **99.545%** | Best result - creates local, copies to map |
| `ChallengeBadgeInfo value = { }; ...` | 98.3% | Empty braces generate `std` instead of `stw` |
| `memset(&value, 0, sizeof(value));` | 98.3% | Compiler uses `std` (64-bit) instead of 3x `stw` (32-bit) |
| Reversed member assignment order | 98.4% | Slightly worse register swap pattern |
| `ChallengeBadgeInfo value = {0,0,0};` | 95.3% | Compiler used separate zero constants |
| Default constructor `ChallengeBadgeInfo()` | 95.3% | Same as explicit init |
| Direct reference: `auto& ref = badgeInfos[key]; ref.mMedalCounts[...] = ...;` | 94.8% | Wrong pattern - no local zeroing |
| Three separate `operator[]` calls | 92.9% | Generated 3 map lookups instead of 1 |
| `insert(make_pair(...))` | 86.2% | Generated `insert_unique` call, completely different |

## Remaining Differences (Known So Far)

### 1. LINKER_MERGED Calls (9 instructions)
ICF (Identical COMDAT Folding) merged identical template instantiations:
- `merged_82610090` (6 calls) - `MakeString<int>`
- `merged_StringCtor` (1 call) - `String::String(const char*)`
- `merged_825A3DA0` (1 call) - `map::_M_find`
- `merged_823314D8` (1 call) - `MakeString<const char*>`

These are cosmetic - the actual function calls are correct, just merged to shared addresses.

### 2. Missing `stw` Instruction (1 instruction)
Target generates 3 zero stores:
```asm
stw r11, 0x0(r10)   ; mMedalCounts[0] = 0
stw r11, 0x4(r10)   ; mMedalCounts[1] = 0
stw r11, 0x8(r10)   ; mMedalCounts[2] = 0  <-- our compiler omits this
```

Our compiler only generates 2 stores, optimizing away the third as "dead code" since all three members are immediately overwritten.

### 3. Stack Offset Difference (4 bytes)
- Target: `addi r10, r31, 0x80`
- Ours: `addi r10, r31, 0x84`

Different register allocation/stack layout, but functionally equivalent.

### 4. Prolog/Epilog Save/Restore Mismatch (2 instructions)
- Target uses `__savegprlr` / `__restgprlr`
- Base uses `__savegprlr_14` / `__restgprlr_14`

These are usually harmless but indicate a register-save pattern difference that may be fixable by source changes.

## Technical Analysis

The target binary appears to have been compiled with less aggressive dead store elimination. The pattern:

1. Zero-initialize all 3 array elements
2. Immediately overwrite all 3 elements with computed values

Our compiler recognizes that step 1 is redundant and eliminates the third zero store. This is a valid optimization that produces correct code, just different machine code.

The save/restore mismatch suggests the compiler is selecting a slightly different prolog/epilog variant (e.g., number of saved registers). This can sometimes be nudged by changing local variable lifetimes or structure.

## Git History

Checked 5 commits touching this file:
- Original implementation had `ChallengeBadgeInfo value;` (uninitialized)
- Later changed to `= { 0 }` for the current best match

## RB3 Reference

No matching `GetBadgeInfo` function found in RB3 decomp - this is DC3-specific network code.

## Verification Command

```bash
./bin/objdiff-cli diff '?GetBadgeInfo@@YAXAAVJsonConverter@@PBVJsonObject@@AAV?$map@VString@@VChallengeBadgeInfo@@U?$less@VString@@@stlpmtx_std@@V?$StlNodeAlloc@U?$pair@$$CBVString@@VChallengeBadgeInfo@@@stlpmtx_std@@@4@@stlpmtx_std@@@Z'
```

## Conclusion

This function is **not** AT_LIMIT. As of 2026-02-04, `objdiff-cli diff --analyze --verdict` classifies it as **NEEDS_INVESTIGATION** at **99.545%**. Known differences include:
- 9 LINKER_MERGED calls (cosmetic)
- 1 missing `stw` from dead-store elimination
- 4-byte stack layout variance
- Save/restore helper mismatch (`__savegprlr` vs `__savegprlr_14`)

Recommendation: treat this as still open. Try source tweaks that affect stack layout and register-save patterns before calling it AT_LIMIT.
