#include "m.h"
void F(const char *s) {
    if (Cond(0)) { char buf[0x80]; Use(buf); }
    else { const char t[] = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~"; for (const char *p = t; *p; p++) Use((void*)p); }
}
