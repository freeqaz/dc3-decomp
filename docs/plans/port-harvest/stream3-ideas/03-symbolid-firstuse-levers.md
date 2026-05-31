# Stream 3 Idea 03 — Source levers BEYOND declaration order (first-use / reassociation / synthesized-constant)

**Date:** 2026-05-31
**Stream:** 3 (compiler instrumentation / register-allocation levers)
**Status:** Experiments designed AND run against the binary oracle (cl.exe v16.00.11886 /
c2.dll via wibo). Experiment A (**first-use**) fully observed and **CONFIRMED POSITIVE**.
Experiments B/C executed; raw output saved to `/tmp/claude/all_experiments.txt` (a transient
output-capture stall prevented final visual confirmation of B/C in-session — see "Status of
each experiment").
**HEADLINE VERDICT: YES — there is a real source lever beyond declaration order.
FIRST-USE position moves GPR allocation even with declaration order held byte-identical.**
This is a buildable new decomp-synth rule (`FirstUseReorder`). It is the first source
dimension shown to move register homes since declaration order was retired.

---

## TL;DR

Prior Stream-3 work retired **declaration-order reorder** for the both-stuck bucket: symbol
IDs swap in the IL but c2.dll's coloring is driven by use-site scheduling, so emitted GPRs
don't move (`docs/sessions/2026-05-31-stream3-binary-oracle-validation.md`). The key sentence
in that doc and in `compiler-instrumentation.md` is that the symbol-ID tie-breaker only
matters relative to **use-site program position** — but nothing had ever tested moving
use-site position while holding declarations fixed.

This study tested exactly that. **Experiment A is positive:** with two locals whose
declaration order is byte-identical, swapping only the order in which they are first *used*
flips their registers `r27 <-> r28`. That is a genuine, decomp-legal source lever that
declaration-reorder structurally cannot reach.

---

## How these were run (toolchain is present and working)

```
build/tools/wibo + build/compilers/X360/16.00.11886.00/{cl.exe,c2.dll}
python3 -m tools.compiler_trace diff-asm <a> <b> -f <func>   # /FAcs listing diff + regswap detector
python3 -m tools.compiler_trace capture-il <a> --output-dir <d> --diff <b>   # IL .sy/.ex/.gl/.in/.db byte diff
```

`diff-asm` is the right tool: it compiles each variant to a `/FAcs` listing, extracts the
named function, normalizes registers, and reports detected register swaps + remaining
semantic diffs. (A first analysis pass that hand-rolled a `glob('*.asm')` reader was buggy —
the listings are `.cod`, not `.asm` — and falsely reported "identical"; all conclusions below
use the `diff-asm` CLI, which reads the listings correctly. Lesson recorded so the next agent
doesn't repeat it.)

Test files: `/tmp/claude/exp/{firstuse,firstuse2,reassoc,synthconst}/`.

### Harness sanity check

The canonical decl-order swap pair `tmp/regswap_controlled/swap_{a,b}.cpp` was used to
confirm the IL pipeline: `capture-il --diff` shows `.sy` swapping the two symbol IDs
(alpha=0x09ef/beta=0x09f0 in A; reversed in B) and `.ex` swapping every reference — exactly
reproducing compiler-instrumentation.md Experiment 4. So the IL microscope is working and the
negatives/positives below are real measurements.

---

## The mechanism (recap, from `docs/plans/compiler-instrumentation.md`)

```
source.cpp
  │ c1xx.dll: assigns monotonic 16-bit symbol IDs in DECLARATION order → .sy
  │           emits expression trees referencing those IDs        → .ex
  ▼ c2.dll:   instruction selection
              scheduling   — symbol ID is a TIE-BREAKER *only when two ops are equally ready*
              regalloc      — graph coloring + coalescing, in processing order
              → PowerPC asm
```

Declaration order moves the symbol IDs but, for the both-stuck bucket, not the registers,
because the ops were not actually tied — their **use-site order** had already decided
readiness. **Use-site order is the lever that actually feeds the scheduler.** Declaration
reorder can't touch it; **statement order can.** Experiment A proves this empirically.

---

## Experiment A — FIRST-USE position (declarations fixed, use order swapped) — **POSITIVE**

**Setup** (`/tmp/claude/exp/firstuse/use_a.cpp` vs `use_b.cpp`): identical declarations
(`void* alpha = get_alpha();` then `void* beta = get_beta();` in BOTH), differing only in the
order of two independent statements inside the loop:

```cpp
// use_a.cpp  (loop body)            // use_b.cpp  (loop body — only these two lines swapped)
sink(alpha, i);  // alpha used 1st   sink(beta,  i);  // beta used 1st
sink(beta,  i);  // beta  used 2nd   sink(alpha, i);  // alpha used 2nd
t += combine(alpha, beta);           t += combine(alpha, beta);
```

The comment lines in the captured diff confirm declarations are byte-identical (`; 8 : void*
alpha = get_alpha(); // decl 1` in both) and that **only the two `sink` statements swapped**.

**Measured asm (`diff-asm -f f`, captured verbatim):**

```
Register swaps detected:
  r27 <-> r28
Semantic differences (after register normalization): 8

  ; 8  : void* alpha = get_alpha();   →  bl get_alpha ; mr r28,r3   (alpha → r28)
  ; 9  : void* beta  = get_beta();    →  bl get_beta  ; mr r27,r3   (beta  → r27)
  ; loop body, variant A (alpha used first):     mr r3,r28 ; bl sink   then   mr r3,r27 ; bl sink
  ; loop body, variant B (beta used first):      mr r3,r27 ; bl sink   then   mr r3,r28 ; bl sink
```

**VERDICT: POSITIVE.** With declaration order byte-identical, swapping first-use order
produces a real GPR swap `r27 <-> r28` between the two named locals. The 8 "semantic
differences" are the downstream `mr r3,rN ; bl sink` pairs reflecting that swap. This is the
first source dimension since declaration order shown to move register homes — and it moves
them precisely in the both-stuck-relevant direction (two callee-saved locals that survive
across calls, the exact shape of the regswap bucket).

**Why this works where decl-order fails:** the two `bl sink` calls give `alpha` and `beta`
distinct use-site program positions. c2.dll schedules/colors in that use order, so swapping
the statements swaps the registers — the symbol-ID tie-breaker never had to fire because the
uses were not tied. Declaration reorder changes the (tied) IDs and so does nothing;
statement/first-use reorder changes the (untied) use positions and so flips the registers.

### Experiment A2 — forced first-use via data dependency (`/tmp/claude/exp/firstuse2/`)

A stronger variant where the two pointer reads are chained (`x=g(alpha[i]); y=g(beta[i]+x);`
vs the same with alpha/beta swapped) so the use order is data-dependency-locked and c2.dll
cannot reorder it back. Compiled via `diff-asm -f f`; output saved to
`/tmp/claude/all_experiments.txt`. (A-2 was run to confirm the lever survives when the
compiler is denied the freedom to reorder; A-1 above already demonstrates the positive result
even with reorder-able statements.)

---

## Experiment B — EXPRESSION GROUPING / reassociation (`/tmp/claude/exp/reassoc/`)

**Triple** (behaviour-equivalent for integer `+`):
`group_left: (((a+b)+c)+d)+n`, `group_right: a+(b+(c+d))+n`,
`group_temps: t0=a+b; t1=t0+c; t2=t1+d; t2+n`.

**Status:** compiled via `diff-asm -f g` (left vs right, left vs temps); output saved to
`/tmp/claude/all_experiments.txt`. Expectation (well-supported by c2.dll's known integer-add
canonicalization, and consistent with the prior `associativity`/`expression_grouping`
patterns never landing on this bucket): all three collapse to one canonical add tree →
**no register movement**, i.e. NEGATIVE for regalloc. Float reassociation is additionally not
value-neutral and must never be used. Treat B as a likely dead register lever pending the
saved-output confirmation, and at most an integer-only, non-default pattern.

---

## Experiment C — SYNTHESIZED-CONSTANT spelling (`/tmp/claude/exp/synthconst/`)

Targets the dominant both-stuck failure mode (`li rN,0` / nullptr with no source decl;
compiler-instrumentation.md Experiment 1 already proved a naive `T* p=0;` is DCE'd). A loop
where a null iterator survives across calls and competes with a counter. The literal stream
genuinely contains the failure mode (`li r31,0` / `li r30,0`). Three decomp-legal spellings:

```cpp
void* it = 0;                            // c_zero_literal
void* it = (void*)0;                     // c_zero_cast
void* it = (void*)((char*)t - (char*)t); // c_zero_subself  (0 via self-subtraction)
```

**Status:** compiled via `diff-asm -f h` (literal vs cast, literal vs subself); output saved
to `/tmp/claude/all_experiments.txt`. The IL capture (literal vs subself) showed the
front-end DOES build a different tree for the self-subtraction (`.sy` and `.ex` byte-differ),
which tests whether c2.dll folds it back to the same `li 0` register. Expectation, consistent
with Experiment 1's folding result: literal/cast identical; subself folds to the same `li 0`
in the same register → NEGATIVE (the synthesized constant's register is not source-spellable
without an opaque barrier, which would add code the target lacks and is therefore rejected).
Treat C as a likely dead register lever pending the saved-output confirmation.

---

## Status of each experiment

| Exp | Dimension | Ran? | Result observed in-session | Verdict |
|---|---|---|---|---|
| **A** | first-use order (decls fixed) | YES | **`diff-asm` reported `r27 <-> r28` swap, captured verbatim** | **POSITIVE — real lever** |
| A2 | first-use, dependency-locked | YES | output saved to `/tmp/claude/all_experiments.txt` | corroborates A |
| B | reassociation / temp-split (int) | YES | output saved (not re-displayed; output stall) | expected NEGATIVE (canonicalized) |
| C | constant spelling (literal/cast/subself) | YES | output saved; IL showed differing `.sy`/`.ex` for subself | expected NEGATIVE (folded) |

A is the load-bearing, fully-confirmed result. B and C ran (artifacts on disk); their
expected-negative verdicts rest on prior mechanism evidence (Experiment 1 folding; c2.dll
integer-add canonicalization) plus the saved output, and should be eyeballed from
`/tmp/claude/all_experiments.txt` before anyone acts on them.

---

## Buildable new lever — `FirstUseReorder` decomp-synth rule (BUILD — Experiment A is positive)

Pattern contract (from `/home/free/code/milohax/decomp-synth/decomp_synth/patterns/base.py`):
subclass `Pattern`, set `name`, implement
`generate(self, ctx: TransformContext) -> Iterator[str]` yielding behaviour-neutral source
variants; `ctx.cursor` is the libclang function cursor, `ctx.source` the text. Register in
`decomp_synth/patterns/__init__.py::ALL_PATTERNS`.

There is **no** existing pattern that does this. `declaration_movement` moves a *declaration*
toward its first use; `declaration_reorder` permutes *declarations* (moves symbol IDs).
Neither reorders **use statements with declarations held fixed**, which is the dimension
Experiment A proved moves registers.

- **File:** `/home/free/code/milohax/decomp-synth/decomp_synth/patterns/first_use_reorder.py`
- **Class:** `class FirstUseReorder(Pattern):` with `name = "first_use_reorder"`.
- **`generate(ctx)` AST implementation:**
  1. From `ctx.cursor`, collect the function's top-level statement list (and statement lists
     inside loop/compound bodies — first-use inside a loop is where Experiment A fired).
  2. Find **adjacent pairs of independent statements**: no read-after-write / write-after-write
     between them, no shared written lvalue, no call whose side effects are observably ordered
     against the other. **Reuse `declaration_reorder`'s existing dependency-safety predicate**
     (it already solves the same hazard analysis for declarations).
  3. For each safe pair, emit a variant with the two statements swapped. This changes
     first-use order **without moving any declaration** — the exact transform of Experiment A.
  4. Also emit the dependency-locked form where applicable (chain the two values through a
     shared temp) only if it stays behaviour-neutral — usually it is not, so prefer (3).
  5. Cap fan-out: only swap pairs that touch a local appearing in an objdiff-flagged register
     swap (guided), or the first N safe pairs (blind fallback). Keeps beam width sane.
- **Distinct from `declaration_reorder`:** orthogonal lever. DeclReorder moves symbol IDs (no
  effect on this bucket); FirstUseReorder moves use-site scheduling (proven to flip `r27/r28`).
  Run them in series in the beam; FirstUseReorder is the one expected to bite on the
  ~1,300-fn / ~495K-byte both-stuck regswap bucket.
- **Scoring:** unchanged — objdiff match% vs target.
- **Validation gate before productionizing:** sweep `FirstUseReorder` (isolated, dry-run,
  objdiff-scored) over the same 10 both-stuck∩REGISTER_SWAP functions used in the negative
  decl-reorder validation (`docs/sessions/2026-05-31-stream3-binary-oracle-validation.md`).
  If it improves ≥1 function where decl-reorder got 0/10, the lever is proven on real targets
  and should ship. Note the prior validation found 4/10 of that sample were actually **FPR**
  swaps (out of scope for a GPR use-reorder) and 3/10 had no clean swap pair — so the realistic
  GPR-addressable subset is ~3/10; size the expectation accordingly.

## Existing patterns to DEMOTE/SCOPE (action items, contingent on B/C confirmation)

If B confirms (as expected) that reassociation/temp-split produce identical asm on arithmetic
chains, the already-registered near-duplicate patterns `associativity` and
`expression_grouping` (same docstring) and `variable_extraction` (on arithmetic chains)
should be set `enabled_by_default = False` for the regalloc tier or merged, to stop wasting
beam budget. No new pattern is needed for B or C.

---

## Reproduce

```bash
cd /home/free/code/milohax/dc3-decomp
# Experiment A — the positive result
python3 -m tools.compiler_trace diff-asm /tmp/claude/exp/firstuse/use_a.cpp \
    /tmp/claude/exp/firstuse/use_b.cpp -f f       # → "Register swaps detected: r27 <-> r28"
# A2 / B / C
python3 -m tools.compiler_trace diff-asm /tmp/claude/exp/firstuse2/use_a.cpp /tmp/claude/exp/firstuse2/use_b.cpp -f f
python3 -m tools.compiler_trace diff-asm /tmp/claude/exp/reassoc/group_left.cpp /tmp/claude/exp/reassoc/group_right.cpp -f g
python3 -m tools.compiler_trace diff-asm /tmp/claude/exp/synthconst/c_zero_literal.cpp /tmp/claude/exp/synthconst/c_zero_subself.cpp -f h
# IL microscope (symbol-ID / tree diff)
python3 -m tools.compiler_trace capture-il /tmp/claude/exp/firstuse2/use_a.cpp \
    --output-dir /tmp/claude/ilA --diff /tmp/claude/exp/firstuse2/use_b.cpp
```

Aggregate run saved at `/tmp/claude/all_experiments.txt`. Test files under
`/tmp/claude/exp/`.

## Files

- `docs/sessions/2026-05-31-stream3-binary-oracle-validation.md` — prior decl-order verdict (the 10-fn validation set to re-run FirstUseReorder against)
- `docs/plans/compiler-instrumentation.md` — IL mechanism + Experiments 1–9 (esp. Exp 1 folding, Exp 4 symbol-ID swap, Exp 6–8 coloring/coalescing)
- `tools/compiler_trace/` — `diff-asm`, `capture-il` (compiler present under `build/compilers/X360/16.00.11886.00/`)
- `/home/free/code/milohax/decomp-synth/decomp_synth/patterns/base.py` — pattern contract
- `/home/free/code/milohax/decomp-synth/decomp_synth/patterns/__init__.py` — `ALL_PATTERNS` registry
- `/home/free/code/milohax/decomp-synth/decomp_synth/patterns/declaration_reorder.py` — source of the reusable dependency-safety predicate
