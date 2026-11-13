#pragma once

// The budget App::App gives the persistent file cache (PLAN H-11).
//
// config/persistent_filecache.dta's first line is a budget in ON-DISC bytes:
// the 360 reads the compressed .milo_xbox from the ark.  Desktop reads the ark
// too (the same bytes), so it gets the image's budget unchanged.  The web
// build reads the inflated extraction (extract_ark.sh --inflate, served by
// native/web/server.py): the shipped list is 5.2 MiB there against a 2 MiB
// budget, and FileCache::DumpOverSize would evict whole entries in the order
// the host's std::sort leaves equal priorities in -- which can be the
// loading-music .mid.  Scale it by the inflate ratio's ceiling, as
// PreloadPanel does for the song preload budget (W-6).
// Guarded by native/tests/test_persistent_filecache.cpp.
inline int NativePersistentFileCacheBudget(int imageBudget, bool web) {
    return web ? imageBudget * 4 : imageBudget;
}
