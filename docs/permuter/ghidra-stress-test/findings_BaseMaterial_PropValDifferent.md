# Ghidra Type Seeding Stress Test - BaseMaterial::PropValDifferent

**Date**: 2026-02-13
**Tester**: Claude Opus 4.6
**Goal**: Evaluate whether type-aware Ghidra decompilation improves decomp workflow

---

## Function: BaseMaterial::PropValDifferent

**Symbol**: `?PropValDifferent@BaseMaterial@@IAA_NVSymbol@@PAV1@@Z`
**File**: `src/system/rndobj/BaseMaterial.cpp:87`
**Subsystem**: rndobj
**Size**: 102 instructions (408 bytes)

---

### Baseline (Before Analysis)

| Metric | Value |
|--------|-------|
| **Match %** | 97.7% |
| **Primary Mismatch Type** | Condition register (cr6 vs cr0), symbol relocs |
| **Mismatch Count** | 23 diff_arg, 2 replace |
| **Verdict** | LIKELY_FIXABLE |

**Objdiff Summary**:
```
2 real replaces:
  idx 17: cmplwi cr6, r30, 0x0 vs cmplwi r30, 0x0
  idx 18: bne cr6, 0x9bc vs bne 0x84
6 symbol relocations
17 unexplained diff_arg (no detail available)
LINKER_MERGED: 2 calls to merged_824D1870 (kAssertStr MakeString)
```

---

### Ghidra Analysis (With Type Seeding)

#### Type Information Quality

| Aspect | Rating (1-5) | Notes |
|--------|--------------|-------|
| **Class types visible** | 3 | `BaseMaterial *this` correctly typed |
| **Struct members named** | 2 | `mTexXfm` accessed as `+0x74` offset, not by name |
| **Function signatures** | 2 | Parameters shown but calling convention warning |
| **Cross-references** | 4 | 3 callers identified (UpdatePropertiesFromMetaMat, CreateMetaMaterial, IsEquivalent) |

#### Key Observations

**What was immediately clear from types?**
- `BaseMaterial *this` typing confirmed the class identity
- `in_r5 + 0x74` → `mTexXfm` (confirmed via struct_info lookup)
- Cross-references showed this is called from MetaMaterial comparison code — helps understand purpose
- The `DataNode` copy constructor and comparison operators are visible in the decompilation

**What remained unclear?**
- The `cr6` vs `cr0` condition register choice is a compiler optimization not related to types
- 17 unexplained diff_arg instructions have no breakdown — likely branch target differences from symbol relocation
- Why the compiler chose `cr6` for the null check (`if (!base)`) — may relate to the `&&` short-circuit with `gDefaultMat` read

**Ghidra Decompilation Snippet** (key structure):
```c
if (((in_r5 & 0xffffffff) == 0) && (in_r5 = (ulonglong)DAT_830e15cc, in_r5 == 0)) {
    // MILO_ASSERT(base, 0x133)
}
cVar3 = __8Symbol__QBA_NPBD_Z(&stack0x0000001c, "tex_xfm");
if (cVar3 == '\0') {
    // Property comparison path
} else {
    uVar2 = __9Transform__QBA_NABV0__Z(in_r5 + 0x74, lVar1 + 0x74);
    // ^ base->mTexXfm != this->mTexXfm
}
```

---

### Matching Attempt

**Changes Made**: None attempted.

**Reasoning**: The primary mismatch is the condition register selection (`cr6` vs `cr0`). This is a compiler-internal decision about which CR field to use for the comparison in the `!base` / `gDefaultMat` compound condition. The `&&` short-circuit evaluation with a global variable read may cause the compiler to allocate a non-default CR field. This is not controllable from source code.

The 17 unexplained diff_arg are likely cascading effects from the CR field difference and symbol relocations (LINKER_MERGED MakeString calls).

**Status**: Blocked (condition register allocation + LINKER_MERGED)

---

### Learnings

**Type Seeding Helpfulness**: 2/5

**Specific Value Add**:
- ✅ Cross-references revealed the function's role in material comparison pipeline
- ✅ `mTexXfm` at offset 0x74 confirmed via struct lookup (but could be found without Ghidra)
- ✅ DataNode comparison operators visible in typed decompilation

**Gaps Identified**:
- ❌ Condition register allocation is invisible to type information
- ❌ LINKER_MERGED pattern (kAssertStr MakeString) unresolvable regardless of types
- ❌ Symbol string comparisons (`s == "tex_xfm"`) shown as raw function calls, not readable

**Pattern Recognized**:
- `cr6` vs `cr0` mismatch appears in compound conditions with short-circuit evaluation
- This pattern may be common in functions with `if (!ptr) ptr = default; MILO_ASSERT(ptr, ...)`

---

### Verdict

**Should pursue 100% match?** No

**Reasoning**: The cr6 vs cr0 condition register difference is a compiler-internal optimization choice not controllable from source. The 2 LINKER_MERGED calls add to the unfixable portion. Source logic at line 87-104 is correct.

**Recommended Next Steps**:
- Mark as AT_LIMIT
- The cr6 pattern should be documented as a known unfixable pattern
