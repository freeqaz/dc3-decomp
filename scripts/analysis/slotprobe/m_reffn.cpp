#include "m.h"
static inline void InitR(P8 &p, int v) { p.x = v; }
void F(const char *s) {
    if (Cond(0)) { P8 a; InitR(a, 1); Use(&a); }
    if (Cond(1)) { P8 b; InitR(b, 2); Use(&b); }
    if (Cond(2)) { P8 c; InitR(c, 3); Use(&c); }
}
