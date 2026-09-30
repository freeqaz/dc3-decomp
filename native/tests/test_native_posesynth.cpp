// Native pose-synthesis lane (branch native-posesynth, 2026-09-30).
//
// scripts/synthetic_kinect.py now PERFORMS: it replays the choreography /
// fatality pose the game asks for as live skeleton frames, so the decompiled
// scoring rates moves natively for the first time.  Each test here pins a
// divergence that only became reachable once a native player could score.
//
// HamUserPadNumTest: a perform-route run that scored 1.66M points crashed on
// perform_endgame_screen: MetagameRank::GetNextDeferredPoints ->
// ComputeRankNumber -> AwardForRankUp -> HamProfile::GetHamUser ->
// HamUserMgr::GetUserFromPad -> HamUser::GetPadNum, SIGSEGV at 0x88 --
// TheSkeletonIdentifier is null on native (ShellInput::Init's HX_NATIVE arm
// never builds one; the image always has one from boot).  A rank-up needs
// points, so no native run had reached it before.

#include <gtest/gtest.h>

#include "game/HamUser.h"
#define private public
#include "meta_ham/SkeletonIdentifier.h"
#undef private

namespace {

TEST(HamUserPadNumTest, NoSkeletonIdentifierMeansNoEnrolledPad) {
    // The image's HamUser::GetPadNum reads the enrolled player's pad from
    // TheSkeletonIdentifier (SkeletonIdentifier::UpdateEnrolledPlayers fills
    // it from NuiIdentityGetEnrollmentInformation; a player with no NUI
    // enrollment gets mPadNum = -1, and GetPadNum returns -1).  Native has no
    // identifier at all -- the same "nobody enrolled" state -- so the answer
    // must be -1, not a null dereference.
    SkeletonIdentifier *saved = TheSkeletonIdentifier;
    TheSkeletonIdentifier = nullptr;
    HamUser *user = HamUser::NewHamUser(0);
    ASSERT_NE(user, nullptr);
    EXPECT_EQ(user->GetPadNum(), -1);
    EXPECT_FALSE(user->CanSaveData());
    delete user;
    TheSkeletonIdentifier = saved;
}

TEST(HamUserPadNumTest, EnrolledPadStillComesFromTheIdentifier) {
    // Control: with an identifier present the image's path is untouched --
    // an enrolled pad below 4 is returned, 4 and up reads as none.
    SkeletonIdentifier *saved = TheSkeletonIdentifier;
    {
        SkeletonIdentifier ident; // ctor sets TheSkeletonIdentifier = this
        ASSERT_EQ(TheSkeletonIdentifier, &ident);
        HamUser *user = HamUser::NewHamUser(1);
        ASSERT_NE(user, nullptr);
        ident.mEnrolledPlayers[1].mPadNum = 2;
        EXPECT_EQ(user->GetPadNum(), 2);
        ident.mEnrolledPlayers[1].mPadNum = 5;
        EXPECT_EQ(user->GetPadNum(), -1);
        delete user;
    }
    TheSkeletonIdentifier = saved;
}

} // namespace
