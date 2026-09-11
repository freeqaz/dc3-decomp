#include "m.h"
void F(const char *s) {
    if (Cond(0)) { D8 a(s); Use(&a); }
    if (Cond(1)) { D8 b(s); Use(&b); }
    if (Cond(2)) { D8 c(s); Use(&c); }
}
