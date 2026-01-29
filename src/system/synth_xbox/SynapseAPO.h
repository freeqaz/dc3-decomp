#pragma once

namespace DSP {

struct SynapseAPOParams {
  SynapseAPOParams();
};

class CXAPOBase {
public:
  virtual ~CXAPOBase() {}
};

class IXAPOParameters {
public:
  virtual ~IXAPOParameters() {}
};

template <typename T, typename Params>
class CSampleXAPOBase : public CXAPOBase, public IXAPOParameters {
public:
  CSampleXAPOBase();
  virtual ~CSampleXAPOBase() {}

protected:
  virtual void OnSetParameters(const Params& params) = 0;
  virtual void DoProcess(const Params& params, int* arg1, float arg2, int arg3, int arg4) = 0;
};

class SynapseAPO : public CSampleXAPOBase<SynapseAPO, SynapseAPOParams> {
public:
  SynapseAPO();
  virtual ~SynapseAPO();
  void SetSamplingRate(float rate);
  void OnSetParameters(const SynapseAPOParams& params);
  void DoProcess(const SynapseAPOParams& params, int* arg1, float arg2, int arg3, int arg4);

private:
  int unk168;
  SynapseAPOParams unk16C;
};

}  // namespace DSP
