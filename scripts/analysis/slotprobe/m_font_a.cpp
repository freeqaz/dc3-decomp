#include "m.h"
extern void UseC(char);
void F(const char *s) {
    char buf[0x80];
    if (Cond(0)) { Use(buf); if (Cond(5)) Use(buf); }
    if (Cond(1)) { Use((void*)s); }
    else { const char t[] = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~"; for (const char *p = t; *p; p++) UseC(*p); }
}
