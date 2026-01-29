#pragma once

#include "../stlport/stl/_vector.h"

namespace DSP {
namespace Synapse {
namespace GranularSynth {

struct Voice;

} // namespace GranularSynth
} // namespace Synapse
} // namespace DSP

namespace stlpmtx_std {

// Explicit specialization of _Vector_base constructor
template <>
inline _Vector_base<DSP::Synapse::GranularSynth::Voice, StlNodeAlloc<DSP::Synapse::GranularSynth::Voice>>::_Vector_base(
    size_t __n,
    const StlNodeAlloc<DSP::Synapse::GranularSynth::Voice>& __a
) : _M_start(0), _M_finish(0), _M_end_of_storage(__a, 0)
{
    _M_start = _M_end_of_storage.allocate(__n);
    _M_finish = _M_start;
    _M_end_of_storage._M_data = _M_start + __n;
}

} // namespace stlpmtx_std
