// Regression tests for the /api/dta/eval debug endpoint's support layer
// (native/src/platform/DtaEvalSupport.{h,cpp}).
//
// Covers the three defects fixed in this change:
//   1. gCallStackPtr (and siblings) leaking on crash recovery — the endpoint
//      recovers from SIGSEGV with siglongjmp, which skips every C++ destructor,
//      so DataCallStackFrame never pops. 100 leaked frames = a delayed MAIN
//      THREAD assert in unrelated script. ScriptStateGuard repairs it.
//   2. kDataString results were unserializable (fell into the numeric default
//      arm) — plus every other DataType is now named, with exact byte
//      round-tripping for strings/globs.
//   3. Request/response byte caps are named constants matching the console
//      channel (RB3E_DTA_SCRIPT_MAX / RB3E_DTA_OUTPUT_MAX).
//
// The HTTP transport itself is exercised end-to-end by curl against a live
// headless engine (see docs/tools/HTTP_DEBUG_SERVER.md); these tests cover the
// logic that used to live inline inside HttpServer.cpp and could not be tested.

#include "test_helpers.h"

#include "platform/DtaEvalSupport.h"
#include "obj/Data.h"
#include "obj/DataFile.h"
#include "obj/DataUtl.h"
#include "utl/MemMgr.h"
#include "utl/MemHeap.h"

#include <string>
#include <cstring>

struct VarStack;
extern VarStack *gVarStackPtr;

namespace {

class DtaEvalSupportTest : public SymbolTestFixture {};

// Evaluate a DTA source string exactly the way HandleDtaEval does and return
// the serialized JSON for the final result.
std::string EvalToJson(const char *src) {
    DataArray *parsed = DataReadString(src);
    EXPECT_NE(parsed, nullptr);
    if (!parsed)
        return "";
    DataNode result(0);
    for (int i = 0; i < parsed->Size(); i++)
        result = parsed->Evaluate(i);
    parsed->Release();
    return DtaEval::NodeToJson(result);
}

bool Contains(const std::string &hay, const char *needle) {
    return hay.find(needle) != std::string::npos;
}

} // namespace

// ===========================================================================
// Defect 3: caps
// ===========================================================================

TEST_F(DtaEvalSupportTest, CapsMatchConsoleChannel) {
    // tools/console/dc3_eval.py: RB3E_DTA_SCRIPT_MAX / RB3E_DTA_OUTPUT_MAX.
    // The native endpoint used to cap requests at 8192 (cpp-httplib's
    // form-urlencoded default), which is TIGHTER than the console — tooling
    // sized against the console failed on localhost.
    EXPECT_EQ(DtaEval::kMaxScriptBytes, 16384u);
    EXPECT_EQ(DtaEval::kMaxResultBytes, 32768u);
    EXPECT_GE(DtaEval::kMaxScriptBytes, 16384u) << "must not be tighter than console";
}

// ===========================================================================
// Defect 2: serialization
// ===========================================================================

// NOTE: these parse + Evaluate literals only. Evaluating a {command} would
// need the DataFunc registry (engine init); the serializer is the unit under
// test here, and it sees the same DataNode either way.
TEST_F(DtaEvalSupportTest, SerializesInt) {
    std::string j = EvalToJson("3");
    EXPECT_EQ(j, "{\"type\":\"int\",\"typeId\":0,\"value\":3}");
}

TEST_F(DtaEvalSupportTest, SerializesFloat) {
    std::string j = EvalToJson("3.5");
    EXPECT_TRUE(Contains(j, "\"type\":\"float\"")) << j;
    EXPECT_TRUE(Contains(j, "3.5")) << j;
}

TEST_F(DtaEvalSupportTest, SerializesSymbol) {
    std::string j = EvalToJson("'hello_sym'");
    EXPECT_EQ(j, "{\"type\":\"symbol\",\"typeId\":5,\"value\":\"hello_sym\"}");
}

// The headline of defect 2: a string result used to fall into the numeric
// default arm and come back as {"type":18,"value":null}.
TEST_F(DtaEvalSupportTest, SerializesString) {
    std::string j = EvalToJson("\"hello world\"");
    EXPECT_TRUE(Contains(j, "\"type\":\"string\"")) << j;
    EXPECT_TRUE(Contains(j, "\"typeId\":18")) << j;
    EXPECT_TRUE(Contains(j, "\"encoding\":\"utf8\"")) << j;
    EXPECT_TRUE(Contains(j, "\"value\":\"hello world\"")) << j;
    EXPECT_TRUE(Contains(j, "\"bytes\":11")) << j;
}

TEST_F(DtaEvalSupportTest, SerializesEmptyString) {
    std::string j = EvalToJson("\"\"");
    EXPECT_TRUE(Contains(j, "\"type\":\"string\"")) << j;
    EXPECT_TRUE(Contains(j, "\"value\":\"\"")) << j;
    EXPECT_TRUE(Contains(j, "\"bytes\":0")) << j;
}

TEST_F(DtaEvalSupportTest, SerializesStringNodeDirectly) {
    // Build the node the way sprintf/sprint do, bypassing the DTA lexer's
    // escape rules, so quotes/newlines/tabs are really in the payload.
    DataNode node("say \"hi\"\n\tdone\\");
    std::string j = DtaEval::NodeToJson(node);
    EXPECT_TRUE(Contains(j, "\\\"hi\\\"")) << j;
    EXPECT_TRUE(Contains(j, "\\n")) << j;
    EXPECT_TRUE(Contains(j, "\\t")) << j;
    EXPECT_TRUE(Contains(j, "\\\\")) << j;
    // No raw control bytes may survive into the JSON body.
    for (char c : j)
        EXPECT_FALSE((unsigned char)c < 0x20) << "raw control byte in JSON";
}

TEST_F(DtaEvalSupportTest, StringWithNonAsciiUtf8PassesThrough) {
    DataNode node("caf\xC3\xA9 \xE2\x9C\x93");
    std::string j = DtaEval::NodeToJson(node);
    EXPECT_TRUE(Contains(j, "\"encoding\":\"utf8\"")) << j;
    EXPECT_TRUE(Contains(j, "caf\xC3\xA9")) << j;
}

TEST_F(DtaEvalSupportTest, StringWithInvalidUtf8UsesBase64) {
    // Latin-1 / binary junk must not be emitted raw: a Python client's
    // json.loads() would fail on the invalid UTF-8. Base64 round-trips exactly.
    DataNode node("caf\xE9 \xFF\xFE");
    std::string j = DtaEval::NodeToJson(node);
    EXPECT_TRUE(Contains(j, "\"encoding\":\"base64\"")) << j;
    EXPECT_TRUE(Contains(j, "\"bytes\":7")) << j; // c a f 0xE9 ' ' 0xFF 0xFE
    EXPECT_TRUE(Contains(j, "\"value\":\"Y2Fm6SD//g==\"")) << j;
    EXPECT_FALSE(Contains(j, "\xE9")) << "raw invalid byte leaked into JSON";
}

TEST_F(DtaEvalSupportTest, EscaperNeverEmitsInvalidUtf8) {
    std::string junk;
    for (int i = 0; i < 256; i++)
        junk += (char)i;
    std::string esc = DtaEval::JsonEscape(junk);
    EXPECT_TRUE(DtaEval::IsValidUtf8(esc.data(), esc.size()));
    for (char c : esc)
        EXPECT_FALSE((unsigned char)c < 0x20);
}

TEST_F(DtaEvalSupportTest, Utf8Validator) {
    EXPECT_TRUE(DtaEval::IsValidUtf8("", 0));
    EXPECT_TRUE(DtaEval::IsValidUtf8("ascii", 5));
    EXPECT_TRUE(DtaEval::IsValidUtf8("\xC3\xA9", 2));       // é
    EXPECT_TRUE(DtaEval::IsValidUtf8("\xE2\x9C\x93", 3));   // ✓
    EXPECT_TRUE(DtaEval::IsValidUtf8("\xF0\x9F\x8E\xB8", 4)); // 🎸
    EXPECT_FALSE(DtaEval::IsValidUtf8("\xC3", 1));          // truncated
    EXPECT_FALSE(DtaEval::IsValidUtf8("\xC3\x28", 2));      // bad continuation
    EXPECT_FALSE(DtaEval::IsValidUtf8("\xC0\xAF", 2));      // overlong
    EXPECT_FALSE(DtaEval::IsValidUtf8("\xED\xA0\x80", 3));  // surrogate
    EXPECT_FALSE(DtaEval::IsValidUtf8("\xFF", 1));
}

TEST_F(DtaEvalSupportTest, Base64RoundTripsKnownVectors) {
    EXPECT_EQ(DtaEval::Base64Encode((const unsigned char *)"", 0), "");
    EXPECT_EQ(DtaEval::Base64Encode((const unsigned char *)"f", 1), "Zg==");
    EXPECT_EQ(DtaEval::Base64Encode((const unsigned char *)"fo", 2), "Zm8=");
    EXPECT_EQ(DtaEval::Base64Encode((const unsigned char *)"foo", 3), "Zm9v");
    EXPECT_EQ(DtaEval::Base64Encode((const unsigned char *)"foobar", 6), "Zm9vYmFy");
}

TEST_F(DtaEvalSupportTest, NonFiniteFloatsAreValidJson) {
    // std::to_string(NAN) emits "nan", which is NOT valid JSON — the old
    // serializer would have produced a body Python could not parse.
    DataNode nan(0.0f / 0.0f);
    std::string j = DtaEval::NodeToJson(nan);
    EXPECT_TRUE(Contains(j, "\"value\":null")) << j;
    EXPECT_TRUE(Contains(j, "\"special\":\"nan\"")) << j;
    EXPECT_FALSE(Contains(j, "nan\","));
    EXPECT_EQ(DtaEval::JsonFloat(1.0f / 0.0f), "null");
    EXPECT_EQ(DtaEval::JsonFloat(1.5f), "1.5");
}

TEST_F(DtaEvalSupportTest, SerializesArrayRecursively) {
    DataArray *arr = new DataArray(3);
    arr->Node(0) = DataNode(7);
    arr->Node(1) = DataNode("str");
    arr->Node(2) = DataNode(Symbol("sym"));
    DataNode node(arr, kDataArray);
    arr->Release();

    std::string j = DtaEval::NodeToJson(node);
    EXPECT_TRUE(Contains(j, "\"type\":\"array\"")) << j;
    EXPECT_TRUE(Contains(j, "\"size\":3")) << j;
    EXPECT_TRUE(Contains(j, "\"value\":7")) << j;
    EXPECT_TRUE(Contains(j, "\"value\":\"str\"")) << j;
    EXPECT_TRUE(Contains(j, "\"value\":\"sym\"")) << j;
}

TEST_F(DtaEvalSupportTest, EveryDataTypeSerializesToNamedJsonObject) {
    // No DataType may fall through to a bare numeric type id or malformed JSON.
    const int kTypes[] = { kDataInt,     kDataFloat,  kDataVar,     kDataFunc,
                           kDataObject,  kDataSymbol, kDataUnhandled, kDataIfdef,
                           kDataElse,    kDataEndif,  kDataArray,   kDataCommand,
                           kDataString,  kDataProperty, kDataGlob,  kDataDefine,
                           kDataInclude, kDataMerge,  kDataIfndef,  kDataAutorun,
                           kDataUndef };
    for (int t : kTypes) {
        const char *name = DtaEval::TypeName(t);
        ASSERT_NE(name, nullptr);
        EXPECT_STRNE(name, "unknown") << "type " << t << " has no name";
    }
    // Types whose payload is a null pointer must still produce valid JSON.
    for (int t : { kDataUnhandled, kDataIfdef, kDataDefine, kDataFunc }) {
        DataNode node((DataType)t, 0);
        std::string j = DtaEval::NodeToJson(node);
        EXPECT_EQ(j.front(), '{');
        EXPECT_EQ(j.back(), '}');
        EXPECT_TRUE(Contains(j, "\"typeId\":")) << j;
    }
}

// ===========================================================================
// Defect 1: interpreter-state leak on crash recovery
// ===========================================================================

TEST_F(DtaEvalSupportTest, GuardRestoresCallStackPointer) {
    DataArray **base = gCallStackPtr;
    {
        DtaEval::ScriptStateGuard guard;
        // Simulate what a crash mid-Execute leaves behind: DataCallStackFrame
        // pushed frames whose destructors were skipped by siglongjmp.
        gCallStackPtr += 4;
        std::string repaired = guard.Restore();
        EXPECT_TRUE(Contains(repaired, "call stack depth")) << repaired;
        EXPECT_EQ(gCallStackPtr, base);
    }
    EXPECT_EQ(gCallStackPtr, base);
}

TEST_F(DtaEvalSupportTest, GuardRestoresOnDestructionWithoutExplicitCall) {
    DataArray **base = gCallStackPtr;
    {
        DtaEval::ScriptStateGuard guard;
        gCallStackPtr += 3;
    } // destructor must repair even though Restore() was never called
    EXPECT_EQ(gCallStackPtr, base);
}

TEST_F(DtaEvalSupportTest, GuardRestoresSiblingState) {
    Hmx::Object *oldThis = gDataThis;
    ObjectDir *oldDir = gDataDir;
    DataFunc *oldPreExec = gPreExecuteFunc;
    int oldLevel = gPreExecuteLevel;
    VarStack *oldVarStack = gVarStackPtr;
    MemHeapStack &heap = ThreadMemStack(true);
    int oldHeapDepth = heap.mSize;
    int oldTempRefs = heap.mTempRefs;

    {
        DtaEval::ScriptStateGuard guard;
        gDataThis = (Hmx::Object *)0x1234;
        gDataDir = (ObjectDir *)0x5678;
        gPreExecuteFunc = (DataFunc *)0x9abc;
        gPreExecuteLevel = 42;
        gVarStackPtr = (VarStack *)((char *)gVarStackPtr + 64);
        heap.mSize += 2;
        heap.mTempRefs += 1;
    }

    EXPECT_EQ(gDataThis, oldThis);
    EXPECT_EQ(gDataDir, oldDir);
    EXPECT_EQ(gPreExecuteFunc, oldPreExec);
    EXPECT_EQ(gPreExecuteLevel, oldLevel);
    EXPECT_EQ(gVarStackPtr, oldVarStack);
    EXPECT_EQ(ThreadMemStack(true).mSize, oldHeapDepth);
    EXPECT_EQ(ThreadMemStack(true).mTempRefs, oldTempRefs);
}

TEST_F(DtaEvalSupportTest, GuardReportsNothingWhenNothingLeaked) {
    DtaEval::ScriptStateGuard guard;
    EXPECT_EQ(guard.Restore(), "");
}

// The actual regression for the reported bug: dozens of crashed evals in a row
// must not drift the handle stack. HANDLE_STACK_SIZE is 100, so 200 leaked
// frames without the guard would blow the MILO_ASSERT in DataCallStackFrame
// on the main thread, in whatever script ran next.
TEST_F(DtaEvalSupportTest, RepeatedCrashesDoNotDriftHandleStack) {
    DataArray **base = gCallStackPtr;
    for (int i = 0; i < 200; i++) {
        DtaEval::ScriptStateGuard guard;
        gCallStackPtr += 1 + (i % 5); // frames abandoned by the "crash"
        gDataThis = (Hmx::Object *)(intptr_t)(i + 1);
    }
    EXPECT_EQ(gCallStackPtr, base);
    EXPECT_LT(gCallStackPtr - gCallStack, HANDLE_STACK_SIZE);
}

TEST_F(DtaEvalSupportTest, GuardIsIdempotent) {
    DataArray **base = gCallStackPtr;
    DtaEval::ScriptStateGuard guard;
    gCallStackPtr += 2;
    std::string first = guard.Restore();
    EXPECT_FALSE(first.empty());
    // A second call must not "restore" a now-correct state into something else,
    // and must report the same thing.
    gCallStackPtr += 1;
    EXPECT_EQ(guard.Restore(), first);
    EXPECT_EQ(gCallStackPtr, base + 1);
    gCallStackPtr = base; // leave the interpreter as we found it
}
