#ifdef HX_NATIVE

#include "Skeleton_Native.h"
#include "NativeSettings.h" // Dc3EnvFlag
#include "gesture/CameraInput.h"
#include "gesture/GestureMgr.h"
#include "gesture/Skeleton.h" // SkeletonCallback
#include "gesture/SkeletonHistory.h"
#include "gesture/SkeletonUpdate.h"
#include "hamobj/HamGameData.h"   // TheGameData (player->skeleton binding)
#include "hamobj/HamPlayerData.h" // GetSkeletonTrackingID
#include "obj/Task.h"
#include <vector>
#ifdef ENABLE_NCNN
#include "pose/InternalPoseProvider.h"
#endif
#include <chrono>
#include <cstdio>
#include <cstring>

// Lightweight SkeletonHistory for native -- follows the MocapSkeletonIterator
// pattern. Inherits SkeletonHistoryArchive (ring buffer storage) and
// SkeletonHistory (PrevSkeleton lookup). Populated each frame in
// GestureMgr_NativePoll() to mirror Xbox's SkeletonUpdate::UpdateCallbacks().
class NativeSkeletonHistory : public SkeletonHistoryArchive, public SkeletonHistory {
public:
    bool PrevSkeleton(
        const Skeleton &s, int targetMs, ArchiveSkeleton &out, int &elapsedMs
    ) const override {
        bool found = PrevFromArchive(*this, s, targetMs, out, elapsedMs);
        // DC3_SCORING_DEBUG=1: once-per-second archive-lookup liveness counters,
        // so a crash-free run can be distinguished from silently-dead lookups.
        static bool sDebug = Dc3EnvFlag("DC3_SCORING_DEBUG", false);
        if (sDebug) {
            static unsigned sCalls = 0, sHits = 0;
            static std::chrono::steady_clock::time_point sLastPrint;
            sCalls++;
            if (found)
                sHits++;
            std::chrono::steady_clock::time_point now =
                std::chrono::steady_clock::now();
            if (now - sLastPrint > std::chrono::seconds(1)) {
                sLastPrint = now;
                fprintf(stderr, "DC3 SCORING: PrevSkeleton calls=%u hits=%u\n",
                    sCalls, sHits);
            }
        }
        return found;
    }
};

static NativeSkeletonHistory *sNativeHistory = nullptr;

// Minimal CameraInput for native -- reports connected, no real frame data.
// Only used to satisfy SkeletonUpdateData::mCameraInput pointer.
class NativeCameraInput : public CameraInput {
public:
    const SkeletonFrame *PollNewFrame() override { return nullptr; }
};

static NativeCameraInput *sNativeCameraInput = nullptr;
#ifdef ENABLE_NCNN
static InternalPoseProvider *sInternalPose = nullptr;
#endif

// Persistent trackId -> skeleton-slot assignment (index = slot, value = owning
// trackId, -1 = free). A new trackId claims the lowest free slot and keeps it
// while it persists; when a trackId stops appearing the slot is released so a
// later person cannot inherit the departed person's archive history. Reset when
// no provider runs. Shared by both live branches (dummy always uses slot 0).
static int sSlotTrackId[NUM_SKELETONS] = {-1, -1, -1, -1, -1, -1};

static void ResetSlotMap() {
    for (int s = 0; s < NUM_SKELETONS; s++)
        sSlotTrackId[s] = -1;
}

// Native implementation of GestureMgr::Init — replaces the early return stub.
// Called from game startup to initialize skeleton tracking via webcam + YOLO pose.
void GestureMgr_NativeInit() {
    ResetSlotMap();

    // Always create the camera input stub -- needed for PostUpdate pipeline.
    if (!sNativeCameraInput)
        sNativeCameraInput = new NativeCameraInput();

    // Create skeleton history so displacement-based scoring works on native.
    // This replaces SkeletonUpdate's history (which requires Xbox NUI hardware).
    if (!sNativeHistory) {
        sNativeHistory = new NativeSkeletonHistory();
        SkeletonUpdate::SetNativeHistoryFallback(sNativeHistory);
    }

    // In headless mode (tests, CLI tools), skip the pose server entirely.
    // The dummy skeleton in GestureMgr_NativePoll provides a neutral standing
    // pose so skeleton-gated paths still work without a real camera.
    // An explicit DC3_POSE overrides the skip so headless CI can exercise the
    // live-provider path against a synthetic pose server.
    if (getenv("MILO_HEADLESS") && !getenv("DC3_POSE")) {
        printf("Native: headless mode, using dummy skeleton (no pose server)\n");
        if (TheGestureMgr) {
            TheGestureMgr->SetInControllerMode(true);
        }
        return;
    }

    const char *poseMode = getenv("DC3_POSE");
    const char *camStr = getenv("DC3_POSE_CAMERA");
    int camIdx = camStr ? atoi(camStr) : 0;

#ifdef ENABLE_NCNN
    // Try internal ncnn-based pose estimation first (unless explicitly set to external)
    if (!poseMode || strcmp(poseMode, "external") != 0) {
        const char *modelDir = getenv("DC3_POSE_MODELS");
        if (!modelDir) modelDir = "native/models";
        bool useGPU = getenv("DC3_POSE_GPU") != nullptr;

        sInternalPose = new InternalPoseProvider();
        if (sInternalPose->Start(modelDir, camIdx, useGPU)) {
            printf("Native: internal pose estimation started (ncnn + RTMPose)\n");
            goto pose_ready;
        }
        printf("Native: internal pose failed, falling back to external server\n");
        delete sInternalPose;
        sInternalPose = nullptr;
    }
#endif

    // Fall back to external Python pose server
    if (!poseMode || strcmp(poseMode, "off") != 0) {
        if (!TheSkeletonProvider) {
            TheSkeletonProvider = new NativeSkeletonProvider();

            const char *socketPath = getenv("DC3_POSE_SOCKET");
            if (!socketPath) socketPath = "/tmp/dc3_pose.sock";

            const char *modelPath = getenv("DC3_POSE_MODEL");
            if (!modelPath) modelPath = "yolo11n-pose.pt";

            if (TheSkeletonProvider->Start(socketPath, modelPath, camIdx)) {
                printf("Native: external pose server started\n");
            } else {
                printf("Native: pose tracking unavailable (no ncnn, no pose server)\n");
            }
        }
    }

pose_ready:

    if (TheGestureMgr) {
        TheGestureMgr->SetInControllerMode(true);
    }
}

void GestureMgr_NativeTerminate() {
#ifdef ENABLE_NCNN
    if (sInternalPose) {
        sInternalPose->Stop();
        delete sInternalPose;
        sInternalPose = nullptr;
    }
#endif
    if (TheSkeletonProvider) {
        TheSkeletonProvider->Stop();
        delete TheSkeletonProvider;
        TheSkeletonProvider = nullptr;
    }
    SkeletonUpdate::SetNativeHistoryFallback(nullptr);
    ResetSlotMap();
    delete sNativeHistory;
    sNativeHistory = nullptr;
    delete sNativeCameraInput;
    sNativeCameraInput = nullptr;
}

// Resolve stable slot assignments for this frame's persons. trackIds[k] is the
// identity of the k-th valid person (k in [0,numValid)); on return
// personForSlot[slot] is the person index k filling that slot, or -1. Slots
// whose owner departed are released here (caller MarkUntracked's them). A slot
// that changes occupants drops its archived poses immediately (see step 3).
// Returns the number of newly-assigned slots (slot reassignments this frame).
static int AssignSlots(const int *trackIds, int numValid, int *personForSlot) {
    for (int s = 0; s < NUM_SKELETONS; s++)
        personForSlot[s] = -1;

    bool placed[NUM_SKELETONS];
    for (int k = 0; k < numValid; k++)
        placed[k] = false;

    // 1. Persisting trackIds keep their slot.
    for (int k = 0; k < numValid; k++) {
        if (trackIds[k] < 0) continue;
        for (int s = 0; s < NUM_SKELETONS; s++) {
            if (sSlotTrackId[s] == trackIds[k]) {
                personForSlot[s] = k;
                placed[k] = true;
                break;
            }
        }
    }

    // 2. Release slots whose owner is no longer present.
    for (int s = 0; s < NUM_SKELETONS; s++) {
        if (sSlotTrackId[s] >= 0 && personForSlot[s] < 0)
            sSlotTrackId[s] = -1;
    }

    // 3. New trackIds claim the lowest free slot. A slot changing occupants must
    //    drop its archived poses HERE, not rely on MarkUntracked's next-frame
    //    ClearHistory: ArchivePrevFrame already ran this accepted frame, so a
    //    same-frame free+reclaim (person A leaves, person B enters, both in one
    //    accepted frame) would otherwise leave A's history under B and corrupt B's
    //    displacement lookback. ClearHistory is idempotent, so clearing a slot that
    //    was already free (empty history) is harmless. trackId < 0 is a transient
    //    untracked detection and never claims a persistent slot (mirrors step 1).
    int reassignments = 0;
    for (int k = 0; k < numValid; k++) {
        if (placed[k] || trackIds[k] < 0) continue;
        for (int s = 0; s < NUM_SKELETONS; s++) {
            if (sSlotTrackId[s] < 0 && personForSlot[s] < 0) {
                sSlotTrackId[s] = trackIds[k];
                personForSlot[s] = k;
                if (sNativeHistory) sNativeHistory->ClearHistory(s);
                // Same reasoning applies to the low-confidence joint-hold cache:
                // a new occupant must not inherit the previous person's held
                // joint positions.
                NativeSkeletonProvider::ResetJointHold(s);
                reassignments++;
                break;
            }
        }
    }
    return reassignments;
}

// Archive each slot's PREVIOUS finalized pose before it is overwritten, matching
// Xbox SkeletonUpdate::UpdateCallbacks archive-then-poll ordering. Untracked
// slots ClearHistory (the Xbox tracking-gap contract). Called once per ACCEPTED
// frame -- per new provider frame, or every game frame for the static dummy.
static void ArchivePrevFrame(GestureMgr *mgr) {
    if (!sNativeHistory) return;
    for (int i = 0; i < NUM_SKELETONS; i++) {
        Skeleton &skel = mgr->GetSkeleton(i);
        if (skel.IsTracked()) {
            sNativeHistory->AddToHistory(i, skel);
        } else {
            sNativeHistory->ClearHistory(i);
        }
    }
}

// Xbox builds SkeletonUpdateData::mSkeletonsLeft as a 2-entry array indexed by
// PLAYER, by matching each player's assigned skeleton tracking ID against the 6
// hardware slots (SkeletonUpdate::Update, src/system/gesture/SkeletonUpdate.cpp
// :390-398; the IDs are snapshotted from HamPlayerData::GetSkeletonTrackingID at
// :455). mSkeletonsRight is the flat 6-entry SLOT array. Consumers depend on the
// distinction: FilterQueue::Poll indexes mSkeletonsLeft by inFrame->mSlot, which
// is a PLAYER index, and HamSkeletonConverter / HamVisDir / FreestyleMotionFilter
// all loop i < 2 over players.
//
// Native previously passed the slot array for BOTH, so player 0 scored only
// because it happened to occupy slot 0, while player 1 read an untracked slot 1
// and therefore always took the errors=1.0 short-circuit -- it could never score.
//
// SkeletonChooser normally performs the binding (TheGameData->AssignSkeleton), but
// that UI flow does not necessarily complete in a headless/fast-boot run, so fall
// back to auto-binding each unbound player to the first unclaimed slot. That
// mirrors the intent of HamGameData::AutoAssignSkeletons, which has no caller in
// the decompiled tree. Leaving an entry null is CORRECT when nobody is present --
// the errors=1.0 path is the right answer for an absent player.
static Skeleton *sPlayerSkeletons[2];

static void BindPlayerSkeletons(GestureMgr *mgr) {
    for (int p = 0; p < 2; p++)
        sPlayerSkeletons[p] = nullptr;

    // Tier 1: the faithful path -- explicit tracking-ID match per player.
    for (int p = 0; p < 2; p++) {
        int wantId = -1;
        if (TheGameData) {
            HamPlayerData *playerData = TheGameData->Player(p);
            if (playerData)
                wantId = playerData->GetSkeletonTrackingID();
        }
        if (wantId < 0)
            continue;
        for (int s = 0; s < NUM_SKELETONS; s++) {
            Skeleton &skel = mgr->GetSkeleton(s);
            if (skel.TrackingID() == wantId) {
                sPlayerSkeletons[p] = &skel;
                break;
            }
        }
    }

    // Tiers 2 and 3: auto-bind an unbound player to the first unclaimed slot,
    // preferring a quality-filter-valid skeleton and falling back to merely
    // tracked. Tier 3 matters for the static dummy, which is tracked but whose
    // validity depends on the quality filter having seen enough frames.
    for (int pass = 0; pass < 2; pass++) {
        bool requireValid = (pass == 0);
        for (int p = 0; p < 2; p++) {
            if (sPlayerSkeletons[p])
                continue;
            for (int s = 0; s < NUM_SKELETONS; s++) {
                Skeleton &skel = mgr->GetSkeleton(s);
                if (requireValid ? !skel.IsValid() : !skel.IsTracked())
                    continue;
                bool claimed = false;
                for (int q = 0; q < 2; q++) {
                    if (sPlayerSkeletons[q] == &skel)
                        claimed = true;
                }
                if (claimed)
                    continue;
                sPlayerSkeletons[p] = &skel;
                if (TheGameData) {
                    HamPlayerData *playerData = TheGameData->Player(p);
                    if (playerData
                        && playerData->GetSkeletonTrackingID() != skel.TrackingID())
                        TheGameData->AssignSkeleton(p, skel.TrackingID());
                }
                break;
            }
        }
    }

    static bool sDebug = Dc3EnvFlag("DC3_SCORING_DEBUG", false);
    if (sDebug) {
        static int sPrevIdx[2] = { -2, -2 };
        int idx[2];
        for (int p = 0; p < 2; p++)
            idx[p] = sPlayerSkeletons[p] ? sPlayerSkeletons[p]->SkeletonIndex() : -1;
        if (idx[0] != sPrevIdx[0] || idx[1] != sPrevIdx[1]) {
            sPrevIdx[0] = idx[0];
            sPrevIdx[1] = idx[1];
            fprintf(stderr, "DC3 SCORING: player->slot binding p0=%d p1=%d\n",
                idx[0], idx[1]);
        }
    }
}

// Wall-clock ms since the previous ACCEPTED frame (Xbox gets this from
// NUI_SKELETON_FRAME). Displacement scoring integrates these, so a garbage value
// poisons it; clamped to [1,200], first accepted frame returns 33.
static int AcceptedFrameElapsed() {
    static std::chrono::steady_clock::time_point sPrevTime;
    static bool sHavePrevTime = false;
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    int elapsedMs;
    if (sHavePrevTime) {
        long ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - sPrevTime).count();
        if (ms < 1) ms = 1;
        if (ms > 200) ms = 200;
        elapsedMs = (int)ms;
    } else {
        elapsedMs = 33;
        sHavePrevTime = true;
    }
    sPrevTime = now;
    return elapsedMs;
}

// DC3_SCORING_DEBUG=1: once-per-second frame-gating liveness counters, matching
// the PrevSkeleton counter style. accepted/skipped are per-call increments;
// reassigns accumulates the AssignSlots reassignment count.
static void ScoringDebugTick(bool accepted, bool skipped, int reassigns) {
    static bool sDebug = Dc3EnvFlag("DC3_SCORING_DEBUG", false);
    if (!sDebug) return;
    static unsigned sAccepted = 0, sSkipped = 0, sReassign = 0;
    static std::chrono::steady_clock::time_point sLastPrint;
    if (accepted) sAccepted++;
    if (skipped) sSkipped++;
    sReassign += reassigns;
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    if (now - sLastPrint > std::chrono::seconds(1)) {
        sLastPrint = now;
        fprintf(stderr, "DC3 SCORING: newFrames=%u skipped=%u slotReassigns=%u\n",
            sAccepted, sSkipped, sReassign);
    }
}

// Called each frame by GestureMgr::Poll() to update skeleton slots
// from the YOLO pose server (or a dummy skeleton), then run the
// filtering pipeline.
void GestureMgr_NativePoll(GestureMgr *mgr) {
    bool providerRunning = false;

#ifdef ENABLE_NCNN
    // Try internal pose pipeline first. Gate the archive+fill+finalize block on
    // a NEW worker frame: the worker runs at camera rate, so re-integrating the
    // same generation at game rate would dilute the displacement lookback. When
    // no new frame arrived, tracked slots keep their pose+history untouched
    // (the camera is just slower than the game loop -- nobody disappeared).
    if (sInternalPose && sInternalPose->IsRunning()) {
        providerRunning = true;
        sInternalPose->Poll();

        static unsigned sNcnnLastGen = 0;
        static bool sNcnnHaveGen = false;
        unsigned gen = sInternalPose->Generation();
        bool newFrame = !sNcnnHaveGen || gen != sNcnnLastGen;
        sNcnnHaveGen = true;
        sNcnnLastGen = gen;

        if (newFrame) {
            int elapsedMs = AcceptedFrameElapsed();
            ArchivePrevFrame(mgr);

            NativeSkeletonProvider::PersonData persons[NativeSkeletonProvider::kMaxPersons];
            int numPersons = 0;
            sInternalPose->FillPersonData(persons, NativeSkeletonProvider::kMaxPersons, numPersons);

            int trackIds[NUM_SKELETONS], origIdx[NUM_SKELETONS], numValid = 0;
            for (int i = 0; i < numPersons && numValid < NUM_SKELETONS; i++) {
                if (persons[i].valid) {
                    trackIds[numValid] = persons[i].trackId;
                    origIdx[numValid] = i;
                    numValid++;
                }
            }
            int personForSlot[NUM_SKELETONS];
            int reassigns = AssignSlots(trackIds, numValid, personForSlot);

            // Use a temporary NativeSkeletonProvider to access FillSkeleton
            // (which has friend access to Skeleton's protected members)
            static NativeSkeletonProvider sFillHelper;
            for (int s = 0; s < NUM_SKELETONS; s++) {
                Skeleton &skel = mgr->GetSkeleton(s);
                int k = personForSlot[s];
                if (k >= 0) {
                    sFillHelper.FillSkeleton(skel, persons[origIdx[k]]);
                    NativeSkeletonProvider::FinalizeSkeletonFrame(skel, s, elapsedMs);
                } else if (skel.IsTracked()) {
                    NativeSkeletonProvider::MarkUntracked(skel);
                }
            }
            ScoringDebugTick(true, false, reassigns);
        } else {
            ScoringDebugTick(false, true, 0);
        }
    }
#endif

    // Fall back to external pose server. Same new-frame gating on the socket
    // packet frame_id.
    if (!providerRunning && TheSkeletonProvider && TheSkeletonProvider->IsRunning()) {
        providerRunning = true;
        TheSkeletonProvider->Poll();

        static uint32_t sExtLastFrameId = 0;
        static bool sExtHaveFrame = false;
        uint32_t frameId = TheSkeletonProvider->FrameId();
        bool newFrame = !sExtHaveFrame || frameId != sExtLastFrameId;
        sExtHaveFrame = true;
        sExtLastFrameId = frameId;

        if (newFrame) {
            int elapsedMs = AcceptedFrameElapsed();
            ArchivePrevFrame(mgr);

            int numPersons = TheSkeletonProvider->NumPersons();
            int trackIds[NUM_SKELETONS], origIdx[NUM_SKELETONS], numValid = 0;
            for (int i = 0; i < numPersons && numValid < NUM_SKELETONS; i++) {
                if (TheSkeletonProvider->GetPerson(i).valid) {
                    trackIds[numValid] = TheSkeletonProvider->GetPerson(i).trackId;
                    origIdx[numValid] = i;
                    numValid++;
                }
            }
            int personForSlot[NUM_SKELETONS];
            int reassigns = AssignSlots(trackIds, numValid, personForSlot);

            for (int s = 0; s < NUM_SKELETONS; s++) {
                Skeleton &skel = mgr->GetSkeleton(s);
                int k = personForSlot[s];
                if (k >= 0) {
                    TheSkeletonProvider->FillSkeleton(skel, origIdx[k]);
                    NativeSkeletonProvider::FinalizeSkeletonFrame(skel, s, elapsedMs);
                } else if (skel.IsTracked()) {
                    NativeSkeletonProvider::MarkUntracked(skel);
                }
            }
            ScoringDebugTick(true, false, reassigns);
        } else {
            ScoringDebugTick(false, true, 0);
        }
    }

    if (!providerRunning) {
        // No pose provider running at all — provide a dummy skeleton in slot 0
        // so skeleton-gated code paths (scroll behavior, enter anims) still run.
        // Now that move-scoring is DEFAULT-ON (App.cpp DC3_NATIVE_SCORING gate),
        // this static tracked dummy is the default-run SCORING INPUT: it exercises
        // the whole pipeline (archive-before-fill -> FilterQueue::Poll -> MoveDir
        // fan-out) every frame, yielding a deterministic DetectFrac ~0 (the correct
        // "player standing still" signal). Keep it TRACKED — MarkUntracked would
        // break ShellInput::HasSkeleton()/SkeletonChooser::Poll and silently drop
        // scoring coverage to the errors=1.0 short-circuit.
        // The static dummy pose treats every game frame as a new frame (harmless
        // -- it never moves). Remaining slots stay untracked. A transient
        // 0-person dropout from a running provider intentionally does NOT reach
        // here (see MarkUntracked above) so slot 0 history is never poisoned.
        ResetSlotMap();
        int elapsedMs = AcceptedFrameElapsed();
        ArchivePrevFrame(mgr);
        Skeleton &slot0 = mgr->GetSkeleton(0);
        NativeSkeletonProvider::FillDummySkeleton(slot0);
        NativeSkeletonProvider::FinalizeSkeletonFrame(slot0, 0, elapsedMs);
        for (int i = 1; i < NUM_SKELETONS; i++) {
            Skeleton &skel = mgr->GetSkeleton(i);
            if (skel.IsTracked()) {
                NativeSkeletonProvider::MarkUntracked(skel);
            }
        }
    }

    // Set active skeleton so GetActiveSkeletonTrackingID() returns a
    // valid ID and HamNavList::Poll() finds our skeleton.
    if (mgr->GetActiveSkeletonTrackingID() <= 0) {
        mgr->SetActiveSkeletonTrackingID(1);
    }

    // Run the quality filter + identity tracking pipeline.
    // On Xbox this is done by SkeletonUpdate's thread; on native we
    // do it synchronously here.
    Skeleton *skelPtrs[NUM_SKELETONS];
    for (int i = 0; i < NUM_SKELETONS; i++) {
        skelPtrs[i] = &mgr->GetSkeleton(i);
    }

    SkeletonUpdateData data;
    data.mSkeletonsLeft = skelPtrs;
    data.mSkeletonsRight = skelPtrs;
    data.mFrame = nullptr;
    data.mHistory = sNativeHistory;
    data.mCameraInput = sNativeCameraInput;

    // GestureMgr::PostUpdate reads only mSkeletonsRight (the slot array), and it
    // refreshes the quality filter that Skeleton::IsValid consults -- so bind the
    // per-PLAYER array after it, and before the scoring callbacks that consume
    // mSkeletonsLeft.
    mgr->PostUpdate(&data);
    BindPlayerSkeletons(mgr);
    data.mSkeletonsLeft = sPlayerSkeletons;

    // Drive the SkeletonUpdate scoring callbacks (MoveDir/Game/HamVisDir) that
    // Xbox runs from SkeletonUpdate::UpdateCallbacks/PostUpdate. Update() must
    // run before PostUpdate() (PostUpdateFilters reads the FilterQueue::Poll
    // output produced by Update). mgr->PostUpdate above (GestureMgr identity
    // tracking) is orthogonal and intentionally kept.
    std::vector<SkeletonCallback *> cbs =
        SkeletonUpdate::NativeCallbacks(); // copy: callbacks may register/unregister
    for (size_t i = 0; i < cbs.size(); i++)
        cbs[i]->Update(data);
    for (size_t i = 0; i < cbs.size(); i++)
        cbs[i]->PostUpdate(&data);
}

#endif // HX_NATIVE
