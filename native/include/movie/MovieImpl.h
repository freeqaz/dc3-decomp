// Shim: the shared engine still spells this header by its pre-rename name.
//
// b606a4c96 renamed src/system/movie/MovieImpl.h to MovieImpl_p.h, on the
// evidence of the __FILE__ retail embedded in the asserts inside it, and fixed
// the 36 include sites inside this repo. milo-native-engine is a separate repo,
// so its one site -- src/platform/FFmpegMovieImpl.h:8, `#include
// "movie/MovieImpl.h"` -- was not carried along, and every native target that
// links libmilo-engine.a has failed to build since.
//
// The engine cannot simply switch to the new spelling: rb3-xenon, which shares
// it, still has src/system/movie/MovieImpl.h under the old name. So the engine
// wants an __has_include fork, and that belongs in the engine repo. Until it
// lands there, this file answers the old spelling from DC3's native include
// path -- native/include is native-only and is not on the PPC compiler's -I
// list, so no decomp translation unit can see it.
#pragma once

#include "movie/MovieImpl_p.h"
