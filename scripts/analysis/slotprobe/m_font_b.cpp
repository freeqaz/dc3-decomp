#include "m.h"
extern void UseC(char);
void F(const char *s) {
    const char t[] = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";
    if (Cond(0)) { char buf[0x80]; Use(buf); if (Cond(5)) Use(buf); }
    if (Cond(1)) { Use((void*)s); }
    else { for (const char *p = t; *p; p++) UseC(*p); }
}
