#include "FxSendReverb.h"
#include "FxSend.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "utl/Symbol.h"
#include "xdk/xaudio2/xaudio2.h"
#include "xdk/xaudio2/xaudio2fx.h"

FxSendReverb360::FxSendReverb360() : FxSend360(this) {}

FxSendReverb360::~FxSendReverb360() {}

void FxSendReverb360::Recreate(std::vector<FxSend *> &sends) { FxSend360::Refresh(sends); }

void FxSendReverb360::UpdateMix() { FxSend360::UpdateVolumes(); }

void FxSendReverb360::OnParametersChanged() { FxSend360::SyncEffectParams(); }

namespace {
    // One I3DL2 environmental reverb preset paired with the Symbol name it answers to.
    struct ReverbPreset {
        Symbol name;
        XAUDIO2FX_REVERB_I3DL2_PARAMETERS params;
    };
}

void FxSendReverb360::SyncEffectParams(IXAudio2SubmixVoice *voice) const {
    // The I3DL2 preset table is built once on first call (static-local guard). Each entry
    // is { Symbol; XAUDIO2FX_REVERB_I3DL2_PARAMETERS } so the environment name and its
    // parameters live in one 0x38-byte slot, matching the target's flat table layout.
    static ReverbPreset presets[] = {
        { Symbol("default"),          { 100.0f, -10000,    0, 0.0f,  1.00f, 0.50f, -10000, 0.020f, -10000, 0.040f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("generic"),          { 100.0f,  -1000, -100, 0.0f,  1.49f, 0.83f,  -2602, 0.007f,    200, 0.011f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("padded_cell"),      { 100.0f,  -1000, -6000, 0.0f, 0.17f, 0.10f,  -1204, 0.001f,    207, 0.002f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("room"),             { 100.0f,  -1000, -454, 0.0f,  0.40f, 0.83f,  -1646, 0.002f,     53, 0.003f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("bath_room"),        { 100.0f,  -1000, -1200, 0.0f, 1.49f, 0.54f,   -370, 0.007f,   1030, 0.011f, 100.0f,  60.0f, 5000.0f } },
        { Symbol("living_room"),      { 100.0f,  -1000, -6000, 0.0f, 0.50f, 0.10f,  -1376, 0.003f,  -1104, 0.004f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("stone_room"),       { 100.0f,  -1000, -300, 0.0f,  2.31f, 0.64f,   -711, 0.012f,     83, 0.017f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("auditorium"),       { 100.0f,  -1000, -476, 0.0f,  4.32f, 0.59f,   -789, 0.020f,   -289, 0.030f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("concert_hall"),     { 100.0f,  -1000, -500, 0.0f,  3.92f, 0.70f,  -1230, 0.020f,     -2, 0.029f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("cave"),             { 100.0f,  -1000,    0, 0.0f,  2.91f, 1.30f,   -602, 0.015f,   -302, 0.022f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("arena"),            { 100.0f,  -1000, -698, 0.0f,  7.24f, 0.33f,  -1166, 0.020f,     16, 0.030f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("hangar"),           { 100.0f,  -1000, -1000, 0.0f, 10.05f, 0.23f,  -602, 0.020f,    198, 0.030f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("carpeted_hallway"), { 100.0f,  -1000, -4000, 0.0f, 0.30f, 0.10f,  -1831, 0.002f,  -1630, 0.030f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("hallway"),          { 100.0f,  -1000, -300, 0.0f,  1.49f, 0.59f,  -1219, 0.007f,    441, 0.011f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("stone_corridor"),   { 100.0f,  -1000, -237, 0.0f,  2.70f, 0.79f,  -1214, 0.013f,    395, 0.020f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("alley"),            { 100.0f,  -1000, -270, 0.0f,  1.49f, 0.86f,  -1204, 0.007f,     -4, 0.011f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("forest"),           { 100.0f,  -1000, -3300, 0.0f, 1.49f, 0.54f,  -2560, 0.162f,   -613, 0.088f,  79.0f, 100.0f, 5000.0f } },
        { Symbol("city"),             { 100.0f,  -1000, -800, 0.0f,  1.49f, 0.67f,  -2273, 0.007f,  -2217, 0.011f,  50.0f, 100.0f, 5000.0f } },
        { Symbol("mountains"),        { 100.0f,  -1000, -2500, 0.0f, 1.49f, 0.21f,  -2780, 0.300f,  -2014, 0.100f,  27.0f, 100.0f, 5000.0f } },
        { Symbol("quarry"),           { 100.0f,  -1000, -1000, 0.0f, 1.49f, 0.83f, -10000, 0.061f,    500, 0.025f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("plain"),            { 100.0f,  -1000, -2000, 0.0f, 1.49f, 0.50f,  -2466, 0.179f,  -2514, 0.100f,  21.0f, 100.0f, 5000.0f } },
        { Symbol("parking_lot"),      { 100.0f,  -1000,    0, 0.0f,  1.65f, 1.50f,  -1363, 0.008f,  -1153, 0.012f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("sewer_pipe"),       { 100.0f,  -1000, -1000, 0.0f, 2.81f, 0.14f,    429, 0.014f,    648, 0.021f,  80.0f,  60.0f, 5000.0f } },
        { Symbol("underwater"),       { 100.0f,  -1000, -4000, 0.0f, 1.49f, 0.10f,   -449, 0.007f,   1700, 0.011f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("small_room"),       { 100.0f,  -1000, -600, 0.0f,  1.10f, 0.83f,   -400, 0.005f,    500, 0.010f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("medium_room"),      { 100.0f,  -1000, -600, 0.0f,  1.30f, 0.83f,  -1000, 0.010f,   -200, 0.020f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("large_room"),       { 100.0f,  -1000, -600, 0.0f,  1.50f, 0.83f,  -1600, 0.020f,  -1000, 0.040f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("medium_hall"),      { 100.0f,  -1000, -600, 0.0f,  1.80f, 0.70f,  -1300, 0.015f,   -800, 0.030f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("large_hall"),       { 100.0f,  -1000, -600, 0.0f,  1.80f, 0.70f,  -2000, 0.030f,  -1400, 0.060f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("plate"),            { 100.0f,  -1000, -200, 0.0f,  1.30f, 0.90f,      0, 0.002f,      0, 0.010f, 100.0f,  75.0f, 5000.0f } },
    };

    unsigned int idx;
    for (idx = 0; idx < 30; idx++) {
        if (presets[idx].name == mEnvironmentPreset)
            break;
    }
    if (idx == 30)
        MILO_FAIL("Unexpected environment preset.");

    XAUDIO2FX_REVERB_PARAMETERS native;
    ReverbConvertI3DL2ToNative(&presets[idx].params, &native);
    voice->SetEffectParameters(0, &native, sizeof(native), 0);
}

IUnknown *FxSendReverb360::CreateFx() {
    IUnknown *apo;
    CreateAudioReverb(&apo);
    return apo;
}
