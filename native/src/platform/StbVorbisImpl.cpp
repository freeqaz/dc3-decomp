// StbVorbisImpl.cpp — compiles the vendored stb_vorbis implementation exactly
// once. Every other includer of platform/XmaPcmSidecar.h (SampleData.cpp) gets
// header-only declarations, so the decoder symbols link from this TU alone.
#ifdef HX_NATIVE
#define DC3_STB_VORBIS_IMPL
#include "platform/XmaPcmSidecar.h"
#endif
