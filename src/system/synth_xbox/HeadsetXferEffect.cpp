#include "HeadsetXferEffect.h"
#include "xdk/LIBCMT/string.h"

HeadsetXferEffect::HeadsetXferEffect() : CSampleXAPOBase() {
    *(int*)((char*)this + 0x60) = 0;
    memset((char*)this + 0x64, 0, 0x800);

    int temp = 0;
    ((CXAPOParametersBase*)((char*)this + 0x20))->SetParameters(&temp, 4);
}
