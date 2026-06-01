// scrolltotarget_xor_levers2.cpp  — round 2
// V0 already has the RIGHT registers (sign_adj=r11, sign_diff=r10, adjusted=r3,
// diff=r31) but the WRONG slot (sign first). Goal: flip slot to value-first at
// FIXED registers. Test fine-grained handles: order of the two srawi computations,
// order of the two xors, and whether re-reading the value inside the xor (vs the
// pre-bound temp) changes the symbol-ID tiebreak.
//
// COMPILE: see header of scrolltotarget_xor_levers.cpp (swap filename).

extern int numshowing();

// W0: baseline (== st_v0).
int st_w0(int target, int first, bool circular) {
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

// W1: inline the sign expr directly into the xor & subf (no named sign temp).
//     abs idiom spelled (v ^ (v>>31)) - (v>>31). value temp is the only named one.
int st_w1(int target, int first, bool circular) {
    int diff = target - first;
    if (circular) {
        int adjusted;
        if (diff > 0) adjusted = diff - numshowing();
        else          adjusted = numshowing() + diff;
        int absAdjusted = (adjusted ^ (adjusted >> 31)) - (adjusted >> 31);
        int absDiff     = (diff ^ (diff >> 31)) - (diff >> 31);
        if (absAdjusted < absDiff) return adjusted;
        if (absAdjusted == absDiff) return 1;
    }
    return diff;
}

// W2: standard library abs() (what RB3 source uses).
int st_w2(int target, int first, bool circular) {
    int diff = target - first;
    if (circular) {
        int adjusted;
        if (diff > 0) adjusted = diff - numshowing();
        else          adjusted = numshowing() + diff;
        int absAdjusted = adjusted < 0 ? -adjusted : adjusted;
        int absDiff     = diff < 0 ? -diff : diff;
        if (absAdjusted < absDiff) return adjusted;
        if (absAdjusted == absDiff) return 1;
    }
    return diff;
}

// W3: compute the two sign temps in the OPPOSITE order (sign_diff before sign_adj),
//     keep value-temps first. Tests whether sign-temp decl order (not value/sign
//     relative order) is the tiebreak handle.
int st_w3(int target, int first, bool circular) {
    int diff = target - first;
    if (circular) {
        int adjusted;
        if (diff > 0) adjusted = diff - numshowing();
        else          adjusted = numshowing() + diff;
        int sign_diff     = diff >> 31;
        int sign_adjusted = adjusted >> 31;
        int xor_adj = adjusted ^ sign_adjusted;
        int xor_dif = diff ^ sign_diff;
        int absAdjusted = xor_adj - sign_adjusted;
        int absDiff     = xor_dif - sign_diff;
        if (absAdjusted < absDiff) return adjusted;
        if (absAdjusted == absDiff) return 1;
    }
    return diff;
}

// W4: compute xor_dif before xor_adj (swap the two xor statements).
int st_w4(int target, int first, bool circular) {
    int diff = target - first;
    if (circular) {
        int adjusted;
        if (diff > 0) adjusted = diff - numshowing();
        else          adjusted = numshowing() + diff;
        int sign_adjusted = adjusted >> 31;
        int sign_diff     = diff >> 31;
        int xor_dif = diff ^ sign_diff;
        int xor_adj = adjusted ^ sign_adjusted;
        int absDiff     = xor_dif - sign_diff;
        int absAdjusted = xor_adj - sign_adjusted;
        if (absAdjusted < absDiff) return adjusted;
        if (absAdjusted == absDiff) return 1;
    }
    return diff;
}
