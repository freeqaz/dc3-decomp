//==============================================================================
// fmuls / fadds / fmadds operand-order proof fixtures
//==============================================================================
// PURPOSE
//   Empirically prove (or break) the model in
//   docs/plans/port-harvest/stream3-ideas/05-fmuls-operand-order.md:
//
//     "The operand order of a commutative FP op (fmuls fD,fA,fC; fadds fD,fA,fB;
//      fmadds fD,fA,fC,fB) is decided by the backend AFTER register allocation,
//      driven by which PHYSICAL FPR each operand lands in (and which register is
//      reused as the destination) -- NOT by source operand spelling, statement
//      order, or operand liveness."
//
//   Each function below is a tiny, self-contained probe of ONE variable in the
//   model. The header comment for each states the PREDICTION (what the model
//   says MSVC should emit and WHY) and the OBSERVED machine bytes/mnemonic read
//   from the real toolchain's /FAcs listing, marked PROVEN / BROKEN.
//
// HOW TO RE-RUN  (from a worktree or the repo root)
//   python3 -c "from pathlib import Path; \
//     from tools.compiler_trace.invoker import CompilerInvoker; \
//     CompilerInvoker().compile_with_asm( \
//       Path('tools/compiler_trace/fixtures/fmuls_operand_order.cpp'), \
//       Path('tools/compiler_trace/fixtures/_out'), listing_type='/FAcs')"
//   grep -E 'PROC NEAR|lfs |fmuls|fadds|fmadds|stfs' \
//     tools/compiler_trace/fixtures/_out/fmuls_operand_order.cod
//   (see README.md in this directory for the full procedure + decode table)
//
// TOOLCHAIN
//   cl.exe 16.00.11886.00 / c2.dll, 32-bit wibo, project flags /O1 /Oi /GR /EHsc
//   (each function reports "; Function compile flags: /Ogsu" -- the per-function
//   expansion of /O1, identical to the real DC3 build).
//
// PPC A-form decode (so the hex below is checkable by hand):
//   word = 0x3B<<26 ... operands packed as:  D=bits[21:25] A=[16:20]
//                                             B=[11:15]    C=[6:10]  xo=[1:5]
//   fmuls  xo=25 (0x19): operands fD,fA,fC      (B field is 0)
//   fadds  xo=21 (0x15): operands fD,fA,fB      (C field is 0)
//   fmadds xo=29 (0x1D): operands fD,fA,fC,fB
//==============================================================================

extern void sink(float);

//------------------------------------------------------------------------------
// FIXTURE 1 -- BASELINE COMMUTATIVITY (source spelling is discarded)
//------------------------------------------------------------------------------
// PREDICTION: f1_ab (a*b) and f1_ba (b*a) load the SAME two values into the same
//   physical FPRs, so per the model they must emit a BYTE-IDENTICAL fmuls -- the
//   source operand order has no effect.
// OBSERVED: BOTH -> lfs fr0,4(r3); lfs fr13,0(r3); fmuls fr1,fr0,fr13 (ec200372)
//   Byte-identical. Source spelling discarded.  => PROVEN
float f1_ab(float *p) { return p[0] * p[1]; }
float f1_ba(float *p) { return p[1] * p[0]; }

//------------------------------------------------------------------------------
// FIXTURE 2 -- REGISTER-NUMBER / SCHEDULE DEPENDENCE (the A/C slot follows regs)
//------------------------------------------------------------------------------
// Two variants with IDENTICAL source product a*b but a DIFFERENT physical-FPR
// assignment forced by the surrounding code:
//
//   f2_free:  no extra use. The scheduler is free and loads p[1]->fr0 FIRST,
//             p[0]->fr13.  => fmuls fr1,fr0,fr13  (fr0 in fA)
//   f2_pinned: an extra use of `a` (stored to *out) anchors the load order so
//             a=p[0]->fr0, b=p[1]->fr13.          => fmuls fr1,fr13,fr0 (fr0 in fC)
//
// PREDICTION: the A/C slot of the SAME source product flips (fr0 moves from fA to
//   fC) purely because the register-to-value assignment changed -- the source
//   product spelling is identical in both. This proves the slot tracks the
//   physical register / schedule, not the source.
// OBSERVED:
//   f2_free   -> lfs fr0,4(r3); lfs fr13,0(r3); fmuls fr1,fr0,fr13 (ec200372)
//   f2_pinned -> lfs fr0,0(r3); lfs fr13,4(r3); stfs fr0,0(r4);
//                fmuls fr1,fr13,fr0 (ec2d0032)
//   Same source product, OPPOSITE A/C order, driven entirely by which value the
//   scheduler placed in fr0.  => PROVEN (the slot is register/schedule-driven)
float f2_free(float *p) { float a = p[0]; float b = p[1]; return a * b; }
float f2_pinned(float *p, float *out) {
    float a = p[0];
    float b = p[1];
    *out = a;           // extra use anchors a into fr0 (loaded first)
    return a * b;
}

//------------------------------------------------------------------------------
// FIXTURE 3 -- DESTINATION-REUSE (where the dest-equal operand lands)
//------------------------------------------------------------------------------
// ABI: a=fr1, b=fr2, return value also fr1, so the destination register fr1
// equals source operand `a`.
// PREDICTION (our c2 build): the destination-equal operand lands in fA (FIRST).
//   This is the recurring divergence direction from doc 05: our build puts the
//   dest-equal register FIRST; the real DC3 target tends to put it SECOND on the
//   otherwise-identical assignment.
// OBSERVED: fmuls fr1,fr1,fr2 (ec2100b2).  dest-equal operand (fr1) is fA/FIRST.
//   => PROVEN (our build: dest-equal -> fA)
float f3_dest_reuse(float x, float y) { return x * y; }

//------------------------------------------------------------------------------
// FIXTURE 4 -- LIVENESS PROBE (the killer test: liveness does NOT move the slot)
//------------------------------------------------------------------------------
// Same load layout in all four (a=p[0]->fr0, b=p[1]->fr13). We vary WHICH
// operand is kept live after the multiply, and in what order.
// PREDICTION: given the FIXED register assignment, the fmuls A/C order is
//   INVARIANT to liveness/store ordering -- all four emit byte-identical fmuls.
//   (Storing an operand changes only the stfs; it does not move the operand
//   across the A/C boundary, because the boundary is fixed by the register
//   assignment, which is the same in all four.)
// OBSERVED: ALL FOUR -> fmuls fr1,fr13,fr0 (ec2d0032), byte-identical. Only the
//   stfs source/order differs.  => PROVEN (liveness is inert w.r.t. A/C order)
float f4_a_live(float *p, float *out)  { float a=p[0]; float b=p[1]; *out=a; return a*b; }
float f4_b_live(float *p, float *out)  { float a=p[0]; float b=p[1]; *out=b; return a*b; }
float f4_a_last(float *p, float *out)  { float a=p[0]; float b=p[1]; *out=b; out[1]=a; return a*b; }
float f4_b_last(float *p, float *out)  { float a=p[0]; float b=p[1]; *out=a; out[1]=b; return a*b; }

//------------------------------------------------------------------------------
// FIXTURE 5 -- CALLEE-SAVED FPR ASSIGNMENT (float-decl order -> f31,f30,...)
//------------------------------------------------------------------------------
// Forcing a,b live across a call parks them in callee-saved FPRs assigned by
// float-declaration order: 1st float -> fr31, 2nd -> fr30 (doc 01 rule).
// PREDICTION: the product uses fr30/fr31; the A/C order is again register-driven
//   (here fr30 in fA, fr31 in fC) -- and NOT a simple "lower-first" rule, since
//   fixture 2/4 show {fr0,fr13} appearing in BOTH orders. This documents that the
//   slot rule is a post-regalloc scheduler artifact, not characterizable as
//   "low reg first" or "high reg first".
// OBSERVED: lfs fr31,0(r3); lfs fr30,4(r3); fadds fr1,fr30,fr31 (ec3ef82a);
//   bl sink; fmuls fr1,fr30,fr31 (ec3e07f2).  fr30 in fA, fr31 in fC.
//   => PROVEN (register-driven; lower reg fr30 happens to be fA here, but
//      fixture 2 shows the same reg set can take either order -> NOT a reg-#
//      rule)
float f5_callee_saved(float *p) {
    float a = p[0];   // -> fr31 (1st float)
    float b = p[1];   // -> fr30 (2nd float)
    sink(a + b);      // forces a,b live across the call
    return a * b;
}

//------------------------------------------------------------------------------
// FIXTURE 6 -- fadds GENERALIZATION
//------------------------------------------------------------------------------
// PREDICTION: fadds obeys the identical discipline -- a+b vs b+a byte-identical;
//   the A/B slot tracks the register assignment exactly like fmuls.
// OBSERVED:
//   f6_add_ab / f6_add_ba (ABI args)  -> fadds fr1,fr1,fr2 (ec21102a) IDENTICAL
//   f6_padd_ab / f6_padd_ba (ptr load)-> lfs fr0,4(r3); lfs fr13,0(r3);
//                                        fadds fr1,fr0,fr13 (ec20682a) IDENTICAL
//   The ptr-load fadds (fr1,fr0,fr13) mirrors the ptr-load fmuls (fixture 1)
//   exactly.  => PROVEN (rule generalizes to fadds)
float f6_add_ab(float a, float b)  { return a + b; }
float f6_add_ba(float a, float b)  { return b + a; }
float f6_padd_ab(float *p)         { return p[0] + p[1]; }
float f6_padd_ba(float *p)         { return p[1] + p[0]; }

//------------------------------------------------------------------------------
// FIXTURE 7 -- fmadds GENERALIZATION (3-operand; multiply AND add both commute)
//------------------------------------------------------------------------------
// PREDICTION: fmadds fD,fA,fC,fB obeys the same discipline. Commuting the
//   multiply part (a*b -> b*a) AND/OR the add part (a*b+c -> c+a*b) is fully
//   discarded -- all variants byte-identical; the slots track registers.
// OBSERVED:
//   f7_madd_ab (a*b+c) / f7_madd_ba (b*a+c) / f7_madd_cleft (c+a*b)
//        ALL -> fmadds fr1,fr1,fr2,fr3 (ec2118ba), byte-identical.
//   f7_pmadd (ptr loads, p[0]*p[1]+p[2])
//        -> lfs fr0,4(r3); lfs fr13,0(r3); lfs fr12,8(r3);
//           fmadds fr1,fr0,fr13,fr12 (ec20637a)
//        -- the (fA=fr0, fC=fr13) multiplicand pair matches the fmuls ptr-load
//           layout (fixture 1) exactly; the addend lands in fB.
//   => PROVEN (rule generalizes to fmadds; both commutations discarded)
float f7_madd_ab(float a, float b, float c)    { return a * b + c; }
float f7_madd_ba(float a, float b, float c)    { return b * a + c; }
float f7_madd_cleft(float a, float b, float c) { return c + a * b; }
float f7_pmadd(float *p)                       { return p[0] * p[1] + p[2]; }
