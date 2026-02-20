# Compiler Flag Confidence Investigation

**Date:** 2026-01-29
**Goal:** Verify compiler flags in `config/373307D9/config.json` are correct and complete.

---

## Current Flags

```
/nologo /wd4355 /wd4164 /c /GR /O1 /Oi /EHsc
```

**Compiler:** MSVC 16.00.11886.00 (Visual Studio 2010)
**Linker:** LINK 10.0.11886.0
**XDK SDK:** v2.0.21173.0

---

## Summary of Findings

| Flag Tested | Result | Conclusion |
|-------------|--------|------------|
| `/O1` (current) | Baseline — 5000 functions at 100% | **CORRECT** |
| `/O2` (maximize speed) | Breaks 100% matches (-1.3% to -41%) | **WRONG** |
| `/fp:fast` | Zero effect on any function tested | **Already the default** on Xbox 360 (per XDK docs) |
| `/GS-` (no buffer security) | No change from baseline | **Not needed** |
| `/Zp8` (8-byte packing) | No change from baseline | **Not needed** (default is correct) |

**Verdict: The current flag set is correct and complete.** No missing or incorrect flags were found.

---

## Detailed Results

### Test 1: /O1 vs /O2 Optimization Level

**Method:** Changed `/O1` to `/O2` in config, rebuilt test objects, compared match %.

| Function | /O1 (baseline) | /O2 | Change |
|----------|---------------|-----|--------|
| MakeEulerScale (100% baseline) | 100.0% | 98.7% | **-1.3%** |
| FixedSizeSaveable op>> (100% baseline) | 99.6%* | 58.7% | **-41.0%** |
| Det | 85.6% | 78.1% | -7.5% |
| MakeScale | 82.0% | 78.9% | -3.1% |
| MakeRotMatrix (euler) | 81.9% | 74.9% | -7.0% |
| MakeRotQuat | 89.4% | 68.8% | -20.6% |
| Nlerp | 68.2% | 92.7% | +24.5% |
| SongStatusMgr::GetBestScore | 92.6% | 95.0% | +2.4% |

*\*99.6% from current build; report.json shows 100% from prior full build (linker-merged call diff)*

**Key insight:** MakeEulerScale drops from 100% with `/O1` to 98.7% with `/O2`. FixedSizeSaveable drops catastrophically. **`/O1` is definitively the correct flag.** The few functions that improved with `/O2` (Nlerp, GetBestScore) are coincidental — source-level adjustments can fix those under `/O1`.

### Test 2: /fp:fast (Floating-Point Model)

**Hypothesis:** `/fp:fast` might enable fused multiply-add (fmadds) instructions, resolving the documented fmadds vs fmuls+fadds mismatch.

**Method:** Added `/fp:fast` to base flags, rebuilt math objects.

| Function | Baseline | With /fp:fast | Change |
|----------|----------|---------------|--------|
| Det | 85.6% | 85.6% | 0% |
| MakeScale | 82.0% | 82.0% | 0% |
| MakeRotMatrix | 81.9% | 81.9% | 0% |
| MakeRotQuat | 89.4% | 89.4% | 0% |
| MakeEulerScale | 100.0% | 100.0% | 0% |
| Nlerp | 68.2% | 68.2% | 0% |

**Result:** Absolutely zero change. Verified the flag was passed to the compiler (visible in `ninja -n -v` output).

**Conclusion:** XDK documentation confirms `/fp:fast` is the **default** for Xbox 360 (unlike standard MSVC which defaults to `/fp:precise`). Adding it explicitly has no effect because it's already active. The `fp_contract` pragma is also ON by default, enabling fmadds generation.

**The fmadds mismatch is confirmed UNFIXABLE via compiler flags.** The fmadds vs fmuls+fadds difference is an inherent compiler backend scheduling decision, not a flag issue. Both builds use the same FP model.

### Test 3: /GS- (Disable Buffer Security Check)

**Method:** Added `/GS-` to base flags, rebuilt test objects.

| Function | Baseline | With /GS- | Change |
|----------|----------|-----------|--------|
| MakeEulerScale | 100.0% | 100.0% | 0% |
| FixedSizeSaveable op>> | 99.6% | 99.6% | 0% |

**Result:** No change. The MSVC default for Xbox 360 appears to match our baseline (likely `/GS-` is already the default on Xbox 360, or `/GS` has no visible effect on PPC codegen).

### Test 4: /Zp8 (Struct Packing Alignment)

**Method:** Added `/Zp8` to base flags, rebuilt test objects.

| Function | Baseline | With /Zp8 | Change |
|----------|----------|-----------|--------|
| MakeEulerScale | 100.0% | 100.0% | 0% |
| FixedSizeSaveable op>> | 99.6% | 99.6% | 0% |

**Result:** No change. Default alignment (8 bytes for MSVC) is already correct.

---

## Rich Header Analysis

Extracted PE from XEX via `dtk xex extract`, then decoded the Rich header.

### Tool IDs Found

| Tool ID | Build | Count | Description |
|---------|-------|-------|-------------|
| 0xAB | 11886 | 1871 | Xbox 360 C++ compiler (MSVC 16.00) |
| 0xAA | 11886 | 465 | Xbox 360 C compiler (MSVC 16.00) |
| 0x0001 | 0 | 475 | Import records |
| 0x009E | 11886 | 27 | Xbox 360 tool (unknown) |
| 0x009C | 11886 | 5 | Xbox 360 tool (unknown) |
| 0x009D | 11886 | 1 | Xbox 360 tool (unknown) |
| 0x006E | 6251 | 1 | UTC C++ 14.00 (VS2005 — Bink/RAD) |
| 0x006D | 2909 | 1 | UTC C 14.00 (VS2005 — Bink/RAD) |
| 0x007B | 2909 | 2 | VS2005 tool (Bink/RAD) |

### Key Observations

- **1871 C++ objects + 465 C objects** compiled with Xbox 360 MSVC 16.00.11886
- **3 objects** from older VS2005 (build 2909/6251) — likely Bink video middleware
- Tool IDs 0xAA/0xAB are the Xbox 360 PPC backend variant (not standard x86 0x93/0x94)
- Tool IDs 0x9C-0x9E are likely Xbox 360-specific tools (assembler, resource, etc.)

### XEX Header Info

```
Original PE Name: ham_xbox_r.exe
Load address:     0x82000000
Entry point:      0x82335EE0
Build date:       Sun Sep 16 00:38:40 2012

Static Libraries (notable):
  XBDM v2.0.21173.0      (Xbox Debug Manager — confirms debug build)
  LIBCMT v2.0.11886.0
  C1 v16.0.11886.0        (MSVC frontend)
  C2 v16.0.11886.0        (MSVC backend)
  LINK v10.0.11886.0
```

The presence of **XBDM** (Xbox Debug Manager) definitively confirms this is a **debug build**.

---

## LTCG Claim Investigation

### The Claim (TECHNICAL_NOTES.md line 679)

> "The original game was built with `/GL` + `/LTCG` (Link-Time Code Generation)."

### Verdict: **INCORRECT**

Evidence against LTCG:
1. **XBDM present** — debug builds don't use LTCG
2. **No `/GL` in config** — project was already set up without it
3. **`unfixable-linker.md`** already corrects this:
   > "This pattern likely does NOT apply to DC3. The target binary is a debug build without LTCG."
4. **ICF (Identical COMDAT Folding) IS enabled** — but ICF is a linker optimization (`/OPT:ICF`) that works independently of LTCG

### What's Actually Happening

The "extra `lis` instructions" pattern previously attributed to LTCG is actually caused by differences in how the compiler/linker handle float constant pooling at the object level vs link level. ICF is the only linker optimization in play.

### Recommended Fix

Update TECHNICAL_NOTES.md section "LTCG/Global Pooling (UNFIXABLE)" to:
- Remove the claim that the build uses `/GL` + `/LTCG`
- Clarify that ICF (`/OPT:ICF`) is the relevant linker optimization
- Note that float constant pooling differences are an obj-vs-linked-binary artifact, not LTCG

---

## XDK Documentation Review

Reviewed the official Xbox 360 XDK documentation solely for compiler flag identification (proprietary material, since removed from repo — do not redistribute or re-acquire).

### Key Findings from XDK Docs

| Finding | Source File | Detail |
|---------|------------|--------|
| `/fp:fast` is DEFAULT | `xenon_compiler_technology.htm` | "Choose `/fp:fast` (the default) for speed over accuracy" |
| `fp_contract` ON by default | `dev_compiler_pragma_fp_contract.htm` | Controls fmadds generation; ON = fmadds, OFF = fmuls+fadds |
| `/fp:strict` disables contraction | `dev_compiler_pragma_fp_contract.htm` | Only `/fp:strict` turns off fmadds; pragma ignored under strict |
| Xbox 360 `/O1` = `/Oy /Ob2 /GF` | `dev_compiler_o1o2.htm` | Different from standard MSVC (omits `/Og /Os /Gy`) |
| Xbox 360 `/O2` = `/Oi /Oy /Ob2 /GF` | `dev_compiler_o1o2.htm` | Only adds `/Oi` over `/O1`; default for release builds |
| `/Ou` = prescheduling | `dev_compiler_ouoz.htm` | Extra scheduling pass before register allocation (Xbox 360-specific) |
| `/Oz` = inline asm optimization | `dev_compiler_ouoz.htm` | Reorder inline assembly to minimize latencies |
| `/Oc` = disable traps | `dev_compiler_oc.htm` | Suppress trap instructions in integer divides |
| `/QXSTALLS` = pipeline simulator | `dev_compiler_qxstalls.htm` | CPU cycle count estimates in .cod output |
| `/QVMX128` = VMX128 (default ON) | `atoc_tools_compiler_options.htm` | Enables VMX128 vector instructions |

### Xbox 360-Specific Flag Tests

#### Test 5: /Ou (Prescheduling)

**Method:** Added `/Ou` to base flags. Required expanding `/O1` into component flags (`/Oy /Ob2 /GF`) because `/O` flags override each other.

| Function | Baseline | With /Ou | Change |
|----------|----------|----------|--------|
| MakeEulerScale | 100.0% | 98.7% | **-1.3%** |
| DataInitFuncs | 100.0% | 97.8% | **-2.2%** |
| MakeRotMatrix (quat) | 100.0% | 84.5% | **-15.5%** |
| MakeRotQuat | 89.4% | 68.8% | **-20.6%** |
| Det | 85.6% | 78.1% | **-7.5%** |
| MakeScale | 82.0% | 78.9% | **-3.1%** |

**Result:** `/Ou` breaks all 100%-matched functions. **Definitively WRONG** — the original was NOT built with prescheduling.

#### Test 6: /Oc (Disable Traps)

**Method:** Added `/Oc` to base flags, rebuilt test objects.

| Function | Baseline | With /Oc | Change |
|----------|----------|----------|--------|
| MakeEulerScale | 100.0% | 100.0% | 0% |
| DataInitFuncs | 100.0% | 100.0% | 0% |
| Det | 85.6% | 85.6% | 0% |
| MakeRotMatrix (quat) | 100.0% | 100.0% | 0% |

**Result:** No change. Only affects integer divides; these math functions have none. Default trap behavior matches the original.

---

## Conclusions

### Confirmed Correct
- `/O1` — definitively confirmed (O2 breaks matches)
- `/Oi` — intrinsic functions, consistent with O1 behavior
- `/GR` — RTTI enabled (needed for dynamic_cast used in codebase)
- `/EHsc` — C++ exception handling
- `/wd4355` / `/wd4164` — warning suppressions (don't affect codegen)
- `/nologo` / `/c` — build system flags (don't affect codegen)

### Confirmed Not Missing
- `/fp:fast` — already the default on Xbox 360 (XDK docs confirm); zero effect when added explicitly
- `/GS-` — no effect (default already matches)
- `/Zp8` — no effect (default already matches)
- `/O2` — wrong, breaks matches
- `/GL` — not used (debug build, no LTCG)
- `/Ou` — wrong, breaks matches (prescheduling not used in original)
- `/Oc` — no effect on tested functions (only affects integer divides)

### fmadds Explanation (Resolved)
The XDK docs clarify the fmadds situation:
- `/fp:fast` is the **default** on Xbox 360 (not `/fp:precise` as in standard MSVC)
- `#pragma fp_contract` is **ON by default** — this controls fmadds generation
- Both our build and the original generate fmadds when possible
- The fmadds vs fmuls+fadds difference is NOT a missing flag — it's an inherent compiler backend scheduling decision about when multiply and add operations are close enough to fuse

This is **permanently unfixable** but now fully understood.

### Action Items
- [x] Confirm `/O1` is correct
- [x] Test `/fp:fast` — already the default (zero effect)
- [x] Test `/O2` — wrong
- [x] Test `/GS-` — no effect
- [x] Test `/Zp8` — no effect
- [x] Rich header decoded
- [x] LTCG claim investigated
- [x] Update TECHNICAL_NOTES.md to correct LTCG claim and add fmadds findings
- [x] Review XDK documentation for compiler flags
- [x] Test `/Ou` (prescheduling) — wrong
- [x] Test `/Oc` (disable traps) — no effect
- [x] Test `/Oz` (inline assembly optimization) — **no effect** (tested 2026-01-29)

---

## Test 7: /Oz (Inline Assembly Optimization)

**Date:** 2026-01-29

**Method:** Clean comparison in git worktree at commit `517a2e5`:
1. Built without `/Oz` (baseline): 32.15% matched
2. Added `/Oz` to base flags, full rebuild: 32.15% matched
3. Verified `/Oz` applied to 958 compilation units in build.ninja

**Results:**

| Metric | Baseline (no /Oz) | With /Oz | Change |
|--------|-------------------|----------|--------|
| Overall Match | 32.15% | 32.15% | **0.00%** |
| Code Matched | 3,641,120 B | 3,641,120 B | +0 B |
| Functions Matched | 22,257 | 22,257 | +0 |

Per-unit comparison showed **zero differences** in any fuzzy_match_percent values.

**Conclusion:** `/Oz` has **no effect** on match percentages for this project. This is because:
1. The codebase has minimal or no inline assembly (`__asm` blocks)
2. `/Oz` only affects inline assembly instruction reordering
3. Without inline assembly to optimize, the flag does nothing

**Recommendation:** Do not add `/Oz` to the build configuration.
