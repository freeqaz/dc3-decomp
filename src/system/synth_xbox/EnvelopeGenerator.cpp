#include "synth360\EnvelopeGenerator.h"
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
    // Signed throughout: every channel-count test in the target is cmpwi, not
    // cmplwi, and the MILO_PRINT_ONCE argument instantiates MakeString<int>
    // (ICF-folded onto MakeString<_D3DFORMAT>), not MakeString<unsigned int>.
    // The override's declared parameter type is fixed by the base class, so the
    // narrowing has to happen here.
    int channels = numChannels;
    if (channels != 1 && channels != 2) {
        MILO_PRINT_ONCE(
            "Envelope generator is attempting to process a buffer with %d channels!\n", channels
        );
        return;
    }

    if (unk90 == 1)
        return;

    if (unk90 == 3) {
        if (channels == 1) {
            for (unsigned int pos = 0; pos < validFrameCount; pos++) {
                buffer[pos] = 0;
            }
        } else {
            for (unsigned int pos = 0; pos < validFrameCount * 2; pos++) {
                buffer[pos] = 0.0f;
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
    // /fp:fast reassociates `fFrames * 60.0f / stepDb` into
    // `(fFrames / stepDb) * 60.0f`; the target multiplies first. Same for the
    // two negations below -- written inline MSVC sinks the fneg past the
    // fmuls/fdivs, so each one gets its own temp.
    float scaledFrames = fFrames * 60.0f;
    float deltaDb = scaledFrames / stepDb;
    if (unk90 == 2) {
        deltaDb = -deltaDb;
    }

    int rampFrames = validFrameCount;
    if (deltaDb > -clampedDb) {
        float frac = clampedDb / deltaDb;
        float negFrac = -frac;
        rampFrames = (int)(negFrac * fFrames);
        deltaDb = -clampedDb;
    } else if (unk90 == 2 && deltaDb + clampedDb < -60.0f) {
        float negDelta = -deltaDb;
        float scaledHeadroom = (clampedDb - -60.0f) * fFrames;
        rampFrames = (int)(scaledHeadroom / negDelta);
    }

    float ramp = DbToRatio(deltaDb + clampedDb);
    // The ramp step is computed unconditionally (the target has a single
    // `cmpwi rN, 0` / `ble` for the whole thing -- the loop's own entry test),
    // and everything after the loop keys off the loop counter, not off
    // rampFrames.  When rampFrames <= 0 the counter is 0, so the tail fill
    // still covers the whole buffer; keying off rampFrames instead made
    // `(unsigned)rampFrames < validFrameCount` false for a negative ramp and
    // skipped the fill entirely.
    float gainStep = (ramp - gain) / (float)rampFrames;
    int frame;
    // Plain subscripts, not hand-rolled walking pointers: MSVC does the
    // strength reduction itself and puts both induction variables in the loop
    // preheader, where the target has them.  Written as two pre-initialised
    // float* the initialisation is hoisted above the loop guard instead.
    for (frame = 0; frame < rampFrames; frame++) {
        if (channels == 1) {
            buffer[frame] = buffer[frame] * gain;
        } else {
            buffer[frame * 2] = buffer[frame * 2] * gain;
            buffer[frame * 2 + 1] = buffer[frame * 2 + 1] * gain;
        }
        gain = gainStep + gain;
    }

    if ((unsigned int)frame < validFrameCount) {
        if (unk90 == 0) {
            gain = 1.0f;
            unk90 = 1;
        } else {
            unk90 = 3;
            gain = 0.0f;
            // Deliberately asymmetric: the mono fill is a counted (ctr) loop
            // so its counter is a separate variable, while the stereo fill
            // walks the loop counter itself -- reusing `frame` there is what
            // removes the target-absent `mr` that a fresh variable costs.
            if (channels == 1) {
                for (unsigned int tail = frame; tail < validFrameCount; tail++) {
                    buffer[tail] = 0;
                }
            } else {
                for (; (unsigned int)frame < validFrameCount * 2; frame++) {
                    buffer[frame] = 0.0f;
                }
            }
        }
    }

    EnvelopeGeneratorParams copy = params;
    copy.unkc = (unk90 == 3) ? 1.0f : 0.0f;
    SetParameters(&copy, sizeof(EnvelopeGeneratorParams));
    unk8c = gain;
}
