#include "hamobj/RhythmBattlePlayer.h"
#include "char/Waypoint.h"
#include "flow/PropertyEventProvider.h"
#include "gesture/GestureMgr.h"
#include "hamobj/HamDirector.h"
#include "hamobj/HamGameData.h"
#include "hamobj/HamLabel.h"
#include "hamobj/HamPlayerData.h"
#include "math/Easing.h"
#include "obj/Dir.h"
#include "obj/Msg.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "os/Debug.h"
#include "rndobj/Anim.h"
#include "rndobj/PartLauncher.h"
#include "rndobj/Poll.h"
#include "utl/Loader.h"
#include "utl/Symbol.h"
#include "world/Dir.h"

RhythmBattlePlayer::RhythmBattlePlayer()
    : mComboPosAnim(this), mComboColorAnim(this), mResetComboAnim(this),
      m2xMultAnim(this), m3xMultAnim(this), m4xMultAnim(this), mRhythmBattleAnim(this),
      unk94(this), mBattleMeterStaleAnim(this), mBattleMeterInAnim(this),
      mShowScoreAnim(this), mBattleMeterOutAnim(this), mBoxyDir(this), unk10c(this),
      mScoreLabel(this), mInTheZoneFlow(this), mOutTheZoneOkFlow(this),
      mOutTheZoneBadFlow(this), mSwagJackedFlow(this), mPhraseMeter(this),
      mTransConstraint(this), mBoxyWaistTrans(this), mBoxyman1(this), mBoxyman2(this),
      mTextFeedback(this), mMoveFeedback(this), mStealPart(this), mStealAnim(this),
      mPlayer(0), unk23c(0), unk244(0), unk248(0), unk250(0), unk258(0), unk25c(0),
      unk260(0), mInTheZone(-2), unk270(0), unk274(0), unk280(0), unk284(0),
      unk288(false), unk294(-1), unk298("none"), unk29c(0), unk2a4(false), unk2a5(false),
      unk2a8(0) {}

RhythmBattlePlayer::~RhythmBattlePlayer() {}

BEGIN_HANDLERS(RhythmBattlePlayer)
    HANDLE_SUPERCLASS(RndPollable)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(RhythmBattlePlayer)
    SYNC_PROP(score_label, mScoreLabel)
    SYNC_PROP(combo_position_anim, mComboPosAnim)
    SYNC_PROP(combo_color_anim, mComboColorAnim)
    SYNC_PROP(reset_combo_anim, mResetComboAnim)
    SYNC_PROP(2x_mult_anim, m2xMultAnim)
    SYNC_PROP(3x_mult_anim, m3xMultAnim)
    SYNC_PROP(4x_mult_anim, m4xMultAnim)
    SYNC_PROP(player, mPlayer)
    SYNC_PROP(in_anim, mBattleMeterInAnim)
    SYNC_PROP(out_anim, mBattleMeterOutAnim)
    SYNC_PROP(boxydir, mBoxyDir)
    SYNC_SUPERCLASS(RndPollable)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(RhythmBattlePlayer)
    SAVE_REVS(1, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    bs << mComboPosAnim;
    bs << mComboColorAnim;
    bs << mResetComboAnim;
    bs << m2xMultAnim;
    bs << m3xMultAnim;
    bs << m4xMultAnim;
    unk10c = nullptr;
    bs << unk10c;
    mScoreLabel = nullptr;
    bs << mScoreLabel;
    bs << mPlayer;
    bs << mBoxyDir;
END_SAVES

BEGIN_COPYS(RhythmBattlePlayer)
    COPY_SUPERCLASS(Hmx::Object)
    CREATE_COPY_AS(RhythmBattlePlayer, c)
    BEGIN_COPYING_MEMBERS_FROM(c)
        COPY_MEMBER(mComboPosAnim)
        COPY_MEMBER(mComboColorAnim)
        COPY_MEMBER(mResetComboAnim)
        COPY_MEMBER(m2xMultAnim)
        COPY_MEMBER(m3xMultAnim)
        COPY_MEMBER(m4xMultAnim)
        COPY_MEMBER(mPlayer)
        COPY_MEMBER(mScoreLabel)
        COPY_MEMBER(mBattleMeterInAnim)
        COPY_MEMBER(mShowScoreAnim)
        COPY_MEMBER(mBattleMeterOutAnim)
        COPY_MEMBER(mPhraseMeter)
    END_COPYING_MEMBERS
END_COPYS

BEGIN_LOADS(RhythmBattlePlayer)
    LOAD_REVS(bs)
    ASSERT_REVS(1, 0)
    LOAD_SUPERCLASS(Hmx::Object)
    d >> mComboPosAnim;
    d >> mComboColorAnim;
    d >> mResetComboAnim;
    d >> m2xMultAnim;
    d >> m3xMultAnim;
    d >> m4xMultAnim;
    d >> unk10c;
    unk10c = nullptr;
    d >> mScoreLabel;
    mScoreLabel = nullptr;
    d >> mPlayer;
    if (d.rev >= 1)
        d >> mBoxyDir;
END_LOADS

void RhythmBattlePlayer::Enter() {
    RndPollable::Enter();
    mBattleMeterStaleAnim = Dir()->Find<RndAnimatable>(
        mPlayer != 0 ? "battle_meter_L_stale.anim" : "battle_meter_R_stale.anim", false
    );
    mBattleMeterInAnim = Dir()->Find<RndAnimatable>(
        mPlayer != 0 ? "battle_meter_l_in.anim" : "battle_meter_r_in.anim", false
    );
    mBattleMeterOutAnim = Dir()->Find<RndAnimatable>(
        mPlayer != 0 ? "battle_meter_l_out.anim" : "battle_meter_r_out.anim", false
    );
    mShowScoreAnim = Dir()->Dir()
        ? Dir()->Dir()->Find<RndAnimatable>(
              mPlayer != 0 ? "show_left_score.anim" : "show_right_score.anim", false
          )
        : nullptr;
    ObjectDir *rhythmBattleDir = Dir()->Find<ObjectDir>("rhythmbattle_audio", false);
    if (rhythmBattleDir) {
        mInTheZoneFlow = rhythmBattleDir->Find<Flow>(
            mPlayer != 0 ? "inthezone_p2.flow" : "inthezone_p1.flow", false
        );
        mOutTheZoneOkFlow = rhythmBattleDir->Find<Flow>(
            mPlayer != 0 ? "outthezone_ok_p2.flow" : "outthezone_ok_p1.flow", false
        );
        mOutTheZoneBadFlow = rhythmBattleDir->Find<Flow>(
            mPlayer != 0 ? "outthezone_bad_p2.flow" : "outthezone_bad_p1.flow", false
        );
        mSwagJackedFlow = rhythmBattleDir->Find<Flow>(
            mPlayer != 0 ? "swag_jacked_p2.flow" : "swag_jacked_p1.flow", false
        );
    }
    unk240 = false;
    unk94 = nullptr;
    mTransConstraint = nullptr;
    if (!mBoxyDir && TheHamDirector) {
        WorldDir *wdir = TheHamDirector->GetVenueWorld();
        if (wdir) {
            mBoxyDir = wdir->Find<RndDir>("boxyman", false);
        }
    }
    if (mBoxyDir) {
        mTransConstraint = mBoxyDir->Find<TransConstraint>(
            mPlayer != 0 ? "TransConstraint1.tc" : "TransConstraint.tc", false
        );
        mPhraseMeter = mBoxyDir->Find<HamPhraseMeter>(
            mPlayer != 0 ? "phrase_meter1" : "phrase_meter0", false
        );
        mBoxyman1 = mBoxyDir->Find<DepthBuffer3D>(
            mPlayer != 0 ? "boxyman1_p2.db3d" : "boxyman1_p1.db3d", false
        );
        mBoxyman2 = mBoxyDir->Find<DepthBuffer3D>(
            mPlayer != 0 ? "boxyman2_p2.db3d" : "boxyman2_p1.db3d", false
        );
        mRhythmBattleAnim = mBoxyDir->Find<RndAnimatable>(
            mPlayer != 0 ? "rhythmbattle_p2.anim" : "rhythmbattle_p1.anim", false
        );
        mBoxyWaistTrans = mBoxyDir->Find<RndTransformable>(
            mPlayer != 0 ? "boxywaist_p2.trans" : "boxywaist_p1.trans", false
        );
        mStealPart = mBoxyDir->Find<RndParticleSys>(
            mPlayer != 0 ? "steal_p2.part" : "steal_p1.part", false
        );
        mStealAnim = mBoxyDir->Find<RndAnimatable>(
            mPlayer != 0 ? "steal_p2.anim" : "steal_p1.anim", false
        );
        mTextFeedback = mBoxyDir->Find<ObjectDir>(
            mPlayer != 0 ? "text_feedback1" : "text_feedback0", false
        );
        mMoveFeedback = mBoxyDir->Find<ObjectDir>(
            mPlayer != 0 ? "move_feedback1" : "move_feedback0", false
        );
        if (mStealPart && !TheLoadMgr.EditMode()) {
            mStealPart->SetEmitRate(0, 0);
        }
        if (mBoxyman1) {
            DepthBuffer3DAttachment attachment;
            attachment.obj = mBoxyWaistTrans;
            attachment.player = mPlayer;
            attachment.unk8 = 0;
            attachment.unkc = Vector3::ZeroVec();
            attachment.unk1c = 0;
            attachment.unk20 = 0;
            mBoxyman1->AddAttachment(attachment);
        }
    }
    AnimateBoxyState(-1, false, false);
}

int RhythmBattlePlayer::SwagJacked(Hmx::Object *, RhythmBattleJackState) {
    MILO_ASSERT(mInTheZone == 1, 0x44a);
    static Symbol rhythmbattle_swagjackeddd1("rhythmbattle_swagjackeddd1");
    unk298 = rhythmbattle_swagjackeddd1;
    unk260 = 0;
    if (mSwagJackedFlow)
        mSwagJackedFlow->Activate();
    return 1;
}

void RhythmBattlePlayer::HackPlayerQuit() {
    HamPlayerData *player = TheGameData->Player(mPlayer);
    int trackingID = player->GetSkeletonTrackingID();
    int skelIdx = TheGestureMgr->GetSkeletonIndexByTrackingID(trackingID);
    if (skelIdx != -1 && mShowScoreAnim) {
        mShowScoreAnim->Animate(
            mShowScoreAnim->GetFrame(),
            mShowScoreAnim->EndFrame(),
            mShowScoreAnim->Units(),
            0,
            0,
            nullptr,
            kEaseLinear,
            0,
            false
        );
    }
}

int RhythmBattlePlayer::InTheZone() const { return mInTheZone == 1; }

float RhythmBattlePlayer::InAnimBeatLength() const { return 4.0f; }

void RhythmBattlePlayer::SetWindow(float f1, float f2) {
    unk28c = f1;
    unk290 = f2;
}

void RhythmBattlePlayer::SetInTheZone(int i, bool b1, bool b2) {
    if (!unk240) {
        i = -1;
    }
    if (mInTheZone != i) {
        AnimateBoxyState(i, b1, b2);
    }
}

void RhythmBattlePlayer::SetActive(bool b1) {
    unk240 = b1;
    if (!unk23c)
        return;
    // unk23c is a ptr
    if (mInTheZone == -1)
        return;
    AnimateBoxyState(-1, true, false);
}

void RhythmBattlePlayer::AnimateIn() { AnimateBoxyState(0, true, false); }

bool RhythmBattlePlayer::UpdateState() {
    unk26c = mInTheZone;
    unk264 = unk260;
    if (unk270 < 0.5f) {
        unk260 = 0;
    } else if (unk274 >= 0.6f) {
        unk260 = 1;
    } else if (unk260 <= 1) {
        unk260 = 2;
    }
    return unk264 != unk260;
}

void RhythmBattlePlayer::ResetCombo() {
    int i = 0;
    if (!unk240)
        i = -1;

    if (mInTheZone != i)
        AnimateBoxyState(i, false, false);

    unk264 = 0;
    unk2a8 = 0;
    unk26c = mInTheZone;
    unk284 = 0.0f;
}

void RhythmBattlePlayer::SwagJackedBonus(Hmx::Object *, RhythmBattleJackState, int i) {
    if (mStealAnim) {
        mStealAnim->Animate(
            mStealAnim->StartFrame(),
            mStealAnim->EndFrame(),
            mStealAnim->Units(),
            0,
            0,
            nullptr,
            kEaseLinear,
            0,
            0
        );
    }
    static Symbol swag_jacked("swag_jacked");
    TheGameData->Player(mPlayer)->Provider()->Export(Message(swag_jacked), true);
    static Symbol rhythmbattle_swagjackeddd2("rhythmbattle_swagjackeddd2");
    unk298 = rhythmbattle_swagjackeddd2;
    if (i != 0) {
        unk260 = 1;
        unk284 = 16.0f;
    }
}

void RhythmBattlePlayer::SwapObjs(RhythmBattlePlayer *player) {
    RndAnimatable *temp = player->mComboPosAnim;
    player->mComboPosAnim = mComboPosAnim;
    mComboPosAnim = temp;

    temp = player->mComboColorAnim;
    player->mComboColorAnim = mComboColorAnim;
    mComboColorAnim = temp;

    temp = player->mResetComboAnim;
    player->mResetComboAnim = mResetComboAnim;
    mResetComboAnim = temp;

    temp = player->m2xMultAnim;
    player->m2xMultAnim = m2xMultAnim;
    m2xMultAnim = temp;

    temp = player->m3xMultAnim;
    player->m3xMultAnim = m3xMultAnim;
    m3xMultAnim = temp;

    temp = player->m4xMultAnim;
    player->m4xMultAnim = m4xMultAnim;
    m4xMultAnim = temp;

    temp = player->mBattleMeterStaleAnim;
    player->mBattleMeterStaleAnim = mBattleMeterStaleAnim;
    mBattleMeterStaleAnim = temp;

    temp = player->mBattleMeterInAnim;
    player->mBattleMeterInAnim = mBattleMeterInAnim;
    mBattleMeterInAnim = temp;

    temp = player->mShowScoreAnim;
    player->mShowScoreAnim = mShowScoreAnim;
    mShowScoreAnim = temp;

    temp = player->mBattleMeterOutAnim;
    player->mBattleMeterOutAnim = mBattleMeterOutAnim;
    mBattleMeterOutAnim = temp;

    HamLabel *tempLabel = player->unk10c;
    player->unk10c = unk10c;
    unk10c = tempLabel;

    tempLabel = player->mScoreLabel;
    player->mScoreLabel = mScoreLabel;
    mScoreLabel = tempLabel;

    temp = player->unk94;
    player->unk94 = unk94;
    unk94 = temp;

    unk288 = !unk288;
    player->unk288 = !player->unk288;
}

void RhythmBattlePlayer::UpdateScore(int i1) {
    unk280 += (InTheZone() + 1) * i1;
    static Symbol rhythm_battle("rhythm_battle");
    static Symbol gameplay_mode("gameplay_mode");
    if (TheHamProvider->Property(gameplay_mode)->Sym() == rhythm_battle) {
        static Symbol score("score");
        HamPlayerData *hpd = TheGameData->Player(mPlayer);
        hpd->Provider()->SetProperty(score, unk280);
    }
}
