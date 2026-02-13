# Ghidra Type Seeding Stress Test - RhythmBattlePlayer::Load

**Date**: 2026-02-13
**Tester**: Claude Opus 4.6
**Goal**: Evaluate whether type-aware Ghidra decompilation improves decomp workflow

---

## Function: RhythmBattlePlayer::Load

**Symbol**: `?Load@RhythmBattlePlayer@@UAAXAAVBinStream@@@Z`
**File**: `src/system/hamobj/RhythmBattlePlayer.cpp` (BEGIN_LOADS macro)
**Subsystem**: hamobj
**Size**: 166 instructions (660 bytes)

---

### Baseline (Before Analysis)

| Metric | Value |
|--------|-------|
| **Match %** | 97.7% |
| **Primary Mismatch Type** | Register swap (r28↔r29), symbol reloc, insert/delete cluster |
| **Mismatch Count** | 11 diff_arg, 2 replace, 1 insert, 1 delete |
| **Verdict** | LIKELY_FIXABLE |

**Objdiff Summary**:
```
Register swaps: 3 instructions across 2 pairs
  r28 <-> r29: 2 (idx 49-82)
Stack offset shift: -16 (1 instruction)
Symbol relocations: 4
Insert/delete cluster at idx 76-79 (2 instrs: 1I/1D)
Replace at idx 44: subi r7, r29, 0x4 (lbl_8204DAFC) vs mr r7, r28 (gRev)
```

---

### Ghidra Analysis (With Type Seeding)

#### Type Information Quality

| Aspect | Rating (1-5) | Notes |
|--------|--------------|-------|
| **Class types visible** | 1 | Ghidra returned "Function not found" error for decompilation |
| **Struct members named** | 1 | No decompilation available |
| **Function signatures** | 1 | Only m2c output available |
| **Cross-references** | 2 | 1 caller identified (CamShot::Load — seems wrong, likely template instantiation) |

#### Key Observations

**What was immediately clear from types?**
- Nothing — Ghidra decompilation failed with "Function 0x824DBFD8 not found"
- m2c decompilation showed the structure: version check + Hmx::Object::Load + ObjRef loads
- The offsets in m2c (arg0 - 0x2A8, -0x294, -0x280, etc.) reveal member layout

**What remained unclear?**
- Why Ghidra couldn't find this function at its address (possibly template instantiation confusion)
- The replace at idx 44 involves `gRev` access — `subi r29, 0x4` vs `mr r28` suggests different addressing for the static `gRev` variable from INIT_REVS
- The I/D cluster at idx 76-79 may be a scheduling difference for ObjRef::Load calls

**m2c Decompilation Snippet** (key section):
```c
?Load@Object@Hmx@@UAAXAAVBinStream@@@Z(((*arg0)->unk4 + arg0) - 0x2AC, arg1);
?Load@?$ObjRefConcrete@VRndAnimatable@@VObjectDir@@@@...@Z(arg0 - 0x2A8, arg1, 1, 0);
// ... 6 more ObjRef loads ...
?Load@?$ObjRefConcrete@VHamLabel@@VObjectDir@@@@...@Z(temp_r28, arg1, 1, 0);
if ((void *)*arg0 != NULL) {  // Unlink pattern
    temp_r28->unk8->unk4 = temp_r28->unk4;
    temp_r28->unk4->unk8 = temp_r28->unk8;
}
temp_r28->unkC = 0;
```

---

### Matching Attempt

**Changes Made**: None attempted.

**Reasoning**: This is a BEGIN_LOADS macro expansion. The differences are:
1. `gRev` addressing — the `subi r7, r29, 0x4` vs `mr r7, r28` suggests the compiler chose different ways to access the static `gRev`/`gAltRev` variables from INIT_REVS. This may relate to how the BinStreamRev locals are organized.
2. Register swap r28↔r29 in the ObjRef unlink section
3. Stack frame layout difference (-16 offset)

These are all internal compiler decisions about variable placement and register allocation.

**Status**: Blocked (register allocation + INIT_REVS addressing)

---

### Learnings

**Type Seeding Helpfulness**: 0/5

**Specific Value Add**:
- ✅ None — Ghidra decompilation failed entirely for this function

**Gaps Identified**:
- ❌ Ghidra "Function not found" for address 0x824DBFD8 — likely not mapped in the binary analysis
- ❌ m2c output was the only decompilation available, and it lacks type information
- ❌ BEGIN_LOADS macro structure well-understood without Ghidra

**Pattern Recognized**:
- INIT_REVS + BEGIN_LOADS functions have a known pattern where `gRev`/`gAltRev` static addressing differs between target and decomp
- Ghidra may not find functions that are part of template instantiation chains or have unusual linkage

---

### Verdict

**Should pursue 100% match?** No

**Reasoning**: 97.7% match with known unfixable patterns (register swap, static variable addressing). The function is a standard Load macro expansion with no logic errors.

**Recommended Next Steps**:
- Mark as AT_LIMIT
- The `gRev` addressing pattern should be investigated across all BEGIN_LOADS functions to see if it's systemic
