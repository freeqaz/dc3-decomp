#pragma once
#include "synth_xbox/FxSendSynapse.h"

// Global XAPO base classes (no namespace)
class CXAPOBase {
public:
    virtual ~CXAPOBase();

private:
    char mCXAPOBasePad[0x1c]; // CXAPOBase is 0x20 bytes total (vtable + 0x1c data)
};

class IXAPOParameters {
public:
    virtual ~IXAPOParameters() {}
};

namespace ATG {

template <typename T, typename Params>
class CSampleXAPOBase : public CXAPOBase, public IXAPOParameters {
public:
    virtual ~CSampleXAPOBase() {}

protected:
    __declspec(noinline) CSampleXAPOBase();
    virtual void OnSetParameters(const Params& params) = 0;
    virtual void DoProcess(const Params& params, unsigned int* arg1, float& arg2, unsigned int arg3, unsigned int arg4) = 0;

private:
    // Internal state - CXAPOBase = 0x20 bytes, CXAPOParametersBase (IXAPOParameters) = 0x4 bytes at 0x20
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
