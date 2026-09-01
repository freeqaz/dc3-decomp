#include "types.h"
#include "GranularSynth.h"
#ifndef HX_NATIVE
#include "..\stlport\stl\_uninitialized.h"
#include <math.h>

// Synapse natural-log helper with a small-magnitude clamp (avoids log(0)).
namespace Util {
template <class T>
T Log(const T &x) {
    if (x < 1.0000000359391298e-36f && x > -1.0000000359391298e-36f) {
        return -1.0000000409184788e+35f;
    }
    return (T)log(x);
}
template float Log<float>(const float &);
} // namespace Util

namespace DSP {
namespace Synapse {

GranularSynth::~GranularSynth() {
}

// Compact the granule pool: keep the active grains at the front (in order) and
// clear the active flag on every slot they no longer occupy.
void GranularSynth::Flush() {
    unsigned int kept = 0;
    for (unsigned int i = 0; i < mGranules.size(); i++) {
        if (mGranules[i].mActive) {
            mGranules[kept] = mGranules[i];
            kept++;
        }
    }

    for (unsigned int j = kept; j < mGranules.size(); j++) {
        mGranules[j].mActive = false;
    }
}

} // namespace Synapse
} // namespace DSP

namespace stlpmtx_std {

// Explicit specialization of _Vector_base constructor for UVoice
template <>
_Vector_base<DSP::Synapse::GranularSynth::Voice, StlNodeAlloc<DSP::Synapse::GranularSynth::Voice>>::_Vector_base(
    size_t __n,
    const StlNodeAlloc<DSP::Synapse::GranularSynth::Voice>& __a
) : _M_start(0), _M_finish(0), _M_end_of_storage(__a, 0)
{
    _M_start = _M_end_of_storage.allocate(__n);
    _M_finish = _M_start;
    _M_end_of_storage._M_data = _M_start + __n;
}

// Explicit specialization of vector fill constructor for UVoice
template <>
vector<DSP::Synapse::GranularSynth::Voice, StlNodeAlloc<DSP::Synapse::GranularSynth::Voice>>::vector(
    size_type __n,
    const DSP::Synapse::GranularSynth::Voice& __val,
    const StlNodeAlloc<DSP::Synapse::GranularSynth::Voice>& __a
) : _Vector_base<DSP::Synapse::GranularSynth::Voice, StlNodeAlloc<DSP::Synapse::GranularSynth::Voice>>(__n, __a)
{
    _M_finish = __uninitialized_fill_n(_M_start, __n, __val, __false_type());
}

// Explicit specialization of vector constructor for Granule
template <>
vector<DSP::Synapse::GranularSynth::Granule, StlNodeAlloc<DSP::Synapse::GranularSynth::Granule>>::vector(
    unsigned int __n,
    const DSP::Synapse::GranularSynth::Granule& __val,
    const StlNodeAlloc<DSP::Synapse::GranularSynth::Granule>& __a
) : _Vector_base<DSP::Synapse::GranularSynth::Granule, StlNodeAlloc<DSP::Synapse::GranularSynth::Granule>>(__n, __a)
{
    this->_M_finish = __uninitialized_fill_n(this->_M_start, __n, __val, __type_traits<DSP::Synapse::GranularSynth::Granule>::is_POD_type());
}

} // namespace stlpmtx_std
#endif // HX_NATIVE
