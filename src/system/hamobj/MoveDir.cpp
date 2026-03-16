#include "hamobj/MoveDir.h"
#include "FilterQueue.h"
#include "HamMaster.h"
#include "MoveDir.h"
#include "ScoreUtl.h"
#include "char/Character.h"
#include "flow/PropertyEventProvider.h"
#include "gesture/BaseSkeleton.h"
#include "gesture/GestureMgr.h"
#include "gesture/Skeleton.h"
#include "gesture/SkeletonClip.h"
#include "gesture/SkeletonDir.h"
#include "gesture/SkeletonUpdate.h"
#include "gesture/SkeletonViz.h"
#include "gesture/StubCameraInput.h"
#include "hamobj/CharFeedback.h"
#include "hamobj/DancerSequence.h"
#include "hamobj/DetectFrame.h"
#include "hamobj/Difficulty.h"
#include "hamobj/ErrorNode.h"
#include "hamobj/FilterVersion.h"
#include "hamobj/HamAudio.h"
#include "hamobj/HamDirector.h"
#include "hamobj/HamGameData.h"
#include "hamobj/HamMove.h"
#include "hamobj/HamPhraseMeter.h"
#include "hamobj/MoveMgr.h"
#include "hamobj/HamPlayerData.h"
#include "hamobj/MoveDetector.h"
#include "hamobj/PracticeSection.h"
#include "hamobj/ScoreUtl.h"
#include "hamobj/SongCollision.h"
#include "meta/SongMetadata.h"
#include "meta/SongMgr.h"
#include "obj/Data.h"
#include "obj/DataFile.h"
#include "obj/DataFunc.h"
#include "obj/DataUtl.h"
#include "obj/Dir.h"
#include "obj/DirLoader.h"
#include "obj/Msg.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "obj/Utl.h"
#include "os/DateTime.h"
#include "os/Debug.h"
#include "os/File.h"
#include "os/System.h"
#include "rndobj/Dir.h"
#include "rndobj/Draw.h"
#include "rndobj/Font.h"
#include "rndobj/FontBase.h"
#include "rndobj/Overlay.h"
#include "rndobj/Rnd.h"
#include "rndobj/Utl.h"
#include "ui/PanelDir.h"
#include "ui/ResourceDirPtr.h"
#include "ui/UILabelDir.h"
#include "utl/BinStream.h"
#include "utl/FilePath.h"
#include "utl/Loader.h"
#include "utl/SongInfoCopy.h"
#include "utl/Std.h"
#include "utl/Symbol.h"
#include "utl/TimeConversion.h"
#include "world/Dir.h"
#include "xdk/XAPILIB.h"

std::vector<FilterVersion *> MoveDir::sFilterVersions;

namespace {
    static Hmx::Color sGray(0.5, 0.5, 0.5, 1);
    static Hmx::Color sGreen(0, 0.6, 0, 0.5);
    static Hmx::Color sDarkerGray(0.3, 0.3, 0.3, 0.8);
    static Hmx::Color sLightGray(0.8, 0.8, 0.8, 1);
    static Hmx::Color sDarkGray(0.3, 0.3, 0.3, 0.6);

    float DrawOverlayBar(float f1, float f2, float f3, const Hmx::Color &c, float f4) {
        TheRnd.DrawRectScreen(
            Hmx::Rect(f2, f1, (f3 - f2), f4), c, nullptr, nullptr, nullptr
        );
        UtilDrawLine(Vector2(f2, f1), Vector2(f2, f1 + f4), sGray);
        UtilDrawLine(Vector2(f3, f1), Vector2(f3, f1 + f4), sGray);
        return f4;
    }

    float DrawDetectedBar(
        float y, const char *name, float detectFrac, float startX, float endX, bool halfAlpha, bool asPercent
    ) {
        Hmx::Color bgColor = sDarkerGray;
        Hmx::Color fillColor = sGreen;
        Hmx::Color textColor = sLightGray;

        if (halfAlpha) {
            bgColor.red *= 0.5f;
            bgColor.green *= 0.5f;
            bgColor.blue *= 0.5f;
            fillColor.red *= 0.5f;
            fillColor.green *= 0.5f;
            fillColor.blue *= 0.5f;
            textColor.red *= 0.5f;
            textColor.green *= 0.5f;
            textColor.blue *= 0.5f;
        }

        String str(name);
        const char *fmt;
        float dispValue;
        if (asPercent) {
            dispValue = detectFrac * 100.0f;
            fmt = ": %.2f%%";
        } else {
            dispValue = detectFrac;
            fmt = ": %.2f";
        }
        str += MakeString(fmt, dispValue);

        float barHeight = 0.01f;
        float filledWidth = (endX - startX) * detectFrac;
        DrawOverlayBar(y, startX, endX, bgColor, barHeight);
        DrawOverlayBar(y, startX, filledWidth + startX, fillColor, barHeight);

        Vector2 pos(startX, y);
        TheRnd.DrawStringScreen(str.c_str(), pos, textColor, true);

        return y + barHeight;
    }

    void DrawBeatLine(float, float, float, const Hmx::Color &);
    float DrawPlayClip(float farg0, SkeletonClip *clip, int player) {
        MILO_ASSERT(clip, 0x762);
        String str(clip->Name());
        const char *suffix;
        if (player < clip->NumMoveRatings()) {
            const SkeletonClip::MoveRating &rating = clip->GetMoveRating(player);
            const Symbol *sym = &rating.mExpected;
            if (sym->Null()) {
                Symbol none("<none>");
                sym = &none;
            }
            suffix = MakeString(" (bar %i: expected=%s)", player, *sym);
        } else {
            suffix = " (no rating overrides)";
        }
        str += suffix;

        Hmx::Rect rect(0.009999999776482582f, farg0, 0.9f, 0.01f);
        TheRnd.DrawRectScreen(rect, sDarkGray, nullptr, nullptr, nullptr);
        Vector2 pos(0.009999999776482582f, farg0);
        TheRnd.DrawStringScreen(str.c_str(), pos, sLightGray, true);
        return pos.y;
    }

}

String RecordClipName(const char *cc, int i2) {
    DateTime dt;
    GetDateAndTime(dt);
    Difficulty playerDiff = TheGameData->Player(0)->GetDifficulty();
    char diff;
    if (playerDiff == kDifficultyExpert) {
        diff = 'h';
    } else {
        diff = DifficultyToSym(playerDiff).Str()[0];
    }
    const char *prefix = "";
    switch (i2) {
    case 1:
        prefix = "b";
        break;
    case 2:
        prefix = "c";
        break;
    case 3:
        prefix = "d";
        break;
    default:
        break;
    }
    String ret(MakeString(
        "%s%d~%s~%c~%s~%s",
        prefix,
        dt.ToCode(),
        TheGameData->GetSong(),
        diff,
        TheGameData->Player(0)->CurrentDancer(),
        cc
    ));
    if (ret.length() > 38) {
        ret.resize(38);
    }
    return ret;
}

MoveMode CurrentMoveMode() {
    MILO_ASSERT(TheHamDirector, 0x79);
    return TheHamDirector->InPracticeMode() ? (MoveMode)1 : (MoveMode)0;
}

MoveDir::MoveDir()
    : mShowMoveOverlay(0), mErrorNodeInfo(0), mPlayClip(this), mRecordClip(this),
      mAlternateRecordClip(this), mSkeletonRecordClip(this), unk2e4(0), mReportMove(this), mFiltersEnabled(0),
      mGamePanel(0), unk30c(0), mFilterQueue(0), mAsyncDetector(0), mUpdateLoader(0),
      mFinishingMoveMeasure(10000), mMoveOverlay(RndOverlay::Find("ham_move")),
      mDancerSeq(this), unk414(0), mSkeletonViz(Hmx::Object::New<SkeletonViz>()),
      mShowErrorFrames(0), mDebugLatencyOffset(0), mDebugLoop(0), mLastPollMs(0),
      mDebugCollision(0), unkf84(-1) {
    for (int i = 0; i < 2; i++) {
        mMovePlayerData[i].Reset();
        mCurMoveSmoothers[i].SetCoeffs(1, 0);
        filler[i] = 0;
        mCurMoveNormalizedResult[i] = 0;
        mPrevMoveNormalizedResult[i] = 0;
        mCurMove[i] = 0;
        mCurMoveRating[i] = kMoveRatingOk;
        mPrevMoveRating[i] = kMoveRatingOk;
        unkf04[i].Reset();
    }
    SetFilterVersion("ham2");
}

MoveDir::~MoveDir() {
    RELEASE(mFilterQueue);
    RELEASE(mAsyncDetector);
    mMoveOverlay = RndOverlay::Find("ham_move", false);
    if (mMoveOverlay && mMoveOverlay->GetCallback() == this) {
        mMoveOverlay->SetCallback(nullptr);
        if (TheLoadMgr.EditMode()) {
            mMoveOverlay->SetShowing(false);
        }
    }
    delete mSkeletonViz;
    if (SkeletonUpdate::HasInstance()) {
        SkeletonUpdateHandle handle = SkeletonUpdate::InstanceHandle();
        if (handle.HasCallback(this)) {
            handle.RemoveCallback(this);
        }
    }
}

BEGIN_HANDLERS(MoveDir)
    HANDLE_ACTION(start_song_record, 0)
    HANDLE_ACTION(stop_song_record, StopSongRecord())
    HANDLE_ACTION(
        simulate_song,
        SimulateSong(
            _msg->Size() > 2 ? _msg->Int(2) : 0, _msg->Size() > 3 ? _msg->Int(3) : 0
        )
    )
    HANDLE_ACTION(reload_scoring, ReloadScoring())
    HANDLE_ACTION(reset_detection, ResetDetection())
    HANDLE(stream_jump, OnStreamJump)
    HANDLE_EXPR(import_clip, ImportClip(_msg->Int(2)))
    HANDLE_ACTION(debug_rotate, mSkeletonViz->Rotate(_msg->Float(2)))
    // these don't appear to be inlined methods
    {
        static Symbol _s("disable_all_detectors");
        if (sym == _s) {
            MILO_ASSERT(mAsyncDetector, 0x136F);
            mAsyncDetector->DisableAllDetectors();
            return 0;
        }
    }
    {
        static Symbol _s("enable_detector");
        if (sym == _s) {
            MILO_ASSERT(mAsyncDetector, 0x1371);
            mAsyncDetector->EnableDetector(_msg->Obj<HamMove>(2));
            return 0;
        }
    }
    {
        static Symbol _s("disable_detector");
        if (sym == _s) {
            MILO_ASSERT(mAsyncDetector, 0x1373);
            mAsyncDetector->DisableDetector(_msg->Obj<HamMove>(2));
            return 0;
        }
    }
    HANDLE_EXPR(
        active_detector_result,
        mAsyncDetector->MoveRatingFrac(
            _msg->Int(2), (MoveAsyncDetector::RatingBar)0, _msg->Obj<HamMove>(3)
        )
    )
    HANDLE_EXPR(
        last_detector_result,
        mAsyncDetector->MoveRatingFrac(
            _msg->Int(2), (MoveAsyncDetector::RatingBar)1, _msg->Obj<HamMove>(3)
        )
    )
    HANDLE_EXPR(cur_move_normalized_result, mCurMoveNormalizedResult[_msg->Int(2)])
    HANDLE_EXPR(
        active_detector_looped_result,
        mAsyncDetector->MoveRatingFrac(
            _msg->Int(2), (MoveAsyncDetector::RatingBar)2, _msg->Obj<HamMove>(3)
        )
    )
    HANDLE_EXPR(
        cur_move_normalized_result_smoothed, mCurMoveSmoothers[_msg->Int(2)].Level()
    )
    HANDLE_ACTION(
        detector_clear_looped_result,
        mAsyncDetector->ClearLoopedRatingFrac(_msg->Obj<HamMove>(2))
    )
    HANDLE_EXPR(get_cur_move, mCurMove[_msg->Int(2)])
    HANDLE_EXPR(get_cur_measure, MoveIdx())
    HANDLE_EXPR(get_cur_beat, TheTaskMgr.TotalBeat())
    HANDLE_EXPR(get_finishing_move_measure, mFinishingMoveMeasure)
    HANDLE_ACTION(clear_limb_feedback, ClearLimbFeedback(_msg->Int(2)))
    HANDLE_ACTION(beat, OnBeat())
    HANDLE_SUPERCLASS(SkeletonDir)
END_HANDLERS

BEGIN_PROPSYNCS(MoveDir)
    SYNC_PROP_SET(current_move, mMovePlayerData[0].mCurMove.Ptr(), )
    SYNC_PROP_SET(filters_enabled, mFiltersEnabled, SetFiltersEnabled(_val.Int()))
    SYNC_PROP_SET(move_overlay, mShowMoveOverlay, SetMoveOverlay(_val.Int()))
    SYNC_PROP(debug_latency_offset, mDebugLatencyOffset)
    SYNC_PROP_SET(
        debug_skeleton_rotation,
        mSkeletonViz->PhysicalCamRotation(),
        mSkeletonViz->SetPhysicalCamRotation(_val.Float())
    )
    SYNC_PROP(debug_collision, mDebugCollision)
    SYNC_PROP(debug_node_types, mErrorNodeInfo)
    SYNC_PROP(debug_node_joints, mErrorNodeInfo)
    SYNC_PROP_SET(play_clip, mPlayClip.Ptr(), SetSongPlayClip(_val.Obj<SkeletonClip>()))
    SYNC_PROP(report_move, mReportMove)
    SYNC_PROP(record_clip, mRecordClip)
    SYNC_PROP(import_clip_path, mImportClipPath)
    SYNC_SUPERCLASS(SkeletonDir)
END_PROPSYNCS

BEGIN_SAVES(MoveDir)
    SAVE_REVS(35, 0)
    SAVE_SUPERCLASS(SkeletonDir)
    if (IsProxy()) {
        bs << mFiltersEnabled;
    }
    bs << mShowMoveOverlay;
    bs << mErrorNodeInfo;
    if (!bs.Cached()) {
        bs << mImportClipPath;
    } else {
        bs << 0;
    }
    MILO_ASSERT(mFilterVer, 0x922);
    bs << mFilterVer->mVersionSym;
END_SAVES

BEGIN_COPYS(MoveDir)
    COPY_SUPERCLASS(SkeletonDir)
    CREATE_COPY(MoveDir)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mShowMoveOverlay)
        COPY_MEMBER(mErrorNodeInfo)
        COPY_MEMBER(mImportClipPath)
        COPY_MEMBER(mFiltersEnabled)
        COPY_MEMBER(mPlayClip)
        COPY_MEMBER(mRecordClip)
        COPY_MEMBER(mAlternateRecordClip)
        COPY_MEMBER(mSkeletonRecordClip)
        COPY_MEMBER(mReportMove)
    END_COPYING_MEMBERS
END_COPYS

BEGIN_LOADS(MoveDir)
    PreLoad(bs);
    PostLoad(bs);
END_LOADS

INIT_REVS(0x23, 0)

void MoveDir::PreLoad(BinStream &bs) {
    LOAD_REVS(bs)
    ASSERT_REVS(0x23, 0)
    if (d.rev < 9) {
        RndDir::PreLoad(bs);
    } else {
        SkeletonDir::PreLoad(bs);
    }
    Symbol song = TheGameData->GetSong();
    if (!IsProxy() && gLoadingProxyFromDisk && !song.Null()) {
        SongMgr *songMgr = ObjectDir::Main()->Find<SongMgr>("song_mgr", false);
        if (songMgr) {
            const SongMetadata *songData =
                songMgr->Data(songMgr->GetSongIDFromShortName(song, true));
            if (songData->Version() < 11) {
                mUpdateLoader = dynamic_cast<DirLoader *>(TheLoadMgr.AddLoader(
                    FilePath(FileRoot(), songMgr->SongFilePath(song, "_update.milo", 11)),
                    kLoadFront
                ));
            }
        }
    }
    d.PushRev(this);
}

void MoveDir::PostLoad(BinStream &bs) {
    BinStreamRev d(bs, bs.PopRev(this));
    if (d.rev < 9) {
        RndDir::PostLoad(bs);
    } else {
        SkeletonDir::PostLoad(bs);
    }
    if (d.rev < 5) {
        bool b;
        d >> b;
    }
    if (d.rev > 0 && d.rev < 2) {
        String str;
        d >> str;
    }
    if (!IsProxy() || d.rev < 8) {
        if (d.rev > 3 && d.rev < 9) {
            String str;
            d >> str;
        }
        if (d.rev > 5 && d.rev < 32) {
            if (d.rev > 0x1A) {
                ObjPtrVec<HamMove> moves(this, (EraseMode)0, kObjListAllowNull);
                d >> moves;
            } else {
                ObjPtr<HamMove> move(this);
                d >> move;
            }
        }
    }
    if (IsProxy() && d.rev > 10 && d.rev < 0xD) {
        ObjPtr<Character> character(this);
        WorldDir *wDir = TheHamDirector ? TheHamDirector->GetVenueWorld() : nullptr;
        character.Load(d.stream, true, wDir);
    }
    if (IsProxy() && d.rev > 0xC) {
        d >> mFiltersEnabled;
    }
    char buf[0x80];
    if (d.rev < 0x23) {
        if (IsProxy() && d.rev > 0xE) {
            d.stream.ReadString(buf, 0x80);
        }
        if (IsProxy() && d.rev > 0xD) {
            d.stream.ReadString(buf, 0x80);
        }
        if (IsProxy() && d.rev > 0x17) {
            d.stream.ReadString(buf, 0x80);
        }
    }
    if (d.rev > 6) {
        if (d.rev > 9 && d.rev < 18) {
            int x;
            d >> x;
            mShowMoveOverlay = x;
        } else {
            d >> mShowMoveOverlay;
        }
    }
    if (d.rev > 0xF && d.rev < 0x1F) {
        bool b;
        d >> b;
    }
    if (d.rev > 0x16 && d.rev < 0x22) {
        bool b;
        d >> b;
    }
    if (d.rev > 0x14) {
        if (d.rev > 0x1B) {
            d >> mErrorNodeInfo;
        } else {
            Symbol s;
            d >> s;
        }
    } else if (d.rev > 0x11) {
        int x;
        d >> x;
    }
    if (d.rev > 0x15 && d.rev < 0x1D) {
        bool b;
        d >> b;
    }
    if (d.rev > 0x19 && d.rev < 0x21) {
        int x;
        d >> x;
        bool b;
        d >> b;
        int y, z;
        d >> y >> z;
        for (int i = 0; i < 3; i++) {
            d >> b >> b;
        }
    }
    if (d.rev > 0x13) {
        d >> mImportClipPath;
    }
    if (d.rev < 0x19) {
        if (d.rev > 0x14) {
            int x;
            d >> x;
            Symbol s;
            for (int i = 0; i < x; i++) {
                int n;
                d >> s >> n;
            }
        } else if (d.rev > 0xB) {
            int max = 5;
            if (d.rev < 0x11) {
                max = 4;
            }
            for (int i = 0; i < max; i++) {
                int x;
                d >> x;
            }
        }
    }
    Symbol filterVersion;
    static Symbol ham1("ham1");
    static Symbol ham2("ham2");
    if (d.rev < 0x1A) {
        filterVersion = ham1;
    } else if (d.rev < 0x1E) {
        filterVersion = ham2;
    } else {
        d >> filterVersion;
    }
    SetFilterVersion(filterVersion);
    if (mUpdateLoader) {
        ObjectDir *loaderDir = mUpdateLoader->GetDir();
        RELEASE(mUpdateLoader);
        if (loaderDir) {
            for (ObjDirItr<Hmx::Object> it(loaderDir, true); it != nullptr; ++it) {
                Hmx::Object *cur = it;
                if (cur != loaderDir) {
                    const char *curName = cur->Name();
                    HamMove *move = dynamic_cast<HamMove *>(cur);
                    if (move) {
                        HamMove *find = Find<HamMove>(curName, false);
                        if (find) {
                            find->Update(move);
                        }
                    } else {
                        ObjectDir *dir = dynamic_cast<ObjectDir *>(cur);
                        if (dir && !*dir->GetPathName()) {
                            continue;
                        } else {
                            Hmx::Object *find = Find<Hmx::Object>(curName, false);
                            if (find) {
                                delete find;
                            }
                            it->SetName(curName, this);
                        }
                    }
                }
            }
            delete loaderDir;
        } else {
            MILO_NOTIFY("%s has no associated update file for song", PathName(this));
        }
    }
    static Symbol DLC_UPDATE_FONTS("DLC_UPDATE_FONTS");
    DataArray *updateArray = DataGetMacro(DLC_UPDATE_FONTS);
    for (int i = 0; i < updateArray->Size(); i++) {
        char buffer[256];
        String curStr(updateArray->Str(i));
        strcpy(buffer, MakeString("%s_%s", curStr, SystemLanguage()));
        AddClassExt(buffer, RndFont::StaticClassName());
        RndFont *updateFont = Find<RndFont>(buffer, false);
        if (updateFont) {
            FilePath path;
            if (ResourceDirBase::MakeResourcePath(
                    path, "HamLabel", "UILabelDir", curStr.c_str()
                )) {
                ObjDirPtr<UILabelDir> labelDirPtr;
                labelDirPtr.LoadFile(path, false, true, kLoadFront, false);
                if (labelDirPtr.IsLoaded()) {
                    RndFontBase *font = labelDirPtr->FontObj(gNullStr);
                    MILO_ASSERT(font, 0xA52);
                    ReplaceObject(font, updateFont, false, false, true);
                    mUpdateFonts.push_back(labelDirPtr);
                }
            }
        }
    }
    if (d.rev < 3 && !IsProxy()) {
        MILO_NOTIFY(
            "%s MoveDir older than version 3, need to resave this file", PathName(this)
        );
    }
    if (TheLoadMgr.EditMode()) {
        if (mFiltersEnabled) {
            MiloInit();
        }
    } else {
        mRecordClip = nullptr;
    }
}

void MoveDir::Poll() {
    SkeletonDir::Poll();
    mSkeletonViz->Poll();
    if (TheHamDirector) {
        int curMeasure = TheTaskMgr.CurrentMeasure();
        for (int i = 0; i < 2; i++) {
            HamMove *oldMove = mCurMove[i];
            mCurMove[i] = nullptr;
            filler[i] = oldMove;
            MovePlayerData &curPlayerData = mMovePlayerData[i];
            if (curMeasure >= 0 && curMeasure < curPlayerData.mMoveKeys.size()) {
                mCurMove[i] = curPlayerData.mMoveKeys[curMeasure].move;
            }
            MoveRating oldRating = mCurMoveRating[i];
            mCurMoveRating[i] = kMoveRatingOk;
            mPrevMoveRating[i] = oldRating;
            float oldRes = mCurMoveNormalizedResult[i];
            mCurMoveNormalizedResult[i] = 0;
            mPrevMoveNormalizedResult[i] = oldRes;

            if (mCurMove[i]) {
                std::pair<DetectFrame *, DetectFrame *> frames;
                DetectRange(curPlayerData.mDetectFrames, frames, curMeasure, curMeasure);
                float frac = DetectFrac(i, mCurMove[i], frames);
                mCurMoveRating[i] =
                    DetectFracToMoveRating(frac, mCurMove[i]->RatingOverride());
                mCurMoveNormalizedResult[i] =
                    DetectFracToRatingFrac(frac, mCurMove[i]->RatingOverride());
            }
            mCurMoveSmoothers[i].Smooth(
                mCurMoveNormalizedResult[i],
                TheMaster && TheMaster->GetMeasure() == 3 ? TheTaskMgr.DeltaUISeconds() * 4.0f
                                                     : TheTaskMgr.DeltaUISeconds()
            );
            if (mCurMoveRating[i] <= kMoveRatingPerfect && mPrevMoveRating[i] > 1) {
                static Symbol passed_move_p1("passed_move_p1");
                static Symbol passed_move_p2("passed_move_p2");
                TheHamProvider->Export(
                    Message(i == 0 ? passed_move_p1 : passed_move_p2, mCurMove[i]->Name()),
                    true
                );
            }
        }
        if ((mCurMoveRating[0] <= 1 || mCurMoveRating[1] <= 1) && mPrevMoveRating[0] > 1
            && mPrevMoveRating[1] > 1) {
            static Message msg_passed_move("passed_move");
            TheHamProvider->Export(msg_passed_move, true);
        }
    }
}

void MoveDir::Enter() {
    PanelDir::Enter();
    int i13 = 0;
    if (TheHamDirector) {
        std::vector<HamMoveKey> hamMoveKeys;
        for (int i = 0; i < kNumDifficultiesDC2; i++) {
            TheHamDirector->MoveKeys((Difficulty)i, this, hamMoveKeys);
            int numKeys = hamMoveKeys.size();
            if (i13 < numKeys) {
                i13 = numKeys;
            }
            if (i == kDifficultyEasy) {
                while (--numKeys > 0) {
                    HamMoveKey &curKey = hamMoveKeys[numKeys];
                    if (curKey.move && curKey.move->IsFinalPose()) {
                        int tmp = curKey.beat / -4.0f;
                        mFinishingMoveMeasure = 1 - tmp;
                    }
                }
            }
        }
    }

    for (int i = 0; i < 2; i++) {
        MovePlayerData &cur = mMovePlayerData[i];
        cur.mCurMove = nullptr;
        cur.mMoveKeys.reserve(i13);
        cur.mDetectFrames.reserve(i13 << 4);
    }

    if (!TheLoadMgr.EditMode()) {
        mGamePanel = ObjectDir::Main()->Find<Hmx::Object>("game_panel", false);
        mErrorNodeInfo = 0;
        mFiltersEnabled = true;
        if (TheLoadMgr.EditMode()) {
            MiloInit();
        }
        mDebugLoopMarker = -1;
    } else {
        mGamePanel = nullptr;
    }

    if (TheHamDirector) {
        if (TheLoadMgr.EditMode()) {
            ResetDetection();
        }
        WorldDir *wDir = TheHamDirector->GetVenueWorld();
        if (wDir) {
            for (int i = 0; i < 2; i++) {
                MovePlayerData &cur = mMovePlayerData[i];
                ObjectDir *playerDir =
                    wDir->Find<ObjectDir>(MakeString("player%i", i), false);
                if (playerDir) {
                    cur.mFeedback =
                        playerDir->Find<CharFeedback>("char_feedback.cf", false);
                }
                if (cur.mFeedback) {
                    cur.mFeedback->ResetErrors();
                }
                cur.mPhraseMeter =
                    wDir->Find<HamPhraseMeter>(MakeString("phrase_meter%i", i), false);
                cur.mTextFeedback =
                    wDir->Find<RndDrawable>(MakeString("text_feedback%i", i), false);
            }
        }
        delete mFilterQueue;
        mFilterQueue = new FilterQueue();
        RELEASE(mAsyncDetector);
        mAsyncDetector = new MoveAsyncDetector(this);
        mSkeletonViz->Init();
        if (TheMaster) {
            static Symbol stream_jump("stream_jump");
            static Symbol beat("beat");
            TheMaster->AddSink(this, stream_jump);
            TheMaster->AddSink(this, beat);
        }
    }
}

void MoveDir::Exit() {
    PanelDir::Exit();
    if (TheMaster) {
        TheMaster->RemoveSink(this);
    }
}

void MoveDir::Update(const SkeletonUpdateData &data) {
    if (mFilterQueue) {
        mFilterQueue->Poll(data);
    }
}

void MoveDir::PostUpdate(const SkeletonUpdateData *data) {
    if (data) {
        if (mRecordClip) {
            mRecordClip->PollRecording(*data->mFrame);
        }
        if (mAlternateRecordClip) {
            mAlternateRecordClip->PollRecording(*data->mFrame);
        }
        if (mSkeletonRecordClip) {
            mSkeletonRecordClip->PollRecording(*data->mFrame);
        }
        if (TheLoadMgr.EditMode()) {
            MILO_ASSERT(TheGameData, 0x387);
            TheGameData->AutoAssignSkeletons(data);
        }
        if (mMoveOverlay->Showing()) {
            if (mPlayClip && mDebugLatencyOffset) {
                SkeletonFrame skeletonFrame;
                if (mPlayClip->SkeletonFrameAt(
                        sLatencySeconds + SongSeconds(), skeletonFrame
                    )) {
                    mDebugSkeleton.Poll(0, skeletonFrame);
                }
            } else {
                const Skeleton *playerSkeleton = TheGameData->Player(0)->GetSkeleton(
                    (const Skeleton *const(&)[6])data->mSkeletonsRight
                );
                if (playerSkeleton) {
                    mDebugSkeleton = *playerSkeleton;
                }
            }
        }
    }
    PostUpdateFilters();
    for (int i = 0; i < 2; i++) {
        if (!mFiltersEnabled
            || (mMovePlayerData[i].mCurMove && mMovePlayerData[i].mCurMove->IsRest())) {
            if (mMovePlayerData[i].mFeedback) {
                mMovePlayerData[i].mFeedback->ResetErrors();
            }
        }
    }
    FinalPoseStateMachine();
}

void MoveDir::Draw(const BaseSkeleton &baseSkeleton, SkeletonViz &skeletonViz) {
    if (unk414) {
        int actual_ms = unk414->ElapsedMs();
        if (actual_ms != -1) {
            for (int i = 0; i < kNumJoints; i++) {
                Vector3 vdisp;
                int disp_ms;
                unk414->Displacement(
                    nullptr, kCoordCamera, (SkeletonJoint)i, actual_ms, vdisp, disp_ms
                );
                MILO_ASSERT(actual_ms == disp_ms, 0x50F);
                Vector3 camJointPos = unk414->CamJointPos((SkeletonJoint)i);
                Vector3 vdiff;
                Subtract(camJointPos, vdisp, vdiff);
                Hmx::Color color(0.3f, 0.6f, 0.3f);
                mSkeletonViz->DrawLine3D(vdiff, camJointPos, 0.01f, color, nullptr);
            }
        }
    } else if (mFiltersEnabled && mShowErrorFrames) {
        MILO_ASSERT(TheGestureMgr, 0x51A);
        MILO_ASSERT(TheHamDirector, 0x51B);
        MoveMode moveMode = CurrentMoveMode();
        const Skeleton *player_skel = dynamic_cast<const Skeleton *>(&baseSkeleton);
        MILO_ASSERT(player_skel, 0x51F);
        SkeletonUpdateHandle handle = SkeletonUpdate::InstanceHandle();
        ErrorFrameInput input(
            handle.History(), mShowErrorFrames->GetDancerFrame()->mSkeleton, baseSkeleton,
            SongSpeed()
        );
        ErrorNode **nodePtr = mFilterVer->mErrorNodes;
        for (int i = 0; i < mFilterVer->NumNodes(); i++) {
            ErrorNode *node = *nodePtr;
            if (node->IsTypeJointMatch(mErrorNodeInfo)) {
                ErrorNodeInput nodeInput;
                mFilterVer->NodeInput(i, mShowErrorFrames, moveMode, nodeInput);
                node->VizError(skeletonViz, input, nodeInput);
            }
            nodePtr++;
        }
    }
}

DataNode OnDetectFracToRating(DataArray *a) {
    HamMove *move = a->Size() > 2 ? a->Obj<HamMove>(2) : nullptr;
    const std::vector<float> *ratings = nullptr;
    if (move) {
        ratings = move->RatingOverride();
    }
    return DetectFracToRating(a->Float(1), ratings, nullptr);
}

DataNode OnDetectFracToRatingFrac(DataArray *a) {
    HamMove *move = a->Obj<HamMove>(2);
    return DetectFracToRatingFrac(a->Float(1), move->RatingOverride());
}

DataNode OnRatingStateToIndex(DataArray *a) { return RatingStateToIndex(a->Sym(1)); }

DataNode OnGetScoreBonus(DataArray *a) {
    HamMove *move = a->Size() > 2 ? a->Obj<HamMove>(2) : nullptr;
    const std::vector<float> *ratings = nullptr;
    if (move) {
        ratings = move->RatingOverride();
    }
    return GetScoreBonus(a->Float(1), ratings);
}

void MoveDir::Init() {
    REGISTER_OBJ_FACTORY(MoveDir);
    DataArray *cfg = SystemConfig()->FindArray("scoring", false);
    if (cfg) {
        LoadScoring(cfg);
    }
    DataRegisterFunc("detect_frac_to_rating", OnDetectFracToRating);
    DataRegisterFunc("detect_frac_to_rating_frac", OnDetectFracToRatingFrac);
    DataRegisterFunc("rating_state_to_index", OnRatingStateToIndex);
    DataRegisterFunc("get_score_bonus", OnGetScoreBonus);
}

void MoveDir::ClearLimbFeedback(int player) {
    MILO_LOG("MoveDir::ClearLimbFeedback(int player = %d)\n", player);
    CharFeedback *feedback = mMovePlayerData[player].mFeedback;
    HamPlayerData *hpd = TheGameData->Player(player);
    if (feedback && hpd) {
        feedback->ResetErrors();
        for (int i = 0; i < 4; i++) {
            feedback->UpdateLimb(i, false);
        }
    }
}

void MoveDir::SetFiltersEnabled(bool enabled) {
    mFiltersEnabled = enabled;
    if (mFiltersEnabled && TheLoadMgr.EditMode()) {
        MiloInit();
    }
}

void MoveDir::SetFilterVersion(Symbol version) {
    for (int i = 0; i < sFilterVersions.size(); i++) {
        if (sFilterVersions[i]->mVersionSym == version) {
            mFilterVer = sFilterVersions[i];
            return;
        }
    }
    MILO_FAIL("Could not find filter version %s", version);
}

const FilterVersion *MoveDir::FindFilterVersion(FilterVersionType t) {
    for (std::vector<FilterVersion *>::iterator it = sFilterVersions.begin();
         it != sFilterVersions.end();
         ++it) {
        if ((*it)->mType == t)
            return *it;
    }
    return nullptr;
}

HamMove *MoveDir::CurrentMove(int player) const {
    MILO_ASSERT((0) <= (player) && (player) < (2), 0x164);
    return mMovePlayerData[player].mCurMove;
}

int MoveDir::MoveIdx() const { return TheTaskMgr.CurrentMeasure(); }
int MoveDir::MoveBeat() const { return TheTaskMgr.CurrentBeat(); }

void MoveDir::SetMoveOverlay(bool overlay) {
    if (!mFiltersEnabled && TheLoadMgr.EditMode()) {
        mFiltersEnabled = true;
        if (TheLoadMgr.EditMode()) {
            MiloInit();
        }
    }
    mShowMoveOverlay = overlay;
    mMoveOverlay->SetShowing(overlay);
}

SkeletonClip *MoveDir::ImportClip(bool b1) {
    if (mImportClipPath.empty()) {
        MILO_NOTIFY("Set import_clip_path first");
        return nullptr;
    } else {
        const char *filename = FileGetName(mImportClipPath.c_str());
        SkeletonClip *clip = Find<SkeletonClip>(filename, false);
        if (clip) {
            MILO_LOG("%s already exists, not importing\n", filename);
        } else {
            clip = Hmx::Object::New<SkeletonClip>();
            clip->SetName(filename, this);
            clip->SetPath(mImportClipPath.c_str());
        }
        return clip;
    }
}

void MoveDir::StopSongRecord() {
    if (mRecordClip && mRecordClip->IsRecording()) {
        mRecordClip->StopRecording();
        if (mAlternateRecordClip)
            mAlternateRecordClip->StopRecording();
    } else {
        MILO_NOTIFY("Start recording first");
    }
}

void MoveDir::FlushMoveRecord() {
    SkeletonClip *clip = mSkeletonRecordClip;
    if (clip) {
        String clipName = RecordClipName("ktb", -1);
        clip->FlushMoveRecord(clipName.c_str());
    } else {
        MILO_NOTIFY("skeleton recording not yet active");
    }
}

void MoveDir::SwapMoveRecord() {
    SkeletonClip *clip = mSkeletonRecordClip;
    if (clip) {
        clip->SwapMoveRecord();
    } else {
        MILO_NOTIFY("skeleton recording not yet active");
    }
}

HamMove *MoveDir::GetMoveAtMeasure(int player, int i2) {
    static Symbol move("move");
    HamPlayerData *hpd = TheGameData->Player(player);
    Keys<Symbol, Symbol> *keys =
        TheHamDirector->GetPropKeys(hpd->GetDifficulty(), move)->AsSymbolKeys();
    return Find<HamMove>((*keys)[i2].value.Str(), false);
    return nullptr;
}

DancerSequence *MoveDir::PerformanceSequence(Difficulty diff) {
    MILO_ASSERT((0) <= (diff) && (diff) < (kNumDifficulties), 0x207);
    Symbol diffSym = DifficultyToSym(diff);
    const char *seqName = MakeString("performance_%s.seq", diffSym);
    return Find<DancerSequence>(seqName, false);
}

void SetupRecordClip(
    ObjPtr<SkeletonClip> &clip, int i1, int i2, const char *cc, ObjectDir *dir
) {
    clip = Hmx::Object::New<SkeletonClip>();
    clip->EnableAlternateRecord(i1);
    clip->SetRecordClipIndexHint(i2);
    String clipName = RecordClipName(cc, i1);
    clipName += ".clp";
    clip->SetName(clipName.c_str(), dir);
    const char *path = MakeString("devkit:\\%s", clip->Name());
    MILO_LOG("Starting song recording: %s\n", path);
    clip->StartXboxRecording(path);
}

void MoveDir::FinishGameRecord() {
    MILO_ASSERT(!TheLoadMgr.EditMode(), 0x604);
    if (mRecordClip) {
        MILO_LOG("Finishing song recording: %s\n", mRecordClip->Path());
        mRecordClip->StopRecording();
        RELEASE(mRecordClip);
    }
    if (mAlternateRecordClip) {
        MILO_LOG("Finishing song recording: %s\n", mAlternateRecordClip->Path());
        mAlternateRecordClip->StopRecording();
        RELEASE(mAlternateRecordClip);
    }
    RELEASE(mSkeletonRecordClip);
}

void MoveDir::SetupSongRecordClip() {
    static Symbol rhythm_battle("rhythm_battle");
    bool b1 = mGamePanel && mGamePanel->Type() == rhythm_battle;
    bool b7 = false;
    if (mGamePanel) {
        static Message msg("is_game_over");
        b7 = mGamePanel->Handle(msg, true).Int();
    }
    if (!b7) {
        const char *modeStr;
        if (b1) {
            modeStr = "ktb";
        } else if (TheHamDirector->InPracticeMode()) {
            modeStr = "bid";
        } else
            modeStr = "pi";
        if (sGameRecord && !mRecordClip) {
            unsigned int x = sGameRecord2Player;
            if (x) {
                SetupRecordClip(mRecordClip, 0, 0, modeStr, this);
                SetupRecordClip(mAlternateRecordClip, 1, 1, modeStr, this);
            } else {
                SetupRecordClip(mRecordClip, x, -1, modeStr, this);
            }
        }
        if (b1 && !mSkeletonRecordClip) {
            SetupRecordClip(mSkeletonRecordClip, 2, 0, modeStr, this);
        }
    }
}

void MoveDir::SetDancerSequence(DancerSequence *seq) { mDancerSeq = seq; }

void MoveDir::LoadScoring(const DataArray *cfg) {
    static Symbol min_frame_dist_beats("min_frame_dist_beats");
    cfg->FindData(min_frame_dist_beats, HamMove::sMinFrameDistBeats);
    static Symbol latency_offset("latency_offset");
    cfg->FindData(latency_offset, sLatencySeconds);
    sLatencySeconds /= 1000;
    static Symbol plf_min_time_error("plf_min_time_error");
    sPLFMinTimeError = cfg->FindFloat(plf_min_time_error);
    ScoreUtlInit(cfg);
    DeleteAll(sFilterVersions);
    DataArray *versionsArr = cfg->FindArray("versions");
    for (int i = 1; i < versionsArr->Size(); i++) {
        sFilterVersions.push_back(FilterVersion::Create(versionsArr->Array(i)));
    }
    MILO_ASSERT(!sFilterVersions.empty(), 0x2E2);
}

void MoveDir::FinalPoseStateMachine() {
    float songBeat = (float)(TheTaskMgr.CurrentMeasure() * 4);
    float beatInMeasure = TheTaskMgr.TotalBeat() - songBeat;
    for (int i = 0; i < 2; i++) {
        int other_player = 1 - i;
        MovePlayerData &mpd = mMovePlayerData[i];
        HamMove *move = mpd.mCurMove;
        HamPlayerData *playerData = TheGameData->Player(i);
        if (playerData->IsPlaying() && !InGracePeriod(i) && move
            && move->IsFinalPose()) {
            const FilterVersion *fv = move->FilterVer();
            if (move->IsFinalPose() && mpd.mFeedbackMode != 2) {
                float frac;
                if (TheMoveMgr->HasRoutine()) {
                    frac = mAsyncDetector->MoveRatingFrac(
                        i, (MoveAsyncDetector::RatingBar)0, move
                    );
                } else {
                    frac = DetectFrac(i, -1);
                }
                const std::vector<MoveFrame> &moveFrames =
                    ((const HamMove *)move)->GetMoveFrames();
                if (moveFrames.begin() != moveFrames.end()) {
                    float lastFrameBeat = (moveFrames.end() - 1)->GetBeat();
                    if (mpd.mFeedbackMode == 0 && lastFrameBeat <= beatInMeasure) {
                        MILO_ASSERT(
                            (0) <= (other_player) && (other_player) < (2), 0x4ce
                        );
                        if (mMovePlayerData[other_player].mFeedbackMode == 0) {
                            static Message msg("final_pose_photo");
                            TheHamProvider->Export(msg, true);
                        }
                        mpd.mFeedbackMode = 1;
                    }
                    if (mpd.mFeedbackMode == 1) {
                        float measureBeat =
                            (float)(TheTaskMgr.CurrentMeasure() * 4);
                        float lastFrameSeconds =
                            BeatToSeconds(lastFrameBeat + measureBeat);
                        float errorDist = ScaleFullErrorDist(fv->mScaleOp);
                        float detectEndSeconds =
                            errorDist + sLatencySeconds + lastFrameSeconds;
                        float detectEndBeat = SecondsToBeat(detectEndSeconds);
                        if ((float)(detectEndBeat - measureBeat) >= 4.0f) {
                            MILO_NOTIFY_ONCE(
                                "%s last frame is too late, end pose won't be "
                                "scored correctly",
                                PathName(move)
                            );
                        }
                        if (detectEndSeconds <= unk30c
                            || beatInMeasure
                                   >= (float)(4.0f
                                              - HamMove::sMinFrameDistBeats)) {
                            static Symbol final_pose_rating(
                                "final_pose_rating"
                            );
                            const std::vector<float> *ratings =
                                move->RatingOverride();
                            DataNode ratingNode(
                                DetectFracToRating(frac, ratings, nullptr)
                            );
                            HamPlayerData *pd =
                                TheGameData->Player(i);
                            pd->Provider()->SetProperty(
                                final_pose_rating, ratingNode
                            );
                            mpd.mFeedbackMode = 2;
                        }
                    }
                }
            }
        }
    }
}

void MoveDir::ReloadScoring() {
    MILO_ASSERT(TheLoadMgr.EditMode(), 0x1268);
    DataArray *cfg = SystemConfig("scoring");
    DataArray *file = DataReadFile(cfg->Array(1)->File(), true);
    LoadScoring(file);
    ScoreUtlInit(file);
    Enter();
    file->Release();
}

void MoveDir::ResetDetection() {
    if (TheHamDirector) {
        if (SkeletonUpdate::HasInstance()) {
            SkeletonUpdateHandle handle = SkeletonUpdate::InstanceHandle();
            if (!handle.HasCallback(this)) {
                handle.AddCallback(this);
            }
        }
        MILO_ASSERT(TheGameData, 0x642);
        for (int i = 0; i < 2; i++) {
            HamPlayerData *player_data = TheGameData->Player(i);
            MILO_ASSERT(player_data, 0x646);
            if (player_data->IsPlaying()) {
                ResetDetectFrames(i, player_data->GetDifficulty());
            }
        }
        SetupSongRecordClip();
    }
}

void MoveDir::ResetDetectFrames(int player, Difficulty diff) {
    MILO_ASSERT((0) <= (player) && (player) < (2), 0x678);
    MILO_ASSERT((0) <= (diff) && (diff) < (kNumDifficulties), 0x679);
    MILO_ASSERT(TheHamDirector, 0x67a);
    SetupSongRecordClip();
    if (mFilterQueue) {
        mFilterQueue->CancelJob();
    }
    mDebugLoopMarker = -1.0f;
    mMovePlayerData[player].mFeedbackMode = 0;
    if (mMovePlayerData[player].mDetectFrames.begin() != mMovePlayerData[player].mDetectFrames.end()) {
        mMovePlayerData[player].mDetectFrames.erase(mMovePlayerData[player].mDetectFrames.begin(), mMovePlayerData[player].mDetectFrames.end());
    }
    if (diff != kDifficultyBeginner) {
        DancerSequence *seq;
        if (TheHamDirector->InPracticeMode()) {
            seq = SkillsSequence(
                diff, TheHamDirector->mPracticeStart, TheHamDirector->mPracticeEnd
            );
        } else {
            seq = PerformanceSequence(diff);
        }
        if (!seq) {
            const char *mode;
            if (TheHamDirector->InPracticeMode()) {
                mode = "skills";
            } else {
                mode = "perform";
            }
            MILO_NOTIFY(
                "%s: could not find %s DancerSequence (%s)",
                PathName(this), DifficultyToSym(diff), mode
            );
        } else {
            const std::vector<DancerFrame> &dancerFrames = seq->GetDancerFrames();
            const DancerFrame *dfIt = &*dancerFrames.begin();
            if (dfIt == &*dancerFrames.end()) {
                TheDebug << MakeString(
                    "%s %s: could not reset detect frames, no DancerFrames\n",
                    PathName(this), DifficultyToSym(diff)
                );
            } else {
                int prevCapacity = mMovePlayerData[player].mMoveKeys.capacity();
                TheHamDirector->MoveKeys(diff, this, mMovePlayerData[player].mMoveKeys);
                unsigned int newSize = mMovePlayerData[player].mMoveKeys.size();
                if (newSize > prevCapacity) {
                    MILO_NOTIFY(
                        "%s move keys size (%i) above capacity (%i)",
                        PathName(this), newSize, prevCapacity
                    );
                }
                unsigned int detectCapacity = mMovePlayerData[player].mDetectFrames.capacity();
                for (int moveKeyIdx = 0;
                     moveKeyIdx < (int)mMovePlayerData[player].mMoveKeys.size();
                     moveKeyIdx++) {
                    if (dfIt->mMoveIdx == moveKeyIdx) {
                        HamMove *curMove = mMovePlayerData[player].mMoveKeys[moveKeyIdx].move;
                        const std::vector<MoveFrame> &moveFrames =
                            ((const HamMove *)curMove)->GetMoveFrames();
                        MoveMirrored mirrored = curMove->Mirrored();
                        unsigned int numMoveFrames = (unsigned int)moveFrames.size();
                        if (numMoveFrames != 0) {
                            for (unsigned int j = 0;
                                 j < (unsigned int)moveFrames.size();
                                 j++) {
                                if (dfIt->mMoveFrameIdx == (int)j) {
                                    DetectFrame df;
                                    float secs =
                                        moveFrames[j].QuantizedSeconds(
                                            mMovePlayerData[player].mMoveKeys[moveKeyIdx].beat
                                        );
                                    df.Reset(
                                        mFilterVer, secs, &moveFrames[j],
                                        dfIt, mirrored
                                    );
                                    mMovePlayerData[player].mDetectFrames.push_back(df);
                                    dfIt++;
                                    if (dfIt == &*dancerFrames.end()) {
                                        unsigned int detectSize =
                                            mMovePlayerData[player].mDetectFrames.size();
                                        if (detectSize > detectCapacity) {
                                            MILO_NOTIFY(
                                                "%s detect frames size (%i) "
                                                "above capacity (%i)",
                                                PathName(this), detectSize,
                                                detectCapacity
                                            );
                                        }
                                        return;
                                    }
                                } else {
                                    TheDebug << MakeString(
                                        "%s %s: invalid DancerFrame at move "
                                        "%i frame %i\n",
                                        PathName(this),
                                        DifficultyToSym(diff), moveKeyIdx,
                                        dfIt->mMoveFrameIdx
                                    );
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

void MoveDir::SetSongPlayClip(SkeletonClip *clip) {
    if (!mFiltersEnabled && clip) {
        mFiltersEnabled = true;
        if (TheLoadMgr.EditMode()) {
            MiloInit();
        }
    }
    if (mRecordClip && mRecordClip->IsRecording()) {
        MILO_NOTIFY("Can't set play clip while recording");
    } else {
        mPlayClip = clip;
        SetSkeletonClip(clip);
        ResetDetection();
        TheGameData->UnassignSkeletons();
    }
}

void MoveDir::MiloUpdate() {
    SkeletonDir::MiloUpdate();
    MILO_ASSERT(TheGestureMgr, 0xB14);
    SetCurrentMove(0, mMovePlayerData[0].mCurMove);
    SetMoveOverlay(mShowMoveOverlay);
    SetSongPlayClip(mPlayClip);
}

DataNode MoveDir::OnStreamJump(const DataArray *) {
    if (mDebugLoop) {
        ResetDetection();
        mDebugLoopMarker = -1;
    }
    return 0;
}

void MoveDir::OnBeat() {
    if (TheMaster && (int)TheMaster->TotalBeat2() % 4 == 3
        && (int)TheMaster->TotalBeat1() % 4 == 0) {
        for (int i = 0; i < 2; i++) {
            mCurMoveSmoothers[i].Reset();
        }
    }
}

void MoveDir::SetDebugLoop(bool loop) { mDebugLoop = loop; }

PracticeSection *MoveDir::GetPracticeSection(Difficulty d) {
    for (ObjDirItr<PracticeSection> it(this, true); it != nullptr; ++it) {
        if (it->GetDifficulty() == d) {
            return it;
        }
    }
    return nullptr;
}

DancerSequence *MoveDir::SkillsSequence(Difficulty d, Symbol s1, Symbol s2) {
    PracticeSection *section = GetPracticeSection(d);
    if (section) {
        return section->SequenceForDetection(s1, s2);
    } else {
        return nullptr;
    }
}

void MoveDir::SetCurrentMove(int player, HamMove *move) {
    MILO_ASSERT_RANGE(player, 0, 2, 0x563);
    MovePlayerData &mpd = mMovePlayerData[player];
    HamPhraseMeter *hpm = mpd.mPhraseMeter;
    if (hpm) {
        hpm->SetRatingFrac(0, -1);
        if (move && move->Scored() && TheGameData->Player(player)->IsPlaying()
            && !InGracePeriod(player)) {
            hpm->SetShowing(true);
        } else {
            hpm->SetShowing(false);
        }
    }
    if (mpd.mTextFeedback) {
        mpd.mTextFeedback->SetShowing(0 == mpd.mFeedbackMode);
    }
    mpd.mCurMove = move;
    if (move) {
        float f8 = TheTaskMgr.TotalBeat() - TheTaskMgr.CurrentMeasure() * 4;
        float f9 = BeatToSeconds(f8);
        f9 = (BeatToSeconds(f8 + 4.0f) - f9) * 1000.0f;
        if (TheMaster && TheMaster->GetAudio()
            && TheMaster->GetAudio()->GetSongStream()) {
            f9 = f9 / TheMaster->GetAudio()->GetSongStream()->GetSpeed();
        }
        if (move->SuppressGuideGesture()) {
            XNuiDelayUI((int)f9);
        }
        if (move->SuppressPracticeOptions()) {
            static Message suppressMsg("begin_suppress_practice_options", 0);
            suppressMsg[0] = f9 / 1000.0f;
            TheHamProvider->Handle(suppressMsg, false);
        }
    }
    mMoveOverlay->SetCallback(this);
}

float MoveDir::SongSeconds() {
    float seconds = TheTaskMgr.Seconds(TaskMgr::kRealTime);
    if (TheMaster) {
        HamAudio *audio = TheMaster->GetAudio();
        if ((int)audio) {
            Stream *stream = audio->GetSongStream();
            if (stream) {
                stream = TheMaster->GetAudio()->GetSongStream();
                seconds += stream->GetJumpBackTotalTime(seconds) * 0.001f;
            }
        }
    }
    return seconds;
}

float MoveDir::SongSpeed() const {
    if (TheMaster) {
        return TheMaster->GetAudio()->GetSongStream()->GetSpeed();
    } else {
        return 1;
    }
}

float MoveDir::DetectRangePSNR(
    const std::pair<const DetectFrame *, const DetectFrame *> &detectFrames,
    const FilterVersion *fv
) const {
    MILO_ASSERT(fv->mType == kFilterVersionHam2, 0x1E8);
    float ret = 0;
    MoveMode moveMode = CurrentMoveMode();
    for (const DetectFrame *it = detectFrames.first; it != detectFrames.second; ++it) {
        const Ham2FrameWeight &wt = it->GetMoveFrame()->FrameWeight(it->Mirror());
        float cmp = wt.mWeight;
        if (cmp > 0 && it->HasScore()) {
            ret += it->Score(fv, moveMode) * cmp;
        }
    }
    return ret;
}

float MoveDir::DetectRangeFrac(
    const std::pair<DetectFrame *, DetectFrame *> &detectFrames, const FilterVersion *fv
) const {
    MILO_ASSERT(fv->mType == kFilterVersionHam1, 0x1D5);
    int idx = 0;
    float ret = 0;
    MoveMode moveMode = CurrentMoveMode();
    for (DetectFrame *it = detectFrames.first; it != detectFrames.second; ++it, ++idx) {
        ret += it->Score(fv, moveMode);
    }
    if (idx > 0) {
        return Clamp(0.0f, 1.0f, ret / (float)idx);
    } else {
        return 0;
    }
}

bool MoveDir::InGracePeriod(int player) {
    Hmx::Object *provider = TheGameData->Player(player)->Provider();
    if (!provider)
        return false;
    static Symbol start_score_move_index("start_score_move_index");
    const DataNode *prop = provider->Property(start_score_move_index, false);
    if (!prop)
        return false;
    return TheTaskMgr.CurrentMeasure() < prop->Int();
}

MoveFrame *MoveDir::ClosestMoveFrame() {
    struct FilterFrameDist {
        FilterFrameDist(float dist) : mDist(dist) {}
        bool operator()(const MoveFrame &frame1, const MoveFrame &frame2) const {
            return fabsf(frame1.Beat() - mDist) < fabsf(frame2.Beat() - mDist);
        }

        float mDist; // 0x0
    };
    HamMove *move = mMovePlayerData[0].mCurMove;
    if (!move)
        return nullptr;

    int measure = TheTaskMgr.CurrentMeasure();
    float beat = TheTaskMgr.TotalBeat();
    int measureBeats = measure * 4;
    std::vector<MoveFrame> &frames = move->GetMoveFrames();
    MoveFrame *ret = std::min_element(
        frames.begin(), frames.end(), FilterFrameDist(beat - (float)measureBeats)
    );
    return ret != frames.end() ? ret : nullptr;
}

float MoveDir::DetectFrac(
    int player,
    const HamMove *move,
    const std::pair<DetectFrame *, DetectFrame *> &detectFrames
) {
    MILO_ASSERT_RANGE(player, 0, 2, 0x187);
    MILO_ASSERT(TheGameData, 0x188);
    MILO_ASSERT(move, 0x189);
    const FilterVersion *fv = move->FilterVer();
    float frac;
    if (fv->mType == kFilterVersionHam2) {
        frac = move->PSNRToDetectFrac(DetectRangePSNR(detectFrames, fv));
    } else {
        frac = DetectRangeFrac(detectFrames, fv);
    }
    Symbol autoplay = TheGameData->Player(player)->Autoplay();
    if (!autoplay.Null()) {
        static Symbol maximum("maximum");
        if (autoplay == maximum) {
            frac = 1;
        } else {
            frac = RatingToDetectFrac(autoplay, move->RatingOverride());
        }
        int i8 = 0;
        int i7 = 0;
        for (DetectFrame *it = detectFrames.first; it != detectFrames.second; ++it) {
            const Ham2FrameWeight &wt = it->GetMoveFrame()->FrameWeight(it->Mirror());
            if (wt.mWeight != 0) {
                i8++;
                if (it->HasScore()) {
                    i7++;
                }
            }
        }
        if (i8 != 0) {
            frac = i7 / (i8 * frac);
        }
    }
    return frac;
}

void MoveDir::DetectRange(
    std::vector<DetectFrame> &frames,
    std::pair<DetectFrame *, DetectFrame *> &range,
    int low,
    int high
) {
    range.first =
        std::lower_bound(frames.begin(), frames.end(), low, DetectFrameMoveIdxCmp());
    range.second =
        std::upper_bound(frames.begin(), frames.end(), high, DetectFrameMoveIdxCmp());
}

void MoveDir::DrawShowing() {
    if (HashTable().Begin() != nullptr) {
        if (mDebugCollision) {
            SongCollision *songCol = Find<SongCollision>("SongCollision", false);
            if (songCol) {
                float beat = TheTaskMgr.Beat();
                int intBeat = (int)beat;

                if ((unsigned int)unkf84 != intBeat) {
                    unkf84 = intBeat;
                    MILO_ASSERT(TheHamDirector, 0xab1);
                    for (int i = 0; i < 2; i++) {
                        HamCharacter *ch = TheHamDirector->GetCharacter(i);
                        if (ch) {
                            const Transform &xfm = ch->WorldXfm();
                            memcpy(&unkf04[i], &xfm, sizeof(Transform));
                        }
                    }
                }

                Difficulty diffs[2];
                for (int i = 0; i < 2; i++) {
                    diffs[i] = TheGameData->Player(i)->GetDifficulty();
                }

                std::vector<SongCollisionOutput> outputs;
                songCol->IsCollision(intBeat, intBeat + 1, diffs, unkf04, &outputs);

                float gray = 0.8f;
                float radius2 = 2.0f;
                float zero = 0.0f;
                float radius1 = 1.0f;

                unsigned int beatIdx = 0;
                size_t outputSize = outputs.size();

                if (outputSize > 0) {
                    for (size_t i = 0; i < outputSize; i++) {
                        const SongCollisionOutput &out = outputs[i];

                        Hmx::Color color;
                        if (out.Colliding()) {
                            color.Set(gray, gray, gray, 1.0f);
                        } else {
                            color.Set(1.0f, zero, 1.0f, 1.0f);
                        }

                        int playerIdx = 0;
                        int labeledBeat = beatIdx + intBeat;

                        for (playerIdx = 0; playerIdx < 2; playerIdx++) {
                            const Vector3 &pos = out.WorldPos(playerIdx);
                            UtilDrawSphere(pos, radius1, color, nullptr);

                            const char *label = MakeString("%i:%i", playerIdx, labeledBeat);
                            UtilDrawString(label, pos, color);

                            TheRnd.DrawLine(pos, out.Offset(playerIdx), color, false);
                            UtilDrawSphere(out.Offset(playerIdx), radius1, color, nullptr);

                            TheRnd.DrawLine(pos, out.Offset(playerIdx + 2), color, false);
                            UtilDrawSphere(out.Offset(playerIdx + 2), radius1, color, nullptr);
                        }

                        for (playerIdx = 0; playerIdx < 2; playerIdx++) {
                            const Vector3 &worldPos = out.WorldPos(playerIdx);
                            Vector3 offsetPos = out.Offset(playerIdx + 4);
                            offsetPos += worldPos;
                            Hmx::Color altColor;
                            altColor.Set(zero, 1.0f, zero, 1.0f);
                            TheRnd.DrawLine(worldPos, offsetPos, altColor, false);
                            UtilDrawSphere(offsetPos, radius2, altColor, nullptr);

                            const char *label = MakeString("%i", playerIdx);
                            UtilDrawString(label, offsetPos, altColor);
                        }

                        beatIdx++;
                    }
                }
            }
        }
    } else if (TheLoadMgr.EditMode()) {
        if (mDancerSeq) {
            ObjDirItr<SkeletonViz> it(this, true);
            if (it) {
                StubCameraInput camInput;
                camInput.PollTracking();
                const DancerSkeleton *skeleton = mDancerSeq->CurSkeleton();
                if (skeleton) {
                    it->Visualize(camInput, *skeleton, nullptr, false);
                }
            }
        } else {
            SkeletonDir::DrawShowing();
        }
    }
}

namespace {
    // Global data for beat line rendering - exact layout from assembly
    struct BeatLineData {
        float minValue;
        float maxValue;
        float rangeOffset;
        float rangeScale;
    };

    extern BeatLineData gBeatLineData = { 0.0f, 1.0f, 0.0f, 1.0f };
    extern float gFourPointZero = 4.0f;

    void DrawBeatLine(float x, float y, float z, const Hmx::Color& color) {
        float sum = x + y;
        float numerator = gBeatLineData.rangeOffset + z;
        float denominator = gBeatLineData.rangeScale + gBeatLineData.rangeOffset + gFourPointZero;
        float t = numerator / denominator;
        float linePos = t * (gBeatLineData.maxValue - gBeatLineData.minValue) + gBeatLineData.minValue;

        Vector2 endPos(linePos, sum);
        Vector2 startPos(linePos, x);

        UtilDrawLine(startPos, endPos, color);
    }

    static float sLineHeight = 0.0f;

    struct DetectFrameSecondsCmp {
        bool operator()(const DetectFrame &a, float b) const {
            return a.Seconds() < b;
        }
        bool operator()(float a, const DetectFrame &b) const {
            return a < b.Seconds();
        }
    };
}

float MoveDir::DetectFrac(int player, int idx) {
    MILO_ASSERT_RANGE(player, 0, 2, 0x16a);
    int moveIdx = MoveIdx();
    if (idx == -1) {
        idx = moveIdx;
    }
    MovePlayerData &playerData = mMovePlayerData[player];
    unsigned int numMoveKeys = (unsigned int)playerData.mMoveKeys.size();
    HamMove *move;
    if (idx < 0 || (unsigned int)idx >= numMoveKeys
        || !(move = playerData.mMoveKeys[idx].move))
        return 0.0f;
    std::pair<DetectFrame *, DetectFrame *> range;
    DetectRange(playerData.mDetectFrames, range, idx, idx);
    if (range.first == range.second) {
        return mAsyncDetector->MoveRatingFrac(player, (MoveAsyncDetector::RatingBar)(idx != moveIdx), move);
    }
    return DetectFrac(player, move, range);
}

float MoveDir::UpdateOverlay(RndOverlay *overlay, float y) {
    if (!mFiltersEnabled || !mMovePlayerData[0].mCurMove)
        return y;

    HamMove *move = mMovePlayerData[0].mCurMove;
    const FilterVersion *filterVer = move->FilterVer();
    SkeletonUpdateHandle handle = SkeletonUpdate::InstanceHandle();
    int numNodes = filterVer->NumNodes();

    MILO_ASSERT(TheGestureMgr, 0x795);
    MILO_ASSERT(mSkeletonViz, 0x796);
    MILO_ASSERT(TheHamDirector, 0x797);

    MoveMode moveMode = CurrentMoveMode();
    MoveMirrored mirrored = move->Mirrored();

    const Hmx::Color &textColor = sLightGray;

    // Compute line height from "W" text if not yet calculated
    if (sLineHeight == 0.0f) {
        Vector2 wPos(gBeatLineData.minValue, y);
        const Vector2 &wResult = TheRnd.DrawStringScreen("W", wPos, sLightGray, false);
        sLineHeight = (wResult.y - y) * 0.8f;
    }

    // Draw play clip info if present
    if (mPlayClip) {
        y = DrawPlayClip(y, mPlayClip, 0);
    }

    // Draw filter version name
    {
        Vector2 namePos(gBeatLineData.minValue - 0.05f, y);
        TheRnd.DrawStringScreen(MakeString("%s", filterVer->mVersionSym), namePos, sLightGray, true);
    }

    // Draw rating state thresholds
    float endX = 0.99f;
    for (int i = 0; i < 4; i++) {
        Symbol ratingName = gNullStr;
        float thresh;
        RatingStateThreshold(i, ratingName, thresh, move->RatingOverride());

        // Draw threshold line
        float threshLineY = y;
        float threshEndY = sLineHeight + y;
        float startScreenX = (gBeatLineData.maxValue - gBeatLineData.minValue) *
            (gBeatLineData.rangeOffset / (gBeatLineData.rangeScale + gBeatLineData.rangeOffset + gFourPointZero)) +
            gBeatLineData.minValue;
        float threshX = (endX - startScreenX) * thresh + startScreenX;
        Vector2 lineStart(threshX, threshLineY);
        Vector2 lineEnd(threshX, threshEndY);
        UtilDrawLine(lineStart, lineEnd, sGray);

        // Draw rating name if it has a separator
        const char *sep = strstr(ratingName.Str(), "/");
        if (sep) {
            Vector2 textPos(threshX, y);
            TheRnd.DrawStringScreen(sep + 1, textPos, sLightGray, true);
        }
    }

    // Compute screen coordinates for the overlay area
    float barY = sLineHeight + y;
    float startScreenX = (gBeatLineData.maxValue - gBeatLineData.minValue) *
        (gBeatLineData.rangeOffset / (gBeatLineData.rangeScale + gBeatLineData.rangeOffset + gFourPointZero)) +
        gBeatLineData.minValue;
    float barWidth = endX - startScreenX;

    // Check if rest; get detect fraction
    bool isRest = move->IsRest();
    float detectFrac = 0.0f;
    if (!isRest) {
        detectFrac = DetectFrac(0, -1);
    }

    // Build move name with measure info
    const char *mirrorStr = gNullStr;
    if (mirrored == kMirroredYes) {
        mirrorStr = " (mirrored)";
    }

    int measureIdx = MoveIdx();
    const char *moveName = MakeString("%i %s %s", measureIdx,
        move->Name(), mirrorStr);

    // Draw detected bar
    float detectedBarY = DrawDetectedBar(barY, moveName, detectFrac, startScreenX, endX, false, false);

    // Draw timer bar
    DrawOverlayBar(detectedBarY, startScreenX, endX, textColor, sLineHeight);
    DrawOverlayBar(detectedBarY, startScreenX,
        mLastPollMs * 0.0625f * barWidth + startScreenX, textColor, sLineHeight);

    // Draw timer text
    {
        Vector2 timerPos(startScreenX, detectedBarY);
        TheRnd.DrawStringScreen(MakeString("timer: %.3fms\n", mLastPollMs), timerPos, sLightGray, true);
    }

    // Compute main overlay area
    float lineH = sLineHeight;
    float overlayY = sLineHeight * 2.0f + detectedBarY;
    float overlayX = overlayY;

    // Compute overlay height based on filter type
    if (filterVer->mType == kFilterVersionHam1) {
        lineH = ((float)(numNodes + 1)) * lineH;
    } else if (filterVer->mType == kFilterVersionHam2) {
        lineH = lineH * 2.0f;
    }

    // Draw background rect
    {
        float bgBottom = (gBeatLineData.maxValue - gBeatLineData.minValue) *
            (0.0f / (gBeatLineData.rangeScale + gBeatLineData.rangeOffset + gFourPointZero)) +
            gBeatLineData.minValue;
        Hmx::Rect bgRect(0.0f, overlayY, bgBottom, lineH);
        Hmx::Color bgColor(0.3f, 0.3f, 0.3f);
        bgColor.alpha = 1.0f;
        TheRnd.DrawRectScreen(bgRect, bgColor, nullptr, nullptr, nullptr);
    }

    // Draw full range rect
    {
        float rangeEnd = (gBeatLineData.rangeScale + gBeatLineData.rangeOffset + gFourPointZero) /
            (gBeatLineData.rangeScale + gBeatLineData.rangeOffset + gFourPointZero);
        float rangeWidth = (gBeatLineData.maxValue - gBeatLineData.minValue) * rangeEnd +
            gBeatLineData.minValue;
        float bgBottom2 = (gBeatLineData.maxValue - gBeatLineData.minValue) *
            (0.0f / (gBeatLineData.rangeScale + gBeatLineData.rangeOffset + gFourPointZero)) +
            gBeatLineData.minValue;
        Hmx::Rect rangeRect(bgBottom2, overlayX, rangeWidth - bgBottom2, lineH);
        TheRnd.DrawRectScreen(rangeRect, sDarkGray, nullptr, nullptr, nullptr);
    }

    // Draw node labels (for ham1)
    if (filterVer->mType == kFilterVersionHam1) {
        float labelY = sLineHeight + overlayX;
        float labelX = gBeatLineData.minValue;
        for (int n = 0; n < numNodes; n++) {
            ErrorNode *node = filterVer->mErrorNodes[n];
            Vector2 labelPos(labelX, labelY);
            const Vector2 &result = TheRnd.DrawStringScreen(node->NodeName().Str(), labelPos, sLightGray, false);
            // Draw again right-aligned
            Vector2 adjPos(labelX - (result.x - labelX), labelY);
            TheRnd.DrawStringScreen(node->NodeName().Str(), adjPos, sLightGray, true);
            labelY += sLineHeight;
        }
    }

    // Draw beat number markers
    for (int b = 0; b < 5; b++) {
        float beatScreenX = (gBeatLineData.maxValue - gBeatLineData.minValue) *
            (((float)b + gBeatLineData.rangeOffset) /
             (gBeatLineData.rangeScale + gBeatLineData.rangeOffset + gFourPointZero)) +
            gBeatLineData.minValue;
        Vector2 beatPos(beatScreenX, overlayX);
        TheRnd.DrawStringScreen(MakeString("%i", b), beatPos, sGray, true);
        DrawBeatLine(overlayX, lineH, (float)b, sGray);
    }

    // Get current song position
    float songSeconds = SongSeconds();
    float songBeat = SecondsToBeat(songSeconds);
    int measureBeats = measureIdx * 4;
    float beatInMeasure = songBeat - (float)measureBeats;

    // Get half line width for rendering
    float halfLineWidth = TheRnd.YRatio() * sLineHeight * 0.5f;

    // Get closest move frame
    MoveFrame *closestFrame = ClosestMoveFrame();

    // Draw move frames
    const std::vector<MoveFrame> &moveFrames = ((const HamMove *)move)->GetMoveFrames();
    int numFrames = (int)moveFrames.size();
    for (int f = 0; f < numFrames; f++) {
        const MoveFrame &frame = moveFrames[f];
        const Ham2FrameWeight &fw = frame.FrameWeight(mirrored);
        if (fw.mWeight == 0.0f)
            continue;

        float frameScreenX = (gBeatLineData.maxValue - gBeatLineData.minValue) *
            ((frame.GetBeat() + gBeatLineData.rangeOffset) /
             (gBeatLineData.rangeScale + gBeatLineData.rangeOffset + gFourPointZero)) +
            gBeatLineData.minValue;

        Hmx::Color frameColor;
        if (&frame == closestFrame) {
            frameColor = Hmx::Color(0.8f, 0.8f, 0.0f, 1.0f);
        } else {
            frameColor = Hmx::Color(0.8f, 0.8f, 0.8f, 1.0f);
        }

        // Draw beat line for this frame
        DrawBeatLine(overlayX, lineH, frame.GetBeat(), frameColor);

        // Draw beat label
        Vector2 labelPos(frameScreenX, overlayX - sLineHeight);
        TheRnd.DrawStringScreen(MakeString("%.2f", frame.GetBeat()), labelPos, frameColor, true);

        // Draw per-node weight rects (for ham1)
        if (filterVer->mType == kFilterVersionHam1) {
            float nodeY = sLineHeight + overlayX;
            float nodeLeft = frameScreenX - halfLineWidth;
            for (int n = 0; n < numNodes; n++) {
                float nodeRight = frameScreenX + halfLineWidth;
                Vector2 rectStart(nodeLeft, nodeY);
                Vector2 rectEnd(nodeRight, nodeY + sLineHeight);
                UtilDrawRect2D(rectStart, rectEnd, frameColor);
                nodeY += sLineHeight;
            }
        }
    }

    // Get detect frame range for current measure
    std::pair<DetectFrame *, DetectFrame *> detectRange;
    DetectRange(mMovePlayerData[0].mDetectFrames, detectRange, MoveIdx(), MoveIdx());

    // Draw per-node detect frame data (for ham1)
    if (filterVer->mType == kFilterVersionHam1 && detectRange.first != detectRange.second) {
        for (DetectFrame *df = detectRange.first; df != detectRange.second; df += 1) {
            const MoveFrame *mf = df->GetMoveFrame();
            for (int n = 0; n < numNodes; n++) {
                float nodeY = sLineHeight + overlayX;
                float mfScreenX = (gBeatLineData.maxValue - gBeatLineData.minValue) *
                    ((mf->GetBeat() + gBeatLineData.rangeOffset) /
                     (gBeatLineData.rangeScale + gBeatLineData.rangeOffset + gFourPointZero)) +
                    gBeatLineData.minValue;
                float nodeLeft = mfScreenX - halfLineWidth;
                float nodeRight = mfScreenX + halfLineWidth;
                float nodeH = sLineHeight;
                float rectY = nodeY + n * sLineHeight;

                const Ham1NodeWeight &nw = mf->NodeWeightHam1(n, moveMode, mirrored);
                if (nw.mActive) {
                    const Vector3 &error = df->BestNodeError(n);
                    float errorVal = 1.0f - error.x;
                    Hmx::Color nodeColor(errorVal * 0.0f, errorVal * -1.0f + 1.0f, 0.5f, 1.0f);
                    Hmx::Rect nodeRect(nodeLeft, rectY, nodeRight - nodeLeft, nodeH);
                    TheRnd.DrawRectScreen(nodeRect, nodeColor, nullptr, nullptr, nullptr);
                }
            }
        }
    }

    // Check merge_moves property
    static Symbol merge_moves("merge_moves");
    int mergeMoves = TheHamProvider->Property(merge_moves, true)->Int();

    const BaseSkeleton *vizSkeleton = nullptr;
    mShowErrorFrames = nullptr;

    if (mergeMoves == 0) {
        // Non-merged mode: draw detect frame error lines
        float startSeconds = BeatToSeconds((float)measureBeats - gBeatLineData.rangeOffset);
        float endSeconds = BeatToSeconds((float)(measureBeats + 4) + gBeatLineData.rangeScale);

        DetectFrame *lowerDf = std::lower_bound(
            mMovePlayerData[0].mDetectFrames.data(),
            mMovePlayerData[0].mDetectFrames.data() + mMovePlayerData[0].mDetectFrames.size(),
            startSeconds, DetectFrameSecondsCmp());

        const Hmx::Color *vizColor = &textColor;
        float errorLineY = lineH + overlayX;

        for (DetectFrame *df = lowerDf;
             df != mMovePlayerData[0].mDetectFrames.data() + mMovePlayerData[0].mDetectFrames.size();
             df++) {
            if (df->Seconds() >= endSeconds)
                break;

            // Color based on whether in detect range
            Hmx::Color dfColor;
            if (df < detectRange.first || df >= detectRange.second) {
                dfColor = Hmx::Color(0.6f, 0.6f, 0.6f, 1.0f);
            } else {
                dfColor = Hmx::Color(1.0f, 1.0f, 1.0f, 1.0f);
                if (df->GetMoveFrame() == closestFrame) {
                    mShowErrorFrames = df;
                    vizSkeleton = &df->GetDancerFrame()->mSkeleton + 1;
                }
            }

            // Draw error distance line for this detect frame
            const Ham2FrameWeight &fw = df->GetMoveFrame()->FrameWeight(mirrored);
            if (fw.mWeight != 0.0f) {
                float prevY = errorLineY;
                float prevX = (gBeatLineData.maxValue - gBeatLineData.minValue) *
                    (0.0f / (gBeatLineData.rangeScale + gBeatLineData.rangeOffset + gFourPointZero)) +
                    gBeatLineData.minValue;

                for (float t = startSeconds; t < endSeconds; t += 0.03333333f) {
                    float dist = ScaleDistToError(filterVer->mScaleOp,
                        fabsf(t - df->Seconds()));
                    float beatAtT = SecondsToBeat(t);
                    float screenX = (gBeatLineData.maxValue - gBeatLineData.minValue) *
                        (((beatAtT - (float)measureBeats) + gBeatLineData.rangeOffset) /
                         (gBeatLineData.rangeScale + gBeatLineData.rangeOffset + gFourPointZero)) +
                        gBeatLineData.minValue;

                    if (dist >= 1.0f) {
                        if (prevY < errorLineY) {
                            Vector2 p1(prevX, prevY);
                            Vector2 p2(screenX, errorLineY);
                            UtilDrawLine(p1, p2, dfColor);
                            prevY = errorLineY;
                        }
                    } else {
                        float errorY = dist * lineH + overlayX;
                        Vector2 p1(prevX, prevY);
                        Vector2 p2(screenX, errorY);
                        UtilDrawLine(p1, p2, dfColor);
                        prevY = errorY;
                    }
                    prevX = screenX;
                }
            }

            // Draw elapsed ms label
            float dfBeat = SecondsToBeat(df->Seconds());
            const BaseSkeleton *dfSkel = (const BaseSkeleton *)(&df->GetDancerFrame()->mSkeleton + 1);
            int elapsedMs = df->GetDancerFrame()->mSkeleton.ElapsedMs();
            if (elapsedMs != -1) {
                int elapsed = df->GetDancerFrame()->mSkeleton.ElapsedMs();
                float dfScreenX = (gBeatLineData.maxValue - gBeatLineData.minValue) *
                    (((dfBeat - (float)measureBeats) + gBeatLineData.rangeOffset) /
                     (gBeatLineData.rangeScale + gBeatLineData.rangeOffset + gFourPointZero)) +
                    gBeatLineData.minValue;
                Vector2 elapsedPos(dfScreenX, errorLineY);
                TheRnd.DrawStringScreen(MakeString("%i", elapsed), elapsedPos, *vizColor, true);
            }
        }
    } else {
        // Merged mode: use dancer sequence
        DancerSequence *dancerSeq = move->GetDancerSequence();
        const Hmx::Color *vizColor = &textColor;
        if (dancerSeq) {
            const std::vector<DancerFrame> &dancerFrames = dancerSeq->GetDancerFrames();
            int numDancerFrames = (int)dancerFrames.size();
            float frameIdx = (float)(numDancerFrames - 1) * beatInMeasure * 0.25f;
            if (frameIdx <= 0.0f) {
                frameIdx = frameIdx - 0.5f;
            } else {
                frameIdx = frameIdx + 0.5f;
            }
            int idx = (int)frameIdx;
            vizSkeleton = (const BaseSkeleton *)(&dancerFrames[idx].mSkeleton + 1);
            vizColor = &textColor;
        }
    }

    // Draw current beat position
    Hmx::Color beatColor(1.0f, 1.0f, 0.0f, 1.0f);
    DrawBeatLine(overlayX, lineH, beatInMeasure, beatColor);

    // Draw beat in measure label
    {
        float beatScreenX = (gBeatLineData.maxValue - gBeatLineData.minValue) *
            ((beatInMeasure + gBeatLineData.rangeOffset) /
             (gBeatLineData.rangeScale + gBeatLineData.rangeOffset + gFourPointZero)) +
            gBeatLineData.minValue;
        Vector2 beatLabelPos(beatScreenX, overlayX);
        TheRnd.DrawStringScreen(MakeString("%.2f", beatInMeasure), beatLabelPos, beatColor, true);
    }

    // Draw latency offset beat
    {
        Hmx::Color latencyColor(0.0f, 0.5f, 0.0f, 1.0f);
        float latencyBeat = SecondsToBeat(songSeconds - sLatencySeconds);
        DrawBeatLine(overlayX, lineH, latencyBeat - (float)measureBeats, latencyColor);
    }

    // Advance Y past the overlay area
    y = lineH + sLineHeight + overlayX;
    if (filterVer->mType == kFilterVersionHam2) {
        y = y + sLineHeight;
    }

    // Compute skeleton viz area
    float vizHeight = 0.2f;
    if ((1.0f - y) < vizHeight) {
        vizHeight = 1.0f - y;
    }
    float yRatio = TheRnd.YRatio();
    float vizWidth = yRatio * vizHeight;

    Hmx::Rect vizRect(gBeatLineData.minValue, y, vizWidth, vizHeight);

    TheRnd.DrawRectScreen(vizRect, sDarkGray, nullptr, nullptr, nullptr);

    // Draw skeleton visualization
    if (!closestFrame) {
        // No closest frame - draw all from set
        int numErrorFrames = (int)unkf88.size();
        Vector2 asyncPos(gBeatLineData.minValue, y);
        const Vector2 &asyncResult = TheRnd.DrawStringScreen(
            MakeString("asyc: %d", numErrorFrames), asyncPos, textColor, true);

        if (numErrorFrames != 0) {
            mSkeletonViz->SetUsePhysicalCam(true);
            float gridSize = ceilf(sqrtf((float)numErrorFrames));
            int gridCols = (int)gridSize;
            float cellH = (1.0f / gridSize) * vizHeight;
            float cellW = vizWidth * (1.0f / gridSize);

            std::set<DetectFrame *>::iterator it = unkf88.begin();
            unsigned int idx = 0;
            while (it != unkf88.end()) {
                DetectFrame *ef = *it;
                Hmx::Rect cellRect(
                    (float)(idx % gridCols) * cellW + vizRect.x,
                    (float)(idx / gridCols) * cellH + vizRect.y,
                    cellW, cellH);
                mSkeletonViz->SetPhysicalCamScreenRect(cellRect);

                StubCameraInput stubCam;
                stubCam.PollTracking();

                std::vector<SkeletonCallback *> callbacks;
                callbacks.push_back(this);

                int dfDancerFrameIdx = *((int *)ef);
                mShowErrorFrames = ef;
                const BaseSkeleton *skel = (const BaseSkeleton *)(dfDancerFrameIdx + 4);
                mSkeletonViz->Visualize(stubCam, *skel, &callbacks, false);

                if (callbacks.data()) {
                    // vector cleanup handled automatically
                }

                ++it;
                idx++;
            }
        }
    } else {
        // Closest frame exists - draw single skeleton
        Vector2 closestPos(gBeatLineData.minValue, y);
        TheRnd.DrawStringScreen(
            MakeString("%.2f", closestFrame->GetBeat()), closestPos, textColor, true);

        mSkeletonViz->SetUsePhysicalCam(true);
        mSkeletonViz->SetPhysicalCamScreenRect(vizRect);

        if (vizSkeleton) {
            StubCameraInput stubCam;
            stubCam.PollTracking();

            std::vector<SkeletonCallback *> callbacks;
            callbacks.push_back(this);

            mShowErrorFrames = (const DetectFrame *)vizSkeleton;
            mSkeletonViz->Visualize(stubCam, *vizSkeleton, &callbacks, false);
            mShowErrorFrames = nullptr;
        }
    }

    // Draw current skeleton with live camera
    {
        std::vector<SkeletonCallback *> callbacks;
        callbacks.push_back(this);

        Hmx::Rect liveRect(vizRect.x + vizWidth + 0.01f, vizRect.y, vizWidth, vizHeight);
        TheRnd.DrawRectScreen(liveRect, sDarkGray, nullptr, nullptr, nullptr);
        mSkeletonViz->SetUsePhysicalCam(true);
        mSkeletonViz->SetPhysicalCamScreenRect(liveRect);

        CameraInput *camInput = handle.GetCameraInput();
        mSkeletonViz->Visualize(*camInput, mDebugSkeleton, &callbacks, false);

        // Draw latency offset toggle
        {
            const char *offsetStr = mDebugLatencyOffset ? "ON" : "OFF";
            Vector2 offsetPos(liveRect.x, liveRect.y);
            const Vector2 &offsetResult = TheRnd.DrawStringScreen(
                MakeString("latency offset: %s", offsetStr), offsetPos, textColor, true);

            // Draw rotation
            float rotation = mSkeletonViz->PhysicalCamRotation();
            Vector2 rotPos(liveRect.x, offsetResult.y);
            TheRnd.DrawStringScreen(MakeString("rotation: %.2f", rotation), rotPos, textColor, true);
        }

        y = vizHeight + y;
    }

    return y;
}

#ifdef HX_NATIVE
void MoveDir::PostUpdateFilters() {}
#endif
