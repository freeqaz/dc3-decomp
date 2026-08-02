// DC3 Native Port — DTA eval support: interpreter-state guard + result serializer.
//
// Split out of HttpServer.cpp so it can be unit-tested (milo-tests) without
// pulling in httplib or requiring a live HTTP server. Compiled into every
// native target; nothing here depends on DC3_HTTP_SERVER.
//
// Native-only (HX_NATIVE). Nothing in src/ (the PPC decomp) is modified.

#pragma once

#include <cstddef>
#include <string>

class DataNode;

namespace DtaEval {

// ---------------------------------------------------------------------------
// Request/response size caps
// ---------------------------------------------------------------------------
// These deliberately mirror the RB3Enhanced console channel so that tooling
// sized against the console works unchanged against localhost HTTP:
//   RB3E_DTA_SCRIPT_MAX = 16384  (script/request bytes)
//   RB3E_DTA_OUTPUT_MAX = 32768  (result/response bytes)
// See tools/console/dc3_eval.py and docs/tools/HTTP_DEBUG_SERVER.md.
// Exceeding either produces an explicit error (413), never silent truncation.
const size_t kMaxScriptBytes = 16384;
const size_t kMaxResultBytes = 32768;

// Max bracket nesting accepted before the parser is even invoked.
const int kMaxNestingDepth = 256;

// Recursion/element caps for serializing array-valued results.
const int kMaxArrayDepth = 8;
const int kMaxArrayElements = 256;

// ---------------------------------------------------------------------------
// JSON helpers
// ---------------------------------------------------------------------------

// Escape a byte string for inclusion in a JSON string literal. ALWAYS produces
// valid JSON / valid UTF-8: control bytes become \u00XX, and bytes that do not
// form valid UTF-8 are escaped as \u00XX (Latin-1 reading) so a Python client's
// json.loads() can never choke. Lossy for non-UTF-8 input — the kDataString
// path below uses base64 instead when it needs an exact byte round-trip.
std::string JsonEscape(const std::string &s);

// True if [data, data+len) is well-formed UTF-8 (rejects overlongs, surrogates,
// out-of-range code points, and embedded NULs are allowed as U+0000).
bool IsValidUtf8(const char *data, size_t len);

std::string Base64Encode(const unsigned char *data, size_t len);

// Format a float as a JSON number. Non-finite values are not representable in
// JSON, so they are emitted as null (the caller adds a "special" field).
std::string JsonFloat(float f);

// ---------------------------------------------------------------------------
// Result serialization
// ---------------------------------------------------------------------------
// Serializes an evaluated DataNode into the {"type":...,"typeId":...} object
// returned by /api/dta/eval. Every DataType produces a well-formed object;
// types with no meaningful payload get "value":null. Strings and globs
// round-trip exactly: valid UTF-8 is sent as-is ("encoding":"utf8"), anything
// else is base64 ("encoding":"base64").
std::string NodeToJson(const DataNode &node);

// Short name for a DataType ("int", "string", "array", ...). Never null.
const char *TypeName(int dataType);

// ---------------------------------------------------------------------------
// ScriptStateGuard — snapshot/restore of DTA interpreter globals
// ---------------------------------------------------------------------------
// The HTTP eval endpoint recovers from SIGSEGV/SIGBUS/SIGFPE/SIGABRT raised by
// arbitrary user script via siglongjmp. siglongjmp does NOT run C++ destructors,
// so every scope guard between the crash site and the setjmp frame is skipped —
// most importantly DataCallStackFrame, which pops gCallStackPtr. Without a
// repair, each crashed eval permanently burns one of the 100 handle-stack slots
// and the engine later dies on the MAIN thread in unrelated code
// (MILO_ASSERT(gCallStackPtr - gCallStack < HANDLE_STACK_SIZE) in DataArray.cpp).
//
// Declare one of these in the frame that owns the sigsetjmp, BEFORE the
// sigsetjmp call. Its destructor then runs on every exit path from that frame,
// including the post-longjmp recovery return, and cannot be bypassed by an early
// return or by a failure path added later.
class ScriptStateGuard {
public:
    ScriptStateGuard();
    ~ScriptStateGuard();

    // Restore every snapshotted global. Idempotent — safe to call directly and
    // then again from the destructor. Returns a human-readable description of
    // what had to be repaired, or "" if the interpreter unwound cleanly.
    std::string Restore();

    // Description produced by the last Restore() (empty if nothing leaked).
    const std::string &LastRepair() const { return mLastRepair; }

private:
    // Snapshotted state. Plain values only — the guard lives across a
    // siglongjmp, so it must not depend on anything being unwound.
    void *mCallStackPtr;
    void *mPreExecuteFunc;
    int mPreExecuteLevel;
    void *mDataThis;
    void *mDataDir;
    void *mVarStackPtr;
    int mMemHeapDepth;
    int mMemTempRefs;
    const char *mDataFile;
    std::string mLastRepair;
    bool mRestored;
};

} // namespace DtaEval
