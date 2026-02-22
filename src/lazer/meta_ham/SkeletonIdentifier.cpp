#include "meta_ham/SkeletonIdentifier.h"
#include "SkeletonIdentifier.h"
#include "flow/PropertyEventProvider.h"
#include "gesture/GestureMgr.h"
#include "gesture/Skeleton.h"
#include "hamobj/HamGameData.h"
#include "hamobj/HamPlayerData.h"
#include "meta_ham/PassiveMessenger.h"
#include "meta_ham/ProfileMgr.h"
#include "obj/Data.h"
#include "obj/Dir.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "os/PlatformMgr.h"
#include "utl/Locale.h"
#include "utl/MakeString.h"
#include "utl/Symbol.h"
#include "xdk/NUI.h"

String EnrollmentIndexString(int idx) {
    String str = MakeString("enrollment index %d", idx);
    if (idx == -1) {
        str = MakeString("%s %s", str, " (NUI_IDENTITY_ENROLLMENT_INDEX_CALL_IDENTIFY)");
    }
    if (idx == -2) {
        str = MakeString("%s %s", str, " (NUI_IDENTITY_ENROLLMENT_INDEX_UNKNOWN)");
    }
    if (idx == -4) {
        str = MakeString("%s %s", str, " (NUI_IDENTITY_ENROLLMENT_INDEX_BUSY)");
    }
    if (idx == -5) {
        str = MakeString("%s %s", str, " (NUI_IDENTITY_ENROLLMENT_INDEX_FAILURE)");
    }
    return str;
}

bool SkeletonIdentifier::EnrolledPlayer::UpdatePlayerBinding() {
    bool b3 = false;
    Skeleton *skeleton = TheGestureMgr->GetSkeletonByEnrollmentIndex(mEnrollmentIndex);
    String str(mName);
    if (TheSkeletonIdentifier->IsAssociatedWithProfile(mEnrollmentIndex)) {
        mName = ThePlatformMgr.GetName(mPadNum);
    } else {
        if (skeleton && skeleton->ProfileMatched()) {
            static Symbol signing_in("signing_in");
            mName = Localize(signing_in, nullptr, TheLocale);
        }
    }
    if (skeleton) {
        int pad = mPadNum;
        if (!TheSkeletonIdentifier->IsAssociatedWithProfile(mEnrollmentIndex)) {
            pad = -1;
        }
        int player = TheGameData->GetPlayerFromSkeleton(*skeleton);
        if (player >= 0) {
            TheGameData->SetAssociatedPadNum(player, pad);
        }
    }
    static Symbol signing_in("signing_in");
    if (mName != "") {
        if (mName != str) {
            if (mName != Localize(signing_in, nullptr, TheLocale)) {
                b3 = true;
            }
        }
    }
    TheProfileMgr.UpdateUsingFitnessState();
    return b3;
}

SkeletonIdentifier::SkeletonIdentifier()
    : mIdentityStatus(kIdentityStatus_None), mTrackingSkelIndex(-1), mIdentifyingPlayerIndex(-1),
      mWaitingPlayerIndex(-1), mCorrectingPlayerIndex(-1), mCorrectionTrackingID(-1),
      mIdentificationTimeout(-1), mDrawDebug(false) {
    TheSkeletonIdentifier = this;
}

SkeletonIdentifier::~SkeletonIdentifier() { TheSkeletonIdentifier = nullptr; }

BEGIN_HANDLERS(SkeletonIdentifier)
    HANDLE_EXPR(toggle_draw_debug, mDrawDebug = !mDrawDebug)
    HANDLE_ACTION(correct_identity, CorrectIdentity(_msg->Int(2)))
    HANDLE_ACTION(set_up_initial_profiles, SetUpInitialProfiles())
    HANDLE_MESSAGE(SkeletonIdentifiedMsg)
    HANDLE_MESSAGE(SkeletonEnrollmentChangedMsg)
    HANDLE_MESSAGE(SigninChangedMsg)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

void SkeletonIdentifier::Init() {
    SetName("skeleton_identifier", ObjectDir::Main());
    TheGestureMgr->AddSink(this, "skeleton_enrollment_changed");
    ThePlatformMgr.AddSink(this, "signin_changed");
    for (int i = 0; i < 8; i++) {
        mEnrolledPlayers[i].mEnrollmentIndex = i;
    }
    UpdateEnrolledPlayers();
}

void SkeletonIdentifier::SetEnrolling() {
    TheGestureMgr->AddSink(this, "skeleton_identified");
}

void SkeletonIdentifier::CorrectIdentity(int i) {
    Skeleton *skel = TheGestureMgr->GetActiveSkeleton();
    if (skel) {
        mCorrectingPlayerIndex = i;
        mCorrectionTrackingID = skel->TrackingID();
    }
}

void SkeletonIdentifier::RequestIdentity() {
    if (mIdentityStatus != 0) {
        MILO_NOTIFY("Already searching for identity");
    } else {
        MILO_ASSERT(!GestureMgr::sIdentityOpInProgress, 0x77);
        mIdentityStatus = kIdentityStatus_Identifying;
    }
}

void SkeletonIdentifier::SearchForIdentity() {
    if (TheGestureMgr->IDEnabled()) {
        MILO_ASSERT(mIdentityStatus == kIdentityStatus_Identifying, 0x81);
        if (!GestureMgr::sIdentityOpInProgress) {
            Skeleton &skel = TheGestureMgr->GetSkeleton(mTrackingSkelIndex);
            if (skel.IsTracked()) {
                TheGestureMgr->AddSink(this, "skeleton_identified");
                skel.RequestIdentity();

            } else {
                mIdentityStatus = kIdentityStatus_None;
            }
        }
    }
}

IdentityStatus SkeletonIdentifier::GetIdentityStatus(int i) {
    if (mIdentityStatus != kIdentityStatus_None) {
        Skeleton &skel = TheGestureMgr->GetSkeleton(mTrackingSkelIndex);
        if (!skel.IsTracked()) {
            mIdentityStatus = kIdentityStatus_None;
        }
    }
    if (mIdentifyingPlayerIndex == i) {
        return mIdentityStatus;
    } else
        return kIdentityStatus_None;
}

void SkeletonIdentifier::NotifyOfRecognition(int i) const {
    bool check = true;
    Skeleton *skel = TheGestureMgr->GetSkeletonByEnrollmentIndex(i);
    if (skel) {
        if (!IsAssociatedWithProfile(i)) {
            check = !skel->ProfileMatched() == 0;
        }
        if (check) {
            int playerID = TheGameData->GetPlayerFromSkeleton(*skel);
            static Symbol p1("p1");
            static Symbol p2("p2");
            static Symbol identity_recognized("identity_recognized");
            static Symbol identification("identification");
            if (0 <= playerID && IsAssociatedWithProfile(i)) {
                String playerName = TheGameData->GetPlayerName(playerID);
                ThePassiveMessenger->TriggerStringMsg(
                    MakeString(
                        Localize(identity_recognized, nullptr, TheLocale), playerName
                    ),
                    playerID == 0 ? p1 : p2,
                    kPassiveMessageGeneral,
                    identification,
                    -1
                );
            }
        }
    }
}

void SkeletonIdentifier::UpdateIdentityStatus() {
    static Symbol identifying("identifying");
    for (int i = 0; i < 2; i++) {
        HamPlayerData *pPlayer = TheGameData->Player(i);
        MILO_ASSERT(pPlayer, 0x146);
        PropertyEventProvider *pProvider = pPlayer->Provider();
        MILO_ASSERT(pProvider, 0x149);
        pProvider->SetProperty(identifying, GetIdentityStatus(i));
    }
}

bool SkeletonIdentifier::IsAssociatedWithProfile(int i1) const {
    int userIndex = mEnrolledPlayers[i1].mPadNum;
    if (userIndex >= 0 && userIndex != 0xFE) {
        MILO_ASSERT(userIndex < user_max_count, 0x1E0);
        return true;
    } else {
        return false;
    }
}

void SkeletonIdentifier::SetUpInitialProfiles() {
    HamPlayerData *p0 = TheGameData->Player(0);
    if (p0->PadNum() < 0) {
        HamPlayerData *p1 = TheGameData->Player(1);
        if (p1->PadNum() < 0) {
            int pad0 = -1;
            int pad1 = -1;
            int i = 0;
            do {
                if (ThePlatformMgr.IsPadNumSignedIn(i)) {
                    if (pad0 == -1) {
                        pad0 = i;
                    } else if (pad1 == -1) {
                        pad1 = i;
                    }
                }
                i++;
            } while (i < 4);
            if (pad0 != -1) {
                TheGameData->SetAssociatedPadNum(0, pad0);
            }
            if (pad1 != -1) {
                TheGameData->SetAssociatedPadNum(1, pad1);
            }
            TheProfileMgr.UpdateUsingFitnessState();
        }
    }
}

void SkeletonIdentifier::UpdateEnrolledPlayers() {
    for (int i = 0; i < 8; i++) {
        NUI_ENROLLMENT_INFORMATION info;
        NuiIdentityGetEnrollmentInformation(i, &info);
        if (info.dwEnrollmentFlags == 0) {
            mEnrolledPlayers[i].mPadNum = -1;
            mEnrolledPlayers[i].mName = gNullStr;
        } else {
            mEnrolledPlayers[i].mPadNum = info.dwUserIndex;
            if (mIdentityStatus == 4 && mWaitingPlayerIndex == i) {
                if (IsAssociatedWithProfile(i)) {
                    mIdentityStatus = kIdentityStatus_None;
                }
            }
            if (mEnrolledPlayers[i].UpdatePlayerBinding()) {
                NotifyOfRecognition(i);
            }
        }
    }
}

void SkeletonIdentifier::Poll() {
    if (TheGestureMgr->IDEnabled()) {
        if (mIdentityStatus == kIdentityStatus_Identifying) {
            SearchForIdentity();
        } else {
            Skeleton *skeleton = TheGestureMgr->GetSkeletonByTrackingID(mCorrectionTrackingID);
            if (skeleton) {
                if (skeleton->EnrollIdentity(mCorrectingPlayerIndex)) {
                    mIdentityStatus = (IdentityStatus)3;
                    SetEnrolling();
                } else {
                    mIdentityStatus = (IdentityStatus)0;
                }
                mCorrectionTrackingID = -1;
                mCorrectingPlayerIndex = -1;
            } else if (!GestureMgr::sIdentityOpInProgress) {
                for (int i = 0; i < NUM_SKELETONS; i++) {
                    Skeleton &skeleton = TheGestureMgr->GetSkeleton(i);
                    int player = TheGameData->GetPlayerFromSkeleton(skeleton);
                    if (skeleton.IsTracked() && skeleton.IsValid()
                        && skeleton.NeedIdentify() && skeleton.TrackingID() != mIdentificationTimeout
                        && player >= 0 && mIdentityStatus == (IdentityStatus)0) {
                        mTrackingSkelIndex = i;
                        mIdentifyingPlayerIndex = player;
                        RequestIdentity();
                        break;
                    }
                }
            }
        }
    }
    UpdateIdentityStatus();
}

SkeletonIdentifiedMsg::SkeletonIdentifiedMsg(int arg1, int arg2) : Message(Type(), arg1, arg2) {}

DataNode SkeletonIdentifier::OnMsg(SkeletonEnrollmentChangedMsg const &msg) {
    UpdateEnrolledPlayers();
    return DATA_UNHANDLED;
}
