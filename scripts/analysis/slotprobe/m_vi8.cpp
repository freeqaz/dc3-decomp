#include "m.h"
void F(const char *s) {
    if (Cond(0)) { VI8 a(s); Use(&a); }
    if (Cond(1)) { VI8 b(s); Use(&b); }
    if (Cond(2)) { VI8 c(s); Use(&c); }
}
