// Switch dispatch fixture: IL SWITCH/SWITCH_TABLE/CASE opcodes.
// Tests: small switch, large switch, default fall-through, sparse cases.

extern void handle_a();
extern void handle_b();
extern void handle_c();
extern void handle_default();
extern int get_value();

// Small dense switch (3 cases)
void switch_small(int x) {
    switch (x) {
        case 0: handle_a(); break;
        case 1: handle_b(); break;
        case 2: handle_c(); break;
        default: handle_default(); break;
    }
}

// Larger dense switch (6 cases)
int switch_dense(int x) {
    switch (x) {
        case 0: return 10;
        case 1: return 20;
        case 2: return 30;
        case 3: return 40;
        case 4: return 50;
        case 5: return 60;
        default: return -1;
    }
}

// Sparse switch (non-contiguous cases)
void switch_sparse(int x) {
    switch (x) {
        case 1: handle_a(); break;
        case 10: handle_b(); break;
        case 100: handle_c(); break;
        default: handle_default(); break;
    }
}

// Switch with fall-through
int switch_fallthrough(int x) {
    int result = 0;
    switch (x) {
        case 3:
            result += 100;
            // fall through
        case 2:
            result += 10;
            break;
        case 1:
            result = 1;
            break;
    }
    return result;
}

// Switch on enum-like values
void switch_enum(unsigned int state) {
    switch (state) {
        case 0: handle_a(); break;
        case 1: handle_b(); break;
        case 2: handle_c(); break;
    }
}
