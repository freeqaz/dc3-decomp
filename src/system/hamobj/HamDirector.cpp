#include "hamobj/HamDirector.h"
#include "Difficulty.h"
#include "MoveMgr.h"
#include "PoseFatalities.h"
#include "SuperEasyRemixer.h"
#include "SongCollision.h"
#include "SongUtl.h"
#include "char/CharBone.h"
#include "char/CharBoneDir.h"
#include "char/CharBonesMeshes.h"
#include "char/CharClip.h"
#include "char/CharForeTwist.h"
#include "char/CharLipSync.h"
#include "char/CharNeckTwist.h"
#include "char/CharPollable.h"
#include "char/CharUpperTwist.h"
#include "char/CharUtl.h"
#include "char/Character.h"
#include "char/FileMerger.h"
#include "flow/Flow.h"
#include "flow/PropertyEventProvider.h"
#include "gesture/BaseSkeleton.h"
#include "hamobj/ClipPlayer.h"
#include "hamobj/Difficulty.h"
#include "hamobj/HamCamShot.h"
#include "hamobj/HamCharacter.h"
#include "hamobj/HamWardrobe.h"
#include "hamobj/HamGameData.h"
#include "hamobj/HamMaster.h"
#include "hamobj/HamMove.h"
#include "hamobj/HamPhraseMeter.h"
#include "hamobj/HamPlayerData.h"
#include "hamobj/HamRibbon.h"
#include "hamobj/HamSkeletonConverter.h"
#include "hamobj/HamSong.h"
#include "hamobj/HamVisDir.h"
#include "hamobj/HamWardrobe.h"
#include "hamobj/TransConstraint.h"
#include "math/Mtx.h"
#include "math/Rand.h"
#include "math/Rot.h"
#include "math/Utl.h"
#include "math/Vec.h"
#include "obj/Data.h"
#include "obj/DataFile.h"
#include "obj/DataUtl.h"
#include "obj/Dir.h"
#include "obj/Msg.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "obj/Utl.h"
#include "os/Debug.h"
#include "os/File.h"
#include "os/Timer.h"
#include "rndobj/Anim.h"
#include "rndobj/Draw.h"
#include "rndobj/Overlay.h"
#include "rndobj/Poll.h"
#include "rndobj/PostProc.h"
#include "rndobj/PropAnim.h"
#include "rndobj/PropKeys.h"
#include "rndobj/Tex.h"
#include "rndobj/TexRenderer.h"
#include "rndobj/Trans.h"
#include "utl/FakeSongMgr.h"
#include "utl/FilePath.h"
#include "utl/Loader.h"
#include "utl/Str.h"
#include "utl/Symbol.h"
#include "utl/TextStream.h"
#include "utl/TimeConversion.h"
#include "world/CameraManager.h"
#include "world/Dir.h"
#include <cctype>

HamDirector *TheHamDirector;
OfflineCallback gOfflineCallback;
std::map<Symbol, int> gMoveMergeMap;

float FrameToBeat(float frame) { return SecondsToBeat(frame * 0.033333335f); }
float BeatToFrame(float beat) { return BeatToSeconds(beat) * 30.0f; }

ObjectDir *OfflineCallback::SongMainDir() {
    MILO_ASSERT(TheHamDirector, 0x1137);
    return TheHamDirector->GetWorld();
}

HamDirector::HamDirector()
    : mMasterClipAnim(this), mPlayer1RoutineBuilderAnim(this),
      mPlayer2RoutineBuilderAnim(this), unkc8(0), unkcc(""), mBackupDrift(1),
      mMerger(this), mMoveMerger(this), mGameModeMerger(this), mVenue(this), mSongCollision(this),
      mPickNewShot(0), mSyncScene(0), mWorldPostProc(this), mCamPostProc(this),
      mForcePostProc(this), unk18c(this), mForcePostProcBlend(0),
      mForcePostProcBlendRate(1), mPostProcInterpA(this), mPostProcInterpB(this), mPostProcInterpBlend(0), unk1d4(0),
      mSavedForcePostProc(this), mVisualizerPostProc(this), mFreestyleEnabled(1), mPlayer0Char(this),
      mPlayer1Char(this), mBackup0Char(this), mBackup1Char(this), mBackupHidden(0), mDisabled(0),
      mAsyncLoaded(0), mCurShot(this), mNextShot(this), mIntroShot(this), mLastShotTime(-kHugeFloat),
      mDisablePicking(0), mSuppressIntroShot(0), mSuppressNextShot(0), mLastCollisionTime(-kHugeFloat), mPollEnabled(1),
      mPlayerFreestyle(0), mPlayerFreestylePaused(0), mVisualizer(this),
      mPracticeStart(0), mPracticeEnd(0), mStartLoopMargin(1), mEndLoopMargin(1),
      mBlendDebug(0), mBackupDancers((HamBackupDancers)0), mClipDir(this), mMoveDir(this),
      mNoTransitions(0), mCollisionChecks(1), mLoadedNewSong(1), mPoseFatalities(0),
      mCamshotFlag(RandomInt(0, 2)), mGameStartHold(0), mIconManChar(this), mIconManTex(this),
      mPhraseMetersFlipped(0), mOfflineSong(0) {
    static DataNode &n = DataVariable("hamdirector");
    n = this;
    TheHamDirector = this;
    mDirCutKeys.reserve(100);
#ifdef HX_NATIVE
    static int sCtorCount = 0;
    sCtorCount++;
    fprintf(stderr, "DC3 HamDirector::ctor #%d this=%p TheHamDirector=%p\n",
            sCtorCount, (void*)this, (void*)TheHamDirector);
#endif
}

HamDirector::~HamDirector() {
    delete mOfflineSong;
    MILO_ASSERT(TheGameData, 0xC5);
    TheGameData->Clear();
    if (TheHamDirector == this) {
        static DataNode &n = DataVariable("hamdirector");
        n = NULL_OBJ;
        TheHamDirector = nullptr;
    }
    delete mPoseFatalities;
}

BEGIN_HANDLERS(HamDirector)
    HANDLE(shot_over, OnShotOver)
    HANDLE(postproc_interp, OnPostProcInterp)
    HANDLE(save_song, OnSaveSong)
    HANDLE(save_face_anims, OnSaveFaceAnims)
    HANDLE(on_file_loaded, OnFileLoaded)
    HANDLE(on_file_merged, OnFileMerged)
    HANDLE(load_song, OnLoadSong)
    HANDLE_EXPR(is_world_loaded, IsWorldLoaded())
    HANDLE_ACTION(unload_all, UnloadAll())
    HANDLE_ACTION(pick_new_shot, mPickNewShot = true)
    HANDLE(select_camera, OnSelectCamera)
    HANDLE(cycle_shot, OnCycleShot)
    HANDLE(force_shot, OnForceShot)
    HANDLE_EXPR(camera_source, (Hmx::Object *)mVenue)
    HANDLE_ACTION(force_scene, ForceScene(_msg->Sym(2)))
    HANDLE_ACTION(force_minivenue, ForceMiniVenue(_msg->Sym(2)))
    HANDLE(cur_postprocs, OnPostProcs)
    HANDLE_ACTION(reselect_world_postproc, ReselectWorldPostProc())
    HANDLE_EXPR(get_venue_world, GetVenueWorld())
    HANDLE_EXPR(get_world, mMerger ? mMerger->Dir() : (ObjectDir *)nullptr)
    HANDLE(set_dircut, OnSetDircut)
    HANDLE(get_dancer_visemes, OnGetDancerVisemes)
    HANDLE_ACTION(play_base_visemes, PlayCharBaseVisemes())
    HANDLE_ACTION(enable_facial_animation, EnableFacialAnimation())
    HANDLE_ACTION(disable_facial_animation, DisableFacialAnimation())
    HANDLE_ACTION(reset_facial_animation, ResetFacialAnimation())
    HANDLE_ACTION(set_lipsync_offsets, SetLipsyncOffsets(_msg->Float(2)))
    HANDLE_ACTION(resync_face_drivers, ResyncFaceDrivers())
    HANDLE(blend_face_clip, OnBlendInFaceClip)
    HANDLE_ACTION(blend_face_overrides_in, BlendInFaceOverrides(_msg->Float(2)))
    HANDLE_ACTION(blend_face_overrides_out, BlendOutFaceOverrides(_msg->Float(2)))
    HANDLE(practice_beats, OnPracticeBeats)
    HANDLE_EXPR(beat_to_movename, MoveNameFromBeat(_msg->Float(2), _msg->Int(3)))
    HANDLE_EXPR(is_intro, strneq(_msg->Sym(2).Str(), "INTRO_", 6))
    HANDLE_ACTION(initialize, Initialize())
    HANDLE_EXPR(player_song_anim, SongAnim(_msg->Int(2)))
    HANDLE_EXPR(difficulty_song_anim, SongAnimByDifficulty((Difficulty)_msg->Int(2)))
    HANDLE_EXPR(
        dancer_face_anim_by_difficulty,
        mDancerFaceAnims[LegacyDifficulty((Difficulty)_msg->Int(2))].Ptr()
    )
    HANDLE_EXPR(dancer_face_anim_by_player, DancerFaceAnimByPlayer(_msg->Int(2)))
    HANDLE_EXPR(toggle_camshot_flag, OnToggleCamshotFlag())
    HANDLE_EXPR(get_character_sym, mCharacterOutfits[_msg->Int(2)])
    HANDLE_ACTION(hide_backups, HideBackups(_msg->Int(2), _msg->Int(3)))
    HANDLE_ACTION(restore_backups, RestoreBackups())
    HANDLE_ACTION(teleport_chars, TeleportChars())
    HANDLE_ACTION(reteleport, Reteleport())
    HANDLE_EXPR(list_possible_move, OnListPossibleMoves())
    HANDLE_EXPR(list_possible_variants, OnListPossibleVariants())
    HANDLE_ACTION(set_grooviness, mVisualizer->SetGrooviness(_msg->Float(2)))
    HANDLE_ACTION(start_stop_visualizer, StartStopVisualizer(_msg->Int(2), _msg->Int(3)))
    HANDLE_ACTION(set_player_spotlights_enabled, SetPlayerSpotlightsEnabled(_msg->Int(2)))
    HANDLE_ACTION(hud_entered, HudEntered())
    HANDLE_ACTION(
        change_player_character,
        ChangePlayerCharacter(_msg->Int(2), _msg->Sym(3), _msg->Sym(4), _msg->Sym(5))
    )
    HANDLE_ACTION(set_suppress_intro_shot, mSuppressIntroShot = _msg->Int(2))
    HANDLE_EXPR(get_suppress_intro_shot, 0)
    HANDLE_ACTION(set_suppress_next_shot, mSuppressNextShot = _msg->Int(2))
    HANDLE_EXPR(get_suppress_next_shot, mSuppressNextShot)
    HANDLE_EXPR(is_game_start_hold, mGameStartHold)
    HANDLE_ACTION(enable_poll, mPollEnabled = _msg->Int(2))
    HANDLE(clip_annotate, OnClipAnnotate)
    HANDLE(clip_safetoadd, OnClipSafeToAdd)
    HANDLE(clip_list, OnClipList)
    HANDLE(practice_safetoadd, OnPracticeSafeToAdd)
    HANDLE(practice_annotate, OnPracticeAnnotate)
    HANDLE_EXPR(practice_list, PracticeList((Difficulty)_msg->Int(2)))
    HANDLE(toggle_debug_interests, OnToggleDebugInterests)
    HANDLE_ACTION(init_offline, InitOffline())
    HANDLE_ACTION(offline_load_song, OfflineLoadSong(_msg->Sym(2)))
    HANDLE(toggle_cam_character_skeleton, OnToggleCamCharacterSkeleton)
    HANDLE_ACTION(populate_moves, OnPopulateMoves())
    HANDLE_ACTION(populate_movemgr, OnPopulateMoveMgr())
    HANDLE_ACTION(populate_from_file, OnPopulateFromFile())
    HANDLE_SUPERCLASS(RndPollable)
    HANDLE_SUPERCLASS(RndDrawable)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(HamDirector)
    SYNC_PROP_SET(shot, mShot, SetShot(_val.Sym()))
    static Symbol none("none");
    SYNC_PROP_SET(postproc, NULL_OBJ, )
    SYNC_PROP_SET(world_event, none, SetWorldEvent(_val.Sym()))
    SYNC_PROP_SET(clip, ClosestMove(), )
    SYNC_PROP_SET(practice, Symbol(), )
    SYNC_PROP_SET(move, Symbol(), )
    SYNC_PROP_SET(move_instance, Symbol(), )
    SYNC_PROP_SET(move_parents, Symbol(), )
    SYNC_PROP_SET(clip_crossover, Symbol(), )
    SYNC_PROP(merger, mMerger)
    SYNC_PROP(game_mode_merger, mGameModeMerger)
    SYNC_PROP(move_merger, mMoveMerger)
    SYNC_PROP(disable_picking, mDisablePicking)
    SYNC_PROP_SET(player_freestyle, mPlayerFreestyle, UpdatePlayerFreestyle(_val.Int()))
    SYNC_PROP_SET(
        pause_player_freestyle, mPlayerFreestylePaused, PausePlayerFreestyle(_val.Int())
    )
    SYNC_PROP(force_postproc, mForcePostProc)
    SYNC_PROP(force_postproc_blend, mForcePostProcBlend)
    SYNC_PROP(force_postproc_blend_rate, mForcePostProcBlendRate)
    SYNC_PROP(disabled, mDisabled)
    SYNC_PROP(excitement, mExcitement)
    SYNC_PROP(num_players_failed, mNumPlayersFailed)
    SYNC_PROP(cam_postproc, mCamPostProc)
    SYNC_PROP_SET(cur_shot, mCurShot.Ptr(), )
    SYNC_PROP_SET(cur_world, mVenue.Ptr(), )
    SYNC_PROP_SET(backup_drift, mBackupDrift, )
    SYNC_PROP_SET(spot_instructor, Symbol("off"), SetCharSpot("instructor", _val.Sym()))
    SYNC_PROP(practice_start, mPracticeStart)
    SYNC_PROP(practice_end, mPracticeEnd)
    SYNC_PROP(start_loop_margin, mStartLoopMargin)
    SYNC_PROP(end_loop_margin, mEndLoopMargin)
    SYNC_PROP(blend_debug, mBlendDebug)
    SYNC_PROP(no_transitions, mNoTransitions)
    SYNC_PROP(collision_checks, mCollisionChecks)
    SYNC_PROP_SET(
        dancer_face_clip, GetMainFaceOverrideClip(), SetMainFaceOverrideClip(_val.Sym())
    )
    SYNC_PROP_SET(
        dancer_face_weight,
        GetMainFaceOverrideWeight(),
        SetMainFaceOverrideWeight(_val.Float())
    )
    SYNC_PROP(freestyle_enabled, mFreestyleEnabled)
    SYNC_PROP(loaded_new_song, mLoadedNewSong)
    SYNC_SUPERCLASS(RndDrawable)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(HamDirector)
    SAVE_REVS(9, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    SAVE_SUPERCLASS(RndPollable)
    SAVE_SUPERCLASS(RndDrawable)
    bs << mPracticeStart;
    bs << mPracticeEnd;
    bs << mBlendDebug;
    bs << mNoTransitions;
    bs << mCollisionChecks;
    bs << mStartLoopMargin;
    bs << mEndLoopMargin;
END_SAVES

BEGIN_COPYS(HamDirector)
    COPY_SUPERCLASS(Hmx::Object)
    COPY_SUPERCLASS(RndPollable)
    CREATE_COPY(HamDirector)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mPracticeStart)
        COPY_MEMBER(mPracticeEnd)
        COPY_MEMBER(mBlendDebug)
        COPY_MEMBER(mNoTransitions)
        COPY_MEMBER(mCollisionChecks)
        COPY_MEMBER(mStartLoopMargin)
        COPY_MEMBER(mEndLoopMargin)
    END_COPYING_MEMBERS
END_COPYS

INIT_REVS(9, 0)

BEGIN_LOADS(HamDirector)
    LOAD_REVS(bs)
    ASSERT_REVS(9, 0)
    Hmx::Object::Load(bs);
    RndPollable::Load(bs);
    RndDrawable::Load(bs);
    if (d.rev < 7) {
        Symbol s;
        d >> s;
    }
    if (d.rev < 8) {
        int x;
        d >> x;
    }
    if (d.rev > 2) {
        d >> mPracticeStart;
        d >> mPracticeEnd;
    }
    if (d.rev > 3) {
        d >> mBlendDebug;
    }
    if (d.rev > 4) {
        d >> mNoTransitions;
    }
    if (d.rev > 8) {
        d >> mCollisionChecks;
    }
    if (d.rev > 5) {
        d >> mStartLoopMargin;
        d >> mEndLoopMargin;
    }
END_LOADS

void HamDirector::Enter() {
    RndPollable::Enter();
#ifdef HX_NATIVE
    fprintf(stderr, "DC3 HamDirector::Enter() — mMerger=%p mVenue=%p\n",
            (void*)mMerger.Ptr(), (void*)mVenue.Ptr());
    // No Kinect on native — force merge_moves=0 so SongAnim() returns the
    // pre-authored difficulty song.anim instead of the empty routine builder.
    if (TheHamProvider) {
        TheHamProvider->SetProperty("merge_moves", 0);
    }
#endif
    if (mMerger) {
        mExcitement = 3;
        mNumPlayersFailed = 0;
        mLastShotTime = -kHugeFloat;
        mLastCollisionTime = -kHugeFloat;
        mShot = "";
        mCurShot = nullptr;
        mSyncScene = true;
        mGameStartHold = false;
        mWorldPostProc = GetWorld()->Find<RndPostProc>("world.pp", true);
        RndPostProc *start = GetWorld()->Find<RndPostProc>("world_start.pp", true);
        if (start) {
            mWorldPostProc->Copy(start, kCopyDeep);
        }
        mWorldPostProc->Select();
        mPostProcInterpA = mWorldPostProc;
        mPostProcInterpB = mWorldPostProc;
        mPostProcInterpBlend = 0;
        mCamPostProc = nullptr;
        mForcePostProc = nullptr;
        mForcePostProcBlend = 0;
        mForcePostProcBlendRate = 1;
        mSavedForcePostProc = nullptr;
        mVisualizerPostProc =
            mVisualizer ? mVisualizer->Find<RndPostProc>("viz_start.pp", false) : nullptr;
        unk2e4 = -kHugeFloat;
        mDisabled = false;
        mVisualizerRunning = false;
        static Message msg("set_force_postproc_no_blend", "performance_high");
        HandleType(msg);
        if (TheHamWardrobe) {
            TheHamWardrobe->ClearCrowd();
        }
        if (mVenue) {
            VenueEnter(mVenue);
        }
        Initialize();
        RndPropAnim *anim = SongAnim(0);
#ifdef HX_NATIVE
        fprintf(stderr, "DC3 HamDirector::Enter() — SongAnim(0)=%p mClipDir=%p mMoveDir=%p\n",
                (void*)anim, (void*)mClipDir.Ptr(), mMoveDir ? (void*)mMoveDir.Ptr() : nullptr);
        fprintf(stderr, "DC3 HamDirector::Enter() — mSongAnims: easy=%p med=%p expert=%p\n",
                (void*)mSongAnims[kDifficultyEasy].Ptr(),
                (void*)mSongAnims[kDifficultyMedium].Ptr(),
                (void*)mSongAnims[kDifficultyExpert].Ptr());
        fprintf(stderr, "DC3 HamDirector::Enter() — routineBuilder: p1=%p p2=%p merge_moves=%d\n",
                (void*)mPlayer1RoutineBuilderAnim.Ptr(),
                (void*)mPlayer2RoutineBuilderAnim.Ptr(),
                TheHamProvider->Property("merge_moves", true)->Int());
#endif
        if (anim) {
            anim->StartAnim();
            anim->SetFrame(-kHugeFloat, 1);
        }
        mDisablePicking = false;
        mNextShot = nullptr;
        mPlayerFreestyle = false;
        SyncScene();
        PlayIntroShot();
        unk1d4 = 0;
        mPlayerFreestylePaused = false;
        if (mVisualizer) {
            mVisualizer->SetShowing(false);
        }
        if (TheHamWardrobe) {
            TheHamWardrobe->PlayCrowdAnimation("realtime_idle", 2, true);
        }
    }
}

void HamDirector::Exit() {
    RndPollable::Exit();
    RndPropAnim *anim = SongAnim(0);
    if (anim) {
        anim->EndAnim();
    }
    if (mVenue) {
        mVenue->Exit();
    }
}

void HamDirector::ListPollChildren(std::list<RndPollable *> &polls) const {
    if (mVenue) {
        polls.push_back(mVenue);
    }
#ifdef HX_NATIVE
    // Poll the HUD dir so its Flows can tick (advance keyframes that
    // control material alpha/color). Without polling, activated flows
    // never execute and HUD elements stay at default (transparent) state.
    if (mHudDir) {
        polls.push_back(mHudDir);
    }
#endif
}

void HamDirector::DrawShowing() {
    static Symbol hide_venue("hide_venue");
    bool hide = TheHamProvider->Property(hide_venue, true)->Int();
    if (mVenue && !hide) {
        mVenue->DrawShowing();
    }
#ifdef HX_NATIVE
    // Draw the HUD overlay after the venue so it renders on top.
    // The HUD PanelDir (kept intact via proxy mode) switches to its own
    // camera (Cam.cam), draws score/flashcards/phrase meters, then restores.
    if (mHudDir) {
        mHudDir->DrawShowing();
    }
#endif
}

void HamDirector::ListDrawChildren(std::list<RndDrawable *> &draws) {
    if (mVenue) {
        draws.push_back(mVenue);
    }
}

void HamDirector::CollideList(const Segment &s, std::list<Collision> &colls) {
    if (mVenue) {
        mVenue->CollideList(s, colls);
    }
    RndDrawable::CollideList(s, colls);
}

DataNode HamDirector::OnSaveSong(DataArray *) { return 0; }
DataNode HamDirector::OnSaveFaceAnims(DataArray *) { return 0; }
DataNode HamDirector::OnFileMerged(DataArray *a) {
#ifdef HX_NATIVE
    Symbol cat = a->Sym(2);
    if (cat == Symbol("game_hud") && mGameModeMerger) {
        // _default_hud.milo loads as RndDir 'game_mode_hud' (top-level container)
        // with PanelDir 'hud' inside (from hud_shared.milo — has DTA type handlers).
        // With proxy mode, the loaded dir stays intact as a subdir.
        FileMerger::Merger *gm = mGameModeMerger->FindMerger("game_hud", false);
        RndDir *hudContainer = nullptr;
        if (gm && !gm->mLoadedObjects.empty()) {
            hudContainer = dynamic_cast<RndDir *>(gm->mLoadedObjects.front());
        }
        if (hudContainer) {
            // Draw the entire container (game_mode_hud) after venue
            mHudDir = hudContainer;
            // Find the PanelDir 'hud' inside — it has the DTA type handlers
            // from hud_objects.dta that set $hud_panel and init animation flows
            // Search subdirs for the HUD PanelDir
            PanelDir *hudPanel = nullptr;
            for (ObjDirItr<PanelDir> it(hudContainer, true); it != nullptr; ++it) {
                // hud_objects.dta defines type 'hud' on the PanelDir
                if (!strcmp(it->Name(), "hud") || !strcmp(it->Name(), "hud1")) {
                    hudPanel = &*it;
                    break;
                }
            }
            if (hudPanel) {
                hudPanel->Enter();
                DataVariable("hud_panel") = (Hmx::Object *)hudPanel;
                fprintf(stderr, "DC3 HamDirector: HUD container '%s' + PanelDir '%s' entered\n",
                        hudContainer->Name(), hudPanel->Name());
            } else {
                // The 'hud' PanelDir was merged into the container during
                // _default_hud.milo internal loading. Use the container itself
                // as $hud_panel — it has all the objects DTA references.
                hudContainer->Enter();
                DataVariable("hud_panel") = (Hmx::Object *)hudContainer;
                // Activate initialization flows
                Flow *resetFlow = hudContainer->Find<Flow>("reset_common.flow", false);
                if (resetFlow) resetFlow->Activate();
                fprintf(stderr, "DC3 HamDirector: HUD container '%s' entered, $hud_panel set, reset_common activated\n",
                        hudContainer->Name());
            }
        } else {
            fprintf(stderr, "DC3 HamDirector: WARNING — no HUD dir found in game_hud merger\n");
        }
    }
#endif
    return 0;
}

void HamDirector::ForceScene(Symbol s) {
    mForcedScene = s;
    mForcedMiniVenue = gNullStr;
}

__forceinline void HamDirector::ForceMiniVenue(Symbol s) {
    Symbol idk(gNullStr);
    mForcedMiniVenue = idk;
    mForcedMiniVenue = s;
}

void HamDirector::DrawDebug() {
    if (mPoseFatalities)
        mPoseFatalities->DrawDebug();
}

void HamDirector::ArmMultiIntroMode() {
    mGameStartHold = true;
    mDisablePicking = true;
}

void HamDirector::HudEntered() {
    if (mPoseFatalities)
        mPoseFatalities->Enter();
}

void HamDirector::PlayIntroShot() {
    if (!mIntroShot)
        PickIntroShot();
    if (!mSuppressIntroShot) {
        if (mIntroShot) {
            static Message msg("set_intro_shot", 0);
            msg[0] = mIntroShot.Ptr();
            DataNode handled = HandleType(msg);
            mNextShot = mIntroShot;
            mIntroShot = nullptr;
        } else
            FindNextShot();
        PlayNextShot();
    }
}

void HamDirector::SetupAnims() {
    mSongAnims.clear();
    mDancerFaceAnims.clear();
    for (int i = 0; i < 3; i++) {
        Difficulty d = (Difficulty)i;
        mSongAnims[d] = GetPropAnim(d, "song.anim", true);
        mDancerFaceAnims[d] = GetPropAnim(d, "dancer_face.anim", false);
    }
    SetupRoutineBuilderAnims();
    mClipDir = mMerger->Dir()->Find<ObjectDir>("clips", false);
    mMoveDir = mMerger->Dir()->Find<ObjectDir>("moves", false);
    ObjDirItr<SongCollision> it(mMoveDir, true);
    if (it)
        mSongCollision = &*it;
}

void HamDirector::RemapSongAnimToTempoMap(TempoMap *newTempoMap) {
    TempoMap *existingTempoMap = HamSongData::sInstance->GetTempoMap();
    DataArrayPtr clipProp(Symbol("clip"));
    DataArrayPtr moveProp(Symbol("move"));
    DataArrayPtr practiceProp(Symbol("practice"));
    existingTempoMap->TickToTime(1920.0f);
    newTempoMap->TickToTime(1920.0f);
    for (int diff = 0; diff < kNumDifficultiesDC2; diff++) {
        RndPropAnim *songAnim = TheHamDirector->SongAnimByDifficulty((Difficulty)diff);
        if (songAnim) {
            PropKeys *clipPK = songAnim->GetKeys(TheHamDirector, clipProp);
            PropKeys *movePK = songAnim->GetKeys(TheHamDirector, moveProp);
            PropKeys *practicePK = songAnim->GetKeys(TheHamDirector, practiceProp);
            Keys<Symbol, Symbol> *moveSymKeys = movePK->AsSymbolKeys();
            Keys<Symbol, Symbol> *clipSymKeys = clipPK->AsSymbolKeys();
            Keys<Symbol, Symbol> *practiceSymKeys = practicePK->AsSymbolKeys();
            unsigned int moveCount = moveSymKeys->size();
            unsigned int clipCount = clipSymKeys->size();
            unsigned int practiceCount = practiceSymKeys->size();
            for (unsigned int i = 0; i < moveCount; i++) {
                float frame = moveSymKeys->at(i).frame;
                float ms = frame * (1000.0f / 30.0f);
                float tick = newTempoMap->TimeToTick(ms);
                float newMs = existingTempoMap->TickToTime(tick);
                movePK->ChangeFrame(i, newMs * (30.0f / 1000.0f), false);
            }
            for (unsigned int i = 0; i < clipCount; i++) {
                float frame = clipSymKeys->at(i).frame;
                float ms = frame * (1000.0f / 30.0f);
                float tick = newTempoMap->TimeToTick(ms);
                float newMs = existingTempoMap->TickToTime(tick);
                clipPK->ChangeFrame(i, newMs * (30.0f / 1000.0f), false);
            }
            for (unsigned int i = 0; i < practiceCount; i++) {
                float frame = practiceSymKeys->at(i).frame;
                float ms = frame * (1000.0f / 30.0f);
                float tick = newTempoMap->TimeToTick(ms);
                float newMs = existingTempoMap->TickToTime(tick);
                practicePK->ChangeFrame(i, newMs * (30.0f / 1000.0f), true);
            }
        }
    }
}

WorldDir *HamDirector::GetWorld() {
    return mMerger ? dynamic_cast<WorldDir *>(mMerger->Dir()) : nullptr;
}

WorldDir *HamDirector::GetVenueWorld() { return mVenue; }

void HamDirector::Initialize() {
    SetupAnims();
    ObjectDir *iconManDir = GetWorld()->Find<ObjectDir>("iconmandir", false);
    if (iconManDir) {
        mIconManChar = iconManDir->Find<Character>("iconman", false);
        if (mIconManChar) {
            RndAnimatable *anim =
                mIconManChar->Find<RndAnimatable>("outline.anim", false);
            if (anim)
                anim->SetFrame(1, 1);
        }
        mIconManTex = iconManDir->Find<RndTexRenderer>("iconman.rndtex", false);
    }
    delete mPoseFatalities;
    mPoseFatalities = Hmx::Object::New<PoseFatalities>();
}

RndPropAnim *HamDirector::SongAnim(int playerIndex) {
    if (!mSongAnims[kDifficultyEasy]) {
        return nullptr;
    }
    MILO_ASSERT((0) <= (playerIndex) && (playerIndex) < (2), 0x620);
    // When merge_moves=1 (perform mode), the routine builder system dynamically
    // populates a separate anim with clip/move keyframes based on the generated
    // choreography (SelectMove → AddRoutineMove → InsertMoveInSong).
    if (TheHamProvider->Property("merge_moves", true)->Int()) {
        RndPropAnim *routineAnim = playerIndex == 0 ? mPlayer1RoutineBuilderAnim
                                                     : mPlayer2RoutineBuilderAnim;
#ifdef HX_NATIVE
        // Fallback: the routine builder anim's clip keys are cleared by
        // SetupRoutineBuilderAnims() and repopulated by the DanceRemixer
        // (OriginalChoreoRemixer::Reset → SelectMove → InsertMoveInSong).
        // If the remixer never ran (DTA handlers didn't fire), the routine
        // builder has zero clip keys. Fall back to the pre-authored song.anim
        // which has all clip keyframes baked in for the difficulty.
        if (routineAnim) {
            static Symbol sClip("clip");
            PropKeys *clipKeys = routineAnim->GetKeys(this, DataArrayPtr(sClip));
            if (!clipKeys || clipKeys->NumKeys() == 0) {
                static int sLog = 0;
                if (sLog++ < 3)
                    MILO_LOG("SongAnim(%d): routine builder empty, falling back to expert anim\n", playerIndex);
                // Use the expert/master anim so mClipKeys == mMasterClipKeys
                // in ClipPlayer, which routes to PushExpertClip (direct clip
                // lookup). The PushClip path requires practice-frame mapping
                // that fails without the full remixer pipeline.
                return SongAnimByDifficulty(kDifficultyExpert);
            }
        }
#endif
        return routineAnim;
    }
    // With merge_moves=0 (holla_back, campaign outro, practice), use the
    // difficulty-specific song.anim directly.
use_preauthored:
    HamPlayerData *hpd = TheGameData->Player(playerIndex);
    return SongAnimByDifficulty(LegacyDifficulty(hpd->GetDifficulty()));
}

PropKeys *HamDirector::GetPropKeys(Difficulty d, Symbol s) {
    RndPropAnim *anim = GetPropAnim(d, "song.anim", false);
    if (!anim) {
        return nullptr;
    }
    return anim->GetKeys(this, DataArrayPtr(s));
}

void HamDirector::VenueEnter(WorldDir *dir) {
#ifdef HX_NATIVE
    fprintf(stderr, "DC3 HamDirector::VenueEnter(%p)\n", (void*)dir);
#endif
    if (dir) {
        // Xbox venue has no type after SetSubDir(true) clears TypeDef.
        // select_camera fires on the world root (not venue), so venue type
        // is irrelevant for camera management.
        dir->Enter();
    }
    mPlayer0Char = dir ? dir->Find<HamCharacter>("player0", true) : nullptr;
    mPlayer1Char = dir ? dir->Find<HamCharacter>("player1", true) : nullptr;
    mBackup0Char = dir ? dir->Find<HamCharacter>("backup0", true) : nullptr;
    mBackup1Char = dir ? dir->Find<HamCharacter>("backup1", true) : nullptr;
#ifdef HX_NATIVE
    fprintf(stderr, "DC3 HamDirector::VenueEnter — player0=%p player1=%p backup0=%p backup1=%p\n",
            (void*)mPlayer0Char.Ptr(), (void*)mPlayer1Char.Ptr(),
            (void*)mBackup0Char.Ptr(), (void*)mBackup1Char.Ptr());
#endif

    RndTransformable *p0 =
        dir ? dir->Find<RndTransformable>("player0.trans", true) : nullptr;
    RndTransformable *p1 =
        dir ? dir->Find<RndTransformable>("player1.trans", true) : nullptr;
    RndTransformable *b0 =
        dir ? dir->Find<RndTransformable>("backup0.trans", true) : nullptr;
    RndTransformable *b1 =
        dir ? dir->Find<RndTransformable>("backup1.trans", true) : nullptr;

    if (b1) {
        MILO_LOG(
            "(%7.2f,%7.2f,%7.2f)\n",
            b1->LocalXfm().v.x,
            b1->LocalXfm().v.y,
            b1->LocalXfm().v.z
        );
    } else {
        MILO_LOG("NULL\n");
    }

    if (p0) {
        p0->SetLocalXfm(Transform::IDXfm());
    }
    if (p1) {
        p1->SetLocalXfm(Transform::IDXfm());
    }
    if (b0) {
        b0->SetLocalXfm(Transform::IDXfm());
    }
    if (b1) {
        b1->SetLocalXfm(Transform::IDXfm());
    }
    mBackupHidden = false;
    for (int i = 0; i < 4; i++) {
        mCharsShowing[i] = false;
    }
}

void HamDirector::SetMasterClipAnim() {
    WorldDir *dir = GetWorld();
    if (dir) {
        ObjectDir *clipDir = dir->Find<ObjectDir>("master_clips", false);
        if (clipDir) {
            mMasterClipAnim = clipDir->Find<RndPropAnim>("song.anim", false);
        }
        if (!mMasterClipAnim) {
            mMasterClipAnim = GetPropAnim(kDifficultyExpert, "song.anim", false);
        }
    }
}

void HamDirector::PickIntroShot() {
    if (!DataVariable("skip_intro").Int()) {
        mNextShot = nullptr;
        static Message m("pick_intro_shot");
        DataNode n = HandleType(m);
        mIntroShot = mNextShot;
        mNextShot = nullptr;
    }
}

void HamDirector::ForceShot(const char *name) {
    if (mVenue) {
        mNextShot = mVenue->Find<HamCamShot>(name, false);
        mDisablePicking = mNextShot;
    }
}

PropKeys *HamDirector::GetMasterKeys(Symbol s) {
    if (!mMasterClipAnim) {
        SetMasterClipAnim();
    }
    if (!mMasterClipAnim) {
        MILO_NOTIFY(
            "HamDirector::GetMasterKeys: no master clip anim, can't return PropKeys."
        );
        return nullptr;
    } else {
        return mMasterClipAnim->GetKeys(this, DataArrayPtr(s));
    }
}

Key<Symbol> *HamDirector::GetMasterPracticeFrame(Symbol s) {
    if (!mMasterClipAnim) {
        SetMasterClipAnim();
    }
    MILO_ASSERT(mMasterClipAnim, 0x23E);
    static Symbol practice("practice");
    PropKeys *keys = mMasterClipAnim->GetKeys(this, DataArrayPtr(practice));
    if (keys) {
        Keys<Symbol, Symbol> *symKeys = keys->AsSymbolKeys();
        int i = 0;
        for (; i < symKeys->size(); i++) {
            if (s == (*symKeys)[i].value) {
                goto done;
            }
        }
        i = -1;
    done:
        if (i != -1) {
            return &(*symKeys)[i];
        }
    }
    return nullptr;
}

HamCamShot *HamDirector::FindNextDircut() {
    float secs = TheTaskMgr.Seconds(TaskMgr::kRealTime);
    const DircutEntry *entry = mDirCutKeys.Cross(secs, secs - TheTaskMgr.DeltaSeconds());
    HamCamShot *shot = nullptr;
#ifdef HX_NATIVE
    if (!entry)
        return shot;
#endif
    if (mNumPlayersFailed || (entry->mForced && mExcitement >= 3)) {
            shot = entry->mShot;
            if (shot) {
                mPickNewShot = true;
            }
        };
    return shot;
}

void HamDirector::SetDircut(Symbol s, std::vector<CameraManager::PropertyFilter> filters) {
    static Symbol gameplay_mode("gameplay_mode");
    static Symbol holla_back("holla_back");
    if (TheHamProvider->Property(gameplay_mode, true)->Sym() == holla_back) {
        return;
    } else {
        MILO_LOG("HamDirector::SetDircut cat = '%s'\n", s.Str());
        mNextShot = dynamic_cast<HamCamShot *>(
            mVenue->GetCameraManager()->FindCameraShot(s, filters)
        );
        MILO_LOG("   mNextShot = '%s'\n", SafeName(mNextShot));
    }
}

void HamDirector::SetupRoutineBuilderAnims() {
    for (int i = 0; i < 2; i++) {
        RndPropAnim *routineBuilderAnim;
        if (i == 0) {
            mPlayer1RoutineBuilderAnim =
                GetWorld()->Find<RndPropAnim>("player_1_routine_builder.anim", true);
            routineBuilderAnim = mPlayer1RoutineBuilderAnim;
        } else {
            mPlayer2RoutineBuilderAnim =
                GetWorld()->Find<RndPropAnim>("player_2_routine_builder.anim", true);
            routineBuilderAnim = mPlayer2RoutineBuilderAnim;
        }
        HamPlayerData *hpd = TheGameData->Player(i);
        RndPropAnim *anim = mSongAnims[LegacyDifficulty(hpd->GetDifficulty())];
        if (anim) {
            routineBuilderAnim->Copy(anim, kCopyDeep);
#ifdef HX_NATIVE
            // After Copy, PropKeys still target the source HamDirector.
            // Retarget any pointing to a different HamDirector so camera
            // shots and visibility commands fire on this director.
            for (auto it = routineBuilderAnim->mPropKeys.begin();
                 it != routineBuilderAnim->mPropKeys.end(); ++it) {
                Hmx::Object *t = (*it)->Target();
                if (t && t != this && dynamic_cast<HamDirector *>(t)) {
                    (*it)->SetTarget(this);
                }
            }
#endif
            Symbol syms[3] = { "clip", "move", "practice" };
            for (int i = 0; i < 3; i++) {
                DataArrayPtr ptr(syms[i]);
                routineBuilderAnim->GetKeys(this, ptr)->AsSymbolKeys()->clear();
            }
        }
    }
}

RndPropAnim *HamDirector::SongAnimByDifficulty(Difficulty diff) {
    MILO_ASSERT((0) <= (diff) && (diff) < (kNumDifficultiesDC2), 0x633);
    return mSongAnims[diff];
}

RndPropAnim *HamDirector::DancerFaceAnimByPlayer(int player) {
    return mDancerFaceAnims[LegacyDifficulty(TheGameData->Player(player)->GetDifficulty())];
}

void HamDirector::AddNumPlayers(
    std::vector<CameraManager::PropertyFilter> &filters, DataArray *arr
) {
    CameraManager::PropertyFilter filter;
    if (arr) {
        filter.prop = arr->Sym(0);
        filter.match = arr->Array(1);
    } else {
        static Symbol player_flag("player_flag");
        filter.prop = player_flag;
        static Symbol cam_player_config("cam_player_config");
        DataArrayPtr ptr(3, TheHamProvider->Property(cam_player_config, true)->Int());
        filter.match = (DataArray *)ptr;
    }
    filters.push_back(filter);
}

PropKeys *HamDirector::GetPropKeysByPlayer(int player, Symbol s) {
    RndPropAnim *anim = SongAnim(player);
    if (!anim) {
        return nullptr;
    } else {
        return anim->GetKeys(this, DataArrayPtr(s));
    }
}

Symbol HamDirector::MoveNameFromBeat(float f1, int player) {
    RndPropAnim *anim = SongAnim(player);
    if (!anim)
        return gNullStr;
    else {
        PropKeys *keys = anim->GetKeys(this, DataArrayPtr(Symbol("move")));
        if (!keys)
            return gNullStr;
        else {
            Symbol ret;
            float frame = BeatToFrame(f1);
            Keys<Symbol, Symbol> *symKeys = keys->AsSymbolKeys();
            symKeys->AtFrame(frame, ret);
            return ret;
        }
    }
}

void HamDirector::TriggerNextIntro() {
    mDisablePicking = false;
    std::vector<CameraManager::PropertyFilter> filters;
    static Symbol s("CAMP_SONG1_INTRO_CONTINUE");
    SetDircut(s, filters);
    mIntroShot = mNextShot;
    mNextShot = nullptr;
    PlayIntroShot();
    mGameStartHold = false;
}

void HamDirector::ReactToCollision_InsertRealShot(Symbol s, float f2) {
    static Symbol shot("shot");
    PropKeys *keys = GetPropKeysByPlayer(0, shot);
    Keys<Symbol, Symbol> *shot_keys = keys->AsSymbolKeys();
    MILO_ASSERT(shot_keys, 0xE08);
    shot_keys->Add(s, BeatToFrame(TheTaskMgr.Beat()), false);
}

void HamDirector::ReactToCollision_MoveShot(int shotIdx, float beat) {
    static Symbol shot("shot");
    PropKeys *shot_keys = GetPropKeysByPlayer(0, shot);
    MILO_ASSERT(shot_keys, 0xE10);
    shot_keys->ChangeFrame(shotIdx, BeatToFrame(beat), true);
}

bool AreDancersColliding1D(
    std::vector<RndTransformable *> &,
    std::vector<RndTransformable *> &,
    const Vector3 &,
    const Vector3 &
);

bool HamDirector::AreCharactersColliding() {
    HamCharacter *chars[2];
    std::vector<RndTransformable *> bones[2];
    for (int i = 0; i < 2; i++) {
        chars[i] = TheHamWardrobe ? TheHamWardrobe->GetCharacter(i) : nullptr;
        if (!chars[i])
            return false;
        SongCollision::GatherUsefulBones(bones[i], chars[i]);
    }
    return AreDancersColliding1D(
        bones[0], bones[1], chars[0]->WorldXfm().v, chars[1]->WorldXfm().v
    );
}

bool HamDirector::ShouldDoCollisionPrevention() const {
    if (TheLoadMgr.EditMode() && !mCollisionChecks) {
        return false;
    } else {
        static Symbol cam_player_config("cam_player_config");
        return TheHamProvider->Property(cam_player_config, true)->Int() == 2;
    }
}

void HamDirector::StartStopVisualizer(bool b1, int i2) {
    if (mVisualizer && mVisualizerRunning != b1) {
        mVisualizerRunning = b1;
        mVisualizer->SetShowing(b1);
        mVisualizer->Run(b1);
        if (b1) {
            mVisualizer->Find<Flow>("enter_timeywimey.flow", true)->Activate();
        } else {
            if (mVisualizerPostProc) {
                mVisualizerPostProc->Unselect();
            }
            switch (i2) {
            case 0:
                mVisualizer->Find<Flow>("exit_timeywimey.flow", true)->Activate();
                break;
            case 1:
                mVisualizer->Find<Flow>("exit_timeywimey_fast.flow", true)->Activate();
                break;
            case 2:
                mVisualizer->Find<Flow>("exit_timeywimey_totimeywimey.flow", true)
                    ->Activate();
                mVisualizer->SetShowing(false);
                break;
            default:
                break;
            }
        }
    }
}

void HamDirector::UnselectVisualizerPostProc() {
    if (mVisualizerPostProc)
        mVisualizerPostProc->Unselect();
}

void HamDirector::ReselectWorldPostProc() {
    MILO_LOG("HamDirector::ReselectWorldPostProc()\n");
    if (mWorldPostProc)
        mWorldPostProc->Select();
}

void HamDirector::StartStopVisualizer() {
    if (mVisualizer) {
        mVisualizer->SetShowing(mPlayerFreestyle);
    }
    if (mVisualizer) {
        StartStopVisualizer(mPlayerFreestyle, 1);
    }
}

void HamDirector::UpdatePlayerFreestyle(bool b1) {
    if (b1 != mPlayerFreestyle) {
        static Symbol in_freestyle("in_freestyle");
        static Symbol game_stage("game_stage");
        mPlayerFreestyle = b1;
        if (mPlayerFreestyle) {
            unk1d4 = 0;
            mSavedForcePostProc = mForcePostProc;
            mForcePostProc = mVisualizerPostProc;
            mForcePostProcBlend = 0;
            mForcePostProcBlendRate = 0.625;
            if (GetWorld()) {
                static Symbol freestyle("freestyle");
                TheHamProvider->SetProperty(game_stage, freestyle);
                HamPlayerData *pPlayer0 = TheGameData->Player(0);
                MILO_ASSERT(pPlayer0, 0xEDF);
                HamPlayerData *pPlayer1 = TheGameData->Player(1);
                MILO_ASSERT(pPlayer1, 0xEE1);
                PropertyEventProvider *pPlayer0Provider = pPlayer0->Provider();
                MILO_ASSERT(pPlayer0Provider, 0xEE4);
                PropertyEventProvider *pPlayer1Provider = pPlayer1->Provider();
                MILO_ASSERT(pPlayer1Provider, 0xEE6);
                bool p1InFreestyle = pPlayer1->InFreestyle();
                pPlayer0Provider->SetProperty(in_freestyle, pPlayer0->InFreestyle());
                pPlayer1Provider->SetProperty(in_freestyle, p1InFreestyle);
            }
        } else {
            StartStopVisualizer();
            mForcePostProc = mSavedForcePostProc;
            mForcePostProcBlendRate = 0;
            mForcePostProcBlend = 1;
            if (GetWorld()) {
                static Symbol playing("playing");
                TheHamProvider->SetProperty(game_stage, playing);
                HamPlayerData *pPlayer0 = TheGameData->Player(0);
                MILO_ASSERT(pPlayer0, 0xEFA);
                HamPlayerData *pPlayer1 = TheGameData->Player(1);
                MILO_ASSERT(pPlayer1, 0xEFC);
                PropertyEventProvider *pPlayer0Provider = pPlayer0->Provider();
                MILO_ASSERT(pPlayer0Provider, 0xEFF);
                PropertyEventProvider *pPlayer1Provider = pPlayer1->Provider();
                MILO_ASSERT(pPlayer1Provider, 0xF01);
                pPlayer0Provider->SetProperty(in_freestyle, false);
                pPlayer1Provider->SetProperty(in_freestyle, false);
            }
        }
    }
}

void HamDirector::SetWorldEvent(Symbol event) {
    static Symbol none("none");
    if (event != none && mVenue) {
        static Message msg("");
        msg.SetType(event);
        mVenue->Handle(msg, false);
    }
}

void HamDirector::SendCurWorldMsg(Symbol s, bool b2) {
    static Message msg("");
    if (mVenue) {
        msg.SetType(s);
        if (b2) {
            mVenue->HandleType(msg);
        } else {
            mVenue->Handle(msg, false);
        }
    }
}

void HamDirector::SetCharSpot(Symbol charType, Symbol spotState) {
    const char *str = MakeString("spotlight_%s_%s", charType.Str(), spotState.Str());
    SendCurWorldMsg(Symbol(str), false);
}

DataNode HamDirector::OnToggleCamshotFlag() { return mCamshotFlag = !mCamshotFlag; }

DataNode HamDirector::OnLoadSong(DataArray *a) {
    FilePathTracker tracker(FileRoot());
    MILO_ASSERT(TheGameData, 0xC1D);
#ifdef HX_NATIVE
    fprintf(stderr, "DC3 HamDirector::OnLoadSong() — song='%s' venue='%s' mMerger=%p\n",
            a->Str(2), TheGameData->Venue().Str(), (void*)mMerger.Ptr());
#endif
    for (int i = 0; i < 2; i++) {
        HamPlayerData *hpd = TheGameData->Player(i);
        MILO_ASSERT(hpd, 0xC21);
        mCrews[i] = hpd->Crew();
        mCharacterOutfits[i] = hpd->CharacterOutfit(mCrews[i]);
    }
    int i3 = a->Int(3);
    bool i4 = a->Int(4);
    bool b5 = a->Int(5);
    String str(a->Str(2));
    int dancers = a->Int(6);
    MILO_ASSERT(dancers >= 0 && dancers < kBackupDancersNumTypes, 0xC2E);
    mBackupDancers = (HamBackupDancers)dancers;
    mLoadedNewSong = true;
    if (mMerger && !str.empty()) {
        const char *speed;
        if (i3 < 113)
            speed = "slow";
        else if (i3 < 136)
            speed = "medium";
        else
            speed = "fast";
        mSongSpeed = speed;
        auto _tmp1 = FileGetBase(str.c_str());
        TheGameData->SetSong(_tmp1);
        mMerger->Select("song", str.c_str(), true);
        if (i4) {
            mMerger->StartLoad(b5);
            if (mVenue) {
                FileMerger *extras = mVenue->Find<FileMerger>("extras.fm", false);
                if (extras) {
                    extras->StartLoad(b5);
                }
            }
        }
    }
    return 0;
}

DataNode HamDirector::OnPostProcs(DataArray *a) {
    DataNode *var1 = a->Var(2);
    DataNode *var2 = a->Var(3);
    DataNode *var3 = a->Var(4);
    DataNode *var4 = a->Var(5);
    *var1 = mWorldPostProc.Ptr();
    *var2 = mCamPostProc.Ptr();
    *var3 = mForcePostProc.Ptr();
    *var4 = mVisualizerPostProc.Ptr();
    return 0;
}

DataNode HamDirector::OnShotOver(DataArray *a) {
    if (strneq(a->Obj<HamCamShot>(2)->Category().Str(), "dc_", 3)) {
        mPickNewShot = true;
    }
    mLastShotTime = -kHugeFloat;
    return 0;
}

DataNode HamDirector::OnListPossibleMoves() {
    if (!TheMoveMgr) {
        MoveMgr::Init("../meta/move_data.dta");
    }
    DataArray *moveArr = new DataArray(0);
    for (std::map<Symbol, MoveParent *>::const_iterator it =
             TheMoveMgr->MoveParents().begin();
         it != TheMoveMgr->MoveParents().end();
         ++it) {
        moveArr->Insert(moveArr->Size(), it->first);
    }
    moveArr->SortNodes(0);
    DataNode ret(moveArr);
    moveArr->Release();
    return ret;
}

DataNode HamDirector::OnListPossibleVariants() {
    if (!TheMoveMgr) {
        MoveMgr::Init("../meta/move_data.dta");
    }
    DataArray *moveArr = new DataArray(0);
    for (std::set<const MoveVariant *>::const_iterator it =
             TheMoveMgr->GetVariants().begin();
         it != TheMoveMgr->GetVariants().end();
         ++it) {
        moveArr->Insert(moveArr->Size(), (*it)->Name());
    }
    moveArr->SortNodes(0);
    DataNode ret(moveArr);
    moveArr->Release();
    return ret;
}

namespace {
    const char *gGrooveName = "groove";
}

DataNode HamDirector::PracticeList(Difficulty d) {
    DataArray *arr = new DataArray(0);
    arr->Insert(0, Symbol());
    PropKeys *keys = GetPropKeys(d, "practice");
    if (keys) {
        Keys<Symbol, Symbol> *symKeys = keys->AsSymbolKeys();
        for (int i = 0; i < symKeys->size(); i++) {
            arr->Insert(arr->Size(), (*symKeys)[i].value);
        }
    }
    arr->Insert(arr->Size(), Symbol(gGrooveName));
    DataNode ret(arr);
    arr->Release();
    return ret;
}

DataNode HamDirector::OnCycleShot(DataArray *a) {
    if (mVenue) {
        mNextShot =
            dynamic_cast<HamCamShot *>(mVenue->GetCameraManager()->ShotAfter(mCurShot));
        mDisablePicking = mNextShot;
    }
    return 0;
}

DataNode HamDirector::OnForceShot(DataArray *a) {
    ForceShot(a->Str(2));
    return 0;
}

void GetVenuePath(FilePath &path, const char *cc) {
    FilePathTracker tracker(FileRoot());
    path.Set(FilePath::Root().c_str(), "");
    if (*cc == '\0')
        return;
    else {
        const char *milo = MakeString("world/%s/%s.milo", cc, cc);
        path.Set(FilePath::Root().c_str(), milo);
    }
}

DataNode HamDirector::OnFileLoaded(DataArray *a) {
    static Symbol song("song");
    static Symbol venue("venue");
    static Symbol viz("viz");
    static Symbol game_hud("game_hud");
    Symbol sym = a->Sym(2);
#ifdef HX_NATIVE
    fprintf(stderr, "DC3 HamDirector::OnFileLoaded('%s') — mMerger=%p mVenue=%p\n",
            sym.Str(), (void*)mMerger.Ptr(), (void*)mVenue.Ptr());
#endif
    if (mMerger) {
        mAsyncLoaded = mMerger->AsyncLoad();
        if (sym == song) {
#ifdef HX_NATIVE
            fprintf(stderr, "DC3 HamDirector::OnFileLoaded('song') — venue='%s' (null=%d) wardrobe=%p outfits=['%s','%s'] crews=['%s','%s']\n",
                    TheGameData->Venue().Str(), TheGameData->Venue().Null(),
                    (void*)TheHamWardrobe,
                    mCharacterOutfits[0].Str(), mCharacterOutfits[1].Str(),
                    mCrews[0].Str(), mCrews[1].Str());
#endif
            if (!TheGameData->Venue().Null()) {
                if (TheHamWardrobe) {
                    TheHamWardrobe->LoadCharacters(
                        mCharacterOutfits[0],
                        mCharacterOutfits[1],
                        mCrews[0],
                        mCrews[1],
                        mBackupDancers,
                        mSongSpeed,
                        TheGameData->Venue().Str(),
                        mAsyncLoaded
                    );
                }
                FilePath path;
                {
                    FilePathTracker tracker(FileRoot());
                    path.Set(FilePath::Root().c_str(), "ui/visualizer/visualizer.milo");
                }
                mMerger->Select("viz", path, false);
                GetVenuePath(path, TheGameData->Venue().Str());
                mMerger->Select("venue", path, false);
                if (mGameModeMerger) {
                    static Message load_game_hud("load_game_hud", 0, 0, 0, 0);
                    mGameModeMerger->HandleType(load_game_hud);
#ifdef HX_NATIVE
                    // Force proxy mode on game_hud merger so the loaded HUD PanelDir
                    // stays intact as a subdir (preserving DTA type handlers for
                    // $hud_panel initialization, animation flows, and score display).
                    // Without proxy, MergeDirs flattens objects and loses type info.
                    FileMerger::Merger *hudMerger = mGameModeMerger->FindMerger("game_hud", false);
                    if (hudMerger) {
                        hudMerger->mProxy = true;
                    }
#endif
                    mGameModeMerger->StartLoad(mAsyncLoaded);
                }
            }
            mMerger->StartLoad(mAsyncLoaded);
        } else {
            ObjectDir *dir = a->Obj<ObjectDir>(3);
            if (sym == venue && dir) {
                mVenue = dynamic_cast<WorldDir *>(dir);
#ifdef HX_NATIVE
                fprintf(stderr, "DC3 HamDirector::OnFileLoaded('venue') — dir=%p mVenue=%p (cast %s)\n",
                        (void*)dir, (void*)mVenue.Ptr(), mVenue ? "OK" : "FAILED");
                // DTA scripts expect video_recorder.srec in the venue world
                // (Kinect video recording). Register a no-op stub so find_obj succeeds.
                if (mVenue && !mVenue->FindObject("video_recorder.srec", false, false)) {
                    Hmx::Object *stub = Hmx::Object::NewObject("Object");
                    stub->SetName("video_recorder.srec", mVenue);
                }
#endif
            } else if (sym == viz && dir) {
                mVisualizer = dynamic_cast<HamVisDir *>(dir);
#ifdef HX_NATIVE
                fprintf(stderr, "DC3 HamDirector::OnFileLoaded('viz') — mVisualizer=%p\n", (void*)mVisualizer.Ptr());
#endif
            }
        }
    }
    return 0;
}

DataNode HamDirector::OnPostProcInterp(DataArray *a) {
    mPostProcInterpA = a->Obj<RndPostProc>(2);
    mPostProcInterpB = a->Obj<RndPostProc>(3);
    mPostProcInterpBlend = a->Float(4);
    return 0;
}

DataNode HamDirector::OnPracticeBeats(DataArray *a) {
    Key<Symbol> *key1;
    Key<Symbol> *key2;
    if (!GetPracticeFrames(key1, key2)) {
        return 0;
    } else {
        *a->Var(2) = FrameToBeat(key1->frame);
        *a->Var(3) = FrameToBeat(key2->frame);
        return 1;
    }
}

DataNode HamDirector::OnClipList(DataArray *a) {
    HamPlayerData *data = TheGameData->Player(0);
    if (data->GetDifficulty() == kDifficultyExpert) {
        return ObjectList(
            mMerger->Dir()->Find<ObjectDir>("clips", true), "CharClip", false
        );
    } else {
        DataNode list = PracticeList(kDifficultyExpert);
        DataArray *arr = list.Array();
        arr->SortNodes(0);
        return list;
    }
}

DataNode HamDirector::OnSetDircut(DataArray *a) {
    if (mVenue && !ShotsDisabled()) {
        Symbol sym = a->Sym(2);
        std::vector<CameraManager::PropertyFilter> filters;
        if (a->Size() > 3) {
            const DataNode &node = a->Evaluate(3);
            DataArray *arr;
            if (node.Type() == kDataInt && node.Int() != 0) {
                arr = nullptr;
            } else {
                MILO_ASSERT(node.Type() == kDataArray, 0xE74);
                arr = node.Array();
            }
            AddNumPlayers(filters, arr);
        }
        SetDircut(sym, filters);
    }
    return (Hmx::Object *)mNextShot;
}

HamCharacter *HamDirector::GetCharacter(int i) const {
    if (TheHamWardrobe) {
        return TheHamWardrobe->GetCharacter(i);
    } else
        return nullptr;
}

HamCharacter *HamDirector::GetBackup(int i) {
    if (TheHamWardrobe) {
        return TheHamWardrobe->GetBackup(i);
    } else
        return nullptr;
}

void HamDirector::ChangePlayerCharacter(int i1, Symbol s1, Symbol s2, Symbol s3) {
    HamPlayerData *hpd = TheGameData->Player(i1);
    hpd->SetCharacter(s1);
    hpd->SetCharacterOutfit(s3);
    mCharacterOutfits[i1] = s1;
    mCrews[i1] = s2;
    TheHamWardrobe->LoadCharacters(
        mCharacterOutfits[0],
        mCharacterOutfits[1],
        mCrews[0],
        mCrews[1],
        mBackupDancers,
        mSongSpeed,
        TheGameData->Venue().Str(),
        true
    );
}

void HamDirector::SetMainFaceOverrideClip(Symbol s) {
    HamCharacter *hChar = GetCharacter(0);
    if (hChar) {
        String str(s);
        hChar->SetFaceOverrideClip(str.c_str(), true);
    }
}

Symbol HamDirector::GetMainFaceOverrideClip() const {
    HamCharacter *hChar = GetCharacter(0);
    if (hChar) {
        Symbol s = hChar->GetFaceOverrideClip();
        return s;
    }
    return Symbol();
}

void HamDirector::SetMainFaceOverrideWeight(float wt) {
    HamCharacter *hChar = GetCharacter(0);
    if (hChar)
        hChar->SetFaceOverrideWeight(wt);
}

float HamDirector::GetMainFaceOverrideWeight() {
    HamCharacter *hChar = GetCharacter(0);
    if (hChar) {
        return hChar->GetFaceOverrideWeight();
    } else
        return 0;
}

void HamDirector::TeleportChars() {
    for (int i = 0; i < 2; i++) {
        HamCharacter *hChar = GetCharacter(i);
        if (hChar)
            hChar->SetTeleport(true);
    }
}

bool HamDirector::SongAnimation() {
    bool ret = false;
    for (int i = 0; i < 2; i++) {
        HamCharacter *hChar = GetCharacter(i);
        if (hChar && hChar->SongAnimation() > -1) {
            ret = true;
            break;
        }
    }
    return ret;
}

void HamDirector::ResyncFaceDrivers() {
    for (int i = 0; i < 2; i++) {
        HamCharacter *hChar = GetCharacter(i);
        if (hChar)
            hChar->ResyncLipSync(nullptr);
    }
    int i = 0;
    while (true) {
        HamCharacter *hChar = GetBackup(i++);
        if (!hChar)
            break;
        else {
            hChar->ResyncLipSync(nullptr);
        }
    }
}

void HamDirector::PlayCharBaseVisemes() {
    for (int i = 0; i < 2; i++) {
        HamCharacter *hChar = GetCharacter(i);
        if (hChar)
            hChar->PlayBaseViseme();
    }
    int i = 0;
    while (true) {
        HamCharacter *hChar = GetBackup(i++);
        if (!hChar)
            break;
        else {
            hChar->PlayBaseViseme();
        }
    }
}

void HamDirector::DisableFacialAnimation() {
    for (int i = 0; i < 2; i++) {
        HamCharacter *hChar = GetCharacter(i);
        if (hChar)
            hChar->DisableFacialAnimation();
    }
    int i = 0;
    while (true) {
        HamCharacter *hChar = GetBackup(i++);
        if (!hChar)
            break;
        else {
            hChar->DisableFacialAnimation();
        }
    }
}

void HamDirector::ResetFacialAnimation() {
    for (int i = 0; i < 2; i++) {
        HamCharacter *hChar = GetCharacter(i);
        if (hChar)
            hChar->ResetFacialAnimation();
    }
    int i = 0;
    while (true) {
        HamCharacter *hChar = GetBackup(i++);
        if (!hChar)
            break;
        else {
            hChar->ResetFacialAnimation();
        }
    }
}

void HamDirector::SetLipsyncOffsets(float f1) {
    for (int i = 0; i < 2; i++) {
        HamCharacter *hChar = GetCharacter(i);
        if (hChar) {
            hChar->ResetFaceOverrideBlending();
            hChar->SetLipsyncOffset(f1);
        }
    }
    int i = 0;
    while (true) {
        HamCharacter *hChar = GetBackup(i++);
        if (!hChar)
            break;
        else {
            hChar->SetLipsyncOffset(f1);
        }
    }
}

void HamDirector::BlendInFaceOverrides(float f1) {
    for (int i = 0; i < 2; i++) {
        HamCharacter *hChar = GetCharacter(i);
        if (hChar) {
            hChar->BlendInFaceOverrides(f1);
        }
    }
}

void HamDirector::BlendOutFaceOverrides(float f1) {
    for (int i = 0; i < 2; i++) {
        HamCharacter *hChar = GetCharacter(i);
        if (hChar) {
            hChar->BlendOutFaceOverrides(f1);
        }
    }
}

bool HamDirector::ShotsDisabled() {
    if (!mDisablePicking) {
        if (GetWorld() && GetWorld()->GetCameraManager()->HasFreeCam()) {
            return true;
        }
        if (!mPlayerFreestyle || mFreestyleEnabled) {
            return false;
        }
    }
    return true;
}

void HamDirector::SyncScene() {
    mSyncScene = false;
    if (!ShotsDisabled() && mVenue) {
        SetNewWorld();
    }
}

void HamDirector::RestoreBackups() {
    if (mBackupHidden) {
        if (mPlayer0Char) {
            mPlayer0Char->SetShowing(mCharsShowing[0]);
        }
        if (mPlayer1Char) {
            mPlayer1Char->SetShowing(mCharsShowing[1]);
        }
        mPlayer1Char->SetShowing(mCharsShowing[1]);
        if (mBackup0Char) {
            mBackup0Char->SetShowing(mCharsShowing[2]);
        }
        if (mBackup1Char) {
            mBackup1Char->SetShowing(mCharsShowing[3]);
        }
        for (int i = 0; i < 4; i++)
            mCharsShowing[i] = false;
        mBackupHidden = false;
    }
}

ObjectDir *HamDirector::GetDifficultyProxy(Difficulty d) {
    Difficulty d_legacy = LegacyDifficulty(d);
    WorldDir *dir = GetWorld();
    if (dir) {
        Symbol sym = DifficultyToSym(d_legacy);
        return dir->Find<ObjectDir>(sym.Str(), false);
    } else
        return nullptr;
}

RndPropAnim *HamDirector::GetPropAnim(Difficulty d, const char *name, bool warn) {
    RndPropAnim *anim = nullptr;
    ObjectDir *proxy = GetDifficultyProxy(d);
    if (proxy) {
        anim = proxy->Find<RndPropAnim>(name, false);
        if (warn && !anim) {
            MILO_NOTIFY("%s has no PropAnim \"%s\"", PathName(proxy), name);
        }
    }
    return anim;
}

void HamDirector::EnableFacialAnimation() {
    for (int i = 0; i < 2; i++) {
        CharLipSync *lipsync = nullptr;
        ObjectDir *proxy = GetDifficultyProxy(TheGameData->Player(i)->GetDifficulty());
        if (proxy) {
            lipsync = proxy->Find<CharLipSync>("dancer_face.lipsync", false);
        }
        HamCharacter *hChar = GetCharacter(i);
        if (hChar) {
            hChar->EnableFacialAnimation(lipsync, 0);
        }
        if (i == 0) {
            int j = 0;
            while (true) {
                HamCharacter *hChar = GetBackup(j++);
                if (!hChar)
                    break;
                else {
                    hChar->EnableFacialAnimation(lipsync, 0);
                }
            }
        }
    }
}

Symbol HamDirector::ClosestMove() {
    char buf[256];
    Symbol out = mPrevMove;
    Difficulty playerDiff = TheGameData->Player(0)->GetDifficulty();
    if (playerDiff != kDifficultyExpert) {
        if (mSongAnims[playerDiff]) {
            SymbolKeys *keys =
                dynamic_cast<SymbolKeys *>(GetPropKeys(playerDiff, "move"));
            if (keys) {
                Key<Symbol> *nearest =
                    keys->KeyNearest(mSongAnims[playerDiff]->GetFrame());
                if (nearest) {
                    strcpy(buf, nearest->value.Str());
                    char *dot = strchr(buf, '.');
                    if (dot)
                        *dot = '\0';
                    DataNode list = PracticeList(kDifficultyExpert);
                    DataArray *listArr = list.Array();
                    int i = 0;
                    int maxScore = -1;
                    if (0 < listArr->Size()) {
                        do {
                            DataNode *node = &listArr->Node(i);
                            const char *candidate = node->Str();
                            int matchCount = 0;
                            if (*candidate != '\0') {
                                const char *p = candidate;
                                do {
                                    if (buf[p - candidate] == '\0')
                                        break;
                                    int pLower = tolower(*p);
                                    if (pLower != tolower(buf[p - candidate]))
                                        break;
                                    p++;
                                    matchCount++;
                                } while (*p != '\0');
                            }

                            const char *q = &buf[matchCount];
                            do {
                            } while (*q++ != '\0');
                            int bufRem = q - &buf[matchCount] - 1;

                            const char *r = &candidate[matchCount];
                            do {
                            } while (*r++ != '\0');
                            int candRem = r - &candidate[matchCount] - 1;

                            int penalty = candRem;
                            if (penalty < bufRem)
                                penalty = bufRem;

                            int score = matchCount - penalty;
                            if (maxScore < score) {
                                maxScore = score;
                                out = candidate;
                            }
                            i++;
                        } while (i < listArr->Size());
                    }
                }
            }
        }
    }
    return out;
}

bool HamDirector::IsWorldLoaded() const {
    bool loaded = mVenue && mMerger && !mMerger->HasPendingFiles() && mMoveMerger
        && !mMoveMerger->HasPendingFiles();
#ifdef HX_NATIVE
    static int sLoadLog = 0;
    if (!loaded && sLoadLog++ < 20) {
        fprintf(stderr, "DC3 HamDirector::IsWorldLoaded() = false — mVenue=%p mMerger=%p(pending=%d) mMoveMerger=%p(pending=%d)\n",
                (void*)mVenue.Ptr(), (void*)mMerger.Ptr(),
                mMerger ? mMerger->HasPendingFiles() : -1,
                (void*)mMoveMerger.Ptr(),
                mMoveMerger ? mMoveMerger->HasPendingFiles() : -1);
    }
#endif
    return loaded;
}

void HamDirector::CheckBeginFatal(int i1, HamMove *move, int i3) {
    static Symbol gameplay_mode("gameplay_mode");
    static Symbol dance_battle("dance_battle");
    if (i3 <= 1) {
        if (move->IsFinalPose() && !mPoseFatalities->InFatality(i1)) {
            if (TheHamProvider->Property(gameplay_mode, true)->Sym() == dance_battle) {
                mPoseFatalities->ActivateFatal(i1);
            }
        }
    }
}

void HamDirector::UpdatePostProcOverlay(
    const char *cc, const RndPostProc *p1, const RndPostProc *p2, float f4
) {
    RndOverlay *ppOverlay = RndOverlay::Find("postproc", true);
    static const RndPostProc *sPostProcA;
    static const RndPostProc *sPostProcB;
    static float sPostProcBlend = -99;
    if (!ppOverlay->Showing())
        return;
    TextStream *reflect = TheDebug.SetReflect(ppOverlay);
    if (p1 == sPostProcA && p2 == sPostProcB && f4 == sPostProcBlend)
        return;
    static int sHamDirID = 0;
    sHamDirID++;
    int displayId = sHamDirID % 100;
    if (p1) {
        if (!p2) {
            MILO_LOG(
                "%03d:HAMDIR Post Proc %s is not blended\n", displayId, p1->Name()
            );
        } else {
            MILO_LOG("%03d:HAMDIR Post Proc A %s\n", displayId, p1->Name());
        }
    }
    if (p2) {
        MILO_LOG("%03d:HAMDIR Post Proc B %s\n", displayId, p2->Name());
    }
    MILO_LOG(
        "           PostProc set by %s, blend is %.2f%%\n", cc ? cc : "", f4 * 100.0f
    );
    sPostProcBlend = f4;
    sPostProcB = p2;
    sPostProcA = p1;
    TheDebug.SetReflect(reflect);
}

bool HamDirector::IsMoveMergerFinished() const {
    return mMoveMerger && !mMoveMerger->HasPendingFiles();
}

void HamDirector::SetNewWorld() {
    MILO_ASSERT(mVenue, 0x7D5);
    if (TheHamWardrobe) {
        TheHamWardrobe->SetDir(mVenue);
    }
    mPickNewShot = true;
    GetWorld()->SetSphere(mVenue->GetSphere());
}

void HamDirector::HideBackups(bool b1, bool b2) {
    mCharsShowing[0] = mPlayer0Char && mPlayer0Char->Showing();
    mCharsShowing[1] = mPlayer1Char && mPlayer1Char->Showing();
    if (b1 ^ b2) {
        if (b1) {
            mPlayer1Char->SetShowing(false);
        } else {
            mPlayer0Char->SetShowing(false);
        }
    }
    mCharsShowing[2] = mBackup0Char && mBackup0Char->Showing();
    if (mBackup0Char) {
        mBackup0Char->SetShowing(false);
    }
    mCharsShowing[3] = mBackup1Char && mBackup1Char->Showing();
    if (mBackup1Char) {
        mBackup1Char->SetShowing(false);
    }
    mBackupHidden = true;
}

void HamDirector::LoadCrew(Symbol s1, Symbol s2) {
    char buffer[128];
    Symbol symbols[2] = { s1, s2 };
    Symbol mind_control("mind_control");
    Symbol gameplaySym = TheHamProvider->Property("gameplay_mode", true)->Sym();
    for (int i = 0; i < 2; i++) {
        HamPlayerData *hpd = TheGameData->Player(i);
        MILO_ASSERT(hpd, 0x98B);
        mCrews[i] = symbols[i];
        strcpy(buffer, hpd->CharacterOutfit(mCrews[i]).Str());
        if (strstr(buffer, "lima") || strstr(buffer, "rasa")) {
            buffer[strlen(buffer)] = '0';
            buffer[strlen(buffer)] = gameplaySym == mind_control ? '6' : '5';
        } else {
            buffer[strlen(buffer)] = '0';
            buffer[strlen(buffer)] = '4';
        }
        mCharacterOutfits[i] = buffer;
    }
    TheHamWardrobe->LoadCharacters(
        mCharacterOutfits[0],
        mCharacterOutfits[1],
        mCrews[0],
        mCrews[1],
        mBackupDancers,
        mSongSpeed,
        TheGameData->Venue().Str(),
        true
    );
}

void HamDirector::OfflineLoadSong(Symbol song) {
    MILO_ASSERT(TheFakeSongMgr, 0x1154);
    MILO_ASSERT(mOfflineSong, 0x1155);
    mOfflineSong->SetSong(song);
}

DataNode HamDirector::OnClipAnnotate(DataArray *a) {
    ClipPlayer player;
    if (player.Init(TheGameData->Player(0)->GetDifficulty())) {
        float frame;
        RndPropAnim *anim = a->Obj<RndPropAnim>(2);
        anim->GetKeys(this, a->Array(3))->FrameFromIndex(a->Int(4), frame);
        return player.AnnotateClip(frame);
    } else {
        return 0;
    }
}

DataNode HamDirector::OnPracticeAnnotate(DataArray *a) {
    ClipPlayer player;
    if (player.Init(TheGameData->Player(0)->GetDifficulty())) {
        return player.AnnotatePractice();
    } else {
        return 0;
    }
}

DataNode HamDirector::OnToggleDebugInterests(DataArray *a) {
    for (int i = 0; i < 2; i++) {
        HamCharacter *hc = GetCharacter(i);
        if (hc) {
            hc->SetDebugDrawInterestObjects(!hc->DebugDrawInterestObjects());
        }
    }
    return 0;
}

DataNode HamDirector::OnToggleCamCharacterSkeleton(DataArray *a) {
    for (int i = 0; i < 2; i++) {
        HamCharacter *hc = GetCharacter(i);
        if (hc) {
            hc->SetUseCameraSkeleton(!hc->UseCameraSkeleton());
        }
    }
    return 0;
}

DataNode HamDirector::OnBlendInFaceClip(DataArray *a) {
    Symbol s1 = a->Sym(2);
    float f1 = a->Float(3);
    float f2 = a->Float(4);
    HamCharacter *hc = GetCharacter(0);
    if (hc) {
        hc->BlendInFaceOverrideClip(s1, f1, f2);
    }
    return 0;
}

void HamDirector::InitOffline() {
    if (!mOfflineSong) {
        HamSong::mPreferStreaming = true;
        mOfflineSong = dynamic_cast<Song *>(Hmx::Object::NewObject("Song"));
        MILO_ASSERT(mOfflineSong, 0x114B);
        DataVariable("tool_song") = mOfflineSong;
        Song::sCallback = &gOfflineCallback;
    }
}

bool HamDirector::GetPracticeFrames(Key<Symbol> *&k1, Key<Symbol> *&k2) {
    if (!mPracticeStart.Null() && !mPracticeEnd.Null()) {
        PropKeys *propKeys =
            GetPropKeys(TheGameData->Player(0)->GetDifficulty(), "practice");
        if (propKeys) {
            Keys<Symbol, Symbol> *keys = propKeys->AsSymbolKeys();
            int numKeys = keys->size();
            int startIdx = 0;
            for (; (unsigned int)startIdx < numKeys; startIdx++) {
                if (mPracticeStart == (*keys)[startIdx].value)
                    goto next;
            }
            startIdx = -1;
        next:
            int endIdx = 0;
            for (; (unsigned int)endIdx < numKeys; endIdx++) {
                if (mPracticeEnd == (*keys)[endIdx].value)
                    goto end;
            }
            endIdx = -1;
        end:
            if (startIdx < endIdx && startIdx != -1 && endIdx != -1) {
                k2 = &(*keys)[endIdx];
                k1 = &(*keys)[startIdx];
                return true;
            }
        }
    }
    return false;
}

void HamDirector::SetPhraseMetersFlipped(bool b1) {
    if (mPhraseMetersFlipped != b1) {
        mPhraseMetersFlipped = b1;
        static Symbol spotlight_constraint("spotlight_constraint");
        WorldDir *venue = TheHamDirector->GetVenueWorld();
        if (venue) {
            RndTransformable *p0 = venue->Find<RndTransformable>("player0", true);
            RndTransformable *p1 = venue->Find<RndTransformable>("player1", true);
            TransConstraint *c0 =
                venue->Find<TransConstraint>("TransConstraint.tc", true);
            TransConstraint *c1 =
                venue->Find<TransConstraint>("TransConstraint1.tc", true);
            if (b1) {
                p0->SetProperty(spotlight_constraint, c1);
                p1->SetProperty(spotlight_constraint, c0);
                c0->SetParent(p1);
                c0->SnapToParent();
                c1->SetParent(p0);
                c1->SnapToParent();
            } else {
                p0->SetProperty(spotlight_constraint, c0);
                p1->SetProperty(spotlight_constraint, c1);
                c0->SetParent(p0);
                c0->SnapToParent();
                c1->SetParent(p1);
                c1->SnapToParent();
            }
        }
    }
}

void HamDirector::SetPlayerSpotlightsEnabled(bool b1) {
    WorldDir *venue = TheHamDirector->GetVenueWorld();
    if (venue) {
        RndTransformable *players[2] = { venue->Find<RndTransformable>("player0", true),
                                         venue->Find<RndTransformable>("player1", true) };
        TransConstraint *constraints[2] = {
            venue->Find<TransConstraint>("TransConstraint.tc", true),
            venue->Find<TransConstraint>("TransConstraint1.tc", true)
        };
        HamPhraseMeter *phraseMeters[2] = {
            venue->Find<HamPhraseMeter>("phrase_meter0", true),
            venue->Find<HamPhraseMeter>("phrase_meter1", true)
        };
        RndDrawable *moveFeedbacks[2] = {
            venue->Find<RndDrawable>("move_feedback0", true),
            venue->Find<RndDrawable>("move_feedback1", true)
        };
        for (int i = 0; i < 2; i++) {
            if (b1) {
                constraints[i]->SetParent(players[i]);
                constraints[i]->SnapToParent();
                constraints[i]->mEnabled = true;
                constraints[i]->SnapToParent();
                phraseMeters[i]->SetShowing(true);
                moveFeedbacks[i]->SetShowing(true);
            } else {
                constraints[i]->SetParent(nullptr);
                constraints[i]->SnapToParent();
                constraints[i]->mEnabled = false;
                Vector3 v(-1000000.0f, -1000000.0f, 0);
                phraseMeters[i]->SetLocalPos(v);
                phraseMeters[i]->SetShowing(false);
                moveFeedbacks[i]->SetShowing(false);
            }
        }
    }
}

bool HamDirector::InPracticeMode() {
    Key<Symbol> *start;
    Key<Symbol> *end;
    return GetPracticeFrames(start, end);
}

void HamDirector::PoseIconMan(
    CharClip *clip1, float f2, RndTex *tex, bool b4, CharClip *clip2, float f5, float f6
) {
    if (clip1) {
        CharBonesMeshes meshes;
        meshes.SetName("preview_anim", mIconManChar);
        clip1->StuffBones(meshes);
        meshes.Zero();
        float fOne = 1.0f;
        float fZero = 0.0f;
        if (clip2) {
            clip1->ScaleAdd(meshes, fOne - f6, f2, fZero);
            clip2->ScaleAdd(meshes, f6, f5, fZero);
        } else {
            clip1->ScaleAdd(meshes, fOne, f2, fZero);
        }
        meshes.PoseMeshes();
        if (b4) {
            RndTransformable *pelvis = CharUtlFindBoneTrans("bone_pelvis", meshes.Dir());
            void *rotzPtr = meshes.FindPtr("bone_facing.rotz");
            void *posPtr = meshes.FindPtr("bone_facing.pos");
            if (pelvis && posPtr && rotzPtr) {
                float *rot = (float *)rotzPtr;
                Vector3 *pos = (Vector3 *)posPtr;
                Transform &pelvisXfm = pelvis->DirtyLocalXfm();
                RotateAboutZ(pelvisXfm.m, *rot, pelvisXfm.m);
                RotateAboutZ(pelvisXfm.v, *rot, pelvisXfm.v);
                Normalize(pelvisXfm.m, pelvisXfm.m);
                pelvisXfm.v += *pos;
            }
            CharBoneDir *resourceDir = clip1->GetResource();
            for (ObjDirItr<CharBone> it(resourceDir, false); it != nullptr; ++it) {
                if (it->BakeOutAsTopLevel()) {
                    String name(it->Name());
                    if (name.find(".cb") != FixedString::npos) {
                        name = name.substr(0, name.length() - 3);
                    }
                    RndTransformable *curTrans =
                        CharUtlFindBoneTrans(name.c_str(), meshes.Dir());
                    if (curTrans && posPtr && rotzPtr) {
                        float *rot = (float *)rotzPtr;
                        Vector3 *pos = (Vector3 *)posPtr;
                        Transform &curXfm = curTrans->DirtyLocalXfm();
                        RotateAboutZ(curXfm.m, *rot, curXfm.m);
                        RotateAboutZ(curXfm.v, *rot, curXfm.v);
                        Normalize(curXfm.m, curXfm.m);
                        curXfm.v += *pos;
                    }
                }
            }
        }
        for (ObjDirItr<CharPollable> it(mIconManChar, true); it != nullptr; ++it) {
            if (dynamic_cast<CharForeTwist *>(&*it)
                || dynamic_cast<CharUpperTwist *>(&*it)
                || dynamic_cast<CharNeckTwist *>(&*it)) {
                it->Poll();
            }
        }
    }
    mIconManTex->SetShowing(true);
    mIconManTex->SetOutputTexture(tex);
    mIconManTex->DrawShowing();
    mIconManTex->SetShowing(false);
}

void HamDirector::PoseIconMan(const BaseSkeleton *skeleton, RndTex *tex) {
    HamCharacter *iconMan = dynamic_cast<HamCharacter *>(mIconManChar.Ptr());
    MILO_ASSERT(iconMan, 0x52D);
    for (ObjDirItr<HamRibbon> it(iconMan, true); it != nullptr; ++it) {
        it->Reset();
    }
    iconMan->SetUseCameraSkeleton(true);
    HamSkeletonConverter *skelCvt =
        iconMan->Find<HamSkeletonConverter>("HamSkeletonConverter.cvt", true);
    MILO_ASSERT(skelCvt, 0x535);
    skelCvt->Set(skeleton);
    iconMan->SetPollWhenHidden(true);
    iconMan->Poll();
    iconMan->SetPollWhenHidden(false);
    mIconManTex->SetShowing(true);
    mIconManTex->SetOutputTexture(tex);
    mIconManTex->DrawShowing();
    mIconManTex->SetShowing(false);
    iconMan->SetUseCameraSkeleton(false);
}

DataNode HamDirector::OnGetDancerVisemes(DataArray *a) {
    std::list<Symbol> visemes;
    HamCharacter *hc = GetCharacter(0);
    if (hc) {
        ObjectDir *visemeDir = hc->Find<ObjectDir>("viseme", false);
        if (visemeDir) {
            for (ObjDirItr<CharClip> it(visemeDir, false); it != nullptr; ++it) {
                visemes.push_back(it->Name());
            }
        }
    }
    DataArray *visemeArr = new DataArray(visemes.size() + 1);
    visemeArr->Node(0) = Symbol();
    int idx = 1;
    FOREACH (it, visemes) {
        visemeArr->Node(idx) = *it;
        ++idx;
    }
    DataNode ret(visemeArr);
    visemeArr->Release();
    return ret;
}

void HamDirector::CleanOriginalMoveData() {
    for (ObjDirItr<CharClip> it(mClipDir, true); it != nullptr; ++it) {
        MILO_LOG("[HamDirector::CleanOriginalMoveData] Removing %s ...\n", it->Name());
        delete it;
    }
    for (ObjDirItr<HamMove> it(mMoveDir, true); it != nullptr; ++it) {
        RndTex *smallTex = it->SmallTex();
        RndTex *regTex = it->Tex();
        if (smallTex && smallTex != regTex) {
            MILO_LOG(
                "[HamDirector::CleanOriginalMoveData] Removing %s ...\n", smallTex->Name()
            );
            delete smallTex;
        }
        if (regTex) {
            MILO_LOG(
                "[HamDirector::CleanOriginalMoveData] Removing %s ...\n", regTex->Name()
            );
            delete regTex;
        }
        MILO_LOG("[HamDirector::CleanOriginalMoveData] Removing %s ...\n", it->Name());
        delete it;
    }
}

void HamDirector::LoadRoutineBuilderData(
    std::set<const MoveVariant *> &moveVariants, bool b2
) {
    if (moveVariants.empty()) {
        FOREACH (it, mRoutineBuilderObjects) {
            delete *it;
        }
        mRoutineBuilderObjects.clear();
    } else {
        ObjectDir *moveMgrDir = TheMoveMgr->MoveDataDir();
        if (!moveMgrDir) {
            MILO_LOG("Move data missing from %s", TheGameData->GetSong());
        } else {
            ObjectDir *movesDir = GetWorld()->Find<ObjectDir>("moves", true);
            int movesDirHash = movesDir->HashTableSize() + moveMgrDir->HashTableSize();
            int movesDirStr = movesDir->StrTableSize() + moveMgrDir->StrTableSize();
            movesDir->Reserve(movesDirHash, movesDirStr);
            std::vector<Hmx::Object *> objects;
            objects.reserve(0x80);
            for (ObjDirItr<HamMove> it(moveMgrDir, false); it != nullptr; ++it) {
                objects.push_back(it);
            }
            FOREACH (it, objects) {
                Hmx::Object *cur = *it;
                const char *name = cur->Name();
                Hmx::Object *find = movesDir->FindObject(name, false, false);
                if (!find) {
                    cur->SetName(name, movesDir);
                    mRoutineBuilderObjects.insert(cur);
                }
            }
            objects.clear();
            ObjectDir *clipsDir = GetWorld()->Find<ObjectDir>("clips", true);
            int clipsDirHash = clipsDir->HashTableSize() + moveMgrDir->HashTableSize();
            int clipsDirStr = clipsDir->StrTableSize() + moveMgrDir->StrTableSize();
            clipsDir->Reserve(clipsDirHash, clipsDirStr);
            for (ObjDirItr<CharClip> it(clipsDir, false); it != nullptr; ++it) {
                objects.push_back(it);
            }
            FOREACH (it, objects) {
                Hmx::Object *cur = *it;
                const char *name = cur->Name();
                Hmx::Object *find = clipsDir->FindObject(name, false, false);
                if (!find) {
                    cur->SetName(name, clipsDir);
                    mRoutineBuilderObjects.insert(cur);
                }
            }
        }
    }
}

void HamDirector::OnPopulateMoveMgr() {
    TheMoveMgr->LoadSongData();
    TheMoveMgr->InitSong();
    TheMoveMgr->AutoFillParents();
    TheMoveMgr->FillRoutineFromParents(-1);
    TheMoveMgr->ComputeLoadedMoveSet();
    LoadRoutineBuilderData((std::set<const MoveVariant *> &)TheMoveMgr->GetVariants(), true);
    OnPopulateFromMoveMgr();
    DataArrayPtr variants;
    TheMoveMgr->SaveRoutineVariants(variants);
    DataWriteFile("routine_test_variants.dta", variants, 0);
    DataArrayPtr parents;
    TheMoveMgr->SaveRoutine(parents);
    DataWriteFile("routine_test_parents.dta", parents, 0);
}

void HamDirector::OnPopulateFromFile() {
    TheMoveMgr->LoadSongData();
    TheMoveMgr->InitSong();
    DataArray *variants = DataReadFile("routine_test_variants.dta", true);
    if (variants) {
        TheMoveMgr->LoadRoutineVariants(variants);
        variants->Release();
    }
    TheMoveMgr->ComputeLoadedMoveSet();
    LoadRoutineBuilderData((std::set<const MoveVariant *> &)TheMoveMgr->GetVariants(), true);
    OnPopulateFromMoveMgr();
}

void HamDirector::MoveKeys(
    Difficulty diff, MoveDir *moveDir, std::vector<HamMoveKey> &moveKeys
) {
    moveKeys.clear();
    PropKeys *propKeys = GetPropKeys(diff, "move");
    if (propKeys) {
        Keys<Symbol, Symbol> *symKeys = propKeys->AsSymbolKeys();
        for (int i = 0; i < symKeys->size(); i++) {
            Key<Symbol> curSymKey = (*symKeys)[i];
            HamMove *move = moveDir->Find<HamMove>(curSymKey.value.Str(), false);
            if (move) {
                HamMoveKey key;
                key.move = move;
                key.beat = FrameToBeat(curSymKey.frame);
                moveKeys.push_back(key);
            }
        }
    }
}

DataNode HamDirector::OnClipSafeToAdd(DataArray *a) {
    if (a->Int(2)) {
        for (int i = 0; i < kNumDifficultiesDC2; i++) {
            if (mSongAnims[(Difficulty)i]) {
                PropKeys *propKeys = mSongAnims[(Difficulty)i]->GetKeys(
                    this, DataArrayPtr(Symbol("clip"))
                );
                if (propKeys) {
                    Keys<Symbol, Symbol> *symKeys = propKeys->AsSymbolKeys();
                    for (int j = 0; j < symKeys->size(); j++) {
                        Key<Symbol> &curKey = (*symKeys)[j];
                        curKey.frame =
                            BeatToFrame(floor(FrameToBeat(curKey.frame) + 0.5f));
                    }
                }
            }
        }
    }
    return 0;
}

DataNode HamDirector::OnPracticeSafeToAdd(DataArray *a) {
    if (a->Int(2)) {
        for (int i = 0; i < kNumDifficultiesDC2; i++) {
            if (mSongAnims[(Difficulty)i]) {
                PropKeys *propKeys = mSongAnims[(Difficulty)i]->GetKeys(
                    this, DataArrayPtr(Symbol("practice"))
                );
                if (propKeys) {
                    Keys<Symbol, Symbol> *symKeys = propKeys->AsSymbolKeys();
                    for (int j = 0; j < symKeys->size(); j++) {
                        Key<Symbol> &curKey = (*symKeys)[j];
                        curKey.frame =
                            BeatToFrame(floor(FrameToBeat(curKey.frame) + 0.5f));
                    }
                }
            }
        }
    }
    return 0;
}

void HamDirector::HandleDifficultyChange() {
    if (TheLoadMgr.EditMode()) {
        if (TheHamProvider->Property("merge_moves", true)->Int()) {
            MoveDir *dir = TheHamDirector->GetWorld()->Find<MoveDir>("moves", false);
            if (dir) {
                SetupRoutineBuilderAnims();
                TheMoveMgr->ResetRemixer();
            }
        }
    }
}

void HamDirector::FindNextShot() {
    mNextShot = nullptr;
    Key<Symbol> *start;
    Key<Symbol> *end;
    if (GetPracticeFrames(start, end)) {
        static Symbol skills_mode("skills_mode");
        static Symbol review("review");
        bool inReview = TheHamProvider->Property(skills_mode, true)->Sym() == review;
        static Symbol PRACTICE("PRACTICE");
        static Symbol PRACTICE_RECAP("PRACTICE_RECAP");
        static Symbol SKILLS_RESULTS("SKILLS_RESULTS");
        if (mShot != SKILLS_RESULTS) {
            if (inReview) {
                mShot = PRACTICE_RECAP;
            } else {
                mShot = PRACTICE;
            }
        }
    }
    if (!mShot.Null() && mVenue) {
        std::vector<CameraManager::PropertyFilter> propFilters;
        AddNumPlayers(propFilters, nullptr);
        String shotStr(mShot.Str());
        shotStr.ToLower();
        if (shotStr.compare(0, 5, "area2") == 0 || shotStr.compare(0, 5, "area3") == 0) {
            shotStr = mShot;
            shotStr[4] = '1';
            mShot = shotStr.c_str();
        }
        mNextShot = dynamic_cast<HamCamShot *>(
            mVenue->GetCameraManager()->FindCameraShot(mShot, propFilters)
        );
#ifdef HX_NATIVE
        // Fallback: if the shot category wasn't found, try Area1_WIDE
        if (!mNextShot) {
            static Symbol Area1_WIDE("Area1_WIDE");
            mNextShot = dynamic_cast<HamCamShot *>(
                mVenue->GetCameraManager()->FindCameraShot(Area1_WIDE, propFilters)
            );
        }
#endif
        if (!mNextShot) {
            MILO_NOTIFY(
                "could not find HamCamShot %s in %s at %s, ignoring",
                mShot,
                mVenue->GetPathName(),
                TheTaskMgr.GetMBT()
            );
        }
    }
}

void HamDirector::SetShot(Symbol s) {
    if (TheTaskMgr.Seconds(TaskMgr::kRealTime) >= 0
        && !(SongAnim(0) && SongAnim(0)->GetFrame() < 0)) {
        static Symbol review("review");
        static Symbol skills_mode("skills_mode");
        bool inReview = TheHamProvider->Property(skills_mode, true)->Sym() == review;
        Key<Symbol> *start;
        Key<Symbol> *end;
        if (s == "SKILLS_RESULTS" || !GetPracticeFrames(start, end)
            || (inReview && s != "DC_PLAYER_FREESTYLE")) {
            static Symbol mind_control("mind_control");
            static Symbol gameplay_mode("gameplay_mode");
            static Symbol game_stage("game_stage");
            static Symbol playing("playing");
            static Symbol CAMP_MINDCONTROL_DANCE("CAMP_MINDCONTROL_DANCE");
            Symbol gameplayModeSym = TheHamProvider->Property(gameplay_mode, true)->Sym();
            Symbol stageSym = TheHamProvider->Property(game_stage, true)->Sym();
            if (gameplayModeSym == mind_control && stageSym == playing) {
                s = CAMP_MINDCONTROL_DANCE;
            }
            if (strncmp(s.Str(), "dc_", 3) != 0) {
                mShot = s;
                mPickNewShot = true;
            }
        }
    }
}

float HamDirector::BeatFromTag(Symbol s) {
    static Symbol practice("practice");
    RndPropAnim *songAnim = SongAnim(0);
    PropKeys *propKeys = songAnim->GetKeys(this, DataArrayPtr(practice));
    if (propKeys) {
        for (int i = 0; i < propKeys->NumKeys(); i++) {
            float frame = 0;
            propKeys->FrameFromIndex(i, frame);
            if (s == (*propKeys->AsSymbolKeys())[i].value) {
                return FrameToBeat(frame);
            }
        }
    }
    return -1;
}

void HamDirector::Reteleport() {
    Symbol s;
    float beat = MsToBeat(TheMaster->StreamMs());
    static Symbol practice("practice");
    RndPropAnim *anim =
        GetPropAnim(TheGameData->Player(0)->GetDifficulty(), "song.anim", false);
    PropKeys *propKeys = anim->GetKeys(this, DataArrayPtr(practice));
    int frameIdx = 0;
    CharClip *clip;
    float endBeat;
    float frameTime;
    if (propKeys) {
        frameTime = BeatToSeconds(beat) * 30.0f;
        frameIdx = propKeys->AsSymbolKeys()->AtFrame(frameTime, s);
        auto _tmp7 = GetClipStartAndEndBeats(s, endBeat, beat, 0);
        clip = _tmp7;
    }
    Vector3 v = Vector3::ZeroVec();
    if (clip) {
        if (frameIdx > 0) {
            ClipPredict predict(clip, Vector3::ZeroVec(), 0);
            predict.PredictDeltaPos(beat - 4.0f, beat);
            v = predict.mPos;
        }
    }
    if (mCurShot) {
        mCurShot->Reteleport(v, false, Symbol(gNullStr));
    }
}

bool HamDirector::ReactToCollision(float frame) {
    if (TheLoadMgr.EditMode()) {
        return false;
    }
    float beat = FrameToBeat(frame);
    if (!mCurShot)
        return false;
    Symbol cat = mCurShot->Category();
    if (strncmp(cat.Str(), "Area", 4) != 0) {
        return false;
    }
    Symbol symAt;
    static Symbol shot("shot");
    PropKeys *propKeys = GetPropKeysByPlayer(0, shot);
    if (!propKeys)
        return false;
    int keyIdx = propKeys->SymbolAt(frame, symAt);
    if (keyIdx >= 0 && strncmp(symAt.Str(), "Area", 4) == 0) {
        cat = symAt;
    }
    float frame2;
    Symbol symAt2;
    bool idxExists = propKeys->FrameFromIndex(keyIdx, frame2);
    if (!idxExists)
        return false;
    int keyIdx2 = propKeys->SymbolAt(frame, symAt2);
    if (keyIdx2 == -1 || keyIdx2 == propKeys->NumKeys() - 1
        || strncmp(symAt2.Str(), "Area", 4) != 0) {
        mShot = cat;
        mPickNewShot = true;
        return true;
    }
    float beat2 = FrameToBeat(frame2);
    static float sSongCollisionUseShotWithinXBeats =
        DataGetMacro("SONG_COLLISION_USE_SHOT_WITHIN_X_BEATS")->Float(0);
    if (beat2 < sSongCollisionUseShotWithinXBeats + beat) {
        ReactToCollision_MoveShot(keyIdx2, beat);
        return true;
    } else {
        static float sSongCollisionForXBeatsSuppressNextShot =
            DataGetMacro("SONG_COLLISION_FOR_X_BEATS_SUPPRESS_NEXT_SHOT")->Float(0);
        float beatSum = sSongCollisionForXBeatsSuppressNextShot + beat;
        if (beat2 < beatSum) {
            ReactToCollision_InsertRealShot(cat, beat);
        } else {
            static bool sSongCollisionRoundUpSuppressedShotToMeasure =
                DataGetMacro("SONG_COLLISION_ROUND_UP_SUPPRESSED_SHOT_TO_MEASURE")->Int(0);
            if (sSongCollisionRoundUpSuppressedShotToMeasure) {
                beatSum = ceil(beatSum / 4.0f);
            }
            float frame3;
            if (!propKeys->FrameFromIndex(keyIdx2 + 1, frame3)) {
                return false;
            }
            float beat3 = FrameToBeat(frame3);
            static float sSongCollisionAbortSuppressedShotIfAnotherWithinXBeats =
                DataGetMacro(
                    "SONG_COLLISION_ABORT_SUPPRESSED_SHOT_IF_ANOTHER_WITHIN_X_BEATS"
                )
                    ->Float(0);
            if (beatSum
                < beat3 - sSongCollisionAbortSuppressedShotIfAnotherWithinXBeats) {
                ReactToCollision_MoveShot(keyIdx2, beat3);
            } else {
                ReactToCollision_MoveShot(keyIdx2, beat2);
            }
        }
    }
    return true;
}

void HamDirector::UnloadMergers() {
    if (mMerger) {
        mMerger->Clear();
        mMoveMerger->Clear();
        ObjVector<FileMerger::Merger>& mergers = mMoveMerger->Mergers();
        mergers.erase(mergers.begin(), mergers.end());
        HamWardrobe *wardrobe = TheHamWardrobe;
        if (wardrobe) {
            for (int i = 0; i < 2; i++) {
                HamCharacter *hc = wardrobe->GetCharacter(i);
                if (hc) {
                    hc->UnloadAll();
                }
            }
            int i = 0;
            while (true) {
                HamWardrobe *curWardrobe = TheHamWardrobe;
                HamCharacter *hc;
                if (curWardrobe) {
                    hc = curWardrobe->GetBackup(i);
                } else {
                    hc = 0;
                }
                i++;
                if (!hc)
                    break;
                hc->UnloadAll();
            }
            wardrobe->ClearCrowdClips();
        }
        mRoutineBuilderObjects.clear();
    }
}

void HamDirector::UnloadAll() {
    AutoGlitchReport report(50, "HamDirector::UnloadAll");
    TheMoveMgr->Clear();
    UnloadMergers();
}

void HamDirector::PlayNextShot() {
    if (mSuppressNextShot > 0) {
        mPickNewShot = false;
        if (!mNextShot) {
            return;
        }
        mSuppressNextShot--;
        return;
    }
    HamCamShot *nextShot = mNextShot;
    mPickNewShot = false;
    if ((int)nextShot == 0) {
        return;
    }
    mNextShot = nullptr;
    float lastShotTime;
    if (nextShot && strstr(nextShot->Category().Str(), "dc")) {
        float totalFrames = (float)nextShot->mMinTime + nextShot->mZeroTime;
        nextShot->ConvertFrames(totalFrames);
        lastShotTime = TheTaskMgr.Seconds(TaskMgr::kRealTime) + totalFrames;
    } else {
        if (!mCurShot || strncmp(mCurShot->Category().Str(), "dc_", 3) != 0) {
            lastShotTime = -kHugeFloat;
        } else {
            lastShotTime = TheTaskMgr.Seconds(TaskMgr::kRealTime) + 1.0f;
        }
    }
    mLastShotTime = lastShotTime;
    mCurShot = nextShot;
    // Apply shot to the world root's CameraManager — WorldDir::DrawShowing
    // reads mCameraMgr from the world root when rendering through the panel system.
    WorldDir *world = dynamic_cast<WorldDir *>(mMerger ? mMerger->Dir() : nullptr);
    if (world) {
        world->GetCameraManager()->ForceCameraShot(mCurShot, false);
    }
}

DataNode HamDirector::OnSelectCamera(DataArray *a) {
#ifdef HX_NATIVE
    static int sSelectCamLog = 0;
    if (sSelectCamLog++ < 10 || (sSelectCamLog % 1000 == 0)) {
        fprintf(stderr, "DC3 HamDirector::OnSelectCamera — beat=%.2f disabled=%d songAnim=%p mCurShot=%p mPickNewShot=%d\n",
                TheTaskMgr.Beat(), mDisabled, (void*)SongAnim(0), (void*)mCurShot.Ptr(), mPickNewShot);
    }
    // One-time diagnostic: check if song.anim has 'shot' PropKeys targeting us
    static bool sShotDiagDone = false;
    if (!sShotDiagDone && SongAnim(0) && TheTaskMgr.Beat() > 0.0f) {
        sShotDiagDone = true;
        RndPropAnim *sa = SongAnim(0);
        // Check known property tracks
        const char *propNames[] = {"shot", "clip", "move", "practice", "world_event", "postproc"};
        fprintf(stderr, "DC3 SHOT DIAG: SongAnim(0)=%p this=%p Name='%s'\n",
                (void*)sa, (void*)this, Name());
        for (int pi = 0; pi < 6; pi++) {
            PropKeys *pk = sa->GetKeys(this, DataArrayPtr(Symbol(propNames[pi])));
            if (pk) {
                Hmx::Object *tgt = pk->Target();
                fprintf(stderr, "  '%s': target=%p class='%s' name='%s' numKeys=%d (this_offset=%ld)\n",
                        propNames[pi], (void*)tgt,
                        tgt ? tgt->ClassName() : "null",
                        tgt ? tgt->Name() : "null",
                        pk->NumKeys(),
                        tgt ? ((char*)tgt - (char*)this) : 0);
            } else {
                fprintf(stderr, "  '%s': NOT FOUND (GetKeys returned null)\n", propNames[pi]);
            }
        }
        // Also check difficulty song.anim directly (not routineBuilderAnim)
        for (int d = 0; d < 3; d++) {
            RndPropAnim *diffAnim = GetPropAnim((Difficulty)d, "song.anim", false);
            if (diffAnim) {
                PropKeys *shotPk2 = diffAnim->GetKeys(this, DataArrayPtr(Symbol("shot")));
                fprintf(stderr, "  difficulty[%d] song.anim=%p shot_keys=%p\n",
                        d, (void*)diffAnim, (void*)shotPk2);
            }
        }
    }
#endif
    // Full songAnim path — Debug::Fail is non-fatal on native, so DTA
    // handler FAILs (missing panels, stubs) are harmless warnings.
    RndPropAnim *songAnim = SongAnim(0);
    if (!mDisabled) {
        float beat = TheTaskMgr.Beat();
        float seconds = BeatToSeconds(beat);
        float val = seconds * 30.0f;
        float frame = val < 0.0f ? 0.0f : val;

        float realTime = TheTaskMgr.Seconds(TaskMgr::kRealTime);
        if (realTime >= 0.0f || TheLoadMgr.EditMode()) {
            float blend = 1.0f;
            if (songAnim && (!TheLoadMgr.EditMode() || frame != songAnim->GetFrame())) {
                static Timer *song_anim_timer = AutoTimer::GetTimer(Symbol("song_anim"));
                AutoTimer timer(song_anim_timer, 50.0f, NULL, NULL);
                songAnim->SetFrame(frame, blend);
            }

            for (Difficulty d = (Difficulty)0; (int)d < kNumDifficultiesDC2; d = (Difficulty)((int)d + 1)) {
                if (mDancerFaceAnims[d].Ptr() &&
                    (!TheLoadMgr.EditMode() || frame != mDancerFaceAnims[d]->GetFrame())) {
                    mDancerFaceAnims[d]->SetFrame(frame, blend);
                }
            }
        }

        if (mSyncScene) {
            SyncScene();
        }

        if (TheLoadMgr.EditMode() && TheTaskMgr.DeltaSeconds() < 0.0f) {
            mLastShotTime = -kHugeFloat;
            mLastCollisionTime = -kHugeFloat;
        }

        if (!mNextShot.Ptr() &&
            TheTaskMgr.Seconds(TaskMgr::kRealTime) >= mLastShotTime &&
            !ShotsDisabled()) {
            HamCamShot *dircut = FindNextDircut();
            mNextShot = dircut;

            if (!mNextShot.Ptr() && !mPickNewShot &&
                ShouldDoCollisionPrevention() &&
                AreCharactersColliding() &&
                TheTaskMgr.Seconds(TaskMgr::kRealTime) >= mLastCollisionTime) {
                if (ReactToCollision(frame)) {
                    static Symbol collisionMacro("SONG_COLLISION_DONT_CUT_AGAIN_FOR_X_BEATS");
                    DataArray *macro = DataGetMacro(collisionMacro);
                    float collisionDelay = macro->Node(0).Float(macro);
                    float currentBeat = TheTaskMgr.Beat();
                    float futureMs = BeatToMs(currentBeat + collisionDelay);
                    float currentMs = BeatToMs(currentBeat);
                    float delaySec = (futureMs - currentMs) * 0.001f;
                    mLastCollisionTime = TheTaskMgr.Seconds(TaskMgr::kRealTime) + delaySec;
                }
            }

            if (!mNextShot.Ptr() && mPickNewShot) {
                FindNextShot();
                if (mNextShot.Ptr() && mSongCollision.Ptr() &&
                    ShouldDoCollisionPrevention()) {
                    ChangeNextShotIfCharacterCollisionLikely();
                }
            }
        }
    }
    PlayNextShot();
    return 0;
}

void HamDirector::ChangeNextShotIfCharacterCollisionLikely() {
    static Symbol shot("shot");
    PropKeys *shotKeys = GetPropKeysByPlayer(0, shot);
    if (!shotKeys)
        return;

    auto& nextShot = mNextShot;
    const char *cat = nextShot->Category().Str();
    if (strncmp(cat, "Area", 4) != 0)
        return;

    float beat = TheTaskMgr.Beat();
    float frame = BeatToSeconds(beat) * 30.0f;

    Symbol unused;
    int keyIdx = shotKeys->SymbolAt(frame, unused);

    int currentBeatInt = (int)TheTaskMgr.Beat();
    keyIdx++;
    int numKeys = shotKeys->NumKeys();

    float nextFrame;
    if (keyIdx >= numKeys) {
        RndPropAnim *anim = SongAnim(0);
        nextFrame = anim->EndFrame();
    } else {
        shotKeys->FrameFromIndex(keyIdx, nextFrame);
    }

    int nextBeatPlusOne = (int)SecondsToBeat(nextFrame / 30.0f) + 1;

    Difficulty diffs[2];
    Transform transforms[2];

    int playerIdx = 0;
    int otherIdx = 1;
    do {
        bool swapped = TheGameData->SidesSwapped();
        int targetIdx = swapped ? otherIdx : playerIdx;

        HamPlayerData *player = TheGameData->Player(playerIdx);
        diffs[targetIdx] = player->GetDifficulty();

        static Symbol player0("player0");
        static Symbol player1("player1");
        Symbol targetSym = (targetIdx == 0) ? player0 : player1;

        if (!nextShot->TargetTeleportTransform(targetSym, transforms[targetIdx])) {
            return;
        }

        otherIdx--;
        playerIdx++;
    } while (-1 < otherIdx);

    if (mSongCollision->IsCollision(
            currentBeatInt, nextBeatPlusOne, diffs, transforms, NULL
        )) {
        static Symbol area1Wide("Area1_WIDE");
        static Symbol area2Wide("Area2_WIDE");

        if (strncmp(cat, "Area1", 5) == 0) {
            mShot = area1Wide;
        } else {
            mShot = area2Wide;
        }
        FindNextShot();
    }
}

void HamDirector::OnPopulateMoves() {
    if (!mMasterClipAnim.Ptr()) {
        MILO_NOTIFY("No MasterClipAnim in HamDirector.  Did you load a song?");
        return;
    }

    static bool sPopulating = false;
    if (sPopulating) {
        MILO_NOTIFY(
            "[HamDirector::OnPopulateMoves] Please wait until the function is finished."
        );
        return;
    }

    sPopulating = true;

    DataArray *pMoveData = DataReadFile("../meta/move_data.dta", true);
    MILO_ASSERT(pMoveData, 0xb1c);

    ObjectDir *movesDir = mMerger->Dir()->Find<ObjectDir>("moves", true);
    ObjectDir *clipsDir = mMerger->Dir()->Find<ObjectDir>("clips", true);

    static Symbol clip("clip");
    static Symbol move("move");
    static Symbol move_instance("move_instance");
    static Symbol charclips("charclips");
    static Symbol hammoves("hammoves");
    static Symbol transition_charclips("transition_charclips");

    mMasterClipAnim->RemoveKeys(this, DataArrayPtr(move));
    mMasterClipAnim->RemoveKeys(this, DataArrayPtr(clip));

    PropKeys *moveKeys =
        mMasterClipAnim->AddKeys(this, DataArrayPtr(move), PropKeys::kSymbol);
    PropKeys *clipKeys =
        mMasterClipAnim->AddKeys(this, DataArrayPtr(clip), PropKeys::kSymbol);
    PropKeys *moveInstanceKeys =
        mMasterClipAnim->GetKeys(this, DataArrayPtr(move_instance));

    Keys<Symbol, Symbol> *moveInstSymKeys = moveInstanceKeys->AsSymbolKeys();
    Keys<Symbol, Symbol> *moveSymKeys = moveKeys->AsSymbolKeys();
    Keys<Symbol, Symbol> *clipSymKeys = clipKeys->AsSymbolKeys();

    gMoveMergeMap.clear();

    ObjVector<FileMerger::Merger> *mergers =
        (ObjVector<FileMerger::Merger> *)((char *)mMoveMerger.Ptr() + 0x40);
    if (mergers->begin() != mergers->end()) {
        mergers->erase(mergers->begin(), mergers->end());
    }

    int numKeys = moveInstSymKeys->size();
    for (int i = 0; i != numKeys; i++) {
            if ((*moveInstSymKeys)[i].value == "") continue;

            float keyFrame = (*moveInstSymKeys)[i].frame;
            float beat = SecondsToBeat(keyFrame / 30.0f);
            float roundedBeat = (float)floor(beat + 0.5f);
            if (i != 0) {
                roundedBeat -= 1.0f;
            }
            float frame = BeatToSeconds(roundedBeat) * 30.0f;

            int moveIdx = moveKeys->SetKey(keyFrame);
            int clipIdx = clipKeys->SetKey(frame);

            // Find matching move variant in move_data.dta
            DataArray *pVariant = NULL;
            for (int j = 0; j < pMoveData->Size(); j++) {
                DataArray *arr = pMoveData->Node(j).Array(pMoveData);
                arr = arr->FindArray("name", true);
                const char *entryName = arr->Str(1);
                arr = pMoveData;
                if (strcmp(entryName, (*moveInstSymKeys)[i].value.Str()) == 0) {
                    arr = pMoveData->Node(j).Array(arr);
                    DataArray *varArr = arr->FindArray("variant", true);
                    pVariant = varArr->Node(1).Array(varArr);
                    if (pVariant) break;
                    break;
                }
            }
            MILO_ASSERT(pVariant, 0xb5e);

            pMoveData->Release();

            // Get move and clip names from variant
            Symbol hamMoveName = pVariant->FindArray("ham_move_name", true)->Sym(1);
            Symbol clipName = pVariant->Sym(0);

            (*moveSymKeys)[moveIdx].value = hamMoveName;
            (*clipSymKeys)[clipIdx].value = clipName;

            // Handle transition charclips
            Symbol transName;
            if (i > 0) {
                Symbol prevClipName = (*clipSymKeys)[clipIdx - 1].value;
                DataArray *prevCandidates =
                    pVariant->FindArray("prev_candidates", true);
                DataArray *prevEntry = prevCandidates->FindArray(prevClipName, false);
                if (prevEntry) {
                    if (prevEntry->Int(2) != 0) {
                        transName = Symbol(
                            MakeString("%s_%s", prevClipName.Str(), clipName.Str())
                        );
                    }
                }

                if (transName.Str() != gNullStr && gMoveMergeMap[transName] == 0) {
                    FilePath fp(MakeString(
                        "modular_song_data/transition_charclips/%s.milo",
                        transName
                    ));
                    FileMerger::Merger merger(mMoveMerger.Ptr());
                    merger.mName = transition_charclips;
                    merger.mDir = clipsDir;
                    merger.mSubdirs = MergeFilter::kAllSubdirs;
                    merger.mPreClear = true;
                    merger.mSelected = fp;
                    merger.mForceReload = true;
                    mergers->push_back(merger);
                    gMoveMergeMap[transName]++;
                }
            }

            // Add charclip merger
            if (gMoveMergeMap[clipName] == 0) {
                FilePath fp(
                    MakeString("modular_song_data/charclips/%s.milo", clipName)
                );
                FileMerger::Merger merger(mMoveMerger.Ptr());
                merger.mName = charclips;
                merger.mDir = clipsDir;
                merger.mSubdirs = MergeFilter::kAllSubdirs;
                merger.mPreClear = true;
                merger.mSelected = fp;
                merger.mForceReload = true;
                mergers->push_back(merger);
                gMoveMergeMap[clipName]++;
            }

            // Add hammove merger
            Symbol hamMiloName =
                pVariant->FindArray("ham_move_milo_name", true)->Sym(1);
            if (gMoveMergeMap[hamMiloName] == 0) {
                FilePath fp(
                    MakeString("modular_song_data/hammoves/%s.milo", hamMiloName)
                );
                FileMerger::Merger merger(mMoveMerger.Ptr());
                merger.mName = hammoves;
                merger.mDir = movesDir;
                merger.mSubdirs = MergeFilter::kAllSubdirs;
                merger.mPreClear = true;
                merger.mSelected = fp;
                merger.mForceReload = true;
                mergers->push_back(merger);
                gMoveMergeMap[hamMiloName]++;
            }
    }

    mMoveMerger->StartLoad(mAsyncLoaded);
    sPopulating = false;
}

void HamDirector::OnPopulateFromMoveMgr() {
    if (!mMasterClipAnim.Ptr()) {
        MILO_NOTIFY("No MasterClipAnim in HamDirector.  Did you load a song?");
    } else {
        static Symbol move_parents("move_parents");
        static Symbol clip_crossover("clip_crossover");
        PropKeys *moveKeys =
            mMasterClipAnim->AddKeys(this, DataArrayPtr(move_parents), PropKeys::kSymbol);
        PropKeys *clipKeys =
            mMasterClipAnim->AddKeys(this, DataArrayPtr(clip_crossover), PropKeys::kSymbol);
        Keys<Symbol, Symbol> *moveSymKeys = moveKeys->AsSymbolKeys();
        Keys<Symbol, Symbol> *clipSymKeys = clipKeys->AsSymbolKeys();
        int count = TheMoveMgr->CurParents(0).size();
        for (int i = 0; i < count; i++) {
            if (TheMoveMgr->CurParents(0)[i] != 0) {
                float frame = BeatToSeconds((float)i * 4.0f - 1.0f) * 30.0f;
                int moveIdx = moveKeys->SetKey(frame);
                int clipIdx = clipKeys->SetKey(frame);
                (*moveSymKeys)[moveIdx].value = TheMoveMgr->CurParents(0)[i]->Name();
                const MoveVariant *second = TheMoveMgr->mRoutineMeasures[0][i].second;
                if (second != 0) {
                    (*clipSymKeys)[clipIdx].value = second->Name();
                }
            }
        }
    }
}

void HamDirector::DrawIconMan(Symbol sym1, Symbol sym2, Symbol sym3, float f1, float f2, RndTex *tex) {
    if (!mMasterClipAnim.Ptr()) {
        SetMasterClipAnim();
    }
    if (!mIconManChar.Ptr() || !mIconManTex.Ptr() || !mMasterClipAnim.Ptr() || !tex || !mClipDir.Ptr()) {
        return;
    }

    static Symbol practice("practice");
    static Symbol clip_sym("clip");

    auto _tmp0 = DataArrayPtr(practice);
    PropKeys *practiceKeys = mMasterClipAnim->GetKeys(this, _tmp0);
    Keys<Symbol, Symbol> *keys = practiceKeys->AsSymbolKeys();

    unsigned int foundIdx = 0;
    unsigned int numKeys = (unsigned int)keys->size();
    if (numKeys != 0) {
        for (unsigned int i = 0; i < numKeys; i++) {
            if ((*keys)[i].value == sym1) goto found;
            foundIdx++;
        }
    }
    foundIdx = -1;
found:
    if (foundIdx == (unsigned int)-1) {
        String moveStr(sym1.Str());
        moveStr += ".move";
        Symbol moveSym(moveStr.c_str());

        static Symbol move("move");
        PropKeys *moveKeys = mMasterClipAnim->GetKeys(this, DataArrayPtr(move));
        keys = moveKeys->AsSymbolKeys();

        foundIdx = 0;
        numKeys = (unsigned int)keys->size();
        if (numKeys != 0) {
            for (unsigned int i = 0; i < numKeys; i++) {
                if ((*keys)[i].value == moveSym) goto found2;
                foundIdx++;
            }
        }
        foundIdx = -1;
    found2:;
    }

    float keyFrame = (*keys)[foundIdx].frame;
    float keyBeat = SecondsToBeat(keyFrame / 30.0f);
    if (keyBeat + f1 < 0.0f) {
        f1 = 0.0f;
    }

    float beat = SecondsToBeat(keyFrame / 30.0f) + f2 + f1;
    float frame = BeatToSeconds(beat) * 30.0f;
    SecondsToBeat(keyFrame / 30.0f);

    PropKeys *clipKeys = mMasterClipAnim->GetKeys(this, DataArrayPtr(clip_sym));
    Keys<Symbol, Symbol> *clipKeysData = clipKeys->AsSymbolKeys();

    int clipIdx = clipKeysData->KeyLessEq(frame);
    Key<Symbol> &clipKey = (*clipKeysData)[clipIdx];

    CharClip *clip = mClipDir->Find<CharClip>(clipKey.value.Str(), false);
    if (!clip) {
        TheDebug << MakeString("Could not draw IconMan for %s", (char *)sym1.Str());
        return;
    }

    float clipBeat = SecondsToBeat(clipKey.frame / 30.0f);
    float alignOff = 0.0f;
    int beatAlign = (clip->PlayFlags() >> 12) & 0xf;
    if ((float)beatAlign != 0.0f) {
        alignOff = Mod(clipBeat - clip->StartBeat(), (float)beatAlign);
    }
    float poseBeat = beat - (clipBeat - alignOff) + clip->StartBeat();

    PoseIconMan(clip, poseBeat, NULL, (bool)tex, NULL, 0.0f, 0.0f);
}

void HamDirector::DrawIconMan(Difficulty diff, float f1, float f2, float f3, float f4, RndTex *tex) {
    if (!mMasterClipAnim.Ptr()) {
        SetMasterClipAnim();
    }
    if (!mIconManChar.Ptr() || !mIconManTex.Ptr() || !mMasterClipAnim.Ptr() || !tex || !mClipDir.Ptr()) {
        return;
    }

    if (diff == kDifficultyExpert) {
        static Symbol clip_sym("clip");
        PropKeys *clipKeys = mMasterClipAnim->GetKeys(this, DataArrayPtr(clip_sym));
        if (clipKeys) {
            Keys<Symbol, Symbol> *keys = clipKeys->AsSymbolKeys();
            float frame = BeatToSeconds(f1) * 30.0f;
            int clipIdx = keys->KeyLessEq(frame);
            Key<Symbol> &key = keys->at(clipIdx);

            CharClip *clip = mClipDir->Find<CharClip>(key.value.Str(), false);
            if (clip) {
                float clipBeat = SecondsToBeat(key.frame / 30.0f);
                if (f2 + f4 < clipBeat) {
                    f4 = 0.0f;
                }
                int beatAlign = (clip->PlayFlags() >> 12) & 0xf;
                float alignOff = 0.0f;
                if ((float)beatAlign != 0.0f) {
                    alignOff = Mod(clipBeat - clip->StartBeat(), (float)beatAlign);
                }
                float poseBeat = f1 - (clipBeat - alignOff) + clip->StartBeat();
                if (f1 - f2 > f3 + f4) {
                    poseBeat -= f3;
                }
                PoseIconMan(clip, poseBeat, NULL, (bool)tex, NULL, 0.0f, 0.0f);
            }
        }
    } else {
        static Symbol clip_sym2("clip");
        PropKeys *clipKeys = GetPropKeys(diff, clip_sym2);
        if (clipKeys) {
            Keys<Symbol, Symbol> *keys = clipKeys->AsSymbolKeys();
            float frame = BeatToSeconds(f1) * 30.0f;
            int clipIdx = keys->KeyLessEq(frame);
            Key<Symbol> &key = keys->at(clipIdx);
            float clipBeat = SecondsToBeat(key.frame / 30.0f);
            Symbol nextValue;
            Symbol prevValue;
            if ((unsigned)(clipIdx + 1) < keys->size()) {
                nextValue = (*keys)[clipIdx + 1].value;
            }
            if (clipIdx > 0) {
                prevValue = keys->at(clipIdx - 1).value;
            }
            DrawIconMan(key.value, nextValue, prevValue, f1 - clipBeat, f4, tex);
        }
    }
}

CharClip *HamDirector::GetClipStartAndEndBeats(
    Symbol clipName, float &startBeat, float &endBeat, std::pair<float, float> *range
) {
    static Symbol practice_sym("practice");
    static Symbol clip_sym("clip");

    if (!mMasterClipAnim) return nullptr;

    PropKeys *practiceKeys = mMasterClipAnim->GetKeys(this, DataArrayPtr(practice_sym));
    PropKeys *clipKeys = mMasterClipAnim->GetKeys(this, DataArrayPtr(clip_sym));

    if (!practiceKeys || !clipKeys) return nullptr;

    Keys<Symbol, Symbol> *practiceSymbols = practiceKeys->AsSymbolKeys();
    unsigned int size = practiceSymbols->size();
    unsigned int foundIdx = 0xffffffff;
    if (size != 0) {
        int byteOff = 0;
        do {
            if ((*practiceSymbols)[foundIdx + 1].value == clipName) goto found;
            foundIdx++;
            byteOff += 8;
        } while (foundIdx < size);
    }
    foundIdx = 0xffffffff;
found:
    if (foundIdx != 0xffffffff && (int)(foundIdx + 1) < (int)size) {
        Keys<Symbol, Symbol> *clipSymbols = clipKeys->AsSymbolKeys();
        int clipKeyIdx = clipSymbols->KeyLessEq((*practiceSymbols)[foundIdx].frame);
        if ((unsigned int)clipKeyIdx >= clipSymbols->size()) {
#ifndef HX_NATIVE
            stlpmtx_std::__stl_throw_out_of_range("vector");
#endif
        }
        CharClip *clip = mClipDir->Find<CharClip>((*clipSymbols)[clipKeyIdx].value.Str(), true);
        if (clip) {
            float beat1 = SecondsToBeat((*practiceSymbols)[foundIdx].frame * (1.0f / 30.0f));
            float clipStartBeat = clip->StartBeat();
            int loopCount = (clip->PlayFlags() >> 12) & 0xF;
            float loopAdjust = 0.0f;
            if (loopCount > 0) {
                loopAdjust = Mod(beat1 - clipStartBeat, (float)loopCount);
            }
            float adjust = beat1 - loopAdjust;
            startBeat = SecondsToBeat((*practiceSymbols)[foundIdx].frame * (1.0f / 30.0f)) - adjust + clipStartBeat;
            endBeat = SecondsToBeat((*practiceSymbols)[foundIdx + 1].frame * (1.0f / 30.0f)) - adjust + clipStartBeat;
            if (range) {
                range->first = SecondsToBeat((*practiceSymbols)[foundIdx].frame * (1.0f / 30.0f));
                range->second = SecondsToBeat((*practiceSymbols)[foundIdx + 1].frame * (1.0f / 30.0f));
                return clip;
            }
            return clip;
        }
    }
    return nullptr;
}


void HamDirector::Poll() {
    if (TheHamWardrobe) {
        TheHamWardrobe->UpdateOverlay();
    }
    if (!mPollEnabled) return;
    HamCharacter *player0 = TheHamWardrobe ? TheHamWardrobe->GetCharacter(0) : nullptr;
    HamCharacter *player1 = TheHamWardrobe ? TheHamWardrobe->GetCharacter(1) : nullptr;
    RndPropAnim *songAnim = SongAnim(0);
#ifdef HX_NATIVE
    {
        static int sDbg = 0;
        if (sDbg++ < 10 || (sDbg % 300 == 0)) {
            static Symbol mm("merge_moves");
            int mmVal = TheHamProvider ? TheHamProvider->Property(mm, true)->Int() : -1;
            MILO_LOG("ANIM-DIAG Poll: pollEnabled=%d p0=%p p1=%p songAnim=%p merge_moves=%d beat=%.2f\n",
                     mPollEnabled, (void*)player0, (void*)player1, (void*)songAnim, mmVal, TheTaskMgr.Beat());
            if (songAnim)
                MILO_LOG("ANIM-DIAG Poll: songAnim->GetFrame()=%.2f\n", songAnim->GetFrame());
            if (player0) {
                CharClip *fc = player0->Driver() ? player0->Driver()->FirstClip() : nullptr;
                MILO_LOG("ANIM-DIAG Poll: p0.SongAnimation()=%d p0.SongDriver()=%p p0.Driver()=%p firstClip=%p (%s, type=%s)\n",
                         player0->SongAnimation(), (void*)player0->SongDriver(),
                         (void*)player0->Driver(), (void*)fc,
                         fc ? fc->Name() : "null", fc ? fc->Type().Str() : "null");
                // Log default clip via Handle
                if (player0->Driver()) {
                    static Message dcMsg("default_clip");
                    DataNode dcVal = player0->Driver()->Handle(dcMsg, true);
                    Hmx::Object *dc = dcVal.GetObj(nullptr);
                    MILO_LOG("ANIM-DIAG Poll: p0.Driver().defaultClip=%p (%s)\n",
                             (void*)dc, dc ? dc->Name() : "null");
                }
            }
        }
    }
#endif
    if (songAnim) {
        // Song.anim frame advancement is driven by the world root's DTA path:
        //   WorldDir::Poll() → HandleType("select_camera") → OnSelectCamera()
        //     → frame = BeatToSeconds(Beat()) * 30.0f → songAnim->SetFrame(frame)
        // ProcCounter ensures select_camera fires every frame after Enter().
        if (player0 && player1) {
            int p0anim = player0->SongAnimation();
            int p1anim = player1->SongAnimation();
            bool doSongAnim = SongAnimation();
            if (doSongAnim) {
                ClipPlayer player0Clip, player1Clip;
                Key<Symbol> *practiceEnd = nullptr;
                Key<Symbol> *practiceStart = nullptr;
                if (p0anim != -1) {
                    bool clipInited = player0Clip.Init(0);
#ifdef HX_NATIVE
                    { static int s=0; if(s++<5) MILO_LOG("ANIM-DIAG ClipPlayer::Init(0)=%d frame=%.2f\n", clipInited, songAnim->GetFrame()); }
#endif
                    if (clipInited) {
                        player0Clip.PlayAnims(player0, songAnim->GetFrame(), unk2e4, mBlendDebug);
                    }
                }
                if (p1anim != -1) {
                    bool hasPractice = GetPracticeFrames(practiceStart, practiceEnd);
                    if (!hasPractice) {
                        bool clipInited = player1Clip.Init(1);
                        if (clipInited) {
                            player1Clip.PlayAnims(player1, songAnim->GetFrame(), unk2e4, mBlendDebug);
                        }
                    }
                }
                HamPlayerData *p0data = TheGameData->Player(0);
                HamPlayerData *p1data = TheGameData->Player(1);
                ClipPlayer *backupClipPlayer = IsEasierDifficulty(p0data->GetDifficulty(), p1data->GetDifficulty()) ? &player0Clip : &player1Clip;
                bool hasPractice2 = GetPracticeFrames(practiceEnd, practiceStart);
                if (!hasPractice2) {
                    const float sBackupDriftScale = 0.14f;
                    const float sBackupDriftOffset = 0.5f;
                    const float sBackupDriftFreq = 534.46f;
                    const float sBackupDriftDt = 1.0f / 300.0f;
                    const float sBackupDriftMax = 30.0f;
                    const float sBackupDriftNeg = -1.0f;
                    int backupIdx = 0;
                    while (true) {
                        HamCharacter *backup = TheHamWardrobe ? TheHamWardrobe->GetBackup(backupIdx) : nullptr;
                        backupIdx++;
                        if (!backup) break;
                        float noise = RndWind::GetWhiteNoise(
                            (float)backupIdx * sBackupDriftFreq + songAnim->GetFrame() * sBackupDriftDt
                        );
                        float drift = (noise - sBackupDriftOffset) * mBackupDrift * sBackupDriftScale;
                        if (0.0f < drift) {
                            drift = drift * sBackupDriftNeg;
                        }
                        backupClipPlayer->PlayAnims(
                            backup, songAnim->GetFrame() - drift * sBackupDriftMax, unk2e4 - drift * sBackupDriftMax, mBlendDebug
                        );
                        HamDriver *driver = backup->SongDriver();
                        driver->OffsetSec(drift);
                    }
                }
            }
        }
        unk2e4 = songAnim->GetFrame();
        float currentSeconds = TheTaskMgr.Seconds(TaskMgr::kRealTime);
        if (0.0f <= currentSeconds) {
            float currentSec = TheTaskMgr.Seconds(TaskMgr::kRealTime);
            float deltaSec = TheTaskMgr.DeltaSeconds();
            if (currentSec - deltaSec < 0.0f) {
                songAnim->StartAnim();
            }
        }
        mPoseFatalities->Poll();
        if (mVenue) {
            mVenue->Poll();
        }
        if (mWorldPostProc) {
            float blend = 1.0f;
            const char *overlayName;
            RndPostProc *overlayA = nullptr;
            RndPostProc *overlayB = nullptr;
            if (mCamPostProc) {
                mWorldPostProc->Copy(mCamPostProc, Hmx::Object::kCopyDeep);
                unk18c.CopyRef(mCamPostProc);
                overlayA = mCamPostProc;
                overlayName = "camera";
            } else if (mForcePostProc && !(mForcePostProcBlend < 1.0f)) {
                mWorldPostProc->Copy(mForcePostProc, Hmx::Object::kCopyDeep);
                unk18c.CopyRef(mForcePostProc);
                overlayA = mForcePostProc;
                overlayName = "force";
            } else if (mPostProcInterpA == mPostProcInterpB) {
                mWorldPostProc->Copy(mPostProcInterpA, Hmx::Object::kCopyDeep);
                unk18c.CopyRef(mPostProcInterpA);
                overlayName = "song authoring - 2 equiv";
            } else {
                mWorldPostProc->Interp(mPostProcInterpA, mPostProcInterpB, mPostProcInterpBlend);
                unk18c.CopyRef(mPostProcInterpB);
                overlayName = "song authoring";
                blend = mPostProcInterpBlend;
            }
            if (mForcePostProc && !mCamPostProc) {
                float forceBlend = mForcePostProcBlend;
                if (forceBlend > 0.0f && forceBlend < blend) {
                    mWorldPostProc->Interp(mWorldPostProc, mForcePostProc, forceBlend);
                    overlayB = mForcePostProc;
                    overlayName = "force";
                    blend = forceBlend;
                }
                if ((0.0f < mForcePostProcBlendRate && forceBlend < blend) ||
                    (mForcePostProcBlendRate < 0.0f && 0.0f < forceBlend)) {
                    float newBlend = TheTaskMgr.DeltaSeconds() * mForcePostProcBlendRate + mForcePostProcBlend;
                    mForcePostProcBlend = newBlend;
                    newBlend = -newBlend >= 0.0f ? 0.0f : newBlend;
                    blend = newBlend - blend >= 0.0f ? blend : newBlend;
                    mForcePostProcBlend = blend;
                }
            }
            UpdatePostProcOverlay(overlayName, overlayA, overlayB, blend);
        }
        if (mFreestyleEnabled && mVisualizer && !mVisualizer->Showing()) {
            float deltaSeconds = TheTaskMgr.DeltaSeconds();
            unk1d4 += deltaSeconds;
            if (unk1d4 > 1.6f) {
                StartStopVisualizer();
            }
        }
    }
}
