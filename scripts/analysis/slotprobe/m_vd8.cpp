#include "m.h"
void F(const char *s) {
    if (Cond(0)) { VD8 a(s); Use(&a); }
    if (Cond(1)) { VD8 b(s); Use(&b); }
    if (Cond(2)) { VD8 c(s); Use(&c); }
}
