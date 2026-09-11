#include "m.h"
extern void UseInt(int);
void F(const char *s) {
    if (Cond(0)) { FD8 a; Use(&a); UseInt(a.Get()); }
    if (Cond(1)) { FD8 b; Use(&b); UseInt(b.Get()); }
    if (Cond(2)) { FD8 c; Use(&c); UseInt(c.Get()); }
}
