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

#include "utl/MemMgr.h"

#include <csignal>
#include <cstring>
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

// ---------------------------------------------------------------------------
// 3. The rest of the class: the sibling sites, pinned the same way.
// ---------------------------------------------------------------------------
//
// The MoveDir detector above is shaped around that one function's `if (!active)`
// arm. The siblings do not share a single syntactic shape, so they get a
// generic detector instead: inside the HX_NATIVE-visible text, between a named
// ANCHOR and the first DEREF after it, at least one of the site's GUARD strings
// must appear.
//
// That detector is weak on its own -- it would accept a guard that happened to
// sit anywhere in the window. Both of its failure directions are therefore
// exercised per site, against the REAL file rather than a hand-written copy:
//
//   * delete the guard string from the real text -> must read kUnguarded.
//     (If it still reads guarded, the pin is passing on something other than
//     the guard, and would keep passing after the guard was deleted for real.)
//   * flip every `#ifdef HX_NATIVE` in the real text to `#ifndef HX_NATIVE`
//     -> must read kUnguarded. This is the preprocessor control: a blind grep
//     calls a backwards fix "guarded", which is wrong in the direction that
//     ships a crash.

namespace {

enum class SiteVerdict { kNoAnchor, kNoDeref, kGuarded, kUnguarded };

struct GuardSite {
    const char *label;
    const char *relPath; // relative to DC3_DECOMP_SRC_DIR
    const char *anchor;  // where to start looking
    const char *deref;   // the access that must be guarded
    const char *guard;   // the test that must precede it
    const char *why;     // failure message
};

// Drop `//` comments (respecting string and char literals). Without this the
// detector reads the guard's OWN explanatory comment: every guard added for this
// class quotes the expression it is guarding, so `text.find(deref)` lands in the
// comment ABOVE the guard and the site reports unguarded. Measured, not
// theorised -- UI.cpp and FitnessGoalJobs.cpp both failed exactly that way
// before this was added.
std::string StripLineComments(const std::string &text) {
    std::string out;
    out.reserve(text.size());
    bool inString = false, inChar = false, escaped = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (escaped) {
            escaped = false;
            out += c;
            continue;
        }
        if (c == '\\' && (inString || inChar)) {
            escaped = true;
            out += c;
            continue;
        }
        if (c == '"' && !inChar)
            inString = !inString;
        else if (c == '\'' && !inString)
            inChar = !inChar;
        else if (c == '/' && !inString && !inChar && i + 1 < text.size() && text[i + 1] == '/') {
            while (i < text.size() && text[i] != '\n')
                ++i;
            if (i < text.size())
                out += '\n';
            continue;
        }
        out += c;
    }
    return out;
}

SiteVerdict ClassifySite(const std::string &sourceText, const GuardSite &site) {
    const std::string text = StripLineComments(KeepHxNativeArms(sourceText));
    const size_t anchor = text.find(site.anchor);
    if (anchor == std::string::npos)
        return SiteVerdict::kNoAnchor;
    const size_t deref = text.find(site.deref, anchor);
    if (deref == std::string::npos)
        return SiteVerdict::kNoDeref;
    const std::string window = text.substr(anchor, deref - anchor);
    return window.find(site.guard) != std::string::npos ? SiteVerdict::kGuarded
                                                        : SiteVerdict::kUnguarded;
}

// Delete the site's guard so the detector has something to catch. Searching
// from the ANCHOR, not from the start of the file: `if (mProfile) {` occurs
// earlier in MetagameRank.cpp for an unrelated reason, and deleting THAT one
// left the real guard in place, so the control passed while proving nothing.
std::string RemoveGuardAfterAnchor(const std::string &text, const GuardSite &site) {
    size_t from = text.find(site.anchor);
    if (from == std::string::npos)
        from = 0;
    const size_t at = text.find(site.guard, from);
    if (at == std::string::npos)
        return text;
    return text.substr(0, at) + text.substr(at + std::strlen(site.guard));
}

std::string FlipHxNativeArms(const std::string &text) {
    std::string out;
    const std::string from = "#ifdef HX_NATIVE";
    const std::string to = "#ifndef HX_NATIVE";
    size_t pos = 0;
    for (;;) {
        const size_t at = text.find(from, pos);
        if (at == std::string::npos) {
            out += text.substr(pos);
            return out;
        }
        out += text.substr(pos, at - pos);
        out += to;
        pos = at + from.size();
    }
}

// Every site is a pointer that some test in the same function proves nullable,
// dereferenced on a path that skips that test. The console absorbs the access
// in its mapped, zeroed guest page 0; the host has no page 0.
const GuardSite kSites[] = {
    {"UI.cpp FailAppendCallback", "/system/ui/UI.cpp",
     "void FailAppendCallback(FixedString &str) {", "TheUI->TransitionScreen()",
     "if (!TheUI) {",
     "`(TheUI && TheUI->CurrentScreen()) || TheUI->TransitionScreen()` evaluates "
     "its right operand precisely when TheUI is null. This one runs INSIDE crash "
     "reporting, so faulting here destroys the report you needed."},

    {"Game.cpp Game::IsLoaded", "/lazer/game/Game.cpp", "bool Game::IsLoaded() {",
     "if (!mMaster->IsLoaded()) {", "if (!mMaster) {",
     "`(int)mMaster && !mMaster->IsLoaded()` short-circuits to false rather than "
     "returning, so the mLoadState==0 and ==2 arms below run with mMaster null."},

    {"GameMode.cpp IsInLoaderMode", "/lazer/game/GameMode.cpp",
     "bool IsInLoaderMode(const Symbol &sym) {",
     "if (TheGameMode->InMode(\"campaign\", true)) {", "if (!TheGameMode) {",
     "`TheGameMode && TheGameMode->InMode(sym, true)` proves TheGameMode nullable; "
     "the mind_control arm then calls through it unguarded."},

    {"MetagameRank.cpp AwardForRankUp", "/lazer/meta_ham/MetagameRank.cpp",
     "void MetagameRank::AwardForRankUp(int i1) {", "mProfile->UnlockContent(*sit);",
     "if (mProfile) {",
     "`mProfile && mProfile->GetHamUser()` proves mProfile nullable ten lines above "
     "this loop, which calls through it regardless."},

    {"MoveMgr.cpp InsertMoveInSong", "/system/hamobj/MoveMgr.cpp",
     "void MoveMgr::InsertMoveInSong(", "anim = TheHamDirector->SongAnim(player);",
     "if (!anim && TheHamDirector) {",
     "Native-added code: the merge_moves condition tests `TheHamDirector &&`, then "
     "the SongAnim fallback called straight through it."},

    {"ShellInput.cpp ShellInput::Poll", "/lazer/meta_ham/ShellInput.cpp",
     "OverlayPanel *panel = TheHamUI.GetOverlayPanel();", "mHandsUpGestureFilter->Clear();",
     "if (mHandsUpGestureFilter) {",
     "mHandsUpGestureFilter is `&&`-tested immediately above and immediately below "
     "this Clear(), which is guarded only by `panel` -- a different pointer."},

    {"FitnessGoalJobs.cpp GetFitnessGoal", "/lazer/net_ham/FitnessGoalJobs.cpp",
     "void GetFitnessGoalJob::GetFitnessGoal(HamProfile *profile) {",
     "profile->SetFitnessGoal(", "if (!profile) {",
     "`if (profile)` proves it nullable, then both exits call "
     "profile->SetFitnessGoal() unguarded."},

    {"HamDirector.cpp PlayNextShot", "/system/hamobj/HamDirector.cpp",
     "void HamDirector::PlayNextShot() {",
     "world->GetCameraManager()->ForceCameraShot(curShot, false);",
     "if (world && world->GetCameraManager()) {",
     "`world` is assigned nullptr outright when mMerger is null, twenty lines above "
     "this call."},

    {"FreestyleMoveRecorder.cpp UpdateRecordingAttempt",
     "/system/hamobj/FreestyleMoveRecorder.cpp",
     "void FreestyleMoveRecorder::UpdateRecordingAttempt(", "skeleton.Set(*skeleton);",
     "if (skeleton == nullptr) {",
     "GetScore(int,...) leaves skeletonToScore null when the player index is negative "
     "and there is no live skeleton, and passes it straight here."},

    // ---- dc3 task #143: the diagnostic ITSELF was the guard ---------------
    //
    // These four differ from the nine above in what proves the pointer
    // nullable: not a `&&` conjunction, but a MILO_ASSERT / MILO_FAIL sitting
    // directly over the access. On the 360 those stop the title, so the access
    // is unreachable. Debug::Fail on native prints to stderr and RETURNS
    // (src/system/os/Debug.cpp), so the access runs with exactly the value the
    // diagnostic was written to exclude. Same page-0 memory-map story for the
    // ones that are stores or non-virtual reads; the Lit_NG one is a VIRTUAL
    // call, which faults on both hosts once it gets a vtable pointer of 0.

    {"FlowRun.cpp ResolveTarget", "/system/flow/FlowRun.cpp",
     "void FlowRun::ResolveTarget() {", "targetDir->Find<Flow>(",
     "if (targetDir == nullptr) {",
     "MILO_ASSERT(targetDir, 0x72) is the only thing between `targetDir = "
     "ownerFlow->Dir()` (which returns mDir, freely null) and "
     "ObjectDir::Find<Flow> -- a non-virtual template that walks mHashTable "
     "through a null `this`."},

    {"Lit_NG.cpp NgLight::RenderShadows", "/system/rndobj/Lit_NG.cpp",
     "void NgLight::RenderShadows(", "mShadowRT->MakeDrawTarget();",
     "if (!mShadowRT || !unk188) {",
     "MILO_ASSERT(mShadowRT && !shadowCasters.empty(), 0x112) then "
     "mShadowRT->MakeDrawTarget(), which is virtual -- a vtable load from "
     "address 0. CreateShadowTex() returns null whenever Hmx::Object::New<RndTex> "
     "hits its own non-fatal MILO_FAIL."},

    {"Object.cpp RemoveFromDir", "/system/obj/Object.cpp",
     "void Hmx::Object::RemoveFromDir() {", "entry->obj = nullptr;", "return;",
     "MILO_FAIL(\"No entry for %s in %s\") then `entry->obj = nullptr`. Null "
     "entry stores through page 0; a non-null entry owned by ANOTHER object is "
     "memory-safe and silently unregisters that live object from its dir -- see "
     "ObjectDirDuplicateName.DestroyingTheFirstDoesNotUnregisterTheSecond."},

    {"Font.cpp UpdateChars", "/system/rndobj/Font.cpp", "void RndFont::UpdateChars() {",
     "mMaterialOffsets[pageIdx].x = mCellSize.x / (float)bmap->Width();",
     "if (!bmap) {",
     "`bmap` is null-tested at its first use and then re-fetched from "
     "BitmapLocker::LoadPage() -- which leaves mBitmapPtr null for a page with "
     "no valid texture -- without being re-tested."},
};

std::string SitePath(const GuardSite &site) {
    return std::string(DC3_DECOMP_SRC_DIR) + site.relPath;
}

} // namespace

class NullThisGuardSite : public ::testing::TestWithParam<GuardSite> {};

TEST_P(NullThisGuardSite, DetectorSeesTheBugWhenTheGuardIsDeleted) {
    const GuardSite &site = GetParam();
    const std::string text = ReadFile(SitePath(site));
    ASSERT_FALSE(text.empty()) << "could not read " << SitePath(site);
    const std::string sabotaged = RemoveGuardAfterAnchor(text, site);
    ASSERT_NE(sabotaged, text) << "the guard string `" << site.guard
                               << "` is not in the file at all -- this control is vacuous";
    EXPECT_EQ(ClassifySite(sabotaged, site), SiteVerdict::kUnguarded)
        << "With the guard deleted the detector still calls " << site.label
        << " guarded, so it is passing on something other than the guard.";
}

TEST_P(NullThisGuardSite, DetectorIsNotFooledByAGuardInTheNonNativeArm) {
    const GuardSite &site = GetParam();
    const std::string text = ReadFile(SitePath(site));
    ASSERT_FALSE(text.empty()) << "could not read " << SitePath(site);
    // Every guard in this table lives in an `#ifdef HX_NATIVE` arm. Flipping
    // those to `#ifndef` puts each one where the native build cannot see it --
    // the "backwards fix" that a preprocessor-blind grep would accept.
    const std::string flipped = FlipHxNativeArms(text);
    ASSERT_NE(flipped, text) << "no `#ifdef HX_NATIVE` in " << SitePath(site)
                             << " -- this control is vacuous";
    const SiteVerdict v = ClassifySite(flipped, site);
    EXPECT_NE(v, SiteVerdict::kGuarded)
        << "A guard the native build cannot reach is not a guard, but the "
           "detector accepted it for "
        << site.label;
}

TEST_P(NullThisGuardSite, SiteIsGuarded) {
    const GuardSite &site = GetParam();
    const std::string text = ReadFile(SitePath(site));
    ASSERT_FALSE(text.empty()) << "could not read " << SitePath(site);

    const SiteVerdict v = ClassifySite(text, site);
    ASSERT_NE(v, SiteVerdict::kNoAnchor)
        << "anchor `" << site.anchor << "` not found in " << SitePath(site)
        << " -- the code moved and this pin needs re-aiming, not deleting.";
    ASSERT_NE(v, SiteVerdict::kNoDeref)
        << "deref `" << site.deref << "` not found after the anchor in "
        << SitePath(site) << " -- re-aim the pin.";
    EXPECT_EQ(v, SiteVerdict::kGuarded) << site.label << ": " << site.why;
}

INSTANTIATE_TEST_SUITE_P(
    NullThisSiblings,
    NullThisGuardSite,
    ::testing::ValuesIn(kSites),
    [](const ::testing::TestParamInfo<GuardSite> &info) {
        std::string name(info.param.label);
        for (char &c : name)
            if (!isalnum(static_cast<unsigned char>(c)))
                c = '_';
        return name;
    }
);


// ---------------------------------------------------------------------------
// 4. The one site in dc3 task #143 that is NOT guarded, and why.
// ---------------------------------------------------------------------------
//
// MemHeap::InsertFreeBlock opens with
//
//     MILO_ASSERT((iBlock != iPrevBlock) && (iBlock != iNextBlock), 0x68);
//     iBlock->mSizeWords = size;
//     iBlock->mNextBlock = iNextBlock;      // self-link if iBlock == iNextBlock
//     ...
//     if (iPrevBlock) iPrevBlock->mNextBlock = iBlock;   // ditto
//
// and a self-link makes every later walk of mFreeBlockChain spin forever. That
// fall-through IS corrupting -- but it is unreachable on native, so no guard
// was added. Adding one would have had to choose between returning (leaking the
// block out of the chain) and continuing (the corruption), and it would have
// been dead code either way.
//
// The reason it is unreachable is a single line in src/system/os/System.cpp:
// the native arm of SystemInit skips MemInit(), so gNumHeaps stays 0 and no
// MemHeap is ever Init()ed. MemAlloc/MemFree/MemOrPoolAllocSTL are #ifdef
// HX_NATIVE'd straight to malloc/free, and the one remaining caller that can
// still reach MemHeap -- MemTruncate, live via CharClip.cpp -- loops
// `for (i = 0; i < gNumHeaps; i++) gHeaps[i].Truncate(...)`, which executes
// zero times. Measured, not only argued: an instrumented build that printed on
// the first InsertFreeBlock call produced no output across the whole ctest
// suite, a bare milo-tests run, and a 60k-frame headless engine run that
// reached gameplay.
//
// That reachability argument has exactly one load-bearing fact, so pin it. If
// somebody re-enables MemInit on native, this fails and points at the analysis
// instead of letting a heap corruption go looking for a reader.
TEST(MemHeapInertOnNative, NoHeapsAreInitialisedSoInsertFreeBlockIsUnreachable) {
    EXPECT_EQ(MemNumHeaps(), 0)
        << "A MemHeap now exists on native, so MemHeap::Alloc/Free/Truncate can "
           "run and MemHeap::InsertFreeBlock's non-fatal MILO_ASSERT (self-link "
           "-> infinite mFreeBlockChain walk) is reachable again. Guard the site "
           "before raising this number; see dc3 task #143.";
}
