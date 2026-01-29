#include "types.h"
#include "Voice.h"
#include "../stlport/stl/_vector.h"

namespace DSP {
namespace Synapse {
namespace GranularSynth {

// Forward declaration of Voice struct
struct Voice {
    u8 _pad[0x18];
};

} // namespace GranularSynth
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

// Force instantiation
template class _Vector_base<DSP::Synapse::GranularSynth::Voice, StlNodeAlloc<DSP::Synapse::GranularSynth::Voice>>;

} // namespace stlpmtx_std
