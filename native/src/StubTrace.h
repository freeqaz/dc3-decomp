// StubTrace — opt-in runtime hit counter for native engine stubs (roadmap N.2).
//
// The native port links a large set of silent return-0 / return-nullptr stubs
// (native/src/engine_stubs_generated.cpp) for Xbox SDK / Kinect / Bink / online
// symbols it does not implement. When one is hit during gameplay there is no
// signal at all — it neither logs nor counts. This turns those silent stubs into
// an evidence-ranked worklist.
//
// Usage: put HX_STUB_TRACE("Symbol") as the first statement of a stub body.
//
// Cost when OFF: a single load + predicted-not-taken branch on a global bool
// (gStubTraceEnabled). The hash-map insert only runs when tracing is enabled,
// which is opt-in via the DC3_STUB_TRACE=1 environment variable (read once at
// first use). Off by default, so normal builds/runs pay essentially nothing.

#pragma once

#include <cstdint>
#include <string>

namespace dc3 {

// Set once, on first StubTrace use, from the DC3_STUB_TRACE env var (!=0/"" ).
extern bool gStubTraceEnabled;

// Slow path: records a hit for `name`. Only called when tracing is enabled.
void StubTraceHit(const char* name);

// Lazily initializes gStubTraceEnabled from the environment. Returns the flag.
bool StubTraceInit();

// Serializes the current hit table to a ranked JSON array
// [{"name":"Foo","count":3},...] (descending count). Always callable (returns
// "[]" when empty). total = sum of all counts, distinct = number of distinct
// stubs hit. We return JSON directly to keep the HTTP layer trivial and hold the
// table lock for the shortest possible window.
class StubTraceDump {
public:
    static std::string ToJson(uint64_t* total = nullptr, uint64_t* distinct = nullptr);

    // Writes the ranked JSON (ToJson) to `path`. Returns the number of distinct
    // stubs written, or -1 on file-open failure. Used to capture the stub-hit
    // worklist even when the engine crashes during boot before /api/stubs can be
    // polled (the crash signal handler calls this when DC3_STUB_TRACE_DUMP is set).
    static long DumpToFile(const char* path);
};

}  // namespace dc3

// The instrumentation macro. Branch-predicted off by default.
#define HX_STUB_TRACE(name)                       \
    do {                                          \
        if (::dc3::gStubTraceEnabled) {           \
            ::dc3::StubTraceHit(name);            \
        }                                         \
    } while (0)
