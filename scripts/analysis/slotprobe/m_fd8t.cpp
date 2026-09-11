#include "m.h"
extern void UseInt(int);
extern void ReadInt(int *);
void F(const char *s) {
    if (Cond(0)) { int t; ReadInt(&t); UseInt(t); }
    if (Cond(1)) { FD8 b; Use(&b); UseInt(b.Get()); }
    if (Cond(2)) { int t; ReadInt(&t); UseInt(t); }
}
