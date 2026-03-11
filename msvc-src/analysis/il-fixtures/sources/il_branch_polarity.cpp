// Branch polarity fixture: how if/else conditions map to IL branch opcodes.
// Tests: condition inversion, nested branches, early return patterns.

extern int get_value();
extern void do_a();
extern void do_b();
extern void do_c();

// Simple if/else with == 0
void branch_eq_zero(int x) {
    if (x == 0) {
        do_a();
    } else {
        do_b();
    }
}

// Simple if/else with != 0
void branch_ne_zero(int x) {
    if (x != 0) {
        do_a();
    } else {
        do_b();
    }
}

// Early return (guard pattern)
void branch_guard(int x) {
    if (x == 0) return;
    do_a();
    do_b();
}

// Nested conditions
void branch_nested(int x, int y) {
    if (x > 0) {
        if (y > 0) {
            do_a();
        } else {
            do_b();
        }
    } else {
        do_c();
    }
}

// Comparison operators
void branch_signed_gt(int x) {
    if (x > 5) do_a(); else do_b();
}

void branch_unsigned_gt(unsigned int x) {
    if (x > 5) do_a(); else do_b();
}

void branch_signed_le(int x) {
    if (x <= 0) do_a(); else do_b();
}
