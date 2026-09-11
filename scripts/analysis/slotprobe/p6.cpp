#include "pre.h"
void F() {
    if (Cond()) {
        Dtor a;
        UseD(&a);
        Use(a.c);
    } else {
        Dtor b;
        UseD(&b);
    }
}
