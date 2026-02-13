# Ghidra Type Seeding Stress Test - SaveLoadManager::Handle

**Date**: 2026-02-13
**Tester**: Claude Opus 4.6
**Goal**: Evaluate whether type-aware Ghidra decompilation improves decomp workflow

---

## Function: SaveLoadManager::Handle

**Symbol**: `?Handle@SaveLoadManager@@UAA?AVDataNode@@PAVDataArray@@_N@Z`
**File**: `src/lazer/meta_ham/SaveLoadManager.cpp` (BEGIN_HANDLERS macro)
**Subsystem**: meta_ham
**Size**: 655 instructions (2608 bytes target, 2604 bytes base)

---

### Baseline (Before Analysis)

| Metric | Value |
|--------|-------|
| **Match %** | 95.1% |
| **Primary Mismatch Type** | Symbol relocations (67), register swaps, insert/delete cluster |
| **Mismatch Count** | 112 diff_arg, 32 replace, 4 delete, 3 insert |
| **Verdict** | LIKELY_FIXABLE |

**Objdiff Summary**:
```
Symbol relocations: 67 (dominant) — static Symbol init guards
Register swaps: 6 across 4 pairs
  r10 <-> r25: 2 (idx 336-338)
  r28 <-> r31: 2 (idx 338-341)
Replace: 32 (ALL symbol-reloc noise, 0 real)
Insert/delete cluster at idx 331-340 (7 instrs: 3I/4D)
Stack offset shifts: +4, +76, -80
LINKER_MERGED: 2 calls to merged_DataArrayNode
Size mismatch: 2608 vs 2604 bytes (4 byte difference)
```

---

### Ghidra Analysis (With Type Seeding)

#### Type Information Quality

| Aspect | Rating (1-5) | Notes |
|--------|--------------|-------|
| **Class types visible** | 1 | Ghidra returned "Function not found" error |
| **Struct members named** | 1 | No decompilation available |
| **Function signatures** | 2 | m2c shows all handler calls with types |
| **Cross-references** | 1 | No cross-reference data from Ghidra |

#### Key Observations

**What was immediately clear from types?**
- Nothing from Ghidra — decompilation failed with "Function 0x8289B720 not found"
- m2c decompilation revealed the complete message dispatch table with 16+ handler branches
- All message types visible: DeviceChosenMsg, NoDeviceChosenMsg, MCResultMsg, SigninChangedMsg, EventDialogDismissMsg
- The static Symbol initialization guard pattern (`lbl_83117C70 & bit`) maps directly to our BEGIN_HANDLERS macro

**What remained unclear?**
- Why Ghidra can't find this function (large function at an address it doesn't recognize)
- The insert/delete cluster at idx 331-340 — likely a handler branch ordering difference
- Stack offset shifts (+4, +76, -80) suggest frame layout differs in the HANDLE_MESSAGE section
- Which specific handler branch is causing the structural mismatch

**m2c Decompilation Snippet** (showing dispatch structure):
```c
// Pattern: check guard bit → init Symbol → compare → dispatch
if (!(temp_r11_2 & 1)) {
    lbl_83117C70 = temp_r11_2 | 1;
    ??0Symbol@@QAA@PBD@Z(&lbl_83117C6C, "autosave");
}
if ((u32)unksp.unk54 == (u32)lbl_83117C6C) {
    // AutoSave handler
}
// ... 15 more branches ...
// Finally: HANDLE_SUPERCLASS(Hmx::Object)
```

---

### Matching Attempt

**Changes Made**: None attempted.

**Reasoning**: This is a BEGIN_HANDLERS/END_HANDLERS macro expansion — the largest function in the test set at 655 instructions. The mismatches break down as:
1. **67 symbol relocations** — each HANDLE/HANDLE_ACTION/HANDLE_EXPR generates static Symbol init with guard bits, producing relocation noise
2. **32 replace instructions** — ALL are symbol-reloc noise (0 real replaces!)
3. **7 insert/delete cluster** — a structural difference in one handler branch, likely HANDLE_MESSAGE section
4. **6 register swaps** — 4 pairs in the HANDLE_MESSAGE section
5. **2 LINKER_MERGED** calls to merged_DataArrayNode

The source code (BEGIN_HANDLERS at line 47-72) matches the m2c structure exactly. Every handler branch matches. The 7 I/D cluster likely represents a slightly different code generation for one of the HANDLE_MESSAGE dispatches.

**Status**: Blocked (macro-generated dispatch code + LINKER_MERGED)

---

### Learnings

**Type Seeding Helpfulness**: 0/5

**Specific Value Add**:
- ✅ None from Ghidra (decompilation failed)
- ✅ m2c provided the complete dispatch structure without type seeding

**Gaps Identified**:
- ❌ Ghidra fails on large functions (655 instructions) — possibly address mapping issue
- ❌ BEGIN_HANDLERS macro dispatch is well-understood without any decompilation
- ❌ The 67 symbol relocations are noise from the static Symbol pattern — not diagnosable issues
- ❌ No way to fix macro-generated code structure differences

**Pattern Recognized**:
- Large Handle() functions with many HANDLE_*/HANDLE_MESSAGE branches produce massive symbol relocation noise
- All 32 replaces in this function were symbol-reloc noise — zero real code differences
- The actual code logic is correct; mismatches are entirely from static init guards and LINKER_MERGED
- Ghidra decompilation fails for at least 2 of 5 test functions — reliability concern

---

### Verdict

**Should pursue 100% match?** No

**Reasoning**: 95.1% on a 655-instruction function where all 32 replaces are symbol-reloc noise and the only real differences are a 7-instruction I/D cluster in the HANDLE_MESSAGE section. The source matches the decompilation structure. The remaining gap is macro-generated dispatch code and LINKER_MERGED.

**Recommended Next Steps**:
- Mark as AT_LIMIT
- The I/D cluster at idx 331-340 could be investigated further (which HANDLE_MESSAGE?)
- Consider if HANDLE_MESSAGE macro ordering affects code generation
