// Who is who in front of the camera (branch np-player-identity):
// Dc3PoseIdentity::People and ::Players, native/src/platform/
// PoseIdentity_Native.h.  Both are pure, so they are driven directly here; the
// sensor-level halves (the slot map, the dropout hold) are in
// test_native_pose_socket.cpp, and the whole thing with the game on top in the
// real-time replays docs/native/POSE_INPUT.md reports.
//
// The recorded cases come from native/tests/data/pose/two_dancers_hips.txt:
// the hip centres of two dancers and a bystander, with the pose tracker's
// ids, cut from a real two-player session (a 640x480 webcam at 8-10 Hz).
//   song2  the tracker's id 4 walks from the centre dancer onto the left one
//          in three frames (9.0-9.4 s), the left dancer's id 7 is dropped,
//          and the centre dancer comes back at 12.1 s as id 8 -- later the
//          tracker calls BOTH of them 8 on alternate frames.
//   song3  the tracker's id 43 moves from the left dancer onto the right one
//          (15.8 s), and takes one frame on a bystander 0.41 m away (37.2 s).

#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "platform/PoseIdentity_Native.h"
#include "platform/PoseSeen_Native.h"
#include "platform/PoseTwin_Native.h"

#ifndef DC3_DECOMP_SRC_DIR
#error "DC3_DECOMP_SRC_DIR is the repo's src/ (native/CMakeLists.txt)"
#endif

namespace {

using Dc3PoseIdentity::Detection;
using Dc3PoseIdentity::People;
using Dc3PoseIdentity::Players;
using Dc3PoseIdentity::SlotView;

const int kWindowMs = 5000;
const float kFrame = 0.1f; // a 10 Hz camera

Detection Det(int rawId, float x, float z = 2.5f) {
    Detection d;
    d.rawId = rawId;
    d.x = x;
    d.y = 0.0f;
    d.z = z;
    return d;
}

// One camera frame through `people`; the stable ids in the order given.
std::vector<int> See(People &people, const std::vector<Detection> &dets, float dt = kFrame,
                     int windowMs = kWindowMs) {
    int ids[Dc3PoseIdentity::kMaxDetections] = { 0 };
    people.Update(dets.data(), (int)dets.size(), dt, windowMs, ids);
    return std::vector<int>(ids, ids + dets.size());
}

// ===========================================================================
// People
// ===========================================================================

TEST(PoseIdentityPeopleTest, ATrackerIdThatWalksOntoTheNeighbourDoesNotTakeHerIdentity) {
    // The song-2 failure, in small: the centre dancer (tracker id 4) and the
    // left one (7) are seen on alternate frames; then the tracker's 4 walks
    // to the left dancer's place while 7 is not reported at all.
    People people;
    for (int i = 0; i < 10; i++) {
        ASSERT_EQ(See(people, { Det(4, 0.0f) })[0], 4);
        ASSERT_EQ(See(people, { Det(7, -0.64f) })[0], 7);
    }
    EXPECT_EQ(See(people, { Det(4, -0.04f) })[0], 4);
    See(people, { Det(4, -0.24f) }); // on the way: either of them
    EXPECT_EQ(See(people, { Det(4, -0.48f) })[0], 7) << "that is where the left dancer stands";
    for (int i = 0; i < 25; i++)
        ASSERT_EQ(See(people, { Det(4, -0.60f) })[0], 7) << "frame " << i;
    // The centre dancer is found again 2.7 s later, as somebody new to the
    // tracker: she is who she was.
    std::vector<int> ids = See(people, { Det(8, 0.0f), Det(4, -0.62f) });
    EXPECT_EQ(ids[0], 4);
    EXPECT_EQ(ids[1], 7);
}

TEST(PoseIdentityPeopleTest, TwoPeopleWhoseTrackerIdsSwapKeepTheirOwn) {
    People people;
    for (int i = 0; i < 5; i++)
        See(people, { Det(11, -0.4f), Det(12, 0.4f) });
    std::vector<int> ids = See(people, { Det(12, -0.4f), Det(11, 0.4f) });
    EXPECT_EQ(ids[0], 11) << "the person on the left is still the person on the left";
    EXPECT_EQ(ids[1], 12);
}

TEST(PoseIdentityPeopleTest, OneTrackerIdForTwoBodiesOnAlternateFramesIsStillTwoPeople) {
    // Song 2 from 21 s: the tracker has one id left and MediaPipe reports the
    // two dancers, 0.3 m apart, in turn.
    People people;
    for (int i = 0; i < 5; i++)
        See(people, { Det(21, -0.30f), Det(22, -0.60f) });
    for (int i = 0; i < 20; i++) {
        ASSERT_EQ(See(people, { Det(21, -0.30f) })[0], 21) << "round " << i;
        ASSERT_EQ(See(people, { Det(21, -0.60f) })[0], 22) << "round " << i;
    }
}

TEST(PoseIdentityPeopleTest, ALostPersonFoundNearbyUnderANewTrackerIdIsHerself) {
    People people;
    for (int i = 0; i < 10; i++)
        See(people, { Det(31, 0.0f) });
    for (int i = 0; i < 20; i++) // 2 s of nobody
        See(people, {});
    EXPECT_EQ(See(people, { Det(32, 0.35f) })[0], 31);
    EXPECT_EQ(people.StableForRaw(32), 31) << "the camera stream's mask for tracker id 32 is hers";
    EXPECT_EQ(people.Reacquired(), 1);
}

TEST(PoseIdentityPeopleTest, ABodyOutOfReachIsSomebodyNew) {
    // The wrong-person control: 0.6 m from where the lost dancer stood is
    // further than a neighbour stands (0.43 m at the 5th percentile).
    People people;
    for (int i = 0; i < 10; i++)
        See(people, { Det(41, 0.0f) });
    for (int i = 0; i < 20; i++)
        See(people, {});
    EXPECT_EQ(See(people, { Det(42, 0.6f) })[0], 42);
}

TEST(PoseIdentityPeopleTest, ASecondBodyNextToASeenPersonIsNotHer) {
    People people;
    for (int i = 0; i < 10; i++)
        See(people, { Det(51, 0.0f) });
    std::vector<int> ids = See(people, { Det(51, 0.0f), Det(52, 0.3f) });
    EXPECT_EQ(ids[0], 51);
    EXPECT_EQ(ids[1], 52);
}

TEST(PoseIdentityPeopleTest, AfterTheWindowALostPersonIsForgotten) {
    People people;
    for (int i = 0; i < 10; i++)
        See(people, { Det(61, 0.0f) });
    for (int i = 0; i < 51; i++) // 5.1 s
        See(people, {});
    EXPECT_EQ(See(people, { Det(62, 0.0f) })[0], 62) << "the same spot, but too late";
    EXPECT_EQ(people.Reacquired(), 0);
}

TEST(PoseIdentityPeopleTest, AOneFrameJumpDoesNotLoseHer) {
    // Song 3 at 37.2 s: after 0.35 s unseen the dancer's detection is on the
    // bystander 0.41 m away for one frame, then back.
    People people;
    for (int i = 0; i < 10; i++)
        See(people, { Det(71, 0.37f) });
    See(people, {}, 0.35f);
    See(people, { Det(71, 0.78f) });
    EXPECT_EQ(See(people, { Det(71, 0.37f) })[0], 71);
}

TEST(PoseIdentityPeopleTest, ATrackerIdAlreadySomebodysGetsAPrivateId) {
    // The tracker's 81 slid onto B; A comes back out of reach of where she
    // was, and the tracker happens to hand her... 81 cannot be two people.
    People people;
    for (int i = 0; i < 5; i++)
        See(people, { Det(81, 0.0f), Det(82, 1.0f) });
    for (int i = 0; i < 5; i++)
        See(people, { Det(82, 1.0f) });
    std::vector<int> ids = See(people, { Det(82, 1.0f), Det(81, -1.2f) });
    EXPECT_EQ(ids[0], 82);
    EXPECT_NE(ids[1], 81) << "81 is still the person remembered at 0.0";
    EXPECT_GE(ids[1], Dc3PoseIdentity::kPrivateIdBase);
}

TEST(PoseIdentityPeopleTest, ATrackerIdMeansTheBodyItIsOnNow) {
    // The camera stream's person masks are labelled with the tracker's ids
    // (StableForRaw).  The tracker's 81 was A's and has walked onto B.
    People people;
    for (int i = 0; i < 5; i++)
        See(people, { Det(81, 0.0f), Det(82, 1.0f) });
    EXPECT_EQ(people.StableForRaw(81), 81);
    for (int i = 0; i < 5; i++)
        See(people, { Det(81, 1.0f) });
    EXPECT_EQ(people.StableForRaw(81), 82) << "the mask the tracker calls 81 is B's";
    EXPECT_EQ(people.StableForRaw(82), 82);
}

TEST(PoseIdentityPeopleTest, AnUntrackedDetectionPassesThrough) {
    People people;
    std::vector<int> ids = See(people, { Det(-1, 0.0f), Det(91, 0.5f) });
    EXPECT_EQ(ids[0], -1);
    EXPECT_EQ(ids[1], 91);
    EXPECT_EQ(people.Remembered(), 1);
}

TEST(PoseIdentityPeopleTest, ZeroIsTheTrackersIdsAndNoMemory) {
    People people;
    for (int i = 0; i < 10; i++)
        See(people, { Det(101, 0.0f) }, kFrame, 0);
    EXPECT_EQ(See(people, { Det(102, 0.05f) }, kFrame, 0)[0], 102);
    EXPECT_EQ(people.Remembered(), 0);
    EXPECT_EQ(people.StableForRaw(102), 102);
}

TEST(PoseIdentityPeopleTest, SeenTimeCountsSightingsNotTheGapsBetween) {
    People people;
    See(people, { Det(111, 0.0f) });
    EXPECT_FLOAT_EQ(people.SeenSec(111), 0.0f);
    for (int i = 0; i < 4; i++)
        See(people, { Det(111, 0.0f) });
    EXPECT_NEAR(people.SeenSec(111), 0.4f, 1e-4f);
    See(people, {}, 3.0f);
    See(people, { Det(111, 0.0f) });
    EXPECT_NEAR(people.SeenSec(111), 0.6f, 1e-4f) << "a sighting after a gap is one frame's worth";
}

// --- the picture moved (kMovedAgree) ---------------------------------------
//
// The synthetic sensor (scripts/synthetic_kinect.py) stands every body up at
// y 0.9, z 2.5 whenever the game has no move to show and puts them back at
// the move's position when it has: everybody at once, in one frame, under the
// same tracker ids.  A nudged camera does the same.  Each of those used to
// make everybody somebody new (SecondPersonPerformTest: player 1 on three
// tracking ids in one song).  Two people or more: see kMovedAgree.

Detection At(int rawId, float x, float y, float z) {
    Detection d = Det(rawId, x, z);
    d.y = y;
    return d;
}

TEST(PoseIdentityPeopleTest, WhenThePictureMovesEverybodyKeepsHerId) {
    People people;
    for (int i = 0; i < 12; i++)
        See(people, { At(5, -0.4f, 0.9f, 2.5f), At(9, 0.4f, 0.9f, 2.5f) });
    ASSERT_EQ(people.Moved(), 0);
    // Both 0.2 m left, 0.9 m down and 0.9 m nearer between two frames: 0.67 m
    // as People measures it, further than any reach.
    std::vector<int> ids = See(people, { At(5, -0.6f, 0.0f, 1.6f), At(9, 0.2f, 0.0f, 1.6f) });
    EXPECT_EQ(ids[0], 5);
    EXPECT_EQ(ids[1], 9);
    EXPECT_EQ(people.Moved(), 1);
    EXPECT_EQ(people.Remembered(), 2) << "nobody new was made";
    // They dance on there, and it moves back.
    for (int i = 0; i < 12; i++)
        ids = See(people, { At(5, -0.6f, 0.0f, 1.6f), At(9, 0.2f, 0.0f, 1.6f) });
    ids = See(people, { At(9, 0.4f, 0.9f, 2.5f), At(5, -0.4f, 0.9f, 2.5f) });
    EXPECT_EQ(ids[0], 9);
    EXPECT_EQ(ids[1], 5);
    EXPECT_EQ(people.Moved(), 2);
    EXPECT_EQ(people.Reacquired(), 0);
}

TEST(PoseIdentityPeopleTest, WhenThePictureMovesSoDoesWhereALostPersonWas) {
    // Three in view; the third is lost; the picture moves 0.9 m down and
    // nearer; she is found again where she stood, in the picture as it is now.
    People people;
    for (int i = 0; i < 12; i++)
        See(people, { At(5, -0.8f, 0.9f, 2.5f), At(9, 0.0f, 0.9f, 2.5f), At(13, 0.8f, 0.9f, 2.5f) });
    See(people, { At(5, -0.8f, 0.9f, 2.5f), At(9, 0.0f, 0.9f, 2.5f) });
    std::vector<int> ids = See(people, { At(5, -0.8f, 0.0f, 1.6f), At(9, 0.0f, 0.0f, 1.6f) });
    ASSERT_EQ(people.Moved(), 1);
    EXPECT_EQ(ids[0], 5);
    EXPECT_EQ(ids[1], 9);
    ids = See(people, { At(5, -0.8f, 0.0f, 1.6f), At(9, 0.0f, 0.0f, 1.6f), At(14, 0.8f, 0.0f, 1.6f) });
    EXPECT_EQ(ids[2], 13) << "the lost person, under a new tracker id, where she stood";
}

TEST(PoseIdentityPeopleTest, PeopleWhoGoDifferentWaysAreNotThePictureMoving) {
    People people;
    for (int i = 0; i < 12; i++)
        See(people, { At(5, -0.4f, 0.9f, 2.5f), At(9, 0.4f, 0.9f, 2.5f) });
    // The same jump for one, 0.3 m further along x for the other.
    std::vector<int> ids = See(people, { At(5, -0.6f, 0.0f, 1.6f), At(9, 0.5f, 0.0f, 1.6f) });
    EXPECT_EQ(people.Moved(), 0);
    EXPECT_NE(ids[0], 5) << "a body out of reach is somebody new, whatever the tracker calls it";
    EXPECT_NE(ids[1], 9);
}

TEST(PoseIdentityPeopleTest, SomebodyMissingOrNewIsNotThePictureMoving) {
    People people;
    for (int i = 0; i < 12; i++)
        See(people, { At(5, -0.8f, 0.9f, 2.5f), At(9, 0.0f, 0.9f, 2.5f), At(13, 0.8f, 0.9f, 2.5f) });
    // One of the three is not in the frame; the other two are 0.64 m away,
    // the same way.
    std::vector<int> two = See(people, { At(5, -0.8f, 0.0f, 1.6f), At(9, 0.0f, 0.0f, 1.6f) });
    EXPECT_EQ(people.Moved(), 0);
    EXPECT_NE(two[0], 5);
    EXPECT_NE(two[1], 9);

    People more;
    for (int i = 0; i < 12; i++)
        See(more, { At(5, -0.4f, 0.9f, 2.5f) });
    // She is 0.67 m away and somebody else has appeared.
    std::vector<int> ids = See(more, { At(5, -0.6f, 0.0f, 1.6f), At(9, 0.9f, 0.0f, 1.6f) });
    EXPECT_NE(ids[0], 5);
    EXPECT_EQ(more.Moved(), 0);
}

TEST(PoseIdentityPeopleTest, ATrackerIdWhereSomebodyElseWasIsNotThePictureMoving) {
    // A is in view, B was lost 0.3 s ago 0.8 m to her right.  The tracker's
    // id for A turns up on B's spot: that is B (the id slid), not A moved.
    People people;
    for (int i = 0; i < 12; i++)
        See(people, { Det(5, -0.4f), Det(9, 0.4f) });
    for (int i = 0; i < 3; i++)
        See(people, { Det(5, -0.4f) });
    EXPECT_EQ(See(people, { Det(5, 0.4f) })[0], 9);
    EXPECT_EQ(people.Moved(), 0);
    // And two whose ids swap are each where the other was (see above).
    People swap;
    for (int i = 0; i < 12; i++)
        See(swap, { Det(5, -0.4f), Det(9, 0.4f) });
    std::vector<int> ids = See(swap, { Det(9, -0.4f), Det(5, 0.4f) });
    EXPECT_EQ(ids[0], 5);
    EXPECT_EQ(ids[1], 9);
    EXPECT_EQ(swap.Moved(), 0);
}

TEST(PoseIdentityPeopleTest, OnePersonAloneIsNotThePictureMoving) {
    // Nobody to agree with: that is the one-body rule's (below), which keeps
    // where she was.
    People people;
    for (int i = 0; i < 12; i++)
        See(people, { At(101, 0.6f, 0.9f, 2.5f) });
    See(people, { At(101, 0.4f, 0.0f, 1.6f) });
    EXPECT_EQ(people.Moved(), 0);
}

TEST(PoseIdentityPeopleTest, WhereSheWasAMomentAgoMovesWithThePicture) {
    // The anchor (kAnchorSec) is a place in the picture too.  The picture
    // moves, one of them is missed for a frame, and a body turns up where she
    // stood in the OLD picture: 0.64 m from where she is, so somebody else.
    // (0.12 s frames: the anchor is taken every fifth sighting, the 2nd, 6th
    // and 10th here, so the 13th -- the move -- is not one of them.)
    const float dt = 0.12f;
    People people;
    for (int i = 0; i < 12; i++)
        See(people, { At(5, -0.4f, 0.9f, 2.5f), At(9, 0.4f, 0.9f, 2.5f) }, dt);
    See(people, { At(5, -0.4f, 0.0f, 1.6f), At(9, 0.4f, 0.0f, 1.6f) }, dt);
    ASSERT_EQ(people.Moved(), 1);
    See(people, { At(5, -0.4f, 0.0f, 1.6f) }, dt);
    std::vector<int> ids = See(people, { At(5, -0.4f, 0.0f, 1.6f), At(77, 0.4f, 0.9f, 2.5f) }, dt);
    EXPECT_EQ(ids[1], 77);
}

TEST(PoseIdentityPeopleTest, ATrackerIdSlidingAlongTheRowIsNotThePictureMoving) {
    // Two side by side 0.8 m apart; the next frame has the left one's id on
    // the right one's spot and the right one's id 0.8 m further on.  They
    // agree on the way they went, and one of them is exactly where somebody
    // else was: position decides, as it does for any id that slides.
    People people;
    for (int i = 0; i < 12; i++)
        See(people, { Det(5, -0.4f), Det(9, 0.4f) });
    std::vector<int> ids = See(people, { Det(5, 0.4f), Det(9, 1.2f) });
    EXPECT_EQ(people.Moved(), 0);
    EXPECT_EQ(ids[0], 9);
    EXPECT_NE(ids[1], 9);
    EXPECT_NE(ids[1], 5);
}

TEST(PoseIdentityPeopleTest, AJumpAfterFramesWereMissedIsNotThePictureMoving) {
    // Only between consecutive frames: over a gap people do move, and reach
    // has grown to say how far (0.25 + 0.5 m/s).
    People people;
    for (int i = 0; i < 12; i++)
        See(people, { At(5, -0.4f, 0.9f, 2.5f), At(9, 0.4f, 0.9f, 2.5f) });
    // 1.4 m along x in 0.6 s (reach by then: 0.55 m, 0.85 m from the anchor).
    std::vector<int> ids = See(people, { At(5, 1.0f, 0.9f, 2.5f), At(9, 1.8f, 0.9f, 2.5f) }, 0.6f);
    EXPECT_EQ(people.Moved(), 0);
    EXPECT_NE(ids[0], 5);
    EXPECT_NE(ids[1], 9);
    People consecutive;
    for (int i = 0; i < 12; i++)
        See(consecutive, { At(5, -0.4f, 0.9f, 2.5f), At(9, 0.4f, 0.9f, 2.5f) });
    ids = See(consecutive, { At(5, 1.0f, 0.9f, 2.5f), At(9, 1.8f, 0.9f, 2.5f) });
    EXPECT_EQ(consecutive.Moved(), 1) << "the same jump, next frame";
    EXPECT_EQ(ids[0], 5);
    EXPECT_EQ(ids[1], 9);
}

// --- one body alone ---------------------------------------------------------
//
// BoxymanTest's one body: stood up at (0, 0.9, 2.5), then at the first move's
// position, 0.48 m away as People measures it, in the next frame under the
// same tracker id.  It was somebody new -- a second slot, "New player
// detected!" for a lone dancer -- unless her anchor happened to be old enough
// to reach (0.45 s or more: these jump when it is at its youngest).

TEST(PoseIdentityPeopleTest, OneBodyAloneThatJumpsIsStillHer) {
    // 0.1 s frames: the anchor is taken on the 2nd, 7th and 12th sightings.
    People people;
    for (int i = 0; i < 12; i++)
        See(people, { At(5, 0.0f, 0.9f, 2.5f) });
    EXPECT_EQ(See(people, { At(5, 0.02f, 0.08f, 2.02f) })[0], 5);
    EXPECT_EQ(people.Jumped(), 1);
    EXPECT_EQ(people.Remembered(), 1) << "one body, one person";
    // She dances on there, stands up again and goes back: one person.
    for (int i = 0; i < 30; i++)
        ASSERT_EQ(See(people, { At(5, 0.02f, 0.08f, 2.02f) })[0], 5) << "frame " << i;
    EXPECT_EQ(See(people, { At(5, 0.0f, 0.9f, 2.5f) })[0], 5);
    for (int i = 0; i < 3; i++)
        EXPECT_EQ(See(people, { At(5, 0.02f, 0.08f, 2.02f) })[0], 5);
    EXPECT_EQ(people.Remembered(), 1);
    EXPECT_EQ(people.Reacquired(), 0);
    EXPECT_EQ(people.Moved(), 0);
}

TEST(PoseIdentityPeopleTest, AJumpIsOnTrialAndTheBodyWhereSheWasIsHer) {
    // The wrong-person control, as recorded (song 3, 132.7 s): the dancer's
    // tracker id is on somebody walking past 0.5 m away for one frame, and
    // the dancer is back under a new tracker id in the next.
    People people;
    for (int i = 0; i < 12; i++)
        See(people, { Det(51, 0.09f, 2.36f) });
    EXPECT_EQ(See(people, { Det(51, 0.59f, 2.57f) })[0], 51) << "one body, as the tracker said";
    ASSERT_EQ(people.Jumped(), 1);
    EXPECT_EQ(See(people, { Det(52, 0.01f, 2.54f) })[0], 51) << "the dancer, where she was";
    // The passer-by, when she is seen again, is somebody else: the dancer's
    // tracking id stayed with the dancer.
    std::vector<int> ids = See(people, { Det(52, 0.01f, 2.5f), Det(51, 1.06f, 2.97f) });
    EXPECT_EQ(ids[0], 51);
    EXPECT_NE(ids[1], 51);
    EXPECT_GE(ids[1], Dc3PoseIdentity::kPrivateIdBase);
}

TEST(PoseIdentityPeopleTest, WithBothInViewAfterAJumpTheOneWhereSheWasIsHer) {
    // The same, with the passer-by still in the next frame under the dancer's
    // tracker id: the pairing alone would leave the dancer's identity on it
    // (the nearer body, and the tracker's id), and the dancer somebody new.
    People people;
    for (int i = 0; i < 12; i++)
        See(people, { Det(51, 0.09f, 2.36f) });
    ASSERT_EQ(See(people, { Det(51, 0.59f, 2.57f) })[0], 51);
    std::vector<int> ids = See(people, { Det(51, 0.66f, 2.6f), Det(52, 0.01f, 2.54f) });
    EXPECT_EQ(ids[1], 51) << "the body where the dancer was";
    EXPECT_NE(ids[0], 51) << "the body that jumped is somebody new";
    // And it stays that way.
    for (int i = 0; i < 10; i++) {
        ids = See(people, { Det(51, 0.7f + 0.05f * i, 2.6f), Det(52, 0.01f, 2.54f) });
        ASSERT_EQ(ids[1], 51) << "frame " << i;
        ASSERT_NE(ids[0], 51) << "frame " << i;
    }
    EXPECT_EQ(people.Remembered(), 2);
}

TEST(PoseIdentityPeopleTest, TheTrialIsAsLongHoweverOldHerAnchorWas) {
    // 0.12 s frames: the anchor is taken on the 2nd, 6th and 10th sightings,
    // so at the 13th -- the jump -- the one she has is 0.48 s old and about to
    // be replaced.  The place she jumped from is kept from the jump on: the
    // passer-by is seen alone once more, and then both.
    const float dt = 0.12f;
    People people;
    for (int i = 0; i < 12; i++)
        See(people, { Det(51, 0.0f) }, dt);
    ASSERT_EQ(See(people, { Det(51, 0.6f) }, dt)[0], 51);
    ASSERT_EQ(people.Jumped(), 1);
    ASSERT_EQ(See(people, { Det(51, 0.65f) }, dt)[0], 51);
    std::vector<int> ids = See(people, { Det(51, 0.7f), Det(52, 0.0f) }, dt);
    EXPECT_EQ(ids[1], 51) << "the body where the dancer was, 0.24 s after the jump";
    EXPECT_NE(ids[0], 51);
}

TEST(PoseIdentityPeopleTest, DuringTheTrialOnlyABodyWhereSheWasAndNobodyElseIsHer) {
    // Somebody new elsewhere in the picture is somebody new.
    People people;
    for (int i = 0; i < 12; i++)
        See(people, { Det(5, 0.0f) });
    ASSERT_EQ(See(people, { Det(5, 0.7f) })[0], 5);
    std::vector<int> ids = See(people, { Det(5, 0.75f), Det(6, -1.5f) });
    EXPECT_EQ(ids[0], 5);
    EXPECT_EQ(ids[1], 6);

    // And a remembered person found near where she jumped from is that
    // person: 9 stood 0.4 m to her left and was lost 1.2 s ago.
    People known;
    for (int i = 0; i < 12; i++)
        See(known, { Det(5, 0.0f), Det(9, -0.4f) });
    for (int i = 0; i < 12; i++)
        See(known, { Det(5, 0.0f) });
    ASSERT_EQ(See(known, { Det(5, 0.7f) })[0], 5);
    ASSERT_EQ(known.Jumped(), 1);
    ids = See(known, { Det(5, 0.75f), Det(9, -0.22f) });
    EXPECT_EQ(ids[0], 5);
    EXPECT_EQ(ids[1], 9);
}

TEST(PoseIdentityPeopleTest, TheTrialEndsAndABodyWhereSheWasIsThenSomebodyNew) {
    // She really did jump (kAnchorSec later the place she jumped from is no
    // longer hers): a second person who walks in there is a second person.
    People people;
    for (int i = 0; i < 12; i++)
        See(people, { Det(5, 0.0f) });
    ASSERT_EQ(See(people, { Det(5, 0.7f) })[0], 5);
    for (int i = 0; i < 8; i++) // 0.8 s
        ASSERT_EQ(See(people, { Det(5, 0.7f) })[0], 5);
    std::vector<int> ids = See(people, { Det(5, 0.7f), Det(6, 0.0f) });
    EXPECT_EQ(ids[0], 5);
    EXPECT_EQ(ids[1], 6);
}

TEST(PoseIdentityPeopleTest, ANewTrackerIdOutOfReachIsNotAJump) {
    // The tracker says somebody new, and position does not say otherwise.
    People people;
    for (int i = 0; i < 12; i++)
        See(people, { Det(5, 0.0f) });
    EXPECT_EQ(See(people, { Det(6, 0.7f) })[0], 6);
    EXPECT_EQ(people.Jumped(), 0);
    EXPECT_EQ(people.Remembered(), 2);
}

TEST(PoseIdentityPeopleTest, WithTwoInViewAFrameAgoOneBodyIsNotAJump) {
    // One of the two is missing, and which is the question position answers:
    // the body out of everybody's reach is somebody new.
    People people;
    for (int i = 0; i < 12; i++)
        See(people, { Det(5, -0.4f), Det(9, 0.4f) });
    EXPECT_NE(See(people, { Det(9, 1.2f) })[0], 9);
    EXPECT_EQ(people.Jumped(), 0);
    // And a tracker id that turns up where the other one was is the other
    // one, alone in the frame or not.
    People slide;
    for (int i = 0; i < 12; i++)
        See(slide, { Det(5, -0.4f), Det(9, 0.4f) });
    for (int i = 0; i < 3; i++)
        See(slide, { Det(5, -0.4f) });
    EXPECT_EQ(See(slide, { Det(5, 0.4f) })[0], 9);
    EXPECT_EQ(slide.Jumped(), 0);
}

TEST(PoseIdentityPeopleTest, AfterFramesWereMissedOneBodyIsNotAJump) {
    // Only between consecutive frames, like the picture: over a gap reach has
    // grown to say how far she can have gone, and 1.4 m in 0.6 s is not it.
    People people;
    for (int i = 0; i < 12; i++)
        See(people, { Det(5, 0.0f) });
    EXPECT_NE(See(people, { Det(5, 1.4f) }, 0.6f)[0], 5);
    EXPECT_EQ(people.Jumped(), 0);
    // And not when she was missed for a frame in between.
    People gap;
    for (int i = 0; i < 12; i++)
        See(gap, { Det(5, 0.0f) });
    See(gap, {});
    EXPECT_NE(See(gap, { Det(5, 0.7f) })[0], 5);
    EXPECT_EQ(gap.Jumped(), 0);
}

// --- the recording ---------------------------------------------------------

struct RecordedFrame {
    float t;
    std::vector<Detection> dets;
};

std::vector<RecordedFrame> LoadSection(const std::string &name) {
    std::vector<RecordedFrame> out;
    FILE *f = fopen(DC3_DECOMP_SRC_DIR "/../native/tests/data/pose/two_dancers_hips.txt", "r");
    if (!f)
        return out;
    char line[256];
    bool in = false;
    while (fgets(line, sizeof(line), f)) {
        char sec[64];
        float t, x, y, z;
        int n, id;
        if (sscanf(line, "S %63s", sec) == 1) {
            in = (name == sec);
        } else if (in && sscanf(line, "F %f %d", &t, &n) == 2) {
            RecordedFrame frame;
            frame.t = t;
            out.push_back(frame);
        } else if (in && !out.empty() && sscanf(line, "D %d %f %f %f", &id, &x, &y, &z) == 4) {
            Detection d;
            d.rawId = id;
            d.x = x;
            d.y = y;
            d.z = z;
            out.back().dets.push_back(d);
        }
    }
    fclose(f);
    return out;
}

// What the game was given for one recorded detection.
struct Sighting {
    float t;
    int rawId, id;
    float x;
};

std::vector<Sighting> Replay(const std::vector<RecordedFrame> &frames, int windowMs) {
    People people;
    std::vector<Sighting> out;
    float prev = 0.0f;
    for (size_t i = 0; i < frames.size(); i++) {
        float dt = i ? frames[i].t - prev : 1.0f / 30.0f;
        prev = frames[i].t;
        std::vector<int> ids = See(people, frames[i].dets, dt, windowMs);
        for (size_t k = 0; k < ids.size(); k++)
            out.push_back({ frames[i].t, frames[i].dets[k].rawId, ids[k], frames[i].dets[k].x });
    }
    return out;
}

std::set<int> Ids(const std::vector<Sighting> &s, float from, float to, float xLo, float xHi) {
    std::set<int> ids;
    for (const Sighting &g : s) {
        if (g.t >= from && g.t <= to && g.x >= xLo && g.x <= xHi)
            ids.insert(g.id);
    }
    return ids;
}

std::string Show(const std::set<int> &ids) {
    std::string out;
    for (int id : ids)
        out += std::to_string(id) + " ";
    return out;
}

TEST(PoseIdentityRecordingTest, Song2TheTwoDancersKeepTheirIdsThroughTheTrackersSlide) {
    std::vector<RecordedFrame> frames = LoadSection("song2");
    ASSERT_EQ(frames.size(), 184u) << "native/tests/data/pose/two_dancers_hips.txt";
    std::vector<Sighting> seen = Replay(frames, kWindowMs);

    // 0-9 s: the left dancer stands at x -0.8..-0.55, the centre one at
    // -0.1..+0.15; the tracker calls them 7 and 4.
    ASSERT_EQ(Show(Ids(seen, 0.0f, 9.0f, -0.85f, -0.5f)), "7 ");
    ASSERT_EQ(Show(Ids(seen, 0.0f, 9.0f, -0.15f, 0.2f)), "4 ");
    // 9.4-21 s: the tracker has given the left dancer's body id 4, then 8;
    // the centre dancer comes back as 8, then 9.
    EXPECT_EQ(Show(Ids(seen, 9.4f, 21.0f, -0.85f, -0.5f)), "7 ")
        << "the left dancer is one person for the whole stretch";
    EXPECT_EQ(Show(Ids(seen, 12.0f, 21.0f, -0.3f, 0.2f)), "4 ")
        << "and the centre dancer the other one";
    // Nobody else was there.
    std::set<int> all = Ids(seen, 0.0f, 25.0f, -10.0f, 10.0f);
    EXPECT_EQ(Show(all), "4 7 ");
}

TEST(PoseIdentityRecordingTest, TheRecordedPeopleNeverMoveThePicture) {
    // The rule above is for a picture that moves.  Nothing in the recorded
    // session is one: two dancers and a bystander, tracker ids that slide.
    for (const char *section : { "song2", "song3", "song3_walkby" }) {
        std::vector<RecordedFrame> frames = LoadSection(section);
        ASSERT_FALSE(frames.empty()) << section;
        People people;
        float prev = 0.0f;
        for (size_t i = 0; i < frames.size(); i++) {
            float dt = i ? frames[i].t - prev : 1.0f / 30.0f;
            prev = frames[i].t;
            See(people, frames[i].dets, dt, kWindowMs);
        }
        EXPECT_EQ(people.Moved(), 0) << section;
    }
}

TEST(PoseIdentityRecordingTest, Song2StrictIsWhatTheTrackerSaid) {
    // DC3_POSE_REACQUIRE_MS=0, and the reason this branch exists: the left
    // dancer's body under three ids, one of them the centre dancer's.
    std::vector<Sighting> seen = Replay(LoadSection("song2"), 0);
    ASSERT_FALSE(seen.empty());
    EXPECT_EQ(Show(Ids(seen, 0.0f, 21.0f, -0.85f, -0.5f)), "4 7 8 ");
    EXPECT_EQ(Show(Ids(seen, 0.0f, 25.0f, -10.0f, 10.0f)), "4 7 8 9 ");
}

TEST(PoseIdentityRecordingTest, Song3TheRightDancerIsOnePersonForFiftySeconds) {
    std::vector<RecordedFrame> frames = LoadSection("song3");
    ASSERT_EQ(frames.size(), 445u);
    std::vector<Sighting> seen = Replay(frames, kWindowMs);
    // The right dancer: tracker id 38 until 15.8 s, then 43 -- which until
    // then was the LEFT dancer's -- and at 37.2 s one frame on the bystander.
    std::set<int> right;
    for (const Sighting &g : seen) {
        if ((g.t < 15.7f && g.rawId == 38) || (g.t > 16.5f && g.rawId == 43))
            right.insert(g.id);
    }
    EXPECT_EQ(Show(right), "38 ");
    // The left dancer after her step up to the camera and back (she is found
    // 0.63 m from where she was lost half a second before: out of reach here,
    // Players' business): tracker ids 43, 44, 45, one person.
    std::set<int> left = Ids(seen, 11.0f, 50.0f, -0.6f, -0.05f);
    EXPECT_EQ(Show(left), "43 ");
    // Five tracker ids, three people (the left dancer twice).
    EXPECT_EQ(Show(Ids(seen, 0.0f, 50.0f, -10.0f, 10.0f)), "38 42 43 ");
}

TEST(PoseIdentityRecordingTest, Song3SomebodyWalksPastBehindTheDancerAndTheirTrackerIdsSwap) {
    // 132.7 s into song 3, one dancer in view (tracker id 51).  Somebody
    // comes in from the left as 52, passes 0.5 m behind her unseen, and comes
    // out on the right as 51 -- alone in that frame, the dancer undetected --
    // while the dancer is 52 from the next frame on.
    std::vector<RecordedFrame> frames = LoadSection("song3_walkby");
    ASSERT_EQ(frames.size(), 91u);
    std::vector<Sighting> seen = Replay(frames, kWindowMs);
    EXPECT_EQ(Show(Ids(seen, 0.0f, 10.0f, -0.3f, 0.2f)), "51 ")
        << "the dancer is one person, before and after";
    EXPECT_EQ(Show(Ids(seen, 2.2f, 3.5f, -1.5f, -0.5f)), "52 ") << "the passer-by on her way in";
    std::set<int> after = Ids(seen, 6.0f, 10.0f, 1.0f, 2.0f);
    ASSERT_EQ(after.size(), 1u) << Show(after);
    EXPECT_GE(*after.begin(), Dc3PoseIdentity::kPrivateIdBase)
        << "and on her way out: not the dancer, whose tracker id she has";
    // Strict (DC3_POSE_REACQUIRE_MS=0): the dancer's tracking id walks off.
    std::vector<Sighting> strict = Replay(frames, 0);
    EXPECT_EQ(Show(Ids(strict, 0.0f, 10.0f, -0.3f, 0.2f)), "51 52 ");
    EXPECT_EQ(Show(Ids(strict, 6.0f, 10.0f, 1.0f, 2.0f)), "51 ");
}

// ===========================================================================
// Players
// ===========================================================================

// Six slots and the two players' bindings, driven a frame at a time the way
// NativeAutoAssignPlayers does -- with a stand-in for the image: a binding
// Players asks for is made, and `Unbind` is the image taking one away.
class PoseIdentityPlayersTest : public ::testing::Test {
protected:
    void Skeleton(int slot, int id, float x, float z = 2.5f, bool valid = true) {
        mSlots[slot].tracked = true;
        mSlots[slot].valid = valid;
        mSlots[slot].id = id;
        mSlots[slot].x = x;
        mSlots[slot].z = z;
    }
    void Gone(int slot) { mSlots[slot] = SlotView(); }
    void Unbind(int player) { mBound[player] = -1; }

    // One sensor frame (33 ms).  Returns the result; applies its bindings.
    Players::Result Tick(int autoPlayers = 1, int windowMs = kWindowMs) {
        Players::Result r = mPlayers.Update(mSlots, mBound, autoPlayers, 33.0f, windowMs, 1000);
        for (int p = 0; p < 2; p++) {
            if (r.assign[p] >= 0) {
                mBound[p] = r.assign[p];
                mBinds[p]++;
                mLastRule[p] = r.rule[p];
            }
        }
        return r;
    }
    void Ticks(int n, int autoPlayers = 1, int windowMs = kWindowMs) {
        for (int i = 0; i < n; i++)
            Tick(autoPlayers, windowMs);
    }

    // Two dancers playing: player 1 on skeleton 201 at x 0 (auto-assigned),
    // player 2 on 202 at x -0.7 (she joined the image's way).
    void TwoPlaying() {
        Skeleton(0, 201, 0.0f);
        Skeleton(1, 202, -0.7f);
        Tick();
        ASSERT_EQ(mBound[0], 201);
        mBound[1] = 202; // the chooser's doing
        Ticks(30);
        ASSERT_EQ(mBinds[0], 1);
        ASSERT_EQ(mBinds[1], 0);
    }

    Players mPlayers;
    SlotView mSlots[Dc3PoseIdentity::kSlots];
    int mBound[2] = { -1, -1 };
    int mBinds[2] = { 0, 0 };
    int mLastRule[2] = { 0, 0 };
};

TEST_F(PoseIdentityPlayersTest, ALostPlayerTwoIsBoundAgainWhenHerSkeletonReturns) {
    TwoPlaying();
    // Out of view for 2.5 s; the image unbinds her on the way.
    Gone(1);
    Ticks(40);
    Unbind(1);
    Ticks(35);
    EXPECT_EQ(mBinds[1], 0);
    // Back under her own tracking id, in another slot.  Perform is not a
    // two-player mode (autoPlayers 1): before this she stayed out.
    Skeleton(3, 202, -0.6f);
    Players::Result r = Tick();
    EXPECT_EQ(mBound[1], 202);
    EXPECT_EQ(r.rule[1], Dc3PoseIdentity::kRuleSameId);
    EXPECT_EQ(r.slot[1], 3);
    Ticks(30);
    EXPECT_EQ(mBinds[1], 1) << "once";
    EXPECT_EQ(mBound[0], 201);
}

TEST_F(PoseIdentityPlayersTest, ALostPlayerIsBoundToABodyThatAppearsWhereSheStood) {
    TwoPlaying();
    Gone(1);
    Ticks(40);
    Unbind(1);
    Ticks(20);
    // A new skeleton, 0.5 m from where she stood, 2 s after she went.
    Skeleton(4, 250, -0.2f);
    Players::Result r = Tick();
    EXPECT_EQ(mBound[1], 250);
    EXPECT_EQ(r.rule[1], Dc3PoseIdentity::kRuleNear);
    EXPECT_NEAR(r.distance[1], 0.5f, 1e-3f);
}

TEST_F(PoseIdentityPlayersTest, ABodyThatWasThereAllAlongIsNotTheLostPlayer) {
    TwoPlaying();
    // A bystander 0.4 m from player 2, in view for three seconds before she
    // goes (longer than the dropout hold, which the slot's skeleton outlives
    // its person by).
    Skeleton(2, 260, -1.1f);
    Ticks(90);
    Gone(1);
    Ticks(40);
    Unbind(1);
    Ticks(60);
    EXPECT_EQ(mBound[1], -1) << "the bystander did not appear after she was lost";
    EXPECT_EQ(mBinds[1], 0);
}

TEST_F(PoseIdentityPlayersTest, ABodyFarFromWhereSheStoodJoinsTheNormalWay) {
    TwoPlaying();
    Gone(1);
    Ticks(40);
    Unbind(1);
    Ticks(20);
    Skeleton(4, 270, -1.6f); // 0.9 m away: further than kRebindMax
    Ticks(60);
    EXPECT_EQ(mBound[1], -1);
}

TEST_F(PoseIdentityPlayersTest, AfterTheWindowNobodyIsBoundForHer) {
    TwoPlaying();
    Gone(1);
    Ticks(40);
    Unbind(1);
    Ticks(120); // 5.3 s in all
    Skeleton(3, 202, -0.7f); // even her own id, where she stood
    Ticks(30);
    EXPECT_EQ(mBound[1], -1) << "after DC3_POSE_REACQUIRE_MS she raises her hand like anyone";
}

TEST_F(PoseIdentityPlayersTest, APlayerTheImageUnboundInViewIsLeftAlone) {
    // The chooser (or a one-player mode's ResolveSinglePlayer) unbinds player
    // 2 while her skeleton is right there: not a loss, and binding her again
    // would be undone every frame (PLAN W-10).
    TwoPlaying();
    Unbind(1);
    Ticks(60);
    EXPECT_EQ(mBound[1], -1);
    EXPECT_EQ(mBinds[1], 0);
    // Not even after her skeleton then drops out and returns.
    Gone(1);
    Ticks(60);
    Skeleton(1, 202, -0.7f);
    Ticks(30);
    EXPECT_EQ(mBinds[1], 0);
}

TEST_F(PoseIdentityPlayersTest, OneRebindPerLossNotOnePerFrame) {
    TwoPlaying();
    Gone(1);
    Ticks(40);
    Unbind(1);
    Ticks(20);
    Skeleton(3, 202, -0.7f);
    Tick();
    ASSERT_EQ(mBinds[1], 1);
    // The image does not want her (unbinds her at once, skeleton in view).
    for (int i = 0; i < 60; i++) {
        Unbind(1);
        Tick();
    }
    EXPECT_EQ(mBinds[1], 1);
}

TEST_F(PoseIdentityPlayersTest, PlayerOneWaitsForHerOwnBodyThenTakesTheNearest) {
    // Player 1 alone is playing; two strangers are in view (slots 0 and 2,
    // never bound).  Her skeleton, slot 1, goes.
    Skeleton(1, 301, 0.0f);
    Tick();
    ASSERT_EQ(mBound[0], 301);
    Skeleton(0, 302, -1.5f);
    Skeleton(2, 303, 0.9f);
    Ticks(30);
    Gone(1);
    Ticks(20); // 660 ms: the old rule had her on slot 0 in the first frame
    EXPECT_EQ(mBound[0], 301) << "still waiting for her own body";
    Ticks(3);
    EXPECT_EQ(mBound[0], 303) << "the nearer stranger (0.9 m), not the first slot (1.5 m)";
    EXPECT_EQ(mLastRule[0], Dc3PoseIdentity::kRuleAuto);
}

// Song 3 in the recording, four measures in: the camera lost player 1 (the
// left dancer) for 1.7 s and the right dancer, who never joined, got her
// binding -- and kept it for 41 measures.
TEST_F(PoseIdentityPlayersTest, AStandInGivesThePlayerBackWhenSheReturns) {
    Skeleton(0, 301, -0.4f);
    Tick();
    ASSERT_EQ(mBound[0], 301);
    Skeleton(1, 302, 0.4f); // dancing along, never bound
    Ticks(60);
    Gone(0);
    Ticks(30);
    ASSERT_EQ(mBound[0], 302) << "the song goes on with the stand-in";
    ASSERT_EQ(mLastRule[0], Dc3PoseIdentity::kRuleAuto);
    EXPECT_TRUE(mPlayers.StandIn(0));
    Ticks(30);
    EXPECT_EQ(mBinds[0], 2);
    // She is back 2 s after she went, in another slot.
    Skeleton(2, 301, -0.3f);
    Players::Result r = Tick();
    EXPECT_EQ(mBound[0], 301) << "the player herself, not whoever stood in";
    EXPECT_EQ(r.rule[0], Dc3PoseIdentity::kRuleSameId);
    EXPECT_EQ(r.slot[0], 2);
    EXPECT_FALSE(mPlayers.StandIn(0));
    EXPECT_EQ(r.released[0], 302) << "and the stand-in is named, to be forgotten as a player";
    EXPECT_EQ(r.released[1], -1);
    r = Tick();
    EXPECT_EQ(r.released[0], -1) << "once";
    Ticks(60);
    EXPECT_EQ(mBinds[0], 3) << "and it stays";
    EXPECT_EQ(mBound[0], 301);
}

// The wait for her own body is in sensor time and the image's recovery second
// is on the UI clock.  Under DC3_FAST_TIME the UI clock runs four times as
// fast, the image unbinds her 250 sensor-ms in, and waiting on to 700 paused
// the song (ReacquiredDancerTest in the gate).  The wait ends where the image
// gives her up, whatever the clocks say.
TEST_F(PoseIdentityPlayersTest, APlayerTheImageHasGivenUpDoesNotWaitAnyLonger) {
    Skeleton(1, 301, 0.0f);
    Tick();
    ASSERT_EQ(mBound[0], 301);
    Skeleton(0, 302, -1.5f);
    Skeleton(2, 303, 0.9f);
    Ticks(30);
    Gone(1);
    Ticks(5); // 165 ms
    ASSERT_EQ(mBound[0], 301) << "the image holds her binding: she waits";
    Unbind(0); // the image's recovery has run out
    Players::Result r = Tick();
    EXPECT_EQ(mBound[0], 303) << "the nearest skeleton, in the same frame";
    EXPECT_EQ(r.rule[0], Dc3PoseIdentity::kRuleAuto);
    EXPECT_TRUE(mPlayers.StandIn(0)) << "as a stand-in: she is still looked for";
    // Her own body, back in view 0.4 s after it went.
    Ticks(6);
    Skeleton(1, 301, 0.1f);
    r = Tick();
    EXPECT_EQ(mBound[0], 301);
    EXPECT_EQ(r.rule[0], Dc3PoseIdentity::kRuleSameId);
    EXPECT_EQ(r.released[0], 303);
    EXPECT_EQ(mBinds[0], 3);
}

// The image's quality filter fails a dancer close to the camera with her feet
// out of the picture (the recorded session: 1.4 m).  She is tracked, and she is
// still the player: bound by the near rule, not half a second later by the
// auto-assign as a stand-in.  A valid skeleton is taken before a tracked one.
TEST_F(PoseIdentityPlayersTest, ANewBodyTheQualityFilterFailsIsStillHer) {
    Skeleton(0, 301, -0.4f);
    Tick();
    Ticks(60);
    Gone(0);
    Ticks(10);
    Skeleton(2, 399, -0.3f, 2.5f, false);
    Players::Result r = Tick();
    EXPECT_EQ(mBound[0], 399);
    EXPECT_EQ(r.rule[0], Dc3PoseIdentity::kRuleNear);
    EXPECT_FALSE(mPlayers.StandIn(0));
}

TEST_F(PoseIdentityPlayersTest, AValidNewBodyIsTakenBeforeATrackedOne) {
    Skeleton(0, 301, -0.4f);
    Tick();
    Ticks(60);
    Gone(0);
    Ticks(10);
    Skeleton(2, 398, -0.38f, 2.5f, false); // nearer, fails the filter
    Skeleton(3, 399, -0.25f);
    Players::Result r = Tick();
    EXPECT_EQ(mBound[0], 399);
    EXPECT_EQ(r.rule[0], Dc3PoseIdentity::kRuleNear);
}

// Only a stand-in is released: a player who comes back to her own skeleton
// with nobody having stood in, and a stand-in who became the player when the
// window ran out, leave nobody for the image to forget.
TEST_F(PoseIdentityPlayersTest, OnlyAStandInIsReleased) {
    Skeleton(0, 301, -0.4f);
    Tick();
    Ticks(60);
    Gone(0);
    Ticks(40);
    Unbind(0); // the image, after its hold and its recovery second
    Ticks(20);
    Skeleton(0, 301, -0.4f);
    Players::Result r = Tick();
    ASSERT_EQ(r.rule[0], Dc3PoseIdentity::kRuleSameId);
    EXPECT_EQ(r.released[0], -1);

    Skeleton(1, 302, 0.4f);
    Ticks(60);
    Gone(0);
    Ticks(170); // 5.6 s: past the window, 302 is the player now
    ASSERT_EQ(mBound[0], 302);
    ASSERT_FALSE(mPlayers.StandIn(0));
    Gone(1);
    Ticks(40);
    Unbind(0);
    Ticks(20);
    Skeleton(1, 302, 0.4f);
    r = Tick();
    ASSERT_EQ(r.rule[0], Dc3PoseIdentity::kRuleSameId);
    EXPECT_EQ(r.released[0], -1);
}

TEST_F(PoseIdentityPlayersTest, AStandInGivesThePlayerBackToANewBodyWhereSheStood) {
    Skeleton(0, 301, -0.4f);
    Tick();
    Skeleton(1, 302, 0.4f);
    Ticks(60);
    Gone(0);
    Ticks(60);
    ASSERT_EQ(mBound[0], 302);
    Skeleton(0, 399, -0.2f); // a tracking id nobody had, 20 cm from her place
    Players::Result r = Tick();
    EXPECT_EQ(mBound[0], 399);
    EXPECT_EQ(r.rule[0], Dc3PoseIdentity::kRuleNear);
    EXPECT_EQ(r.released[0], 302);
}

TEST_F(PoseIdentityPlayersTest, AfterTheWindowTheStandInIsThePlayer) {
    Skeleton(0, 301, -0.4f);
    Tick();
    Skeleton(1, 302, 0.4f);
    Ticks(60);
    Gone(0);
    Ticks(170); // 5.6 s
    ASSERT_EQ(mBound[0], 302);
    EXPECT_FALSE(mPlayers.StandIn(0));
    Skeleton(2, 301, -0.3f);
    Ticks(30);
    EXPECT_EQ(mBound[0], 302) << "301 walks back in as anybody would: the normal join";
    EXPECT_EQ(mBinds[0], 2);
}

TEST_F(PoseIdentityPlayersTest, ABindingTheImageMadeMeanwhileIsNotAStandIn) {
    // Player 1 is lost, a stand-in is asked for, and the image binds somebody
    // else of its own (a hand raised): that one joined, and stays.
    Skeleton(0, 301, -0.4f);
    Tick();
    Skeleton(1, 302, 0.4f);
    Skeleton(2, 303, 1.2f);
    Ticks(60);
    Gone(0);
    Ticks(30);
    ASSERT_EQ(mBound[0], 302);
    mBound[0] = 303;
    Ticks(5);
    Skeleton(0, 301, -0.4f);
    Ticks(30);
    EXPECT_EQ(mBound[0], 303);
}

TEST_F(PoseIdentityPlayersTest, ABodyKnownBeforeSheWasLostIsNotHerInANewSlotEither) {
    // The slot a skeleton is in starts again when the slot is given up and
    // taken back (a body the camera sees every other frame).  How long the
    // PERSON has been known does not.
    TwoPlaying();
    Skeleton(2, 203, -0.2f); // half a metre from player 2, there all along
    mSlots[2].ageMs = 20000.0f;
    Ticks(30);
    Gone(1);
    Ticks(40);
    Unbind(1);
    Gone(2);
    Tick();
    Skeleton(4, 203, -0.2f);
    mSlots[4].ageMs = 21500.0f;
    Ticks(30);
    EXPECT_EQ(mBound[1], -1) << "the neighbour is not player 2";
    // Control: with the person's age unknown the slot's own is all there is,
    // and a body that has just taken a slot looks new.
    Gone(4);
    Tick();
    Skeleton(5, 203, -0.2f);
    Ticks(2);
    EXPECT_EQ(mBound[1], 203);
}

TEST_F(PoseIdentityPlayersTest, PlayerOneUnboundInViewTakesTheNearestToWhereSheWas) {
    // The image takes player 1's binding away with her skeleton in view; the
    // auto-assign gives her one again at once, as it always did -- the body
    // where she was, not the first slot.
    Skeleton(0, 302, -1.0f);
    Skeleton(1, 301, 0.3f);
    Tick();
    ASSERT_EQ(mBound[0], 302) << "a first join is the first valid skeleton";
    mBound[0] = 301; // the chooser moved her
    Ticks(30);
    Unbind(0);
    Tick();
    EXPECT_EQ(mBound[0], 301);
}

TEST_F(PoseIdentityPlayersTest, PlayerOneDoesNotHopOntoPlayerTwosBody) {
    // Song 3 in the recording: binding p0 0 -> 2 -> 0 -> 1.  Both lost, then
    // player 2's skeleton returns first: it is hers.
    TwoPlaying();
    Gone(0);
    Gone(1);
    Ticks(40);
    Unbind(0);
    Unbind(1);
    Skeleton(0, 202, -0.7f); // slot 0: the first free slot the old rule took
    Ticks(60);
    EXPECT_EQ(mBound[1], 202);
    EXPECT_EQ(mBound[0], -1) << "player 1 is not in view";
    Skeleton(2, 201, 0.1f);
    Ticks(2);
    EXPECT_EQ(mBound[0], 201);
}

TEST_F(PoseIdentityPlayersTest, AFirstJoinIsTheFirstValidSkeletonAsBefore) {
    Skeleton(2, 401, 0.3f, 2.5f, false); // tracked, not yet valid
    Skeleton(4, 402, -0.3f);
    Tick();
    EXPECT_EQ(mBound[0], 402) << "a quality-filter-valid skeleton first";
    EXPECT_EQ(mBound[1], -1) << "player 2 is not auto-assigned outside a two-player mode";
    Gone(4);
    Unbind(0);
    Ticks(30);
    EXPECT_EQ(mBound[0], 401) << "else a tracked one";
}

TEST_F(PoseIdentityPlayersTest, ATwoPlayerModeAutoAssignsBoth) {
    Skeleton(0, 501, 0.4f);
    Skeleton(1, 502, -0.4f);
    Tick(2);
    EXPECT_EQ(mBound[0], 501);
    EXPECT_EQ(mBound[1], 502);
}

TEST_F(PoseIdentityPlayersTest, OneBodyIsNeverBothPlayers) {
    // A two-player mode with one body in view: she is player 1 and player 2
    // is nobody (the game pauses and asks for the second: OnePersonBattleTest).
    Skeleton(0, 601, 0.0f);
    for (int i = 0; i < 180; i++) {
        Tick(2);
        ASSERT_EQ(mBound[0], 601) << "frame " << i;
        ASSERT_EQ(mBound[1], -1) << "frame " << i;
    }
    // And a lost player 2 is not found in player 1's body, in either mode,
    // though it comes to stand where she stood.
    for (int autoPlayers = 1; autoPlayers <= 2; autoPlayers++) {
        mPlayers = Players();
        mBound[0] = mBound[1] = -1;
        mBinds[0] = mBinds[1] = 0;
        Gone(0);
        Gone(1);
        TwoPlaying();
        Gone(1);
        Ticks(40, autoPlayers);
        Unbind(1);
        Skeleton(0, 201, -0.7f);
        for (int i = 0; i < 120; i++) {
            Tick(autoPlayers);
            ASSERT_EQ(mBound[0], 201) << "mode " << autoPlayers << " frame " << i;
            ASSERT_EQ(mBound[1], -1) << "mode " << autoPlayers << " frame " << i;
        }
    }
}

TEST_F(PoseIdentityPlayersTest, ZeroIsTheOldRule) {
    // DC3_POSE_REACQUIRE_MS=0: player 1 takes the first free skeleton at
    // once, player 2 is never bound again outside a two-player mode.
    Skeleton(1, 601, 0.0f);
    Tick(1, 0);
    ASSERT_EQ(mBound[0], 601);
    Skeleton(0, 602, -1.5f);
    Skeleton(2, 603, 0.9f);
    mBound[1] = 603;
    Ticks(30, 1, 0);
    Gone(1);
    Gone(2);
    Tick(1, 0);
    EXPECT_EQ(mBound[0], 602) << "the first slot, in the same frame";
    Ticks(40, 1, 0);
    Unbind(1);
    Skeleton(2, 603, 0.9f);
    Ticks(60, 1, 0);
    EXPECT_EQ(mBound[1], -1);
}

} // namespace

// MOVE_RATING's seen= (np-gpd-track): per body, the fraction of the camera's
// frames since the previous question in which that body was detected.  The
// 2026-11-21 GPD session's player 2 was in 55% of the frames, and most of her
// move_bad came from measures she was barely in; the log said nothing of it.
TEST(PoseSeenTest, CountsCameraFramesPerBodySinceTheLastQuestion) {
    Dc3PoseSeen::Counter c;
    const int both[2] = {7, 9};
    const int left[1] = {7};
    // her first sighting starts her span: 10 frames, she is in 5
    for (int i = 0; i < 10; i++)
        c.Frame(both, (i % 2) ? 2 : 1);
    // 9 was first seen in frame 2 (i = 1): 5 of the 9 frames from there
    EXPECT_FLOAT_EQ(c.Since(9), 5.0f / 9.0f);
    EXPECT_FLOAT_EQ(c.Since(7), 1.0f);
    // the next span starts where the question was asked
    for (int i = 0; i < 4; i++)
        c.Frame(left, 1);
    c.Frame(nullptr, 0); // a camera frame with nobody in it
    EXPECT_FLOAT_EQ(c.Since(7), 4.0f / 5.0f);
    EXPECT_FLOAT_EQ(c.Since(9), 0.0f);
    // no frame since, no body, a body never seen: not a measurement
    EXPECT_FLOAT_EQ(c.Since(7), -1.0f);
    EXPECT_FLOAT_EQ(c.Since(-1), -1.0f);
    EXPECT_FLOAT_EQ(c.Since(42), -1.0f);
    c.Reset();
    EXPECT_FLOAT_EQ(c.Since(7), -1.0f);
}

// ---------------------------------------------------------------------------
// Dc3PoseTwin::Twins (PoseTwin_Native.h, gpd-menubind): one body reported
// twice at two depths.  The geometry is the shape measured on the GPD session
// of 2026-11-21 (not its data): a dancer 1.1 m from the camera, and a second
// report of her with the same head and shoulders in the picture, her hips
// guessed higher and so read 0.4 m further away.
namespace {

// A body: hip centre at (x, hipY, z), shoulders 0.45 m above the hip, head
// 0.25 m above them, shoulders 0.18 m to each side -- all scaled about the
// camera by `s`, which keeps every joint where it is in the picture.
Dc3PoseTwin::View TwinBody(int rawId, float x, float hipY, float z, float s, int seen) {
    Dc3PoseTwin::View v;
    v.rawId = rawId;
    auto put = [&](float *d, float px, float py, float pz) {
        d[0] = px * s;
        d[1] = py * s;
        d[2] = pz * s;
    };
    put(v.shoulder, x, 0.30f, z);
    put(v.head, x, 0.55f, z);
    put(v.shoulderL, x - 0.18f, 0.30f, z);
    put(v.shoulderR, x + 0.18f, 0.30f, z);
    put(v.hip, x, hipY, z);
    v.seen = seen;
    return v;
}

struct TwinEnv {
    TwinEnv() { unsetenv("DC3_POSE_TWIN"); }
    ~TwinEnv() { unsetenv("DC3_POSE_TWIN"); }
};

} // namespace

TEST(PoseTwinTest, OneBodyReportedTwiceIsOneDetection) {
    TwinEnv env;
    Dc3PoseTwin::Twins twins;
    // Her report (legs seen, 16 joints) and the twin: the same upper body 1.4x
    // further away, its hip 0.12 m higher in her frame.
    Dc3PoseTwin::View v[2] = { TwinBody(64, 0.35f, -0.15f, 1.10f, 1.0f, 16),
                               TwinBody(68, 0.35f, -0.03f, 1.10f, 1.4f, 11) };
    EXPECT_LT(Dc3PoseTwin::UpperBodyRatio(v[0], v[1]), 0.05f);
    Dc3PoseTwin::Result r = twins.Update(v, 2, 0.06f);
    EXPECT_FALSE(r.drop[0]);
    EXPECT_TRUE(r.drop[1]);
    // Either order: the one with more joints seen is kept.
    Dc3PoseTwin::View w[2] = { v[1], v[0] };
    r = twins.Update(w, 2, 0.06f);
    EXPECT_TRUE(r.drop[0]);
    EXPECT_FALSE(r.drop[1]);
}

TEST(PoseTwinTest, TheTwinAloneIsHerAtHerDepth) {
    TwinEnv env;
    Dc3PoseTwin::Twins twins;
    Dc3PoseTwin::View v[2] = { TwinBody(64, 0.35f, -0.15f, 1.10f, 1.0f, 16),
                               TwinBody(68, 0.35f, -0.03f, 1.10f, 1.4f, 11) };
    twins.Update(v, 2, 0.06f);
    // Next frame MediaPipe reports only the twin.
    Dc3PoseTwin::View alone = TwinBody(68, 0.36f, -0.03f, 1.10f, 1.4f, 11);
    Dc3PoseTwin::Result r = twins.Update(&alone, 1, 0.06f);
    EXPECT_FALSE(r.drop[0]);
    EXPECT_EQ(r.rawId[0], 64);
    EXPECT_NEAR(alone.shoulder[2] * r.scale[0], 1.10f, 0.01f);
    // And she herself, back, is herself.
    Dc3PoseTwin::View back = TwinBody(64, 0.36f, -0.15f, 1.10f, 1.0f, 16);
    r = twins.Update(&back, 1, 0.06f);
    EXPECT_EQ(r.rawId[0], 64);
    EXPECT_FLOAT_EQ(r.scale[0], 1.0f);
}

TEST(PoseTwinTest, TheTwinsMemoryEnds) {
    TwinEnv env;
    Dc3PoseTwin::Twins twins;
    Dc3PoseTwin::View v[2] = { TwinBody(64, 0.35f, -0.15f, 1.10f, 1.0f, 16),
                               TwinBody(68, 0.35f, -0.03f, 1.10f, 1.4f, 11) };
    twins.Update(v, 2, 0.06f);
    Dc3PoseTwin::View alone = TwinBody(68, 0.35f, -0.03f, 1.10f, 1.4f, 11);
    Dc3PoseTwin::Result r = twins.Update(&alone, 1, Dc3PoseTwin::kTwinMemorySec + 0.1f);
    EXPECT_EQ(r.rawId[0], 68);
    EXPECT_FLOAT_EQ(r.scale[0], 1.0f);
    // Seen apart (two bodies after all), the memory goes at once.
    twins.Update(v, 2, 0.06f);
    Dc3PoseTwin::View apart[2] = { TwinBody(64, 0.35f, -0.15f, 1.10f, 1.0f, 16),
                                   TwinBody(68, 0.95f, -0.15f, 1.20f, 1.0f, 11) };
    r = twins.Update(apart, 2, 0.06f);
    EXPECT_FALSE(r.drop[0]);
    EXPECT_FALSE(r.drop[1]);
    r = twins.Update(&apart[1], 1, 0.06f);
    EXPECT_EQ(r.rawId[0], 68);
    EXPECT_EQ(twins.Aliases(), 0);
}

TEST(PoseTwinTest, TwoPeopleAreNeverTwins) {
    TwinEnv env;
    Dc3PoseTwin::Twins twins;
    // Side by side, 0.6 m apart (P-18's dancers were never closer than 0.43).
    Dc3PoseTwin::View side[2] = { TwinBody(1, -0.30f, -0.15f, 2.5f, 1.0f, 18),
                                  TwinBody(2, 0.30f, -0.15f, 2.5f, 1.0f, 18) };
    // One 0.5 m behind the other and 0.3 m aside (the walk-past of P-18).
    Dc3PoseTwin::View behind[2] = { TwinBody(3, 0.0f, -0.15f, 2.5f, 1.0f, 18),
                                    TwinBody(4, 0.30f, -0.15f, 3.0f, 1.0f, 18) };
    // One standing behind the other, head over her shoulder (0.15 m aside).
    Dc3PoseTwin::View over[2] = { TwinBody(5, 0.0f, -0.15f, 2.0f, 1.0f, 18),
                                  TwinBody(6, 0.15f, -0.05f, 2.6f, 1.0f, 18) };
    for (int f = 0; f < 30; f++) {
        Dc3PoseTwin::Result r = twins.Update(side, 2, 0.1f);
        EXPECT_FALSE(r.drop[0] || r.drop[1]);
        r = twins.Update(behind, 2, 0.1f);
        EXPECT_FALSE(r.drop[0] || r.drop[1]);
        r = twins.Update(over, 2, 0.1f);
        EXPECT_FALSE(r.drop[0] || r.drop[1]) << Dc3PoseTwin::UpperBodyRatio(over[0], over[1]);
    }
    EXPECT_EQ(twins.Aliases(), 0);
}

TEST(PoseTwinTest, OffIsTheBehaviourBefore) {
    TwinEnv env;
    setenv("DC3_POSE_TWIN", "0", 1);
    Dc3PoseTwin::Twins twins;
    Dc3PoseTwin::View v[2] = { TwinBody(64, 0.35f, -0.15f, 1.10f, 1.0f, 16),
                               TwinBody(68, 0.35f, -0.03f, 1.10f, 1.4f, 11) };
    Dc3PoseTwin::Result r = twins.Update(v, 2, 0.06f);
    EXPECT_FALSE(r.drop[0] || r.drop[1]);
    Dc3PoseTwin::Result s = twins.Update(&v[1], 1, 0.06f);
    EXPECT_EQ(s.rawId[0], 68);
}
