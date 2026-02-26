# Clean Link Project

Goal: eliminate `/FORCE` flags from the hybrid link and produce a 1:1 XEX.

Spans three repos: **jeff** (dtk fork — splitting + COFF generation), **wibo** (Win32-on-Linux — runs cl.exe/link.exe), **dc3-decomp** (source + build config).

## Current State (2026-02-26)

The hybrid link uses `/FORCE:MULTIPLE` + `/FORCE:UNRESOLVED`. Link succeeds with **0 errors** and the XEX boots in Xenia.

| Metric | Value |
|--------|-------|
| **LNK2001/LNK2019 (unresolved)** | **0** |
| **LNK2005 (hard duplicates)** | **0** |
| **LNK2013 (fixup overflow)** | **0** |
| **LNK4006 (duplicate warnings)** | **3,735** (see breakdown below) |
| **LNK4210 (.CRT warnings)** | 28 |
| **Link flags** | `/FORCE:MULTIPLE` + `/FORCE:UNRESOLVED` |

### LNK4006 Breakdown

The 3,735 LNK4006 warnings break down into two categories:

| Category | Count | Cause |
|----------|-------|-------|
| **Same-object** (X.obj defined in X.obj) | 3,183 | Decomp uses `NODUPLICATES`, split uses `ANY` |
| **Cross-object** (X.obj defined in Y.obj) | 552 | Templates/inlines/globals in multiple TUs |

**Same-object warnings (3,183):** These occur for the 364 Matching units where both decomp and split objects are linked. The MSVC X360 compiler emits `IMAGE_COMDAT_SELECT_NODUPLICATES` for function-level COMDATs. Jeff's split objects use `IMAGE_COMDAT_SELECT_ANY`. The NODUPLICATES-vs-ANY mismatch triggers LNK4006.

**Cross-object warnings (552):** Template instantiations (e.g., `PropSync<Key<Color>>`), inline functions (e.g., `KeylessHash::Remove`), and global data (e.g., `gEaseFuncs`) appearing in multiple split objects. These are expected COMDAT dedup situations.

### History: The "275" Number

The 275 LNK4006 count from 2026-02-20 was measured when:
1. **Wine** was used for linking (not wibo — switched to wibo on 2026-02-23 in `bb632dc6`)
2. Only **252** Matching units existed (now 364)

Testing confirms: rebuilding with the same objects.json (252 units) and the same jeff produces **1,635** LNK4006 under wibo vs 275 under wine. The wine-based link.exe was silently suppressing NODUPLICATES-vs-ANY warnings. The current wibo-based count is the **true** warning count.

The `cf01a80` COMDAT regression (which inflated LNK4006 to ~9,987) has been **fixed** — reverted to `if sect.kind == ObjSectionKind::Bss { continue; }` in jeff.

### The String Hash Problem

Decomp objects consistently produce `??_C@` string literals with hash `A` (= CRC value 0), while original objects have full 8-character hashes:

```
Decomp (wibo):   ??_C@_09A@CharBones?$AA@            hash=A   (CRC=0)
Original:        ??_C@_09CBPAIBIE@CharBones?$AA@      hash=CBPAIBIE
```

**Every** decomp string has hash=0. This is not a per-string issue — the hash function itself is broken under wibo.

**Root cause (confirmed):** The `??_C@` hash is a **JamCRC** (CRC-32 with `XorOut=0` instead of `0xFFFFFFFF`) computed over the **string content bytes only** — no file path, no build context, no compilation unit. This is confirmed by:

1. **LLVM's `MicrosoftMangle.cpp:mangleStringLiteral`** — the CRC input is exclusively `GetLittleEndianByte(I)` for each byte of the string literal plus null terminator bytes. No other data is fed into the hash.
2. **MSVC's `/GF` (string pooling)** — relies on identical strings across different translation units getting the same `??_C@` symbol for COMDAT deduplication. This would be impossible if the hash included build context.
3. **Arthur O'Dwyer's collision demonstration** — two strings in different files with identical first 32 bytes, same length, and same JamCRC collide, proving the hash is deterministic over content alone.

Under wibo, `cl.exe` calls `RtlComputeCrc32` (ntdll) which wibo doesn't implement, causing a silent fallback to CRC=0 for all strings. Fixing this single function will fix **all** string hashes for strings with identical content. The only residual mismatches will be `__FILE__` path strings (see below).

Currently this doesn't cause link errors (symbols resolve via content suffix matching), but it prevents 1:1 symbol matching.

#### JamCRC Algorithm Reference

| Parameter | Value |
|-----------|-------|
| Width | 32 |
| Poly | `0x04C11DB7` |
| Init | `0xFFFFFFFF` |
| RefIn | True |
| RefOut | True |
| XorOut | `0x00000000` (standard CRC-32 uses `0xFFFFFFFF`) |
| Check | `0x340BC6D9` (result for ASCII "123456789") |

Relationship: `JamCRC(data) = CRC32(data) ^ 0xFFFFFFFF` (bitwise complement of standard CRC-32).

The CRC value is encoded in the mangled name using MSVC's number encoding: nibbles mapped to `A`-`P` (`A`=0, `B`=1, ..., `P`=15), terminated by `@`. Value 0 encodes as `A@`, which is exactly what wibo produces.

### File Path Difference

Original: `e:\lazer_build_gmc1\system\src\...` | Decomp: `z:\home\free\code\milohax\dc3-de...`

Since the hash is over string **content** bytes, `__FILE__` and assert strings will produce different hashes because their content genuinely differs (different path bytes → different CRC). These are the **only** strings that will mismatch after fixing wibo CRC32. Fix requires matching build paths (Work Item 4).

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

### 3. Fix Wibo CRC32 for String Hashes (hash=0 → real hashes)

**The problem.** MSVC cl.exe computes a JamCRC hash over string literal content bytes for the `??_C@` mangled name. Under wibo, the CRC always returns 0 (producing hash `A`). The original build produces proper 8-character hashes like `CBPAIBIE`.

**Root cause (confirmed):** cl.exe calls `RtlComputeCrc32` (ntdll.dll) to compute the hash. Wibo's `ntdll.cpp` doesn't implement this function, causing a silent fallback to 0. Since the hash is over string content only (not build context), implementing this single function will fix all string hashes.

**Note on JamCRC vs standard CRC-32:** MSVC uses JamCRC (`XorOut=0`), but `RtlComputeCrc32` implements standard CRC-32 (`XorOut=0xFFFFFFFF`). The compiler likely applies the `^0xFFFFFFFF` itself after calling `RtlComputeCrc32`, or uses a different internal path. The wibo implementation should match `RtlComputeCrc32`'s standard CRC-32 behavior — the compiler handles the JamCRC conversion.

**Fix:**

```cpp
// wibo/dll/ntdll.cpp
DWORD WINAPI RtlComputeCrc32(DWORD dwInitial, const BYTE *pData, INT iLen) {
    // Standard CRC32 (ISO 3309 / ITU-T V.42)
    DWORD crc = dwInitial ^ 0xFFFFFFFF;
    for (INT i = 0; i < iLen; i++) {
        crc ^= pData[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return crc ^ 0xFFFFFFFF;
}
```

**Validation**: After implementing, recompile one `.cpp` file and check that the `??_C@` symbols have real hashes (not `A`).

**Status:** [ ] Not started. Required for 1:1 symbol matching.

**Files:**
- `wibo/dll/ntdll.cpp` — add `RtlComputeCrc32`
- `wibo/dll/ntdll.h` — add declaration

---

### 4. Match Original Build Paths (path strings → identical content)

**The problem.** `__FILE__` macro and assert strings embed the build path. Original uses `e:\lazer_build_gmc1\system\src\...`, decomp uses `z:\home\free\code\milohax\dc3-de...`. Even with correct CRC32, these strings will have different hashes because their content differs.

**Fix options:**

| Option | Complexity | Effect |
|--------|-----------|--------|
| A. Map wibo working dir to `e:\lazer_build_gmc1\` | Small | `__FILE__` paths match original |
| B. Use `/FC` flag with mapped drive | Small | Full canonical path matching |
| C. Normalize path strings in jeff | Medium | Rewrite split object strings to match decomp |

Option A: Configure wibo to use `e:` drive mapping and set the working directory to match the original build tree layout. This requires:
1. Setting up wibo's drive letter mapping (`z:` → Linux root, but we need `e:` → project root)
2. Adjusting include paths to match: `/I e:\lazer_build_gmc1\system\src` etc.
3. Or symlinking the project at the expected path

**Status:** [ ] Not started. Required for 1:1 string symbol matching.

**Files:**
- `wibo/src/files.cpp` — drive letter handling (line 95)
- `dc3-decomp/configure.py` — include path configuration
- `dc3-decomp/scripts/build/configure.sh` — build wrapper

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
| **2** | Fix wibo CRC32 (`RtlComputeCrc32`) | Correct string hashes | wibo | Small (one function) |
| **3** | Match original build paths | `__FILE__` strings match | wibo + dc3-decomp | Medium (drive mapping) |
| **4** | Investigate wine vs wibo LNK4006 | Understand 3,183 same-obj warnings | wibo | Research |
| **5** | Jeff hash normalization (fallback) | Safety net for residual mismatches | jeff | Small (string transform) |
| **6** | NODUPLICATES acceptance | ~3,183 same-obj LNK4006 (permanent) | — | Accepted |

After priority 1, the link dropped from ~10K warnings to ~3,735 (done). After priority 2, all non-path string symbols hash correctly. After priority 3, path strings also match — achieving 1:1 `??_C@` symbol matching. After all, the link needs only `/FORCE:MULTIPLE` for NODUPLICATES conflicts and cross-object template duplicates.

---

## Milestone Definitions

**M1: Fix COMDAT Regression** — DONE
- Restored `if sect.kind == ObjSectionKind::Bss { continue; }` in jeff
- LNK4006: ~9,987 → 3,735
- 0 errors, 0 unresolved, 0 fixup overflow

**M2: Drop `/FORCE:UNRESOLVED`**
- Already achievable today (0 unresolved errors)
- Remove flag from config, verify link succeeds with only `/FORCE:MULTIPLE`
- Free win — just needs testing

**M3: 1:1 String Symbol Matching**
- Wibo CRC32 implemented (#3)
- Build paths matched (#4)
- Decomp `??_C@` hashes match original `??_C@` hashes

**M4: Minimal `/FORCE`**
- Only `/FORCE:MULTIPLE` remains for NODUPLICATES conflicts
- All unresolved stay at 0
- All `??_C@` string symbols hash correctly
- Clean link state — `/FORCE:MULTIPLE` is cosmetic suppression of harmless warnings

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

M1 is done. M2 is a config change (free win). M3 requires wibo work. M4 follows from M1+M2+M3. M5 requires completing the decomp. M6 can be built any time after M2 (once the linked binary is meaningful).

---

## Key References

| Doc | Path | What it covers |
|-----|------|---------------|
| Linking Status | `docs/LINKING_STATUS.md` | COMDAT infrastructure, subsection layout, marking rules |
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
| `wibo/dll/ntdll.cpp` | — | Missing `RtlComputeCrc32` (cause of hash=0) |
| `wibo/dll/ntdll.h` | — | ntdll function declarations |
| `wibo/src/files.cpp` | 84-97 | Path conversion and drive letter handling |
| `wibo/src/modules.cpp` | 774-777 | Stub mechanism (crashes on unimplemented calls) |

## Resolved Issues

| Issue | Was | Resolution |
|-------|-----|------------|
| COMDAT regression (`cf01a80`) | LNK4006: ~9,987 | Fixed — reverted to BSS-only filter |
| `??_C@` unresolved (533) | LNK2001 errors | Resolved — 0 unresolved |
| `lbl_*` cross-unit labels (195) | LNK2001 errors | Resolved — 0 unresolved |
| Library/CRT gaps (120) | LNK2001 errors | Resolved — `link_glue.cpp` |
| `??__E*` CRT initializers (26) | LNK2001 errors | Resolved — 0 unresolved |
| `__unwind$`/`__catch$` EH (17) | LNK2001 errors | Resolved — 0 unresolved |
| `merged_*` ICF aliases (10) | LNK2001 errors | Resolved — `link_glue.cpp` |
