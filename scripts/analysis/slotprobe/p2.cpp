#include "pre.h"
void F() {
    char a[256];
    char b[256];
    if (Cond()) {
        Read(a, 256);
        Use(a);
    } else {
        Read(b, 256);
    }
}
