// Regression pin for the "null `this` store the console absorbed" class.
//
// The instance: MoveDir::PostUpdateFilters computes
//
//     bool active = feedback && playerData && playerData->IsPlaying();
//     if (!active) { feedback->ResetErrors(); }
//
// `active` is a THREE-way conjunction, one conjunct of which is `feedback !=
// null`, so the `!active` arm is reached with a null `feedback` whenever the
// venue world had no player%i/char_feedback.cf to bind (MoveDir.cpp:718).
// Retail really does this -- PostUpdateFilters is 508/508 equal against the
// shipped Xbox 360 binary at diff score 0/50800 -- and every OTHER ResetErrors
// call site in that file guards (718-722, 808-809, 899-902).
//
// It is survivable on the 360 and fatal here because of a MEMORY-MAP
// difference, not a pointer-width one. CharFeedback::ResetErrors is
// non-virtual and only stores to mLimbStates, 0x7c..0xe4 from `this`; the
// console maps guest page 0 (0x0-0x10000) readable, writable and zeroed, so
// those stores scribble a zero page and the function returns. Xenia has to
// reproduce that same fact to run this title (memory.cc protect_zero=false:
// "the real 360 maps a readable and writable (zeroed) low page"). Linux never
// maps page 0, so the identical store SIGSEGVs. LP64 grows the offsets, but
// they stay far inside 0x10000 -- widening is not what flips the outcome.
//
// Two things are pinned here, and they fail for different reasons:
//
//   1. HostHasNoWritableLowPage -- the PREMISE. If the host ever gained a
//      mapped page 0 (an mmap_min_addr=0 sysctl, a deliberate remap like the
//      one dc3_hack_pack does for Xenia), the guard's rationale would quietly
//      evaporate and this class would stop being a crash. Pin the premise so
//      that change is loud.
//
//   2. PostUpdateFiltersGuardsFeedback -- the FIX. The guard lives inside
//      `#ifdef HX_NATIVE` so the PPC codegen stays byte-identical, which means
//      no runtime call reaches the fixed line on a PPC build and no unit test
//      can construct the MoveDir object graph that reaches it here (it derefs
//      mFilterQueue, mAsyncDetector, TheMoveMgr, TheHamProvider and TheGameData
//      before ever getting to the arm). So this pins the guard at the source
//      level, on the HX_NATIVE-active text only.
//
//      A source-text assertion is only worth having if its detector can
//      actually say "unguarded", so the detector is exercised on synthetic
//      guarded AND unguarded inputs -- including a copy of the exact pre-fix
//      text -- inside the test, before it is pointed at the real file.

#include <gtest/gtest.h>

#include <csignal>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Detector
// ---------------------------------------------------------------------------

// Keep only the lines a compiler with HX_NATIVE defined would see. The `#else`
// arm of the fix deliberately contains the unguarded retail statement, so a
// preprocessor-blind scan would report the fixed file as still broken -- and,
// worse, would report a file that had LOST the guard as still fixed.
std::string KeepHxNativeArms(const std::string &text) {
    std::istringstream in(text);
    std::string line;
    std::string out;
    // Each element: are we currently emitting inside this conditional?
    std::vector<bool> emitting;
    auto Emitting = [&] {
        for (bool e : emitting)
            if (!e)
                return false;
        return true;
    };
    while (std::getline(in, line)) {
        const std::string t = line.substr(line.find_first_not_of(" \t") == std::string::npos
                                              ? 0
                                              : line.find_first_not_of(" \t"));
        if (t.rfind("#ifdef HX_NATIVE", 0) == 0 || t.rfind("#if defined(HX_NATIVE)", 0) == 0) {
            emitting.push_back(true);
            continue;
        }
        if (t.rfind("#ifndef HX_NATIVE", 0) == 0) {
            emitting.push_back(false);
            continue;
        }
        if (t.rfind("#if", 0) == 0) { // unrelated conditional: keep both arms
            emitting.push_back(true);
            continue;
        }
        if (t.rfind("#else", 0) == 0) {
            if (!emitting.empty())
                emitting.back() = !emitting.back();
            continue;
        }
        if (t.rfind("#endif", 0) == 0) {
            if (!emitting.empty())
                emitting.pop_back();
            continue;
        }
        if (Emitting())
            out += line + "\n";
    }
    return out;
}

// Slice out the `if (!active) { ... }` arm, brace-balanced.
bool ExtractNotActiveArm(const std::string &text, std::string *arm) {
    const std::string kMarker = "if (!active) {";
    size_t start = text.find(kMarker);
    if (start == std::string::npos)
        return false;
    size_t i = start + kMarker.size();
    int depth = 1;
    for (; i < text.size() && depth > 0; ++i) {
        if (text[i] == '{')
            ++depth;
        else if (text[i] == '}')
            --depth;
    }
    if (depth != 0)
        return false;
    *arm = text.substr(start, i - start);
    return true;
}

enum class ArmVerdict { kNoArm, kGuarded, kUnguarded };

// The arm is GUARDED iff every `feedback->` in it is preceded, within the arm,
// by an explicit null test on `feedback`.
ArmVerdict ClassifyNotActiveArm(const std::string &sourceText) {
    std::string arm;
    if (!ExtractNotActiveArm(KeepHxNativeArms(sourceText), &arm))
        return ArmVerdict::kNoArm;

    size_t firstDeref = arm.find("feedback->");
    if (firstDeref == std::string::npos)
        return ArmVerdict::kGuarded; // nothing dereferenced, nothing to fault

    const std::string before = arm.substr(0, firstDeref);
    // Comments in the arm explain the guard and mention `feedback`; only accept
    // a real test expression.
    const char *kGuards[] = {
        "if (feedback)",
        "if (feedback != nullptr)",
        "if (feedback != NULL)",
        "if (!feedback)",
    };
    for (const char *g : kGuards)
        if (before.find(g) != std::string::npos)
            return ArmVerdict::kGuarded;
    return ArmVerdict::kUnguarded;
}

// The exact pre-fix text, verbatim from MoveDir.cpp before the guard landed.
const char *kPreFixArm = R"CPP(
        if (!active) {
            feedback->ResetErrors();
        } else {
            HamMove *move = mMovePlayerData[i].mCurMove;
        }
)CPP";

// The shape a "fix" that forgot the #else would have: guard present, but a
// preprocessor-blind detector would also accept the pre-fix text, so this
// input is what separates the two.
const char *kFixedArm = R"CPP(
        if (!active) {
#ifdef HX_NATIVE
            if (feedback) {
                feedback->ResetErrors();
            }
#else
            feedback->ResetErrors();
#endif
        } else {
            HamMove *move = mMovePlayerData[i].mCurMove;
        }
)CPP";

// A fix that guarded only the PPC arm by mistake -- i.e. the native build still
// crashes. Must classify as UNGUARDED or the detector is worthless.
const char *kBackwardsFixArm = R"CPP(
        if (!active) {
#ifdef HX_NATIVE
            feedback->ResetErrors();
#else
            if (feedback) {
                feedback->ResetErrors();
            }
#endif
        } else {
            HamMove *move = mMovePlayerData[i].mCurMove;
        }
)CPP";

std::string ReadFile(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return std::string();
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace

// ---------------------------------------------------------------------------
// 1. The premise: the host has no writable low page.
// ---------------------------------------------------------------------------

TEST(NullThisStoreDeathTest, HostHasNoWritableLowPage) {
    // 0x7c is CharFeedback::mLimbStates[0] on the PPC layout -- the first
    // address CharFeedback::ResetErrors writes through a null `this`. On the
    // 360 this lands in the mapped, writable, zeroed page 0 and is harmless.
    // Here it must be fatal; if it ever stops being fatal, the guard in
    // MoveDir::PostUpdateFilters is no longer load-bearing and this test is the
    // place that should say so.
    EXPECT_DEATH(
        {
            volatile unsigned char *p = reinterpret_cast<volatile unsigned char *>(0x7c);
            *p = 0;
            // Keep the store from being elided if the platform ever tolerates it.
            _exit(*p);
        },
        ""
    );
}

// ---------------------------------------------------------------------------
// 2. The detector's own negative controls, then the real file.
// ---------------------------------------------------------------------------

TEST(MoveDirFeedbackGuard, DetectorReportsUnguardedOnPreFixText) {
    EXPECT_EQ(ClassifyNotActiveArm(kPreFixArm), ArmVerdict::kUnguarded)
        << "The detector cannot see the bug it exists to catch.";
}

TEST(MoveDirFeedbackGuard, DetectorReportsUnguardedWhenOnlyThePpcArmIsGuarded) {
    EXPECT_EQ(ClassifyNotActiveArm(kBackwardsFixArm), ArmVerdict::kUnguarded)
        << "A guard in the #else arm does nothing for the native build; the "
           "detector must not be fooled by it.";
}

TEST(MoveDirFeedbackGuard, DetectorReportsGuardedOnFixedText) {
    EXPECT_EQ(ClassifyNotActiveArm(kFixedArm), ArmVerdict::kGuarded);
}

TEST(MoveDirFeedbackGuard, PostUpdateFiltersGuardsFeedback) {
    const std::string path = std::string(DC3_DECOMP_SRC_DIR) + "/system/hamobj/MoveDir.cpp";
    const std::string text = ReadFile(path);
    ASSERT_FALSE(text.empty()) << "could not read " << path;

    const ArmVerdict v = ClassifyNotActiveArm(text);
    ASSERT_NE(v, ArmVerdict::kNoArm)
        << "PostUpdateFilters' `if (!active) {` arm was not found in " << path
        << " -- the code moved and this pin needs re-aiming, not deleting.";
    EXPECT_EQ(v, ArmVerdict::kGuarded)
        << "MoveDir::PostUpdateFilters dereferences `feedback` in the arm that "
           "`!active` reaches with feedback == nullptr, with no HX_NATIVE null "
           "guard. That is a SIGSEGV on the host; the 360 absorbed it in its "
           "mapped zero page.";
}
