#include "m.h"
extern "C" unsigned int strlen(const char *);
extern void UseC(char);
void F(const char *s) {
    if (Cond(0)) { char buf[0x80]; Use(buf); }
    else { const char t[] = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~"; for (int i = 0; i < strlen(t); i++) UseC(t[i]); }
}
