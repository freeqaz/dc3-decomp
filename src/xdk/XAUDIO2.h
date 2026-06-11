#pragma once
// xaudio2.h transitively pulls xapo.h (IUnknown + tWAVEFORMATEX). The real
// ATG::CSampleXAPOBase template (xapobase.h) is intentionally NOT pulled in here so
// that translation units which include this header alongside a local hand-rolled
// ATG::CSampleXAPOBase (EnvelopeGenerator.h, HeadsetXferEffect.h, SynapseAPO.h) do not
// hit an ODR collision. Code that needs the real template includes
// dsp/StandardEffect.h, which pulls xapobase.h directly.
#include "xdk/xaudio2/xaudio2.h"
