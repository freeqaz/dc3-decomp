//==============================================================================
// INTEGER commutative operand-order proof fixtures
//==============================================================================
// PURPOSE
//   The FP subset of objdiff's COMMUTATIVE_OP_ORDER (fmuls/fadds/fmadds operand
//   order) was already proven NON-source-fixable (see
//   docs/plans/port-harvest/stream3-ideas/05-fmuls-operand-order.md and the
//   companion fmuls_operand_order.cpp). The INTEGER subset (`xor`, `add`, `or`,
//   `and`, `mullw`, `subf`-of-commuted-adds, ...) was explicitly NOT disproven.
//   This file tests it through the SAME real toolchain to decide whether the
//   A/B operand order of an integer commutative op is controllable from source.
//
//   Motivating real functions (all SAME-register pure-swap signatures, the same
//   shape as the FP floor):
//     UIListState::ScrollToTarget  idx18 `xor r9,r3,r11`  vs base `xor r9,r11,r3`
//     ByteGrinder::op0/op6         idx21 `xor r11,r10,r11` vs base `xor r11,r11,r10`
//     SongCollision::Equals        idx31 `add r8,r7,r10`   vs base `add r8,r10,r7`
//     RndBitmap::Create            idx63 `add r25,r28,r11` vs base `add r25,r11,r28`
//     LightPreset::GetKey          idx100 `add r10,r25,r29` vs base `add r10,r29,r25`
//
//   Each function below is a tiny probe of ONE variable in the model. The header
//   comment states the PREDICTION and the OBSERVED machine bytes/mnemonic read
//   from the real toolchain's /FAcs listing, marked PROVEN / BROKEN.
//
// HOW TO RE-RUN  (from a worktree or the repo root)
//   python3 -c "from pathlib import Path; \
//     from tools.compiler_trace.invoker import CompilerInvoker; \
//     r=CompilerInvoker().compile_with_asm( \
//       Path('tools/compiler_trace/fixtures/int_commutative_operand_order.cpp'), \
//       Path('tools/compiler_trace/fixtures/_out'), listing_type='/FAcs'); \
//     print('rc', r.returncode)"
//   grep -E 'PROC NEAR|^  [0-9a-f]+\t' \
//     tools/compiler_trace/fixtures/_out/int_commutative_operand_order.cod
//   (see README.md in this directory for the full procedure + decode table)
//
// TOOLCHAIN
//   cl.exe 16.00.11886.00 / c2.dll, 32-bit wibo, project flags /O1 /Oi /GR /EHsc
//   (each function reports "; Function compile flags: /Ogsu" -- the per-function
//   expansion of /O1, identical to the real DC3 build).
//
// PPC X/XO-form integer decode (so the hex below is checkable by hand):
//   For the register-register forms the word packs operands as:
//     primary opcode = bits[0:5], rD/rS=bits[6:10], rA=bits[11:15],
//     rB=bits[16:20], extended-opcode=bits[21:30]
//   `xor  rA,rS,rB`  : opcode 31, xo=316.  emitted as  rS ^ rB -> rA
//                      (NB: the X-form *names* the dest rA, but the listing
//                       prints `xor rDEST, rSRC1, rSRC2`; SRC1/SRC2 are the
//                       commutable pair we care about.)
//   `add  rD,rA,rB`  : opcode 31, xo=266.  rA + rB -> rD
//   `or   rA,rS,rB`  : opcode 31, xo=444.
//   `and  rA,rS,rB`  : opcode 31, xo=28.
//   `mullw rD,rA,rB` : opcode 31, xo=235.
//   We compare the *listing mnemonic* (`add rD,rX,rY`) byte-for-byte; the
//   commutative question is purely whether rX/rY (the two source slots) swap.
//==============================================================================

extern void sink(int);
extern int  side(int);

//------------------------------------------------------------------------------
// FIXTURE 1 -- BASELINE COMMUTATIVITY (source spelling, two ptr loads)
//------------------------------------------------------------------------------
// PREDICTION: i1_xor_ab (a^b) and i1_xor_ba (b^a) load the SAME two values into
//   the same physical GPRs, so if the INT op behaves like the FP op the emitted
//   `xor` is BYTE-IDENTICAL -- source operand spelling discarded. If INT DIFFERS
//   from FP we would instead see the two slots follow the source order.
// OBSERVED: BOTH -> lwz r11,4(r3); lwz r10,0(r3); xor r3,r11,r10 (7d635278)
//   Byte-identical. Source spelling discarded -- exactly like FP. => PROVEN
//   Note the slots already DON'T match source: i1_xor_ab spelled p[0]^p[1] but
//   r11 holds p[1] (loaded first) and is slot-1; the compiler ignores spelling.
int i1_xor_ab(int *p) { return p[0] ^ p[1]; }
int i1_xor_ba(int *p) { return p[1] ^ p[0]; }

//------------------------------------------------------------------------------
// FIXTURE 1b -- BASELINE COMMUTATIVITY for `add` and `or`
//------------------------------------------------------------------------------
// PREDICTION: same as 1 -- if INT mirrors FP, a+b vs b+a is byte-identical.
// OBSERVED: add_ab == add_ba -> add r3,r11,r10 (7c6b5214) byte-identical.
//           or_ab  == or_ba  -> or  r3,r11,r10 (7d635378) byte-identical.
//   => PROVEN (add and or both discard source spelling like xor / like FP)
int i1_add_ab(int *p) { return p[0] + p[1]; }
int i1_add_ba(int *p) { return p[1] + p[0]; }
int i1_or_ab(int *p)  { return p[0] | p[1]; }
int i1_or_ba(int *p)  { return p[1] | p[0]; }

//------------------------------------------------------------------------------
// FIXTURE 2 -- REGISTER/SCHEDULE DEPENDENCE (does the slot follow the register?)
//------------------------------------------------------------------------------
// Same source product a+b but a DIFFERENT physical-GPR assignment forced by the
// surrounding code. i2_free: scheduler picks load order. i2_pinned: an extra use
// of `a` anchors its load earlier (like the FP f2_pinned probe).
// PREDICTION: if INT mirrors FP, the A/B slot of the SAME source `a+b` flips
//   purely because the value->register assignment changed (slot tracks register,
//   not source). If INT is source-controllable instead, the slot stays put.
// OBSERVED:
//   i2_free   -> lwz r11,4(r3); lwz r10,0(r3); add r3,r11,r10 (7c6b5214)
//   i2_pinned -> lwz r11,0(r3); lwz r10,4(r3); add r3,r10,r11 (7c6a5a14); stw r11,0(r4)
//   SAME source product a+b, OPPOSITE slot order, driven entirely by which value
//   the scheduler placed where (the store of `a` anchored p[0]->r11 loaded first).
//   This is the decisive positive control. => PROVEN (slot is register/schedule
//   driven, NOT source-driven -- identical to FP fixture 2)
int i2_free(int *p)            { int a = p[0]; int b = p[1]; return a + b; }
int i2_pinned(int *p, int *out){ int a = p[0]; int b = p[1]; *out = a; return a + b; }

//------------------------------------------------------------------------------
// FIXTURE 3 -- DESTINATION-REUSE (where the dest-equal operand lands)
//------------------------------------------------------------------------------
// ABI: a=r3, b=r4, return value also r3, so the destination register r3 equals
// source operand `a`. (Mirror of the fmuls f3_dest_reuse probe.)
// PREDICTION: read which slot the dest-equal operand lands in; compare against
//   the FP rule "our build puts dest-equal operand FIRST".
// OBSERVED: add r3,r3,r4 (7c632214); xor r3,r3,r4 (7c632278).
//   dest-equal operand (r3) is FIRST -- same canonicalization direction as the
//   FP build's fmuls f3_dest_reuse. => PROVEN (dest-equal -> first slot)
int i3_dest_reuse_add(int a, int b) { return a + b; }
int i3_dest_reuse_xor(int a, int b) { return a ^ b; }

//------------------------------------------------------------------------------
// FIXTURE 4 -- LIVENESS PROBE (the killer test: does liveness move the slot?)
//------------------------------------------------------------------------------
// Same load layout in all four (a=p[0], b=p[1]). Vary WHICH operand is kept live
// after the op and in what store order. (Mirror of fmuls f4_*.)
// PREDICTION: if INT mirrors FP, the A/B order is INVARIANT to liveness/store
//   ordering -- all four byte-identical, only the stw differs. If INT is
//   source-controllable, liveness moves the slot.
// OBSERVED (the nuance that CONFIRMS the FP model rather than breaking it):
//   i4_a_live  -> lwz r11,0(r3); lwz r10,4(r3); add r3,r10,r11 (7c6a5a14)
//   i4_a_last  -> lwz r11,0(r3); lwz r10,4(r3); add r3,r10,r11 (7c6a5a14)
//   i4_b_last  -> lwz r11,0(r3); lwz r10,4(r3); add r3,r10,r11 (7c6a5a14)
//   i4_b_live  -> lwz r11,4(r3); lwz r10,0(r3); add r3,r11,r10 (7c6b5214)
//   The `add` BYTES differ between a_live and b_live -- BUT NOT because the
//   source moved the slot. Storing `a` vs `b` changed the LOAD ORDER (the
//   stored/earlier-live value is loaded into r11 first), which moved which
//   VARIABLE lands in r10 vs r11. In ALL four the slot order is fixed as a
//   function of the PHYSICAL REGISTER: the value in r11 is always slot-2, the
//   value in r10 is always slot-1 (`add rD,r10,r11`). The source variable's
//   slot flips only because liveness recolored it. => PROVEN: the slot tracks
//   the physical register, NOT the source operand -- the exact FP killer-test
//   result. (Liveness is a register-allocation lever, not an operand-order
//   lever; it moves the variable, the slot still follows the register.)
int i4_a_live(int *p, int *out) { int a=p[0]; int b=p[1]; *out=a; return a+b; }
int i4_b_live(int *p, int *out) { int a=p[0]; int b=p[1]; *out=b; return a+b; }
int i4_a_last(int *p, int *out) { int a=p[0]; int b=p[1]; *out=b; out[1]=a; return a+b; }
int i4_b_last(int *p, int *out) { int a=p[0]; int b=p[1]; *out=a; out[1]=b; return a+b; }

//------------------------------------------------------------------------------
// FIXTURE 5 -- ABS IDIOM (the exact UIListState::ScrollToTarget shape)
//------------------------------------------------------------------------------
// abs(x) compiled as (x ^ (x>>31)) - (x>>31). The `xor` of value^sign is the
// real-function mismatch (target `xor rD, value, sign`; our build sometimes
// emits `xor rD, sign, value`). Vary the source spelling of the xor.
// PREDICTION: if INT mirrors FP, both spellings byte-identical.
// OBSERVED: BOTH -> srawi r11,r3,31; xor r10,r11,r3 (7d6a1a78); subf r3,r11,r10
//   Byte-identical. The exact ScrollToTarget abs-idiom xor: spelling value^sign
//   vs sign^value is discarded. (Note the emitted slot is `xor rD, sign, value`
//   = r11 first; the real ScrollToTarget target wants `xor rD, value, sign`, a
//   pure same-register slot swap our build does not reproduce from source.)
//   => PROVEN (abs-idiom xor slot is not source-controllable)
int i5_abs_vs(int x) { int s = x >> 31; int t = x ^ s; return t - s; } // value^sign
int i5_abs_sv(int x) { int s = x >> 31; int t = s ^ x; return t - s; } // sign^value

//------------------------------------------------------------------------------
// FIXTURE 6 -- CALLEE-SAVED GPR ASSIGNMENT (values live across a call)
//------------------------------------------------------------------------------
// Force a,b live across a call so they land in callee-saved GPRs (r31,r30,...).
// (Mirror of fmuls f5_callee_saved.) Tests whether the slot rule depends on the
// register number or is, like FP, a non-monotonic scheduler artifact.
// PREDICTION: if INT mirrors FP, the slot is register-driven and not a simple
//   "low reg first" rule.
// OBSERVED: lwz r31,0(r3); lwz r30,4(r3); add r3,r30,r31 (7c7efa14); bl sink;
//           xor r3,r30,r31 (7fc3fa78). r30 first, r31 second -- register-driven,
//   same shape as FP f5_callee_saved (fr30 first, fr31 second). => PROVEN
int i6_callee_saved(int *p) {
    int a = p[0];
    int b = p[1];
    sink(a + b);     // forces a,b live across the call
    return a ^ b;
}

//------------------------------------------------------------------------------
// FIXTURE 7 -- TWO INDEPENDENT ADDS sharing a value (SongCollision shape)
//------------------------------------------------------------------------------
// SongCollision::Equals emitted two `add` swaps (idx31 `add r8,r7,r10`, idx35
// `add r11,r7,r11`) where a shared base r7 is added to two different offsets.
// Probe whether the shared-value position in source moves the slot.
// PREDICTION: if INT mirrors FP, shared-first vs shared-second byte-identical.
// OBSERVED: BOTH adds -> lwz r11,off(r3); add r11,r11,r4 (7d6b2214); stw ...
//   `base + p[0]` and `p[1] + base` emit the IDENTICAL add (loaded value first,
//   shared base r4 second) regardless of source operand order. => PROVEN
//   (shared-value position in source is discarded)
void i7_shared_add(int *p, int base, int *out) {
    out[0] = base + p[0];   // shared base FIRST
    out[1] = p[1] + base;   // shared base SECOND
}
