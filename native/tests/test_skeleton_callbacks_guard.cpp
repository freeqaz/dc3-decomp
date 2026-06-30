// SkeletonUpdateHandle::Callbacks() null-instance guard regression test.
//
// On native, SkeletonUpdate::sInstance is never created (CreateInstance only
// runs in the Xbox LiveCameraInput::PreInit path, which is compiled out under
// HX_NATIVE), so SkeletonUpdate::InstanceHandle() returns a handle wrapping a
// null mInst. Every sibling accessor (GetCameraInput / SetCameraInput /
// HasCallback / AddCallback / RemoveCallback / PostUpdate / History) guards
// `if (!mInst) return ...;` under HX_NATIVE, but Callbacks() historically did a
// bare `return mInst->mCallbacks;` -> dereference of NULL (read at NULL+0x94).
//
// This pins the guarded behavior: with no instance, Callbacks() returns an
// empty list instead of crashing. Pre-fix this test SIGSEGVs; post-fix it
// passes deterministically with no GPU, camera, or game state.

#include <gtest/gtest.h>

#include "gesture/SkeletonUpdate.h"

namespace {

TEST(SkeletonUpdateHandle, CallbacksWithNoInstanceReturnsEmpty) {
    // No CreateInstance() runs on native, so this must hold. Asserting it (rather
    // than assuming) keeps the test honest if some other test ever stands one up.
    ASSERT_FALSE(SkeletonUpdate::HasInstance());

    // Pre-fix: dereferences NULL (mInst->mCallbacks). Post-fix: returns the
    // static empty fallback. The handle temporary lives for the full expression;
    // the returned reference aliases a static vector that outlives it.
    std::vector<SkeletonCallback *> &cbs = SkeletonUpdate::InstanceHandle().Callbacks();
    EXPECT_TRUE(cbs.empty());
}

} // namespace
