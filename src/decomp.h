#ifndef DECOMP_H
#define DECOMP_H

// Force reference specific data (strings, floats) that exist in the binary
// but aren't referenced by any known decompiled function.
// No-op for MSVC builds.
#define DECOMP_FORCEACTIVE(module, ...)

#endif
