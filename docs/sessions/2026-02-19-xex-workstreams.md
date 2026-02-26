# XEX Quality Workstreams

Date: 2026-02-19

Three workstreams to produce a solid, correct XEX from the hybrid build.
These are independent and can be tackled in any order.

---

## Workstream 1: Jeff COMDAT Marking

**Goal:** Eliminate 5,545 LNK4006 duplicate symbol warnings, enable `/FORCE:UNRESOLVED` instead of `/FORCE`.

**Problem:** When jeff splits the original XEX into relocatable COFF objects, symbols that appear in multiple compilation units (templates, RTTI, inline functions, float constants, string literals) are written as regular EXTERNAL symbols. The MSVC linker sees these as multiply-defined and warns on each one. This forces `/FORCE` (which suppresses both duplicate AND unresolved errors), masking real problems.

**Current state:** Jeff already implements COMDAT for `__unwind$` symbols (xex.rs lines 1200-1384). It uses the `object` crate's `add_comdat()` with `ComdatKind::Any` (`IMAGE_COMDAT_SELECT_ANY`), creating `.text$x` sections. This infrastructure just needs extending.

**Scope:** 987 unique duplicate symbols across these categories:

| Category | Est. Warnings | Examples |
|----------|---------------|---------|
| String literals | 700-900 | `??_C@_0L@...` |
| Inline functions | 800-1000 | `EasePolyIn`, `EaseBounceInOut` |
| RTTI metadata | 1000-1500 | `vftable`, `RTTI Type Descriptor` |
| Template instantiations | 600-800 | `MakeString<>`, `KeylessHash<>`, `ObjPtr<>` |
| Float constants | 300-400 | `__real@3f800000`, `__real@3fe0000000000000` |
| Virtual method duplicates | 150-200 | `Object::PreLoad`, `StaticClassName` |

**Implementation plan:**

1. **Detect duplicates during split** (`jeff/src/util/split.rs`):
   - Track symbol occurrence count across all split objects
   - Flag symbols appearing in 2+ objects as COMDAT candidates
   - Could add a `comdat: bool` field to `ObjSplit` or pass through symbol metadata

2. **Mark COMDAT in COFF output** (`jeff/src/util/xex.rs` `write_coff()`):
   - For each COMDAT-flagged symbol, create a named COMDAT section (`.text$x`, `.rdata$x`, etc.)
   - Use existing `add_comdat()` API with `ComdatKind::Any`
   - Emit symbol as GLOBAL in the COMDAT section

3. **Validate:**
   - Rebuild split objects: `ninja` (triggers dtk re-split)
   - Check link output: LNK4006 count should drop from 5,545 to ~0
   - Try `/FORCE:UNRESOLVED` instead of `/FORCE` - should now succeed

**Files:**
- `~/code/milohax/jeff/src/util/xex.rs` (COFF output, write_coff)
- `~/code/milohax/jeff/src/util/split.rs` (symbol distribution during split)
- `config/373307D9/config.json` (ldflags: `/FORCE` -> `/FORCE:UNRESOLVED`)

**Risk:** COMDAT sections change the linked PE layout. May shift `.text` addresses (small impact since addresses already don't match exactly). Verify with `compare_pe.py` after.

---

## Workstream 2: .pdata Content Validation (LNK1223)

**Goal:** Eliminate `fix_pdata.py` workaround, restore proper exception handling tables.

**Problem:** Jeff generates `.pdata` entries that the MSVC X360 linker rejects with `fatal error LNK1223: invalid or corrupt file: file contains invalid .pdata contributions`. Currently we rename `.pdata` -> `.pdat0` in all 1924 split objects, which makes exception tables invisible to the Xbox 360 kernel's `RtlLookupFunctionEntry`.

**Impact of current workaround:** C++ `try`/`catch` blocks and stack unwinding in code from split objects will crash. DC3 uses exceptions rarely (mainly MIDI parsing), so basic testing works, but this is a correctness issue.

**Root cause hypothesis:** The X360 `.pdata` format uses 8-byte `IMAGE_CE_RUNTIME_FUNCTION` entries (not the 12-byte standard Windows format):

```
struct IMAGE_CE_RUNTIME_FUNCTION {
    ULONG BeginAddress;        // 4 bytes - RVA of function start
    ULONG PdataValue;          // 4 bytes - packed bitfield:
        // PrologLen:  bits 0-7   (8 bits)
        // FuncLen:    bits 8-29  (22 bits, in 4-byte units)
        // ThirtyTwoBit: bit 30   (1 bit)
        // ExceptionFlag: bit 31  (1 bit)
};
```

Possible issues:
- Entries not sorted by BeginAddress (MSVC requires ascending order)
- Overlapping function ranges
- BeginAddress points to wrong section after relocation
- Entries from multiple split objects reference the same function range

**Investigation plan:**

1. Write `scripts/build/diagnose_pdata.py` to dump and validate `.pdata` entries:
   - Parse 8-byte entries, decode PdataValue bitfields
   - Check sorting (BeginAddress ascending)
   - Check for overlaps (BeginAddress + FuncLen*4 < next BeginAddress)
   - Check relocations targeting `.pdata`

2. Run on a problematic object (e.g., `xapobase.obj`) and a working one (control)

3. Compare against original binary's `.pdata` for the same functions

4. Fix in jeff:
   - `~/code/milohax/jeff/src/util/xex.rs` (pdata generation in write_coff)
   - `~/code/milohax/jeff/src/util/split.rs` (pdata splitting logic, lines 838-891)

5. After fix, remove `fix_pdata.py` from the split rule in `tools/project.py`

**Files:**
- New: `scripts/build/diagnose_pdata.py`
- `~/code/milohax/jeff/src/util/xex.rs`
- `~/code/milohax/jeff/src/util/split.rs` (split_pdata)
- `tools/project.py` (split rule chains fix_pdata.py)

---

## Workstream 3: .text Size Delta + Section Layout

**Goal:** Understand and minimize the 18.8KB .text growth, fix remaining section layout differences.

**Current state:** `.text` is at VA `0x82330000` (correct, after `/MERGE:.xidata=.text` fix). But our `.text` is 18,880 bytes (0x49C0) larger than the original.

**Root cause analysis (completed):**

```
                        Original        Decomp          Delta
Main .text:             0xBAAB90        0xBB7160        +0xC5D0 (+50,640)
.text$x:                (none)          0x2898          +0x2898 (+10,392)
.text$yc:               0x5F88          0x34C           -0x5C3C (-23,612)
.text$yd:               0x4A2C          0x1BC           -0x4870 (-18,544)
.xidata:                0x15D0          0x15D0          +0x0000 (no change)
                        ────────        ────────        ────────
TOTAL:                  0xBB6B14        0xBBB4D4        +0x49BC (+18,876)
```

**Verdict:** This is a **systemic build artifact**, not a source code issue. The decomp and original use the **same compiler** (MSVC 16.00.11886 / Xbox 360 XDK), but the COMDAT subsection layout differs between decomp-compiled objects and jeff-split objects due to how jeff extracts and restructures sections from the original binary. The `.text$x/.text$yc/.text$yd` subsections contain exception handling metadata, RTTI, and inline function instantiations.

**Actionable items:**

1. **Investigate .text$x content** - This 10KB section is NEW (doesn't exist in original). Likely from the `__unwind$` COMDAT extraction jeff already does. Verify by checking if removing the `__unwind$` COMDAT logic eliminates `.text$x`.

2. **Track down main .text growth** (+50KB) - Compare function sizes between original and decomp MAP files. The growth is likely from decomp functions that compile slightly larger than the originals (different optimization, extra padding, etc.).

3. **PE Override VA delta handling** - Since addresses won't match exactly, `build_xex.py`'s PE Override should apply per-function VA deltas using the MAP file rather than assuming 1:1 address mapping. This is the pragmatic fix.

4. **Section merging** - Verify no extra sections before `.text` are inflating the VA. Current layout has `.pdat0` (69KB) as an extra section. Once .pdata is fixed (Workstream 2), `.pdat0` disappears and `.pdata` grows to full size, potentially changing the layout.

**Priority:** This is lowest priority of the three workstreams. The 18.8KB growth (0.15% of .text) is cosmetic and doesn't affect functionality. The per-function VA delta approach for PE Override is the pragmatic solution.

**Files:**
- `build/373307D9/default.exe.MAP` (decomp map)
- `orig/373307D9/ham_xbox_r.map` (original map)
- `scripts/build/build_xex.py` (PE Override logic)
- `scripts/build/compare_pe.py` (anchor-based comparison)

---

## Dependencies

```
Workstream 1 (COMDAT)  ──── independent ────→  enables /FORCE:UNRESOLVED
Workstream 2 (.pdata)  ──── independent ────→  removes fix_pdata.py, fixes EH
Workstream 3 (.text)   ──── depends on 2 ──→  .pdat0 removal changes layout
```

Workstreams 1 and 2 are independent and high-impact. Workstream 3 is lower priority and partially depends on Workstream 2 (fixing .pdata changes the section count before .text).

## Session Progress (2026-02-19)

### Completed this session
- Link order matching verified correct (generate_link_order.py)
- VA shift fixed: `/MERGE:.xidata=.text` puts .text at 0x82330000
- ICF symbols resolved: `link_glue.cpp` injected via configure.py (eliminated 292 errors)
- fix_pdata.py chained into split rule (survives re-splits)
- Link errors reduced from 434 to 242 (5,545 LNK4006 + 242 unresolved)
- XEX builds and loads in Xenia PE Override (347 thunks + 360 variables)
- PE Override boots into main loop but crashes (guest_function nullptr)

### Build pipeline (current)
```
dtk xex split → fix_pdata.py (auto) → configure.py (injects link_glue) → ninja → link /FORCE
                                                                            ↓
                                                                    build_xex.py → XEX2
```

### Remaining link errors (242 total, 178 unique)
- 96 `lbl_*` (cross-unit labels)
- 24 `??__E*` (CRT dynamic initializers)
- 15 `__unwind$` + 2 `__catch$` (EH metadata)
- 10 `merged_*` (ICF aliases)
- ~10 library cross-refs (vorbis, zlib, jpeg)
- 3 jump tables
- ~18 misc (gethostbyname, itoa, stricmp, kCRLF, etc.)
