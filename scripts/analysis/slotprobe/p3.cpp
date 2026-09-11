#include "pre.h"
void F() {
    if (Cond()) {
        char a[256];
        Read(a, 256);
        Use(a);
    }
    if (Cond()) {
        char b[256];
        Read(b, 256);
    }
}
