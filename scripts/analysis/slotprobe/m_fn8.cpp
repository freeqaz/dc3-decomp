#include "m.h"
void F(const char *s) {
    if (Cond(0)) { F8 a; a.Init(1); Use(&a); }
    if (Cond(1)) { F8 b; b.Init(2); Use(&b); }
    if (Cond(2)) { F8 c; c.Init(3); Use(&c); }
}
