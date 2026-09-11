#include "m.h"
void F(const char *s) {
    if (Cond(0)) { D32 a(s); Use(&a); }
    if (Cond(1)) { D32 b(s); Use(&b); }
    if (Cond(2)) { D32 c(s); Use(&c); }
}
