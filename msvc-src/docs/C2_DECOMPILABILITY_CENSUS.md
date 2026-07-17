# c2.dll Ghidra Decompilability Census

**Type:** measurement readout (no port, no source edits).
**Question:** if we attempted a clean-room native re-implementation of the MSVC
Xbox 360 PPC back-end (`c2.dll`), how good is the raw material Ghidra gives us —
what fraction of the binary comes back as structured, portable C, and where is the
recovery hard?
**Date:** 2026-07-10. **Reproduce:** `msvc-src/tools/c2_census.py`
(→ `msvc-src/results/c2_census.json`).

## Method

Opened `build/compilers/X360/16.00.11886.00/c2.dll` (v16.00.11886.00, build 78379,
image base `0x10B00000`, 1,347,072 B, PE32 x86, no PDB) in-process via pyghidra
(Ghidra 12.2 fork), ran full auto-analysis, then ran Ghidra's decompiler over
**every** discovered function (90 s/function timeout) and scored the emitted C text
for recovery-quality artifacts. Whole run: analysis + 4,916 decompilations in **86 s**.

Per-function signals scored over the decompiled C:
- **goto / label** density — Ghidra falls back to `goto` when it can't structure the CFG.
- **phantom regs** (`in_EAX`, `unaff_*`, `extraout_*`, `in_stack_*`) — invented reads
  that betray imprecise calling-convention / register recovery on optimized code.
- **unrecovered switch** (`switchD_*`, `unrecovered_jumptable`) — jump tables Ghidra
  could not fully rebuild.
- **bad disasm** (`halt_baddata`, `code_r0x*`, `BADSPACEBASE`).
- decompiler **`/* WARNING:` comments**, indirect/computed calls, bit-slice pseudo-ops
  (`CONCAT`/`SUBn`/`ZEXT`/`SEXT`), `undefinedN` typing, cast churn, structural keywords.

Each function is bucketed **clean / fair / poor / failed** (thresholds in
`c2_census.py::classify`). `poor` = any unrecovered switch or bad disasm, or
`goto ≥ 8`, or `phantom ≥ 6`, or `indirect ≥ 4`.

## Headline results

| Metric | Value |
|---|---|
| Functions discovered by Ghidra | **4,916** |
| Decompilation completed | **4,916 / 4,916 (100%, 0 failures)** |
| Total decompiled C | **~216,000 non-blank lines** |
| Named by Ghidra (exports + CRT/intrinsics) | 47 — everything else is `FUN_*` |
| Thunks | 60 |

**Recovery buckets** (by function count vs. by C-line volume):

| Bucket | Funcs | % funcs | C-lines | % C-lines |
|---|---|---|---|---|
| clean | 3,447 | 70.1% | 82,887 | 38.4% |
| fair  | 1,128 | 22.9% | 75,177 | 34.8% |
| poor  |   341 |  6.9% | 57,898 | 26.8% |
| failed|     0 |  0.0% | 0 | 0.0% |

**The complexity is concentrated and inverted against function size.** The 6.9% of
functions in `poor` hold 26.8% of the code; `poor` functions have a median of 606 B
vs. 89 B for the binary overall (clean-bucket median: 62 B). By volume, **645 functions
(13%) hold half the C, 1,957 (40%) hold 80%** — the classic optimizing-compiler shape:
a large tail of tiny leaf/helper routines plus a small core of very large, heavily
optimized driver functions.

**Artifact prevalence** (over the 4,916 decompiled):

| Signal | Funcs | Note |
|---|---|---|
| any `goto` | 1,031 (21%) | 4,190 gotos total; 125 funcs ≥ 8 |
| any phantom reg | 716 (15%) | cc/regalloc recovery noise |
| any decompiler WARNING | 555 (11%) | |
| unrecovered switch table | 34 (0.7%) | concentrated in dispatchers |
| bad-disasm / halt | 38 (0.8%) | |
| indirect `(*(code*))` call | 29 (0.6%) | |
| structurally clean (0 goto, 0 phantom, 0 warning) | **3,210 (65%)** | |

Calling conventions (Ghidra's guess): 4,266 `__fastcall`, 410 `__stdcall`,
164 `__thiscall`, 65 `__cdecl`, 11 unknown. `undefinedN` typing appears in 3,712
functions but is a weak signal — `undefined4` is Ghidra's default int placeholder.

**The hard core.** The largest functions are almost all `poor` and are exactly the
subsystems `PLAN.md` targets. Examples:

| addr | bytes | insns | C-lines | goto | sw | bucket | likely role |
|---|---|---|---|---|---|---|---|
| `0x10c027d3` | 8,796 | 2,918 | 1,380 | 168 | 0 | poor | monster driver (goto-soup) |
| `0x10bf7c59` | 4,569 | 1,232 | 1,243 | 146 | 26 | poor | switch-heavy dispatcher |
| `0x10bf9f15` | 3,861 | 1,225 | 649 | 104 | 35 | poor | switch-heavy dispatcher |
| `0x10b943ea` | 5,110 | 1,539 | 794 | 71 | 0 | poor | phantom-reg heavy (60) |
| `0x10bc2d7a` | 5,080 | 1,490 | 840 | 34 | 11 | poor | (COLOR-region addr range) |

## Feasibility read

**Positive signals for a clean-room native port:**
1. **100% decompile completion, 0 failures, in 86 s.** c2.dll is a stock x86 PE;
   Ghidra handles it with no bad-CFG blowups. Nothing in the binary is opaque to the
   decompiler — there is no packing, no anti-analysis, no exotic ISA. (Contrast the
   PPC/VMX128 target side, which needed a custom Ghidra fork.)
2. **~65% of functions come back structurally clean** and **70% land in the `clean`
   bucket** — small, single-purpose, `goto`-free C that a human could port with only
   type/naming cleanup.
3. **Effort is boundable and reproducible.** ~216k lines total; the census regenerates
   in ~90 s, so recovery quality can be re-measured after every structuring/type-import
   improvement.

**Cost / risk signals:**
1. **Scale is ~3.4× the working estimate.** The docs (`PLAN.md`, `FINDINGS.md`) carry
   "~1,430 functions"; Ghidra auto-analysis finds **4,916**. Even discounting the
   ~955 tiny (≤32 B) leaves and 60 thunks, the real body count is far larger than the
   prior planning number. **`PLAN.md`/`FINDINGS.md` should be updated to 4,916.**
2. **No symbols.** 4,869 of 4,916 functions are anonymous `FUN_*`; the PDB is not on
   Microsoft's symbol server. Every non-CRT function needs manual identification. This,
   not decompiler failure, is the dominant cost of a *full* port.
3. **The 27% of code that is `poor` is the code that matters** — the largest functions
   (instruction selection, pass dispatch, register allocation) are precisely the
   goto-soup / unrecovered-switch cases. A *full byte-faithful* port would spend most of
   its effort on this minority. Jump-table reconstruction (34 funcs) and CFG structuring
   of the ~125 goto-heavy functions are the concrete Ghidra-side work items.

**Verdict (measurement, not a go/no-go):** A full clean-room port remains a multi-
person-year effort — bounded not by decompiler capability (which is excellent here:
zero failures, 65% clean) but by **symbol recovery across ~4,900 anonymous functions**
and by **CFG/jump-table structuring of the ~340-function large-driver core that holds a
quarter of the code.** This directly supports `PLAN.md`'s existing posture: *targeted
subsystem RE is very feasible* — the COLOR/inliner/peephole subsystems are ~50–200
mostly-`clean` functions each with thousands of test pairs — whereas a whole-binary
rebuild is dominated by the concentrated `poor` core, not by breadth.

## Caveats

- Buckets score **recovery cleanliness**, not correctness. Ghidra can emit clean-looking
  C that is subtly wrong (the standard reloc-masked / signedness hazards this repo
  tracks elsewhere). This census measures *portability of the raw material*, not
  behavioral fidelity — that would require the compiler-as-judge loop, which does not
  apply to a Windows x86 DLL with no build.
- Ghidra's 4,916 is the auto-analysis function count; a few may be spurious splits or
  data misclassified as code (the 38 halt/bad-disasm cases). Treat 4,916 as an upper
  bound, ~4,850 as the working function count after thunks.
- `__fastcall`-dominant convention is Ghidra's heuristic for register-passing optimized
  x86, not ground truth; it inflates phantom-reg counts on genuinely `__cdecl`/vararg
  functions.

## Artifacts

- `msvc-src/tools/c2_census.py` — the census (reproducible, ~90 s).
- `msvc-src/results/c2_census.json` — summary + per-function table (slimmed to
  load-bearing columns; full artifact sub-counts regenerate from the tool).
