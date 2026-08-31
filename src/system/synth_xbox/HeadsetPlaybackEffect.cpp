#include "synth_xbox\HeadsetPlaybackEffect.h"

// Only the registration block is reconstructed so far. m_regProps and its
// ??__E dynamic initializer come from the primary template in
// xdk/xaudio2/xapobase.h, keyed off __uuidof(HeadsetPlaybackEffect). The
// effect's own methods (DoProcess, the vtables, RTTI) are still missing.
namespace ATG {
template XAPO_REGISTRATION_PROPERTIES
    CSampleXAPOBase<HeadsetPlaybackEffect, HeadsetPlaybackEffectParams>::m_regProps;
}
