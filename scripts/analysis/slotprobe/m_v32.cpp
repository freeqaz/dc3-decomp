#include "m.h"
void F(const char *s) {
    if (Cond(0)) { V32 a(s); Use(&a); }
    if (Cond(1)) { V32 b(s); Use(&b); }
    if (Cond(2)) { V32 c(s); Use(&c); }
}
