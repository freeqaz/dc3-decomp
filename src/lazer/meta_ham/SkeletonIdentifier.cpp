#include "meta_ham/SkeletonIdentifier.h"
#include "SkeletonIdentifier.h"
#include <stdio.h>
#include "flow/PropertyEventProvider.h"
#include "rndobj/Rnd.h"

#ifndef HX_NATIVE
template int sprintf_s<200>(char (&)[200], const char *, ...);
#endif
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
        if (!IsAssociatedWithProfile(i) && !(skel->ProfileMatched() == false)) {
            check = false;
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

DataNode SkeletonIdentifier::OnMsg(SkeletonEnrollmentChangedMsg const &msg) {
    UpdateEnrolledPlayers();
    return DATA_UNHANDLED;
}

static float sLineHeight = 0.03f;
static float sX = 0.1f;
static float sWidth = 0.8f;
static float sBaseY = 0.7f;
static float sHeight = 0.25f;

void SkeletonIdentifier::DrawDebug() {
#ifndef HX_NATIVE
    if (mDrawDebug) {
        static Hmx::Color bgColor(0.2f, 0.2f, 0.2f, 0.7f);
        static Hmx::Color textColor(1.0f, 1.0f, 1.0f, 1.0f);
        Skeleton *activeSkel = TheGestureMgr->GetActiveSkeleton();
        Hmx::Rect rect(sX, sBaseY - 0.05f, sWidth, sHeight + 0.05f);
        TheRnd.DrawRectScreen(rect, bgColor, nullptr, nullptr, nullptr);
        char buf[200];
        for (int i = 0; i < 7; i++) {
            switch (i) {
            case 0:
                if (activeSkel) {
                    if (activeSkel->IsTracked()) {
                        int enrollIdx = activeSkel->GetEnrollmentIndex();
                        sprintf_s<200>(buf, "Active skeleton tracked: %d %s", mEnrolledPlayers[enrollIdx].mPadNum, EnrollmentIndexString(enrollIdx));
                        break;
                    }
                }
                sprintf_s<200>(buf, "Skeleton not tracked");
                break;
            case 1: {
                int secondaryIdx = TheGestureMgr->GetSecondarySkeletonIndex(false);
                if (secondaryIdx < 0) {
                    sprintf_s<200>(buf, "Skeleton not tracked");
                } else {
                    Skeleton &otherSkel = TheGestureMgr->GetSkeleton(secondaryIdx);
                    int enrollIdx = otherSkel.GetEnrollmentIndex();
                    sprintf_s<200>(buf, "Other skeleton tracked: %d %s", mEnrolledPlayers[enrollIdx].mPadNum, EnrollmentIndexString(enrollIdx));
                }
                break;
            }
            case 2:
            case 5:
                buf[0] = '\0';
                break;
            case 3: {
                PropertyEventProvider *provider = TheGameData->Player(0)->Provider();
                int padNum = TheGameData->Player(0)->PadNum();
                Symbol player_name("player_name");
                const char *name = provider->Property(player_name, true)->Str(nullptr);
                sprintf_s<200>(buf, "Player 1: %d %s", padNum, name);
                break;
            }
            case 4: {
                PropertyEventProvider *provider = TheGameData->Player(1)->Provider();
                int padNum = TheGameData->Player(1)->PadNum();
                Symbol player_name("player_name");
                const char *name = provider->Property(player_name, true)->Str(nullptr);
                sprintf_s<200>(buf, "Player 2: %d %s", padNum, name);
                break;
            }
            default:
                sprintf_s<200>(buf, "Identity Status: %i", mIdentityStatus);
                break;
            }
            Vector2 pos(sX, (float)i * sLineHeight + sBaseY);
            TheRnd.DrawStringScreen(buf, pos, textColor, true);
        }
        for (int i = 0; i < 8; i++) {
            Vector2 pos(0.3f, (float)i * sLineHeight + 0.1f);
            const char *str = MakeString("%d. %d %s", i, mEnrolledPlayers[i].mPadNum, mEnrolledPlayers[i].mName);
            TheRnd.DrawStringScreen(str, pos, textColor, true);
        }
    }
#endif
}

DataNode SkeletonIdentifier::OnMsg(const SigninChangedMsg &msg) {
    unsigned int signInMask = msg.mData->Node(3).Int(msg.mData);
    if (signInMask != 0) {
        unsigned int signOutMask = msg.mData->Node(2).Int(msg.mData);
        if (signOutMask != 0) {
            if (mIdentityStatus == kIdentityStatus_WaitingForSignIn) {
                mIdentityStatus = kIdentityStatus_None;
            }

            int signedInPad = -1;
            for (int i = 0; i < 4; i++) {
                if (msg.mData->Node(3).Int(msg.mData) & (1 << i)) {
                    signedInPad = i;
                    break;
                }
            }
            MILO_ASSERT(signedInPad != -1, 0x217);

            if (TheGameData->Player(0)->PadNum() != signedInPad
                && TheGameData->Player(1)->PadNum() != signedInPad) {
                if (TheGameData->Player(0)->PadNum() < 0) {
                    TheGameData->SetAssociatedPadNum(0, signedInPad);
                } else if (TheGameData->Player(1)->PadNum() < 0) {
                    TheGameData->SetAssociatedPadNum(1, signedInPad);
                }
            }
        }
    }

    UpdateEnrolledPlayers();
    TheGameData->UpdateAssociatedPads();

    return DataNode(0);
}

DataNode SkeletonIdentifier::OnMsg(const SkeletonIdentifiedMsg &msg) {
    if (mIdentityStatus != kIdentityStatus_None) {
        TheGestureMgr->RemoveSink(this, "skeleton_identified");
        int enrollmentIdx = msg.GetVal2();
        int skeletonIndex = msg.GetIndex();
        MILO_ASSERT(skeletonIndex >= 0 && skeletonIndex < NUM_SKELETONS, 0x9e);
        if (mIdentityStatus == 1) {
            bool b3 = false;
            if (enrollmentIdx != -2) {
                if (enrollmentIdx == -1) {
                    Skeleton &skel = TheGestureMgr->GetSkeleton(skeletonIndex);
                    if (!skel.ProfileMatched() || !skel.EnrollIdentity(-1)) {
                        b3 = true;
                        mIdentityStatus = kIdentityStatus_None;
                    } else {
                        mIdentityStatus = kIdentityStatus_Enrolling;
                        SetEnrolling();
                    }
                } else {
                    TheGameData->SetAssociatedPadNum(
                        mIdentifyingPlayerIndex, mEnrolledPlayers[enrollmentIdx].mPadNum
                    );
                    mIdentityStatus = kIdentityStatus_None;
                }
                if (!b3) {
                    mIdentificationTimeout = -1;
                    static Message identificationCompleteMsg(
                        "identification_complete", 0, 0
                    );
                    identificationCompleteMsg[0] = enrollmentIdx;
                    identificationCompleteMsg[1] = skeletonIndex;
                    Export(identificationCompleteMsg, true);
                }
            } else {
                b3 = true;
            }
            if (b3) {
                mIdentityStatus = kIdentityStatus_None;
                static Message identificationFailedMsg("identification_failed", 0);
                identificationFailedMsg[0] = enrollmentIdx;
                Export(identificationFailedMsg, true);
                Skeleton &skel = TheGestureMgr->GetSkeleton(skeletonIndex);
                mIdentificationTimeout = skel.TrackingID();
                int skelPlayer = TheGameData->GetPlayerFromSkeleton(skel);
                static Symbol p1("p1");
                static Symbol p2("p2");
                static Symbol identification_failed("identification_failed");
                static Symbol identification("identification");
                ThePassiveMessenger->TriggerStringMsg(
                    Localize(identification_failed, nullptr, TheLocale),
                    skelPlayer == 0 ? p1 : p2,
                    kPassiveMessageGeneral,
                    identification,
                    -1
                );
            }
        } else if (mIdentityStatus == kIdentityStatus_Enrolling
                   || mIdentityStatus == kIdentityStatus_Correcting) {
            mWaitingPlayerIndex = enrollmentIdx;
            UpdateEnrolledPlayers();
            TheGameData->SetAssociatedPadNum(
                mIdentifyingPlayerIndex, mEnrolledPlayers[enrollmentIdx].mPadNum
            );
            Skeleton *skel = TheGestureMgr->GetSkeletonByEnrollmentIndex(enrollmentIdx);
            if (!TheSkeletonIdentifier->IsAssociatedWithProfile(enrollmentIdx) && skel
                && skel->ProfileMatched()
                && TheProfileMgr.GetSignedInProfiles().size() < 4) {
                mIdentityStatus = kIdentityStatus_WaitingForSignIn;
            } else {
                mIdentityStatus = kIdentityStatus_None;
            }
        }
    }
    return DataNode(kDataInt, 0);
}
