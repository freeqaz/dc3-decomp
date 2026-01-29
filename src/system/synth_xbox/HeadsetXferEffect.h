#pragma once

// Minimal forward declarations to allow compilation
struct HeadsetXferEffectParams {};

class CXAPOParametersBase {
public:
    virtual ~CXAPOParametersBase() {}
    virtual void SetParameters(const void *, unsigned int) = 0;
};

class CXAPOBase : public CXAPOParametersBase {
public:
    CXAPOBase();
    virtual ~CXAPOBase() {}
};

template<typename T, typename ParamsT>
class CSampleXAPOBase : public CXAPOBase {
public:
    CSampleXAPOBase();
    virtual ~CSampleXAPOBase() {}
};

class HeadsetXferEffect : public CSampleXAPOBase<HeadsetXferEffect, HeadsetXferEffectParams> {
public:
    HeadsetXferEffect();
};
