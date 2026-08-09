#include "synth_xbox\EnvelopeGenerator.h"
#include "math\Decibels.h"
#include "os\Debug.h"
#include "xdk\LIBCMT\ppcintrinsics.h"

EnvelopeGenerator::EnvelopeGenerator() : unk8c(0) {
    EnvelopeGeneratorParams p;
    p.unk0 = 0;
    p.unk4 = 0;
    p.unk8 = 0;
    p.unkc = 0;
    unk84 = 0;
    unk88 = 0;
    unk90 = 0;
    SetParameters(&p, sizeof(EnvelopeGeneratorParams));
}

void EnvelopeGenerator::OnSetParameters(const EnvelopeGeneratorParams &p) {
    unk84 = p.unk0 * 48000;
    unk88 = p.unk4 * 48000;
    if (p.unk8 > 0.5f && unk90 != 3) {
        unk90 = 2;
    }
}

void EnvelopeGenerator::DoProcess(
    const EnvelopeGeneratorParams &params, float *__restrict buffer, unsigned int validFrameCount,
    unsigned int numChannels
) {
    if (numChannels != 1 && numChannels != 2) {
        MILO_PRINT_ONCE(
            "Envelope generator is attempting to process a buffer with %d channels!\n", numChannels
        );
        return;
    }

    if (unk90 == 1)
        return;

    if (unk90 == 3) {
        if (numChannels == 1) {
            for (unsigned int i = 0; i < validFrameCount; i++) {
                buffer[i] = 0;
            }
        } else {
            for (unsigned int i = 0; i < validFrameCount * 2; i++) {
                buffer[i] = 0.0f;
            }
        }
        return;
    }

    float gain = unk8c;
    float curDb = RatioToDb(gain);
    float clampedDb = (float)__fsel(curDb - -60.0f, curDb, -60.0f);

    int durationSamples = (unk90 == 0) ? unk84 : unk88;
    float stepDb = (float)durationSamples;
    float fFrames = (float)validFrameCount;
    float deltaDb = fFrames * 60.0f / stepDb;
    if (unk90 == 2) {
        deltaDb = -deltaDb;
    }

    int rampFrames = validFrameCount;
    if (deltaDb > -clampedDb) {
        float frac = clampedDb / deltaDb;
        rampFrames = (int)(-frac * fFrames);
        deltaDb = -clampedDb;
    } else if (unk90 == 2 && deltaDb + clampedDb < -60.0f) {
        rampFrames = (int)((clampedDb - -60.0f) * fFrames / -deltaDb);
    }

    float ramp = DbToRatio(deltaDb + clampedDb);
    if (rampFrames > 0) {
        float gainStep = (ramp - gain) / (float)rampFrames;
        float *mono = buffer;
        float *stereo = buffer;
        for (int i = 0; i < rampFrames; i++) {
            if (numChannels == 1) {
                mono[0] = mono[0] * gain;
            } else {
                stereo[0] = stereo[0] * gain;
                stereo[1] = stereo[1] * gain;
            }
            gain = gainStep + gain;
            mono += 1;
            stereo += 2;
        }
    }

    if ((unsigned int)rampFrames < validFrameCount) {
        if (unk90 == 0) {
            gain = 1.0f;
            unk90 = 1;
        } else {
            unk90 = 3;
            gain = 0.0f;
            if (numChannels == 1) {
                for (unsigned int i = rampFrames; i < validFrameCount; i++) {
                    buffer[i] = 0;
                }
            } else {
                for (unsigned int i = rampFrames; i < validFrameCount * 2; i++) {
                    buffer[i] = 0.0f;
                }
            }
        }
    }

    EnvelopeGeneratorParams copy = params;
    copy.unkc = (unk90 == 3) ? 1.0f : 0.0f;
    SetParameters(&copy, sizeof(EnvelopeGeneratorParams));
    unk8c = gain;
}
