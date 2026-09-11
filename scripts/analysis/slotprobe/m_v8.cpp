#include "m.h"
void F(const char *s) {
    if (Cond(0)) { V8 a(s); Use(&a); }
    if (Cond(1)) { V8 b(s); Use(&b); }
    if (Cond(2)) { V8 c(s); Use(&c); }
}
