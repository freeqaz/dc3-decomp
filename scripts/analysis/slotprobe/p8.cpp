#include "pre.h"
void F() {
    if (Cond()) {
        int a;
        UseI(&a);
        UseI(&a);
    } else {
        int b;
        UseI(&b);
    }
}
