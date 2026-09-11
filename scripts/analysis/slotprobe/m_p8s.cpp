#include "m.h"
void F(const char *s) {
    if (Cond(0)) { P8 a; a.x = 1; Use(&a); }
    if (Cond(1)) { P8 b; b.x = 2; Use(&b); }
    if (Cond(2)) { P8 c; c.x = 3; Use(&c); }
}
