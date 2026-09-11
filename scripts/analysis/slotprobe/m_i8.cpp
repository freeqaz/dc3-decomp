#include "m.h"
void F(const char *s) {
    if (Cond(0)) { I8 a(s); Use(&a); }
    if (Cond(1)) { I8 b(s); Use(&b); }
    if (Cond(2)) { I8 c(s); Use(&c); }
}
