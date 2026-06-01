// commutative_regalloc_levers.cpp
//
// QUESTION (2026-06-01): docs 05/06 proved that the *spelling* swap (a+b<->b+a),
// statement order, and operand liveness do NOT move a commutative op's A/B slot
// order FOR A FIXED REGISTER ASSIGNMENT. But the slot order IS a deterministic
// function of the register allocation, and allocation is source-perturbable.
// This fixture tests the two allocation knobs that were NOT isolated before:
//
//   (1) OPERAND-PRODUCTION ORDER — for a commutative op with a FRESH destination
//       fed by two separately-produced values (the ScrollToTarget shape:
//       `x ^ (x>>31)`), does changing the order in which the two operand VALUES
//       are materialized change which physical register each lands in, and hence
//       the A/B slot? (distinct from spelling: we move the producing statements.)
//
//   (2) DEST-COALESCING STRUCTURE — for `r = a + b`, does writing it as an
//       accumulator (`r=a; r+=b` vs `r=b; r+=a`) force the allocator to coalesce
//       the destination with a DIFFERENT input, flipping which operand is the
//       dest-equal one (`add rX,rX,rY` vs `add rX,rY,rX`)?
//
// If EITHER knob flips the emitted operand order, the commutative floor is NOT
// closed — it is a register-allocation lever we can build + sweep. If NEITHER
// does (all forms emit byte-identical adds/xors), the floor stands even for
// these levers and we stop.
//
// COMPILE (no ninja build needed; uses checked-in toolchain):
//   python3 -c "from pathlib import Path; \
//     from tools.compiler_trace.invoker import CompilerInvoker; \
//     r=CompilerInvoker().compile_with_asm( \
//       Path('tools/compiler_trace/fixtures/commutative_regalloc_levers.cpp'), \
//       Path('tools/compiler_trace/fixtures/_out'), listing_type='/FAcs'); \
//     print('rc', r.returncode)"
//   grep -E 'PROC NEAR|lwz |xor |add |srawi|stw ' \
//     tools/compiler_trace/fixtures/_out/commutative_regalloc_levers.cod
//
// PPC integer A-form word: D=bits[21:25] A=bits[16:20] B=bits[11:15] xo=bits[1:5].
//   add  xo=266 -> `add rD,rA,rB`;  xor xo=316 -> `xor rA,rS,rB` (rS in D field).
// "Same registers, different slot" between two forms == the knob is a real lever.

extern void sink(int);
extern void fsink(float);
extern float fext(float);

// ---- (1) operand-production-order, FRESH-dest commutative add ----------------
// Two pointer loads feed one add whose result is sinked (dest not pinned to r3).

// 1a: produce a (p[0]) before b (p[1]).
void prod_ab(const int* p) {
    int a = p[0];
    int b = p[1];
    int r = a + b;          // fresh dest
    sink(r);
}

// 1b: produce b (p[1]) before a (p[0]) — SAME expression a+b, swapped PRODUCTION.
void prod_ba(const int* p) {
    int b = p[1];
    int a = p[0];
    int r = a + b;
    sink(r);
}

// 1c: control — spelling swap only (proven inert), production order unchanged.
void spell_ba(const int* p) {
    int a = p[0];
    int b = p[1];
    int r = b + a;          // spelling swapped, decl/production identical to 1a
    sink(r);
}

// ---- (2) dest-coalescing structure -------------------------------------------
// Same logical a+b, but accumulator form forces dest to coalesce with one input.

// 2a: dest coalesces with a (r initialized from a, then += b).
void acc_a(const int* p) {
    int r = p[0];
    r += p[1];
    sink(r);
}

// 2b: dest coalesces with b (r initialized from b, then += a).
void acc_b(const int* p) {
    int r = p[1];
    r += p[0];
    sink(r);
}

// ---- (3) the ScrollToTarget abs idiom, production-order variants -------------
// Real failing case: `xor r9,r3,r11` (target) vs `xor r9,r11,r3` (ours), x in r3,
// sign=x>>31 in r11, fresh dest r9. Does materializing sign vs x in different
// order move which lands in which register (and thus the xor slot)?

// 3a: x materialized, sign derived inline.
int abs_inline(const int* p) {
    int x = p[0];
    return (x ^ (x >> 31)) - (x >> 31);
}

// 3b: sign hoisted into a temp produced AFTER x.
int abs_sign_after(const int* p) {
    int x = p[0];
    int s = x >> 31;
    return (x ^ s) - s;
}

// 3c: sign temp produced, then x read again — sign "older" than the x use.
int abs_sign_first(const int* p) {
    int s = p[0] >> 31;
    int x = p[0];
    return (x ^ s) - s;
}

// 3d: spelling control (s ^ x) — should be inert vs 3b.
int abs_spell(const int* p) {
    int x = p[0];
    int s = x >> 31;
    return (s ^ x) - s;
}

// ============================================================================
// FP / fmuls — the OTHER op we were chasing (doc 05). Same reframe: operand
// order = f(FPR allocation). We ALREADY proved the callee-saved-FPR knob works
// (fpr_declaration_reorder banked +2.3% on Rot::Multiply, an fmuls/fadds region,
// by reordering float decls). So the fmuls "floor" is NOT monolithic — it is
// specifically the VOLATILE-FPR (f0-f13) cases, where there is no decl-order
// handle because volatile FPRs are assigned in scheduler value-readiness order.
// THE OPEN QUESTION: is there a source lever for VOLATILE-FPR assignment (the FP
// analog of the GPR production-order knob)?  These variants test it.
// ============================================================================

// 4a/4b: VOLATILE-FPR fresh-dest fmul, operand PRODUCTION order swapped.
// (Result passed to fsink, computed inline -> f0-f13, never callee-saved.)
void fp_prod_ab(const float* p) {
    float a = p[0];
    float b = p[1];
    fsink(a * b);
}
void fp_prod_ba(const float* p) {
    float b = p[1];
    float a = p[0];
    fsink(a * b);
}

// 4c: spelling control (b*a) — proven inert (doc 05), expect == 4a.
void fp_spell_ba(const float* p) {
    float a = p[0];
    float b = p[1];
    fsink(b * a);
}

// 5a/5b: CALLEE-SAVED-FPR positive control. Force a,b live across a call so they
// land in f14-f31; reorder the decls. This is the PROVEN lever (Rot::Multiply) —
// if the fmuls operand order differs between 5a and 5b, it confirms callee-saved
// fmuls IS reachable from source (refuting "fmuls is a closed floor").
float fp_callee_ab(const float* p) {
    float a = p[0];
    float b = p[1];
    float pre = fext(p[2]);   // call: a,b must survive -> callee-saved FPRs
    return a * b + pre;
}
float fp_callee_ba(const float* p) {
    float b = p[1];
    float a = p[0];
    float pre = fext(p[2]);
    return a * b + pre;
}
