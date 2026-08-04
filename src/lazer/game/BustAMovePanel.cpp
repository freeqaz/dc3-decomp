#include "BustAMovePanel.h"
#include "flow/Flow.h"
#include "flow/PropertyEventProvider.h"
#include "game/GamePanel.h"
#include "gesture/BaseSkeleton.h"
#include "gesture/DepthBuffer3D.h"
#include "hamobj/FreestyleMoveRecorder.h"
#include "hamobj/HamDirector.h"
#include "hamobj/HamGameData.h"
#include "hamobj/HamLabel.h"
#include "hamobj/HamMaster.h"
#include "hamobj/HamPhraseMeter.h"
#include "hamobj/ScoreUtl.h"
#include "game/GameMode.h"
#include "meta_ham/AccomplishmentManager.h"
#include "meta_ham/HamPanel.h"
#include "meta_ham/ProfileMgr.h"
#include "math/Easing.h"
#include "math/Rand.h"
#include "meta_ham/MetaPerformer.h"
#include "obj/Data.h"
#include "obj/Dir.h"
#include "obj/Msg.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "os/Debug.h"
#include "os/System.h"
#include "rndobj/Anim.h"
#include "rndobj/Dir.h"
#include "rndobj/Mat.h"
#include "rndobj/Tex.h"
#include "rndobj/TexRenderer.h"
#include "ui/UIColor.h"
#include "ui/UIPanel.h"
#include "gesture/GestureMgr.h"
#include "game/Game.h"
#include "rndobj/Graph.h"
#include "utl/DebugGraph.h"
#include "utl/KnownIssues.h"
#include "utl/Symbol.h"
#include "utl/TempoMap.h"
#include "utl/TimeConversion.h"
#include <cstring>
#include <float.h>

namespace {
    // Fisher-Yates shuffle: fill vector [0..n-1], then shuffle in-place
    __declspec(noinline) void GetShuffledInts(std::vector<int> &v, int n) {
        v.clear();

        // Fill with sequential integers [0, 1, 2, ..., n-1]
        int val = 0;
        if (n > 0) {
            do {
                v.push_back(val);
                val++;
            } while (val < n);
        }

        // Shuffle: for each position i, swap with random position j in [i, n)
        // Uses pointer arithmetic pattern required for codegen (idx tracks byte offset)
        int i = 0;
        if (n - 1 > 0) {
            int idx = 0;  // Offset index (increments separately for codegen)
            do {
                int j = RandomInt(i, n);
                int *data = &v[0];  // Base pointer (reloaded each iteration)
                i++;
                int temp = data[idx];
                data[idx] = data[j];
                idx++;
                data[j] = temp;
            } while (i < n - 1);
        }
    }

}

BustAMovePanel::BustAMovePanel()
    : mRecorder(0), mBeatCount(0), mRecordSkelIdx(-1), mMoveScore(0), mHUDPanel(0), mActivePlayer(0), mCountInLength(4), mMatchCount(0),
      mPendingState(10), mRecordScore(0), mCreatorSide(1), mCaptureStep(0), mCaptureTimer(0), mCaptureFrames(0), mLoopStartBeat(-1),
      mLoopEndBeat(-1), mFailureEndBeat(-1), mIsMulligan(0), mRepsRemaining(0), mStreamJumped(0), mMoveNameCursor(0), mNextVOTime(FLT_MAX),
      mNoPosesDetected(0), mDepthBufPlayer(-1) {
    mRecorder = new FreestyleMoveRecorder();
    mRecorder->AssignStaticInstance();
}

BustAMovePanel::~BustAMovePanel() { delete mRecorder; }

BEGIN_HANDLERS(BustAMovePanel)
    HANDLE_ACTION(beat, OnBeat())
    HANDLE_ACTION(cache_objects, CacheObjects())
    HANDLE_ACTION(set_up_song_structure, SetUpSongStructure(_msg->Sym(2)))
    HANDLE_ACTION(on_stream_jump, mStreamJumped = true)
    HANDLE_ACTION(play_intro_vo, PlayIntroVO())
    HANDLE_SUPERCLASS(HamPanel)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(BustAMovePanel)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

void BustAMovePanel::Draw() {
    UIPanel::Draw();
    mRecorder->DrawDebug();
    if (mCaptureFrames) {
        RndDir *renderer = DataDir()->Find<RndDir>("bustamove_flashcard_renderer");
        String flashcard(MakeString("flashcard%i.tex", mMoveIndex));
        RndTexRenderer *texRenderer =
            renderer->Find<RndTexRenderer>("TexRenderer.rndtex");
        int numPoses = 0;
        for (int i = 0; i < 3; i++) {
            if (mCapturePoses[i].Tracked()) {
                numPoses++;
                MILO_ASSERT(numPoses <= 2, 0x1BC);
                String pose(MakeString("pose%i.tex", numPoses));
                RndDir *renderer =
                    DataDir()->Find<RndDir>("bustamove_flashcard_renderer");
                RndTex *tex = renderer->Find<RndTex>(pose.c_str());
                TheHamDirector->PoseIconMan(&mCapturePoses[i], tex);
            }
        }
        if (numPoses == 0) {
            mNoPosesDetected = true;
        }
        RndAnimatable *anim = renderer->Find<RndAnimatable>("num_poses.anim");
        anim->SetFrame(numPoses, 1);
        texRenderer->SetOutputTexture(DataDir()->Find<RndTex>(flashcard.c_str()));
        renderer->DrawShowing();
        mCaptureFrames--;
    }
}

void BustAMovePanel::Enter() {
    HamPanel::Enter();
    mHUDPanel = 0;
    if (InBustAMove()) {
        CacheObjects();
        mPlayIntroVO = true;
    }
}

void BustAMovePanel::Exit() {
    UIPanel::Exit();
#ifdef HX_NATIVE
    if (TheMaster)
#endif
        TheMaster->RemoveSink(this);
    if (mRecorder) {
        mRecorder->Free();
    }
    TheHamDirector->SetPlayerSpotlightsEnabled(true);
}

bool BustAMovePanel::InBustAMove() {
    static Symbol gameplay_mode("gameplay_mode");
    static Symbol bustamove("bustamove");
    return TheGameMode->Property(gameplay_mode)->Sym() == bustamove;
}

Symbol BustAMovePanel::GetPlayerColor(int i1) {
    static Symbol is_in_party_mode("is_in_party_mode");
    const char *color;
    if (TheHamProvider->Property(is_in_party_mode)->Int()) {
        if (TheGameData->Player(i1)->Side() == kSkeletonRight) {
            color = "pink";
        } else {
            color = "blue";
        }
    } else {
        if (i1 == 0) {
            color = "pink";
        } else {
            color = "blue";
        }
    }
    return Symbol(color);
}

MoveRating BustAMovePanel::GetMoveRating(float f1) {
    if (f1 > 0.85f) {
        return kMoveRatingSuperPerfect;
    } else if (f1 > 0.70f) {
        return kMoveRatingPerfect;
    } else if (f1 > 0.4f) {
        return kMoveRatingAwesome;
    } else {
        return kMoveRatingOk;
    }
}

void BustAMovePanel::SetFlashcardText(int side, int index, Symbol s3) {
    HamLabel *label =
        mBAMColumns[side]->Find<HamLabel>(MakeString("flashcard_%d.lbl", index));
    label->SetTextToken(s3);
    HamLabel *label2 =
        mBAMColumns[side == 0]->Find<HamLabel>(MakeString("flashcard_%d.lbl", index));
    if (mState == kBAMState_ShowMoveSequence
        || mState == kBAMState_ShowMoveSequenceSetup) {
        label2->SetTextToken(s3);
    } else {
        label2->SetTextToken(gNullStr);
    }
}

DataArray *BustAMovePanel::GetMoveNameData(int index) {
    static Symbol bustamove_move_names("bustamove_move_names");
    MILO_ASSERT(index >= 0 && index < MAX_FREESTYLE_MOVES, 0x656);
    int nameIndex = mMoveNameIndices[index];
    MILO_ASSERT(nameIndex >= 0 && nameIndex < mShuffledMoveNames.size(), 0x658);
    DataArray *arr = TheGamePanel->Property(bustamove_move_names)->Array();
    return arr->Array(nameIndex);
}

void BustAMovePanel::SetMovePrompt() {
    Symbol sym = GetMoveNameData(mMoveIndex)->Sym(0);
    mMovePromptLabel->SetTextToken(sym);
    UIColor *movePromptColor = DataDir()->Find<UIColor>("move_prompt.color");
    UIColor *playerColor =
        DataDir()->Find<UIColor>(MakeString("%s.color", GetPlayerColor(mActivePlayer)));
    Hmx::Color color = playerColor->GetColor();
    movePromptColor->SetColor(color);
}

void BustAMovePanel::IncreaseScore(int player, int scoreToAdd) {
    static Symbol score("score");
    int oldScore = TheGameData->Player(player)->Provider()->Property(score)->Int();
    TheGameData->Player(player)->Provider()->SetProperty(score, oldScore + scoreToAdd);
}

void BustAMovePanel::ResetScores() {
    static Symbol score("score");
    TheGameData->Player(0)->Provider()->SetProperty(score, 0);
    TheGameData->Player(1)->Provider()->SetProperty(score, 0);
}

void BustAMovePanel::SetFlashcardName(int side, int index, int i3) {
    int flashCardIdx = index;
    Symbol s(gNullStr);
    if (i3 >= 0) {
        Symbol moveName = GetMoveNameData(i3)->Sym(1);
        s = moveName;
    }
    HamLabel *label = mBAMColumns[side]->Find<HamLabel>(
        MakeString("flashcard_name_%d.lbl", flashCardIdx)
    );
    label->SetTextToken(s);
    HamLabel *label2 = mBAMColumns[side == 0]->Find<HamLabel>(
        MakeString("flashcard_name_%d.lbl", flashCardIdx)
    );
    if (mState == kBAMState_ShowMoveSequence
        || mState == kBAMState_ShowMoveSequenceSetup) {
        label2->SetTextToken(s);
    } else {
        label2->SetTextToken(gNullStr);
    }
}

void BustAMovePanel::CountIn(int i1) {
    int f4 = (int)(TheTaskMgr.Beat() + 0.5f) + i1;
    static Message countInMsg("count_in", 0, 0);
    countInMsg[0] = f4;
    countInMsg[1] = f4;
    Handle(countInMsg, true);
}

void BustAMovePanel::ShowMoveRating(MoveRating mr, int side) {
    const char *sideStr = side == 0 ? "left" : "right";
    RndDir *feedbackDir =
        DataDir()->Find<RndDir>(MakeString("bustamove_text_feedback_%s", sideStr));
    static Symbol move_perfect("move_perfect");
    static Symbol move_awesome("move_awesome");
    static Symbol move_ok("move_ok");
    static Symbol move_bad("move_bad");
    static Message moveFinishedMsg("move_finished", 0, 0);
    moveFinishedMsg[0] = side;
    if (mr == kMoveRatingSuperPerfect) {
        feedbackDir->Find<RndAnimatable>("move_flawless_right.anim")->Animate(0, 0, 0);
        moveFinishedMsg[1] = move_perfect;
    }
    if (mr == kMoveRatingPerfect) {
        feedbackDir->Find<RndAnimatable>("move_nice_right.anim")->Animate(0, 0, 0);
        moveFinishedMsg[1] = move_awesome;
    }
    if (mr == kMoveRatingAwesome) {
        feedbackDir->Find<RndAnimatable>("move_okay_right.anim")->Animate(0, 0, 0);
        moveFinishedMsg[1] = move_ok;
    }
    if (mr == kMoveRatingOk) {
        moveFinishedMsg[1] = move_bad;
    }
    TheHamProvider->Handle(moveFinishedMsg, false);
}

void BustAMovePanel::SetRoundFailure() {
    static Message resultMessage("set_bustamove_result", 0, 0, 0);
    resultMessage[0] = mActivePlayer == 0;
    resultMessage[1] = 0;
    resultMessage[2] = 0;
    TheHamProvider->Handle(resultMessage, false);
}

void BustAMovePanel::PlayMovePromptVO() {
    PlayVO(GetMoveNameData(mMoveIndex)->Sym(mCreatorSide + 2));
}

float BustAMovePanel::GetMovePromptVOLength() {
    float len = 0;
    Symbol sym = GetMoveNameData(mMoveIndex)->Sym(mCreatorSide + 2);
    static Message voLengthMsg("get_seq_length", 0);
    voLengthMsg[0] = sym;
    DataNode handled = mHUDPanel->Handle(voLengthMsg, true);
    if (handled != DATA_UNHANDLED) {
        len = handled.Float();
    }
    return len;
}

void BustAMovePanel::ShowGetReadyCard(Symbol s, SkeletonSide side) {
    mBAMColumns[side]->Find<HamLabel>("get_ready.lbl")->SetTextToken(s);
    static Message getReadyMsg("bustamove_get_ready", 0);
    getReadyMsg[0] = side;
    TheHamProvider->Handle(getReadyMsg, false);
}

void BustAMovePanel::CacheObjects() {
    mBAMVisualizerPanel = ObjectDir::Main()->Find<HamPanel>("bustamove_visualizer_panel");
    mBAMVisualizerPanel->DataDir()
        ->Find<RndAnimatable>("num_players.anim")
        ->SetFrame(1, 1);
    for (ObjDirItr<DepthBuffer3D> it(mBAMVisualizerPanel->DataDir(), true); it != nullptr;
         ++it) {
        it->SetGrooviness(1.0f);
        it->mPlayerPaletteTex.SetObjConcrete(NULL);
    }
    TheMaster->AddSink(this, "beat");
    mStatusLabel = DataDir()->Find<HamLabel>("status.lbl");
    mMovePromptLabel = DataDir()->Find<HamLabel>("move_prompt.lbl");
    mStatusLabel->SetTextToken(gNullStr);
    mMovePromptLabel->SetTextToken(gNullStr);
    if (SystemLanguage() == "jpn" || SystemLanguage() == "kor"
        || SystemLanguage() == "cht") {
        DataDir()
            ->Find<RndAnimatable>("asian_prompt_adjust.anim")
            ->Animate(0, false, 0, nullptr, kEaseLinear, 0, false);
    }
    mState = kBAMState_CountIn;
    mBeatCount = 0;
    mMatchCount = 0;
    mMoveIndex = 0;
    mActivePlayer = RandomInt(0, 2);
    mRecordSuccess = false;
    mHUDPanel = DataVariable("hud_panel").Obj<ObjectDir>();
    for (int i = 0; i < 4; i++) {
        String flashcard = MakeString("flashcard_slot%i.mat", i);
        RndMat *flashcardMat = DataDir()->Find<RndMat>(flashcard.c_str());
        RndTex *blank = DataDir()->Find<RndTex>("blank.tex");
        flashcardMat->SetDiffuseTex(blank);
    }
    mBAMColumns[kSkeletonRight] = DataDir()->Find<RndDir>("bustamove_column_right");
    mBAMColumns[kSkeletonLeft] = DataDir()->Find<RndDir>("bustamove_column_left");
    mFlashcardLabels.clear();
    mFlashcardSlots.clear();
    ResetScores();
    mMaxRetries = 1;
    mRetryCount0 = 0;
    mRetryCount1 = 0;
    mPhraseMeters[kSkeletonRight] = DataDir()->Find<HamPhraseMeter>("phrase_meter_right");
    mPhraseMeters[kSkeletonLeft] = DataDir()->Find<HamPhraseMeter>("phrase_meter_left");
    mNextVOTime = FLT_MAX;
    DataDir()->Find<RndAnimatable>("num_players.anim")->SetFrame(1, 1);
    for (int i = 0; i < 4; i++) {
        String flashcardSlot = MakeString("flashcard_slot%i.lbl", i);
        HamLabel *label = DataDir()->Find<HamLabel>(flashcardSlot.c_str());
        label->SetTextToken(gNullStr);
        String flashcardSlotBG = MakeString("flashcard_slot_background%i.mat", i);
        RndMat *mat = DataDir()->Find<RndMat>(flashcardSlotBG.c_str());
        UIColor *gray = DataDir()->Find<UIColor>("gray.color");
        const Hmx::Color &color = gray->GetColor();
        mat->SetColor(color.red, color.green, color.blue);
    }
    mRecorder->mRecordingTarget = MetaPerformer::Current()->GetSong();
    for (int i = 0; i < 2; i++) {
        ((bool *)&mFlawlessFlags)[i] = true;
    }
    mDepthBufPlayer = -1;
}

void BustAMovePanel::SetUpMoveNames() {
    mShuffledMoveNames.clear();
    static Symbol bustamove_move_names("bustamove_move_names");
    GetShuffledInts(
        mShuffledMoveNames, TheGamePanel->Property(bustamove_move_names)->Array()->Size()
    );
}

void BustAMovePanel::PlayVO(Symbol s) {
    static Message playVOMsg("play", 0);
    playVOMsg[0] = s;
    mHUDPanel->Handle(playVOMsg, true);
}

void BustAMovePanel::QueueMovePromptVO() {
    float voLength = GetMovePromptVOLength();
    TempoMap *tempoMap = TheMaster->SongData()->GetTempoMap();
    float bpm = tempoMap->GetTempoBPM(0);
    float secondsPerBeat = 60.0f / bpm;
    int reps = mRepsRemaining;
    float beatsToWait = (float)((reps * 4) - 4);
    float timeOffset = beatsToWait * secondsPerBeat;
    float currentTime = TheTaskMgr.Seconds(TaskMgr::kRealTime);
    mNextVOTime = currentTime + timeOffset - voLength - 1.0f;
}

void BustAMovePanel::PollCaptureFlashcard() {
    if (mCaptureStep != 0) {
        float flashcardTweak = 0.17f;
        if (DataVarExists("flashcard_tweak")) {
            flashcardTweak = DataVariable("flashcard_tweak").Float();
        }
        if (mCaptureTimer >= flashcardTweak) {
            BaseSkeleton *liveSkel = mRecorder->GetLiveSkeleton();
            if (liveSkel != nullptr) {
                mCapturePoses[mCaptureStep - 1].Set(*mRecorder->GetLiveSkeleton());
            } else {
                mCapturePoses[mCaptureStep - 1].SetTracked(false);
            }
            if (mCaptureStep == 3) {
                float score1 =
                    mRecorder->CompareSkeletonPositions(&mCapturePoses[0], &mCapturePoses[1], 1.0f);
                float score2 =
                    mRecorder->CompareSkeletonPositions(&mCapturePoses[0], &mCapturePoses[2], 1.0f);
                if (score2 < 0.5f) {
                    mCapturePoses[1].SetTracked(false);
                } else {
                    mCapturePoses[2].SetTracked(false);
                }
                if (score1 >= 0.5f) {
                    mCapturePoses[1].SetTracked(false);
                }
                mCaptureFrames = 4;
            }
            mCaptureStep = 0;
        } else {
            mCaptureTimer += TheTaskMgr.DeltaUISeconds();
        }
    }
}

void BustAMovePanel::AnimateFlashcard(int i) {
    UIColor *pColor = DataDir()->Find<UIColor>(MakeString("color%d.color", i + 1));
    const Hmx::Color color = pColor->GetColor();

    RndMat *pFlashcardMat = DataDir()->Find<RndMat>("flashcard.mat");
    String flashcardStrMakeString(MakeString("flashcard%i.tex", i));
    RndTex *pFlashcardTex = DataDir()->Find<RndTex>(flashcardStrMakeString.c_str());
    pFlashcardMat->SetDiffuseTex(pFlashcardTex);

    RndMat *pFlashcardCapBgMat =
        DataDir()->Find<RndMat>("flashcard_capture_background.mat");
    pFlashcardCapBgMat->SetColor(color.red, color.green, color.blue);

    HamLabel *pFlashcardCapLabel = DataDir()->Find<HamLabel>("flashcard_capture.lbl");
    Symbol moveDataSym = GetMoveNameData(i)->Sym(1);
    pFlashcardCapLabel->SetTextToken(moveDataSym);

    String flashcardSlotStr(MakeString("flashcard_slot%i.mat", i));
    RndMat *pFlashcardSlot = DataDir()->Find<RndMat>(flashcardSlotStr.c_str());
    RndTex *pFlashcardSlotTex = DataDir()->Find<RndTex>(flashcardStrMakeString.c_str());
    pFlashcardSlot->SetDiffuseTex(pFlashcardSlotTex);

    String flashcardSlotBgStr = MakeString("flashcard_slot_background%i.mat", i);
    RndMat *pFlashcardSlotBg = DataDir()->Find<RndMat>(flashcardSlotBgStr.c_str());
    pFlashcardSlotBg->SetColor(color.red, color.green, color.blue);

    String flashcardSlotLblStr = MakeString("flashcard_slot%i.lbl", i);
    HamLabel *pFlashcardSlotLabel =
        DataDir()->Find<HamLabel>(flashcardSlotLblStr.c_str());
    Symbol moveData = GetMoveNameData(i)->Sym(1);
    pFlashcardSlotLabel->SetTextToken(moveData);

    DataDir()->Find<RndPropAnim>("capture_flashcard.anim")->Animate(0.0f, false, 0.0f);
}

void BustAMovePanel::AdvanceFlashcards() {
    // Stop and restart advance.anim for each column
    for (int i = 0; i < 2; i++) {
        RndPropAnim *anim =
            mBAMColumns[i]->Find<RndPropAnim>("advance.anim", true);
        anim->StopAnimation();
        anim->SetFrame(0.0f, 1.0f);
    }

    int side = mCreatorSide;

    // Pop front of mFlashcardLabels (Symbol list)
    if (!mFlashcardLabels.empty()) {
        mFlashcardLabels.pop_front();
    }

    // Set flashcard text for 4 slots
    std::list<Symbol>::iterator symEnd = mFlashcardLabels.end();
    std::list<Symbol>::iterator symIt = mFlashcardLabels.begin();
    for (int i = 0; i < 4; i++) {
        if (symIt != symEnd) {
            SetFlashcardText(side, i, *symIt);
            ++symIt;
        } else {
            SetFlashcardText(side, i, Symbol(gNullStr));
        }
    }

    // Pop front of mFlashcardSlots (int list)
    if (!mFlashcardSlots.empty()) {
        mFlashcardSlots.pop_front();
    }

    // Set flashcard image and name for 4 slots
    std::list<int>::iterator intEnd = mFlashcardSlots.end();
    std::list<int>::iterator intIt = mFlashcardSlots.begin();
    for (int i = 0; i < 4; i++) {
        int val = -1;
        if (intIt != intEnd) {
            val = *intIt;
            ++intIt;
        }
        SetFlashcardImage(side, i, val);
        SetFlashcardName(side, i, val);
    }
}

int BustAMovePanel::RepsToNextPhrase() {
    int beat = (int)(TheTaskMgr.Beat() + 0.5f);
    if (mStreamJumped) {
        int loopEnd;
        TheMaster->GetAudio()->GetCurrLoopBeats(beat, loopEnd);
    }

    auto songStructureBegin = mSongStructure.begin();
    int size = (int)((mSongStructure.end() - songStructureBegin));
    int *data = &mSongStructure[0];
    unsigned int count = 0;
    int repsInPhrase;

    if (size != 0) {
loop_top:
        beat -= data[count] * 4;
        if (beat >= 0) {
            count++;
            if (count < (unsigned int)size)
                goto loop_top;
            goto default_reps;
        }
        repsInPhrase = (3 - beat) / 4;
        if (repsInPhrase != -1)
            goto calc_total;
    }
default_reps:
    repsInPhrase = data[1];

calc_total:;
    int total = 0;
    int iter = 1;
    unsigned int nextIdx = count + 1;
    do {
        unsigned int wrappedIdx = nextIdx;
        if (nextIdx >= (unsigned int)size) {
            wrappedIdx = (nextIdx - size) + 1;
        }
        if (total + repsInPhrase >= 3 && data[wrappedIdx] == 4) {
            break;
        }
        iter++;
        nextIdx++;
        total += data[wrappedIdx];
    } while (iter < 10);
    return total + repsInPhrase;
}

void BustAMovePanel::SetFlashcardImage(int side, int index, int i3) {
    RndMat *flashcardMat =
        mBAMColumns[side]->Find<RndMat>(MakeString("flashcard%d.mat", index));
    RndMat *flashcardBgMat =
        mBAMColumns[side]->Find<RndMat>(MakeString("flashcard_background%d.mat", index));
    RndTex *blankTex = DataDir()->Find<RndTex>("blank.tex");

    RndTex *flashcardTex;
    RndTex *bgTex;
    if (i3 >= 0) {
        flashcardTex = DataDir()->Find<RndTex>(MakeString("flashcard%i.tex", i3));
        bgTex = mBAMColumns[side]->Find<RndTex>("blank_bustamove.tex");
    } else if (i3 == -2) {
        flashcardTex = blankTex;
        bgTex = mBAMColumns[side]->Find<RndTex>("blank_bustamove.tex");
    } else {
        flashcardTex = DataDir()->Find<RndTex>("blank.tex");
        bgTex = flashcardTex;
    }

    Hmx::Color color(1.0f, 1.0f, 1.0f);
    if (i3 >= 0) {
        String bgName(MakeString("flashcard_slot_background%i.mat", i3));
        RndMat *slotBgMat = DataDir()->Find<RndMat>(bgName.c_str());
        color = slotBgMat->GetColor();
    } else if (i3 == -2) {
        UIColor *grayColor = DataDir()->Find<UIColor>("gray.color");
        color = grayColor->GetColor();
    }

    flashcardMat->SetDiffuseTex(flashcardTex);
    flashcardBgMat->SetColor(color.red, color.green, color.blue);
    flashcardBgMat->SetDiffuseTex(bgTex);

    // Handle the other side
    RndMat *otherFlashcardMat =
        mBAMColumns[side == 0]->Find<RndMat>(MakeString("flashcard%d.mat", index));
    RndMat *otherBgMat = mBAMColumns[side == 0]->Find<RndMat>(
        MakeString("flashcard_background%d.mat", index)
    );

    if (mState == kBAMState_ShowMoveSequence
        || mState == kBAMState_ShowMoveSequenceSetup) {
        otherFlashcardMat->SetDiffuseTex(flashcardTex);
        otherBgMat->SetColor(color.red, color.green, color.blue);
        otherBgMat->SetDiffuseTex(bgTex);
    } else {
        otherFlashcardMat->SetDiffuseTex(blankTex);
        otherBgMat->SetDiffuseTex(blankTex);
    }
}

// Main state machine driver for Bust A Move mode.
// Called on every beat tick. Handles state transitions, flashcard setup,
// recording/playback, scoring, and the final dance sequence.
void BustAMovePanel::OnBeat() {
    if (!InBustAMove())
        return;
    if (TheGamePanel->IsGameOver())
        return;

    // Deduplicate beat events — only process each beat number once
    int currentBeat = TheHamProvider->Property(Symbol("beat"), true)->Int();
    static int sLastBeat = -1;
    if (currentBeat == sLastBeat)
        return;
    sLastBeat = currentBeat;

    // Beat 4: advance flashcard columns and handle countdown VO
    if (currentBeat == 4) {
        for (int i = 0; i < 2; i++) {
            RndPropAnim *anim =
                mBAMColumns[i]->Find<RndPropAnim>("advance.anim", true);
            anim->Animate(0.0f, false, 0.0f);
        }

        if (mState == kBAMState_Recording) {
            if (mBeatCount == 3) {
                static Message endMessage("bustamove_end_create");
                TheHamProvider->Handle(endMessage, false);
            }

            // Play take countdown VO ("take 2!", "take 3!", etc.)
            if (!mIsMulligan) {
                if (mMoveIndex == 0) {
                    switch ((unsigned int)mBeatCount) {
                    case 0:
                        PlayVO(Symbol("nar_bam_take2_firsttime"));
                        break;
                    case 1:
                        PlayVO(Symbol("nar_bam_take3_firsttime"));
                        break;
                    case 2:
                        PlayVO(Symbol("nar_bam_take4_firsttime"));
                        break;
                    }
                } else {
                    switch ((unsigned int)mBeatCount) {
                    case 0:
                        PlayVO(Symbol("nar_bam_take2"));
                        break;
                    case 1:
                        PlayVO(Symbol("nar_bam_take3"));
                        break;
                    case 2:
                        PlayVO(Symbol("nar_bam_take4"));
                        break;
                    }
                }
            }

            // Mulligan countdown: play count-in beats during retry
            if (mIsMulligan && mBeatCount < 3) {
                int beatNum = (int)(TheTaskMgr.Beat() + 0.5f) + 1;
                static Message countInMsg(Symbol("mulligan_count"), 0);
                countInMsg[0] = beatNum;
                Handle(countInMsg, true);
            }
        }

        // Also count in during the last rep of RecordCountIn if it's a mulligan
        if (mState == kBAMState_RecordCountIn && mIsMulligan && mRepsRemaining == 1) {
            int beatNum = (int)(TheTaskMgr.Beat() + 0.5f) + 1;
            static Message countInMsg(Symbol("mulligan_count"), 0);
            countInMsg[0] = beatNum;
            Handle(countInMsg, true);
        }
    }

    // Everything below only runs on beat 1 (measure boundaries)
    if (currentBeat != 1)
        goto end_handling;

    mRecorder->ClearFrameScores();

    // Determine next state: pending override takes priority, otherwise use state machine.
    // Unsigned cast acts as a range guard — states above ShowMoveSequence skip the switch.
    BAMState nextState = kBAMState_None;
    if (mPendingState != kBAMState_None) {
        nextState = (BAMState)mPendingState;
        mPendingState = kBAMState_None;
    } else if ((unsigned int)mState <= (unsigned int)kBAMState_ShowMoveSequence) {
        switch (mState) {
        case kBAMState_CountIn:
            if (mBeatCount == mCountInLength + 3)
                nextState = kBAMState_Recording;
            break;
        case kBAMState_Recording:
            if (mBeatCount == 3) {
                // Hide visualizers and evaluate the 4-beat recording
                for (ObjDirItr<DepthBuffer3D> it(mBAMVisualizerPanel->DataDir(), true);
                     it != nullptr; ++it) {
                    it->SetShowing(false);
                }
                mRecorder->StopPlayback();
                TheDebug << MakeString(
                    "1: %f(%d)   2: %f(%d)\n",
                    mDancerTakeScore,
                    mRecorder->GetDancerTakeFrameCount(),
                    mCurrentMoveScore,
                    mRecorder->GetCurrentMoveNumFrames()
                );
                // Recording passes if both scores exceed 65% (unless no poses detected)
                if (mNoPosesDetected) {
                    mRecordSuccess = false;
                } else {
                    mRecordSuccess = mDancerTakeScore > 0.65f && mCurrentMoveScore > 0.65f;
                }
                if (mRecordSuccess) {
                    mMoveCreators[mMoveIndex] = mActivePlayer;
                    mMoveIndex++;
                    IncreaseScore(mActivePlayer, 200000);
                    AnimateFlashcard(mMoveIndex - 1);
                    nextState = kBAMState_ShowMove;
                    static Message createdMessage("bustamove_move_created");
                    TheHamProvider->Handle(createdMessage, false);
                } else {
                    nextState = kBAMState_FailureToBust;
                }
            }
            break;
        case kBAMState_Playing:
            if (mBeatCount >= 3) {
                if (DataVariable(Symbol("stay_on_bam_play")).Int() == 0)
                    nextState = kBAMState_RecordCountIn;
            }
            break;
        case kBAMState_ShowMove:
            nextState = kBAMState_PlayCountIn;
            break;
        case kBAMState_PlayCountIn:
            if (mRepsRemaining == 1)
                nextState = kBAMState_Playing;
            break;
        case kBAMState_RecordCountIn:
            if (mRepsRemaining == 1)
                nextState = kBAMState_Recording;
            break;
        case kBAMState_FailureToBust:
            if (mBeatCount == 1)
                nextState = kBAMState_RecordCountIn;
            break;
        case kBAMState_ShowMoveSequenceSetup:
            if (mRepsRemaining == 1)
                nextState = kBAMState_ShowMoveSequence;
            break;
        case kBAMState_ShowMoveSequence:
            if (mBeatCount == 15)
                nextState = kBAMState_End;
            break;
        default:
            break;
        }
    }

    mBeatCount++;
    if (mRepsRemaining > 0)
        mRepsRemaining--;

    mStatusLabel->SetTextToken(gNullStr);
    mMovePromptLabel->SetTextToken(gNullStr);

    AdvanceFlashcards();

    if (nextState != kBAMState_None) {
        mState = nextState;
        mBeatCount = 0;
        mRepsRemaining = RepsToNextPhrase();
    }

    if (mStreamJumped) {
        mRepsRemaining = RepsToNextPhrase();
        mStreamJumped = false;
    }

    mRecorder->SetVal44(mBeatCount);

    switch (mState) {
    case kBAMState_CountIn:
        if (mBeatCount == 1) {
            // Pick a move name from the shuffled pool (skip moves without clips)
            SetUpMoveNames();
            unsigned int count = 0;
            if (mShuffledMoveNames.size() != 0) {
                do {
                    mMoveNameIndices[mMoveIndex] = mShuffledMoveNames[mMoveNameCursor];
                    unsigned int size = mShuffledMoveNames.size();
                    unsigned int nextIdx = mMoveNameCursor + 1;
                    mMoveNameCursor = nextIdx % size;
                    DataArray *arr = GetMoveNameData(0);
                    int clipExists = arr->Node(4).Int(arr);
                    if (clipExists != 0)
                        break;
                    count++;
                } while (count < mShuffledMoveNames.size());
            }
        }
        if (mBeatCount == mCountInLength - 2) {
            // Activate intro flow and queue the move prompt VO
            Flow *flow = DataDir()->Find<Flow>("intro.flow", true);
            flow->Activate();
            QueueMovePromptVO();
        }
        if (mBeatCount == mCountInLength - 1) {
            // Set up flashcard slots and labels for the recording phase
            mFlashcardSlots.push_back(-1);
            mFlashcardSlots.push_back(-1);
            mFlashcardSlots.push_back(-1);
            mFlashcardSlots.push_back(-1);
            mFlashcardSlots.push_back(-1);
            mFlashcardSlots.push_back(-2);
            mFlashcardSlots.push_back(-2);
            mFlashcardSlots.push_back(-2);
            mFlashcardSlots.push_back(-2);
            mFlashcardLabels.push_back(Symbol(gNullStr));
            mFlashcardLabels.push_back(Symbol(gNullStr));
            mFlashcardLabels.push_back(Symbol(gNullStr));
            mFlashcardLabels.push_back(Symbol(gNullStr));
            mFlashcardLabels.push_back(Symbol(gNullStr));
            mFlashcardLabels.push_back(Symbol("bam_record1"));
            mFlashcardLabels.push_back(Symbol("bam_record2"));
            mFlashcardLabels.push_back(Symbol("bam_record3"));
            mFlashcardLabels.push_back(Symbol("bam_record4"));
            CountIn(16);
        }
        if (mRepsRemaining == 2 || mRepsRemaining == 1) {
            SetMovePrompt();
        }
        if (mRepsRemaining != 2)
            break;
        ShowGetReadyCard(Symbol("get_ready"), (SkeletonSide)mCreatorSide);
        break;
    case kBAMState_Recording: {
        if (mBeatCount == 0) {
            mNoPosesDetected = false;
            for (ObjDirItr<DepthBuffer3D> it(mBAMVisualizerPanel->DataDir(), true);
                 it != nullptr; ++it) {
                it->mPlayerPaletteTex.SetObjConcrete(NULL);
                it->SetShowing(true);
            }
        } else {
            for (ObjDirItr<DepthBuffer3D> it(mBAMVisualizerPanel->DataDir(), true);
                 it != nullptr; ++it) {
                if (std::strstr(it->Name(), "live")) {
                    it->SetShowing(DataVariable(Symbol("hide_bam_ghost")).Int() == 0);
                    it->mPlayerPaletteTex.SetObjConcrete(NULL);
                } else {
                    it->mPlayerPaletteTex.SetObjConcrete(mRecorder->GetPlayerPalette());
                }
            }
        }
        if (mBeatCount == 0) {
            mRecorder->StopPlayback();
            mRecorder->SetFreestyleMove(mMoveIndex);
            static Message startMessage("bustamove_start_create", 0);
            startMessage[0] = DataNode(mCreatorSide);
            TheHamProvider->Handle(startMessage, false);
            mRecorder->ClearRecording();
            mRecorder->StartRecording();
        }
        // 4-beat recording pipeline:
        //   Beat 1: start dancer take + ghost playback
        //   Beat 2: re-record over first take + restart playback
        //   Beat 3: finalize recording, keep playback for preview
        if (mBeatCount == 1) {
            mRecorder->ClearDancerTake();
            mRecorder->StartRecordingDancerTake();
            mRecorder->StartPlayback(true);
        }
        if (mBeatCount == 2) {
            mRecorder->StartRecording();
            mRecorder->StopPlayback();
            mRecorder->StartPlayback(true);
        }
        if (mBeatCount == 3) {
            mRecorder->StopRecording();
            mRecorder->StopPlayback();
            mRecorder->StartPlayback(true);
        }
        mRecordScore = 0.0f;
        mMoveScore = 0.0f;
        break;
    }
    case kBAMState_Playing: {
        // Opponent plays back the created move — score each beat
        for (ObjDirItr<DepthBuffer3D> it(mBAMVisualizerPanel->DataDir(), true);
             it != nullptr; ++it) {
            it->SetShowing(true);
        }
        if (mBeatCount == 0) {
            mRecorder->StopRecording();
            mMatchCount = 0;
        } else {
            mRecorder->StopPlayback();
            MoveRating rating = GetMoveRating(mMoveScore);
            ShowMoveRating(rating, mCreatorSide);
            // Score Perfect/SuperPerfect moves. The comma operator clears the
            // opponent's flawless flag as a side-effect before testing Perfect.
            // mFlawlessFlags is accessed as a bool[2] array (codegen requirement).
            if (rating == kMoveRatingSuperPerfect
                || (((bool *)&mFlawlessFlags)[!mActivePlayer] = false,
                    rating == kMoveRatingPerfect)) {
                mMatchCount++;
                int score =
                    (rating == kMoveRatingSuperPerfect) ? 50000 : 40000;
                IncreaseScore(!mActivePlayer, score);
                static Message playMsg("bustamove_move_matched", 0);
                playMsg[0] = DataNode(mMatchCount);
                TheHamProvider->Handle(playMsg, false);
            }
        }
        mMoveScore = 0.0f;
        mRecorder->StartPlayback(false);
        for (ObjDirItr<DepthBuffer3D> it(mBAMVisualizerPanel->DataDir(), true);
             it != nullptr; ++it) {
            it->mPlayerPaletteTex.SetObjConcrete(mRecorder->GetPlayerPalette());
        }
        break;
    }
    case kBAMState_ShowMove: {
        for (ObjDirItr<DepthBuffer3D> it(mBAMVisualizerPanel->DataDir(), true);
             it != nullptr; ++it) {
            it->SetShowing(false);
        }
        mRecorder->StopRecording();
        mRecorder->StartPlayback(false);
#define mReps mRepsRemaining
        MILO_ASSERT(mReps == 0, 0x328);
#undef mReps
        mFlashcardLabels.push_back(gNullStr);
        break;
    }
    case kBAMState_PlayCountIn: {
        for (ObjDirItr<DepthBuffer3D> it(mBAMVisualizerPanel->DataDir(), true);
             it != nullptr; ++it) {
            it->SetShowing(false);
        }
        mRecorder->StopPlayback();
        if (mRepsRemaining > 3) {
            mFlashcardLabels.push_back(gNullStr);
        }
        if (mRepsRemaining == 3) {
            mFlashcardSlots.push_back(-1);
            mFlashcardSlots.push_back(-1);
            mFlashcardSlots.push_back(-1);
            mFlashcardSlots.push_back(mMoveIndex - 1);
            mFlashcardSlots.push_back(mMoveIndex - 1);
            mFlashcardSlots.push_back(mMoveIndex - 1);
            mFlashcardSlots.push_back(mMoveIndex - 1);
            mFlashcardLabels.push_back(gNullStr);
            mFlashcardLabels.push_back(gNullStr);
            mFlashcardLabels.push_back(gNullStr);
            mFlashcardLabels.push_back(gNullStr);
            mFlashcardLabels.push_back(gNullStr);
            mFlashcardLabels.push_back(gNullStr);
            mFlashcardLabels.push_back(gNullStr);
                        char *sideStr;
            if ((SkeletonSide)mCreatorSide == kSkeletonLeft) {
                sideStr = "left";
            } else {
                sideStr = "right";
            }
            PlayVO(Symbol(MakeString("nar_bam_%s_needstorepeat", sideStr)));
            CountIn(8);
        }
        if (mRepsRemaining != 2)
            break;
        ShowGetReadyCard(Symbol("get_ready_to_dance"), (SkeletonSide)mCreatorSide);
        break;
    }
    case kBAMState_RecordCountIn: {
        // Transition between rounds — score previous round, set up next move
        for (ObjDirItr<DepthBuffer3D> it(mBAMVisualizerPanel->DataDir(), true);
             it != nullptr; ++it) {
            it->mPlayerPaletteTex.SetObjConcrete(NULL);
            it->SetShowing(true);
        }
        if (mBeatCount == 0) {
            // Advance to next move name in the shuffled pool
            mMoveNameIndices[mMoveIndex] = mShuffledMoveNames[mMoveNameCursor];
            unsigned int nextIdx = mMoveNameCursor + 1;
            mIsMulligan = false;
            unsigned int size = mShuffledMoveNames.size();
            mMoveNameCursor = nextIdx % size;
            if (mRecordSuccess) {
                mRecorder->PlaybackComplete();
                MoveRating rating = GetMoveRating(mMoveScore);
                ShowMoveRating(rating, mCreatorSide);
                // Same comma-operator pattern: clear opponent's flawless flag,
                // then check for Perfect (see kBAMState_Playing for details)
                if (rating == kMoveRatingSuperPerfect
                    || (((bool *)&mFlawlessFlags)[!mActivePlayer] = false,
                        rating == kMoveRatingPerfect)) {
                    mMatchCount++;
                    int score;
                    if (rating == kMoveRatingSuperPerfect) {
                        score = 50000;
                    } else {
                        score = 40000;
                    }
                    IncreaseScore(!mActivePlayer, score);
                    static Message matchedMessage("bustamove_move_matched", 0);
                    matchedMessage[0] = DataNode(mMatchCount);
                    TheHamProvider->Handle(matchedMessage, false);
                }
                if (mMatchCount > 0) {
                    static Message successMessage("bustamove_successfully_matched");
                    TheHamProvider->Handle(successMessage, false);
                    mStatusLabel->SetTextToken(Symbol("bam_matched"));
                } else if (mMatchCount == 0) {
                    SetRoundFailure();
                    mStatusLabel->SetTextToken(Symbol("bam_failed"));
                    HamPlayerData *playerData = TheGameData->Player(mActivePlayer);
                    HamProfile *profile =
                        TheProfileMgr.GetProfileFromPad(playerData->PadNum());
                    if (profile && profile->HasValidSaveData()) {
                        static Symbol acc_inimitable("acc_inimitable");
                        TheAccomplishmentMgr->EarnAccomplishmentForProfile(
                            profile, acc_inimitable, false
                        );
                    }
                    static Message failMessage("bustamove_fail_match");
                    TheHamProvider->Handle(failMessage, false);
                }
            } else {
                // Index into mRetryCount0/mRetryCount1 by player (adjacent ints in struct)
                int *pRetries = &(&mRetryCount0)[mActivePlayer];
                int retries = *pRetries;
                if (retries < mMaxRetries) {
                    *pRetries = retries + 1;
                    mIsMulligan = true;
                }
            }
            if (mRecordSuccess || !mIsMulligan) {
                mActivePlayer = !mActivePlayer;
            }
            mMoveScore = 0.0f;
            if (mMoveIndex == 4) {
                mPendingState = kBAMState_ShowMoveSequenceSetup;
                mFlashcardLabels.push_back(Symbol(gNullStr));
            }
            if (mLoopStartBeat != -1.0f) {
                TheMaster->GetAudio()->SetLoop(mLoopStartBeat, mLoopEndBeat);
            }
        }
        if (mRepsRemaining == 4 && mMoveIndex != 4) {
            QueueMovePromptVO();
            mFlashcardSlots.push_back(-1);
            mFlashcardSlots.push_back(-1);
            mFlashcardSlots.push_back(-1);
            mFlashcardSlots.push_back(-1);
            mFlashcardSlots.push_back(-2);
            mFlashcardSlots.push_back(-2);
            mFlashcardSlots.push_back(-2);
            mFlashcardSlots.push_back(-2);
            mFlashcardLabels.push_back(Symbol(gNullStr));
            mFlashcardLabels.push_back(Symbol(gNullStr));
            mFlashcardLabels.push_back(Symbol(gNullStr));
            mFlashcardLabels.push_back(Symbol(gNullStr));
            mFlashcardLabels.push_back(Symbol("bam_record1"));
            mFlashcardLabels.push_back(Symbol("bam_record2"));
            mFlashcardLabels.push_back(Symbol("bam_record3"));
            mFlashcardLabels.push_back(Symbol("bam_record4"));
        }
        if (mRepsRemaining == 3) {
            CountIn(8);
        }
        if (mRepsRemaining == 2 || mRepsRemaining == 1) {
            SetMovePrompt();
        }
        if (mRepsRemaining != 2)
            break;
        ShowGetReadyCard(Symbol("get_ready"), (SkeletonSide)mCreatorSide);
        break;
    }
    case kBAMState_FailureToBust:
        // Recording failed — announce failure and set up an audio loop for retry
        if (mBeatCount == 0) {
            static Message failMessage("bustamove_fail_bust");
            TheHamProvider->Handle(failMessage, false);
            mMatchCount = 0;
            if (!mIsMulligan) {
                PlayVO(Symbol("nar_bam_gen_fail"));
            } else {
                const char *sideStr =
                    (SkeletonSide)mCreatorSide == kSkeletonLeft ? "left" : "right";
                PlayVO(Symbol(MakeString("nar_bam_gen_second_fail_%s", sideStr)));
            }
            // Round current beat to nearest integer, set 8-beat retry loop
            float streamMs = TheMaster->StreamMs();
            float beat = MsToBeat(streamMs);
            int beatInt;
            if (beat > 0.0f) {
                beatInt = (int)(beat + 0.5f);
            } else {
                beatInt = (int)(beat - 0.5f);
            }
            float beatF = (float)beatInt;
            TheMaster->GetAudio()->SetLoop(beatF, beatF + 8.0f);
            // Re-read beat after setting loop and compute when failure ends
            float beat2 = MsToBeat(TheMaster->StreamMs());
            int beat2Int;
            if (beat2 > 0.0f) {
                beat2Int = (int)(beat2 + 0.5f);
            } else {
                beat2Int = (int)(beat2 - 0.5f);
            }
            mFailureEndBeat = beat2Int + 7;
        }
        break;
    case kBAMState_End:
        // Determine winner, award flawless accomplishments, play result VO
        if (mBeatCount == 0) {
            static Symbol score("score");
            int score0 =
                TheGameData->Player(0)->Provider()->Property(score)->Int();
            int score1 =
                TheGameData->Player(1)->Provider()->Property(score)->Int();
            int winner = -1; // -1 = tie
            if (score0 > score1) {
                winner = 0;
            } else if (score1 > score0) {
                winner = 1;
            }
            static Message winnerMessage("bustamove_winner", 0);
            if (winner >= 0) {
                winnerMessage[0] = DataNode(TheGameData->Player(winner)->Side());
            } else {
                winnerMessage[0] = DataNode(winner);
            }
            TheHamProvider->Handle(winnerMessage, false);
            if (winner >= 0) {
                for (ObjDirItr<DepthBuffer3D> it(mBAMVisualizerPanel->DataDir(), true);
                     it != nullptr; ++it) {
                    it->mPlayerPaletteTex.SetObjConcrete(NULL);
                }
                RndAnimatable *numPlayers =
                    DataDir()->Find<RndAnimatable>("num_players.anim", true);
                numPlayers->SetFrame(1.0f, 1.0f);
                RndAnimatable *vizNumPlayers =
                    mBAMVisualizerPanel->DataDir()->Find<RndAnimatable>("num_players.anim", true);
                vizNumPlayers->SetFrame(1.0f, 1.0f);
                mActivePlayer = winner;
            } else {
                for (ObjDirItr<DepthBuffer3D> it(mBAMVisualizerPanel->DataDir(), true);
                     it != nullptr; ++it) {
                    it->SetShowing(false);
                }
            }
            {
                // Award "flawless every move" accomplishment to players who never missed
                int i = 0;
                do {
                    if (((bool *)&mFlawlessFlags)[i]) {
                        HamPlayerData *playerData = TheGameData->Player(i);
                        HamProfile *profile =
                            TheProfileMgr.GetProfileFromPad(playerData->PadNum());
                        static Symbol acc_flawless("acc_flawless_every_move");
                        TheAccomplishmentMgr->EarnAccomplishmentForProfile(
                            profile, acc_flawless, false
                        );
                    }
                    i++;
                } while (i < 2);
            }
            {
                // Play winner/tie VO based on which side won
                bool isLeft;
                {
                    DataNode leftSide(kSkeletonLeft);
                    isLeft = winnerMessage[0].Equal(leftSide, 0, true);
                }
                if (isLeft) {
                    PlayVO(Symbol("nar_bam_win_left"));
                } else {
                    bool isRight;
                    {
                        DataNode rightSide(kSkeletonRight);
                        isRight = winnerMessage[0].Equal(rightSide, 0, true);
                    }
                    if (isRight) {
                        PlayVO(Symbol("nar_bam_win_right"));
                    } else {
                        PlayVO(Symbol("nar_bam_tie"));
                    }
                }
            }
        }
        if (mBeatCount == 3) {
            TheGamePanel->SetGameOver(true);
            TheMaster->GetAudio()->SetPaused(true);
        }
        break;
    case kBAMState_ShowMoveSequenceSetup:
        // Build the final dance sequence — shuffled move order based on complexity type
        if (mBeatCount == 0) {
            for (ObjDirItr<DepthBuffer3D> it(mBAMVisualizerPanel->DataDir(), true);
                 it != nullptr; ++it) {
                it->SetShowing(false);
            }
        }
        if (mRepsRemaining > 3) {
            mFlashcardLabels.push_back(gNullStr);
        }
        if (mRepsRemaining == 3) {
            static Message bothMessage("bustamove_both_dance");
            TheHamProvider->Handle(bothMessage, false);
            PlayVO(Symbol("nar_bam_trans"));
            mFlashcardLabels.push_back(gNullStr);
            mFlashcardLabels.push_back(gNullStr);
            mFlashcardLabels.push_back(gNullStr);
            mFlashcardSlots.push_back(-1);
            mFlashcardSlots.push_back(-1);
            mFlashcardSlots.push_back(-1);
            mFinalSequenceType = DataVariable(Symbol("bam_final_sequence")).Int();
            if (mFinalSequenceType == 0) {
                mFinalSequenceType = 1;
            }
            // Build flashcard sequence based on complexity:
            //   Type 1: repeat one move 4x, then all 4 moves 2x each, then rotated
            //   Type 2: two full shuffled passes of all 4 moves (2x each)
            //   Type 3: one full pass (2x each), half of second, then rotated remainder
            switch (mFinalSequenceType) {
            case 3: {
                std::vector<int> shuffled1;
                GetShuffledInts(shuffled1, 4);
                std::vector<int> shuffled2;
                GetShuffledInts(shuffled2, 4);
                if (shuffled1[3] == shuffled2[0]) {
                    int *p = &shuffled1[0];
                    int n = 4;
                    do {
                        mFlashcardSlots.push_back(*p);
                        mFlashcardSlots.push_back(*p);
                        p++;
                        n--;
                    } while (n != 0);
                }
                {
                    int *p = &shuffled2[0];
                    int n = 2;
                    do {
                        mFlashcardSlots.push_back(*p);
                        mFlashcardSlots.push_back(*p);
                        p++;
                        n--;
                    } while (n != 0);
                }
                {
                    int k = 0;
                    do {
                        mFlashcardSlots.push_back(shuffled2[(k + 2) % 4]);
                        k++;
                    } while (k < 4);
                }
                break;
            }
            case 2: {
                std::vector<int> shuffled2;
                GetShuffledInts(shuffled2, 4);
                std::vector<int> shuffled1;
                GetShuffledInts(shuffled1, 4);
                if (shuffled2[3] == shuffled1[0]) {
                    int tmp = shuffled1[0];
                    shuffled1[0] = shuffled1[3];
                    shuffled1[3] = tmp;
                }
                {
                    int *p = &shuffled2[0];
                    int n = 4;
                    do {
                        mFlashcardSlots.push_back(*p);
                        mFlashcardSlots.push_back(*p);
                        p++;
                        n--;
                    } while (n != 0);
                }
                {
                    int *p = &shuffled1[0];
                    int n = 4;
                    do {
                        mFlashcardSlots.push_back(*p);
                        mFlashcardSlots.push_back(*p);
                        p++;
                        n--;
                    } while (n != 0);
                }
                break;
            }
            case 1: {
                std::vector<int> shuffled1;
                GetShuffledInts(shuffled1, 4);
                int r = RandomInt(1, 4);
                {
                    int n = 4;
                    do {
                        mFlashcardSlots.push_back(shuffled1[r]);
                        n--;
                    } while (n != 0);
                }
                {
                    int *p = &shuffled1[0];
                    int n = 4;
                    do {
                        mFlashcardSlots.push_back(*p);
                        mFlashcardSlots.push_back(*p);
                        p++;
                        n--;
                    } while (n != 0);
                }
                {
                    int k = 0;
                    do {
                        mFlashcardSlots.push_back(shuffled1[(k + 2) % 4]);
                        k++;
                    } while (k < 4);
                }
                break;
            }
            }
            CountIn(8);
        }
        if (mRepsRemaining != 2)
            break;
        ShowGetReadyCard(Symbol("get_ready"), kSkeletonLeft);
        ShowGetReadyCard(Symbol("get_ready"), kSkeletonRight);
        break;
    case kBAMState_ShowMoveSequence:
        if (mBeatCount == 0) {
            RndAnimatable *numPlayers =
                DataDir()->Find<RndAnimatable>("num_players.anim", true);
            numPlayers->SetFrame(2, 1);
            RndAnimatable *vizNumPlayers =
                mBAMVisualizerPanel->DataDir()->Find<RndAnimatable>(
                    "num_players.anim", true
                );
            vizNumPlayers->SetFrame(2, 1);
            RndAnimatable *crowdAudio =
                DataDir()->Find<RndAnimatable>("finalsequence_crowdaudio.anim", true);
            crowdAudio->Animate(0, false, 0);
        }
        if (mBeatCount < 16) {
            for (ObjDirItr<DepthBuffer3D> it(mBAMVisualizerPanel->DataDir(), true);
                 it != nullptr; ++it) {
                it->mPlayerPaletteTex.SetObjConcrete(mRecorder->GetPlayerPalette());
                it->SetShowing(true);
            }
            mRecorder->SetFreestyleMove(mFlashcardSlots.front());
            mRecorder->StopPlayback();
            mRecorder->StartPlayback(false);
        }
        // Score both players for the final sequence.
        // mPlayerScoreLeft/Right are ints reinterpreted as floats (codegen requirement).
        // The goto merges Perfect and SuperPerfect into a shared scoring path.
        if (mBeatCount > 0) {
            bool sentMsg = false;
            int player = 0;
            float *scores = (float *)&mPlayerScoreLeft;
            do {
                MoveRating rating = GetMoveRating(*scores);
                int side = TheGameData->Player(player)->Side();
                ShowMoveRating(rating, side);
                if (rating != kMoveRatingSuperPerfect) {
                    if (player != mMoveCreators[mFlashcardSlots.front()]) {
                        ((bool *)&mFlawlessFlags)[player] = false;
                    }
                    if (rating == kMoveRatingPerfect) {
                        int score = 40000;
                        goto scoreBlock;
                    }
                } else {
                    int score = 50000;
                scoreBlock:
                    IncreaseScore(player, score);
                    if (!sentMsg) {
                        static Message matchedMessage("bustamove_move_matched_finalsequence");
                        TheHamProvider->Handle(matchedMessage, false);
                    }
                    sentMsg = true;
                }
                player++;
                scores++;
            } while (player < 2);
        }
        if (mBeatCount == 11 && (mFinalSequenceType == 1 || mFinalSequenceType == 3)) {
            PlayVO(Symbol("nar_bam_finale_fast"));
        }
        mPlayerScoreLeft = 0;
        mPlayerScoreRight = 0;
        break;
    default:
        break;
    }

end_handling:
    // Trigger flashcard capture on beats 1-3 of recording measure 2.
    // Three separate ifs rather than a range check — required for codegen match.
    int loopTrigger = 0;
    bool isRecording = (mState == kBAMState_Recording);
    if (isRecording && mBeatCount == 2 && currentBeat == 1) {
        loopTrigger = 1;
    }
    if (isRecording && mBeatCount == 2 && currentBeat == 2) {
        loopTrigger = 2;
    }
    if (isRecording && mBeatCount == 2 && currentBeat == 3) {
        loopTrigger = 3;
    }

    if (loopTrigger != 0) {
        mCaptureTimer = 0.0f;
        mCaptureStep = loopTrigger;
    }
}

void BustAMovePanel::SetUpSongStructure(Symbol s) {
    mSongStructure.clear();
    BustAMoveData *pBAMData =
        TheGame->GetMoveDir()->Find<BustAMoveData>("BustAMoveData.bam", false);
    if (pBAMData) {
        for (int i = 0; i < pBAMData->PhraseSize(); i++) {
            for (int j = 0; j < pBAMData->PhraseAt(i)->count; j++) {
                int bars = pBAMData->PhraseAt(i)->bars;
                mSongStructure.push_back(bars);
            }
        }
    } else {
        for (int i = 0; i < 8; i++) {
            mSongStructure.push_back(4);
        }
        TheKnownIssues.Display("bustamove_wrong_song", 5.0f);
    }
    MILO_ASSERT(mSongStructure.size() >= 2, 0x62c);
    mCountInLength = mSongStructure[0];
    mRepsRemaining = mCountInLength + 4;
    float total = 0.0f;
    for (int i = 1; i < mSongStructure.size(); i++) {
        total += mSongStructure[i];
    }
    mLoopStartBeat = mCountInLength * 4.0f;
    mLoopEndBeat = total * 4.0f + mLoopStartBeat;
    TheMaster->GetAudio()->SetLoop(mLoopStartBeat, mLoopEndBeat);
}

#pragma fp_contract(off)
void BustAMovePanel::PlayIntroVO() {
    if (!mPlayIntroVO)
        return;
    mPlayIntroVO = false;
    float voLength = 0.0f;
    static Symbol nar_bam_intro("nar_bam_intro");
    static Message voLengthMsg("get_seq_length", 0);
    voLengthMsg[0] = nar_bam_intro;
    DataNode handled = mHUDPanel->Handle(voLengthMsg, true);
    if (handled != DATA_UNHANDLED) {
        voLength = handled.Float();
    }
    TempoMap *tempoMap = TheMaster->SongData()->GetTempoMap();
    float secondsPerBeat = 60.0f / tempoMap->GetTempoBPM(0);
    float introBeats = (float)(mSongStructure[0] * 4);
    TheGame->SetIntroRealTime(-(voLength - (introBeats * secondsPerBeat)));
    PlayVO(nar_bam_intro);
}
#pragma fp_contract(on)

void BustAMovePanel::Poll() {
    if (!InBustAMove())
        return;
    if (TheGamePanel->IsGameOver())
        return;

    HamPanel::Poll();

    HamPlayerData *player0 = TheGameData->Player(0);
    HamPlayerData *player1 = TheGameData->Player(1);
    player0->Provider()->Export(Message("hide_hud", 0), true);
    player1->Provider()->Export(Message("hide_hud", 0), true);

    mRecorder->Poll();

    int activePlayer = mActivePlayer;
    if (mState == kBAMState_PlayCountIn || mState == kBAMState_Playing
        || mState == kBAMState_ShowMove) {
        activePlayer = !mActivePlayer;
    }
    mCreatorSide = TheGameData->Player(activePlayer)->Side();
    const Skeleton *skel = TheGameData->Player(activePlayer)->GetSkeleton();
    int skelIdx = -1;
    if (skel != NULL) {
        skelIdx = skel->SkeletonIndex();
    }
    int forceSkelIdx = skelIdx;
    mRecorder->mSkeletonIndex = skelIdx;
    if (mState == kBAMState_Recording || kBAMState_CountIn == mState) {
        mRecordSkelIdx = skelIdx;
    }

    if (mState == kBAMState_Recording && mBeatCount >= 3) {
        mDancerTakeScore = mRecorder->GetScore(skelIdx, 0, mRecordScore, true);
        mCurrentMoveScore = mRecorder->GetScore(skelIdx, 1, mRecordScore, false);
        mRecordScore += TheTaskMgr.DeltaUISeconds();
    }

    if (mState == kBAMState_Playing) {
        auto currentMoveScore = mRecorder->GetScore(skelIdx, 0, -1.0f, false);
        mMoveScore = currentMoveScore;
        mPhraseMeters[mCreatorSide]->SetShowing(true);
        float base = mMoveScore;
        unsigned int e = 2;
        float scoreSq = 1.0f;
        do {
            if (e & 1) scoreSq *= base;
            e >>= 1;
            if (e == 0) break;
            base *= base;
        } while (true);
        mPhraseMeters[mCreatorSide]->SetRatingFrac(
            scoreSq * 1.4f, 4.0f - MsToBeat(mRecordScore * 1000.0f)
        );
        forceSkelIdx = mRecordSkelIdx;
    } else if (mState == kBAMState_ShowMoveSequence) {
        for (int p = 0; p < 2; p++) {
            int pSkelIdx = TheGestureMgr->GetSkeletonIndexByTrackingID(
                TheGameData->Player(p)->GetSkeletonTrackingID()
            );
            SkeletonSide pSide = TheGameData->Player(p)->Side();
            ((float *)&mPlayerScoreLeft)[p] =
                mRecorder->GetScore(pSkelIdx, p, -1.0f, false);
            mPhraseMeters[pSide]->SetShowing(true);
            float pBase = ((float *)&mPlayerScoreLeft)[p];
            unsigned int pE = 2;
            float pScoreSq = 1.0f;
            do {
                if (pE & 1) pScoreSq *= pBase;
                pE >>= 1;
                if (pE == 0) break;
                pBase *= pBase;
            } while (true);
            mPhraseMeters[pSide]->SetRatingFrac(
                pScoreSq * 1.4f, 4.0f - MsToBeat(mRecordScore * 1000.0f)
            );
        }
        forceSkelIdx = mRecorder->mTakes[mRecorder->mCurrentTakeIndex].unkc;
    } else {
        mPhraseMeters[0]->SetRatingFrac(0.0f, -1.0f);
        mPhraseMeters[1]->SetRatingFrac(0.0f, -1.0f);
        mPhraseMeters[0]->SetShowing(false);
        mPhraseMeters[1]->SetShowing(false);
    }

    if (mState == kBAMState_ShowMoveSequence) {
        RndTex *pinkTex =
            mBAMVisualizerPanel->DataDir()->Find<RndTex>("gradient_pink.tex", true);
        RndTex *blueTex =
            mBAMVisualizerPanel->DataDir()->Find<RndTex>("gradient_blue.tex", true);
        bool isPlayer0Pink = true;
        if (TheGameData->Player(0)->Side() != kSkeletonLeft
            || GetPlayerColor(0) != "pink") {
            isPlayer0Pink = false;
        }
        for (ObjDirItr<DepthBuffer3D> it(mBAMVisualizerPanel->DataDir(), true);
             it != nullptr; ++it) {
            bool isLeft = std::strstr(it->Name(), "_left") != NULL;
            if ((isLeft && isPlayer0Pink) || (!isLeft && !isPlayer0Pink)) {
                it->SetPlayerPalette(pinkTex);
            } else {
                it->SetPlayerPalette(blueTex);
            }
        }
        mDepthBufPlayer = -1;
    } else if (mDepthBufPlayer != activePlayer) {
        RndTex *tex;
        if (GetPlayerColor(activePlayer) == "pink") {
            tex = mBAMVisualizerPanel->DataDir()->Find<RndTex>("gradient_pink.tex", true);
        } else {
            tex = mBAMVisualizerPanel->DataDir()->Find<RndTex>("gradient_blue.tex", true);
        }
        for (ObjDirItr<DepthBuffer3D> it(mBAMVisualizerPanel->DataDir(), true);
             it != nullptr; ++it) {
            it->SetPlayerPalette(tex);
        }
        mDepthBufPlayer = activePlayer;
    }

    bool forceShow = !(mState == kBAMState_Recording || mState == kBAMState_End);
    for (ObjDirItr<DepthBuffer3D> it(mBAMVisualizerPanel->DataDir(), true);
         it != nullptr; ++it) {
        it->ForceDrawSkeletonIndex(forceSkelIdx, forceShow);
    }

    PollCaptureFlashcard();

    float streamMs = TheMaster->StreamMs();
    float beat = MsToBeat(streamMs);
    int currentBeat;
    if (beat > 0.0f) {
        currentBeat = (int)(beat + 0.5f);
    } else {
        currentBeat = (int)(beat - 0.5f);
    }
    if (currentBeat == mFailureEndBeat) {
        mFailureEndBeat = -1;
        static Message hideTransitionMsg("bustamove_hide_transition");
        TheHamProvider->Handle(hideTransitionMsg, false);
    }

    if (!(mNextVOTime > TheTaskMgr.Seconds(TaskMgr::kRealTime))) {
        PlayMovePromptVO();
        mNextVOTime = FLT_MAX;
    }

    if (DataVariable("bam_debug").Int()) {
        static DebugGraph scoreGraph(
            0.1f, 0.1f, 0.8f, 0.2f,
            Hmx::Color(1.0f, 1.0f, 1.0f, 1.0f),
            Hmx::Color(0.0f, 0.0f, 0.0f, 0.3f),
            100, 0.0f, 1.0f,
            String("")
        );
        scoreGraph.AddData(mMoveScore, false);
        scoreGraph.Draw();
        String stateName;
        switch (mState) {
        case kBAMState_CountIn: stateName = "kBAMState_CountIn"; break;
        case kBAMState_Recording: stateName = "kBAMState_Recording"; break;
        case kBAMState_Playing: stateName = "kBAMState_Playing"; break;
        case kBAMState_ShowMove: stateName = "kBAMState_ShowMove"; break;
        case kBAMState_PlayCountIn: stateName = "kBAMState_PlayCountIn"; break;
        case kBAMState_RecordCountIn: stateName = "kBAMState_RecordCountIn"; break;
        case kBAMState_FailureToBust: stateName = "kBAMState_FailureToBust"; break;
        case kBAMState_ShowMoveSequenceSetup: stateName = "kBAMState_ShowMoveSequenceSetup"; break;
        case kBAMState_ShowMoveSequence: stateName = "kBAMState_ShowMoveSequence"; break;
        case kBAMState_End: stateName = "kBAMState_End"; break;
        case kBAMState_None: stateName = "kBAMState_None"; break;
        }
        RndGraph *graph = RndGraph::GetOneFrame();
        Hmx::Color white(1.0f, 1.0f, 1.0f, 1.0f);
        Vector2 pos(0.1f, 0.05f);
        graph->AddScreenString(
            MakeString("State: %s  Reps left: %d", stateName, mRepsRemaining), pos, white
        );
        int remainingBeat = (int)(TheTaskMgr.Beat() + 0.5f);
        unsigned int currentPhrase = 0;
        while (currentPhrase < mSongStructure.size()) {
            remainingBeat -= mSongStructure[currentPhrase] * 4;
            if (remainingBeat < 0)
                break;
            currentPhrase++;
        }
        for (int i = 0; i < mSongStructure.size(); i++) {
            graph->AddScreenString(
                MakeString("%d", mSongStructure[i]),
                Vector2((float)i * 0.02f + 0.1f, 0.08f),
                (int)currentPhrase == i ? Hmx::Color(0.0f, 1.0f, 0.0f, 1.0f)
                                        : Hmx::Color(1.0f, 1.0f, 1.0f, 1.0f)
            );
        }
    }
}
