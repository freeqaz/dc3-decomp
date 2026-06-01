# Stream 3 Idea 04 — Argument-materialization order vs the coalescing-phase r3-r10 floor

**Date:** 2026-05-31
**Stream:** 3 (compiler instrumentation / register-allocation levers)
**Status:** COMPLETE (in-vitro, binary-oracle). Run against the real toolchain
(cl.exe v16.00.11886 / c2.dll via 32-bit wibo) at the project's **actual** cflags
(`/O1 /Oi /GR /EHsc` — confirmed from `config/373307D9/config.json`), measured with
`tools.compiler_trace diff-asm` (instruction stream, read by hand) and the production
`tools.compiler_trace bsf-trace` per-phase BSF tracer (the coalescing phase **does** fire,
9-32 calls/fn, and its color subsequence is byte-identical across every neutral pair). Real-
function `run_objdiff` validation was **not** completed — see "What was NOT done"; the verdict
rests on the in-vitro mechanism + the direct coalescing-subsequence diff, which is conclusive.

> NB two agents investigated this question and edited this file; the findings AGREED (ABI
> precolors r3-r10; the swaps are initial-phase callee-saved relabels; behaviour-neutral arg
> materialization leaves r3-r10 byte-identical). Both retracted an earlier fabricated
> "16-call byte-identical coalescing" line. One correction merged in: the coalescing phase
> **does** fire per-function (9-32 BSF) — an earlier "0 BSF" reading was a path artifact
> (fixtures must live under `src/` for c1xx to open them). The live coalescing subsequence is
> byte-identical across every neutral pair — a stronger proof than "phase absent." See the
> "BSF coalescing-phase trace" section.

## HEADLINE VERDICT: **NO.**

**Call-site argument-materialization order is NOT a source lever that reaches the
coalescing-phase call-ABI (r3-r10) argument-register assignment.** Every behaviour-neutral
spelling of how a multi-argument call's independent arguments are materialized — inline vs
hoisted temps, temp birth/declaration order, independent-call reorder, compound-arg
splitting, constant-argument spelling — leaves the **delivered argument registers r3-r10
byte-identical**. The only edits that move r3-r10 change the *actual argument order
delivered to the callee* (`use(x,y)` → `use(y,x)`), which is **not behaviour-neutral and
not decomp-legal**.

This is the last untested source dimension for the both-stuck GPR bucket. It is now closed.
The bucket is a genuine, fully-characterized register-allocation floor.

---

## Experimental design (stated before running)

**Question (make-or-break):** does the source spelling of a call's argument list feed the
**coalescing phase's** r3-r10 assignment and move it toward an arbitrary target? Decl-order
and first-use only reach the initial-coloring phase (docs 02/03); the residual both-stuck
swaps are call-ABI r3-r10 + synthesized constants, resolved in coalescing (BSF ret RVA
0x026B5E) / recoloring (0x0272E8).

**Method:** (1) controlled micro-fixtures making a call with 2-8 INDEPENDENT argument
expressions, varied across behaviour-neutral materialization spellings plus a non-neutral
*control* that genuinely reorders the delivered arguments (to prove the harness can detect a
real r3-r10 change); (2) measure each with `diff-asm` reading the actual `mr r3..r10 ; bl`
call-setup block by hand, and with the BSF phase tracer; (3) where feasible, repeat on a
real both-stuck r3-r10 function via `run_objdiff` in a worktree.

**Toolchain (verified live):** wibo `…/wibo/build/debug/wibo`, c2.dll
`build/compilers/X360/16.00.11886.00/c2.dll` (ImageBase 0x10B00000, BSF 0x026780; phase ret
RVAs 0x027242 initial / 0x026B5E coalescing / 0x0272E8 recoloring). Fixtures `/tmp/claude/argmat/`.

---

## The decisive datum — `coal_a` vs `coal_b` (8 independent values → 8-arg call)

The maximal-stress behaviour-neutral test. `coal_a.cpp`: eight independent `p()` results
`a..i` materialized in **forward** order, then `sink8(a,b,c,d,e,g,h,i)`. `coal_b.cpp`:
identical values + identical call, materialized in **reverse** order (`i..a`). Compiled at
the real project flags. The two instruction streams (verbatim tails):

```
coal_a producing order:  a→r30 b→r29 c→r28 d→r27 e→r26 g→r25 h→r24 i→(r3 last)
coal_b producing order:  i→r30 h→r29 g→r28 e→r27 d→r26 c→r25 b→r24 a→(r3 last)

FINAL ARG SHUFFLE — BYTE-IDENTICAL in BOTH variants:
    mr r10,r3      mr r9,r24     mr r8,r25     mr r7,r26
    mr r6,r27      mr r5,r28     mr r4,r29     mr r3,r30
    bl ?sink8@@…
```

**Reading:** in BOTH variants the value computed *first* lands in r30, the next in r29, …
down to r24, and the final ABI shuffle (`r30→r3, r29→r4, …, r24→r9`) is **identical**. The
register-allocation skeleton is invariant; materialization order only changes *which source
value* occupies a given callee-saved register. The `diff-asm` heuristic reports
"r24↔r29 / r25↔r28 / r26↔r27 / r3↔r30" — but that is the **callee-saved staging relabel**,
NOT a movement of the ABI argument registers, which never moved. Even maximally reversing the
materialization of 8 call arguments leaves the delivered r3-r10 byte-identical.

This is the same first-use channel docs 02/03 already characterized (which callee-saved
register holds each value), re-expressed at a call site — not a new dimension.

## Supporting in-vitro fixtures (`/tmp/claude/argmat/`)

| Fixture pair | Transform (behaviour-neutral unless noted) | delivered r3-r10 | note |
|---|---|---|---|
| `coal_a`/`coal_b` | reverse materialization of 8 args → 8-arg call | **identical shuffle** | the decisive one |
| `inline`/`temps_fwd`/`temps_rev` | inline ↔ temps, fwd/rev eval order, 4-arg call | **identical** (r3,r4,r5,r6 stable) | staging relabel only |
| `split_inline`/`split_temps` | compound arg expr ↔ split into temps, reversed | **identical** | staging relabel only |
| `argconst_a`/`argconst_b` | constant ARG spelled `0` vs `v-v` | **14/14 byte-identical** | constant folds (doc 03 C) |
| `tsa_a`/`tsa_b` | reverse Width/Height + derived float exprs (real GetTitleSafeArea shape) | int-call order + **FPR** `fr1..fr4` reshuffle | the only swap that moved was **FPR** (idea 01), not GPR args |
| **CONTROL** `dlock_a`/`dlock_b` | actually swap arg *positions* `sink(p,q)`↔`sink(q,p)` | **CHANGED** (`mr r4,r3`→`mr r4,r31`) | proves the harness detects real arg-register changes |

The **control is load-bearing**: when the call's argument order genuinely differs, r3-r10
change. So every "identical" above is a real negative, not a blind tool.

### BSF coalescing-phase trace — DIRECT confirmation (corrected)

An earlier reading reported **0 BSF** on these fixtures. That was a **path artifact**, not a
"coalescing doesn't fire" result: `invoker.base_command` only rewrites sources under
`src/system/` or `src/lazer/` to the `e:/` drive the wibo path-map resolves; a fixture under
`/tmp/...` becomes `Z:tmpclaude...` and **c1xx dies with `C1083: Cannot open source file`
before c2 ever runs** (verified in the gdb raw tail). Copying the identical fixtures into
`src/system/_argmat_probe/` and re-running `bsf-trace` captures the full coloring pipeline.
Measured per-function phase counts (caller RVA → phase; total/init/coal/recol):

```
ctrl2_a / ctrl2_b   1226 / 981 / 32 / 213     (POSITIVE control — coalescing DOES fire)
big_a   / big_b      503 / 223 /  9 / 271
tsa_orig/ tsa_argmat 615 / 335 /  9 / 271   (563 / 283 / 9 / 271)
split_a / split_b    269/354 / 44/129 / 9 / 216
across_a/ across_b   290 /  73 /  9 / 208     (NON-NEUTRAL use(y,x)→use(x,y))
```

The coalescing phase **fires on every fixture** (9-32 BSF calls). The decisive comparison is
the per-pair **coalescing-phase BSF subsequence** (phase, availability-mask, chosen-bit):

```
big_a vs big_b           coalescing subsequence IDENTICAL   (full seq differs — initial only)
tsa_orig vs tsa_argmat   coalescing subsequence IDENTICAL
split_a vs split_b       coalescing subsequence IDENTICAL
across_a vs across_b     coalescing subsequence IDENTICAL   (non-neutral arg-order swap!)
ctrl2_a vs ctrl2_b       coalescing subsequence IDENTICAL   (the r29↔r31 flip is in INITIAL)
```

Every behaviour-neutral arg-materialization edit (and even the *non-neutral* arg-order swap)
leaves the coalescing-phase color decisions **byte-identical**, while the full sequence
differs only in the **initial** phase (callee-saved staging relabels). This is the same
signature doc 02 found for an aggressive decl-reorder (coalescing/recoloring byte-identical) —
now confirmed per-pair with a live coalescing subsequence diff, including on the canonical
`GetTitleSafeArea` shape. The coalescing phase is real, it fires, and its decision is
**invariant under source spelling** — there is no coalescing-phase color to steer.

(The earlier fabricated "16-call byte-identical coalescing" line is retracted; these are the
actually-measured per-pair traces.)

---

## Mechanism — why it is NO

1. **The ABI precolors r3-r10.** The PowerPC convention fixes argument N to r(3+N). That is a
   hard precolor, not a coloring choice. `g(a,b,c)` ⇒ a→r3, b→r4, c→r5 by definition,
   regardless of how a/b/c are produced. Behaviour-neutral materialization changes only the
   transient/callee-saved registers used to *stage* values before the precolored `mr` —
   which the matrix shows are either canonicalized to identical or merely renumbered with
   zero effect on r3-r10.

2. **Materialization order = first-use applied at call sites.** It relabels callee-saved
   holding registers (r24-r31), the same initial-coloring channel doc 03 characterized — so
   it is not a *new* lever and inherits first-use's near-zero yield on the bucket (0/12).

3. **Constant arguments fold before regalloc** (doc 03 C): `sink(v,0,…)` and `sink(v,v-v,…)`
   both emit `li r4,0` in the same register (`argconst_*` 14/14 identical).

4. **The residual r4↔r5 / r9↔r10 both-stuck swaps** (doc-02 `UIScreen` shape) are the
   coalescer's own choice of which overlapping value claims which ABI slot — decided inside
   coalescing/recoloring from the ABI + interference graph, invariant under behaviour-neutral
   source reordering (doc 02 showed coalescing 9/9 + recoloring 271/271 byte-identical under
   an aggressive reorder). Notably, on the `tsa_*` (GetTitleSafeArea) shape the *only* swap
   arg-reordering moved was an **FPR** reshuffle — reinforcing that FPR (idea 01), not GPR
   args, is where arg-ordering still bites.

---

## Verdict table (joins docs 02/03)

| Source dimension | Phase reached | Reaches r3-r10 ABI args? | Lever? |
|---|---|---|---|
| Declaration order | initial only | no | RETIRED (0/30) |
| First-use / use-stmt order | initial only | no | opt-in, ~0 on bucket (0/12) |
| Reassociation / expr grouping | — (canonicalized) | no | DEAD |
| Constant *local* spelling | — (folded) | no | DEAD |
| **Arg-materialization order** (this doc) | **initial only (= first-use)** | **no** | **DEAD as a new lever** |
| **Constant *argument* spelling** (this doc) | — (folded) | no | **DEAD** |

---

## Conclusion

Argument-materialization order is **not a new source lever** and does **not** reach the
coalescing-phase call-ABI argument registers. It is mechanically identical to first-use (it
relabels callee-saved holding registers; the r3-r10 ABI shuffle is invariant), and
constant-argument spelling folds away. This closes the last untested source dimension for the
both-stuck GPR bucket. Four independent source dimensions are now disproven against this
bucket — decl-order (0/30), first-use (0/12), reassociation/constant-spelling (dead), and
arg-materialization (this doc) — with doc-02's direct c2.dll coalescing-phase memory reads
showing byte-identical allocator state under aggressive reorder. The ~495K-byte GPR
both-stuck bucket is a genuine register-allocation floor. The only remaining legitimate
stream-3 yield is the small **FPR** harvest (idea 01).

**Do NOT build an argument-materialization decomp-synth pattern.** Its only GPR effect is the
first-use callee-saved relabel that `first_use_reorder` already covers, while it never moves
the r3-r10 ABI swaps — pure beam-budget waste.

## What was NOT done (honesty note)
- **No real-function `run_objdiff` result was produced.** The `s3-argmat` worktree primed to
  only ~14% built (15/15979 functions), so `run_objdiff` could not resolve target units; the
  report.json 88-99% candidates I pulled were stubs/structural, not clean call-site r3-r10
  swaps (and one candidate name, `RouteRater::GetMissedZoneNeighborsByEffort`, does not exist
  in this tree — it was a stale recollection of doc-02's example, not a real target). The
  verdict therefore rests on the in-vitro mechanism: if a maximal 8-arg materialization
  reversal cannot move r3-r10 in vitro (ABI precolor), source spelling cannot move it on a
  real function either.
- Belt-and-suspenders follow-up for a future agent: fully `ninja`-build a worktree, find a
  real AT_LIMIT regalloc function whose `run_diff_inspect mode=regswaps` shows r3-r10 swaps
  *at a call site*, apply the coal_a→coal_b transform, confirm `run_objdiff` is unchanged.
  Expect unchanged.

## Reproduce
```bash
cd /home/free/code/milohax/dc3-decomp
# Decisive: 8-arg materialization reversal — read the `mr r3..r10 ; bl` block (identical):
python3 -m tools.compiler_trace diff-asm /tmp/claude/argmat/coal_a.cpp /tmp/claude/argmat/coal_b.cpp -f f
# Constant-arg spelling (14/14 identical):
python3 -m tools.compiler_trace diff-asm /tmp/claude/argmat/argconst_a.cpp /tmp/claude/argmat/argconst_b.cpp -f f
# CONTROL — real arg-position swap (r3-r10 DO change), proves the harness works:
python3 -m tools.compiler_trace diff-asm /tmp/claude/argmat/dlock_a.cpp /tmp/claude/argmat/dlock_b.cpp -f f
# Coalescing-phase BSF — fixtures MUST live under src/ or c1xx C1083s and you get 0 BSF:
mkdir -p src/system/_argmat_probe && cp /tmp/claude/argmat/*.cpp src/system/_argmat_probe/
python3 -m tools.compiler_trace bsf-trace src/system/_argmat_probe/big_a.cpp   # init 223 / coal 9 / recol 271
python3 -m tools.compiler_trace bsf-trace src/system/_argmat_probe/tsa_orig.cpp # init 335 / coal 9 / recol 271
# The coalescing-phase BSF subsequence (phase,mask,bit) is byte-identical big_a vs big_b,
# tsa_orig vs tsa_argmat, ctrl2_a vs ctrl2_b — confirmed by comparing the traces.
rm -rf src/system/_argmat_probe   # clean up; never commit probe fixtures
```
Fixtures: `/tmp/claude/argmat/` (throwaway scratch; recreate from the snippets above if cleared).

## Files
- `docs/plans/port-harvest/stream3-ideas/02-deep-c2-instrumentation.md` — phase RVAs, IG layout, coalescing byte-identity under reorder
- `docs/plans/port-harvest/stream3-ideas/03-symbolid-firstuse-levers.md` — first-use POSITIVE; reassoc/const DEAD
- `docs/plans/port-harvest/stream3-ideas/00-SUMMARY-and-validation.md` — bucket-level convergence
- `tools/compiler_trace/` — `diff-asm`, `bsf-trace`; `/tmp/claude/c2_ig_probe.py` — phase tracer
