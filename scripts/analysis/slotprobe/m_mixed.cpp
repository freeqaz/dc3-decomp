#include "m.h"
void F(const char *s) {
    if (Cond(0)) { IN8 a(s); Use(&a); }
    else { char b[256]; Use(b); }
}
