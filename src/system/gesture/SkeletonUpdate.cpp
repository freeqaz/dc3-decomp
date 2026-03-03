#include "gesture/SkeletonUpdate.h"
#include "SkeletonUpdate.h"
#include "gesture/CameraInput.h"
#include "gesture/GestureMgr.h"
#include "gesture/LiveCameraInput.h"
#include "gesture/Skeleton.h"
#include "gesture/StubCameraInput.h"
#include "hamobj/HamGameData.h"
#include "hamobj/HamPlayerData.h"
#include "obj/DataFunc.h"
#include "obj/Object.h"
#include "os/CritSec.h"
#include "os/Debug.h"
#include "os/Joypad.h"
#include "os/OSFuncs.h"
#include "os/Timer.h"
#include "utl/MemMgr.h"
#include "utl/Std.h"
#include "xdk/NUI.h"
#include "xdk/XAPILIB.h"

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

#pragma endregion
#pragma region SkeletonUpdate

static bool sBool878;
extern "C" float lbl_82F0BE80;
SkeletonUpdate *SkeletonUpdate::sInstance;
HANDLE SkeletonUpdate::sNewSkeletonEvent;
HANDLE SkeletonUpdate::sSkeletonUpdatedEvent;

DWORD SkeletonUpdateThread(LPVOID) {
    HANDLE new_skeleton_event = SkeletonUpdate::NewSkeletonEvent();
    MILO_ASSERT(new_skeleton_event, 0x21);
    HANDLE skeleton_updated_event = SkeletonUpdate::SkeletonUpdatedEvent();
    MILO_ASSERT(skeleton_updated_event, 0x23);
    WaitForSingleObject(new_skeleton_event, -1);
    while (!sBool878) {
        {
            SkeletonUpdateHandle handle = SkeletonUpdate::InstanceHandle();
            if (SkeletonUpdate::sInstance->mIsUpdateThreadActive) {
                SkeletonUpdate::sInstance->Update();
                SetEvent(skeleton_updated_event);
            }
        }
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
    float fVar11 = fVar4;
    fVar11 *= lbl_82F0BE80;
    float fVar12 = -(fVar1 * fVar11 - unk5398);
    *(volatile float *)&unk5398 = fVar12;

    float fVar0 = -0.25f;
    fVar0 = (-0.25f - fVar12 >= 0.0f) ? -0.25f : fVar12;
    unk5398 = (fVar0 - 0.6f >= 0.0f) ? 0.6f : fVar0;
}

void SkeletonUpdate::InsertFakeArmPos(SkeletonData &data) {
    JoypadData *padData = JoypadGetPadData(0);
    float ry = padData->mSticks[1][0];
    if (ry > 0.5f) {
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
            float rightZ = data.mJointPositions[kJointElbowRight].z - 0.5f;
            float rightY = data.mJointPositions[kJointElbowRight].y + unk5398;
            float rightX = 0.1f - rt * 0.5f + data.mJointPositions[kJointElbowRight].x;
            PaddedJointPos rightPos;
            rightPos.z = rightZ;
            rightPos.y = rightY;
            rightPos.x = rightX;
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
        float leftZ = data.mJointPositions[kJointElbowLeft].z - 0.5f;
        float leftY = data.mJointPositions[kJointElbowLeft].y + unk5398;
        float leftX = data.mJointPositions[kJointElbowLeft].x + (lt * 0.5f - 0.25f);
        PaddedJointPos leftPos;
        leftPos.y = leftY;
        leftPos.z = leftZ;
        leftPos.x = leftX;
        data.mJointPositions[kJointWristLeft] = leftPos;
        data.mJointPositions[kJointHandLeft] = leftPos;
    }
}

void SkeletonUpdate::UpdateCallbacks() {
    if (unk5388 > 0) {
        int tracked = 0;
        for (int i = 0; i < NUM_SKELETONS; i++) {
            if (mSkeletonFrame.mSkeletonDatas[i].mTracking != kSkeletonNotTracked) {
                tracked++;
            }
        }
        if (tracked < 2) {
            for (int i = 0; i < NUM_SKELETONS; i++) {
                if (mSkeletonFrame.mSkeletonDatas[i].mTracking == kSkeletonNotTracked) {
                    Vector3 offset(tracked > 0 ? -0.5f : 0.5f, 0.0f, 0.0f);
                    StubCameraInput::StubSkeletonData(mSkeletonFrame.mSkeletonDatas[i], offset);
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
        unsigned bit = 1;
        for (unsigned i = 0; i < NUM_SKELETONS; i++, bit <<= 1) {
            SkeletonData &data = mSkeletonFrame.mSkeletonDatas[i];
            if ((bit & unk538c) == 0) {
                data.mTracking = kSkeletonNotTracked;
                continue;
            }
            unsigned side = i;
            if (i <= 1 && mSwapSides) {
                side = 1 - i;
            }
            Vector3 offset(-((float)side * 0.5f - 0.25f), 0.0f, 0.0f);
            StubCameraInput::StubSkeletonData(data, offset);
            data.mTrackingID = i + 1;
            if ((int)i == unk5394) {
                InsertFakeArmPos(data);
            }
        }
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
        mSkeletonsLeft[i] = nullptr;
        for (int j = 0; j < NUM_SKELETONS; j++) {
            if (mSkeletonTrackingIDs[i] == mSkeletons[j].TrackingID()) {
                mSkeletonsLeft[i] = &mSkeletons[j];
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
    data.mSkeletonsRight = &mSkeletonsRight[0];
    data.mFrame = &mSkeletonFrame;
    data.mHistory = this;
    data.mCameraInput = mCameraInput;
    FOREACH (it, mCallbacks) {
        (*it)->Update(data);
    }
}

void SkeletonUpdateCallbackSlowdownCB(float msecs, void *cbObj) {
    Hmx::Object *obj = dynamic_cast<Hmx::Object *>((SkeletonCallback *)cbObj);
    const char *name = obj ? obj->Name() : "null";
    if (!name) {
        name = "none";
    }

    const char *className = "null";
    if (obj) {
        Symbol objClass = obj->ClassName();
        className = objClass.Null() ? "unknown class" : objClass.Str();
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
    SkeletonUpdate::sInstance->mIsUpdateThreadActive =
        !SkeletonUpdate::sInstance->mIsUpdateThreadActive;
    ResetEvent(SkeletonUpdate::sSkeletonUpdatedEvent);
    return (int)SkeletonUpdate::sInstance->mIsUpdateThreadActive;
}

DataNode OnCycleNumStubSkeletons(DataArray *) {
    SkeletonUpdateHandle handle = SkeletonUpdate::InstanceHandle();
    int value = (SkeletonUpdate::sInstance->unk5388 + 1) % 3;
    SkeletonUpdate::sInstance->unk5388 = value;
    return value;
}

DataNode OnCycleFakeShellSkeletons(DataArray *a) {
    SkeletonUpdateHandle handle = SkeletonUpdate::InstanceHandle();
    unsigned int idx = a->Int(1);
    SkeletonUpdate::sInstance->unk538c ^= 1 << (idx & 0x3f);
    return SkeletonUpdate::sInstance->unk538c;
}

DataNode OnCycleActiveFakeShellSkeleton(DataArray *) {
    SkeletonUpdateHandle handle = SkeletonUpdate::InstanceHandle();
    int value = (SkeletonUpdate::sInstance->unk5394 + 1) % 2;
    SkeletonUpdate::sInstance->unk5394 = value;
    return value;
}

DataNode OnSetFakeSkeletonSidesSwapped(DataArray *a) {
    SkeletonUpdateHandle handle = SkeletonUpdate::InstanceHandle();
    SkeletonUpdate::sInstance->mSwapSides = a->Int(1) != 0;
    return 0;
}

DataNode OnGetFakeSkeletonSidesSwapped(DataArray *) {
    SkeletonUpdateHandle handle = SkeletonUpdate::InstanceHandle();
    return (int)SkeletonUpdate::sInstance->mSwapSides;
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
