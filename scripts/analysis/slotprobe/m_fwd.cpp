#include "m.h"
extern void UseInt(int);
extern void ReadOut(FD8 &);
struct Rev { int *stream; int rev; Rev &operator>>(FD8 &x) { ReadOut(x); return *this; } };
extern void ReadInt(int *);
void F(Rev &d) {
    if (Cond(0)) { FD8 a; d >> a; UseInt(a.y); }
    if (Cond(1)) { int t; ReadInt(&t); UseInt(t); }
    if (Cond(2)) { FD8 c; d >> c; UseInt(c.y); }
}
