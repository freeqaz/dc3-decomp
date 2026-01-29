#include "utl/DeJitter.h"
#include "obj/Data.h"

float DeJitter::sTimeScale = 1;

DeJitter::DeJitter() { Reset(); }

void DeJitter::Reset() {
    unk80 = 0;
    unk84 = -2;
    unk88 = 0;
    unk8c = 0;
    for (int i = 0; i < 32; i++) {
        unk0[i] = 0;
    }
}

float DeJitter::NewMs(float f1, float &fref) {
    static DataNode &n = DataVariable("dejitter_disable");
    float var_f30 = f1;
    float var_f31 = 1.0000000150474662e+30;
    int temp_r29 = (unk80 - 1) & 0x1F;
    int temp_r28 = (temp_r29 - unk84) & 0x1F;

    if (!n.Int()) {
        if (unk84 > 8) {
            float f0 = (unk0[temp_r29] - unk0[temp_r28]) / (float)unk84;
            if (unk88 == 0.0f) {
                unk88 = f0;
            }
            f0 = (f0 - unk88) * 0.1f + unk88;
            var_f31 = f0;
            unk88 = f0;
            if (sTimeScale != 1.0f) {
                f0 = f0 * sTimeScale;
                unk88 = f0;
                var_f31 = f0 + unk8c;
            } else {
                float f12 = unk8c + f0;
                float f11 = var_f30 - 33.0f;
                float f13 = var_f30 + 33.0f;
                float f10 = ((f11 - f12) >= 0.0f) ? f11 : f12;
                var_f31 = ((f10 - f13) >= 0.0f) ? f13 : f10;
            }
            if (var_f31 < unk8c) {
                var_f31 = unk8c;
            }
        }
    }

    unk0[unk80] = var_f30;
    if (var_f31 != 1.0000000150474662e+30) {
        var_f30 = var_f31;
    }
    unk80 = (unk80 + 1) & 0x1F;

    if (unk84 == -2) {
        fref = 16.666f;
    } else {
        fref = var_f30 - unk8c;
    }

    if (unk84 < 30) {
        unk84 = unk84 + 1;
    }

    unk8c = var_f30;
    return var_f30;
}
