#pragma once

// GainEffect is an XAUDIO2 XAPO effect (CSampleXAPOBase<GainEffect,
// GainEffectParams>) applied to remote-talker chat playback. Its full
// behaviour (DoProcess altivec gain multiply) lives in GainEffect.cpp; this
// header models only the interface/layout needed by callers such as
// MicManagerXbox::AddRemoteMic, which constructs `new GainEffect()` and
// releases it through the IUnknown vtable. Total object size is 0x58.

namespace ATG {

// Forward declarations
struct XAPO_REGISTRATION_PROPERTIES;

// Interface for XAPO parameters. Second vtable subobject (lands at 0x20).
class IXAPOParameters {
public:
    virtual void SetParameters(const void *, unsigned int) {}
    virtual void GetParameters(void *, unsigned int) {}
};

// Base class for XAPO parameters. IUnknown-style vtable at 0x0; instance data
// spans 0x04-0x1F.
class CXAPOParametersBase {
public:
    CXAPOParametersBase(const void *pRegistrationProperties, void *pParameterBlocks,
                        unsigned int uParameterBlockByteSize, unsigned char fProducer);
    virtual long QueryInterface(const void *, void **);
    virtual unsigned long AddRef();
    virtual unsigned long Release();

private:
    unsigned char mParamBaseData[0x1c]; // 0x04-0x1F
};

// Multiple inheritance: CXAPOParametersBase at 0x0 (vtable + 0x1c data),
// IXAPOParameters subobject vtable at 0x20.
class CXAPOBase : public CXAPOParametersBase, public IXAPOParameters {
public:
    CXAPOBase();
};

// Template base for sample XAPOs. Fills 0x24-0x3F so the params block lands at
// 0x40 and the whole GainEffect object ends at 0x58.
template <typename Derived, typename Params>
class CSampleXAPOBase : public CXAPOBase {
protected:
    CSampleXAPOBase();

    static XAPO_REGISTRATION_PROPERTIES m_regProps;

    unsigned char mSampleBaseData[0x1c]; // 0x24-0x3F
    Params mParams;                      // 0x40
};

} // namespace ATG

class DataArray;
class DataNode;
class MicManagerXbox;
DataNode SetRemoteGain(DataArray *);

// Parameter block for the GainEffect XAPO (0x18 bytes -> object size 0x58).
struct GainEffectParams {
    unsigned char data[0x18];
};

// Remote-talker chat gain XAPO. No derived instance data: the static sGain
// holds the linear gain applied in DoProcess.
class GainEffect : public ATG::CSampleXAPOBase<GainEffect, GainEffectParams> {
    friend class MicManagerXbox;
    friend DataNode SetRemoteGain(DataArray *);

public:
    GainEffect();

private:
    static float sGain;
};
