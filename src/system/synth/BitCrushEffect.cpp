#include "synth/BitCrushEffect.h"
#include "os/Debug.h"
#include "xdk/xaudio2/xaudio2.h"

BitCrushEffect::BitCrushEffect(IXAudioBatchAllocator *)
    : unk0(0), unk4(0), unk8(0), unkc(0) {}

void BitCrushEffect::SetParameters(BitCrushEffect::Params const &params) {
    unk0 = params.unk4;
}

void BitCrushEffect::Process(float *f, int numSamples, int numChans) {
    MILO_ASSERT(numChans <= 2, 0x1e);

    if (numSamples > 0) {
        int ctr;
        float *left;
        float *right;
        int stride;

        ctr = numSamples;
        stride = numChans << 2;
        left = f;
        right = f + 1;

        do {
            if (unk4 > 0) {
                *left = unk8;
                if (numChans == 2) {
                    *right = unkc;
                }
                unk4--;
            } else {
                float val = *left;
                int intval = (int)val;
                *left = (float)intval;
                unk8 = *left;

                if (numChans == 2) {
                    val = *right;
                    intval = (int)val;
                    *right = (float)intval;
                    unkc = *right;
                }
            }

            left = (float*)((char*)left + stride);
            right = (float*)((char*)right + 8);
            ctr--;
        } while (ctr != 0);
    }
}
