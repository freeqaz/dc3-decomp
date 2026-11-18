// test_hamribbon_long_run.cpp -- HamRibbon::UpdateChase returns, and still
// draws a trail, once the clock is too large for its time step to register.
//
// The freeze (a Mac left running, 2026-11-17): after about 18.5 hours the main
// thread never left the `while (now > nextTime)` loop of
// HamRibbon::UpdateChase, reached through CursorPanel::Poll. That loop walks
// the newest chase key's time forward by `step = mDecay / mNumSegments` until
// it passes the clock. Both are floats. Once `frame + step` rounds back to
// `frame`, nextTime stops moving and the loop has no exit.
//
// The numbers. cursor.milo holds two ribbons, transfer_blue_laser.rbn and
// transfer_red_laser.rbn, both decay 0.3, num_segments 40: step = 0.0075 s.
// Float spacing is 1/128 s from 65,536 s and 1/64 s from 131,072 s. A sum
// rounds back when the step is under half the spacing, so 0.0075 still
// registers at 65,536 (half spacing 0.0039) and stops at 131,072 (half
// spacing 0.0078). The limit for this ribbon is 2^17 s = 36.4 hours.
// (The native clock got there in 18.5 hours of real time: on LP64
// Timer::CyclesToMs counts the high word of the cycle count twice, so the
// clock gains 4,295 s every 4,295 s.)
//
// The Xbox image has the same loop (fadds at 0x824C7C04 / 0x824C7C70, bgt at
// 0x824C7C78), so the repair is native only: a step that does not register is
// replaced by the spacing of the clock at that key, the smallest step that
// does. The trail keeps its length in seconds and follows the target; it has
// fewer keys, as many as the clock can tell apart.
//
// What is pinned:
//  - CursorRibbonParametersAreTheShippedOnes: the constants below are what
//    HamRibbon::Load reads out of the shipped cursor.milo;
//  - PollReturnsAndTrailsPastTheStepLimit: a real ribbon with those
//    parameters, polled across 2^17 s and at 2^18 .. 2^21 s, with a still and
//    a moving target. Every Poll returns, the key count stays bounded, the
//    newest key is at the target, and the keys span the decay window. The
//    unrepaired loop never returns, so the body runs in a death-test child
//    under alarm(): a hang is SIGALRM after kHangSeconds, not a hung suite;
//  - BelowTheStepLimitMatchesTheOriginalLoop: up to 2^17 s the ribbon's keys
//    are bit for bit those of the Xbox path's loop, written out here. This
//    one passes with or without the repair; it fails if the repair changes
//    anything while the step still registers.

#include "test_helpers.h"

#include "hamobj/HamRibbon.h"
#include "math/Key.h"
#include "math/Mtx.h"
#include "math/Vec.h"
#include "obj/Dir.h"
#include "obj/DirLoader.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "rndobj/Trans.h"
#include "utl/FilePath.h"

#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>
#include <unistd.h>
#include <vector>

namespace {

// transfer_blue_laser.rbn and transfer_red_laser.rbn in ui/gen/cursor.milo_xbox
// (the same values in both; CursorRibbonParametersAreTheShippedOnes reads them).
const int kCursorNumSides = 3;
const float kCursorWidth = 92.99839782714844f; // 0x42B9FF2E
const int kCursorNumSegments = 40;
const float kCursorDecay = 0.3f;
const bool kCursorTaper = true;

const float kFloatInf = std::numeric_limits<float>::infinity();

// The distance from `x` to the next float above it.
float SpacingAt(float x) { return std::nextafter(x, kFloatInf) - x; }

// A HamRibbon whose chase keys can be read. It adds no behaviour: every
// function that runs is HamRibbon's own.
class RibbonProbe : public HamRibbon {
public:
    RibbonProbe() {}
    const Keys<Transform, Transform> &ChaseKeys() const { return mChaseKeys; }
};

// A ribbon with the cursor's parameters, set through its own property syncs,
// following `target`.
RibbonProbe *NewCursorRibbon(RndTransformable *target) {
    RibbonProbe *r = new RibbonProbe;
    r->SetProperty("num_sides", kCursorNumSides);
    r->SetProperty("width", kCursorWidth);
    r->SetProperty("num_segments", kCursorNumSegments);
    r->SetProperty("decay", kCursorDecay);
    r->SetProperty("taper", (int)kCursorTaper);
    r->SetProperty("follow_a", (Hmx::Object *)target);
    return r;
}

// One frame: the engine's real-time clock reads `now`, the target is at `pos`.
void PollAt(RibbonProbe *r, RndTransformable *target, float now, const Vector3 &pos) {
    TheTaskMgr.SetTimeAndDelta(kTaskSeconds, now, 1.0f / 60.0f);
    target->SetLocalPos(pos);
    r->Poll();
}

// ---------------------------------------------------------------------------
// The death-test child
// ---------------------------------------------------------------------------

const int kHangSeconds = 5;

[[noreturn]] void Bad(int code, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    _exit(code);
}

// What the child was doing when the alarm went off, kept ready to print: a
// signal handler can do no formatting of its own.
char sHangMessage[160] = "HANG before the first Poll\n";

void OnAlarm(int) {
    (void)!write(2, sHangMessage, strlen(sHangMessage));
    _exit(14);
}

// One Poll of the child: as PollAt, with the hang message naming this Poll.
void ChildPollAt(RibbonProbe *r, RndTransformable *target, float now, const Vector3 &pos,
                 const char *phase, int frame) {
    snprintf(sHangMessage, sizeof(sHangMessage),
             "HANG: HamRibbon::Poll did not return in %d s at clock %.6f s (phase %s, "
             "frame %d)\n",
             kHangSeconds, now, phase, frame);
    PollAt(r, target, now, pos);
}

// The checks that hold after every Poll, in every phase.
void CheckKeys(const RibbonProbe *r, float now, const char *phase, int frame) {
    const Keys<Transform, Transform> &keys = r->ChaseKeys();
    if (keys.empty())
        Bad(2, "%s frame %d (clock %.4f): the ribbon has no keys", phase, frame, now);
    // Keys one decay window deep, at least half a step apart: 2n + 2 at most.
    if ((int)keys.size() > 2 * kCursorNumSegments + 2)
        Bad(3, "%s frame %d (clock %.4f): the ribbon grew to %d keys", phase, frame, now,
            (int)keys.size());
    for (size_t i = 1; i < keys.size(); i++) {
        if (!(keys[i].frame > keys[i - 1].frame))
            Bad(4, "%s frame %d (clock %.4f): key %d at %.6f is not after key %d at %.6f",
                phase, frame, now, (int)i, keys[i].frame, (int)i - 1, keys[i - 1].frame);
    }
    if (keys.back().frame > now)
        Bad(5, "%s frame %d: the newest key is at %.6f, after the clock %.6f", phase, frame,
            keys.back().frame, now);
    // The loop stops within one step of the clock: the authored step (the sum
    // rounds, by up to half the clock's spacing) or the spacing itself.
    const float reach =
        std::max(kCursorDecay / kCursorNumSegments + 0.5f * SpacingAt(now), SpacingAt(now));
    if (now - keys.back().frame > reach)
        Bad(6, "%s frame %d: the newest key is at %.6f, %.6f s behind the clock %.6f "
               "(more than one step, %.6f)",
            phase, frame, keys.back().frame, now - keys.back().frame, now, reach);
}

// Polls a cursor ribbon at 60 frames a second from one second before `limit`
// (a power of two seconds) to four seconds after it: first with the target
// still, as on the frozen machine, then with it moving in a straight line.
void RunPhase(float limit) {
    char phase[32];
    snprintf(phase, sizeof(phase), "2^%d s", (int)std::lround(std::log2(limit)));
    RndTransformable *target = Hmx::Object::New<RndTransformable>();
    RibbonProbe *r = NewCursorRibbon(target);

    const double kDt = 1.0 / 60.0;
    const double start = (double)limit - 1.0;
    int frame = 0;

    // Still target, across the limit: the incident. Nothing moves, so the
    // loop only ever advances the one key's time.
    const Vector3 rest(100.0f, 50.0f, -25.0f);
    for (; frame < 120; frame++) {
        const float now = (float)(start + frame * kDt);
        ChildPollAt(r, target, now, rest, phase, frame);
        CheckKeys(r, now, phase, frame);
        if (r->ChaseKeys().size() != 1)
            Bad(7, "%s frame %d: a still target made %d keys", phase, frame,
                (int)r->ChaseKeys().size());
        if (Distance(r->ChaseKeys().back().value.v, rest) != 0.0f)
            Bad(8, "%s frame %d: the key left the still target", phase, frame);
    }

    // Moving target: 6000 units a second, far enough each step to make a new
    // key every time. The position is a function of the float clock, so the
    // motion is exactly linear in the time the ribbon sees.
    const float kSpeed = 6000.0f;
    const float moveStart = (float)(start + frame * kDt);
    float now = moveStart;
    Vector3 pos = rest;
    for (; frame < 300; frame++) {
        now = (float)(start + frame * kDt);
        pos.Set(rest.x + kSpeed * (now - moveStart), rest.y, rest.z);
        ChildPollAt(r, target, now, pos, phase, frame);
        CheckKeys(r, now, phase, frame);
        // The newest key is where the target was at that key's time.
        const Key<Transform> &head = r->ChaseKeys().back();
        const float behind = Distance(head.value.v, pos);
        const float allowed = kSpeed * (now - head.frame) + 1.0f;
        if (behind > allowed)
            Bad(9, "%s frame %d: the newest key is %.2f units behind the target; a key "
                   "%.6f s old may be %.2f behind",
                phase, frame, behind, now - head.frame, allowed);
    }

    // After three seconds of movement the keys cover the decay window at the
    // clock's own spacing (or the authored step, where that is larger), and
    // each is where the target was at its time.
    const Keys<Transform, Transform> &keys = r->ChaseKeys();
    const float gap = std::max(kCursorDecay / kCursorNumSegments, SpacingAt(now));
    const int expected = (int)(kCursorDecay / gap);
    if ((int)keys.size() < std::max(1, expected - 1) || (int)keys.size() > expected + 2)
        Bad(10, "%s: %d keys after moving for 3 s; %.3f s of trail at %.6f s a key is "
                "about %d",
            phase, (int)keys.size(), kCursorDecay, gap, expected);
    if (keys.front().frame < now - kCursorDecay - gap)
        Bad(11, "%s: the oldest key is %.4f s old, the trail is %.3f s", phase,
            now - keys.front().frame, kCursorDecay);
    if (expected >= 2 && keys.back().frame - keys.front().frame < kCursorDecay - 2.0f * gap)
        Bad(12, "%s: the keys span %.4f s of a %.3f s trail", phase,
            keys.back().frame - keys.front().frame, kCursorDecay);
    for (size_t i = 0; i < keys.size(); i++) {
        const Vector3 was(rest.x + kSpeed * (keys[i].frame - moveStart), rest.y, rest.z);
        if (Distance(keys[i].value.v, was) > 1.0f)
            Bad(13, "%s: key %d (%.4f s old) is %.2f units off the target's path", phase,
                (int)i, now - keys[i].frame, Distance(keys[i].value.v, was));
    }

    delete r;
    delete target;
}

void PastTheStepLimitChild() {
    signal(SIGALRM, OnAlarm);
    alarm(kHangSeconds);
    // 2^17 s is where this ribbon's step stops registering; each later power
    // of two halves what the clock can tell apart again. 2^21 s is 24 days.
    for (int exp = 17; exp <= 21; exp++)
        RunPhase(std::ldexp(1.0f, exp));
    _exit(0);
}

// ---------------------------------------------------------------------------
// The Xbox path's loop, for the bit-for-bit comparison
// ---------------------------------------------------------------------------

struct OriginalKey {
    float frame;
    Vector3 v;
};

// HamRibbon::UpdateChase's key bookkeeping as the Xbox path has it (the part
// that decides key times and positions; the orientation pass reads them and
// writes neither).
struct OriginalChase {
    std::vector<OriginalKey> keys;
    float lastTime = -1.0f;
    int merged = 0, pushed = 0;

    void Poll(float now, const Vector3 &followed) {
        if (now < lastTime)
            keys.clear();
        unsigned int removeCount = 0;
        for (unsigned int i = 0; i < keys.size() && keys[i].frame < now - kCursorDecay; i++)
            removeCount++;
        keys.erase(keys.begin(), keys.begin() + removeCount);
        if (keys.size() == 0) {
            keys.push_back(OriginalKey{ now, followed });
        } else {
            float step = kCursorDecay / kCursorNumSegments;
            float minDistSq = kCursorWidth * kCursorWidth * 0.125f;
            float nextTime = keys.back().frame + step;
            while (now > nextTime) {
                OriginalKey key;
                key.frame = keys.back().frame + step;
                Interp(keys.back().v, followed, step / (now - keys.back().frame), key.v);
                Vector3 delta;
                Subtract(keys.back().v, key.v, delta);
                if (LengthSquared(delta) < minDistSq) {
                    keys.back().frame = key.frame;
                    merged++;
                } else {
                    keys.push_back(key);
                    pushed++;
                }
                nextTime = keys.back().frame + step;
            }
        }
        lastTime = now;
    }
};

bool SameBits(float a, float b) { return memcmp(&a, &b, sizeof(float)) == 0; }

class HamRibbonLongRun : public EngineTestFixture {
protected:
    void SetUp() override {
        if (IsSkipped())
            return;
        mSavedSeconds = TheTaskMgr.Seconds(TaskMgr::kRealTime);
        mSavedDelta = TheTaskMgr.DeltaSeconds();
    }
    void TearDown() override {
        if (IsSkipped())
            return;
        TheTaskMgr.SetTimeAndDelta(kTaskSeconds, mSavedSeconds, mSavedDelta);
    }
    float mSavedSeconds = 0.0f;
    float mSavedDelta = 0.0f;
};

} // namespace

TEST_F(HamRibbonLongRun, CursorRibbonParametersAreTheShippedOnes) {
    if (IsSkipped())
        return;
    ObjectDir *dir =
        DirLoader::LoadObjects(FilePath("ui/gen/cursor.milo_xbox"), nullptr, nullptr);
    ASSERT_NE(dir, nullptr) << "ui/gen/cursor.milo_xbox did not load";
    int ribbons = 0;
    for (ObjDirItr<HamRibbon> it(dir, false); it != nullptr; ++it) {
        SCOPED_TRACE(it->Name());
        ribbons++;
        EXPECT_EQ(it->Property("num_sides")->Int(), kCursorNumSides);
        EXPECT_TRUE(SameBits(it->Property("width")->Float(), kCursorWidth))
            << it->Property("width")->Float();
        EXPECT_EQ(it->Property("num_segments")->Int(), kCursorNumSegments);
        EXPECT_TRUE(SameBits(it->Property("decay")->Float(), kCursorDecay))
            << it->Property("decay")->Float();
        EXPECT_EQ(it->Property("taper")->Int() != 0, kCursorTaper);
        EXPECT_NE(it->Property("follow_a")->GetObj(), nullptr);
        EXPECT_EQ(it->Property("follow_b")->GetObj(), nullptr);
    }
    // The two the frozen stack reached: polled by the cursor dir itself.
    EXPECT_EQ(ribbons, 2);
    EXPECT_NE(dir->Find<HamRibbon>("transfer_blue_laser.rbn", false), nullptr);
    EXPECT_NE(dir->Find<HamRibbon>("transfer_red_laser.rbn", false), nullptr);
    delete dir;

    // Where that step stops registering: not at 2^16 s, at 2^17 s.
    const float step = kCursorDecay / kCursorNumSegments;
    EXPECT_FLOAT_EQ(step, 0.0075f);
    EXPECT_GT(65536.0f + step, 65536.0f) << "the step still registers at 2^16 s";
    EXPECT_GT(std::nextafter(131072.0f, 0.0f) + step, std::nextafter(131072.0f, 0.0f))
        << "the step still registers just under 2^17 s";
    EXPECT_EQ(131072.0f + step, 131072.0f) << "the step is lost from 2^17 s";
}

TEST_F(HamRibbonLongRun, PollReturnsAndTrailsPastTheStepLimit) {
    if (IsSkipped())
        return;
    GTEST_FLAG_SET(death_test_style, "threadsafe"); // see test_object_lifetime.cpp
    ASSERT_EXIT(PastTheStepLimitChild(), ::testing::ExitedWithCode(0), "")
        << "exit 14 = a Poll did not return within " << kHangSeconds
        << " s (the UpdateChase loop cannot pass the clock); any other code is "
           "one of the checks, named on the child's stderr above";
}

TEST_F(HamRibbonLongRun, BelowTheStepLimitMatchesTheOriginalLoop) {
    if (IsSkipped())
        return;
    // Windows of clock time below 2^17 s, each run on a fresh ribbon: small
    // values, across 2^15 and 2^16 s (where the spacing doubles and nothing
    // else happens), and up to a second short of 2^17.
    const double kWindows[] = { 0.0, 1000.0, 4096.0, 32767.0, 50000.0,
                                65535.0, 100000.0, 131068.0 };
    const double kDt = 1.0 / 60.0;
    const int kFrames = 180;
    int merged = 0, pushed = 0, compared = 0;
    for (double start : kWindows) {
        SCOPED_TRACE(testing::Message() << "window from " << start << " s");
        RndTransformable *target = Hmx::Object::New<RndTransformable>();
        RibbonProbe *r = NewCursorRibbon(target);
        OriginalChase original;
        for (int frame = 0; frame < kFrames; frame++) {
            const float now = (float)(start + frame * kDt);
            // A circle whose speed swings between rest and 6000 units a
            // second: slow frames merge into the newest key, fast ones push.
            const double phase = frame * kDt;
            const double angle = 6.0 * (phase - std::sin(2.0 * phase) / 2.0);
            Vector3 pos((float)(500.0 * std::cos(angle)), (float)(30.0 * phase),
                        (float)(500.0 * std::sin(angle)));
            PollAt(r, target, now, pos);
            original.Poll(now, pos);

            const Keys<Transform, Transform> &keys = r->ChaseKeys();
            ASSERT_EQ(keys.size(), original.keys.size()) << "frame " << frame;
            for (size_t i = 0; i < keys.size(); i++) {
                const OriginalKey &want = original.keys[i];
                const Key<Transform> &got = keys[i];
                ASSERT_TRUE(SameBits(got.frame, want.frame))
                    << "frame " << frame << " key " << i << ": time " << got.frame
                    << ", the original loop has " << want.frame;
                ASSERT_TRUE(SameBits(got.value.v.x, want.v.x)
                            && SameBits(got.value.v.y, want.v.y)
                            && SameBits(got.value.v.z, want.v.z))
                    << "frame " << frame << " key " << i << ": position differs";
                compared++;
            }
        }
        merged += original.merged;
        pushed += original.pushed;
        delete r;
        delete target;
    }
    // The comparison is only worth something if both branches of the loop ran.
    EXPECT_GT(merged, 500) << "the loop hardly ever merged into the newest key";
    EXPECT_GT(pushed, 500) << "the loop hardly ever pushed a key";
    EXPECT_GT(compared, 10000);
    RecordProperty("merged", merged);
    RecordProperty("pushed", pushed);
    RecordProperty("keys_compared", compared);
}
