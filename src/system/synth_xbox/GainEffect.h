#pragma once
#include "xdk\xaudio2\xapobase.h"

class DataArray;
class DataNode;
class MicManagerXbox;
DataNode SetRemoteGain(DataArray *);

// Empty parameter block. GainEffect carries no per-block state -- the gain lives
// in the static sGain, which SetRemoteGain writes. sizeof == 1 (an empty struct),
// confirmed twice in the target: `li r6, 0x1` (uParameterBlockByteSize) in
// ??0?$CSampleXAPOBase@VGainEffect@@..., and `li r5, 0x1` (ParameterByteSize) at
// the SetParameters call site in ??0GainEffect@@QAA@XZ. The base then does
// XMemSet(mParams, 0, 3) -- 3 * 1 for Params mParams[3].
struct GainEffectParams {};

// Remote-talker chat gain XAPO. Layout:
//   0x00  CXAPOParametersBase         (0x40)
//   0x40  CSampleXAPOBase::mParams[3] (3 * 1)
//   0x44  CSampleXAPOBase::mWav       (WAVEFORMATEX, 0x12)
// Total 0x58.
class __declspec(uuid("b4d4c8aa-a20d-40a1-84a7-64193551a9bc")) GainEffect : public ATG::CSampleXAPOBase<GainEffect, GainEffectParams> {
    friend class MicManagerXbox;
    friend DataNode SetRemoteGain(DataArray *);

public:
    GainEffect();

    virtual void
    DoProcess(const GainEffectParams &, float *__restrict, unsigned int, unsigned int);

private:
    static float sGain;
};
