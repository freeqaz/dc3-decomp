// abs_spellings.cpp
//
// MINIMAL A/B control proving the integer-abs commutative-xor slot lever
// (see docs/plans/port-harvest/stream3-ideas/06-commutative-int-and-levers.md
// CORRECTION section). Behaviour-identical abs spellings lower to DIFFERENT
// commutative xor operand slot order on cl.exe 16.00.11886 / c2.dll, on the
// SAME physical registers:
//
//   a_ternary    (x<0?-x:x) -> srawi r10,r11,31 ; xor r11,r11,r10  (VALUE first)
//   a_ternary_ge (x>=0?x:-x)-> identical                            (VALUE first)
//   a_bitwise    (x^s)-s    -> srawi r11,r10,31 ; xor r10,r11,r10  (SIGN  first)
//   a_bitwise_inline        -> identical to a_bitwise               (SIGN  first)
//
// The ternary selects the compiler's intrinsic abs lowering (VALUE-first); the
// open-coded bitwise sequence canonicalizes to SIGN-first. This is the source
// lever behind the int_abs_to_ternary permuter pattern.
//
// COMPILE (checked-in toolchain, no ninja):
//   python3 -c "from pathlib import Path; \
//     from tools.compiler_trace.invoker import CompilerInvoker; \
//     r=CompilerInvoker().compile_with_asm( \
//       Path('tools/compiler_trace/fixtures/abs_spellings.cpp'), \
//       Path('tools/compiler_trace/fixtures/_out'), listing_type='/FAcs'); \
//     print('rc', r.returncode)"
//   grep -E 'PROC NEAR|srawi|xor ' tools/compiler_trace/fixtures/_out/abs_spellings.cod

extern int sink(int);

// Behaviourally-identical abs spellings; observe value-first vs sign-first xor.
int a_ternary(const int* p)        { int x = p[0]; int a = x < 0 ? -x : x;        return sink(a); }
int a_ternary_ge(const int* p)     { int x = p[0]; int a = x >= 0 ? x : -x;        return sink(a); }
int a_bitwise(const int* p)        { int x = p[0]; int s = x >> 31; int a = (x ^ s) - s; return sink(a); }
int a_bitwise_inline(const int* p) { int x = p[0]; int a = (x ^ (x >> 31)) - (x >> 31); return sink(a); }
