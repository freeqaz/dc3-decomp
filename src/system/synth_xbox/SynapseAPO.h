#pragma once
#include "synth_xbox/FxSendSynapse.h"

// Global XAPO base classes (no namespace)
class CXAPOParametersBase {
public:
    virtual ~CXAPOParametersBase();

private:
    char mCXAPOParametersBasePad[0x1c]; // CXAPOParametersBase is 0x20 bytes total
};

class IXAPOParameters {
public:
    virtual ~IXAPOParameters() {}
};

class CXAPOBase : public CXAPOParametersBase, public IXAPOParameters {
public:
    virtual ~CXAPOBase() {}
};

namespace ATG {

template <typename T, typename Params>
class CSampleXAPOBase : public CXAPOBase {
public:
    CSampleXAPOBase();
    virtual ~CSampleXAPOBase() {}

protected:
    virtual void OnSetParameters(const Params& params) = 0;
    virtual void DoProcess(const Params& params, unsigned int* arg1, float& arg2, unsigned int arg3, unsigned int arg4) = 0;

private:
    // Internal state - CXAPOBase = 0x24 bytes (0x20 CXAPOParametersBase + 0x4 IXAPOParameters)
    // CSampleXAPOBase adds 0x144 bytes of state, so total CSampleXAPOBase = 0x168 bytes
    char pad[0x144];
};

} // namespace ATG

namespace DSP {

namespace Synapse {
class Synapse;
}

class SynapseAPO : public ATG::CSampleXAPOBase<SynapseAPO, SynapseAPOParams> {
public:
    SynapseAPO();
    virtual ~SynapseAPO();
    void SetSamplingRate(float rate);
    void OnSetParameters(const SynapseAPOParams& params);
    void DoProcess(const SynapseAPOParams& params, unsigned int* arg1, float& arg2, unsigned int arg3, unsigned int arg4);

private:
    Synapse::Synapse* mSynapse;   // at offset 0x168
    SynapseAPOParams mParams;     // at offset 0x16c
};

}  // namespace DSP
