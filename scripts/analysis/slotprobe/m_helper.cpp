#include "m.h"
struct Res { void SetName(const char *, bool); };
struct Stream { void ReadString(char *, int); };
extern Res &GetRes(int);
static inline void ReadIt(Stream &s, Res &r) {
    char name[256];
    s.ReadString(name, 256);
    r.SetName(name, true);
}
void F(Stream &s) {
    if (Cond(0)) {
        ReadIt(s, GetRes(1));
    } else {
        char name[256];
        s.ReadString(name, 256);
    }
}
