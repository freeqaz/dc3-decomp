// scrolltotarget_xor_levers.cpp
//
// Models UIListState::ScrollToTarget's abs-idiom xor exactly to find the source
// ordering that flips the commutative xor slot from sign-first to VALUE-first.
//
// Target (real DC3) emits VALUE operand FIRST:
//     xor r9, r3(adjusted), r11(sign_adjusted)
//     xor r8, r31(diff),    r10(sign_diff)
// Our current source emits SIGN first (xor r9, r11, r3).
//
// From commutative_regalloc_levers.cpp we proved: for the 2-operand xor of a
// value temp and its sign temp, **whichever temp is declared SECOND lands in the
// FIRST xor slot** (abs_sign_after: x decl 1st, s decl 2nd -> xor s,x [sign first];
// abs_sign_first: s decl 1st, x decl 2nd -> xor x,s [value first]).
//
// So to get VALUE first we must declare the SIGN temp BEFORE the VALUE temp.
// The complication in ScrollToTarget: `adjusted` is branch-computed, so the sign
// `adjusted>>31` can't trivially precede it. These variants test re-rootings.
//
// COMPILE:
//   python3 -c "from pathlib import Path; \
//     from tools.compiler_trace.invoker import CompilerInvoker; \
//     r=CompilerInvoker().compile_with_asm( \
//       Path('tools/compiler_trace/fixtures/scrolltotarget_xor_levers.cpp'), \
//       Path('tools/compiler_trace/fixtures/_out'), listing_type='/FAcs'); \
//     print('rc', r.returncode)"
//   grep -E 'PROC NEAR|xor |srawi' tools/compiler_trace/fixtures/_out/scrolltotarget_xor_levers.cod

extern int numshowing();

// V0: current dc3 source shape (value decl first, sign second). Expect SIGN-first xor.
int st_v0(int target, int first, bool circular) {
    int diff = target - first;
    if (circular) {
        int adjusted;
        if (diff > 0) adjusted = diff - numshowing();
        else          adjusted = numshowing() + diff;
        int sign_adjusted = adjusted >> 31;
        int sign_diff     = diff >> 31;
        int xor_adj = adjusted ^ sign_adjusted;
        int xor_dif = diff ^ sign_diff;
        int absAdjusted = xor_adj - sign_adjusted;
        int absDiff     = xor_dif - sign_diff;
        if (absAdjusted < absDiff) return adjusted;
        if (absAdjusted == absDiff) return 1;
    }
    return diff;
}

// V1: declare BOTH sign temps before the value temps (re-root off root exprs).
// sign_diff from (target-first); sign_adjusted needs adjusted, so compute adjusted
// into a SEPARATE pre-temp 'adj0' first but DECLARE sign_adjusted before the temp
// that the xor reads ('adjusted'). i.e. value-temp-read-by-xor is declared last.
int st_v1(int target, int first, bool circular) {
    int diff = target - first;
    if (circular) {
        int adj0;
        if (diff > 0) adj0 = diff - numshowing();
        else          adj0 = numshowing() + diff;
        int sign_adjusted = adj0 >> 31;   // sign declared BEFORE the xor's value temp
        int sign_diff     = diff >> 31;
        int adjusted = adj0;              // value temp the xor reads, declared AFTER sign
        int xor_adj = adjusted ^ sign_adjusted;
        int xor_dif = diff ^ sign_diff;
        int absAdjusted = xor_adj - sign_adjusted;
        int absDiff     = xor_dif - sign_diff;
        if (absAdjusted < absDiff) return adjusted;
        if (absAdjusted == absDiff) return 1;
    }
    return diff;
}

// V2: sign temps declared before, value re-read fresh for the xor (abs_sign_first shape).
// diff is cheap to re-root; adjusted via pre-temp.
int st_v2(int target, int first, bool circular) {
    int sign_diff = (target - first) >> 31;   // sign_diff BEFORE diff
    int diff = target - first;
    if (circular) {
        int adj0;
        if (diff > 0) adj0 = diff - numshowing();
        else          adj0 = numshowing() + diff;
        int sign_adjusted = adj0 >> 31;
        int adjusted = adj0;
        int xor_adj = adjusted ^ sign_adjusted;
        int xor_dif = diff ^ sign_diff;
        int absAdjusted = xor_adj - sign_adjusted;
        int absDiff     = xor_dif - sign_diff;
        if (absAdjusted < absDiff) return adjusted;
        if (absAdjusted == absDiff) return 1;
    }
    return diff;
}

// V3: minimal — only swap the decl order of (value temp, sign temp) WITHOUT re-root,
// by declaring sign_adjusted/sign_diff above the value temps and assigning later.
// Tests whether DECLARATION position (not init position) is what c2's symbol-ID uses.
int st_v3(int target, int first, bool circular) {
    int diff = target - first;
    if (circular) {
        int sign_adjusted, sign_diff, adjusted;  // signs declared first
        if (diff > 0) adjusted = diff - numshowing();
        else          adjusted = numshowing() + diff;
        sign_adjusted = adjusted >> 31;
        sign_diff     = diff >> 31;
        int xor_adj = adjusted ^ sign_adjusted;
        int xor_dif = diff ^ sign_diff;
        int absAdjusted = xor_adj - sign_adjusted;
        int absDiff     = xor_dif - sign_diff;
        if (absAdjusted < absDiff) return adjusted;
        if (absAdjusted == absDiff) return 1;
    }
    return diff;
}
