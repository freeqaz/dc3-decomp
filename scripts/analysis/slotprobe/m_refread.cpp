#include "m.h"
extern void UseInt(int);
static inline int Peek(const FD8 &p) { return p.y; }
void F(const char *s) {
    if (Cond(0)) { FD8 a; Use(&a); UseInt(Peek(a)); }
    if (Cond(1)) { int t; Use(&t); UseInt(t); }
    if (Cond(2)) { FD8 c; Use(&c); UseInt(Peek(c)); }
}
