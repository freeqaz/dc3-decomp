#include "pre.h"
void F() {
    if (Cond()) {
        Pod a;
        UsePod(&a);
        Use(a.c);
    } else {
        Pod b;
        UsePod(&b);
    }
}
