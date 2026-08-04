---
name: stack-layout
description: Diff stack-frame layouts between target and base for a function. Labels base-side slots with source variable names from a MSVC /Z7 CodeView recompile. Identifies SWAPPED pairs (decl-reorder candidates), SHIFTED slots, DIFFER (different variables in same slot), and TGT_ONLY / BASE_ONLY (extra/missing locals). Filters out callee-save slots.
argument-hint: "<symbol_name> [-u <unit>]"
allowed-tools: Bash(python3 scripts/analysis/stack_layout.py *), Bash(python3 scripts/analysis/codeview_locals.py *), Bash(python3 scripts/analysis/diff_inspect.py *), Read, Grep, Glob
---

# Stack Layout Diff Skill (MSVC X360)

Compare stack-frame layouts between target and our build for a single function.
Returns frame size and callee-save counts at the top, then a per-slot diff table
with verdicts that point to specific source fixes.

## Arguments

`$ARGUMENTS` — the symbol (MSVC mangled `?Name@Class@@QAEHXZ` or demangled
`Class::Method(args)`). Append `-u <unit>` if ambiguous (e.g.
`-u default/system/hamobj/HamCharacter`).

## Steps

1. **Run the diff**:
   ```bash
   python3 scripts/analysis/stack_layout.py --symbol "$ARGUMENTS" --project-dir .
   ```

2. **Read the prologue summary** at the top:
   - `Frame Δ` and `Callee-saved GPR/FPR Δ`
   - On MSVC X360, GPRs are 64-bit (`std`) — 8 bytes per saved GPR.
   - If frame Δ is **fully explained by callee-save counts** → AT_LIMIT
     (not source-fixable).
   - Otherwise the structural Δ remainder is the real lever.

3. **Read the verdict table**. Rows are sorted most-actionable first:

   | Verdict | Meaning | Action |
   |---|---|---|
   | **SWAPPED** | Two slots' fingerprints exchanged | Reorder the two declarations |
   | **DIFFER** | Same offset, different fingerprint | Different variable lives there — decl-reorder |
   | **PERMUTED** | Same offset, same fingerprint, but the two sides touch it at **different program points** | Same slot *set*, variables assigned differently — MSVC slot-allocation shaping. Read the `↔ base 0x..` note for the mapping. **Not** a missing/extra local. |
   | **SHIFTED** | Same fingerprint, offset differs by the dominant Δ | One side has an extra local pushing the rest |
   | **TGT_ONLY** | Slot exists only on target | Target spills a temp we keep in a register (or vice versa) |
   | **BASE_ONLY** | Slot exists only on our build | Extra spill; usually a register-pressure symptom |
   | **MATCH** | Same offset, same fingerprint, **and** same aligned access rows | Hidden by default; pass `--show-equal` to see |

   ⚠ **`MATCH` did not always mean this.** Before 2026-08-04 a row was MATCH
   whenever the offset and the `(kind,size,loads,stores)` fingerprint agreed. For
   a run of same-typed locals that fingerprint is **constant**, so a pure
   permutation of variables across identically-shaped slots read as MATCH.
   Measured on **this** binary over **N = 1,909** functions that have at least one
   exact-offset paired user slot (drawn from all 2,236 partial-match functions in
   units with a base `.obj`): **475 (24.9%)** had at least one false MATCH, and
   **2,512 of 12,917 MATCH rows (19.4%)** moved MATCH → PERMUTED.
   `ArcDetector::UpdateOverlay` went from "MATCH 88" to "9 MATCH / 75 PERMUTED".
   Any pre-2026-08-04 stack-layout reading of "slots all match" should be re-run
   before being trusted.

   Read the **signature discriminating power** line under the summary: it says how
   many target slots share a fingerprint with another. Where that number is high,
   any *fingerprint-based* pairing (SWAPPED, SHIFTED) is arbitrary within the
   group and is flagged `⚠ ambiguous`. On `ArcDetector::UpdateOverlay` it is
   94 of 103, largest group 82.

3b. ** LOUD: BEHAVIOUR CHANGE OTHER LANES MAY DEPEND ON. ** `r31` is now counted
   as a frame base **only when the prologue derives it from r1**. It previously
   counted unconditionally — but on this corpus r31 is the frame base in only
   1,186 of 2,236 target prologues (53.0%); the other 47.0% it holds an incoming
   object pointer, so `lwz r3, 0x50, r31` is a **class member** load. **682
   functions (30.5%) were having class layout tabulated into their stack report
   — 3,515 phantom slots**, now gone. Row counts across the corpus drop from
   19,769 to 16,055.

3c. **Frame size can now REFUSE.** If the prologue cannot be decoded the tool
   prints `UNKNOWN` (never `0x0`) and exits **2** with no frame verdict, because
   the callee-save slot filter is derived from the frame size. Pass
   `--allow-unknown-frame` to force exit 0. The refusal path fires on **0 of
   2,236** functions today — it is insurance, not something catching anything now.

3d. **Callee-save counts were fabricated on bare save helpers.** `bl __savegprlr`
   with no `_NN` scored 0 saved registers, so a target rendered with the FUNCTION
   symbol read 0 against a base rendered with the LABEL symbol reading 18.
   `config/373307D9/symbols.txt` settles it: `__savegprlr` and `__savegprlr_14`
   are both `.text:0x8299D8F0`, so bare == `_14`. **70 functions** in the corpus
   have this asymmetry; `ArcDetector::UpdateOverlay` went from
   "Callee-saved FPRs: TGT 0 BASE 18 Δ +18" to "TGT 18 BASE 18 Δ +0", which also
   corrects its frame-Δ attribution.

   Self-check with no toolchain, objdiff or filesystem needed:
   ```bash
   python3 scripts/analysis/stack_layout.py --selftest   # expect PASS, 28 checks
   ```

4. **Fingerprint columns** (`kind sz=N L=loads S=stores A=accesses [first..last]`):
   - `float sz=4` → `float`
   - `float sz=8` → `double` / paired-single store
   - `int sz=4` → `int`, pointer, `bool`, or 32-bit member
   - `int sz=8` → 64-bit `std`/`ld` (frequent on Xenon)
   - `addr sz=0` → an `addi rN, r1, off` taking address-of

5. **"base var" column** — the source variable name our build allocates at that
   offset, extracted from a MSVC `/Z7` CodeView recompile. Use it to identify
   exactly which declaration to reorder.

## When to Use

- `run_objdiff` output flagged `**Stack:** frame Δ ... | N SWAPPED ...`
- Function diff shows many `[off:+N]` annotations
- Frame sizes don't match between target and our build
- You suspect a declaration reorder is the fix

## Output knobs

- `--no-names` — skip CodeView recompile + name extraction
- `--show-equal` — include MATCH rows
- `--show-callee-save` — include prologue/epilogue callee-save slots (hidden by default)
- `--json-file <path>` — skip objdiff invocation; load diff JSON from cached path
- `--allow-unknown-frame` — exit 0 instead of 2 when the frame size could not be determined
- `--selftest` — run the in-memory regression fixtures (no toolchain, no objdiff, no filesystem) and exit

## How name extraction works

The tool recompiles the function's source file with `/Z7` to embed CodeView
records in the `.debug$S` COFF section, parses `S_REGREL32` records, and maps
each register-relative variable to a frame-offset → name pair. Cached at
`/tmp/claude/stack_codeview/<base>.<hash>.cv.obj` by source mtime + cflags
hash; second runs are ~0.5s.

Frame-register handling: CodeView records `reg=2` (= PPC r1) or `reg=32`
(= PPC r31). MSVC X360 commonly aliases r31 to `new r1` via
`subi r31, r1, FRAMESIZE` before `stwu`, so r31-relative offsets equal
r1-relative offsets after frame allocation. The tool accepts both.

Limits:
- **Base side only**: there's no debug build oracle for the target. TGT_ONLY
  rows show no name.
- **Compiler temps unnamed**: `/O1` strips many locals into registers — empty
  "base var" cell ≠ "unknown variable" — it's "no source declaration."
- **Same name in nested scopes**: deeper scope wins via min-depth merging.

## Detection limits

- Callee-save detection handles:
  - `bl __savegprlr_NN` / `bl __savefpr_NN` helpers
  - Manual pre-stwu saves: `stw r12, -8, r1`, `std rN, -off, r1`, `stfd fN, -off, r1`
  - Post-stwu `stmw rN, off, r1` (rare on X360)
- Unusual prologue shapes may under- or over-count; verify against the asm if
  the frame summary looks off.

## MSVC X360 prologue cheat-sheet

```
mflr r12                              ; LR -> r12 (not r0 like Wii MWCC!)
bl __savegprlr_NN                     ; r12->-8(r1), r31..rNN std at -16, -24, ...
stfd f31, -0x?, r1                    ; manual FPR save (pre-stwu, NEGATIVE offset)
subi r31, r1, FRAMESIZE               ; optional: r31 = new r1 (frame-ptr alias)
stwu r1, -FRAMESIZE, r1               ; allocate frame
... body code (uses r1 or r31 base) ...
addi r1, r1, FRAMESIZE                ; deallocate
b __restgprlr_NN                      ; restore + return
```

After `stwu`, the saved-register slots end up at `frame_size - 8` (LR) down to
`frame_size - 8 - 8*(saved_count-1)`.

## Tips

- After a declaration reorder, re-run to confirm SWAPPED rows resolve.
- `Dominant body-offset shift` is reported separately from frame Δ — the
  dominant shift is what SHIFTED rows are normalized against.
- If verdicts are all DIFFER with no clean SHIFT/SWAP, the function is
  mid-reflow; try `/compare-asm` or `run_diff_inspect mode=diagnose`.
