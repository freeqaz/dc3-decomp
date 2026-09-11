#include "m.h"
void F(const char *s) {
    if (Cond(0)) { VE8 a(s); Use(&a); }
    if (Cond(1)) { VE8 b(s); Use(&b); }
    if (Cond(2)) { VE8 c(s); Use(&c); }
}
