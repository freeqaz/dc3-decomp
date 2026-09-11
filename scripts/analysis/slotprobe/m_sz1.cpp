#include "m.h"
void F(const char *s) {
    char big[128];
    if (Cond(0)) { Use(big); if (Cond(5)) Use(big); }
    if (Cond(1)) { char small[96]; Use(small); if (Cond(6)) Use(small); }
}
