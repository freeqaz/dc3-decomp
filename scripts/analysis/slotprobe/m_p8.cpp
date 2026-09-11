#include "m.h"
void F(const char *s) {
    if (Cond(0)) { P8 a; Use(&a); }
    if (Cond(1)) { P8 b; Use(&b); }
    if (Cond(2)) { P8 c; Use(&c); }
}
