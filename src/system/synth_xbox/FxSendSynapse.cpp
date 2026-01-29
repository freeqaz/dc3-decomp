#include "synth_xbox/FxSendSynapse.h"

extern float __real__435c0000;  // Some float constant
extern float __real__00000000;  // 0.0f
extern float __real__41a00000;  // Some float constant
extern float merged_8201FFCC;   // Merged symbol

namespace DSP {

SynapseAPOParams::SynapseAPOParams() {
    unsigned char *var_r11;
    int var_ctr;

    var_r11 = (unsigned char *)this - 0xc;
    var_ctr = 3;
    do {
        *(float *)(var_r11 + 0x20) = __real__435c0000;
        *(float *)(var_r11 + 0x24) = 0.0f;
        *(signed char *)(var_r11 + 0x1c) = 0;
        var_r11 += 0x1C;
        *(float *)(var_r11 + 0x28) = 0.0f;
        var_ctr -= 1;
    } while (var_ctr != 0);
    *(float *)((unsigned char *)this + 0x54) = __real__41a00000;
    *(float *)((unsigned char *)this + 0x58) = merged_8201FFCC;
}

}  // namespace DSP
