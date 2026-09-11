#include "m.h"
static inline void InitP(P8 *p, int v) { p->x = v; }
void F(const char *s) {
    if (Cond(0)) { P8 a; InitP(&a, 1); Use(&a); }
    if (Cond(1)) { P8 b; InitP(&b, 2); Use(&b); }
    if (Cond(2)) { P8 c; InitP(&c, 3); Use(&c); }
}
