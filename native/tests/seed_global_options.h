#pragma once
// The global-options cache entry a previous session would have left, for the
// tests that boot a child dc3-native against their own persistence root
// (test_dta_flow.cpp, test_native_song_select.cpp).

#include <cstring>
#include <fstream>
#include <string>
#include <sys/stat.h>

// Tutorial bits, config/macros.dta:1466-1489 (ProfileMgr::mTutorialsSeen).
constexpr int kTutorialGeneral = 1;
constexpr int kTutorialFitness = 128;
constexpr int kTutorialPlaylists = 2048;

// Write the global-options cache entry a previous session would have left:
// <root>/cache/global/global, the 0x39-byte block ProfileMgr::SaveGlobalOptions
// fills and SaveLoadManager writes through the cache (states 0x32/0x33,
// kStrGlobalCacheName).  The bytes are what the native engine itself wrote at
// the end of a 2026-11-13 Perform run, with mTutorialsSeen (an int at 0x28,
// after version/mono/sync/volumes/the nine option bools/sync preset/overscan)
// replaced by `tutorialsSeen`.  The offset is measured, not derived: zeroing
// byte 0x28 alone turns {profile_mgr has_seen_tutorial kTutorialGeneral} from
// 1 to 0 on the next boot.  Tutorials-seen is per CONSOLE -- it lives here,
// not in DC3_SAVE_DIR/profile_0_save.dat -- so a save dir without its cache
// dir is a console that has never seen a tutorial.
inline bool SeedGlobalOptions(const std::string &root, int tutorialsSeen) {
    static const unsigned char kGlobalOptions[0x39] = {
        0x1c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb0, 0xc1, 0x00, 0x00, 0x00,
        0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x09, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    unsigned char bytes[sizeof(kGlobalOptions)];
    memcpy(bytes, kGlobalOptions, sizeof(bytes));
    for (int i = 0; i < 4; i++)  // little-endian int, as the native stream writes it
        bytes[0x28 + i] = (unsigned char)((unsigned)tutorialsSeen >> (8 * i));
    std::string dir = root + "/cache/global";
    mkdir(dir.c_str(), 0755);
    std::ofstream f(dir + "/global", std::ios::binary);
    if (!f.is_open()) return false;
    f.write((const char *)bytes, sizeof(bytes));
    return f.good();
}
