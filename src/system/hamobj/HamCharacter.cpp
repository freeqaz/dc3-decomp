#include "hamobj\HamCharacter.h"
#include "HamCharacter.h"
#include "utl\Str.h"
#include "HamRegulate.h"
#include "char\CharClip.h"
#include "char\CharEyes.h"
#include "char\CharFaceServo.h"
#include "char\CharLipSync.h"
#include "char\CharLipSyncDriver.h"
#include "char\CharServoBone.h"
#include "char\CharWeightable.h"
#include "char\Character.h"
#include "char\FileMerger.h"
#include "char\Waypoint.h"
#include "hamobj\HamDriver.h"
#include "hamobj\HamGameData.h"
#include "math\Mtx.h"
#include "math\Rot.h"
#include "obj\Data.h"
#include "obj\DataUtl.h"
#include "obj\Dir.h"
#include "obj/DirLoader.h"
#include "obj\Msg.h"
#include "obj/Object.h"
#include "obj\Utl.h"
#include "os\Debug.h"
#include "os\File.h"
#include "os\System.h"
#include "rndobj\Anim.h"
#include "rndobj\Draw.h"
#include "rndobj\TexBlender.h"
#include "rndobj\Trans.h"
#include "synth\Sound.h"
#include "synth\Synth.h"
#include "obj/Task.h"
#include "utl/BinStream.h"
#include "utl\FilePath.h"
#include "utl/Loader.h"
#include "utl\Symbol.h"

bool HamCharacter::sLoadVO = true;
CharClip *HamCharacter::sSkeletonClips[HamCharacter::kNumSkeletons];

extern "C" char *_strlwr(char *);

namespace {
    const char *kCrewCardMeshName = "crew_card.mesh";
}

String mCampaignVO;

HamCharacter::HamCharacter()
    : mCampaignVOBank(0), mCampaignVODir(0), mFileMerger(0), mIsCampaignChar(0), mShowBox(0),
      mNeedsAcquirePose(1), mEyes(this), mGender(kHamFemale), mAnimationState(0), mPollWhenHidden(0),
      mTexBlendersActive(1), mIKEffectors(this), mBaseLipsyncOffset(0), mNeutralSkelDir(0),
      mSkeletonBones(0), mCrewCardMesh(nullptr), mUseCameraSkeleton(0) {
    mWaypoint = Hmx::Object::New<Waypoint>();
    mWaypoint->SetAngRadius(0);
    mWaypoint->SetRadius(36);
    mWaypoint->SetYRadius(36);
    mWaypoint->SetStrictRadiusDelta(0.01);
    const char *path = "";
    DataArray *cfg = SystemConfig("objects", "HamCharacter");
    if (cfg->FindData("skeleton_path", path, false) && *path != '\0') {
        FilePathTracker tracker(path);
        mNeutralSkelDir =
            DirLoader::LoadObjects("neutral_skeleton.milo", nullptr, nullptr);
        MILO_ASSERT(mNeutralSkelDir, 0x7D);
        mSkeletonBones =
            mNeutralSkelDir->Find<CharServoBone>("skeleton_bones.servo", true);
        MILO_ASSERT(mSkeletonBones, 0x7F);
    }
}

HamCharacter::~HamCharacter() {
    if (TheSynth) {
        TheSynth->RemovePlayHandler(this);
    }
#ifdef HX_NATIVE
    if (!ObjectDir::InDeleteObjects())
#endif
    delete mWaypoint;
}

BEGIN_HANDLERS(HamCharacter)
    HANDLE(configure_file_merger, OnConfigureFileMerger)
    HANDLE_ACTION(start_load, StartLoad(_msg->Int(2)))
    HANDLE(cam_teleport, OnCamTeleport)
    HANDLE(post_delete, OnPostDelete)
    HANDLE_ACTION(set_lipsync_offset, SetLipsyncOffset(_msg->Float(2)))
    HANDLE(sound_play, OnSoundPlay)
    HANDLE_ACTION(
        enable_facial_animation,
        EnableFacialAnimation(_msg->Obj<CharLipSync>(2), _msg->Float(3))
    )
    HANDLE_ACTION(set_blinking, SetBlinking(_msg->Int(2)))
    HANDLE_EXPR(crew_card_found, Find<RndMesh>(kCrewCardMeshName, false))
    HANDLE_ACTION(set_campaign_vo, SetCampaignVo(_msg->Str(2)))
    HANDLE_EXPR(get_campaign_vo_bank, mCampaignVOBank)
    HANDLE(toggle_interests_overlay, OnToggleInterestDebugOverlay)
    HANDLE_SUPERCLASS(Character)
END_HANDLERS

BEGIN_PROPSYNCS(HamCharacter)
    SYNC_PROP(outfit, mOutfit)
    SYNC_PROP(outfit_dir, mOutfitDir)
    SYNC_PROP(show_box, mShowBox)
    SYNC_PROP(gender, (int &)mGender)
    SYNC_PROP_SET(force_blink, true, if (_val.Int()) ForceBlink())
    SYNC_PROP_SET(enable_auto_blinks, true, EnableBlinks(_val.Int(), false))
    SYNC_PROP_SET(
        force_lookat,
        mEyes->GetCurrentInterest() ? Symbol(mEyes->GetCurrentInterest()->Name())
                                    : Symbol(),
        SetFocusInterest(_val.Sym(), 0)
    )
    SYNC_PROP(poll_when_hidden, mPollWhenHidden)
    SYNC_PROP_MODIFY(
        tex_blenders_active, mTexBlendersActive, SetTexBlendersActive(mTexBlendersActive)
    )
    // The 12th SyncProperty row is here, not in SetPropShowing: the image masks
    // the materialised bool with `clrlwi r11, r11, 24` at 82490B88 before
    // storing it into the DataNode, even though its own `li r11, 0x1` /
    // `li r11, 0x0` pair (82490B78 / 82490B84) makes the mask redundant; we
    // drop it.  MEASURED (w9-f): the mask IS reachable -- spelling this getter
    // `mCrewCardMesh ? mCrewCardMesh->Showing() : false` emits it -- but the
    // ternary replaces the `li r11, 0x1` with a `b` and costs four rows net
    // (97.93 -> 97.71), because MSVC then merges the two arms instead of
    // computing 0/1 in place.  Not worth chasing on its own: the other 11 rows
    // are the SetPropShowing cross-jump above, so closing the mask alone cannot
    // cross this function.
    SYNC_PROP_SET(
        crew_card_showing,
        mCrewCardMesh && mCrewCardMesh->Showing(),
        bool showCrewCard = _val.Int();
        if (mCrewCardMesh) mCrewCardMesh->SetShowing(showCrewCard)
    )
    SYNC_PROP_SET(prop_0_showing, GetPropShowing(0), SetPropShowing(0, _val.Int()))
    SYNC_PROP_SET(prop_1_showing, GetPropShowing(1), SetPropShowing(1, _val.Int()))
    SYNC_PROP_SET(prop_2_showing, GetPropShowing(2), SetPropShowing(2, _val.Int()))
    SYNC_PROP_SET(prop_3_showing, GetPropShowing(3), SetPropShowing(3, _val.Int()))
    SYNC_SUPERCLASS(Character)
END_PROPSYNCS

BEGIN_SAVES(HamCharacter)
    SAVE_REVS(3, 0)
    SAVE_SUPERCLASS(Character)
    bs << mOutfit;
    bs << mOutfitDir;
    bs << mShowBox;
    bs << mPollWhenHidden;
    bs << mTexBlendersActive;
END_SAVES

BEGIN_COPYS(HamCharacter)
    COPY_SUPERCLASS(Character)
    CREATE_COPY(HamCharacter)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mOutfit)
        COPY_MEMBER(mShowBox)
        COPY_MEMBER(mOutfitDir)
        COPY_MEMBER(mGender)
        COPY_MEMBER(mPollWhenHidden)
        COPY_MEMBER(mTexBlendersActive)
    END_COPYING_MEMBERS
END_COPYS

BEGIN_LOADS(HamCharacter)
    PreLoad(bs);
    PostLoad(bs);
END_LOADS

INIT_REVS(3, 0)

void HamCharacter::PreLoad(BinStream &bs) {
    LOAD_REVS(bs)
    ASSERT_REVS(3, 0)
    Character::PreLoad(bs);
    Reserve((mHashTable.UsedSize() + 20) * 2, mStringTable.UsedSize() + 0x1B8);
    bs.PushRev(packRevs(d.altRev, d.rev), this);
}

void HamCharacter::PostLoad(BinStream &bs) {
    BinStreamRev d(bs, bs.PopRev(this));
    Character::PostLoad(bs);
    if (gLoadingProxyFromDisk) {
        Symbol s;
        d >> s;
    } else {
        d >> mOutfit;
    }
    if (d.rev > 0) {
        d >> mOutfitDir;
    }
    if (d.rev > 1) {
        d >> mShowBox;
    }
    if (d.rev > 2) {
        d >> mPollWhenHidden;
        bool active;
        d >> active;
        SetTexBlendersActive(active);
    }
}

void HamCharacter::SyncObjects() {
    const char *meshes[2] = { "bone_pelvis.mesh", "spot_neck.mesh" };
    for (int i = 0; i < 2; i++) {
        RndTransformable *t = Find<RndTransformable>(meshes[i], false);
        if (t) {
            t->SetTransParent(this, false);
        }
    }
    if (mNeedsAcquirePose && BoneServo()) {
        mNeedsAcquirePose = false;
        BoneServo()->AcquirePose();
    }
    SetTexBlendersActive(mTexBlendersActive);
    Character::SyncObjects();
    if (Find<CharLipSyncDriver>("face.lipdrv", false)) {
        CharFaceServo *servo = Find<CharFaceServo>("face.faceservo", false);
        CharLipSyncDriver *lipDrv = Find<CharLipSyncDriver>("face.lipdrv", false);
        EnableFacialAnimation(lipDrv->LipSync(), 0);
        // BEHAVIOURAL FIX (w8-n).  We had `&&` between the two Null() tests; the
        // image's predicate is `!left.Null() || right.Null()`.  0x82491884..
        // 0x824918C4: `beq` on a null servo goes to `li r4, 0`; `bne` on
        // `left != gNullStr` (0xa8) goes STRAIGHT to `li r11, 0x1`, which is a
        // short-circuit TRUE, not a fall-through into the second test; only the
        // left==null path reads 0xb0, and there `bne` (right != gNullStr) selects
        // `li r11, 0x0` while the fall-through selects `li r11, 0x1`.
        // RESIDUAL (w8-n, 98.2990 canonical): 1 cause, 3 rows.  After the `||`
        // merges r11 the image RE-NORMALISES it -- `clrlwi. r11, r11, 24` / `li r4, 0x1` /
        // `bne .L_824918C4`, sharing the `li r4, 0x0` at 0x824918C0 with the else
        // arm -- where we mask straight into the argument (`clrlwi r4, r11, 24`)
        // and jump over it.
        // Measured, as run_objdiff's canonical headline (1 d.p. as printed):
        //     bool blinking; if (servo) {...} else { blinking = false; }  98.3 <- this
        //     SetBlinking(servo && (...))                                96.3
        //     bool blinking = servo && (...)                             95.2
        //     SetBlinking(servo ? (...) : false)                         95.2
        //     bool blinking = false; if (servo) blinking = ...           95.8
        //     if (!servo) blinking = false; else blinking = ...          94.7
        //     ... with an extra `bool namesOk = ...; blinking = namesOk;` INERT
        bool blinking;
        if (servo) {
            blinking = !servo->BlinkClipLeftName().Null()
                || servo->BlinkClipRightName().Null();
        } else {
            blinking = false;
        }
        SetBlinking(blinking);
    }
    mCrewCardMesh = Find<RndMesh>(kCrewCardMeshName, false);
}

void HamCharacter::Draw() {
    if (!mShowing && !mTexBlendersActive) {
        for (ObjDirItr<RndTexBlender> it(this, true); it != nullptr; ++it) {
            it->DrawShowing();
        }
    }
    RndDrawable::Draw();
}

void HamCharacter::DrawShowing() {
    Character::DrawShowing();
    if (mShowBox) {
        mWaypoint->Highlight();
    }
}

void HamCharacter::Enter() {
    Character::Enter();
    if (BoneServo()) {
        BoneServo()->SetRegulateWaypoint(nullptr);
    }
    if (Regulator()) {
        Regulator()->SetWaypoint(nullptr);
    }
    mAnimationState = 0;
    TheSynth->AddPlayHandler(this);
}

void HamCharacter::Exit() {
    if (TheSynth) {
        TheSynth->RemovePlayHandler(this);
    }
    Character::Exit();
}

void HamCharacter::AddedObject(Hmx::Object *obj) {
    Character::AddedObject(obj);
    static Symbol HamIKEffector("HamIKEffector");
    Symbol className = obj->ClassName();
    if (streq(obj->Name(), "char.fm")) {
        mFileMerger = dynamic_cast<FileMerger *>(obj);
    } else if (streq(obj->Name(), "CharEyes.eyes")) {
        mEyes = dynamic_cast<CharEyes *>(obj);
    } else if (className == HamIKEffector) {
        mIKEffectors.push_back(dynamic_cast<CharWeightable *>(obj));
    }
}

void HamCharacter::RemovingObject(Hmx::Object *obj) {
    Character::RemovingObject(obj);
    if (obj == mFileMerger) {
        mFileMerger = nullptr;
    }
}

void HamCharacter::Init() {
    REGISTER_OBJ_FACTORY(HamCharacter);
    TheDebug.AddExitCallback(HamCharacter::Terminate);
    const char *path = "";
    DataArray *cfg = SystemConfig("objects", "HamCharacter");
    static Symbol CHARCLIP_SKELETONS("CHARCLIP_SKELETONS");
    DataArray *macro = DataGetMacro(CHARCLIP_SKELETONS);
    if (macro) {
        if (cfg->FindData("skeleton_path", path, false) && *path != '\0') {
            FilePathTracker tracker(path);
            int numSkels = macro->Size();
            MILO_ASSERT(numSkels == kNumSkeletons, 0x42);
            ObjectDir *clips =
                DirLoader::LoadObjects("skeleton_clips.milo", nullptr, nullptr);
            MILO_ASSERT(clips, 0x45);
#ifdef HX_NATIVE
            if (!clips) return;
#endif
            for (int i = 0; i < numSkels; i++) {
                Symbol s = macro->Sym(i);
                sSkeletonClips[i] =
                    clips->Find<CharClip>(MakeString("%s_skeleton", s), true);
                MILO_ASSERT(sSkeletonClips[i], 0x4C);
            }
        }
    }
}

void HamCharacter::Terminate() {
    for (int i = 0; i < kNumSkeletons; i++) {
        delete sSkeletonClips[i];
    }
}

String HamCharacter::GetCampaignVo() { return mCampaignVO; }

void HamCharacter::StartLoad(bool start) {
    if (!mFileMerger->StartLoad(start)) {
        SyncObjects();
    }
}

void HamCharacter::SetOutfit(Symbol outfit) { mOutfit = outfit; }
void HamCharacter::SetOutfitDir(Symbol outfitDir) { mOutfitDir = outfitDir; }

void HamCharacter::UnloadAll() {
    if (mFileMerger)
        mFileMerger->Clear();
}

String HamCharacter::GetCampaignVoMilo() {
    return MakeString("sfx/loc/eng/campaign/%s.milo", mCampaignVO);
}

void HamCharacter::SetTexBlendersActive(bool active) {
    mTexBlendersActive = active;
    for (ObjDirItr<RndTexBlender> it(this, true); it != nullptr; ++it) {
        it->SetShowing(active);
    }
}

bool HamCharacter::InClipTest() {
    if (TheLoadMgr.EditMode() && streq(Dir()->Name(), "clip_test")) {
        return true;
    } else
        return false;
}

void HamCharacter::SetIKEffectorWeights(float weight) {
    FOREACH (it, mIKEffectors) {
        CharWeightable *cw = *it;
        if (cw) {
            cw->SetWeight(weight);
        }
    }
}

void HamCharacter::ResyncLipSync(CharLipSync *sync) {
    CharLipSyncDriver *driver = Find<CharLipSyncDriver>("face.lipdrv", false);
    if (driver) {
        if (sync) {
            driver->SetLipSync(sync);
        }
        driver->Sync();
    }
}

void HamCharacter::PlayBaseViseme() {
    ObjectDir *visemeDir = Find<ObjectDir>("viseme", false);
    if (visemeDir) {
        CharFaceServo *servo = Find<CharFaceServo>("face.faceservo", false);
        if (servo) {
            servo->SetClips(visemeDir);
            CharClip *clip = servo->BaseClip();
            if (clip) {
                clip->PoseMeshes(this, clip->StartBeat());
            }
        }
    }
}

void HamCharacter::EnableFacialAnimation(CharLipSync *sync, float f2) {
    MILO_LOG(
        "HamCharacter::EnableFacialAnimation Name:%s lipsync name:%s\n",
        Name(),
        SafeName(sync)
    );
    ObjectDir *visemeDir = Find<ObjectDir>("viseme", false);
    if (visemeDir && !visemeDir->Find<CharClip>("Base", false)) {
        return;
    }
    mBaseLipsyncOffset = f2;
    CharLipSyncDriver *driver = Find<CharLipSyncDriver>("face.lipdrv", false);
    if (sync && driver) {
        if (!driver->SetLipSync(sync)) {
            driver->Sync();
        }
        driver->SetSongOffset(f2);
        String str(FileGetBase(sync->Name()));
        str += ".anim";
        RndAnimatable *anim = sync->Dir()->Find<RndAnimatable>(str.c_str(), false);
        if (anim) {
            static Symbol animate("animate");
            anim->Handle(Message(animate), true);
        }
    }
}

void HamCharacter::DisableFacialAnimation() {
    CharLipSyncDriver *driver = Find<CharLipSyncDriver>("face.lipdrv", false);
    if (driver) {
        driver->SetLipSync(nullptr);
        driver->Sync();
    }
}

void HamCharacter::ResetFacialAnimation() {
    MILO_LOG("HamCharacter::ResetFacialAnimation() Name:%s\n", Name());
    CharLipSyncDriver *driver = Find<CharLipSyncDriver>("face.lipdrv", false);
    if (driver) {
        driver->ClearLipSync();
    }
}

void HamCharacter::SetBlinking(bool blinking) {
    CharLipSyncDriver *driver = Find<CharLipSyncDriver>("face.lipdrv", false);
    ObjectDir *visemeDir = Find<ObjectDir>("viseme", false);
    if (visemeDir) {
        CharFaceServo *servo = Find<CharFaceServo>("face.faceservo", false);
        if (servo) {
            servo->SetBlinkClipLeft(blinking ? "Blink" : "");
            servo->SetBlinkClipRight(blinking ? "Blink" : "");
            servo->SetClips(visemeDir);
        }
        if (driver && visemeDir->Find<CharClip>("Base", false)) {
            driver->SetClips(visemeDir);
        }
    }
}

void HamCharacter::BlendInFaceOverrides(float f1) {
    CharLipSyncDriver *driver = Find<CharLipSyncDriver>("face.lipdrv", false);
    if (driver) {
        driver->BlendInOverrides(f1);
    }
}

void HamCharacter::BlendOutFaceOverrides(float f1) {
    CharLipSyncDriver *driver = Find<CharLipSyncDriver>("face.lipdrv", false);
    if (driver) {
        driver->BlendOutOverrides(f1);
    }
}

void HamCharacter::SetLipsyncOffset(float offset) {
    CharLipSyncDriver *driver = Find<CharLipSyncDriver>("face.lipdrv", false);
    if (driver) {
        driver->SetSongOffset(mBaseLipsyncOffset + offset);
    }
}

void HamCharacter::SetFaceOverrideWeight(float weight) {
    CharLipSyncDriver *driver = Find<CharLipSyncDriver>("face.lipdrv", false);
    if (driver) {
        driver->SetOverrideWeight(weight);
    }
}

float HamCharacter::GetFaceOverrideWeight() {
    CharLipSyncDriver *driver = Find<CharLipSyncDriver>("face.lipdrv", false);
    return driver ? driver->GetOverrideWeight() : 0;
}

void HamCharacter::SetUseCameraSkeleton(bool use) {
    mUseCameraSkeleton = use;
    if (mUseCameraSkeleton) {
        SetIKEffectorWeights(0);
    } else {
        SetIKEffectorWeights(1);
    }
}

Symbol HamCharacter::GetFaceOverrideClip() {
    CharLipSyncDriver *driver = Find<CharLipSyncDriver>("face.lipdrv", false);
    // NESTED ifs with a named local, not `if (driver && driver->OverrideClip())`.
    // The image tests the clip with `cmplwi cr6, r11, 0x0` -- UNSIGNED, the same
    // form it uses for the `driver` test one row earlier.  Inside an && chain
    // MSVC gives the SECOND operand the signed `cmpwi`, and an explicit
    // `(unsigned int)driver->OverrideClip() != 0` there does not move it.
    if (driver) {
        CharClip *clip = driver->OverrideClip();
        if (clip)
            return clip->Name();
    }
    return Symbol();
}

void HamCharacter::ResetFaceOverrideBlending() {
    CharLipSyncDriver *driver = Find<CharLipSyncDriver>("face.lipdrv", false);
    if (driver) {
        driver->ResetOverrideBlend();
    }
}

// 100% (w8-n).  Three levers, in the order they paid:
//  1. `int hasVO = !mCampaignVO.empty(); if (hasVO)` materialises the bool
//     (`subic`/`subfe.`) where the image just tests it: 94.4615 -> 96.9.
//  2. DirLoader::LoadObjects takes `const FilePath&`; letting the `const char*`
//     convert implicitly makes MSVC re-materialise the temp's address
//     (`addi r3, r31, 0x60`) after the ctor already returned it in r3.  Writing
//     the temporary explicitly reuses the ctor's r3: 96.9 -> 98.5.
//  3. RELEASE(x) is `(delete x, x = null)`; MSVC folds the comma form's null
//     test into `delete`'s own and drops the pointer's home store.  The image
//     has `stw r3, 0x50(r31)` at 0x824904E0, i.e. one inline level more, which
//     the SPELT-OUT `if (x) { delete x; x = nullptr; }` restores: 98.5 -> 100.0.
//     REFUTED on the way: dropping `auto &_ref1` for three direct
//     `mCampaignVOBank` reads is 95.4, and binding the deleted pointer to its
//     own local (`Hmx::Object *bank = _ref1; if (bank) ...`) stalls at 98.5 --
//     the null test has to read the REFERENCE, not a copy of it.
void HamCharacter::SetCampaignVo(const char *cc) {
    mCampaignVO = cc;
    auto& _ref1 = mCampaignVOBank;
    if (_ref1) {
        delete _ref1;
        _ref1 = nullptr;
    }
    if (!mCampaignVO.empty()) {
        String milo = GetCampaignVoMilo();
        mCampaignVODir = DirLoader::LoadObjects(FilePath(milo.c_str()), 0, 0);
        for (ObjDirItr<Hmx::Object> it(mCampaignVODir, false); it != nullptr; ++it) {
            if (it->Type() == "character_vo") {
                _ref1 = it;
                return;
            }
        }
    }
}

bool HamCharacter::IsLoading() {
    if (mFileMerger) {
        return mFileMerger->HasPendingFiles();
    } else
        return false;
}

HamRegulate *HamCharacter::Regulator() { return Find<HamRegulate>("song.hreg", false); }
HamDriver *HamCharacter::SongDriver() { return Find<HamDriver>("song.hdrv", false); }

int HamCharacter::SongAnimation() {
    CharClip *c = nullptr;
    CharDriver *drv = Driver();
    if (drv) {
        c = drv->FirstClip();
        // The image guards the assert with a null test on the clip -- 0x8248E37C
        // `mr. r30, r3` sets CR from FirstClip()'s return and the following
        // `beq .L_8248E388` jumps PAST the Type()/Symbol compare entirely. This
        // used to be behind #ifdef HX_NATIVE (added because native reaches here
        // before PlayAnims has run); it is what the Xbox build does too, and
        // without it the PPC build dereferences a null clip.
        if (c) {
            MILO_ASSERT(c->Type() == "main", 0x3AB);
        }
    }
    // -1 is the FALL-THROUGH result, not an `else if` arm. In the image the two
    // failing tests inside the InClipTest() arm -- `beq cr6, .L_8248E3EC` after
    // `cmplwi cr6, r30, 0x0` (clip null) and after `cmplw cr6, r11, r31` (clip's
    // dir is this) -- both land on the same `li r3, -0x1` that the
    // mUseCameraSkeleton/clip tests reach with `bne`. Spelled as
    // `if (InClipTest() && (c && ...)) ... else if (mUseCameraSkeleton || c)`,
    // a null clip under InClipTest() falls into the SongDriver() arm, which the
    // image can never do. Writing the -1 as an explicit `return -1;` inside the
    // InClipTest() arm fixes the branches but makes MSVC keep the cross-jumped
    // Property()/Int() tail at the SECOND call site instead of the first
    // (96.5 -> 87.6, measured twice: -1-after-Property and -1-before-Property).
    // Inverting the else-if so -1 is the function tail gets both.
    // REFUTED again (w8-n, third spelling): flattening the else-if entirely --
    // `return -1;` at the end of the InClipTest() arm AND a second
    // `if (mUseCameraSkeleton || c) return -1;` with `return 0;` as the function
    // tail -- reproduces the image's BLOCK ORDER (the -1 block early, `return 0`
    // falling through to the epilogue) and still costs 96.1062 -> 87.2, because
    // the shared Property()/Int() tail moves to the second call site.  The four
    // branch-destination rows and the 63/64-vs-111/112 insert/delete pair are
    // therefore ONE cause with the cross-jump, not two independent ones.
    if (InClipTest()) {
        if (c && c->Dir()->Dir() != this) {
            return c->Property("clip_skeleton_index", false)->Int();
        }
    } else if (!mUseCameraSkeleton && !c) {
        if (SongDriver()) {
            c = SongDriver()->FirstClip();
            if (c) {
                MILO_ASSERT(c->Type() == "main", 0x3C8);
                return c->Property("clip_skeleton_index", false)->Int();
            }
        }
        return 0;
    }
    return -1;
}

// RESIDUAL (w8-i, 95.24 canonical / 94.76 fuzzy): 3 rows of 21, all one artefact.
// The image keeps the loaded node pointer in r10 and makes a SEPARATE zero-extended
// copy to address through -- `lwz r10, 0xc(r11)` / `cmplwi cr6, r10, 0` /
// `clrrwi r11, r10, 0` / `lbz r11, 0x8(r11)` -- where we load straight into r11 and
// address through that, dropping the copy.  Two spellings measured, both WORSE:
//   `size() > prop && mShowableProps[prop] && mShowableProps[prop]->Showing()`
//        -> 92.38 canonical; the double subscript does NOT produce the clrrwi, it only
//           turns the null test from `cmplwi` into a signed `cmpwi`.
//   ... `mShowableProps[prop] != nullptr && ...` -> 92.38, identical rows.
// The assignment-expression form below is what keeps the test unsigned.
bool HamCharacter::GetPropShowing(int prop) {
    RndDrawable *d;
    auto _tmp0 = mShowableProps.size();
    return _tmp0 > prop && (d = mShowableProps[prop]) && d->Showing();
}

void HamCharacter::SetPropShowing(int prop, bool show) {
    // NOTE (w7-x): HamCharacter::SyncProperty inlines this four times and sits at
    // 97.93% on exactly those four copies. The image keeps `cmplwi/beq/clrrwi`
    // inline in each copy and cross-jumps only the shared `bl SetShowing`; our
    // build cross-jumps one instruction deeper, merging the null test too (12
    // target-only instructions, 2276 vs 2324 bytes).
    //
    // MECHANISM (w9-f, read off the listing at 82490C34-82490C48): the image
    // loads the element into a SCRATCH (`lwz r10, 0xc(r11)`), tests r10, and
    // only then copies it into the argument register (`clrrwi r3, r10, 0`), so
    // the `beq` cannot be part of the shared tail -- only `bl SetShowing; b`
    // at .L_82490B50 is, and that block is shared with the crew_card_showing
    // setter.  We load straight into r3, which makes our per-site tail
    // `cmplwi cr6, r3, 0 / beq / bl / b` identical across sites, so MSVC
    // cross-jumps three instructions deep instead of two.  The r10->r3
    // register "swap" objdiff reports is the consequence, not the cause: once
    // the shared block tests r3, the load has to target r3.
    //
    // THREE MORE SPELLINGS MEASURED, none reaches it (w9-f):
    //   `if (mShowableProps[prop]) mShowableProps[prop]->SetShowing(show);`
    //        (nested ifs, repeated subscript)   -> 97.43, WORSE: the merge gets
    //        deeper still (site 1 loses its cmplwi too) and a subfe moves.
    //   `RndDrawable *d; if (size() > prop && (d = mShowableProps[prop]) != 0)`
    //        (the assignment-expression idiom that fixed GetPropShowing)
    //                                          -> byte-identical, 97.93, same
    //        21 rows.  MSVC folds it back to the nested-if form.
    //   w7-x's `mShowableProps[prop] && mShowableProps[prop]->...` with &&
    //                                          -> 97.37, WORSE.
    // Cross-jump DEPTH is a backend decision with no source handle found; the
    // four copies are identical by construction, so no per-site spelling can
    // make their tails differ.
    if (mShowableProps.size() > prop) {
        RndDrawable *drawable = mShowableProps[prop];
        if (drawable)
            drawable->SetShowing(show);
    }
}

DataNode HamCharacter::OnConfigureFileMerger(DataArray *a) {
    FilePathTracker tracker(FileRoot());
    if (!mFileMerger) {
        return 0;
    } else {
        mNeedsAcquirePose = true;
        FilePath outfitPath = "";
        FilePath visemePath = "";
        FilePath voPath = "";
        mIsCampaignChar = !strstr(mOutfitDir.Str(), "dancer");
        if (!mOutfit.Null()) {
            const char *model = GetOutfitModel(mOutfit);
            outfitPath.Set(FilePath::Root().c_str(), model);
            Symbol charSym = GetOutfitCharacter(mOutfit);
            const char *viseme = GetCharacterViseme(charSym);
            visemePath.Set(FilePath::Root().c_str(), viseme);
            if (!mIsCampaignChar) {
                String vo = GetCampaignVo();
                if (!vo.empty()) {
                    voPath.Set(FilePath::Root().c_str(), GetCampaignVoMilo().c_str());
                } else {
                    voPath.Set(FilePath::Root().c_str(), "sfx/lipsynchelper.milo");
                }
                const char *localized = FileLocalize(voPath.c_str(), nullptr);
                voPath.Set(FilePath::Root().c_str(), localized);
            }
        }
        mFileMerger->Select("outfit", outfitPath, false);
        FileMerger::Merger *merger = mFileMerger->FindMerger("vo_bank", false);
        if (merger) {
            if (sLoadVO) {
                merger->SetSelected(voPath, false);
            } else {
                merger->SetSelected(gNullStr, false);
            }
        }
        FileMerger::Merger *visemeMerger = mFileMerger->FindMerger("viseme", false);
        if (visemeMerger) {
            visemeMerger->SetSelected(visemePath, false);
        }
        return 0;
    }
}

DataNode HamCharacter::OnPostDelete(DataArray *a) {
    Symbol s = a->Sym(2);
    if (s == "outfit") {
        ObjectDir *clipDir = Find<ObjectDir>("clips", false);
        if (clipDir) {
            mDriver->SetClips(clipDir);
        }
    }
    return 0;
}

DataNode HamCharacter::OnToggleInterestDebugOverlay(DataArray *a) {
    if (mEyes) {
        mEyes->ToggleInterestsDebugOverlay();
    }
    return 0;
}

DataNode HamCharacter::OnCamTeleport(DataArray *a) {
    mWaypoint->DirtyLocalXfm() = LocalXfm();
    if (Find<HamRegulate>("song.hreg", false)) {
        Find<HamRegulate>("song.hreg", false)->SetWaypoint(0);
    }
    return 0;
}

// RESIDUAL (w7-bs, 98.8 canonical / 98.4 raw, 652 B, 9 rows, target
// 0x82491900-82491B8C).  Was 91.66 (w7-bp, 25 rows).  Two of w7-bp's three
// "backend" clusters were source shapes:
//
//  (1) EPILOGUE HOST (was 6 rows).  The image hosts the epilogue in the
//      `return this` block at 0x8249197C-8C and its `return mNeutralSkelDir`
//      is ONE block at the very end (0x82491B84: `lwz r3, 0x334(r30)` /
//      `b .L_82491980`) reached by fall-through from the else arm and by
//      `b .L_82491B84` from the shared map-destructor site 0x82491AF8.  MSVC
//      hosts the epilogue in the return block that the LAST source-order
//      fall-through reaches, so `return mNeutralSkelDir;` as the final
//      statement (every previous spelling) pins it there: 95.1.  Writing
//      the success paths as `goto neutral;` onto a single
//      `neutral: return mNeutralSkelDir;` INSIDE the else arm's success
//      block, with `return this;` as the function's last statement, gives
//      the image's host and its single return block: 98.8.  Measured
//      alternatives: per-arm `return mNeutralSkelDir;` with a final
//      `return this;` hosts the epilogue right but leaves TWO return-value
//      blocks (96.6; 97.5 once each map's scope is closed before the
//      return, because the image reads 0x334 AFTER the dtor); inverting the
//      timing test to `if (clipMap.size() != 0)` flips 0x82491970 to beq
//      (88.3); explicit `return this;` on the else arm's null/flag tests
//      plus a final `return this;` breaks the clear cross-jump (93.8); a
//      trailing `no_clip: return this;` reached by goto is inert (95.1).
//
//  (2) THE clipTimingMap LOOP GUARD (was 6 rows, "refuted" by w7-af as a
//      per-loop backend rotation decision).  It was the function-scope
//      `bones = ...; bones->Zero();` pair on that path: a local assigned and
//      immediately consumed there kept MSVC from cross-jumping the guard into
//      the latch.  `mSkeletonBones->Zero()` on that one path restores the
//      image's `lwz r29, 0x68(r31)` / `b .L_824919CC` at 0x8249199C-A0.
//      Dropping `bones` on ALL paths loses the hoisted `lwz 0x338 /
//      addi r3, r11, 0x10` above the `beq` at 0x82491A00-0C (92.6), so the
//      goto path keeps it.
//
//  Remaining 9 rows, both still open:
//  (3) r26 <-> r27, 7 rows, mClipWeightMap loop only: the image holds the
//      iterator in r27 and `sSkeletonClips` in r26 (`mr r27, r11` at
//      0x82491A34, `addi r26, r11, sSkeletonClips@l` at 0x82491A4C); we hold
//      them the other way round.  Hoisting the iterator declaration out of
//      the `for` is inert; indexing with the inline `Property()->Int()`
//      expression costs a 7th callee-saved register (91.9).
//  (4) 2 rows at zero_and_scale: the image orders the null-select
//      `addi r4, r11, 0x10` BEFORE `cmplwi cr6, r11, 0` (0x82491B2C-30) while
//      the same select at 0x82491A9C-AA0 has cmplwi first (and matches).
//      Ternary, implicit derived-to-base, and inline
//      `*static_cast<CharBones *>(mSkeletonBones)` spellings are all
//      byte-identical.
ObjectDir *HamCharacter::GetNeutralSkeleton() {
#ifdef HX_NATIVE
    {
        static int sEntryLog = 0;
        const char *p = PathName(this);
        if (sEntryLog < 6 && p && strstr(p, "main.milo") && !strstr(p, "backup")) {
            sEntryLog++;
            fprintf(stderr,
                "DC3_IK_DIAG GetNeutralSkelEntry[%d]: char=%s skelBones=%p "
                "neutralDir=%p self=%d\n",
                sEntryLog, p, (void*)mSkeletonBones, (void*)mNeutralSkelDir,
                (int)(mNeutralSkelDir == (ObjectDir *)this));
        }
    }
    // Skeleton blending requires mSkeletonBones (set via skeleton_path config).
    // If not configured, skip computation and return the dir/self fallback.
    if (!mSkeletonBones) {
        return mNeutralSkelDir ? mNeutralSkelDir : this;
    }
#endif
    int songAnim = SongAnimation();
#ifdef HX_NATIVE
    // On native, compute bones once up front. The goto zero_and_scale below
    // jumps between branches, which is UB on Clang if the assignment on the
    // taken path has not run yet.
    CharBones *bones = static_cast<CharBones *>(mSkeletonBones);
#else
    // One function-scope variable, assigned on each path. Two separate
    // block-scope `bones` force MSVC to unify them through a stack slot at
    // zero_and_scale; the target carries the value in r3 across the goto.
    CharBones *bones;
#endif
    if (songAnim != -1) {
        HamDriver *hamDriver = Find<HamDriver>("song.hdrv", false);
        if (hamDriver == nullptr || hamDriver->FirstClip() == nullptr) {
            CharClip *clip = Driver()->FirstPlayingClip();
#ifndef HX_NATIVE
            bones = reinterpret_cast<CharBones *>((char *)mSkeletonBones + 0x10);
#endif
            if (clip == nullptr) {
                goto zero_and_scale;
            }
            bones->Zero();
            Driver()->SetClipWeightMap();
            std::map<CharClip *, float> clipMap(Driver()->mClipWeightMap);
            float totalWeight = 0.0f;
            for (std::map<CharClip *, float>::iterator it = clipMap.begin();
                 it != clipMap.end(); ++it) {
                CharClip *clipEntry = it->first;
                float weight = it->second;
                if (clipEntry != nullptr && weight > totalWeight) {
                    int skelIdx = clipEntry->Property("clip_skeleton_index", false)->Int(nullptr);
                    CharBones *skBones = mSkeletonBones ? static_cast<CharBones *>(mSkeletonBones) : nullptr;
                    sSkeletonClips[skelIdx]->ScaleAdd(*skBones, weight, totalWeight, totalWeight);
                }
            }
            mSkeletonBones->Poll();
            goto neutral;
        } else {
            hamDriver->SetClipWeightMap();
            std::map<CharClip *, float> clipMap(hamDriver->mClipTimingMap);
            // No explicit clear() here: the target calls _Rb_tree::clear exactly
            // ONCE on this path (0x82491978), which is the map destructor running
            // at `return this`. Spelling `clipMap.clear(); return this;` emits a
            // second `bl clear` and forces the early return to branch to a shared
            // epilogue instead of the inlined one the image has at 0x8249197C.
            if (clipMap.size() == 0) {
                return this;
            }
            mSkeletonBones->Zero();
            // No `bones` local on this path: `bones = ...; bones->Zero();` here
            // made MSVC peel a guard copy of the loop test instead of entering
            // with `b .L_824919CC` (w7-af read that as a backend rotation
            // decision; w7-bs: it was this pair, see the note above).
            for (std::map<CharClip *, float>::iterator it = clipMap.begin();
                 it != clipMap.end(); ++it) {
                CharClip *timedClip = it->first;
                float timedWeight = it->second;
                if (timedClip != nullptr) {
                    ApplyBlendedSkeletons(hamDriver, timedClip, timedWeight);
                }
            }
            mSkeletonBones->Poll();
            goto neutral;
        }
    } else {
        CharClip *clip = Driver()->FirstClip();
        if (clip != nullptr && !(clip->Flags() & 1)) {
#ifndef HX_NATIVE
            bones = reinterpret_cast<CharBones *>((char *)mSkeletonBones + 0x10);
#endif
zero_and_scale:
            bones->Zero();
            {
                CharBones *skBones = mSkeletonBones ? static_cast<CharBones *>(mSkeletonBones) : nullptr;
                sSkeletonClips[mGender == kHamFemale ? 1 : 0]->ScaleAdd(*skBones, 1.0f, 0.0f, 0.0f);
            }
            mSkeletonBones->Poll();
neutral:
#ifdef HX_NATIVE
            {
                // Diagnostic: confirm the neutral skeleton is a SEPARATE posed dir (not
                // a collapse onto `this`), and read its neutral ankle Z after posing.
                // If the neutral ankle is planted (~+4) the IK clamp anchor is good;
                // if it tracks the dropped live pose the foot will sink.
                extern int HamDirector_SelectCameraSetFrameCount();
                static int sNeutralLog = 0;
                const char *p = PathName(this);
                bool isMain = p && strstr(p, "main.milo") && !strstr(p, "backup");
                // Sample during gameplay (frame>3000) AND only the on-screen dancer
                // (player0), so the neutral-anchor values correlate with the sunk
                // dancer's ChainZ trace.
                if (sNeutralLog < 30 && isMain && p && strstr(p, "player0")
                    && HamDirector_SelectCameraSetFrameCount() > 3000) {
                    sNeutralLog++;
                    ObjectDir *nd = mNeutralSkelDir;
                    RndTransformable *nAnkle = nd ?
                        nd->Find<RndTransformable>("bone_L-ankle.mesh", true) : nullptr;
                    RndTransformable *nPelvis = nd ?
                        nd->Find<RndTransformable>("bone_pelvis.mesh", true) : nullptr;
                    RndTransformable *nToe = nd ?
                        nd->Find<RndTransformable>("bone_L-toe.mesh", true) : nullptr;
                    // Live (this) pelvis/toe for the same char.
                    RndTransformable *lPelvis =
                        Find<RndTransformable>("bone_pelvis.mesh", true);
                    RndTransformable *lToe =
                        Find<RndTransformable>("bone_L-toe.mesh", true);
                    fprintf(stderr,
                        "DC3_IK_DIAG GetNeutralSkel[%d] f=%d: char=%s neutralDir=%s "
                        "neutralAnkleWorldZ=%.3f neutralPelvisWorldZ=%.3f "
                        "neutralToeWorldZ=%.3f | livePelvisZ=%.3f liveToeZ=%.3f\n",
                        sNeutralLog, HamDirector_SelectCameraSetFrameCount(), p,
                        nd ? nd->Name() : "(null)",
                        nAnkle ? nAnkle->WorldXfm().v.z : -999.0f,
                        nPelvis ? nPelvis->WorldXfm().v.z : -999.0f,
                        nToe ? nToe->WorldXfm().v.z : -999.0f,
                        lPelvis ? lPelvis->WorldXfm().v.z : -999.0f,
                        lToe ? lToe->WorldXfm().v.z : -999.0f);
                }
            }
#endif
            return mNeutralSkelDir;
        }
    }
    return this;
}

void HamCharacter::SetFaceOverrideClip(Symbol clipName, bool notify) {
    CharLipSyncDriver *driver = Find<CharLipSyncDriver>("face.lipdrv", false);
    bool found = false;
    if (driver) {
        if (clipName.Null()) {
            found = true;
            driver->mOverrideClip = nullptr;
        } else {
            // RESIDUAL (w8-n, 96.6316): the image loads mOverrideOptions into r11,
            // tests it on CR0, HOMES it at 0x54(r31) and only then `mr r4, r11`
            // into the ObjDirItr argument (0x82490214..0x8249022C; the identical
            // sequence in BlendInFaceOverrideClip starts at 0x824903D0); we load
            // straight into r4 on CR6 and emit neither extra row.  REFUTED: the
            // ternary `driver->mOverrideOptions ? driver->mOverrideOptions
            // : driver->mClips` is 92.6 -- MSVC stops folding the ObjPtr read
            // and instead materialises each ObjPtr's ADDRESS (`addi r11, r30,
            // 0xf0` / `0x44` then `lwz r4, 0xc(r11)`), i.e. it costs three rows
            // rather than gaining two.  Also INERT: splitting the first read into
            // its own `ObjectDir *overrideOptions` local.  The same six rows sit
            // on BlendInFaceOverrideClip, which uses the identical idiom.
            ObjectDir *clipDir = driver->mOverrideOptions;
            if (!clipDir) {
                clipDir = driver->mClips;
            }
            for (ObjDirItr<CharClip> it(clipDir, false); it != nullptr; ++it) {
                if (clipName == it->Name()) {
                    found = true;
                    driver->mOverrideClip = it;
                }
            }
            if (!found && notify) {
                TheDebug.Notify(MakeString(
                    "HamCharacter::SetFaceOverrideClip couldn't find clip named %s for %s",
                    clipName.Str(),
                    Name()
                ));
                return;
            }
        }
    }
    if (found)
        return;
    if (!notify)
        return;
    // The `(char *)` cast instantiated MakeString<char *>; the image calls
    // MakeString<const char *> (??$MakeString@PBD@@YAPBDPBDABQBD@Z).
    TheDebug.Notify(MakeString(
        "HamCharacter::SetFaceOverrideClip couldn't find  lip sync driver for %s", Name()
    ));
}

void HamCharacter::BlendInFaceOverrideClip(Symbol clipName, float blendIn, float blendOut) {
    CharLipSyncDriver *driver = Find<CharLipSyncDriver>("face.lipdrv", false);
    bool found = false;
    if (driver) {
        if (clipName.Null()) {
            found = true;
            driver->mOverrideClip = nullptr;
        } else {
            ObjectDir *clipDir = driver->mOverrideOptions;
            if (!clipDir) {
                clipDir = driver->mClips;
            }
            for (ObjDirItr<CharClip> it(clipDir, false); it != nullptr; ++it) {
                if (clipName == it->Name()) {
                    found = true;
                    driver->BlendInOverrideClip(it, blendIn, blendOut);
                }
            }
            if (!found) {
                TheDebug.Notify(MakeString(
                    "HamCharacter::SetFaceOverrideClip couldn't find clip named %s for %s",
                    clipName.Str(),
                    Name()
                ));
                return;
            }
        }
    }
    if (found)
        return;
    // The `(char *)` cast instantiated MakeString<char *>; the image calls
    // MakeString<const char *> (??$MakeString@PBD@@YAPBDPBDABQBD@Z) here too.
    TheDebug.Notify(MakeString(
        "HamCharacter::SetFaceOverrideClip couldn't find  lip sync driver for %s", Name()
    ));
}

DataNode HamCharacter::OnSoundPlay(const DataArray *a) {
    const DataNode &val = const_cast<DataArray *>(a)->Node(2).Evaluate();
    if (val.Type() == kDataObject) {
        Hmx::Object *obj = val.UncheckedObj();
        if (!obj) return DataNode(0);
        Sound *sound = dynamic_cast<Sound *>(obj);
        if (sound) {
            if (mOutfit.Str()[0] == '\0') {
#ifndef HX_NATIVE
                MILO_NOTIFY(
                    "HamCharacter::OnSoundPlay: No outfit specified for character %s. "
                    "Not going to play lipsync\n",
                    (char *)Name()
                );
#endif
                return DataNode(0);
            }
            Symbol outfitChar = GetOutfitCharacter(mOutfit, true);
            StackString<128> soundName(sound->Name());
            unsigned int pos = soundName.find(outfitChar.Str());
            if (pos != (unsigned int)-1) {
                CharLipSync *lipSync = CharLipSync::FindLipSyncForSound(sound);
                if (lipSync) {
                    TheDebug << MakeString(
                        "HamCharacter: found lipsync [%s] to play for sound [%s]\n",
                        lipSync->Name(),
                        sound->Name()
                    );
                    float seconds = TheTaskMgr.Seconds(TaskMgr::kRealTime);
                    EnableFacialAnimation(lipSync, -seconds);
                }
            }
        }
    }
    return DataNode(0);
}

#ifndef HX_NATIVE
// PPC codegen variant — body uses Set() instead of initializer list
QuatXfm::QuatXfm(const Transform &t) : v(t.v) { q.Set(t.m); }
#endif

#ifndef HX_NATIVE
void HamCharacter::Poll() {
    int songAnim = SongAnimation();
    if (songAnim == -1 || InClipTest()) {
        if (mDriver)
            mDriver->SetWeight(1.0f);
    } else {
        if (mDriver)
            mDriver->SetWeight(0.0f);
    }

    bool wasShowing = mShowing;
    if (!wasShowing && mPollWhenHidden) {
        SetShowing(true);
    }
    Character::Poll();
    SetShowing(wasShowing);

    RndTransformable *boneProp = Find<RndTransformable>("bone_prop0.mesh", false);
    if (boneProp) {
        RndTransformable *spotProp = Find<RndTransformable>("spot_prop0.mesh", false);
        if (spotProp) {
            float blendWeight = 1.0f;
            int songAnim2 = SongAnimation();
            if (songAnim2 != -1) {
                blendWeight = 0.0f;
            } else if (mDriver->First()) {
                blendWeight = mDriver->EvaluateFlags(2);
            }

            QuatXfm boneXfm(boneProp->WorldXfm());
            QuatXfm spotXfm(spotProp->WorldXfm());

            QuatXfm interpXfm;
            Interp(spotXfm.v, boneXfm.v, blendWeight, interpXfm.v);
            Interp(spotXfm.q, boneXfm.q, blendWeight, interpXfm.q);

            Transform result;
            result.v = interpXfm.v;
            MakeRotMatrix(interpXfm.q, result.m);
            boneProp->SetWorldXfm(result);
        }
    }

    RndMat *mat = Find<RndMat>("robot_face.mat", false);
    if (!mat) return;

    CharLipSyncDriver *lipDrv = Find<CharLipSyncDriver>("face.lipdrv", false);
    const char *clipName = "base";
    CharLipSync::PlayBack *pb = lipDrv->GetPlayBack();
    if (pb) {
        float maxWeight = 0.0f;
        for (int i = 0; i < pb->mWeights.size(); i++) {
            CharLipSync::PlayBack::Weight &w = pb->mWeights[i];
            if (!w.mClip)
                continue;
            float prevMax = maxWeight;
            maxWeight = Max(maxWeight, w.mCurWeight);
            bool isNewMax = maxWeight != prevMax;
            if (isNewMax) {
                clipName = w.mClip->Name();
            }
        }
    }

    char texName[256];
    strcpy(texName, clipName);
    _strlwr(texName);
    strcat(texName, ".tex");

    RndTex *tex = Find<RndTex>(texName, false);
    if (!tex) {
        tex = Find<RndTex>("base.tex", false);
    }
    if (tex) {
        mat->SetDiffuseTex(tex);
    } else {
        MILO_NOTIFY_ONCE("%s could not find viseme texture %s", PathName(this), texName);
    }
}
#else
// The CRT _strlwr the image calls (82491630) does not exist on the host; this
// is its "C"-locale behaviour: ASCII A-Z lowered in place, nothing else.
static char *HamCharacterStrLwr(char *s) {
    for (char *p = s; *p; p++) {
        if (*p >= 'A' && *p <= 'Z')
            *p += 'a' - 'A';
    }
    return s;
}

// Native body. Same semantics as the image (?Poll@HamCharacter@@UAAXXZ,
// 82491378) except for the ONE labelled workaround below. The prop-attach
// blend and the robot viseme swap used to be missing here entirely: this body
// predated the decompiled one (793a8a4e5) and stopped after Character::Poll.
void HamCharacter::Poll() {
    int songAnim = SongAnimation();
    if (songAnim == -1 || InClipTest()) {
        if (mDriver) mDriver->SetWeight(1.0f);
    } else {
        if (mDriver) mDriver->SetWeight(0.0f);
    }

    // NATIVE WORKAROUND -- NOT Xbox behaviour. The image force-shows a hidden
    // character for the poll only when mPollWhenHidden is set (82491400 lbz
    // r31,-0x108(r29); bne; 8249140C lbz r11,0x16c(r29); beq; 82491420 bl
    // SetShowing). Native forces it whenever the character is hidden, so
    // RndDir::Poll() runs child pollables (CharDriver, etc.) and animations
    // advance. The Xbox-exact gate was tried natively in d07ce3782 and
    // reverted in a37908240 (the character-animation convergence fix) within
    // hours, with no recorded reason. Consequence: a hidden character polls
    // (and animates) on native where the Xbox leaves it frozen.
    // Measured 2026-09-30 (ymca/houseparty headless gameplay, 9050 frames):
    // player0/player1/backup0/backup1 were NEVER hidden at Poll (0 of ~8400
    // polls each); the only character this line force-showed was iconman,
    // on every poll (8400 of 8400) -- on the Xbox iconman is polled only
    // inside HamDirector::PoseIconMan's SetPollWhenHidden(true) bracket. With
    // the Xbox gate instead, all 48 GameplayTelemetryTest cases passed and
    // player0 animated. So the workaround is not load-bearing for that flow;
    // it stays until the other character-bearing flows (menus, practice,
    // campaign, crew select) are measured the same way.
    bool wasShowing = mShowing;
    if (!wasShowing) {
        SetShowing(true);
    }
    Character::Poll();
    SetShowing(wasShowing);

    // Prop attach blend (82491438..82491558).
    RndTransformable *boneProp = Find<RndTransformable>("bone_prop0.mesh", false);
    if (boneProp) {
        RndTransformable *spotProp = Find<RndTransformable>("spot_prop0.mesh", false);
        if (spotProp) {
            float blendWeight = 1.0f;
            if (SongAnimation() != -1) {
                blendWeight = 0.0f;
            } else if (mDriver && mDriver->First()) {
                // The image has no null test on mDriver here (82491488 lwz
                // r3,0xa0(r29); lwz r11,0x58(r3)): a null driver reads the 360's
                // zero page, First() comes back 0 and the weight stays 1. The
                // guard reproduces that result instead of faulting.
                blendWeight = mDriver->EvaluateFlags(2);
            }

            QuatXfm boneXfm(boneProp->WorldXfm());
            QuatXfm spotXfm(spotProp->WorldXfm());

            QuatXfm interpXfm;
            Interp(spotXfm.v, boneXfm.v, blendWeight, interpXfm.v);
            Interp(spotXfm.q, boneXfm.q, blendWeight, interpXfm.q);

            Transform result;
            result.v = interpXfm.v;
            MakeRotMatrix(interpXfm.q, result.m);
            boneProp->SetWorldXfm(result);
        }
    }

    // Robot viseme texture swap (8249155C..8249175C).
    RndMat *mat = Find<RndMat>("robot_face.mat", false);
    if (!mat)
        return;

    CharLipSyncDriver *lipDrv = Find<CharLipSyncDriver>("face.lipdrv", false);
    const char *clipName = "base";
    // The image loads the playback with no null test on lipDrv (8249158C lwz
    // r11,0x88(r3)); on the 360 a missing driver reads the zero page, the
    // playback is 0 and the name stays "base". Guarded to the same result.
    CharLipSync::PlayBack *pb = lipDrv ? lipDrv->GetPlayBack() : nullptr;
    if (pb) {
        float maxWeight = 0.0f;
        for (int i = 0; i < pb->mWeights.size(); i++) {
            CharLipSync::PlayBack::Weight &w = pb->mWeights[i];
            if (!w.mClip)
                continue;
            float prevMax = maxWeight;
            maxWeight = Max(maxWeight, w.mCurWeight);
            if (maxWeight != prevMax) {
                clipName = w.mClip->Name();
            }
        }
    }

    char texName[256];
    strcpy(texName, clipName);
    HamCharacterStrLwr(texName);
    strcat(texName, ".tex");

    RndTex *tex = Find<RndTex>(texName, false);
    if (!tex) {
        tex = Find<RndTex>("base.tex", false);
    }
    if (tex) {
        mat->SetDiffuseTex(tex);
    } else {
        MILO_NOTIFY_ONCE("%s could not find viseme texture %s", PathName(this), texName);
    }
}
#endif

void HamCharacter::ApplyBlendedSkeletons(
    HamDriver *driver, CharClip *clip, float weight
) {
    if (clip->NumBlendSamples() != 0) {
        HamDriver::LayerArray &layers = driver->Layers();
        for (std::list<HamDriver::Layer *>::iterator it = layers.mLayers.begin();
             it != layers.mLayers.end();
             ++it) {
            HamDriver::LayerClip *layerClip;
            if ((*it)->FirstClip() == clip && (*it)->mWeight == weight
                && (layerClip = dynamic_cast<HamDriver::LayerClip *>(*it))
                       != nullptr) {
                float beat = (TheTaskMgr.Beat() - layerClip->mClipBeat)
                    + clip->StartBeat();
                CharBones *bones =
                    mSkeletonBones
                        ? static_cast<CharBones *>(mSkeletonBones)
                        : nullptr;
                clip->ApplyBlendedSkeletons(
                    sSkeletonClips, *bones, beat, weight
                );
                return;
            }
        }
    }
    int skelIndex = clip->Property("clip_skeleton_index", false)->Int();
    CharBones *bones =
        mSkeletonBones ? static_cast<CharBones *>(mSkeletonBones) : nullptr;
    sSkeletonClips[skelIndex]->ScaleAdd(*bones, weight, 0.0f, 0.0f);
}

template class StackString<128>;
