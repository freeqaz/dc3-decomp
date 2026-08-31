#pragma once
#include "xdk\xaudio2\xapobase.h"

// The HeadsetPlaybackEffect class body is not reconstructed yet -- this header
// carries only what is needed to emit its XAPO registration block, which is a
// real runtime artefact rather than a placeholder.
//
// Evidence for the two declarations below, from the target:
//   * ??0?$CSampleXAPOBase@VHeadsetPlaybackEffect@@UHeadsetPlaybackEffectParams@@@ATG@@IAA@XZ
//     at 0x82E441C8 passes `li r6, 0x1` as uParameterBlockByteSize, so
//     sizeof(HeadsetPlaybackEffectParams) == 1: an empty struct, exactly like
//     GainEffectParams.
//   * The CLSID is the one folded into .data at 0x82F491E8.
struct HeadsetPlaybackEffectParams {};

class __declspec(uuid("b4d4c8aa-a20d-40a1-84a7-64193551a9bf")) HeadsetPlaybackEffect;
