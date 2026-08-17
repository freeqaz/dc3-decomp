# Clean Link Project

Goal: eliminate `/FORCE` flags from the hybrid link and produce a 1:1 XEX.

Spans three repos: **jeff** (dtk fork — splitting + COFF generation), **wibo** (Win32-on-Linux — runs cl.exe/link.exe), **dc3-decomp** (source + build config).

## Current State (2026-02-26)

### Config A: Both decomp + split objects linked (historical, replaced by Config B)

Config A linked both decomp and split objects for Matching units. **Abandoned** due to a critical issue: the linker placed split function bodies adjacent to decomp function bodies, creating overlapping address space. Runtime guest-memory patches at MAP addresses could corrupt overlapping split code. This was the root cause of the SkeletonIdentifier boot hang (Session 40).

Config A produced 3,735 LNK4006 warnings (3,183 same-object duplicates + 552 cross-object). Replaced by Config B.

### Config B: Decomp-only for Matching units (current default)

Changed `project.py` to skip split objects for Matching units. Data stubs provide `lbl_*` data symbols. ALTERNATENAME stubs handle remaining gaps.

| Metric | Value |
|--------|-------|
| **LNK2001/LNK2019 (unresolved)** | **0** |
| **LNK2005 (hard duplicates)** | **0** |
| **LNK4006 (duplicate warnings)** | **13,400** (see breakdown and [FORCE_MULTIPLE_ELIMINATION.md](FORCE_MULTIPLE_ELIMINATION.md)) |
| **LNK4210 (.CRT warnings)** | 113 |
| **ALTERNATENAME stubs** | 397 (was 1,451) |
| **Link flags** | `/FORCE:MULTIPLE` only |

All unresolved symbols resolved via three approaches (see below). XEX boots in Xenia with no hangs.

### How Unresolved Symbols Were Resolved (Config B)

Originally 726 unique unresolved symbols across 1017 errors. Resolved in three steps:

| Step | Approach | Symbols Resolved | Implementation |
|------|----------|-----------------|----------------|
| 1 | **Data stubs** | 577 `lbl_*`, 3 `jumptable_*`, 28 `__real@*` | `scripts/create_data_stubs.py` — strips code from split .objs, keeps data sections |
| 2 | **ALTERNATENAME stubs** | 72 remaining (audio SDK, EH records, STL templates, vtordisp thunks) | `src/link_glue.cpp` — `/ALTERNATENAME:mangled=__link_glue_noop` directives |
| 3 | **Wibo CRC + path mapping** | `??_C@` string hash mismatches | `SigForPbCb` CRC-32 in wibo + `WIBO_PATH_MAP` for `__FILE__` paths |

**Data stubs**: When Config B replaces a split .obj with a decomp .obj, cross-references from other split .objs using `lbl_*` data names break. `create_data_stubs.py` creates minimal COFF .objs with only data sections, preserving `lbl_*` symbols. 299 data-stub .obj files, 19,925 data symbols. Auto-linked by `project.py`.

**ALTERNATENAME stubs**: 72 symbols stubbed via MSVC `/ALTERNATENAME` linker directive. Categories: Synth360/FxSend360 audio (14), XAPO/CXAPOBase SDK (8), LEAPFX/NUISPEECH Kinect (3), DSP::Synapse (3), UIPanel vtordisp thunks (5), STL templates (8), `__unwind$` EH records (17), `__catch$` EH (2), misc globals (12).

### LNK4006 Breakdown (Config B, 756 total)

| Category | Count | Notes |
|----------|-------|-------|
| Template instantiations / inline functions | ~654 | Inherent to hybrid linking — emitted into multiple TUs |
| SafeName ICF copies | 54 | Same function in many split .objs |
| link_glue.cpp obsolete stubs | 45 | Can be cleaned up (stubs now duplicate Matching unit symbols) |
| Anonymous namespace | 3 | Expected |

All handled by `/FORCE:MULTIPLE`. The linker picks one definition and discards duplicates. These are cosmetic warnings.

### History: LNK4006 Warning Count Evolution

| Date | Count | Context |
|------|-------|---------|
| 2026-02-20 | 275 | Wine-based linking, 252 Matching units (wine suppressed some warnings) |
| 2026-02-23 | 3,735 | Wibo-based linking (true count), Config A (both decomp + split linked) |
| 2026-02-26 | 756 | Config B (decomp-only for Matching), 707 data stubs |
| 2026-02-28 | 13,400 | 968 data stubs (261 new for promoted Matching units), 397 ALTERNATENAME stubs (was 1,451) |

Config B eliminates same-object duplicates (3,183 warnings) by not linking split .objs for Matching units. The 2026-02-28 increase to 13,400 is because 261 additional data stubs were generated for newly-promoted Matching units, each providing more COMDAT code sections that duplicate templates/RTTI/vtables across TUs. See [FORCE_MULTIPLE_ELIMINATION.md](FORCE_MULTIPLE_ELIMINATION.md).

### The String Hash Problem — RESOLVED

`??_C@` string literal hashes now match 100% between decomp and original objects.

**Root cause:** `cl.exe` calls `SigForPbCb` from `mspdbXX.dll` (not `RtlComputeCrc32`) for CRC-32 hashing. Wibo's dummy `SigForPbCb` returned 0.

**Fixes applied:**
1. Implemented CRC-32 in `SigForPbCb` (`wibo/dll/mspdb/mspdb_dll.cpp`)
2. Fixed `WIBO_PATH_MAP` to use two source roots: `system/src/` → `src/system/`, `lazer/src/` → `src/lazer/`
3. Changed include paths from relative to absolute mapped Windows paths (`/I E:/lazer_build_gmc1/system/src`)
4. Fixed 5 source string bugs found via hash comparison

**Result:** 121 shared `??_C@` symbols, 0 mismatches. See `docs/plans/clean-link/WIBO_CRC_INVESTIGATION.md`.

## Compiler Facts

Both decomp and original use the **same compiler**:
- **cl.exe**: MSVC 16.00.11886.00 (Visual Studio 2010, Xbox 360 XDK)
- **link.exe**: LINK 10.0.11886.0
- **Binary at**: `build/compilers/X360/16.00.11886.00/cl.exe`
- **Rich header confirms**: 1,871 C++ objects + 465 C objects compiled with cl.exe 16.00.11886
- **Build flags**: `/O1 /Oi /GR /EHsc` (base config) — note `/O1` enables `/GF` (string pooling). Xbox 360 `/O1` = `/Oy /Ob2 /GF` per XDK docs (standard MSVC `/O1` also includes `/Gy`, but XDK may differ). Per-category overrides: `/TP` (jpeg), `/TC /GS` (curl).

There is **no compiler version mismatch**. Linking issues stem from: (a) structural differences between compiled-from-source and split-from-binary objects, and (b) wibo environment differences affecting the hash computation.

---

## Work Items

### 1. Fix COMDAT Regression in Jeff — DONE

**The problem.** Commit `cf01a80` restricted COMDAT extraction to code sections only, inflating LNK4006 from ~3,700 to ~9,987.

**Fix applied:** Reverted to `if sect.kind == ObjSectionKind::Bss { continue; }` in `jeff/src/util/xex.rs` ~line 1334. This restores .rdata COMDAT extraction for strings, floats, RTTI, and vtable symbols.

**Result:** LNK4006 dropped from ~9,987 to 3,735. 0 errors. 0 fixup overflow.

**Status:** [x] Complete.

---

### 2. Investigate Wibo vs Wine LNK4006 Difference

**The problem.** The MSVC linker reports 3,183 more LNK4006 warnings under wibo than it did under wine. All are NODUPLICATES-vs-ANY conflicts for same-object duplicates (decomp vs split). Wine-based link.exe was silently suppressing these warnings.

**Investigation needed:**
1. Check if wibo handles the MSVC linker's console output differently (stderr buffering, codepage, etc.)
2. Check if wine provides a Win32 API that affects COMDAT selection resolution
3. Try linking under wine again to verify the 275 count is still achievable

**Impact:** Cosmetic — these warnings don't affect correctness. Both builds produce working executables. But understanding the difference may reveal a wibo bug.

**Status:** [ ] Not started. Low priority — doesn't affect correctness.

---

### 3. Fix Wibo CRC32 for String Hashes (hash=0 → real hashes) — DONE

**The problem.** MSVC cl.exe computes a CRC-32 hash over string literal content bytes for the `??_C@` mangled name. Under wibo, the CRC always returned 0 (producing hash `A`).

**Root cause:** cl.exe calls `SigForPbCb` from `mspdbXX.dll` (NOT `RtlComputeCrc32`). Wibo's dummy `mspdb_dll.cpp` had `SigForPbCb` hardcoded to `return 0;`.

**Fix:** Implemented CRC-32 with reflected polynomial `0xEDB88320` in `SigForPbCb`. Called with `dwInitial=0xFFFFFFFF`, no final XOR.

**Status:** [x] Complete. Committed on wibo `x360-linker-support` branch.

---

### 4. Match Original Build Paths (path strings → identical content) — DONE

**The problem.** `__FILE__` macro and assert strings embed the build path. Original uses `e:\lazer_build_gmc1\system\src\...`, decomp used `z:\home\free\code\milohax\dc3-de...`.

**Fix applied:** `WIBO_PATH_MAP` with two source root mappings + absolute mapped include paths.

The original build had two source trees:
- `e:\lazer_build_gmc1\system\src\` — Milo engine (our `src/system/`)
- `e:\lazer_build_gmc1\lazer\src\` — DC3 game (our `src/lazer/`)

**Changes:**
- `configure.py`: Two-entry `WIBO_PATH_MAP` pointing `system/src/` → `src/system/` and `lazer/src/` → `src/lazer/`
- `tools/defines_common.py`: Include paths changed to absolute mapped Windows paths (`/I E:/lazer_build_gmc1/system/src`)
- `tools/project.py`: Shell quoting for semicolon-separated path map value

**Status:** [x] Complete. All `__FILE__` paths now match the original build tree layout.

---

### 5. Jeff Hash Normalization (fallback — likely unnecessary)

**The problem.** After fixing wibo CRC32 (#3) and matching build paths (#4), there should be **no** remaining hash mismatches — since the hash is JamCRC over string content only, identical strings will produce identical hashes regardless of which source file they appear in. This work item exists purely as a safety net in case edge cases emerge.

**Fix**: Rewrite `??_C@` symbol names in split objects to replace the hash with a canonical value:

```rust
// jeff/src/util/xex.rs — in symbol name emission
fn normalize_string_comdat_hash(name: &str) -> String {
    // ??_C@_XX@OLDHASH@content → ??_C@_XX@A@content
    if !name.starts_with("??_C@_") { return name.to_string(); }
    let rest = &name[6..];                    // after "??_C@_"
    let at1 = rest.find('@')?;               // end of encoding+length
    let after_at1 = &rest[at1 + 1..];        // "OLDHASH@content..."
    let at2 = after_at1.find('@')?;          // end of hash
    let content = &after_at1[at2..];         // "@content..."
    format!("??_C@_{}{}", &rest[..at1 + 1], "A", content)
}
```

**Note**: This is a **fallback** — if wibo CRC32 is fixed and paths match, both sides will produce the same hash and this becomes unnecessary. Keep it as insurance.

**Risk:** None — the content suffix encodes the full string, so identical content suffixes mean identical strings.

**Status:** [ ] Not started. Lower priority if wibo fix works.

**Files:**
- `jeff/src/util/xex.rs` ~line 1630 (symbol emission)
- `jeff/src/util/split.rs` ~line 1625 (COMDAT candidate marking)

---

### 6. NODUPLICATES COMDAT (~3,183 same-obj — accepted)

**The problem.** MSVC X360 uses `IMAGE_COMDAT_SELECT_NODUPLICATES` for function-level COMDATs. Jeff uses `SELECT_ANY`. This mismatch produces LNK4006 for every Matching unit.

**This is inherent** to the hybrid link approach. The linker picks the decomp definition first, which is correct. `/FORCE:MULTIPLE` suppresses the warnings.

**Possible fix:** Change jeff to emit `NODUPLICATES` instead of `ANY` for function COMDATs in split objects. This would match the decomp's selection type and should eliminate the same-obj warnings. Risk: NODUPLICATES-vs-NODUPLICATES for the same symbol may produce LNK2005 instead of LNK4006 — needs testing.

**Status:** [x] Accepted — `/FORCE:MULTIPLE` handles it.

---

## .text Size Delta

Same compiler, different COFF structure:

```
                 Original    Decomp      Delta
Main .text:      0xBAAB90    0xBB7160    +50,640  (decomp functions compile slightly larger)
.text$x:         (none)      0x2898      +10,392  (__unwind$ COMDAT extraction creates new subsection)
.text$yc:        0x5F88      0x034C      -23,612  (RTTI complete_object_locator — fewer in decomp)
.text$yd:        0x4A2C      0x01BC      -18,544  (??__E/??__F dynamic init — fewer in decomp)
                 ────────    ────────    ────────
Total:           0xBB6B14    0xBBB4D4    +18,876  (0.15%)
```

The delta comes from:
1. **`.text$x` is new** — jeff extracts `__unwind$` into COMDAT subsections; the original binary inlined them
2. **`.text$yc/.yd` shrink** — decomp has fewer RTTI/init thunks than the original (not all decompiled yet)
3. **Main `.text` grows** — decomp functions are slightly larger (minor codegen differences, non-matching functions)

This is cosmetic — the linker resolves all relocations correctly. It causes address drift between decomp and original MAP files but doesn't affect runtime correctness.

---

## Priority Order

| Priority | Work | Impact | Repo | Effort |
|----------|------|--------|------|--------|
| **1** | ~~Fix COMDAT regression (`cf01a80`)~~ | ~~LNK4006: ~9,987 → 3,735~~ | jeff | **DONE** |
| **2** | ~~Fix wibo CRC32 (`SigForPbCb`)~~ | ~~Correct string hashes~~ | wibo | **DONE** |
| **3** | ~~Match original build paths~~ | ~~`__FILE__` strings match~~ | wibo + dc3-decomp | **DONE** |
| **4** | ~~Investigate wine vs wibo LNK4006~~ | ~~Understand same-obj warnings~~ | — | **Moot** (Config B eliminates same-obj) |
| **5** | ~~Jeff hash normalization (fallback)~~ | ~~Safety net for mismatches~~ | — | **Unnecessary** (wibo CRC fix works) |
| **6** | ~~NODUPLICATES acceptance~~ | ~~Same-obj LNK4006~~ | — | **Moot** (Config B) |

All critical priorities are complete. The link has 0 errors and only `/FORCE:MULTIPLE` remains for cosmetic COMDAT duplicate warnings.

**Updated 2026-02-28:** After regenerating 968 data stubs (was 707) to cover newly-promoted Matching units, LNK4006 rose to **13,400** (from 756) — more data stubs means more cross-unit COMDAT duplicates. ALTERNATENAME stubs reduced from 1,451 to **397** (data stubs now provide COMDAT code that was previously stubbed). See [FORCE_MULTIPLE_ELIMINATION.md](FORCE_MULTIPLE_ELIMINATION.md) for the plan to eliminate `/FORCE:MULTIPLE` entirely.

---

## Milestone Definitions

**M1: Fix COMDAT Regression** — DONE
- Restored `if sect.kind == ObjSectionKind::Bss { continue; }` in jeff
- LNK4006: ~9,987 → 3,735
- 0 errors, 0 unresolved, 0 fixup overflow

**M2: Drop `/FORCE:UNRESOLVED`** — DONE (2026-02-26)
- All 726 unique unresolved symbols resolved via data stubs (608) + ALTERNATENAME (72) + wibo CRC fix
- `/FORCE:UNRESOLVED` removed from config.json ldflags
- Link succeeds with only `/FORCE:MULTIPLE` — 0 errors, 756 LNK4006 warnings

**M3: 1:1 String Symbol Matching** — DONE (2026-02-26)
- Wibo CRC32 implemented (#3) via `SigForPbCb` in `mspdb_dll.cpp`
- Build paths matched (#4) via `WIBO_PATH_MAP` with two source roots
- 121 shared `??_C@` symbols, 0 hash mismatches
- 5 source string bugs found and fixed via hash comparison

**M4: Minimal `/FORCE`** — DONE (2026-02-26)
- Only `/FORCE:MULTIPLE` remains for COMDAT duplicate warnings
- 0 unresolved symbols, 0 errors
- All `??_C@` string symbols hash correctly
- `/FORCE:MULTIPLE` is cosmetic suppression of 756 harmless COMDAT duplicate warnings

**M5: 1:1 XEX**
- `.text` size delta eliminated (all functions matching, no extra subsections)
- All sections byte-identical to original
- MAP file addresses match original
- Requires finishing the decomp

**M6: Linked binary verification**
- Re-split the decomp XEX with jeff (same tool that split the original)
- Compare re-split objects against original split objects in objdiff
- Gives ground-truth match% that accounts for ICF merging, COMDAT resolution, and string reference resolution
- Useful as a second-pass verification at milestones — "what's the real match% after link-time effects?"
- See [LINKED_BINARY_VERIFICATION.md](LINKED_BINARY_VERIFICATION.md) for full design

M1 is done. M2 is done. M3 is done. M4 is done. M5 requires completing the decomp. M6 can be built any time (the linked binary is now meaningful).

---

## Key References

| Doc | Path | What it covers |
|-----|------|---------------|
| Linking Status | `docs/archive/2026-08-17-doc-audit/status-snapshots/LINKING_STATUS.md` | COMDAT infrastructure, subsection layout, marking rules. **Archived 2026-08-17** — its counts are a 2026-02 snapshot; the architecture it describes is still broadly right. See `docs/tools/BUILD_SYSTEM.md` |
| Jeff Limitations | `docs/sessions/JEFF_LINK_LIMITATIONS.md` | All jeff-side limitations with fix paths |
| Clean Link Plan | `docs/sessions/2026-02-20-clean-link-plan.md` | COMDAT Phase 1/2 task breakdown |
| Technical Notes | `docs/decomp/TECHNICAL_NOTES.md` | Compiler version confirmation, flags |
| Build Roadmap | `docs/plans/BUILD_ROADMAP.md` | Overall build pipeline status |
| link_glue.cpp | `src/link_glue.cpp` | ICF stubs, library stubs |

## Jeff Source Locations

| File | Lines | What |
|------|-------|------|
| `jeff/src/util/xex.rs` | ~1334 | COMDAT section filter (regression fixed) |
| `jeff/src/util/xex.rs` | ~1375 | REL14 COMDAT exclusion filter |
| `jeff/src/util/xex.rs` | 1560-1571 | COMDAT section naming (.text$x, .text$dup) |
| `jeff/src/util/xex.rs` | 1613-1712 | COMDAT symbol emission |
| `jeff/src/util/xex.rs` | 1699-1706 | COMDAT selection type (ComdatKind::Any) |
| `jeff/src/util/split.rs` | 1625-1677 | COMDAT candidate marking |

## Wibo Source Locations

| File | Lines | What |
|------|-------|------|
| `wibo/dll/mspdb/mspdb_dll.cpp` | 559-576 | `SigForPbCb` CRC-32 implementation (the CRC fix) |
| `wibo/src/files.cpp` | — | `WIBO_PATH_MAP` support, `pathToWindows`/`pathFromWindows` |
| `wibo/dll/ntdll.cpp` | — | `RtlComputeCrc32` (for completeness, not used by cl.exe) |
| `wibo/src/modules.cpp` | 774-777 | Stub mechanism (crashes on unimplemented calls) |

## Resolved Issues

| Issue | Was | Resolution |
|-------|-----|------------|
| COMDAT regression (`cf01a80`) | LNK4006: ~9,987 | Fixed — reverted to BSS-only filter |
| `lbl_*` / `jumptable_*` / `__real@*` (608) | LNK2001 errors | Data stubs (`create_data_stubs.py`) |
| `??_C@` string hash=0 | LNK2001 errors | Wibo CRC fix (`SigForPbCb`) + `WIBO_PATH_MAP` |
| Audio SDK (Synth360, XAPO, LEAPFX) | LNK2001 errors | ALTERNATENAME stubs in `link_glue.cpp` |
| `__unwind$`/`__catch$` EH (19) | LNK2001 errors | ALTERNATENAME stubs |
| UIPanel vtordisp thunks (5) | LNK2001 errors | ALTERNATENAME stubs |
| STL template instantiations (8) | LNK2001 errors | ALTERNATENAME stubs |
| `merged_*` ICF aliases | LNK2001 errors | `link_glue.cpp` definitions |
| Library/CRT gaps | LNK2001 errors | `link_glue.cpp` definitions |
| Anonymous namespace hashes | Symbol mismatches | `obj_anon_ns_patcher.py` (post-compile) |
| `/FORCE:UNRESOLVED` | Required for link | **Dropped** — all symbols resolve |
