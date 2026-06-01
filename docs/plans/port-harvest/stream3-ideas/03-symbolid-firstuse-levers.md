# Stream 3 Idea 03 — Source levers BEYOND declaration order (first-use / reassociation / synthesized-constant)

**Date:** 2026-05-31
**Stream:** 3 (compiler instrumentation / register-allocation levers)
**Status:** COMPLETE. All experiments designed AND run against the binary oracle
(cl.exe v16.00.11886 / c2.dll via wibo), measured with `diff-asm` (`/FAcs` listing + register
swap detector) and corroborated with `capture-il` (IL `.sy`/`.ex` byte diff).

**HEADLINE VERDICT: YES — there is a real, buildable source lever beyond declaration order.
FIRST-USE position moves GPR allocation even with declaration order held byte-identical
(two independent confirmations, `r27↔r28` and `r29↔r31`). Reassociation and
synthesized-constant spelling are DEAD for register allocation (c2.dll canonicalizes /
constant-folds them — zero register movement). Build a `FirstUseReorder` decomp-synth rule;
do NOT build constant-spelling or reassociation rules.**

---

## TL;DR

Prior Stream-3 work retired **declaration-order reorder** for the both-stuck bucket: symbol
IDs swap in the IL but c2.dll's coloring is driven by use-site scheduling, so emitted GPRs
don't move (`docs/sessions/2026-05-31-stream3-binary-oracle-validation.md`). The mechanism
docs say the symbol-ID tie-breaker only fires *when two ops are equally ready* — i.e. the real
driver is **use-site program position**, which nothing had ever varied independently of
declarations.

This study did. **First-use is a real lever:** with two locals whose declaration order is
byte-identical, swapping only the order in which they are first *used* flips their registers.
Confirmed twice — once with reorderable statements (`r27↔r28`) and once with a
data-dependency-locked use order the compiler cannot undo (`r29↔r31`, 26 semantic diffs).
Reassociation and constant-spelling, by contrast, move nothing.

---

## How these were run (toolchain present and working)

```
build/tools/wibo + build/compilers/X360/16.00.11886.00/{cl.exe,c2.dll}
python3 -m tools.compiler_trace diff-asm <a> <b> -f <func>     # /FAcs listing diff + regswap detector
python3 -m tools.compiler_trace capture-il <a> --output-dir <d> --diff <b>   # IL .sy/.ex/.gl/.in/.db
```

`diff-asm` compiles each variant to a `/FAcs` `.cod` listing, extracts the named function,
normalizes registers, and reports detected register swaps + remaining semantic diffs. Test
files: `/tmp/claude/exp/{firstuse,firstuse2,reassoc,synthconst}/`. Full captured output:
`/tmp/claude/all_experiments.txt` (A/A2) and `/tmp/claude/bc_experiments.txt` (B/C).

> Tooling note for the next agent: the listings are `.cod`, not `.asm`. A hand-rolled
> `glob('*.asm')` reader silently finds nothing and falsely reports "identical". Always use
> the `diff-asm` CLI (it tries `.cod` then `.asm`), not a custom listing reader.

### Harness sanity (IL microscope works)

`capture-il --diff` on the canonical decl-order pair `tmp/regswap_controlled/swap_{a,b}.cpp`
reproduces compiler-instrumentation.md Experiment 4 exactly: `.sy` swaps the two symbol IDs
(`alpha=0x09ef / beta=0x09f0` in A, reversed in B) and `.ex` swaps every reference. So the IL
pipeline is live and the readings below are real.

---

## Mechanism (recap, `docs/plans/compiler-instrumentation.md`)

```
source.cpp
  │ c1xx.dll: assigns monotonic 16-bit symbol IDs in DECLARATION order → .sy
  │           emits expression trees referencing those IDs        → .ex
  ▼ c2.dll:   scheduling — symbol ID is a TIE-BREAKER *only when two ops are equally ready*
              regalloc    — graph coloring + coalescing, in processing (use) order
              → PowerPC asm
```

Declaration order moves symbol IDs but, for this bucket, not registers — the ops weren't tied;
**use-site order** had already decided readiness. Decl-reorder can't touch use order;
**statement order can.** Experiment A proves it.

---

## Experiment A — FIRST-USE position (declarations fixed) — **POSITIVE (×2)**

### A1 — reorderable independent statements (`/tmp/claude/exp/firstuse/`)

Identical declarations in both (`void* alpha = get_alpha();` then `void* beta = get_beta();`);
the only change is the order of two independent loop-body statements:

```cpp
// A1 var A (loop body)              // A1 var B (loop body — ONLY these two lines swapped)
sink(alpha, i);  // alpha used 1st   sink(beta,  i);  // beta used 1st
sink(beta,  i);  // beta  used 2nd   sink(alpha, i);  // alpha used 2nd
t += combine(alpha, beta);           t += combine(alpha, beta);
```

**Measured (`diff-asm -f f`, verbatim):**

```
Register swaps detected:
  r27 <-> r28
Semantic differences (after register normalization): 8
  var A: bl get_alpha ; mr r28,r3   (alpha→r28)   |  var B: same decls, alpha STILL emitted
  var A: bl get_beta  ; mr r27,r3   (beta →r27)   |  but loop uses r27 first then r28
```

The diff's own comment lines confirm declarations are byte-identical and only the two `sink`
statements moved. Result: **`r27 ↔ r28` swap.** POSITIVE.

### A2 — data-dependency-locked use order (`/tmp/claude/exp/firstuse2/`) — stronger

Here the two pointer reads are chained so the compiler **cannot** reorder them back:

```cpp
// A2 var A                                  // A2 var B (decls IDENTICAL)
int x = g(alpha[i]);     // alpha read 1st   int x = g(beta[i]);      // beta read 1st
int y = g(beta[i] + x);  // beta read 2nd    int y = g(alpha[i] + x); // alpha read 2nd
```

**Measured (`diff-asm -f f`):**

```
Register swaps detected:
  r29 <-> r31
Semantic differences (after register normalization): 26
  var A:  bl get_alpha ; mr r29,r3    |   var B:  bl get_alpha ; mr r31,r3
  var A:  bl get_beta  ; mr r31,r3    |   var B:  bl get_beta  ; mr r29,r3
```

Declarations byte-identical; first-use order data-locked; **alpha/beta swap `r29 ↔ r31`** with
26 downstream semantic diffs. This is the decisive confirmation: the lever survives even when
the compiler is denied the freedom to schedule the uses itself.

**Why first-use works where decl-order fails:** the two uses give alpha/beta distinct use-site
program positions. c2.dll colors in use order, so swapping the uses swaps the registers — the
symbol-ID tie-breaker never had to fire because the uses weren't tied. Decl-reorder changes
the (tied) IDs → nothing; first-use reorder changes the (untied) use positions → register
flip. And it moves exactly the both-stuck-shaped values: two callee-saved locals surviving
across calls.

---

## Experiment B — EXPRESSION GROUPING / reassociation — **NEGATIVE (dead)**

Triple (behaviour-equivalent for integer `+`), `/tmp/claude/exp/reassoc/`:
`group_left: (((a+b)+c)+d)+n`, `group_right: a+(b+(c+d))+n`,
`group_temps: t0=a+b; t1=t0+c; t2=t1+d; t2+n`.

**Measured:**
- left vs right: **no register swaps; the only "semantic diff" is the source-comment line.**
  The 32 instructions are otherwise identical.
- left vs temps: **no register swaps;** only the `$M####` block-label numbers and the comment
  line differ (non-semantic). Instruction stream identical (`mr r31,r3 ; li r3,0 ; bl src …
  add r3,r11,r31`).

**VERDICT: DEAD for regalloc.** c2.dll canonicalizes the integer add tree completely —
operator grouping and temp materialization of a pure arithmetic chain both collapse to one
canonical form before regalloc. (Float reassociation is additionally NOT value-neutral and is
a correctness hazard — never use it.) This retires the register-lever hope for the existing
`associativity` / `expression_grouping` / `variable_extraction` patterns on arithmetic chains.

---

## Experiment C — SYNTHESIZED-CONSTANT spelling — **NEGATIVE (dead)**

Targets the dominant both-stuck failure mode (`li rN,0` / nullptr with no source decl;
compiler-instrumentation.md Experiment 1 already proved a naive `T* p=0;` is DCE'd). A loop
where a null iterator survives across calls and competes with a counter. The literal stream
genuinely contains the failure mode (`li r31,0`, `li r29,0`). Three decomp-legal spellings,
`/tmp/claude/exp/synthconst/`:

```cpp
void* it = 0;                            // c_zero_literal   → li r31,0
void* it = (void*)0;                     // c_zero_cast
void* it = (void*)((char*)t - (char*)t); // c_zero_subself
```

**Measured:**
- literal vs cast: **no register swaps;** `li r31,0` in both; only `$M####` labels + comment
  differ.
- literal vs subself: **no register swaps;** the self-subtraction is fully constant-folded back
  to the same `li r31,0` in the same register; only `$M####` labels + comment differ.

(The IL capture for literal-vs-subself showed the front-end DOES build a different `.sy`/`.ex`
tree for the self-subtraction — yet c2.dll folds it away and lands the constant in the
identical register. Strongest possible disproof: provably different IL, identical registers.)

**VERDICT: DEAD.** The synthesized constant's register is not source-spellable. The only
spelling that would move it is an opaque barrier (a call/volatile that defeats folding) — but
that **adds code the target binary does not contain**, so it is not decomp-legal and is
rejected.

---

## Verdicts table

| Exp | Dimension | IL signal | Register swaps detected | Decomp-legal? | Lever? |
|---|---|---|---|---|---|
| (prior) | Declaration order | `.sy` swaps IDs | none (validated) | yes | RETIRED |
| **A1** | first-use, reorderable stmts | `.ex` differs | **`r27↔r28`** | yes | **YES** |
| **A2** | first-use, dependency-locked | `.ex` differs | **`r29↔r31` (26 diffs)** | yes | **YES (strong)** |
| B1 | reassociation `(a+b)+c↔a+(b+c)` | `.ex` differs | none (canonicalized) | int-only | NO (dead) |
| B2 | temp-split arithmetic chain | adds symbols | none (canonicalized) | yes | NO (dead) |
| C1 | constant spelling literal↔cast | `.ex` differs | none (same `li r,0`) | yes | NO (dead) |
| C2 | constant spelling literal↔subself | `.sy`+`.ex` differ | none (folded to same `li r,0`) | yes | NO (dead) |

**Bottom line:** the source dimension that moves register allocation beyond declaration order
is **first-use / use-statement order**, confirmed twice on the exact value shape of the
both-stuck regswap bucket. Reassociation and constant-spelling are dead (canonicalized /
folded). There IS a buildable new lever.

---

## Buildable new lever — `FirstUseReorder` decomp-synth rule (BUILD)

Pattern contract (`/home/free/code/milohax/decomp-synth/decomp_synth/patterns/base.py`):
subclass `Pattern`, set `name`, implement
`generate(self, ctx: TransformContext) -> Iterator[str]` yielding behaviour-neutral source
variants; `ctx.cursor` is the libclang function cursor, `ctx.source` the text. Register in
`decomp_synth/patterns/__init__.py::ALL_PATTERNS`.

There is **no** existing pattern that does this. `declaration_movement` moves a *declaration*
toward its use; `declaration_reorder` permutes *declarations* (moves symbol IDs). Neither
reorders **use statements with declarations held fixed** — the dimension Experiment A proved
moves registers.

- **File:** `/home/free/code/milohax/decomp-synth/decomp_synth/patterns/first_use_reorder.py`
- **Class:** `class FirstUseReorder(Pattern):` with `name = "first_use_reorder"`.
- **`generate(ctx)` AST implementation:**
  1. From `ctx.cursor`, collect statement lists for the function body and each compound/loop
     body (A1/A2 fired inside a loop — that scope matters most for callee-saved swaps).
  2. Find **adjacent pairs of independent statements** (no read-after-write / write-after-write
     between them, no shared written lvalue, no two side-effecting calls whose order is
     observable). **Reuse `declaration_reorder`'s existing dependency-safety predicate** — it
     already solves the same hazard analysis for declarations.
  3. For each safe pair, emit a variant with the two statements swapped — first-use order
     changes, declarations untouched (exactly A1).
  4. Optionally emit the dependency-locked form (chain two values through a shared temp) only
     when it is provably behaviour-neutral; usually it is not, so prefer step 3.
  5. Cap fan-out: prefer pairs whose statements reference a local flagged in the objdiff
     register swap (guided); else the first N safe pairs (blind fallback).
- **Orthogonal to `declaration_reorder`:** DeclReorder moves symbol IDs (no effect on this
  bucket); FirstUseReorder moves use-site scheduling (flips `r27/r28`, `r29/r31`). Run both in
  the beam; FirstUseReorder is the one expected to bite on the ~1,300-fn / ~495K-byte
  both-stuck regswap bucket.
- **Scoring:** unchanged — objdiff match% vs target.
- **Validation gate before shipping:** sweep `FirstUseReorder` (isolated, dry-run,
  objdiff-scored) over the same 10 both-stuck∩REGISTER_SWAP functions where decl-reorder got
  0/10 (`docs/sessions/2026-05-31-stream3-binary-oracle-validation.md`). If it improves ≥1
  function, ship it. Sizing caveat from that doc: 4/10 of that sample were actually **FPR**
  swaps (a GPR use-reorder can't touch those) and 3/10 had no clean swap pair — so the
  realistically addressable subset is ~3/10. Expect a modest, real hit rate, not a landslide;
  even a few both-stuck functions reaching 100% is net-new ground that NO source lever has
  reached since declaration order was retired.

## Existing patterns to DEMOTE/SCOPE (action items, no new files)

Experiment B confirmed the registered near-duplicate patterns `associativity` and
`expression_grouping` (same docstring) and `variable_extraction` (on arithmetic chains)
produce identical asm → set `enabled_by_default = False` for the regalloc tier or merge them,
to stop wasting beam budget. No new pattern is needed for B or C; constant-spelling and
reassociation are dead and should not be built.

---

## Reproduce

```bash
cd /home/free/code/milohax/dc3-decomp
# A1 (POSITIVE r27<->r28)
python3 -m tools.compiler_trace diff-asm /tmp/claude/exp/firstuse/use_a.cpp  /tmp/claude/exp/firstuse/use_b.cpp  -f f
# A2 (POSITIVE r29<->r31, dependency-locked)
python3 -m tools.compiler_trace diff-asm /tmp/claude/exp/firstuse2/use_a.cpp /tmp/claude/exp/firstuse2/use_b.cpp -f f
# B (DEAD): left/right/temps
python3 -m tools.compiler_trace diff-asm /tmp/claude/exp/reassoc/group_left.cpp /tmp/claude/exp/reassoc/group_right.cpp -f g
python3 -m tools.compiler_trace diff-asm /tmp/claude/exp/reassoc/group_left.cpp /tmp/claude/exp/reassoc/group_temps.cpp -f g
# C (DEAD): literal/cast/subself
python3 -m tools.compiler_trace diff-asm /tmp/claude/exp/synthconst/c_zero_literal.cpp /tmp/claude/exp/synthconst/c_zero_cast.cpp    -f h
python3 -m tools.compiler_trace diff-asm /tmp/claude/exp/synthconst/c_zero_literal.cpp /tmp/claude/exp/synthconst/c_zero_subself.cpp -f h
# IL microscope
python3 -m tools.compiler_trace capture-il /tmp/claude/exp/firstuse2/use_a.cpp --output-dir /tmp/claude/ilA --diff /tmp/claude/exp/firstuse2/use_b.cpp
```

Captured output: `/tmp/claude/all_experiments.txt` (A/A2), `/tmp/claude/bc_experiments.txt`
(B/C). Test files under `/tmp/claude/exp/`.

## Files

- `docs/sessions/2026-05-31-stream3-binary-oracle-validation.md` — prior decl-order verdict + the 10-fn validation set to re-run FirstUseReorder against
- `docs/plans/compiler-instrumentation.md` — IL mechanism + Experiments 1–9 (Exp 1 folding, Exp 4 symbol-ID swap, Exp 6–8 coloring/coalescing)
- `tools/compiler_trace/` — `diff-asm`, `capture-il` (compiler at `build/compilers/X360/16.00.11886.00/`)
- `/home/free/code/milohax/decomp-synth/decomp_synth/patterns/base.py` — pattern contract
- `/home/free/code/milohax/decomp-synth/decomp_synth/patterns/__init__.py` — `ALL_PATTERNS` registry
- `/home/free/code/milohax/decomp-synth/decomp_synth/patterns/declaration_reorder.py` — reusable dependency-safety predicate to share
