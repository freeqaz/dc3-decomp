// ExecutablePath.h -- portable "where is the running executable" for the
// native port. Linux reads the /proc/self/exe symlink; macOS has no /proc and
// uses _NSGetExecutablePath; anything else (Emscripten) reports failure, which
// every caller already handles by falling back to a cwd-relative guess.
//
// Same contract as readlink(2), so a `readlink("/proc/self/exe", buf, n)` call
// site converts by renaming the function: writes at most `size` bytes, does
// NOT NUL-terminate, returns the length written or -1.
#pragma once

#include <cstddef>
#include <cstring>
#include <string>
#include <sys/types.h>

#if defined(__APPLE__)
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

inline ssize_t ReadExecutablePath(char *buf, size_t size) {
#if defined(__APPLE__)
    char raw[PATH_MAX];
    uint32_t rawSize = sizeof(raw);
    if (_NSGetExecutablePath(raw, &rawSize) != 0)
        return -1;
    // _NSGetExecutablePath may return a path with symlinks or "..";
    // /proc/self/exe is always canonical, so canonicalise to match.
    char resolved[PATH_MAX];
    const char *path = realpath(raw, resolved) ? resolved : raw;
    size_t len = strlen(path);
    if (len > size)
        len = size;
    memcpy(buf, path, len);
    return (ssize_t)len;
#elif defined(__linux__)
    return readlink("/proc/self/exe", buf, size);
#else
    (void)buf;
    (void)size;
    return -1;
#endif
}

// The `native/` directory of the tree the running executable was built in:
// every native binary lives directly in the CMake build dir (native/build/
// dc3-native, native/build/milo-tests, ...), so it is the executable's
// grandparent directory.  "" when the executable path is unknown (Emscripten).
//
// This is what a file the TREE ships (native/models/*, native/scripts/*,
// native/dta/*) must be found against -- never the working directory, which
// is the repo root for one launcher, native/build for another and
// orig-assets for ctest (PLAN P-10).
inline std::string NativeTreeDir() {
    char exePath[4096] = {};
    ssize_t len = ReadExecutablePath(exePath, sizeof(exePath) - 1);
    if (len <= 0)
        return std::string();
    exePath[len] = '\0';
    std::string dir(exePath);
    for (int strip = 0; strip < 2; strip++) { // the executable, then build/
        size_t slash = dir.rfind('/');
        if (slash == std::string::npos || slash == 0)
            return std::string();
        dir.resize(slash);
    }
    return dir;
}

// `rel` (a path under native/, e.g. "models/pose_landmarker_full.task")
// resolved against NativeTreeDir() -- an absolute path, whether or not the
// file exists, so a "not found" message names the real place looked.  Falls
// back to the cwd-relative "native/<rel>" only when the executable path is
// unknown.
inline std::string NativeTreePath(const char *rel) {
    std::string dir = NativeTreeDir();
    if (dir.empty())
        return std::string("native/") + rel;
    return dir + "/" + rel;
}
