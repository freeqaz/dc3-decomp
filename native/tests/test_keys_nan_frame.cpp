// Keys::AtFrame on a NaN frame stays inside the key vector (PLAN H-60).
//
// The crash: SIGSEGV reading 0x10 in DataArray::Node, called from
// SymbolKeys::SetFrame <- RndPropAnim::SetFrame <- RhythmBattlePlayer::Poll,
// on the song loading screen of a heavily loaded box (and, before 5cb13ffeb,
// in gameplay with a still skeleton).  RhythmDetector's AnalyzeData z-scores
// each joint axis's |speed| over a 10-sample window; a window of ten equal
// samples has variance 0, (x - mean) * (1/0) is 0 * inf = NaN, and the NaN
// rides RecordData.unk10 -> mMaxRhythmInWindow -> HamPhraseMeter::SetRatingFrac
// -> mAnim->SetFrame(NaN).  In Keys::AtFrame both range tests are false for
// NaN, KeyLessEq(NaN) walks to the last key, and `next` was &(*this)[size()]:
// one past the end.  Symbol's Interp picks `next` (NaN < 1 is false), so the
// macro name came from past-the-end memory, DataGetMacro returned null and
// ->Int(0) read offset 0x10.
//
// These tests check the container, which is where the read happens; any
// keyed animation handed a NaN frame takes the same path.
#include "test_helpers.h"

#include "math/Key.h"
#include "utl/Symbol.h"

#include <limits>

namespace {

class KeysNanFrameTest : public EngineTestFixture {};

const float kNaN = std::numeric_limits<float>::quiet_NaN();

template <class T1, class T2>
void ExpectInside(const Keys<T1, T2> &keys, const Key<T1> *p, const char *what) {
    ASSERT_NE(p, nullptr) << what;
    EXPECT_GE(p, &keys.front()) << what << " is before the first key";
    EXPECT_LE(p, &keys.back()) << what << " is past the last key (index "
                               << (p - &keys.front()) << " of " << keys.size() << ")";
}

TEST_F(KeysNanFrameTest, FloatKeysNanFrameStaysInBounds) {
    for (int n = 1; n <= 4; n++) {
        Keys<float, float> keys;
        for (int i = 0; i < n; i++)
            keys.Add((float)(i * 10), (float)(i * 100), false);
        const Key<float> *prev = nullptr;
        const Key<float> *next = nullptr;
        float ref = -1;
        keys.AtFrame(kNaN, prev, next, ref);
        SCOPED_TRACE(testing::Message() << n << " keys");
        ExpectInside(keys, prev, "prev");
        ExpectInside(keys, next, "next");
        EXPECT_EQ(ref, ref) << "ref is NaN";

        float val = -1;
        keys.AtFrame(kNaN, val);
        EXPECT_EQ(val, val) << "the interpolated value is NaN";
    }
}

// The crashing case: phrase_meter.anim's macro keys.  The value handed back
// must be one of the keys' own symbols.
TEST_F(KeysNanFrameTest, SymbolKeysNanFrameReturnsAKeyValue) {
    for (int n = 1; n <= 3; n++) {
        Keys<Symbol, Symbol> keys;
        static const char *kNames[] = {"npcrash_key_a", "npcrash_key_b", "npcrash_key_c"};
        for (int i = 0; i < n; i++)
            keys.Add(Symbol(kNames[i]), (float)(i * 1920), false);
        const Key<Symbol> *prev = nullptr;
        const Key<Symbol> *next = nullptr;
        float ref = -1;
        keys.AtFrame(kNaN, prev, next, ref);
        SCOPED_TRACE(testing::Message() << n << " keys");
        ExpectInside(keys, prev, "prev");
        ExpectInside(keys, next, "next");

        Symbol val("npcrash_unset");
        keys.AtFrame(kNaN, val);
        bool isKey = false;
        for (int i = 0; i < n; i++)
            isKey = isKey || val == Symbol(kNames[i]);
        EXPECT_TRUE(isKey) << "AtFrame(NaN) produced a symbol no key holds";
    }
}

// Control: every finite frame answers exactly as before the guard.
TEST_F(KeysNanFrameTest, FiniteFramesUnchanged) {
    Keys<float, float> keys;
    keys.Add(0.0f, 0.0f, false);
    keys.Add(100.0f, 10.0f, false);
    keys.Add(300.0f, 20.0f, false);
    const Key<float> *prev = nullptr;
    const Key<float> *next = nullptr;
    float ref = -1;

    EXPECT_EQ(keys.AtFrame(-5.0f, prev, next, ref), -1);
    EXPECT_EQ(prev, &keys[0]);
    EXPECT_EQ(next, &keys[0]);
    EXPECT_EQ(ref, 0.0f);

    EXPECT_EQ(keys.AtFrame(5.0f, prev, next, ref), 0);
    EXPECT_EQ(prev, &keys[0]);
    EXPECT_EQ(next, &keys[1]);
    EXPECT_FLOAT_EQ(ref, 0.5f);

    EXPECT_EQ(keys.AtFrame(15.0f, prev, next, ref), 1);
    EXPECT_EQ(prev, &keys[1]);
    EXPECT_EQ(next, &keys[2]);
    EXPECT_FLOAT_EQ(ref, 0.5f);

    EXPECT_EQ(keys.AtFrame(20.0f, prev, next, ref), 2);
    EXPECT_EQ(prev, &keys[2]);
    EXPECT_EQ(next, &keys[2]);

    float val = -1;
    keys.AtFrame(15.0f, val);
    EXPECT_FLOAT_EQ(val, 200.0f);
}

} // namespace
