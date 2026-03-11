unsigned int zero_test(unsigned int x) {
    return x != 0;
}

unsigned int equality_nonzero(unsigned int x) {
    return x == 1;
}

unsigned int inequality_nonzero(unsigned int x) {
    return x != 1;
}

unsigned int signed_positive(int x) {
    return x > 0;
}

unsigned int unsigned_ordered(unsigned int x) {
    return x > 7;
}

unsigned int signed_ordered(int x) {
    return x > 7;
}
