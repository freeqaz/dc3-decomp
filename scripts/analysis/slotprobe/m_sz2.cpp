#include "m.h"
void F(const char *s) {
    char small[96];
    if (Cond(0)) { Use(small); if (Cond(5)) Use(small); }
    if (Cond(1)) { char big[128]; Use(big); if (Cond(6)) Use(big); }
}
