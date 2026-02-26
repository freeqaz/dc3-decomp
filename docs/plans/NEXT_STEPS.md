# Next Steps Plan

**Date:** 2026-02-26
**Status:** Active — follow this plan to avoid drift

## Context: Where We Are

### Completed This Session
- **Data stubs**: `scripts/create_data_stubs.py` resolves 641 lbl_*/jumptable_*/__real@ symbols
- **ALTERNATENAME stubs**: 72 remaining symbols stubbed in link_glue.cpp
- **`/FORCE:UNRESOLVED` dropped**: 1017 errors → 0. Link is clean.
- **Source commits**: 34 files of decomp improvements committed and pushed
- **Build pipeline**: anon namespace patcher + data stubs integrated into ninja

### Current Link State
- **0 errors** (was 1017)
- **756 LNK4006 warnings** (COMDAT duplicates, harmless but noisy)
  - 45 from link_glue.obj (obsolete stubs duplicating Matching unit symbols)
  - 54 from SafeName ICF copies
  - 3 from anonymous namespace symbols
  - 654 from template instantiations / inline functions in multiple TUs
- **29 LNK4210 warnings** (.CRT section, expected)
- Linker flags: `/FORCE:MULTIPLE` only (no more `/FORCE:UNRESOLVED`)

---

## Work Stream 1: Jeff Improvements

**Goal:** Improve jeff's COFF output to reduce link warnings and enable cleaner linking.

### 1A. Clean up link_glue.cpp stubs (Easy, ~30 min)
Remove the 45+ stubs in link_glue.cpp that are now provided by Matching decomp .objs.
These generate LNK4006 warnings. Keep only:
- ICF-merged definitions (operator delete, DataArray::Node, MemOrPoolFreeSTL)
- Third-party C library stubs (ogg, zlib, curl, jpeg)
- ALTERNATENAME entries for audio SDK / unimplemented symbols
- ObjPtrList template instantiations still needed

**Files:** `src/link_glue.cpp`

### 1B. Jeff: EH metadata colocation (Medium, investigation needed)
17 `__unwind$` and 2 `__catch$` symbols are currently stubbed via ALTERNATENAME.
Jeff could keep these colocated with their parent functions during splitting.

**Investigation:** Check if jeff's split.rs already handles `.pdata` / `.xdata` sections.
If not, adding colocation rules would eliminate the need for ALTERNATENAME stubs.

**Files:** `../jeff/src/util/split.rs`

### 1C. Jeff: SafeName ICF deduplication (Medium)
54 LNK4006 warnings from `SafeName(Hmx::Object*)` appearing in many split .objs.
Jeff already handles ICF for some symbols — extend to cover SafeName and other
frequently-duplicated inline functions.

**Files:** `../jeff/src/util/split.rs`, `src/link_glue.cpp`

### 1D. Jeff: Cross-unit data label references (Hard, may not be worth it)
Some `lbl_*` references cross unit boundaries in the original binary. These are
currently handled by data stubs. Jeff could potentially track cross-unit data
references and emit proper extern declarations.

**Files:** `../jeff/src/util/split.rs`, `../jeff/src/util/symbols.rs`

---

## Work Stream 2: Link Quality

**Goal:** Reduce LNK4006 warnings from 756 toward 0. Each category:

### 2A. Audit link_glue.cpp for removable stubs
Cross-reference each link_glue.cpp stub against Matching unit list.
If the stub's symbol is now provided by a Matching unit's decomp .obj, remove it.

**Approach:**
1. Build with stubs removed
2. If LNK2001/2019 errors appear, the stub is still needed
3. Iteratively remove and test

### 2B. Template instantiation duplicates (~654 warnings)
These are inline functions / template instantiations emitted into multiple .objs.
The linker picks one and warns about the rest. This is inherent to MSVC COMDAT
behavior with `/FORCE:MULTIPLE`.

**Options:**
- Accept them (they're harmless)
- Use `/IGNORE:4006` to suppress the specific warning
- Or find a way to make jeff emit SELECT_ANY for these COMDATs

**Recommendation:** Accept or suppress. These are inherent to the hybrid link approach.

---

## Work Stream 3: Documentation Updates

**Goal:** Update 4 critically outdated docs.

### 3A. JEFF_LINK_LIMITATIONS.md — needs major update
- Frozen at 666 errors (now 0)
- Doesn't mention data stubs, ALTERNATENAME approach
- Several limitations now FIXED (ICF naming, CRT init, COMDAT extraction)
- Add current status section

### 3B. CLEAN_LINK_PROJECT.md — needs milestone update
- M2 (drop /FORCE:UNRESOLVED) is DONE but not marked
- Missing data stub and ALTERNATENAME explanations
- Unresolved symbol counts are pre-fix

### 3C. BUILD_ROADMAP.md — needs Phase 2 completion
- Phase 2 largely done but marked incomplete
- Missing all recent achievements
- Update milestone status

### 3D. LINKING_STATUS.md — needs current numbers
- Shows 571 unresolved (now 0)
- Missing data stub impact
- Matching unit count outdated

---

## Priority Order

1. **3A-3D: Docs first** — so we have accurate references going forward
2. **1A: Clean link_glue stubs** — quick win, reduces 45 warnings
3. **2B: Decide on template duplicates** — accept or suppress
4. **1B: Jeff EH metadata** — if we want to improve jeff
5. **1C: Jeff SafeName ICF** — nice to have
6. **1D: Jeff cross-unit labels** — probably not worth the effort given data stubs work

---

## Anti-Drift Rules

- Do NOT start decomp function work (pushing match% higher) during this plan
- Do NOT start working on wibo cleanup during this plan
- Stick to: docs → link_glue cleanup → jeff improvements
- Commit and push at each completion point
