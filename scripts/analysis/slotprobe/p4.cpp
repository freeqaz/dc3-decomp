#include "pre.h"
void F() {
    if (Cond()) {
        char a[256] = {0};
        Read(a, 256);
        Use(a);
    } else {
        char b[256] = {0};
        Read(b, 256);
    }
}
