#include "m.h"
void F(const char *s) {
    if (Cond(0)) { char a[256]; a[0] = 1; Use(a); }
    if (Cond(1)) { char b[256]; b[0] = 2; Use(b); }
    if (Cond(2)) { char c[256]; c[0] = 3; Use(c); }
}
