#include "synth/CompressionEffect.h"
#include "math/Decibels.h"
#include "xdk/xaudio2/xaudio2.h"
#include <cmath>

CompressionEffect::CompressionEffect(IXAudioBatchAllocator *) {
    Params params;
    params.unk0 = false;
    unk34 = 1.0f;
    Reset();
    params.unk4 = -6.0f;
    params.unk8 = 1.0f;
    params.unkc = 1.0f;
    params.unk10 = 0.005f;
    params.unk14 = 0.2f;
    params.unk18 = 1.0f;
    params.unk1c = 0.99f;
    params.unk20 = 1.01f;
    params.unk24 = -40.0f;
    SetParameters(params);
}

void CompressionEffect::Reset() {
    unk38 = 1.0f;
    unk3c = 1.0f;
}

void CompressionEffect::SetParameters(CompressionEffect::Params const &params) {
    unk4 = params.unk4;
    unk0 = DbToRatio(unk4);
    unk8 = DbToRatio(unk4 / unkc - unk4);
    unkc = params.unk8;
    unk8 = DbToRatio(unk4 / unkc - unk4);
    unk10 = DbToRatio(params.unkc);
    unk8 = DbToRatio(unk4 / unkc - unk4);
    unk14 = 1.0f - (float)exp(-1.0f / (params.unk10 * 48000.0f));
    unk8 = DbToRatio(unk4 / unkc - unk4);
    unk18 = 1.0f - (float)exp(-1.0f / (params.unk14 * 48000.0f));
    unk8 = DbToRatio(unk4 / unkc - unk4);
    unk1c = params.unk18;
    unk8 = DbToRatio(unk4 / unkc - unk4);
    unk20 = 1.0f - (float)exp(-1.0f / (params.unk1c * 48000.0f));
    unk8 = DbToRatio(unk4 / unkc - unk4);
    unk24 = 1.0f - (float)exp(-1.0f / (params.unk20 * 48000.0f));
    unk8 = DbToRatio(unk4 / unkc - unk4);
    unk28 = params.unk24;
    float ratio = DbToRatio(unk28);
    unk30 = ratio;
    unk2c = ratio;
    unk8 = DbToRatio(unk4 / unkc - unk4);
}

void CompressionEffect::Process(float *samples, int numFrames, int numChannels) {
    if (unkc > 0.999999046f) {
        float envelope = unk38;
        float prev_peak = 0.0f;
        float prev_sample = 0.0f;

        for (int frame = 0; frame < numFrames; frame++) {
            unsigned char detect_peak = 0;
            float peak_level = 0.27027027f;
            int channel = 0;

            if (numChannels > 0) {
                for (int ch_idx = 0; ch_idx < numChannels; ch_idx++) {
                    int sample_idx = frame * numChannels + channel;
                    float sample = samples[sample_idx];

                    if (numChannels == 1) {
                        float tmp = prev_peak;
                        prev_peak = prev_sample;
                        prev_sample = sample;

                        if (((tmp > 0.012483216f) || (tmp < -0.012483216f)) &&
                            (fabsf(tmp - prev_peak) < 0.004999995f) &&
                            (fabsf(sample - prev_peak) < 0.004999995f)) {
                            float ratio = unk34;
                            unk34 = ((1.0f - ratio) * 0.004999995f) + ratio;
                        }
                        float ratio = unk34;
                        unk34 = ((1.0f - ratio) * 0.01f) + ratio;
                    }

                    float abs_sample = fabsf(sample);
                    if (peak_level < abs_sample) {
                        peak_level = abs_sample;
                    }
                    channel += 1;
                }
            }

            float ratio = unk8;
            float threshold = unk0;
            float gain_reduction = ratio * peak_level;

            if (peak_level > threshold) {
                gain_reduction = ((((peak_level - threshold) / unkc) + threshold) * ratio);
                if (gain_reduction > 100000.0f) {
                    gain_reduction = 100000.0f;
                }
            }

            float min_level = unk2c;
            float attack_release;
            if (peak_level < min_level) {
                attack_release = (((peak_level - min_level) * 0.2f) + min_level);
            } else {
                attack_release = (((unk30 - min_level) * 0.1f) + min_level);
            }
            unk2c = attack_release;

            if (peak_level < (attack_release * 0.579999983f)) {
                detect_peak = 1;
                gain_reduction = 0.0f;
            }

            float gain = gain_reduction / peak_level;
            float envelope_coef;
            if (envelope > gain) {
                envelope_coef = unk14;
            } else {
                envelope_coef = unk18;
            }

            if (detect_peak != 0) {
                if (envelope > gain) {
                    envelope_coef = unk24;
                } else {
                    envelope_coef = unk20;
                }
            }

            envelope += (gain - envelope) * envelope_coef;

            if (envelope >= 100000.0f) {
                envelope = 100000.0f;
            }

            int channel2 = 0;
            for (int ch_idx2 = 0; ch_idx2 < numChannels; ch_idx2++) {
                int idx = frame * numChannels + channel2;
                samples[idx] = samples[idx] * unk34 * envelope * unk10;
                channel2 += 1;
            }
        }

        unk38 = envelope;
    }
}
