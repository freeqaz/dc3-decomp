# Stream 3 Idea 05 — fmuls / commutative-FP operand order

**VERDICT (2026-06-01, authoritative — controlled c2.dll listings + real-function objdiff):
the operand order of `fmuls fD,fA,fB` (and the commutative FP family `fadds`, `fmadds`) is
NOT source-controllable.** It is a **backend, register-driven emission decision made AFTER register
allocation** — it is a function of the physical registers the two source operands land in, not of
the source expression's operand spelling, statement order, or operand liveness. objdiff's
`COMMUTATIVE_OP_ORDER (LikelyFixable)` label is **misleading for FP multiplies/adds**: those
specific near-misses are effectively a register-allocation/scheduling floor, the same class as the
both-stuck GPR/FPR swaps. Stop hand-chasing fmuls/fadds/fmadds operand-order mismatches.

This is a **validated NEGATIVE** (which the mission states is a fully successful outcome): we now
have a precise mechanism and hard evidence, so we can re-label and stop spending effort here.

---

## TL;DR mechanism

For `fmuls fD,fA,fB` the compiler does **not** preserve the source operand order. It emits the two
commutative source operands in an order chosen by the backend from the **post-regalloc register
state** (which value is in which physical FPR, and which register is reused as the destination).
Every source transform that does **not change the final register assignment** produces
**byte-identical** code; the only transform that moved the order (forcing the products through named
temporaries) did so by perturbing register allocation/scheduling, and it cascaded into *more*
mismatches, not fewer. So there is no behaviour-neutral source lever that reliably flips one
fmuls's A/B order toward the target without also disturbing everything else.

---

## Experimental design

Two independent laboratories, both through the **real toolchain** (cl.exe 16.00.11886 / c2.dll via
32-bit wibo, project flags `/O1`→per-function `/Ogsu`, `/Oi /GR /EHsc`):

1. **Real-function oracle:** `FastInvert` (`?FastInvert@@YAXABVMatrix3@Hmx@@AAV12@@Z`, unit
   `system/math/mtx`, **99.8%**, single mismatch at idx48). One instruction, identical registers on
   both sides — a clean A/B-order oracle: any behaviour-neutral source transform either drives it to
   100% or it doesn't. Scored with MCP `run_objdiff full_build:true` in worktree `wt/s3-fmuls`.
2. **Controlled c2.dll listings:** tiny fixtures compiled with `/FAcs` via
   `python3 -m tools.compiler_trace diff-asm` (emits MSVC `.cod` with machine bytes), varying the
   *source property* of the two operands while holding the product constant, to read the emitted
   `fmuls` operand order directly.

---

## The FastInvert oracle (real function, MCP `run_objdiff full_build:true`)

Baseline single mismatch:

```
idx48  target: fmuls f6, f12, f6      base(ours): fmuls f6, f6, f12     [reg:f6->f12, reg:f12->f6]
```

Note the registers are **identical** on both sides (result f6; sources f6 and f12). Only the A/B
slot of the two sources differs — a pure operand-order divergence with **zero** register-allocation
difference. The source line is `min.x.y * xdot` (mtx.cpp:140), one of the nine
`min.<r>.<c> * <dot>` products fed to `mout.Set(...)`.

| # | Transform (behaviour-neutral) | Result |
|---|---|---|
| 1 | `min.x.y * xdot` → `xdot * min.x.y` (naive a*b↔b*a on the mismatching line) | **byte-identical** — still 99.8%, same idx48 |
| 2 | Flip **all nine** products `load * recip` → `recip * load` | **byte-identical** — still 99.8%, same idx48 |
| 3 | Hoist all nine products into named `float p0..p8` temps, pass temps to `Set` | **regressed to 98.6%** — schedule shifted, **12** mismatches (new f9↔f11 / f10↔f12 cascades, offset swaps); idx48's order *did* shift but the change was a regalloc/scheduling perturbation, not a clean flip |

**Conclusion from the oracle:** naive flips (1,2) are completely inert — the compiler discards the
source operand order. The only transform that moved idx48 (3) did so by changing register
allocation, and it made the function strictly *worse*. There is no behaviour-neutral source edit
that flips idx48 to the target order. FastInvert does **not** reach 100% by any operand-spelling or
statement-order transform.

---

## Controlled c2.dll listings (the mechanism, with machine bytes)

### Fixture A — operand spelling and shared-value position are ignored

```cpp
float f_ab(float *p) { return p[0] * p[1]; }     // -> fmuls fr1,fr0,fr13   (ec200372)
float f_ba(float *p) { return p[1] * p[0]; }     // -> fmuls fr1,fr0,fr13   (ec200372)   IDENTICAL

// s = g[0] used twice (shared); load single-use
//   r = s * p[i]   -> fmuls fr13,fr13,fr0       (edad0032)
//   r = p[i] * s   -> fmuls fr13,fr13,fr0       (edad0032)                 IDENTICAL
```

`a*b` vs `b*a` → **byte-identical** (`ec200372`). `shared*load` vs `load*shared` → **byte-identical**
(`edad0032`). Source operand spelling is discarded outright.

### Fixture B — statement order and operand liveness are ignored too

```cpp
// compute a first vs compute b first (same product a*b):
//   both -> ... fmuls fr1,fr0,fr13   (ec200372)   IDENTICAL  (scheduler re-derives same layout)

// which operand is kept LIVE across the multiply (stored to *out):
float f_a_live_after(...){ *out=a; return a*b; }  // -> fmuls fr1,fr13,fr0   (ec2d0032)
float f_b_live_after(...){ *out=b; return a*b; }  // -> fmuls fr1,fr13,fr0   (ec2d0032)   IDENTICAL
```

The killer evidence is the `*_live_after` pair. Forcing `a` vs `b` to be live-after **changed which
source variable lands in which physical register** (the live one is parked in `fr0`, the dying one
in `fr13`) — yet the emitted instruction is **byte-identical** (`ec2d0032` = `fmuls fr1,fr13,fr0`):
in both, the value in `fr13` is fA and the value in `fr0` is fB. **The A/B order tracks the physical
register, not the source operand.** The store/liveness moved the *variable* between registers, but
the register-slot order in the fmuls is invariant.

This pins the decision point: operand order is chosen from the **post-register-allocation register
assignment**. (At idx48 of FastInvert the registers are identical between target and our build, so
the divergence is purely this final emitter choice — confirming it sits downstream of regalloc, in
instruction emission/canonicalization, exactly where source cannot reach without changing the
register assignment itself.)

### What the register-driven rule looks like (descriptive, not a clean source lever)

Across the fixtures and the two real functions, the emitter's A/B choice correlates with the
physical registers and which register is reused as the destination — e.g. when the destination
register equals one source operand's register, that operand is frequently fA (`fmuls fr13,fr13,fr0`,
`fmuls f12,f12,f3`); the FastInvert/Spotlight targets instead place the destination-register operand
as fB (`fmuls f6,f12,f6`, `fmuls f10,f0,f10`). The exact micro-rule is a scheduler+emitter artifact
of the post-regalloc instruction stream and is not cleanly characterizable from a handful of
fixtures — but it does **not** depend on any source property we can vary behaviour-neutrally. That
is the load-bearing result: the lever, whatever its internal rule, is not exposed to source.

---

## Proof fixtures (durable, re-runnable — `tools/compiler_trace/fixtures/`)

The throwaway `/FAcs` fixtures quoted above were rebuilt as a permanent, documented
regression artifact: **`tools/compiler_trace/fixtures/fmuls_operand_order.cpp`** (+ a
`README.md` with the exact recompile/decode procedure). Each fixture is a tiny function with a
PREDICTION header comment and the OBSERVED machine bytes; recompile any time with the real
toolchain (cl.exe 16.00.11886.00 / wibo, `/O1 /Oi /GR /EHsc` — emits `/Ogsu` per function, same as
the DC3 build). All seven fixtures **CONFIRMED** the model; **none broke it**.

| # | Fixture (probe) | Prediction | Observed (hex / mnemonic) | ✓/✗ |
|---|---|---|---|---|
| 1 | `f1_ab` (`a*b`) vs `f1_ba` (`b*a`), 2 ptr loads | identical bytes — spelling discarded | both `ec200372` = `fmuls fr1,fr0,fr13` | ✓ |
| 2 | `f2_free` vs `f2_pinned` — **same source product**, different forced FPR assignment | A/C slot follows the register assignment, not the source | `f2_free` `ec200372` (`fr1,fr0,fr13`) vs `f2_pinned` `ec2d0032` (`fr1,fr13,fr0`) — **opposite A/C order, identical product** | ✓ |
| 3 | `f3_dest_reuse` (`x*y`, dest reg == `x`'s reg fr1) | our c2 puts the dest-equal operand FIRST (fA) | `ec2100b2` = `fmuls fr1,fr1,fr2` — dest-equal fr1 is fA | ✓ |
| 4 | `f4_{a_live,b_live,a_last,b_last}` — vary which operand is live-after & store order, FIXED register assignment | liveness/store order does NOT move the A/C slot | all four `ec2d0032` = `fmuls fr1,fr13,fr0`, byte-identical (only the `stfs` differs) | ✓ (killer test) |
| 5 | `f5_callee_saved` — a→fr31, b→fr30 (float-decl order, live across call) | order is register-driven, NOT a simple low/high-reg rule | `ec3e07f2` = `fmuls fr1,fr30,fr31` (fr30 in fA); cf. fixture 2 where `{fr0,fr13}` takes BOTH orders → not a reg-# rule | ✓ |
| 6 | `f6_add_*` (`a+b`/`b+a`, ABI + ptr) | `fadds` obeys the same discipline | ABI `ec21102a` `fadds fr1,fr1,fr2` (both); ptr `ec20682a` `fadds fr1,fr0,fr13` (both) — mirrors fixture-1 fmuls layout | ✓ |
| 7 | `f7_madd_*` (`a*b+c`/`b*a+c`/`c+a*b`, ABI + ptr) | `fmadds` obeys it; both the multiply-commute and add-commute discarded | ABI all three `ec2118ba` `fmadds fr1,fr1,fr2,fr3`; ptr `ec20637a` `fmadds fr1,fr0,fr13,fr12` (fA=fr0,fC=fr13 same as fmuls) | ✓ |

**Net: 7/7 fixtures confirm the model; zero contradictions.** Fixtures 1, 4, 6, 7 prove source
spelling / statement order / operand liveness are all discarded (byte-identical output). Fixture 2
is the decisive positive control: the **same** source product `a*b` emits **opposite** A/C orders
purely because the surrounding code forced a different value→FPR assignment. Fixture 5 shows the
slot rule is not "low reg first" / "high reg first" — the register set `{fr0,fr13}` appears in BOTH
orders across fixtures 1/2/4, so the A/C choice is a post-regalloc scheduler artifact with no
behaviour-neutral source lever, exactly as the verdict states.

### The rule, stated for a human to apply by eye
Given a commutative FP op whose two multiplicand/addend values have landed in physical registers
fA-candidate `fX` and fC-candidate `fY` with destination `fD`:
- **Source operand spelling, statement order, and operand liveness do not affect the A/C order at
  all** (proven byte-identical). Do not try to flip it by editing the expression.
- The A/C order is fixed by the **post-register-allocation value→FPR assignment + instruction
  schedule**. It is *not* "lower register first" (the same `{fr0,fr13}` pair appears as
  `fmuls …,fr0,fr13` and `…,fr13,fr0` depending only on which value the scheduler put in fr0).
- When the destination register equals one source operand's register, **our c2 build emits that
  dest-equal operand FIRST (fA)**; the real DC3 target frequently emits it SECOND (fC) on an
  otherwise-identical assignment (FastInvert idx48 `fmuls f6,f12,f6`; Spotlight idx112
  `fmuls f10,f0,f10`). That is a backend-version canonicalization difference, not a source choice.

## Second real-function confirmation (same signature)

`Spotlight::UpdateFloorSpotTransform` (`?UpdateFloorSpotTransform@Spotlight@@IAAXABVTransform@@@Z`,
99.7%) has the identical shape:

```
idx112  target: fmuls f10, f0, f10    base(ours): fmuls f10, f10, f0   [reg:f0->f10, reg:f10->f0]
```

Same as FastInvert: identical registers, pure A/B swap, and **the target puts the destination-equal
register SECOND while we put it FIRST.** This is a recurring, consistent divergence direction — our
c2 build canonicalizes "destination operand first" slightly more often than the original DC3 build
did, on otherwise-identical register assignments. It is a backend-version/scheduling artifact, not a
source difference.

---

## Where in the pipeline it's decided

- **After register allocation.** Proven two ways: (a) at idx48/idx112 the target and our build use
  *identical* physical registers and differ only in A/B slot — so the divergence is necessarily in a
  stage that runs once registers are fixed; (b) Fixture B's `*_live_after` pair shows the A/B order
  follows the *physical register*, so moving a variable to a different register (via liveness) moves
  it across the A/B boundary while the register-slot order stays fixed.
- This is **not** the BSF/graph-coloring phase (doc 02): coloring picks *which* register; the
  operand order is a separate post-coloring emission choice. (We did not need a new c2 operand-order
  probe — the identical-register objdiff signature plus the `/FAcs` listings already localize the
  decision to post-regalloc emission. A dedicated c2 breakpoint on the emitter would only confirm
  the micro-rule, which is not needed for the verdict.)
- Consistent with the doc-02 finding that the residual register-swap class is dominated by
  scheduling/coalescing/recoloring state that source declaration order cannot reach.

---

## Population / prize sizing (honest scope)

From `decomp.db` (`has_commutative_op_order` flag — note this flag covers **all** commutative ops,
including integer `add/or/xor/mulli`, not just FP):

- `has_commutative_op_order=1` total: **209** (199 below 100%).
- In the 99–100% band: **60**; in 95–100%: **96**.
- 99–100% with commutative-op as essentially the sole fixable signal (no register_swap / offset_swap
  flags): **39**. These are overwhelmingly FP-math functions (`CharClipSet::SetFrame`, `QuatKeys`,
  `FloatKeys`, `Quat::Set`, `Normalize`, `FastInvert`, `RndOverlay::Draw`, `RndTransformable`,
  `HamIKEffector::Poll`, …) where the dominant remaining mismatch is exactly an fmuls/fadds A/B swap.

**The prize, if it were fixable, would be ~39–96 high-band functions** (mostly 1–3 instructions
each → low-single-digit-K bytes). Per this investigation it is **NOT fixable by source**, so the
realistic harvest from this lever is **0 functions / 0 bytes**. The value of this doc is the
re-label: those ~39–96 near-misses should be reclassified from "LikelyFixable" to a
register-allocation/scheduling floor so agents and the permuter stop spending build cycles on them.

---

## Recommendations

1. **Re-label the heuristic.** `COMMUTATIVE_OP_ORDER` on FP opcodes (`fmuls/fmsubs/fadds/fsubs?`/
   `fmadds/fmsubs/fnmadds`) is **NOT LikelyFixable** — it is a post-regalloc backend canonicalization
   with no behaviour-neutral source lever. Integer commutative ops (`add/or/xor`) may still be
   genuinely reorderable in some cases and are out of scope of this negative; keep them separate.
   Suggested: split the pattern into `COMMUTATIVE_OP_ORDER_FP (RarelyHandFixable / regalloc-floor)`
   vs `COMMUTATIVE_OP_ORDER_INT`, and update
   `docs/decomp/patterns/fixable-operators.md#commutative-operand-order` to carry this caveat for the
   FP case.
2. **Do not build a decomp-synth pattern for fmuls operand order.** A naive `a*b↔b*a` transform is a
   guaranteed no-op (proven). The only thing that moves it is a regalloc perturbation, which the
   existing declaration-reorder / expression-extraction patterns already cover — and which, per
   doc 02 and the Fixture-A/B evidence, lands on a scheduling floor for these functions.
3. **Don't re-derive this.** Future audits that see a lone `fmuls fD,fA,fB` vs `fD,fB,fA` mismatch
   with *identical registers on both sides* should classify it immediately as a backend
   operand-order artifact (regalloc floor), not a hand-fixable commutative swap.

---

## Evidence index (reproducible)

- Worktree `wt/s3-fmuls` (branch `s3-fmuls`), primed `ninja`, scored via MCP
  `run_objdiff full_build:true`.
- FastInvert transforms 1–3 above (mtx.cpp:136–146), each measured; transforms reverted, worktree
  left at baseline (99.8%, idx48).
- Fixtures A/B compiled with `python3 -m tools.compiler_trace diff-asm <fixture> <fixture>
  --listing-type /FAcs`; machine bytes read from the emitted `.cod`. (Fixtures were throwaway,
  removed after reading; the bytes/listing excerpts are quoted inline above.)
- Toolchain: c2.dll `build/compilers/X360/16.00.11886.00/c2.dll`, 32-bit wibo
  `/home/free/code/milohax/wibo/build/debug/wibo`.
