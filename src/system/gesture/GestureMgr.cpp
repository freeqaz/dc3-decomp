#include "gesture\GestureMgr.h"
#include "GestureMgr.h"
#include "SkeletonViz.h"
#include "gesture\CameraTilt.h"
#include "gesture\DrawUtl.h"
#include "IdentityInfo.h"
#include "gesture\LiveCameraInput.h"
#include "gesture\Skeleton.h"
#include "gesture\SkeletonQualityFilter.h"
#include "gesture\SkeletonUpdate.h"
#include "gesture\SpeechMgr.h"
#include "gesture\WaveToTurnOnLight.h"
#include "hamobj\HamGameData.h"
#include "hamobj\HamPlayerData.h"
#include "math\Rot.h"
#include "obj\Dir.h"
#include "obj/DirLoader.h"
#include "obj/Object.h"
#include "os\Debug.h"
#include "os\PlatformMgr.h"
#include "os\System.h"
#include "rndobj\Anim.h"
#include "rndobj\Dir.h"
#include "rndobj\Rnd.h"
#include "ui\UILabel.h"
#include "utl/Loader.h"
#include "utl\MakeString.h"
#include "xdk\NUI.h"
#include <cmath>

float GestureMgr::sMaxRecoveryDistance = 0.3;
float GestureMgr::sMinRecoveryTime = 0.7;
float GestureMgr::sMaxRecoveryTime = 1;
float GestureMgr::sConfidenceLossThreshold = 12;
float GestureMgr::sConfidenceRegainThreshold = 16;

GestureMgr *TheGestureMgr;
bool GestureMgr::sIdentityOpInProgress;
static bool sAutoPauseOnCameraDisconnect;

#ifdef HX_NATIVE
// Native controller-mode policy, DC3_CONTROLLER_MODE (System_Native.cpp; see
// docs/debugging/native.md "Controller mode policy").
extern bool Dc3ControllerModeForced();
#endif

GestureMgr::GestureMgr()
    : mLiveCamInput(LiveCameraInput::sInstance), mPauseOnSkeletonLossMode(2), mIDEnabled(1),
#ifdef HX_NATIVE
      // forced (default): boot in controller mode, no Kinect; faithful: the
      // image's 0.  Controller mode policy, docs/debugging/native.md.
      mInControllerMode(Dc3ControllerModeForced()),
#else
      mInControllerMode(0),
#endif
      mInVoiceMode(0), mGesturingWithVoice(0), mInDoubleUserMode(0),
      mInShellMode(0), mDebugDir(0) {
#ifndef HX_NATIVE
    MILO_ASSERT(mLiveCamInput, 0x40);
#endif
    mPlayerSkeletonIDs[0] = -1;
    mPlayerSkeletonIDs[1] = -1;
    // Lane history (5 lines moved): docs/decomp/notes/system/gesture/GestureMgr.md
    for (int i = 0; i < NUM_SKELETONS; i++) {
        mSkeletons[i].Init();
        mFilters[i].Init(sConfidenceLossThreshold, sConfidenceRegainThreshold);
        mIdentityInfos[i].Init(i);
        unk30[i] = 0;
    }
    mTrackingAllSkeletons = false;
    SkeletonUpdateHandle handle = SkeletonUpdate::InstanceHandle();
    handle.AddCallback(this);
    memset(&mOverlapped, 0, sizeof(XOVERLAPPED));
}

GestureMgr::~GestureMgr() {
    SkeletonUpdateHandle handle = SkeletonUpdate::InstanceHandle();
    handle.RemoveCallback(this);
    RELEASE(mDebugDir);
}

BEGIN_HANDLERS(GestureMgr)
    HANDLE_EXPR(pause_on_skeleton_loss, mPauseOnSkeletonLossMode)
    HANDLE_EXPR(toggle_pause_on_skeleton_loss, TogglePauseOnSkeletonLoss())
#ifdef HX_NATIVE
    // Native: no Kinect camera — return safe defaults for LiveCameraInput queries
    if (!mLiveCamInput) {
        if (sym == "get_max_snapshots" || sym == "num_snapshots"
            || sym == "num_snapshot_batches" || sym == "get_snapshot_batch_index"
            || sym == "snapshot_tex") {
            return 0;
        }
        if (sym == "init_snapshots" || sym == "clear_snapshots"
            || sym == "start_snapshot_batch" || sym == "set_autoexposure_region"
            || sym == "set_autoexposure" || sym == "dump_camera_properties"
            || sym == "draw_skeletons") {
            return 0;
        }
        if (sym == "toggle_autoexposure_tweak" || sym == "using_autoexposure_tweak"
            || sym == "toggle_autoexposure") {
            return 0;
        }
    }
#endif
    HANDLE_EXPR(get_max_snapshots, mLiveCamInput->MaxSnapshots())
    HANDLE_ACTION(init_snapshots, mLiveCamInput->InitSnapshots(_msg->Int(2)))
    HANDLE_ACTION(clear_snapshots, mLiveCamInput->ClearSnapshots())
    HANDLE_EXPR(num_snapshots, mLiveCamInput->NumSnapshots())
    HANDLE_EXPR(snapshot_tex, GetSnapshotTex(_msg->Int(2)))
    HANDLE_ACTION(start_snapshot_batch, mLiveCamInput->StartSnapshotBatch())
    HANDLE_EXPR(num_snapshot_batches, mLiveCamInput->NumSnapshotBatches())
    HANDLE_EXPR(
        get_snapshot_batch_index,
        mLiveCamInput->GetSnapshotBatchStartingIndex(_msg->Int(2))
    )
    HANDLE_EXPR(
        toggle_autoexposure_tweak,
        mLiveCamInput->SetTweakedAutoexposure(!mLiveCamInput->GetTweakedAutoexposure())
    )
    HANDLE_EXPR(using_autoexposure_tweak, mLiveCamInput->GetTweakedAutoexposure())
    HANDLE_ACTION(
        set_autoexposure_region,
        mLiveCamInput->SetExposureRegion(
            _msg->Float(2), _msg->Float(3), _msg->Float(4), _msg->Float(5)
        )
    )
    HANDLE_ACTION(set_autoexposure, mLiveCamInput->SetAutoexposure(_msg->Int(2)))
    HANDLE_EXPR(
        toggle_autoexposure,
        mLiveCamInput->SetAutoexposure(!mLiveCamInput->GetAutoexposure())
    )
    HANDLE_EXPR(is_identification_enabled, mIDEnabled)
    HANDLE_ACTION(set_identification_enabled, SetIdentificationEnabled(_msg->Int(2)))
    HANDLE_ACTION(auto_tilt, AutoTilt())
    HANDLE_EXPR(get_player_index_by_tracking_id, mPlayerSkeletonIDs[1] == _msg->Int(2))
    HANDLE_EXPR(
        get_skeleton_index_by_tracking_id, GetSkeletonIndexByTrackingID(_msg->Int(2))
    )
    HANDLE_EXPR(get_tracking_id_by_skeleton_index, GetSkeleton(_msg->Int(2)).TrackingID())
    HANDLE_EXPR(is_tracked_skeleton_index, GetSkeleton(_msg->Int(2)).IsTracked())
    HANDLE_ACTION(set_tracked_skeletons, SetTrackedSkeletons(_msg->Int(2), _msg->Int(3)))
    HANDLE_ACTION(draw_skeletons, TheSkeletonViz->SetShowing(_msg->Int(2)))
    HANDLE_ACTION(dump_camera_properties, mLiveCamInput->DumpProperties())
    HANDLE_EXPR(get_max_recovery_dist, sMaxRecoveryDistance)
    HANDLE_ACTION(set_max_recovery_dist, sMaxRecoveryDistance = _msg->Float(2))
    HANDLE_EXPR(get_max_recovery_time, sMaxRecoveryTime)
    HANDLE_ACTION(set_max_recovery_time, sMaxRecoveryTime = _msg->Float(2))
    HANDLE_EXPR(get_min_recovery_time, sMinRecoveryTime)
    HANDLE_ACTION(set_min_recovery_time, sMinRecoveryTime = _msg->Float(2))
    HANDLE_ACTION(set_in_voice_mode, SetInVoiceMode(_msg->Int(2)))
    HANDLE_ACTION(set_gesturing_with_voice, SetGesturingWithVoice(_msg->Int(2)))
    HANDLE_ACTION(show_gesture_guide, ShowGestureGuide())
    HANDLE_ACTION(show_gesture_troubleshooter, XShowNuiTroubleshooterUI())
    HANDLE_MESSAGE(KinectHardwareStatusMsg)
    HANDLE_MESSAGE(KinectUserBindingChangedMsg)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

#ifdef HX_NATIVE
void GestureMgr_NativePreInit();
#endif
void GestureMgr_NativeInit();
void GestureMgr_NativeTerminate();
void GestureMgr_NativePoll(GestureMgr *);

void GestureMgr::Init() {
#ifdef HX_NATIVE
    // PLATFORM stand-in for LiveCameraInput::PreInit: no NUI device, so the
    // native sensor becomes the default CameraInput, then -- as the image's
    // PreInit does -- SkeletonUpdate::Init/CreateInstance and the exit callback
    // that terminates them, all BEFORE the GestureMgr ctor registers itself as
    // the first SkeletonUpdate callback.
    GestureMgr_NativePreInit();
    TheGestureMgr = new GestureMgr();
    TheGestureMgr->SetName("gesture_mgr", ObjectDir::Main());
    TheDebug.AddExitCallback(GestureMgr::Terminate);
    GestureMgr_NativeInit();
    return;
#endif
    LiveCameraInput::PreInit();
    TheGestureMgr = new GestureMgr();
    TheGestureMgr->SetName("gesture_mgr", ObjectDir::Main());
    InitDrawUtl(*TheGestureMgr);
    ThePlatformMgr.AddSink(TheGestureMgr, "kinect_status_changed");
    ThePlatformMgr.AddSink(TheGestureMgr, "kinect_user_binding_changed");
    TheDebug.AddExitCallback(GestureMgr::Terminate);
}

void GestureMgr::DebugInit() {
    const char *debugStr = nullptr;
    if (SystemConfig("kinect")->FindData("gesture_debug", debugStr, false) && debugStr) {
        ObjectDir *dir = DirLoader::LoadObjects(debugStr, nullptr, nullptr);
        TheGestureMgr->mDebugDir = dynamic_cast<RndDir *>(dir);
        if (!TheGestureMgr->mDebugDir && dir) {
            delete dir;
        }
    }
}

void GestureMgr::Terminate() {
#ifdef HX_NATIVE
    GestureMgr_NativeTerminate();
#else
    TerminateDrawUtl();
    ThePlatformMgr.RemoveSink(TheGestureMgr);
#endif
    RELEASE(TheGestureMgr);
}

void GestureMgr::Poll() {
#ifdef HX_NATIVE
    // Native-only active-skeleton seeding (GestureMgr_Native.cpp).  The native
    // skeleton source is the SkeletonUpdate camera input, driven from App's
    // skeleton_post_update step as on the 360.  This call used to
    // REPLACE the body below (eeb43f6a8, no reason recorded), so natively
    // WaveToTurnOnLight::Poll never ran: its no-world and 40 s timeout arms,
    // which send wave_gesture_complete to TheHamProvider, are game logic that
    // runs on the image with or without a Kinect (mWaveStateEnabled stays false
    // natively because NuiWaveSetEnabled fails, exactly as on a 360 with no
    // sensor).  TheSpeechMgr is null natively (no LiveCameraInput), guarded by
    // the image; TheCameraTilt only acts once a camera_scan has been started.
    GestureMgr_NativePoll(this);
#endif
    if (TheSpeechMgr)
        TheSpeechMgr->Poll();
    TheCameraTilt->Poll();
    TheWaveToTurnOnLight->Poll();
}

LiveCameraInput *GestureMgr::GetLiveCameraInput() const { return mLiveCamInput; }

void GestureMgr::PostUpdate(const SkeletonUpdateData *data) {
    if (!data) {
        return;
    }

    if (!UsingCD() && !data->mCameraInput->IsConnected() && !sAutoPauseOnCameraDisconnect) {
        sAutoPauseOnCameraDisconnect = true;
        mPauseOnSkeletonLossMode = 1;
    }

    for (int i = 0; i < 6; i++) {
        bool updateSkeleton = true;
        unk30[i] = 0;

        if (mTrackingAllSkeletons
            && data->mSkeletonsRight[i]->TrackingID() == mSkeletons[i].TrackingID()) {
            // 8242C788..90 is `subfic/subfe` + `and r10, r11, r10`: the compare
            // result becomes a mask ANDed into the register still holding the
            // `true` initialiser.  That is the conditional `X ? updateSkeleton :
            // false` (98.2 -> 99.0); a plain `= X` normalises with subic/subfe,
            // and `&=` / `&&` / `X & updateSkeleton` add a clrlwi (96.4-97.7).
            updateSkeleton = data->mSkeletonsRight[i]->TrackingState() != kSkeletonPositionOnly ? updateSkeleton : false;
        }
        if (updateSkeleton) {
            mSkeletons[i] = *data->mSkeletonsRight[i];
        }

        mFilters[i].Update(mSkeletons[i], mInShellMode);
        mIdentityInfos[i].PostUpdate();
    }

    if (TheLoadMgr.EditMode()) {
        // Lane history (12 lines moved): docs/decomp/notes/system/gesture/GestureMgr.md
        GestureMgr *self = this;
        if (mActiveSkelTrackingID <= 0 || GetSkeletonIndexByTrackingID(mActiveSkelTrackingID) < 0) {
            for (int i = 0; i < 6; i++) {
                Skeleton &skel = self->GetSkeleton(i);
                if (skel.IsTracked()) {
                    mActiveSkelTrackingID = skel.TrackingID();
                    break;
                }
            }
        }
    }

    if (mTrackingAllSkeletons) {
        int leftID = mPlayerSkeletonIDs[0];
        int rightID = mPlayerSkeletonIDs[1];
        // Through the GLOBAL, not `this`.  8242C880/8242C890 load
        // `?TheGestureMgr@@3PAVGestureMgr@@A` into r8 and both inlined searches
        // index `r8 + 0xaf8` (mSkeletons); the mActiveSkelTrackingID search just
        // above indexes `r25 + 0xacc`, i.e. this-relative.  Same address at run
        // time -- r25 is the SkeletonCallback subobject at GestureMgr+0x2c, and
        // 0xacc + 0x2c == 0xaf8 -- so this is a base-pointer choice, not a bug.
        int leftIdx = TheGestureMgr->GetSkeletonIndexByTrackingID(leftID);
        int rightIdx = TheGestureMgr->GetSkeletonIndexByTrackingID(rightID);

        // Lane history (10 lines moved): docs/decomp/notes/system/gesture/GestureMgr.md
        int displacedLeft = -1;
        int start = 0, end = 5;
        if (leftIdx != -1) {
            start = leftIdx + 1;
            end = leftIdx + 5;
        }
        for (int i = start; i <= end; i++) {
            int candidate = GetSkeleton(i % 6).TrackingID();
            if (candidate > 0 && candidate != rightID) {
                displacedLeft = leftID;
                leftID = candidate;
                break;
            }
        }

        start = 0;
        end = 5;
        if (rightIdx != -1) {
            start = rightIdx + 1;
            end = rightIdx + 5;
        }
        for (int i = start; i <= end; i++) {
            int candidate = GetSkeleton(i % 6).TrackingID();
            if (candidate > 0 && candidate != leftID && candidate != displacedLeft) {
                rightID = candidate;
                break;
            }
        }

        mPlayerSkeletonIDs[0] = leftID;
        mPlayerSkeletonIDs[1] = rightID;
        UpdateTrackedSkeletons();
    }

    mRecoverer.Poll();
}

IdentityInfo *GestureMgr::GetIdentityInfo(int idx) {
    if (idx >= 0 && idx < 6)
        return &mIdentityInfos[idx];
    else
        return nullptr;
}

int GestureMgr::GetSkeletonIndexByTrackingID(int id) const {
    if (id > 0) {
        for (int i = 0; i < 6; i++) {
            if (mSkeletons[i].TrackingID() == id)
                return i;
        }
    }
    return -1;
}

Skeleton *GestureMgr::GetSkeletonByTrackingID(int id) {
    if (id > 0) {
        for (int i = 0; i < 6; i++) {
            if (mSkeletons[i].TrackingID() == id
                && mSkeletons[i].TrackingState() != kSkeletonNotTracked)
                return &mSkeletons[i];
        }
    }
    return nullptr;
}

Skeleton *GestureMgr::GetSkeletonByEnrollmentIndex(int idx) {
    if (idx >= 0) {
        for (int i = 0; i < 6; i++) {
            if (mSkeletons[i].GetEnrollmentIndex() == idx) {
                return &mSkeletons[i];
            }
        }
    }
    return nullptr;
}

Skeleton *GestureMgr::GetActiveSkeleton() {
    return GetSkeletonByTrackingID(mActiveSkelTrackingID);
}

Skeleton &GestureMgr::GetSkeleton(int idx) {
    MILO_ASSERT((0) <= (idx) && (idx) < (6), 0x99);
    return mSkeletons[idx];
}

const Skeleton &GestureMgr::GetSkeleton(int idx) const {
    MILO_ASSERT((0) <= (idx) && (idx) < (6), 0x9F);
    return mSkeletons[idx];
}

SkeletonQualityFilter &GestureMgr::GetSkeletonQualityFilter(int idx) {
    MILO_ASSERT((0) <= (idx) && (idx) < (6), 0x122);
    return mFilters[idx];
}

int GestureMgr::GetActiveSkeletonIndex() const {
    int i;
    if (mActiveSkelTrackingID > 0) {
        for (i = 0; i < 6; i++) {
            if (mSkeletons[i].TrackingID() == mActiveSkelTrackingID)
                goto done;
        }
    }
    i = -1;
done:
    return i;
}

int GestureMgr::GetSecondarySkeletonIndex(bool requireValid) const {
    for (int i = 0; i < NUM_SKELETONS; i++) {
        if (i != GetActiveSkeletonIndex()) {
            if (GetSkeleton(i).IsTracked()) {
                if (!requireValid) {
                    return i;
                }
                if (GetSkeleton(i).IsValid()) {
                    return i;
                }
            }
        }
    }
    return -1;
}

void GestureMgr::SetTrackedSkeletons(int i1, int i2) {
    mPlayerSkeletonIDs[0] = i1;
    mPlayerSkeletonIDs[1] = i2;
    UpdateTrackedSkeletons();
}

void GestureMgr::SetIdentificationEnabled(bool enabled) {
    if (enabled != mIDEnabled) {
        if (!enabled) {
            NuiIdentityAbort();
        }
        mIDEnabled = enabled;
    }
}

void GestureMgr::SetInControllerMode(bool mode) {
#ifdef HX_NATIVE
    // Controller mode policy (DC3_CONTROLLER_MODE, docs/debugging/native.md):
    // forced (default) pins controller mode on -- no Kinect gesture input, and
    // DTA calls exit_controller_mode during screen transitions with nothing to
    // call enter back; faithful stores the argument, as the image does.
    mInControllerMode = Dc3ControllerModeForced() ? true : mode;
#else
    mInControllerMode = mode;
#endif
}
void GestureMgr::SetInVoiceMode(bool mode) { mInVoiceMode = mode; }
void GestureMgr::SetGesturingWithVoice(bool gesturing) {
    mGesturingWithVoice = gesturing;
}
void GestureMgr::SetInDoubleUserMode(bool mode) { mInDoubleUserMode = mode; }

void GestureMgr::StartTrackAllSkeletons() {
    mTrackingAllSkeletons = true;
    for (int i = 0; i < 6; i++) {
        mFilters[i].Init(10, 12);
        mFilters[i].SetSidewaysCutoffThreshold(0.9);
    }
}

void GestureMgr::CancelTrackAllSkeletons() {
    mTrackingAllSkeletons = false;
    for (int i = 0; i < 6; i++) {
        mFilters[i].Init(sConfidenceLossThreshold, sConfidenceRegainThreshold);
        mFilters[i].RestoreDefaultSidewaysCutoffThreshold();
    }
}

bool GestureMgr::IsTrackingAllSkeletons() const { return mTrackingAllSkeletons; }

bool GestureMgr::IsSkeletonValid(int idx) const {
    MILO_ASSERT((0) <= (idx) && (idx) < (6), 0x128);
    return mFilters[idx].Valid();
}

bool GestureMgr::IsSkeletonSitting(int idx) const {
    MILO_ASSERT((0) <= (idx) && (idx) < (6), 0x12E);
    return mFilters[idx].Sitting();
}

bool GestureMgr::IsSkeletonSideways(int idx) const {
    MILO_ASSERT((0) <= (idx) && (idx) < (6), 0x134);
    return mFilters[idx].Sideways();
}

void GestureMgr::UpdateTrackedSkeletons() {
#ifdef HX_NATIVE
    // Native: mPlayerSkeletonIDs (set by our callers) is the only thing native
    // consumers read. The notify below is Xbox NUI-only — it tells the Kinect
    // driver which two skeletons to track at full fidelity. Native has no NUI
    // (mLiveCamInput is always null), and the pose provider already fills every
    // slot at full fidelity with its own trackId->slot map, so there is nothing
    // to notify. Returning early also avoids a per-frame assert spam from
    // SkeletonChooser::Poll.
    if (!mLiveCamInput)
        return;
#endif
    LiveCameraInput *cameraInput = mLiveCamInput;
    MILO_ASSERT(cameraInput, 0x14F);
    cameraInput->SetTrackedSkeletons(mPlayerSkeletonIDs[0], mPlayerSkeletonIDs[1]);
}

int GestureMgr::GetPlayerSkeletonID(int playerIndex) {
    MILO_ASSERT((0) <= (playerIndex) && (playerIndex) < (2), 0x2DA);
    return mPlayerSkeletonIDs[playerIndex];
}

void GestureMgr::SetPlayerSkeletonID(int playerIndex, int id) {
    MILO_ASSERT((0) <= (playerIndex) && (playerIndex) < (2), 0x2E0);
    mPlayerSkeletonIDs[playerIndex] = id;
    UpdateTrackedSkeletons();
}

int GestureMgr::GetPlayerFilteredSkeletonID(int playerIndex, bool b2) {
    MILO_ASSERT((0) <= (playerIndex) && (playerIndex) < (2), 0x2E8);
    int id = GetPlayerSkeletonID(playerIndex);
    if (id >= 0) {
        Skeleton *skeleton = GetSkeletonByTrackingID(id);
        if (!skeleton || (b2 && skeleton->IsSitting())) {
            id = -1;
        }
    }
    return id;
}

DataNode GestureMgr::OnMsg(const KinectHardwareStatusMsg &msg) {
    if (msg->Int(2) == 1) {
        MILO_ASSERT(mLiveCamInput, 0x21b);
#ifdef HX_NATIVE
        // MILO_ASSERT is print-and-continue on native (Debug::Fail early-returns
        // unless MILO_FATAL_FAILS=1), so the assert above does NOT stop the
        // SetAutoexposure call, which touches members and would hard fault. There
        // is no LiveCameraInput on native; a webcam has no Kinect autoexposure
        // control. Dormant today only because nothing sends this message yet.
        if (!mLiveCamInput)
            return 1;
#endif
        mLiveCamInput->SetAutoexposure(true);
    }
    return 1;
}

DataNode GestureMgr::OnMsg(const KinectUserBindingChangedMsg &msg) {
    static SkeletonEnrollmentChangedMsg enrollmentChangedMsg;
    Export(enrollmentChangedMsg, true);
    return 0;
}

void GestureMgr::DrawSkeletonKinectData() {
    Vector2 textPos(0.15f, 0.2f);

    auto& debugDir = mDebugDir;
    // Signed: the image's loop-back test is `cmpwi cr6, r11, 0x6` at 0x82DBB014,
    // not `cmplwi` -- the (unsigned int) cast was ours.
    for (int i = 0; i < NUM_SKELETONS; i++) {
        const Skeleton &skel = GetSkeleton(i);
        SkeletonTrackingState tracking = skel.TrackingState();

        if (debugDir == NULL) {
            const char *statusStr;
            if (tracking == kSkeletonTracked) {
                statusStr = "tracked";
            } else if (tracking == kSkeletonPositionOnly) {
                statusStr = "position only";
            } else {
                statusStr = "not tracked";
            }

            int trackingID = skel.TrackingID();
            const char *activeStr = (i == GetActiveSkeletonIndex()) ? "(active)" : "";

            Hmx::Color white(1.0f, 1.0f, 1.0f, 1.0f);
            Vector2 &result = TheRnd.DrawString(
                MakeString("%i: %s %s %d", i, statusStr, activeStr, trackingID),
                textPos, white, true
            );
            textPos.y = result.y;
        } else {
            RndDir *marker =
                debugDir->Find<RndDir>(MakeString("marker%d", i), false);
            if (marker != NULL) {
                marker->SetShowing(tracking != kSkeletonNotTracked);
                if (tracking != kSkeletonNotTracked) {
                    const Vector3 &root = skel.GetUnkab0();
                    Vector3 markerPos(root.x, -root.z, 0.0f);
                    marker->SetLocalPos(markerPos);

                    // Lane history (60 lines moved): docs/decomp/notes/system/gesture/GestureMgr.md
                    UILabel *idLabel =
                        marker->Find<UILabel>("id.lbl", false);
                    if (idLabel) {
                        idLabel->SetInt(skel.TrackingID(), false);
                    }

                    UILabel *indexLabel =
                        marker->Find<UILabel>("index.lbl", false);
                    if (indexLabel) {
                        indexLabel->SetInt(i, false);
                    }

                    RndAnimatable *activeAnim =
                        marker->Find<RndAnimatable>("active.anim", false);
                    if (activeAnim) {
                        float frame =
                            (i == GetActiveSkeletonIndex()) ? 1.0f : 0.0f;
                        activeAnim->SetFrame(frame, 1.0f);
                    }

                    RndAnimatable *trackedAnim =
                        marker->Find<RndAnimatable>("tracked.anim", false);
                    if (trackedAnim) {
                        trackedAnim->SetFrame(
                            (float)(tracking == kSkeletonTracked), 1.0f
                        );
                    }

                    RndAnimatable *colorAnim = marker->Find<RndAnimatable>(
                        "player_color.anim", false
                    );
                    if (colorAnim) {
                        int playerIdx = -1;
                        for (int j = 0; j < 2; j++) {
                            if (skel.TrackingID()
                                == TheGameData->Player(j)
                                       ->GetSkeletonTrackingID()) {
                                playerIdx = j;
                            }
                        }
                        colorAnim->SetFrame((float)playerIdx, 1.0f);
                    }

                    float rotation = 0.0f;
                    if (tracking == kSkeletonTracked) {
                        Vector3 leftShoulder, rightShoulder;
                        skel.JointPos(
                            kCoordCamera, kJointShoulderLeft, leftShoulder
                        );
                        skel.JointPos(
                            kCoordCamera, kJointShoulderRight, rightShoulder
                        );
                        // In-place: the image has only TWO Vector3 slots here
                        // (0x90 and 0xc0) and the Subtract destination aliases
                        // its first operand -- 0x8242A2D0-0x8242A2FC loads
                        // 0x90/0xc0, subtracts, and stores all three components
                        // BACK into 0x90, including the .y lane whose value is
                        // dead.  A separate `Vector3 diff` gives MSVC a third
                        // local and lets it dead-code that store.
                        Subtract(rightShoulder, leftShoulder, rightShoulder);
                        rotation =
                            (float)std::atan(rightShoulder.z / rightShoulder.x) * RAD2DEG;
                    }

                    RndAnimatable *rotAnim = marker->Find<RndAnimatable>(
                        "rotation.anim", false
                    );
                    if (rotAnim) {
                        rotAnim->SetFrame(rotation, 1.0f);
                    }
                }
            }
        }
    }

    // Lane history (5 lines moved): docs/decomp/notes/system/gesture/GestureMgr.md
    if (mDebugDir) {
        mDebugDir->DrawShowing();
    }
}
