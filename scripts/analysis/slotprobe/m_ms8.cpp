#include "m.h"
extern void UseInt(int);
void F(const char *s) {
    if (Cond(0)) { MS8 a; Use(&a); UseInt(a.s.Get()); }
    if (Cond(1)) { MS8 b; Use(&b); UseInt(b.s.Get()); }
    if (Cond(2)) { MS8 c; Use(&c); UseInt(c.s.Get()); }
}
