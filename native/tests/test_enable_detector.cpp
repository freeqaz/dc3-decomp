// MoveAsyncDetector::EnableDetector activation regression test
// (Wave-3 Lane C, roadmap N.4).
//
// EnableDetector activates a previously-inactive MoveDetector. The original
// Xbox 360 binary clears the float mLastDetectFracs[0/1] with an INTEGER store
// (`*(int*)&f = 0`), the same idiom MoveDetector::Reset() uses. The decompiled
// source was `= 0.0f`, which is bit-identical but lowered differently (the int
// store is what the target emits — PPC 93.5% -> 97.3% after the fix).
//
// This test pins the BEHAVIOR of the activation transition (not the lowering):
// after EnableDetector on an inactive detector,
//   * mActive becomes true,
//   * mDetectFrameOffset / mLastDetectFrameIdx are reset to -1,
//   * mLastDetectFracs[0/1] are cleared to *integer* zero (all-zero bits), which
//     this test checks via bit-cast rather than a float-epsilon compare — the
//     point of the `*(int*)&f = 0` idiom is exact zero bits, and a previously
//     poisoned (NaN) value must be wiped, not merely "near 0".
// The detector is also inserted into the active set.
//
// EnableDetector reaches only POD fields of MoveDetector on the activation path
// (it never touches the DancerFrame/DetectFrame vectors), so the test drives the
// REAL EnableDetector code against a controlled detector without standing up the
// full song/HamMove game state. Private/protected access via the standard
// test-only macro hack (see test_movegraph.cpp).

#include <gtest/gtest.h>

#include <cstring>
#include <new>

#define private public
#define protected public
#include "hamobj/MoveDetector.h"  // declares both MoveDetector and MoveAsyncDetector
#undef private
#undef protected

namespace {

// Reinterpret a float's bits as a uint32 (to assert *integer* zero, not a
// float-epsilon "near zero").
uint32_t FloatBits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

// Allocate a MoveDetector without running its game-state-heavy constructor.
// The EnableDetector activation path only reads/writes POD fields, so we zero
// the storage (valid empty-vector state on libstdc++) and set just the fields
// the path observes: mMove (for FindDetector matching) and a *poisoned*
// mLastDetectFracs so we can prove EnableDetector actually clears it.
MoveDetector *MakeInactiveDetector(const HamMove *move) {
    void *mem = ::operator new(sizeof(MoveDetector));
    std::memset(mem, 0, sizeof(MoveDetector));
    MoveDetector *d = static_cast<MoveDetector *>(mem);
    d->mMove = move;
    d->mActive = false;
    d->mLastDetectFrameIdx = 12345;   // non-(-1) so we can see the reset
    d->mDetectFrameOffset = 67890;    // non-(-1) so we can see the reset
    // Poison the fracs with NaN bits so a stale/garbage read is detectable and
    // can't be mistaken for a clean zero.
    uint32_t nan = 0x7FC00000u;
    std::memcpy(&d->mLastDetectFracs[0], &nan, sizeof(float));
    std::memcpy(&d->mLastDetectFracs[1], &nan, sizeof(float));
    return d;
}

void DestroyDetector(MoveDetector *d) { ::operator delete(static_cast<void *>(d)); }

// Build a MoveAsyncDetector without its heavy ctor: placement-construct only the
// three members so the std::vector / std::set are valid.
MoveAsyncDetector *MakeAsyncDetector() {
    void *mem = ::operator new(sizeof(MoveAsyncDetector));
    std::memset(mem, 0, sizeof(MoveAsyncDetector));
    MoveAsyncDetector *a = static_cast<MoveAsyncDetector *>(mem);
    a->mDir = nullptr;
    new (&a->mDetectors) std::vector<MoveDetector *>();
    new (&a->mActiveDetectors) std::set<MoveDetector *>();
    return a;
}

void DestroyAsyncDetector(MoveAsyncDetector *a) {
    a->mDetectors.~vector();
    a->mActiveDetectors.~set();
    ::operator delete(static_cast<void *>(a));
}

}  // namespace

// Activating an inactive detector flips mActive, resets the frame offsets, and
// wipes the (poisoned) mLastDetectFracs to integer zero.
TEST(EnableDetector, ActivationClearsFracsToIntegerZero) {
    // A HamMove* used purely as an identity key for FindDetector's equal_range;
    // EnableDetector never dereferences it on the activation path (the detector
    // is found by pointer-compare on mMove). A non-null sentinel suffices.
    const HamMove *moveKey = reinterpret_cast<const HamMove *>(0x1000);

    MoveAsyncDetector *async = MakeAsyncDetector();
    MoveDetector *det = MakeInactiveDetector(moveKey);
    async->mDetectors.push_back(det);  // FindDetector will return this one

    // Sanity: poisoned before.
    ASSERT_EQ(FloatBits(det->mLastDetectFracs[0]), 0x7FC00000u);
    ASSERT_FALSE(det->mActive);

    async->EnableDetector(const_cast<HamMove *>(moveKey));

    EXPECT_TRUE(det->mActive) << "EnableDetector must activate the detector";
    EXPECT_EQ(det->mLastDetectFrameIdx, -1);
    EXPECT_EQ(det->mDetectFrameOffset, -1);
    // The crux: exact integer-zero bits, proving the poison was cleared by the
    // `*(int*)&f = 0` store (not a float-epsilon approximation).
    EXPECT_EQ(FloatBits(det->mLastDetectFracs[0]), 0u)
        << "mLastDetectFracs[0] must be integer zero, not stale/NaN";
    EXPECT_EQ(FloatBits(det->mLastDetectFracs[1]), 0u)
        << "mLastDetectFracs[1] must be integer zero, not stale/NaN";
    // And it lands in the active set.
    EXPECT_EQ(async->mActiveDetectors.count(det), 1u);

    DestroyAsyncDetector(async);
    DestroyDetector(det);
}

// An already-active detector is NOT re-cleared (EnableDetector only re-inits the
// inactive->active transition), but it is still inserted into the active set.
TEST(EnableDetector, AlreadyActiveIsNotReinitialized) {
    const HamMove *moveKey = reinterpret_cast<const HamMove *>(0x2000);

    MoveAsyncDetector *async = MakeAsyncDetector();
    MoveDetector *det = MakeInactiveDetector(moveKey);
    det->mActive = true;  // already active
    float sentinel = 0.42f;
    det->mLastDetectFracs[0] = sentinel;
    det->mLastDetectFracs[1] = sentinel;
    det->mLastDetectFrameIdx = 7;
    async->mDetectors.push_back(det);

    async->EnableDetector(const_cast<HamMove *>(moveKey));

    EXPECT_TRUE(det->mActive);
    // Unchanged because it was already active.
    EXPECT_EQ(det->mLastDetectFracs[0], sentinel);
    EXPECT_EQ(det->mLastDetectFrameIdx, 7);
    EXPECT_EQ(async->mActiveDetectors.count(det), 1u);

    DestroyAsyncDetector(async);
    DestroyDetector(det);
}
