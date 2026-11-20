// DC3 Native Port - web save store hook (PLAN W-36).
//
// On web the save files (MemcardMgr_Stub.cpp: the profile; Cache_Stub.cpp: the
// global options) live in MEMFS and are gone on a page reload. The page keeps
// them in IndexedDB (native/web/dc3_pre.js, "Save store"); this is how the two
// file backends tell it that a file was written or removed. The page then
// compares what is on disk with what it stored and writes the difference, so a
// call that changed nothing costs one read and compare of the save files
// (1.15 MB) and stores nothing.
//
// Desktop: a no-op (the files are real files).
#pragma once

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

inline void Dc3SaveStoreChanged() {
#ifdef __EMSCRIPTEN__
    EM_ASM({
        if (Module.dc3SaveChanged) Module.dc3SaveChanged();
    });
#endif
}
