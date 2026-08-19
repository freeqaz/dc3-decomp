#include "SynapseAPO.h"
#include "Synapse_dsp.h"
#include "xdk\win_types.h"
#include <string.h>

extern "C" void XMemSet(void* dst, int val, int size);

// The real XAPO registration block (0x42c bytes), not the 0x58-byte placeholder
// this stub TU used to carry. m_regProps is a static member, so widening it does
// not move anything in CSampleXAPOBase's object layout.
struct XAPO_REGISTRATION_PROPERTIES { /* Size=0x42c */
    /* 0x0000 */ _GUID clsid;
    /* 0x0010 */ WCHAR FriendlyName[256];
    /* 0x0210 */ WCHAR CopyrightInfo[256];
    /* 0x0410 */ UINT MajorVersion;
    /* 0x0414 */ UINT MinorVersion;
    /* 0x0418 */ UINT Flags;
    /* 0x041c */ UINT MinInputBufferCount;
    /* 0x0420 */ UINT MaxInputBufferCount;
    /* 0x0424 */ UINT MinOutputBufferCount;
    /* 0x0428 */ UINT MaxOutputBufferCount;
};

namespace DSP {

SynapseAPO::SynapseAPO() : ATG::CSampleXAPOBase<SynapseAPO, SynapseAPOParams>(), mSynapse(nullptr) {
    SetSamplingRate(48000.0f);
}

SynapseAPO::~SynapseAPO() {
    if (mSynapse) {
        delete mSynapse;
    }
}

void SynapseAPO::SetSamplingRate(float rate) {
    Synapse::Synapse* prevSynapse = mSynapse;
    if (prevSynapse) {
        delete prevSynapse;
    }
    mSynapse = new Synapse::Synapse(rate);
}

void SynapseAPO::OnSetParameters(const SynapseAPOParams& params) {
    for (unsigned int i = 0; i < 3; i++) {
        if (mParams.bands[i].enabled != params.bands[i].enabled) {
            mSynapse->SetVoiceEnabled(i, params.bands[i].enabled);
        }
        if (mParams.bands[i].gain != params.bands[i].gain) {
            mSynapse->SetVoiceGain(i, params.bands[i].gain);
        }
        if (mParams.bands[i].freq != params.bands[i].freq) {
            mSynapse->SetVoiceTargetNote(i, params.bands[i].freq);
        }
        if (mParams.bands[i].q != params.bands[i].q) {
            mSynapse->SetVoiceTransposition(i, params.bands[i].q);
        }
        if (mParams.bands[i].coeff0 != params.bands[i].coeff0) {
            mSynapse->SetVoiceAmount(i, params.bands[i].coeff0);
        }
        if (mParams.bands[i].coeff1 != params.bands[i].coeff1) {
            mSynapse->SetVoiceProximityEffect(i, params.bands[i].coeff1);
        }
        if (mParams.bands[i].coeff2 != params.bands[i].coeff2) {
            mSynapse->SetVoiceProximityFocus(i, params.bands[i].coeff2);
        }
    }
    if (mParams.lowCutoffFreq != params.lowCutoffFreq) {
        mSynapse->SetAttackSmoothing(params.lowCutoffFreq);
    }
    if (mParams.highCutoffFreq != params.highCutoffFreq) {
        mSynapse->SetReleaseSmoothing(params.highCutoffFreq);
    }
    memcpy(&mParams, &params, sizeof(SynapseAPOParams));
}

void SynapseAPO::DoProcess(const SynapseAPOParams& params, unsigned int* arg1, float& arg2, unsigned int arg3, unsigned int arg4) {}

}  // namespace DSP

namespace ATG {

// Recovered from the shipped image. Without this initializer the whole block
// landed in .bss, so the APO registered with a null CLSID, an empty name,
// version 0.0, no flags and a buffer-count range of [0,0].
//
// The runtime VALUES now agree with the target byte for byte; the STATIC/DYNAMIC
// SPLIT still does not, and we could not reproduce it. The original obj folds only
// the first 0x24 bytes (clsid + the nine chars of L"SampleAPO") into .data and
// emits ??__E?m_regProps@...@YAXXZ -- 144 bytes at 0x82E43E90 -- to memset
// +0x24..0x210, memcpy the 0x50-byte pooled literal ??_C@_1FA@MJNECBMC@ into
// CopyrightInfo, memset +0x260..0x410, then store 1, 0, 0x3f, 1, 1, 1, 1.
// Our cl.exe (same binary, same /O1 /Oi /EHsc /TP) folds all 0x42c bytes and emits
// no dynamic initializer. Falsified: it is not a literal-length threshold (a
// 160-char copyright still folds) and not explicit-specialization vs template
// definition (both fold). link_glue.cpp still /ALTERNATENAMEs the missing
// ??__E?m_regProps for this and every sibling effect.
template <>
XAPO_REGISTRATION_PROPERTIES CSampleXAPOBase<DSP::SynapseAPO, DSP::SynapseAPOParams>::m_regProps = {
    { 0x03004d97, 0xd165, 0x4cc0, { 0xab, 0xdd, 0x6a, 0x98, 0xf0, 0x4e, 0x6e, 0xb7 } },
    L"SampleAPO",
    L"Copyright (C)2008 Microsoft Corporation",
    1,
    0,
    0x3f, // all six XAPO_FLAG_* bits, i.e. XAPOBASE_DEFAULT_FLAG | INPLACE_REQUIRED
    1,
    1,
    1,
    1,
};

template <typename Derived, typename Params>
CSampleXAPOBase<Derived, Params>::CSampleXAPOBase()
    : CXAPOParametersBase(&m_regProps, (unsigned char*)m_paramBlocks, sizeof(Params), 0)
{
    XMemSet(m_paramBlocks, 0, sizeof(Params) * 3);
}

template class CSampleXAPOBase<DSP::SynapseAPO, DSP::SynapseAPOParams>;

} // namespace ATG
