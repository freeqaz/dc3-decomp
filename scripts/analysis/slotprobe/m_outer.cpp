#include "m.h"
struct Res { void SetName(const char *, bool); };
struct Stream { void ReadString(char *, int); };
extern Res &GetRes(int);
static inline void ReadIt(Res &r, Stream &s) {
    char name[256];
    s.ReadString(name, 256);
    r.SetName(name, true);
}
void F(Stream &s) {
    if (Cond(0)) {
        char name[256];
        if (Cond(1)) {
            ReadIt(GetRes(1), s);
        } else {
            s.ReadString(name, 256);
        }
    }
}
