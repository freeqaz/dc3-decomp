# Ghidra Type Seeding Stress Test - Cross-Function Summary

**Date**: 2026-02-13
**Tester**: Claude Opus 4.6
**Functions Tested**: 5
**Ghidra Setup**: 11,733 typed functions, 2,105 structures seeded from DWARF/headers

---

## Test Results Overview

| # | Function | Match % | Ghidra Worked? | Type Helpfulness | Primary Blocker |
|---|----------|---------|----------------|-----------------|-----------------|
| 1 | CharBonesSamples::Load | 96.7% | Yes | 1/5 | Register swaps (6 pairs) |
| 2 | BaseMaterial::PropValDifferent | 97.7% | Yes | 2/5 | cr6 vs cr0 condition register |
| 3 | RhythmBattlePlayer::Load | 97.7% | **No** (not found) | 0/5 | Register swap + gRev addressing |
| 4 | UIEventMgr::TriggerEvent | 95.6% | Yes | 3/5 | cr6 + LINKER_MERGED + scheduling |
| 5 | SaveLoadManager::Handle | 95.1% | **No** (not found) | 0/5 | Symbol reloc noise (67) + I/D cluster |

**Average Type Helpfulness**: 1.2 / 5.0
**Ghidra Success Rate**: 3/5 (60%)

---

## Cross-Function Patterns

### Pattern 1: Register Allocation Dominates High-Match Functions
All 5 functions are at 95-98% match. At this level, the remaining differences are overwhelmingly:
- **Register swaps**: 3 of 5 functions (CharBonesSamples, RhythmBattlePlayer, SaveLoadManager)
- **Condition register selection** (cr6 vs cr0): 2 of 5 functions (BaseMaterial, UIEventMgr)
- **LINKER_MERGED** (ICF): 4 of 5 functions

Type information provides **zero insight** into any of these patterns. They are compiler-internal decisions about register allocation, condition register usage, and identical code folding.

### Pattern 2: Symbol Relocation Noise Inflates Mismatch Counts
Symbol relocations account for the bulk of diff_arg counts:
- SaveLoadManager::Handle: 67 of 112 diff_arg (60%)
- UIEventMgr::TriggerEvent: 17 of 25 diff_arg (68%)
- BaseMaterial::PropValDifferent: 6 of 23 diff_arg (26%)

These are noise from static Symbol initialization guard bits and LINKER_MERGED calls. They don't indicate code issues.

### Pattern 3: cr6 vs cr0 — Newly Identified Unfixable Pattern
Two functions exhibit `cmplwi cr6, rN, 0x0` (target) vs `cmplwi rN, 0x0` (decomp, implicit cr0). Both occur in compound conditions:
- BaseMaterial: `if (!base) base = gDefaultMat; MILO_ASSERT(base, ...)`
- UIEventMgr: `if (!TheUI->InTransition()) { UIScreen *cur = TheUI->CurrentScreen(); if (cur) {`

The compiler selects cr6 to avoid clobbering cr0 when the result is needed across intervening operations. This is not controllable from source code.

### Pattern 4: Ghidra Decompilation Unreliable (40% Failure Rate)
Ghidra failed to decompile 2 of 5 functions with "Function not found" errors:
- RhythmBattlePlayer::Load (0x824DBFD8)
- SaveLoadManager::Handle (0x8289B720)

Both are relatively large functions (166 and 655 instructions). This may indicate:
- Address mapping issues in the binary analysis
- Functions in sections Ghidra didn't fully analyze
- Template instantiation or macro-generated code confusing function boundaries

---

## What Type Seeding Actually Helped With

### Confirmed Useful (When Ghidra Works)
1. **Class identity confirmation**: `BaseMaterial*`, `UIEventMgr*`, `CharBonesSamples*` correctly typed
2. **Cross-references**: BaseMaterial callers (3) revealed the material comparison pipeline
3. **Function structure validation**: UIEventMgr::TriggerEvent's UI check → dismiss → lookup → create flow was clearly visible
4. **Static local pattern mapping**: Guard bit patterns (`DAT_83119698 & 1`) map to `static Symbol/Message` declarations

### Not Useful
1. **Member field names**: Consistently shown as offsets (+0x2c, +0x74) not field names
2. **Register allocation insight**: Zero — the primary remaining issue at 95-98% match
3. **LINKER_MERGED resolution**: Types don't help identify what got merged
4. **Macro-generated code**: BEGIN_HANDLERS/BEGIN_LOADS patterns already well-understood

---

## Tooling Gaps Identified

1. **Ghidra reliability**: 40% failure rate makes it unreliable as a primary analysis tool
2. **Member name resolution**: Despite 2,105 structures seeded, field access still shown as raw offsets
3. **m2c superiority for PPC**: m2c's output was more useful than Ghidra for every function where both worked — better register tracking, clearer control flow
4. **Missing: cr field analysis**: No tool currently diagnoses condition register allocation mismatches
5. **Missing: symbol relocation filtering**: diff_inspect counts symbol relocs but doesn't cleanly separate them from real issues

---

## Verdict: Is Type Seeding Worth Continued Investment?

### Short Answer: **Not for the 95-98% match tier**

### Detailed Assessment

**Investment cost**: ~4 hours (Session 1) to set up Ghidra + seed 11,733 functions and 2,105 structures.

**Return**: Average 1.2/5 helpfulness across 5 functions. The best case (UIEventMgr, 3/5) provided structure validation that was already available from reading the source code. The worst cases (0/5) provided nothing because Ghidra couldn't even decompile.

**The fundamental mismatch**: Type seeding helps understand *what* code does. But at 95-98% match, we already know what the code does — our source is correct. The remaining differences are *how* the compiler generates machine code (register allocation, condition register selection, instruction scheduling), which types cannot illuminate.

### Where Type Seeding WOULD Help
- **Low-match functions (< 80%)**: Where the source logic is wrong or incomplete
- **Unknown struct layouts**: Where field ordering/sizes are wrong
- **Cross-reference discovery**: Finding callers/callees to understand function purpose
- **Initial implementation**: Writing first-pass source before any matching attempt

### Recommendation
1. **Don't invest further in Ghidra for high-match polishing** — the ROI is negative
2. **Keep Ghidra setup for low-match/unimplemented functions** — it may prove valuable there
3. **Fix Ghidra reliability** (40% failure rate) before using it in any workflow
4. **Invest in diff_inspect improvements** instead — cr6 pattern detection, symbol reloc filtering, and better instruction-level diagnosis tools would directly address the gaps

---

## New Patterns to Document

| Pattern | Description | Fixable? | Frequency |
|---------|-------------|----------|-----------|
| CR6_CONDITION | Compiler uses cr6 instead of cr0 for compound conditions | No | 2/5 (40%) |
| SYMBOL_RELOC_NOISE | Static Symbol guards produce massive diff_arg counts | N/A (noise) | 3/5 (60%) |
| GHIDRA_NOT_FOUND | Ghidra can't decompile certain large functions | N/A (tooling) | 2/5 (40%) |

---

## Files Created

- `findings_CharBonesSamples_Load.md` — Register swap dominated, types unhelpful
- `findings_BaseMaterial_PropValDifferent.md` — cr6 pattern discovered, types marginal
- `findings_RhythmBattlePlayer_Load.md` — Ghidra failed, BEGIN_LOADS pattern
- `findings_UIEventMgr_TriggerEvent.md` — Best type seeding result (3/5), cr6 + merged
- `findings_SaveLoadManager_Handle.md` — Largest function, Ghidra failed, all replaces noise
