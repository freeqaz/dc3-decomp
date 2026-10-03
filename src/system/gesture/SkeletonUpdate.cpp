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
                // A 16-byte PaddedJointPos copy, NOT PaddedJointPos::operator=
                // (const Vector3 &).  The image reads FOUR words out of
                // Vector3::sZero -- 0x0, 0x4, 0x8 and 0xc -- with plain `lwz`
                // and stores them to 0x2e0..0x2ec with `stw`, i.e. it copies
                // the pad slot too and over-reads sZero by one word.  The
                // three-float form emits lfs/stfs and leaves _pad alone.
                // Spell it through a POINTER cast: `(const PaddedJointPos &)`
                // makes MSVC materialise a 16-byte stack temp instead
                // (79.7 canonical, frame +0x10).
                data.mHipCenter = *(const PaddedJointPos *)&Vector3::ZeroVec();
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
        // w7-by (82.132454 -> 89.5): the w7-an note below called the image's
        // reloads of elbowRight.y/.z (lfs f12, 0x1d8/0x1dc(r31) at
        // 0x8242CEA4/0x8242CEB0) a forwarding floor.  They are a spelling: the
        // wrist stores go through a plain `PaddedJointPos &` local, which MSVC
        // treats as an opaque object -- stores through it block forwarding of
        // the later direct loads, and the materialised reference survives as
        // the image's dead `addi r11, r31, 0x1e4` at 0x8242CE84.  The elbow
        // stores stay direct so the shoulder loads still forward.
        // Measured, all canonical: HEAD's shoulderX/Y/Z + elbowRightX locals
        // and all-direct-no-locals are byte-identical (82.1); Vector3& refs
        // for all three joints 83.3; PaddedJointPos& for shoulder+elbow+wrist
        // 83.2; Vector3& wrist only 87.7; wrist stores in x,y,z order 85.7;
        // `wrist.Set(...)` 86.4; `hand = wrist = rightPos` chain 87.8 but
        // copies in the wrong order; Vector3 struct copies 75.8.
        // Residual at 89.5: the image forwards elbow.x and reloads y/z where
        // we forward z and reload x/y; sx/sy load order; arm-3 word order of
        // the two 16-byte copies (see below).
        PaddedJointPos &wrist = data.mJointPositions[kJointWristRight];
        data.mJointPositions[kJointElbowRight].z = data.mJointPositions[kJointShoulderRight].z;
        data.mJointPositions[kJointElbowRight].y = data.mJointPositions[kJointShoulderRight].y - 0.3f;
        data.mJointPositions[kJointElbowRight].x = data.mJointPositions[kJointShoulderRight].x + 0.3f;
        // w21-f (89.45 -> 89.9 canonical): the three wrist values are read
        // into locals BEFORE the first store through `wrist`, as the image
        // reads elbow.y/x/z (0x1d8, forwarded x, 0x1dc) ahead of its first
        // wrist store at 0x1ec.  Storing wrist.z first (HEAD) made the later
        // elbow loads follow an opaque-reference store.  Remaining here: the
        // image still RELOADS elbow.y/z (lfs 0x1d8/0x1dc) and forwards only x;
        // we now forward all three.  Measured, worse: an `elbow` reference too
        // (85.1); wrist stores x,z,y (86.4); a function-scope `wrist` ref also
        // used by the trigger arm (82.4).  Flipping every `a + b` to `b + a`
        // in the trigger/left arms is byte-inert (MSVC canonicalises).
        float wristZ = data.mJointPositions[kJointElbowRight].z;
        float wristX = data.mJointPositions[kJointElbowRight].x + 0.3f;
        float wristY = data.mJointPositions[kJointElbowRight].y - 0.3f;
        wrist.z = wristZ;
        wrist.x = wristX;
        wrist.y = wristY;
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
            // joint field.
            // w7-by: `x + (0.1f - rt * 0.5f)` DOES reproduce the image's
            // `fnmsubs f0, f13, f11, f0` at 0x8242CF98 (c - a*b is the fnmsubs
            // idiom; x + -(a*b - c) and x - (a*b - c) both canonicalise to
            // fmsubs+fsubs).  The unk5398 load order is still the image's.
            PaddedJointPos rightPos;
            rightPos.Set(
                data.mJointPositions[kJointElbowRight].x + (0.1f - rt * 0.5f),
                data.mJointPositions[kJointElbowRight].y + unk5398,
                data.mJointPositions[kJointElbowRight].z - 0.5f
            );
            // RESIDUAL (w7-an, 82.1 canonical): both sides assign handRight
            // from the first materialised `addi rN, r1, 0x50` and wristRight
            // from the second -- same registers, same eight words -- but the
            // image schedules the two interleaved 16-byte copies in a
            // different word order (0x200, 0x1f8, 0x1f4, 0x1fc, ... vs our
            // 0x1f0, 0x200, 0x1f4, 0x1f8, ...).  That is backend scheduling,
            // not a source shape: 39 of the 81 rows are the r9/r10 pairing.
            // w21-f: a block-scoped `wrist` reference (the image materialises a
            // dead `addi r11, r31, 0x1e4` in this arm too) -- +0.1, the addi
            // itself still does not appear in ours.
            PaddedJointPos &wrist = data.mJointPositions[kJointWristRight];
            data.mJointPositions[kJointHandRight] = rightPos;
            wrist = rightPos;
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
        leftPos.Set(
            data.mJointPositions[kJointElbowLeft].x + (lt * 0.5f - 0.25f),
            data.mJointPositions[kJointElbowLeft].y + unk5398,
            data.mJointPositions[kJointElbowLeft].z - 0.5f
        );
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
                }
                // The count advances on EVERY slot, tracked or not: the image's
                // `bne cr6, .L_8242DCD0` for a tracked slot lands ON the
                // `addi r29, r29, 0x1` (0x8242DCD0), not past it.
                tracked++;
                if (tracked == 2 || tracked == unk5388) {
                    break;
                }
            }
        }
    }

    if (unk538c > 0) {
        UpdateFakeArmPos();
        // w19-c (96.69 -> 98.89): a plain index loop (MSVC derives the old
        // hand-kept `revBit = 1 - i` counter itself, byte-identical), and the
        // offset built as a zero Vector3 BEFORE the side select with x
        // assigned after it: that is what lets the image store offset.y/.z
        // (0x74/0x78) ahead of the select.  Writing a hand-stepped
        // `j++, skel++` loop below as an index loop is worse (97.79).
        for (int i = 0; i < NUM_SKELETONS; i++) {
            SkeletonData &sd2 = mSkeletonFrame.mSkeletonDatas[i];
            if (((1 << i) & unk538c) != 0) {
                float spacing = lbl_82F0BECC;
                float halfSpacing = spacing * 0.5f;
                Vector3 offset(0.0f, 0.0f, 0.0f);
                int side;
                if (i >= 2 || !mSwapSides) {
                    side = i;
                } else {
                    side = 1 - i;
                }
                offset.x = halfSpacing - (float)side * spacing;
                StubCameraInput::StubSkeletonData(sd2, offset);
                sd2.mTrackingID = i + 1;
                if (i == unk5394) {
                    InsertFakeArmPos(sd2);
                }
            } else {
                sd2.mTracking = kSkeletonNotTracked;
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
        // w21-w: the null store comes BEFORE the cursor setup -- the image
        // issues `stw r25, 0x0(r11)` ahead of `mr r10, r27` (98.89 -> 99.99).
        mSkeletonsLeft[i] = nullptr;
        int j = 0;
        Skeleton *skel = &mSkeletons[0];
        for (; j < NUM_SKELETONS; j++, skel++) {
            if (mSkeletonTrackingIDs[i] == skel->TrackingID()) {
                mSkeletonsLeft[i] = skel;
                break;
            }
        }
    }

    for (int i = 0; i < NUM_SKELETONS; i++) {
        mSkeletonsRight[i] = &mSkeletons[i];
    }

    // w19-c: the image stores mHistory (0x8c) before mCameraInput (0x90); we
    // store them the other way round.  Inert: field stores in either order,
    // an aggregate initialiser, and the constructor (which closed PostUpdate).
    // w21-w (99.989, these 2 rows are all that is left): also inert -- a
    // `CameraInput *cam = mCameraInput;` local, array-decay arguments, the
    // ctor with a null camera then `data.mCameraInput = ...`, an explicit
    // iterator loop instead of FOREACH.
    SkeletonUpdateData data(
        &mSkeletonsLeft[0], &mSkeletonsRight[0], &mSkeletonFrame, this, mCameraInput
    );
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
    // w19-c (99.988 -> 100): built through SkeletonUpdateData's five-argument
    // constructor (Skeleton.h).  Field-by-field stores, in either order, or an
    // aggregate `= { ... }` initialiser issue the 0x68 (&mSkeletonFrame) and
    // 0x70 (mCameraInput) stores transposed; the constructor gives the image's
    // order.  (UpdateCallbacks above keeps its own 2-row transposition of the
    // 0x8c/0x90 pair either way.)
    SkeletonUpdateData updateData(
        &mSkeletonsLeft[0], &mSkeletonsRight[0], &mSkeletonFrame, this, mCameraInput
    );
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
    int value = Mod(handle.mInst->unk5388 + 1, 3);
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
    int value = Mod(handle.mInst->unk5394 + 1, 2);
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
