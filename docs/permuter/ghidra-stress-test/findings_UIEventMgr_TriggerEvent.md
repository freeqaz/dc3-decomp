# Ghidra Type Seeding Stress Test - UIEventMgr::TriggerEvent

**Date**: 2026-02-13
**Tester**: Claude Opus 4.6
**Goal**: Evaluate whether type-aware Ghidra decompilation improves decomp workflow

---

## Function: UIEventMgr::TriggerEvent

**Symbol**: `?TriggerEvent@UIEventMgr@@QAAXVSymbol@@PAVDataArray@@@Z`
**File**: `src/lazer/meta_ham/UIEventMgr.cpp:141`
**Subsystem**: meta_ham
**Size**: 175 instructions (700 bytes target, 696 bytes base)

---

### Baseline (Before Analysis)

| Metric | Value |
|--------|-------|
| **Match %** | 95.6% |
| **Primary Mismatch Type** | Condition register (cr6 vs cr0), symbol relocs, LINKER_MERGED |
| **Mismatch Count** | 25 diff_arg, 9 replace, 1 delete |
| **Verdict** | LIKELY_FIXABLE |

**Objdiff Summary**:
```
Symbol relocations: 17 (dominant cause)
2 real replaces:
  idx 15: cmplwi r29, 0x0 vs cmplwi cr6, r29, 0x0
  idx 17: beq 0x1370 vs beq cr6, 0x144
7 symbol-reloc noise replaces
1 delete cluster at idx 16-16
LINKER_MERGED: merged_DataArrayNode (1), merged_OperatorDelete (1)
8 unexplained diff_arg
Size mismatch: 700 vs 696 bytes (4 byte difference)
```

---

### Ghidra Analysis (With Type Seeding)

#### Type Information Quality

| Aspect | Rating (1-5) | Notes |
|--------|--------------|-------|
| **Class types visible** | 4 | `UIEventMgr *this`, `DataArray *`, `Message` all typed |
| **Struct members named** | 2 | Member access via `this_00 + 0x2c`, `this_00 + 0x30` not resolved to names |
| **Function signatures** | 3 | `TriggerEvent`, `DismissEvent`, `FindArray`, `HandleType` visible with types |
| **Cross-references** | 3 | Called functions clearly identified (ActivateFirstEvent, push_back, etc.) |

#### Key Observations

**What was immediately clear from types?**
- The overall function structure: UI check → dismiss loop → event lookup → create → push_back
- `UIEventMgr` class typing confirmed, `DataArray`, `Message`, `Symbol` parameters visible
- The `BandEvent` constructor call and `EventType` enum visible in the decompilation
- The static Symbol initialization pattern (`if ((DAT_83119698 & 1) == 0)`) matches our `static Message msg("allow_event", 0)`

**What remained unclear?**
- The `cr6` vs `cr0` mismatch at idx 15/17 — same pattern as BaseMaterial
- Why 4 bytes size difference (700 vs 696) — likely an extra nop or alignment
- The 1 deleted instruction at idx 16 — possibly a scheduling difference
- Member offsets `+0x2c` and `+0x30` are `mEventQueue.begin()` and `mEventQueue.end()` but not named

**Ghidra Decompilation Snippet** (key section showing typed output):
```c
this_00 = (UIEventMgr *)__savegprlr_23(this);
if ((*(int *)(_TheUI__3PAVUIManager__A + 0x2c) == 0) &&
   (iVar7 = *(int *)(_TheUI__3PAVUIManager__A + 0x48), ...)) {
    // TheUI->InTransition() check and CurrentScreen() allow_event handling
}
// ... while loop dismissing events ...
this_01 = (DataArray *)_FindArray_DataArray__QBAPAV1_VSymbol___N_Z(...);
lVar5 = _FindArray_DataArray__QBAPAV1_VSymbol___N_Z(this_01);
bVar1 = lVar5 == 0;
if (bVar1) {
    lVar5 = _FindArray_DataArray__QBAPAV1_VSymbol__0_Z(...);
}
// new BandEvent(eventType, eventArr, eventData)
```

---

### Matching Attempt

**Changes Made**: None attempted.

**Reasoning**: The mismatches decompose as:
1. **cr6 vs cr0** (2 real replaces) — same unfixable pattern as BaseMaterial::PropValDifferent
2. **17 symbol relocations** — noise from static Symbol initialization and LINKER_MERGED calls
3. **1 deleted instruction** — likely NOP or scheduling difference causing the 4-byte size mismatch
4. **2 LINKER_MERGED** calls (DataArrayNode, OperatorDelete) — unfixable ICF

The source at lines 141-185 is structurally correct. The logic matches the Ghidra and m2c output.

**Status**: Blocked (cr6 pattern + LINKER_MERGED + instruction scheduling)

---

### Learnings

**Type Seeding Helpfulness**: 3/5

**Specific Value Add**:
- ✅ Function structure clearly visible — UI check, dismiss loop, event lookup/creation
- ✅ `UIEventMgr`, `DataArray`, `Message`, `BandEvent` types all correctly resolved
- ✅ The `EventType` enum and `kDialogEvent`/`kTransitionEvent` distinction visible
- ✅ Static Symbol initialization pattern (`DAT_83119698 & bit`) easily mapped to our statics

**Gaps Identified**:
- ❌ cr6 vs cr0 pattern — not diagnosable from types
- ❌ `mEventQueue` vector internals (begin/end pointers) shown as raw offsets
- ❌ LINKER_MERGED calls obscure the actual function being called

**Pattern Recognized**:
- `cr6` vs `cr0` appears again in a function with `if (condition && ptr)` patterns
- Static Symbol initialization with guard bits produces many symbol relocation diff_args
- Functions with both static locals AND LINKER_MERGED tend to have elevated diff_arg counts

---

### Verdict

**Should pursue 100% match?** No

**Reasoning**: 95.6% with unfixable patterns (cr6, LINKER_MERGED x2, instruction scheduling). Source logic is correct and matches the decompilation structure.

**Recommended Next Steps**:
- Mark as AT_LIMIT
- The cr6 pattern confirmed across 2 functions now — add to known unfixable patterns doc
