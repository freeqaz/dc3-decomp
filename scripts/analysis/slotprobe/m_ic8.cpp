#include "m.h"
void F(const char *s) {
    if (Cond(0)) { IC8 a(s); Use(&a); }
    if (Cond(1)) { IC8 b(s); Use(&b); }
    if (Cond(2)) { IC8 c(s); Use(&c); }
}
