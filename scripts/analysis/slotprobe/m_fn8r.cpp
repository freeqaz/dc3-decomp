#include "m.h"
extern void UseInt(int);
void F(const char *s) {
    if (Cond(0)) { F8 a; Use(&a); UseInt(a.Get()); }
    if (Cond(1)) { F8 b; Use(&b); UseInt(b.Get()); }
    if (Cond(2)) { F8 c; Use(&c); UseInt(c.Get()); }
}
