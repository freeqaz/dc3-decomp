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
