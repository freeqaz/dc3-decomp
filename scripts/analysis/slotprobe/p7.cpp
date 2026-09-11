#include "pre.h"
void F() {
    if (Cond()) {
        Virt a;
        UseV(&a);
        Use(a.c);
    } else {
        Virt b;
        UseV(&b);
    }
}
