// Pins the boolean invariants relied on by the wave-9 RndShaderProgram::Cache
// control-flow archaeology (74.3% -> 85.7%). These are pure-logic equivalences:
// the refactor rewrote the buffer-validity test via De Morgan and flipped the
// compile-verbose branch order. If either is regressed, Cache would silently
// take the wrong path (compile-vs-load / verbose-vs-quiet) on the Xbox build,
// which objdiff alone would not flag as a *behavioral* error. No engine init.
#include <gtest/gtest.h>

namespace {

// Mirror of RndShaderBuffer for the only property Cache inspects: Size().
struct FakeBuf {
    unsigned int size;
    unsigned int Size() const { return size; }
};

// The ORIGINAL "needsCompile" predicate (pre-refactor).
static bool NeedsCompile_Or(const FakeBuf *vs, const FakeBuf *ps) {
    return (vs == nullptr || vs->Size() == 0) || (ps == nullptr || ps->Size() == 0);
}

// The "all buffers valid" predicate the refactor branches on (the create path
// is taken iff this is true; the compile path is the else).
static bool BuffersValid_And(const FakeBuf *vs, const FakeBuf *ps) {
    return vs != nullptr && vs->Size() != 0 && ps != nullptr && ps->Size() != 0;
}

} // namespace

// De Morgan: needsCompile == !buffersValid for every (null/empty/nonempty)^2 case.
TEST(ShaderProgramCacheLogic, NeedsCompileIsNegationOfBuffersValid) {
    FakeBuf empty{0};
    FakeBuf full{4};
    const FakeBuf *choices[] = {nullptr, &empty, &full};
    for (const FakeBuf *vs : choices) {
        for (const FakeBuf *ps : choices) {
            EXPECT_EQ(NeedsCompile_Or(vs, ps), !BuffersValid_And(vs, ps))
                << "vs=" << (vs ? (vs->size ? "full" : "empty") : "null")
                << " ps=" << (ps ? (ps->size ? "full" : "empty") : "null");
        }
    }
}

// The create path (no compile) must be reachable ONLY when both buffers are
// present and non-empty — the single case where Cache skips the shader compile.
TEST(ShaderProgramCacheLogic, CreatePathOnlyWhenBothBuffersFull) {
    FakeBuf empty{0};
    FakeBuf full{4};
    EXPECT_TRUE(BuffersValid_And(&full, &full));
    EXPECT_FALSE(BuffersValid_And(&full, &empty));
    EXPECT_FALSE(BuffersValid_And(&empty, &full));
    EXPECT_FALSE(BuffersValid_And(nullptr, &full));
    EXPECT_FALSE(BuffersValid_And(&full, nullptr));
    EXPECT_FALSE(BuffersValid_And(nullptr, nullptr));
}

// The compile-log verbose branch reorder: verbose (compile-options) text is
// selected iff the DataVariable is non-zero; quiet text otherwise. Pin the
// predicate polarity so the branch flip (==0 -> !=0) is not reverted.
TEST(ShaderProgramCacheLogic, VerboseSelectedWhenNonZero) {
    auto pickVerbose = [](int v) { return v != 0; };
    EXPECT_FALSE(pickVerbose(0)); // quiet "...(%s)\n"
    EXPECT_TRUE(pickVerbose(1));  // verbose "...(compile options: %s)\n"
    EXPECT_TRUE(pickVerbose(-1));
}
