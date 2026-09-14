#include "gesture\SkeletonUpdate.h"
#include "SkeletonUpdate.h"
#include "gesture\CameraInput.h"
#include "gesture\GestureMgr.h"
#include "gesture\LiveCameraInput.h"
#include "gesture\Skeleton.h"
#include "gesture\StubCameraInput.h"
#include "hamobj\HamGameData.h"
#include "hamobj\HamPlayerData.h"
#include "obj\DataFunc.h"
#include "obj/Object.h"
#include "os\CritSec.h"
#include "os\Debug.h"
#include "os\Joypad.h"
#include "os\OSFuncs.h"
#include "os\Timer.h"
#include "utl\MemMgr.h"
#include "utl\Std.h"
#include "xdk\NUI.h"
#include "xdk\XAPILIB.h"

CriticalSection SkeletonUpdateHandle::sCritSec;

#pragma region SkeletonUpdateHandle

SkeletonUpdateHandle::SkeletonUpdateHandle(SkeletonUpdate *update) : mInst(update) {
#ifndef HX_NATIVE
    MILO_ASSERT(mInst, 0x45);
#endif
    sCritSec.Enter();
}

SkeletonUpdateHandle::~SkeletonUpdateHandle() { sCritSec.Exit(); }

std::vector<SkeletonCallback *> &SkeletonUpdateHandle::Callbacks() {
#ifdef HX_NATIVE
    // Native never creates sInstance (CreateInstance only runs in the Xbox
    // LiveCameraInput path), so InstanceHandle() hands back a handle wrapping a
    // null mInst. Mirror every sibling accessor's null-guard instead of deref'ing.
    static std::vector<SkeletonCallback *> sEmpty;
    if (!mInst) return sEmpty;
#endif
    return mInst->mCallbacks;
}
CameraInput *SkeletonUpdateHandle::GetCameraInput() const {
#ifdef HX_NATIVE
    if (!mInst) return nullptr;
#endif
    return mInst->mCameraInput;
}
void SkeletonUpdateHandle::SetCameraInput(CameraInput *input) {
#ifdef HX_NATIVE
    if (!mInst) return;
#endif
    mInst->SetCameraInput(input);
}

bool SkeletonUpdateHandle::HasCallback(SkeletonCallback *cb) {
#ifdef HX_NATIVE
    if (!mInst) return false;
#endif
    return VectorFind(mInst->mCallbacks, cb);
}

void SkeletonUpdateHandle::AddCallback(SkeletonCallback *cb) {
#ifdef HX_NATIVE
    if (!mInst) return;
#endif
    MILO_ASSERT(!HasCallback(cb), 0xA2);
    mInst->mCallbacks.push_back(cb);
}

void SkeletonUpdateHandle::RemoveCallback(SkeletonCallback *cb) {
#ifdef HX_NATIVE
    if (!mInst) return;
#endif
    MILO_ASSERT(HasCallback(cb), 0xA8);
    mInst->mCallbacks.erase(
        std::find(mInst->mCallbacks.begin(), mInst->mCallbacks.end(), cb)
    );
}

void SkeletonUpdateHandle::PostUpdate() {
#ifdef HX_NATIVE
    if (!mInst) return;
#endif
    mInst->PostUpdate();
}

const SkeletonHistory *SkeletonUpdateHandle::History() const {
#ifdef HX_NATIVE
    if (!mInst) return SkeletonUpdate::sNativeHistoryFallback;
#endif
    return mInst;
}

#pragma endregion
#pragma region SkeletonUpdate

static bool sBool878;
// Target: SkeletonUpdate.obj .data:0xA0 (0x82F0BE80) = .float 2.
extern "C" {
float lbl_82F0BE80 = 2.0f;
// Target: SkeletonUpdate.obj .data:0xEC (0x82F0BECC) = .float 0.8
// Lateral spacing between stubbed (fake) skeletons.
float lbl_82F0BECC = 0.8f;
}
SkeletonUpdate *SkeletonUpdate::sInstance;
HANDLE SkeletonUpdate::sNewSkeletonEvent;
HANDLE SkeletonUpdate::sSkeletonUpdatedEvent;
#ifdef HX_NATIVE
const SkeletonHistory *SkeletonUpdate::sNativeHistoryFallback = nullptr;
std::vector<SkeletonCallback *> SkeletonUpdate::sNativeCallbacks;
#endif

DWORD SkeletonUpdateThread(LPVOID) {
    HANDLE new_skeleton_event = SkeletonUpdate::NewSkeletonEvent();
    MILO_ASSERT(new_skeleton_event, 0x21);
    HANDLE skeleton_updated_event = SkeletonUpdate::SkeletonUpdatedEvent();
    MILO_ASSERT(skeleton_updated_event, 0x23);
    WaitForSingleObject(new_skeleton_event, -1);
    while (!sBool878) {
        {
            SkeletonUpdateHandle handle = SkeletonUpdate::InstanceHandle();
            if (!handle.mInst->mIsUpdateThreadActive) goto wait;
            handle.mInst->Update();
            SetEvent(skeleton_updated_event);
        }
    wait:
        WaitForSingleObject(new_skeleton_event, -1);
    }
    return 0;
}

SkeletonUpdate::SkeletonUpdate()
    : mHasNewFrame(0), mCameraInput(this), mIsCameraConnected(0), mIsCameraOverride(0),
      unk5388(0), unk538c(0), mSwapSides(0), unk5394(0), unk5398(0),
      mIsUpdateThreadActive(true) {
    MILO_ASSERT(sInstance == NULL, 0x119);
    SetCameraInput(LiveCameraInput::sInstance);
    for (int i = 0; i < 2; i++) {
        mSkeletonsLeft[i] = nullptr;
        mSkeletonsRight[i] = nullptr;
    }
    mNUISkeletonFrame = (NUI_SKELETON_FRAME *)MemAlloc(
        sizeof(NUI_SKELETON_FRAME), __FILE__, 0x126, "NUI_SKELETON_FRAME", 0x10
    );
    memset(mNUISkeletonFrame, 0, sizeof(NUI_SKELETON_FRAME));
    memset(&mSkeletonFrame, 0, sizeof(SkeletonFrame));
#ifndef HX_NATIVE
    mUpdateThread = CreateThread(nullptr, 0, SkeletonUpdateThread, nullptr, 4, nullptr);
    XSetThreadProcessor(mUpdateThread, 5);
    ResumeThread(mUpdateThread);
#endif
}

SkeletonUpdate::~SkeletonUpdate() {
    sBool878 = true;
    SetEvent(sNewSkeletonEvent);
    WaitForSingleObject(mUpdateThread, -1);
    CloseHandle(mUpdateThread);
    mUpdateThread = nullptr;
    MemFree(mNUISkeletonFrame);
}

bool SkeletonUpdate::PrevSkeleton(
    const Skeleton &s, int i2, ArchiveSkeleton &as, int &iref
) const {
    return SkeletonHistory::PrevFromArchive(*this, s, i2, as, iref);
}

SkeletonUpdateHandle SkeletonUpdate::InstanceHandle() {
#ifndef HX_NATIVE
    MILO_ASSERT(sInstance, 0x146);
#endif
    return sInstance;
}

bool SkeletonUpdate::Replace(ObjRef *from, Hmx::Object *to) {
    if (from == &mCameraInput) {
        SetCameraInput(LiveCameraInput::sInstance);
    }
    return Hmx::Object::Replace(from, to);
}

void SkeletonUpdate::SetCameraInput(CameraInput *cam_input) {
    MILO_ASSERT(cam_input, 0x165);
    mCameraInput = cam_input;
    FOREACH (it, mCallbacks) {
        (*it)->Clear();
    }
}

void SkeletonUpdate::CreateInstance() {
    MILO_ASSERT(sInstance == NULL, 0x102);
    sInstance = new SkeletonUpdate();
}

void SkeletonUpdate::Terminate() { RELEASE(sInstance); }
bool SkeletonUpdate::HasInstance() { return sInstance; }
void *SkeletonUpdate::NewSkeletonEvent() { return sNewSkeletonEvent; }

#ifdef HX_NATIVE
std::vector<SkeletonCallback *> &SkeletonUpdate::NativeCallbacks() {
    return sNativeCallbacks;
}

bool SkeletonUpdate::HasNativeCallback(SkeletonCallback *cb) {
    return VectorFind(sNativeCallbacks, cb);
}

void SkeletonUpdate::AddNativeCallback(SkeletonCallback *cb) {
    if (!VectorFind(sNativeCallbacks, cb)) {
        sNativeCallbacks.push_back(cb);
    }
}

void SkeletonUpdate::RemoveNativeCallback(SkeletonCallback *cb) {
    std::vector<SkeletonCallback *>::iterator it =
        std::find(sNativeCallbacks.begin(), sNativeCallbacks.end(), cb);
    if (it != sNativeCallbacks.end()) {
        sNativeCallbacks.erase(it);
    }
}
#endif

void SkeletonUpdate::Update() {
    LONGLONG prevFrame = mNUISkeletonFrame->liTimeStamp.QuadPart;
    if (NuiSkeletonGetNextFrame(0, mNUISkeletonFrame) == 0) {
        mHasNewFrame = true;
        if (!mIsCameraOverride) {
            mSkeletonFrame.Create(
                *mNUISkeletonFrame, (int)mNUISkeletonFrame->liTimeStamp.QuadPart - (int)prevFrame
            );
        }
    } else {
        if (mIsCameraConnected) {
            return;
        }
        if (!mIsCameraOverride) {
            mHasNewFrame = true;
            StubCameraInput::StubSkeletonFrame(mSkeletonFrame);
            for (int i = 0; i < NUM_SKELETONS; i++) {
                SkeletonData &data = mSkeletonFrame.mSkeletonDatas[i];
                data.mTracking = kSkeletonNotTracked;
                data.mQualityFlags = 0;
                for (int j = 0; j < kNumJoints; j++) {
                    data.mJointPositions[j].z = 0.0f;
                    data.mJointPositions[j].y = 0.0f;
                    data.mJointPositions[j].x = 0.0f;
                    data.mJointTrackingState[j] = 0;
                }
                data.mTrackingID = -1;
                data.mClippedFlags = -1;
                data.mHipCenter = Vector3::ZeroVec();
            }
        }
    }
    UpdateCallbacks();
}

void SkeletonUpdate::UpdateFakeArmPos() {
    JoypadData *padData = JoypadGetPadData(0);
    float fVar1 = padData->mSticks[1][1];
    float fVar4 = TheTaskMgr.DeltaUISeconds();
    float fVar11 = fVar4 * lbl_82F0BE80;
    unk5398 = -(fVar11 * fVar1 - unk5398);

    float fVar0 = -0.25f;
    fVar0 = (-0.25f - unk5398 >= 0.0f) ? -0.25f : unk5398;
    unk5398 = (fVar0 - 0.6f >= 0.0f) ? 0.6f : fVar0;
}

void SkeletonUpdate::InsertFakeArmPos(SkeletonData &data) {
    JoypadData *padData = JoypadGetPadData(0);
    float ry = padData->mSticks[1][0];
    if (ry > 0.5f) {
        // RESIDUAL (w7-an, 82.1 canonical): the image RELOADS elbowRight.y
        // (lfs f12, 0x1d8(r31)) and elbowRight.z (lfs f12, 0x1dc(r31)) to
        // build the wrist, where MSVC forwards our just-stored values --
        // which also drags the handRight = wristRight word copy forward into
        // the middle of the shoulder loads.  Source order here already
        // matches the image (z, x, y for the wrist); the divergence is
        // MSVC's store-to-load forwarding, which no spelling of the reads
        // reached.
        float shoulderX = data.mJointPositions[kJointShoulderRight].x;
        float shoulderY = data.mJointPositions[kJointShoulderRight].y;
        float shoulderZ = data.mJointPositions[kJointShoulderRight].z;
        data.mJointPositions[kJointElbowRight].z = shoulderZ;
        data.mJointPositions[kJointElbowRight].y = shoulderY - 0.3f;
        float elbowRightX = shoulderX + 0.3f;
        data.mJointPositions[kJointElbowRight].x = elbowRightX;
        data.mJointPositions[kJointWristRight].z = data.mJointPositions[kJointElbowRight].z;
        data.mJointPositions[kJointWristRight].x = elbowRightX + 0.3f;
        data.mJointPositions[kJointWristRight].y =
            data.mJointPositions[kJointElbowRight].y - 0.3f;
        data.mJointPositions[kJointHandRight] = data.mJointPositions[kJointWristRight];
    } else if (ry < -0.5f) {
        data.mJointPositions[kJointHandRight].y = 0.65f;
        data.mJointPositions[kJointHandLeft].y = 0.65f;
        data.mJointPositions[kJointWristRight].y = 0.6f;
        data.mJointPositions[kJointWristLeft].y = 0.6f;
        data.mJointPositions[kJointElbowRight].y = 0.45f;
        data.mJointPositions[kJointElbowLeft].y = 0.45f;
    } else {
        float rt = padData->mTriggers[1];
        float lt = padData->mTriggers[0];
        if (rt <= 0.5f || lt <= 0.5f) {
            // The image computes each component and stores it before touching
            // the next (fsubs -> stfs 0x58, fadds -> stfs 0x54, fnmsubs/fadds
            // -> stfs 0x50), so this is written as direct field assignment
            // rather than through rightZ/rightY/rightX locals.
            // NEGATIVE RESULT (w7-an, 2026-09-14): that rewrite, `x + -(e)`
            // vs `-(e) + x`, and flipping `elbow.y + unk5398` to
            // `unk5398 + elbow.y` are ALL byte-identical here (82.1, same
            // 52/6/10/13 rows).  MSVC normalises the temporaries away and
            // still folds `x + -(rt*0.5f - 0.1f)` to fmsubs+fsubs where the
            // image keeps fnmsubs+fadds, and still loads unk5398 before the
            // joint field.  Kept for readability, not for score.
            PaddedJointPos rightPos;
            rightPos.z = data.mJointPositions[kJointElbowRight].z - 0.5f;
            rightPos.y = data.mJointPositions[kJointElbowRight].y + unk5398;
            rightPos.x =
                data.mJointPositions[kJointElbowRight].x + -(rt * 0.5f - 0.1f);
            // RESIDUAL (w7-an, 82.1 canonical): both sides assign handRight
            // from the first materialised `addi rN, r1, 0x50` and wristRight
            // from the second -- same registers, same eight words -- but the
            // image schedules the two interleaved 16-byte copies in a
            // different word order (0x200, 0x1f8, 0x1f4, 0x1fc, ... vs our
            // 0x1f0, 0x200, 0x1f4, 0x1f8, ...).  That is backend scheduling,
            // not a source shape: 39 of the 81 rows are the r9/r10 pairing.
            data.mJointPositions[kJointHandRight] = rightPos;
            data.mJointPositions[kJointWristRight] = rightPos;
        } else {
            data.mJointPositions[kJointHandRight].y = 0.65f;
            data.mJointPositions[kJointWristRight].y = 0.6f;
            data.mJointPositions[kJointElbowRight].y = 0.45f;
        }
    }

    float lt = padData->mTriggers[0];
    if (lt > 0.0f && padData->mTriggers[1] == 0.0f) {
        // Direct field assignment, same reasoning (and same inertness) as the
        // right-hand block above; the image's compute-and-store order here is
        // y (0x54), z (0x58), x (0x50).
        PaddedJointPos leftPos;
        leftPos.y = data.mJointPositions[kJointElbowLeft].y + unk5398;
        leftPos.z = data.mJointPositions[kJointElbowLeft].z - 0.5f;
        leftPos.x = data.mJointPositions[kJointElbowLeft].x + (lt * 0.5f - 0.25f);
        data.mJointPositions[kJointWristLeft] = leftPos;
        data.mJointPositions[kJointHandLeft] = leftPos;
    }
}

void SkeletonUpdate::UpdateCallbacks() {
    if (unk5388 > 0) {
        float posHalf = 0.5f;
        int tracked = 0;
        SkeletonData *sd = &mSkeletonFrame.mSkeletonDatas[0];
        for (int i = 0; i < NUM_SKELETONS; i++) {
            if (sd[i].mTracking != kSkeletonNotTracked) {
                tracked++;
            }
        }
        if (tracked < 2) {
            float negHalf = -0.5f;
            for (int i = 0; i < NUM_SKELETONS; i++) {
                if (sd[i].mTracking == kSkeletonNotTracked) {
                    Vector3 offset(posHalf, 0.0f, 0.0f);
                    if (tracked > 0)
                        offset.x = negHalf;
                    StubCameraInput::StubSkeletonData(sd[i], offset);
                    tracked++;
                }
                if (tracked == 2 || tracked == unk5388) {
                    break;
                }
            }
        }
    }

    if (unk538c > 0) {
        UpdateFakeArmPos();
        int i = 0;
        int revBit = 1;
        SkeletonData *sd2 = &mSkeletonFrame.mSkeletonDatas[0];
        do {
            if (((1 << i) & unk538c) != 0) {
                float spacing = lbl_82F0BECC;
                float halfSpacing = spacing * 0.5f;
                int side;
                if (i >= 2 || !mSwapSides) {
                    side = i;
                } else {
                    side = revBit;
                }
                Vector3 offset(halfSpacing - (float)side * spacing, 0.0f, 0.0f);
                StubCameraInput::StubSkeletonData(*sd2, offset);
                sd2->mTrackingID = i + 1;
                if (i == unk5394) {
                    InsertFakeArmPos(*sd2);
                }
            } else {
                sd2->mTracking = kSkeletonNotTracked;
            }
            revBit--;
            i++;
            sd2++;
        } while (revBit > -5);
    }


    for (int i = 0; i < NUM_SKELETONS; i++) {
        if (mSkeletons[i].IsTracked()) {
            AddToHistory(i, mSkeletons[i]);
        } else {
            ClearHistory(i);
        }
        mSkeletons[i].Poll(i, mSkeletonFrame);
    }

    for (int i = 0; i < 2; i++) {
        int j = 0;
        Skeleton *skel = &mSkeletons[0];
        mSkeletonsLeft[i] = nullptr;
        for (; j < NUM_SKELETONS; j++, skel++) {
            if (mSkeletonTrackingIDs[i] == skel->TrackingID()) {
                mSkeletonsLeft[i] = skel;
                break;
            }
        }
    }

    Skeleton **rightSkeletons = (Skeleton **)&mSkeletonsRight[0];
    for (int i = 0; i < NUM_SKELETONS; i++) {
        rightSkeletons[i] = &mSkeletons[i];
    }

    SkeletonUpdateData data;
    data.mSkeletonsLeft = &mSkeletonsLeft[0];
    data.mSkeletonsRight = (Skeleton **)&mSkeletonsRight[0];
    data.mFrame = &mSkeletonFrame;
    data.mHistory = this;
    data.mCameraInput = mCameraInput;
    FOREACH (it, mCallbacks) {
        (*it)->Update(data);
    }
}

void SkeletonUpdateCallbackSlowdownCB(float msecs, void *cbObj) {
    Hmx::Object *obj = dynamic_cast<Hmx::Object *>((SkeletonCallback *)cbObj);
    const char *name;
    if (obj) {
        const char *n = obj->Name();
        if ((int)n == 0) {
            n = "none";
        }
        name = n;
    } else {
        name = "null";
    }

    const char *className;
    if (obj) {
        className = obj->ClassName().Null() ? "unknown class" : obj->ClassName().Str();
    } else {
        className = "null";
    }
    MILO_LOG("%2.2f msec %s %s\n", msecs, name, className);
}

void SkeletonUpdate::PostUpdate() {
    MILO_ASSERT(MainThread(), 0x26F);
    MILO_ASSERT(mCameraInput, 0x273);
    mCameraInput->PollTracking();
    mIsCameraConnected = mCameraInput->IsConnected();
    mIsCameraOverride = mCameraInput->IsOverride();
    if (mIsCameraOverride) {
        const SkeletonFrame *newFrame = mCameraInput->NewFrame();
        if (newFrame) {
            mSkeletonFrame = *newFrame;
            mHasNewFrame = true;
        }
    }
    if (TheGameData) {
        for (int i = 0; i < 2; i++) {
            HamPlayerData *player_data = TheGameData->Player(i);
            MILO_ASSERT(player_data, 0x28B);
            mSkeletonTrackingIDs[i] = player_data->GetSkeletonTrackingID();
        }
    }
    if (mIsUpdateThreadActive) {
        WaitForSingleObject(sSkeletonUpdatedEvent, 1);
        ResetEvent(sSkeletonUpdatedEvent);
    } else {
        Update();
    }
    if (mHasNewFrame) {
        LiveCameraInput::sInstance->SetNewFrame(&mSkeletonFrame);
    }
    // Known residual, 2 rows (99.988 canonical).  The image issues the five
    // field stores as 0x6c(this), 0x60, 0x64, 0x68(&mSkeletonFrame),
    // 0x70(mCameraInput); we issue 0x6c, 0x60, 0x64, 0x70, 0x68 -- the same
    // five values into the same five slots, with only the last pair's issue
    // order transposed.  Every instruction before and after matches, loads
    // included, so this is a scheduler tie and not a value or slot bug.
    // Refuted: transposing the source order of the mFrame and mCameraInput
    // assignments here is byte-inert (2 rows before and after), so the store
    // schedule is not derived from the order these lines are written in.
    SkeletonUpdateData updateData;
    updateData.mSkeletonsLeft = &mSkeletonsLeft[0];
    updateData.mSkeletonsRight = &mSkeletonsRight[0];
    updateData.mFrame = &mSkeletonFrame;
    updateData.mHistory = this;
    updateData.mCameraInput = mCameraInput;
    FOREACH (it, mCallbacks) {
        AutoGlitchReport report(4.0f, SkeletonUpdateCallbackSlowdownCB, *it);
        (*it)->PostUpdate(mHasNewFrame ? &updateData : nullptr);
    }
    mHasNewFrame = false;
    for (int i = 0; i < NUM_SKELETONS; i++) {
        mSkeletons[i].PostUpdate();
    }
}

DataNode OnToggleSkeletalUpdateThread(DataArray *) {
    SkeletonUpdateHandle handle = SkeletonUpdate::InstanceHandle();
    handle.mInst->mIsUpdateThreadActive = !handle.mInst->mIsUpdateThreadActive;
    ResetEvent(SkeletonUpdate::sSkeletonUpdatedEvent);
    return (int)handle.mInst->mIsUpdateThreadActive;
}

DataNode OnCycleNumStubSkeletons(DataArray *) {
    SkeletonUpdateHandle handle = SkeletonUpdate::InstanceHandle();
    int value = (handle.mInst->unk5388 + 1) % 3;
    handle.mInst->unk5388 = value;
    return value;
}

DataNode OnCycleFakeShellSkeletons(DataArray *a) {
    SkeletonUpdateHandle handle = SkeletonUpdate::InstanceHandle();
    int result = handle.mInst->unk538c ^ (1 << a->Int(1));
    handle.mInst->unk538c = result;
    return result;
}

DataNode OnCycleActiveFakeShellSkeleton(DataArray *) {
    SkeletonUpdateHandle handle = SkeletonUpdate::InstanceHandle();
    int value = (handle.mInst->unk5394 + 1) % 2;
    handle.mInst->unk5394 = value;
    return value;
}

DataNode OnSetFakeSkeletonSidesSwapped(DataArray *a) {
    SkeletonUpdateHandle handle = SkeletonUpdate::InstanceHandle();
    bool v = a->Int(1) != 0;
    handle.mInst->mSwapSides = v;
    return 0;
}

DataNode OnGetFakeSkeletonSidesSwapped(DataArray *) {
    SkeletonUpdateHandle handle = SkeletonUpdate::InstanceHandle();
    return (int)handle.mInst->mSwapSides;
}

void SkeletonUpdate::Init() {
    sNewSkeletonEvent = CreateEventA(nullptr, true, false, nullptr);
    sSkeletonUpdatedEvent = CreateEventA(nullptr, true, false, nullptr);
    DataRegisterFunc("toggle_skeletal_update_thread", OnToggleSkeletalUpdateThread);
    DataRegisterFunc("cycle_num_stub_skeletons", OnCycleNumStubSkeletons);
    DataRegisterFunc("cycle_fake_shell_skeletons", OnCycleFakeShellSkeletons);
    DataRegisterFunc("cycle_active_fake_shell_skeleton", OnCycleActiveFakeShellSkeleton);
    DataRegisterFunc("set_fake_skeleton_sides_swapped", OnSetFakeSkeletonSidesSwapped);
    DataRegisterFunc("get_fake_skeleton_sides_swapped", OnGetFakeSkeletonSidesSwapped);
}
