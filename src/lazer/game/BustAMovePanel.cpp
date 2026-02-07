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
#include "lazer/game/GameMode.h"
#include "meta_ham/AccomplishmentManager.h"
#include "lazer/meta_ham/HamPanel.h"
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
    : unk40(0), unk44(0), unk58(-1), unk5c(0), mHUDPanel(0), unk64(0), unk68(4), unk6c(0),
      unk70(10), unk80(0), unka0(1), unk92c(0), unk930(0), unk934(0), unk958(-1),
      unk95c(-1), unk968(-1), unk970(0), unk988(0), unk98c(0), unk99c(0), unk9a0(FLT_MAX),
      unk9b9(0), unk9bc(-1) {
    unk40 = new FreestyleMoveRecorder();
    unk40->AssignStaticInstance();
}

BustAMovePanel::~BustAMovePanel() { delete unk40; }

BEGIN_HANDLERS(BustAMovePanel)
    HANDLE_ACTION(beat, OnBeat())
    HANDLE_ACTION(cache_objects, CacheObjects())
    HANDLE_ACTION(set_up_song_structure, SetUpSongStructure(_msg->Sym(2)))
    HANDLE_ACTION(on_stream_jump, unk98c = true)
    HANDLE_ACTION(play_intro_vo, PlayIntroVO())
    HANDLE_SUPERCLASS(HamPanel)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(BustAMovePanel)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

void BustAMovePanel::Draw() {
    UIPanel::Draw();
    unk40->DrawDebug();
    if (unk934) {
        RndDir *renderer = DataDir()->Find<RndDir>("bustamove_flashcard_renderer");
        String flashcard(MakeString("flashcard%i.tex", unk84));
        RndTexRenderer *texRenderer =
            renderer->Find<RndTexRenderer>("TexRenderer.rndtex");
        int numPoses = 0;
        for (int i = 0; i < 3; i++) {
            if (unka4[i].Tracked()) {
                numPoses++;
                MILO_ASSERT(numPoses <= 2, 0x1BC);
                String pose(MakeString("pose%i.tex", numPoses));
                RndDir *renderer =
                    DataDir()->Find<RndDir>("bustamove_flashcard_renderer");
                RndTex *tex = renderer->Find<RndTex>(pose.c_str());
                TheHamDirector->PoseIconMan(&unka4[i], tex);
            }
        }
        if (numPoses == 0) {
            unk9b9 = true;
        }
        RndAnimatable *anim = renderer->Find<RndAnimatable>("num_poses.anim");
        anim->SetFrame(numPoses, 1);
        texRenderer->SetOutputTexture(DataDir()->Find<RndTex>(flashcard.c_str()));
        renderer->DrawShowing();
        unk934--;
    }
}

void BustAMovePanel::Enter() {
    HamPanel::Enter();
    mHUDPanel = 0;
    if (InBustAMove()) {
        CacheObjects();
        unk9b8 = true;
    }
}

void BustAMovePanel::Exit() {
    UIPanel::Exit();
    TheMaster->RemoveSink(this);
    if (unk40) {
        unk40->Free();
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
    label = mBAMColumns[side == 0]->Find<HamLabel>(MakeString("flashcard_%d.lbl", index));
    if (mState == kBAMState_ShowMoveSequence
        || mState == kBAMState_ShowMoveSequenceSetup) {
        label->SetTextToken(s3);
    } else {
        label->SetTextToken(gNullStr);
    }
}

DataArray *BustAMovePanel::GetMoveNameData(int index) {
    static Symbol bustamove_move_names("bustamove_move_names");
    MILO_ASSERT(index >= 0 && index < MAX_FREESTYLE_MOVES, 0x656);
    int nameIndex = unk93c[index];
    MILO_ASSERT(nameIndex >= 0 && nameIndex < mShuffledMoveNames.size(), 0x658);
    DataArray *arr = TheGamePanel->Property(bustamove_move_names)->Array();
    return arr->Array(nameIndex);
}

void BustAMovePanel::SetMovePrompt() {
    Symbol sym = GetMoveNameData(unk84)->Sym(0);
    mMovePromptLabel->SetTextToken(sym);
    UIColor *movePromptColor = DataDir()->Find<UIColor>("move_prompt.color");
    UIColor *playerColor =
        DataDir()->Find<UIColor>(MakeString("%s.color", GetPlayerColor(unk64)));
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
    Symbol s(gNullStr);
    if (i3 >= 0) {
        s = GetMoveNameData(i3)->Sym(1);
    }
    HamLabel *label =
        mBAMColumns[side]->Find<HamLabel>(MakeString("flashcard_name_%d.lbl", index));
    label->SetTextToken(s);
    label = mBAMColumns[side == 0]->Find<HamLabel>(
        MakeString("flashcard_name_%d.lbl", index)
    );
    if (mState == kBAMState_ShowMoveSequence
        || mState == kBAMState_ShowMoveSequenceSetup) {
        label->SetTextToken(s);
    } else {
        label->SetTextToken(gNullStr);
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
        RndAnimatable *anim =
            feedbackDir->Find<RndAnimatable>("move_flawless_right.anim");
        anim->Animate(0, 0, 0, nullptr, kEaseLinear, 0, false);
        moveFinishedMsg[1] = move_perfect;
    } else if (mr == kMoveRatingPerfect) {
        RndAnimatable *anim = feedbackDir->Find<RndAnimatable>("move_nice_right.anim");
        anim->Animate(0, 0, 0, nullptr, kEaseLinear, 0, false);
        moveFinishedMsg[1] = move_awesome;
    } else if (mr == kMoveRatingAwesome) {
        RndAnimatable *anim = feedbackDir->Find<RndAnimatable>("move_okay_right.anim");
        anim->Animate(0, 0, 0, nullptr, kEaseLinear, 0, false);
        moveFinishedMsg[1] = move_ok;
    } else if (mr == kMoveRatingOk) {
        moveFinishedMsg[1] = move_bad;
    }
    TheHamProvider->Handle(moveFinishedMsg, false);
}

void BustAMovePanel::SetRoundFailure() {
    static Message resultMessage("set_bustamove_result", 0, 0, 0);
    resultMessage[0] = unk64 == 0;
    resultMessage[1] = 0;
    resultMessage[2] = 0;
    TheHamProvider->Handle(resultMessage, false);
}

void BustAMovePanel::PlayMovePromptVO() {
    PlayVO(GetMoveNameData(unk84)->Sym(unka0 + 2));
}

float BustAMovePanel::GetMovePromptVOLength() {
    float len = 0;
    Symbol sym = GetMoveNameData(unk84)->Sym(unka0 + 2);
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
        //   while (local_6c != (DepthBuffer3D *)0x0) {
        //     DepthBuffer3D::SetGrooviness(local_6c,(float)dVar23);
        //     if (*(int *)(local_6c + 0x198) != 0) {
        //       *(undefined4 *)(*(int *)(local_6c + 0x194) + 4) = *(undefined4
        //       *)(local_6c + 400);
        //       *(undefined4 *)(*(int *)(local_6c + 400) + 8) = *(undefined4 *)(local_6c
        //       + 0x194);
        //     }
        //     *(undefined4 *)(local_6c + 0x198) = 0;
        //     ObjDirItr<>::operator++(aOStack_70);
        //   }
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
    unk44 = 0;
    unk6c = 0;
    unk84 = 0;
    unk64 = RandomInt(0, 2);
    unk7c = false;
    mHUDPanel = DataVariable("hud_panel").Obj<ObjectDir>();
    for (int i = 0; i < 4; i++) {
        String flashcard = MakeString("flashcard_slot%i.mat", i);
        RndMat *flashcardMat = DataDir()->Find<RndMat>(flashcard.c_str());
        RndTex *blank = DataDir()->Find<RndTex>("blank.tex");
        flashcardMat->SetDiffuseTex(blank);
    }
    mBAMColumns[kSkeletonRight] = DataDir()->Find<RndDir>("bustamove_column_right");
    mBAMColumns[kSkeletonLeft] = DataDir()->Find<RndDir>("bustamove_column_left");
    unk48.clear();
    unk50.clear();
    ResetScores();
    unk954 = 1;
    unk950 = 0;
    unk94c = 0;
    mPhraseMeters[kSkeletonRight] = DataDir()->Find<HamPhraseMeter>("phrase_meter_right");
    mPhraseMeters[kSkeletonLeft] = DataDir()->Find<HamPhraseMeter>("phrase_meter_left");
    unk9a0 = FLT_MAX;
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
    Symbol song = MetaPerformer::Current()->GetSong();
    unk9bc = -1;
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
    int reps = unk988;
    float beatsToWait = (float)((reps * 4) - 4);
    float timeOffset = beatsToWait * secondsPerBeat;
    float currentTime = TheTaskMgr.Seconds(TaskMgr::kRealTime);
    unk9a0 = currentTime + timeOffset - voLength - 1.0f;
}

void BustAMovePanel::PollCaptureFlashcard() {
    if (unk92c != 0) {
        float flashcardTweak = 0.17f;
        if (DataVarExists("flashcard_tweak")) {
            flashcardTweak = DataVariable("flashcard_tweak").Float();
        }
        if (unk930 >= flashcardTweak) {
            BaseSkeleton *liveSkel = unk40->GetLiveSkeleton();
            if (liveSkel != nullptr) {
                unka4[unk92c - 1].Set(*unk40->GetLiveSkeleton());
            } else {
                unka4[unk92c - 1].SetTracked(false);
            }
            if (unk92c == 3) {
                float score1 =
                    unk40->CompareSkeletonPositions(&unka4[0], &unka4[1], 1.0f);
                float score2 =
                    unk40->CompareSkeletonPositions(&unka4[0], &unka4[2], 1.0f);
                if (score2 < 0.5f) {
                    unka4[1].SetTracked(false);
                } else {
                    unka4[2].SetTracked(false);
                }
                if (score1 >= 0.5f) {
                    unka4[1].SetTracked(false);
                }
                unk934 = 4;
            }
            unk92c = 0;
        } else {
            unk930 += TheTaskMgr.DeltaUISeconds();
        }
    }
}

void BustAMovePanel::AnimateFlashcard(int index) {
    int slot = index + 1;
    UIColor *uiColor = DataDir()->Find<UIColor>(MakeString("color%d.color", slot), true);
    Hmx::Color color = uiColor->GetColor();

    // Set flashcard.mat texture
    RndMat *flashcardMat = DataDir()->Find<RndMat>("flashcard.mat", true);
    String texName(MakeString("flashcard%i.tex", index));
    RndTex *tex = DataDir()->Find<RndTex>(texName.c_str(), true);
    flashcardMat->SetDiffuseTex(tex);

    // Set flashcard_capture_background.mat color
    RndMat *capBgMat =
        DataDir()->Find<RndMat>("flashcard_capture_background.mat", true);
    capBgMat->SetColor(color.red, color.green, color.blue);

    // Set flashcard_capture.lbl text
    HamLabel *capLabel = DataDir()->Find<HamLabel>("flashcard_capture.lbl", true);
    capLabel->SetTextToken(GetMoveNameData(index)->Sym(1));

    // Set flashcard_slot mat and texture
    String slotMatName(MakeString("flashcard_slot%i.mat", index));
    RndMat *slotMat = DataDir()->Find<RndMat>(slotMatName.c_str(), true);
    RndTex *slotTex = DataDir()->Find<RndTex>(texName.c_str(), true);
    slotMat->SetDiffuseTex(slotTex);

    // Set flashcard_slot_background mat color
    String slotBgName(MakeString("flashcard_slot_background%i.mat", index));
    RndMat *slotBgMat = DataDir()->Find<RndMat>(slotBgName.c_str(), true);
    slotBgMat->SetColor(color.red, color.green, color.blue);

    // Set flashcard_slot label text
    String slotLblName(MakeString("flashcard_slot%i.lbl", index));
    HamLabel *slotLabel = DataDir()->Find<HamLabel>(slotLblName.c_str(), true);
    slotLabel->SetTextToken(GetMoveNameData(index)->Sym(1));

    // Play capture animation
    RndPropAnim *captureAnim =
        DataDir()->Find<RndPropAnim>("capture_flashcard.anim", true);
    captureAnim->Animate(0, false, 0, nullptr, kEaseLinear, 0, false);
}

void BustAMovePanel::AdvanceFlashcards() {
    // Stop and restart advance.anim for each column
    for (int i = 0; i < 2; i++) {
        RndPropAnim *anim =
            mBAMColumns[i]->Find<RndPropAnim>("advance.anim", true);
        anim->StopAnimation();
        anim->SetFrame(0.0f, 1.0f);
    }

    int side = unka0;

    // Pop front of unk48 (Symbol list)
    if (unk48.begin() != unk48.end()) {
        unk48.erase(unk48.begin());
    }

    // Set flashcard text for 4 slots
    std::list<Symbol>::iterator symIt = unk48.begin();
    for (int i = 0; i < 4; i++) {
        if (symIt != unk48.end()) {
            SetFlashcardText(side, i, *symIt);
            ++symIt;
        } else {
            SetFlashcardText(side, i, Symbol(gNullStr));
        }
    }

    // Pop front of unk50 (int list)
    if (unk50.begin() != unk50.end()) {
        unk50.erase(unk50.begin());
    }

    // Set flashcard image and name for 4 slots
    std::list<int>::iterator intIt = unk50.begin();
    for (int i = 0; i < 4; i++) {
        int val = -1;
        if (intIt != unk50.end()) {
            val = *intIt;
            ++intIt;
        }
        SetFlashcardImage(side, i, val);
        SetFlashcardName(side, i, val);
    }
}

int BustAMovePanel::RepsToNextPhrase() {
    int beat = (int)(TheTaskMgr.Beat() + 0.5f);
    if (unk98c) {
        int loopEnd;
        TheMaster->GetAudio()->GetCurrLoopBeats(beat, loopEnd);
    }

    int *data = &mSongStructure[0];
    unsigned int count = 0;
    int size = (int)((mSongStructure.end() - mSongStructure.begin()));
    int repsInPhrase;

    if (size != 0) {
        int byteOfs = 0;
loop_top:
        beat -= *(int *)((char *)data + byteOfs) * 4;
        if (beat >= 0) {
            count++;
            byteOfs += 4;
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
    RndMat *otherBgMat =
        mBAMColumns[side == 0]->Find<RndMat>(MakeString("flashcard_background%d.mat", index));

    if (mState == kBAMState_ShowMoveSequence || mState == kBAMState_ShowMoveSequenceSetup) {
        otherFlashcardMat->SetDiffuseTex(flashcardTex);
        otherBgMat->SetColor(color.red, color.green, color.blue);
        otherBgMat->SetDiffuseTex(bgTex);
    } else {
        otherFlashcardMat->SetDiffuseTex(blankTex);
        otherBgMat->SetDiffuseTex(blankTex);
    }
}

void BustAMovePanel::OnBeat() {
    if (!InBustAMove())
        return;
    if (TheGamePanel->IsGameOver())
        return;

    Symbol beat("beat");
    static int sLastBeat = -1;
    int currentBeat = TheHamProvider->Property(beat, true)->Int();
    if (currentBeat == sLastBeat)
        return;
    sLastBeat = currentBeat;

    // beat 4 handling
    if (currentBeat == 4) {
        // Animate advance.anim for each column
        for (int i = 0; i < 2; i++) {
            RndPropAnim *anim =
                mBAMColumns[i]->Find<RndPropAnim>("advance.anim", true);
            anim->Animate(0.0f, false, 0.0f, nullptr, kEaseLinear, 0.0f, false);
        }

        if (mState == kBAMState_Recording) {
            if (unk44 == 3) {
                static Message endMessage("bustamove_end_create");
                TheHamProvider->Handle(endMessage, false);
            }

            if (!unk970) {
                if (unk84 == 0) {
                    switch ((unsigned int)unk44) {
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
                    switch ((unsigned int)unk44) {
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

            if (unk970 && unk44 < 3) {
                int beatNum = (int)(TheTaskMgr.Beat() + 0.5f) + 1;
                static Message countInMsg(Symbol("mulligan_count"), 0);
                countInMsg[0] = beatNum;
                Handle(countInMsg, true);
            }
        }

        if (mState == kBAMState_RecordCountIn && unk970 && unk988 == 1) {
            int beatNum = (int)(TheTaskMgr.Beat() + 0.5f) + 1;
            static Message countInMsg(Symbol("mulligan_count"), 0);
            countInMsg[0] = beatNum;
            Handle(countInMsg, true);
        }
    }

    if (currentBeat != 1)
        goto end_handling;

    unk40->ClearFrameScores();

    BAMState nextState = kBAMState_None;
    if (unk70 != kBAMState_None) {
        nextState = (BAMState)unk70;
        unk70 = kBAMState_None;
    } else if ((unsigned int)mState <= (unsigned int)kBAMState_ShowMoveSequence) {
        switch (mState) {
        case kBAMState_CountIn:
            if (unk44 == unk68 + 3)
                nextState = kBAMState_Recording;
            break;
        case kBAMState_Recording:
            if (unk44 == 3) {
                // Iterate DepthBuffer3Ds, disable grooviness
                for (ObjDirItr<DepthBuffer3D> it(mBAMVisualizerPanel->DataDir(), true);
                     it != nullptr; ++it) {
                    it->SetShowing(false);
                }
                unk40->StopPlayback();
                TheDebug << MakeString(
                    "1: %f(%d)  2: %f(%d)\n",
                    unk974,
                    unk40->GetDancerTakeFrameCount(),
                    unk978,
                    unk40->GetCurrentMoveNumFrames()
                );
                // Evaluate recording success
                if (unk9b9) {
                    unk7c = false;
                } else {
                    unk7c = unk974 > 0.65f && unk978 > 0.65f;
                }
                if (unk7c) {
                    (&unk9a8)[unk84] = unk64;
                    unk84++;
                    IncreaseScore(unk64, 200000);
                    AnimateFlashcard(unk84 - 1);
                    nextState = kBAMState_ShowMove;
                    static Message createdMessage("bustamove_move_created");
                    TheHamProvider->Handle(createdMessage, false);
                } else {
                    nextState = kBAMState_FailureToBust;
                }
            }
            break;
        case kBAMState_Playing:
            if (unk44 >= 3) {
                Symbol stay_on_bam_play("stay_on_bam_play");
                if (DataVariable(stay_on_bam_play).Int() == 0)
                    nextState = kBAMState_RecordCountIn;
            }
            break;
        case kBAMState_ShowMove:
            nextState = kBAMState_PlayCountIn;
            break;
        case kBAMState_PlayCountIn:
            if (unk988 == 1)
                nextState = kBAMState_Playing;
            break;
        case kBAMState_RecordCountIn:
            if (unk988 == 1)
                nextState = kBAMState_Recording;
            break;
        case kBAMState_FailureToBust:
            if (unk44 == 1)
                nextState = kBAMState_RecordCountIn;
            break;
        case kBAMState_ShowMoveSequenceSetup:
            if (unk988 == 1)
                nextState = kBAMState_ShowMoveSequence;
            break;
        case kBAMState_ShowMoveSequence:
            if (unk44 == 0xf)
                nextState = kBAMState_End;
            break;
        default:
            break;
        }
    }

    unk44++;
    if (unk988 > 0)
        unk988--;

    mStatusLabel->SetTextToken(gNullStr);
    mMovePromptLabel->SetTextToken(gNullStr);

    AdvanceFlashcards();

    if (nextState != kBAMState_None) {
        mState = nextState;
        unk44 = 0;
        unk988 = RepsToNextPhrase();
    }

    if (unk98c) {
        unk988 = RepsToNextPhrase();
        unk98c = false;
    }

    unk40->SetVal44(unk44);

    switch (mState) {
    case kBAMState_CountIn:
        if (unk44 == 1) {
            SetUpMoveNames();
            int *piVar11 = &mShuffledMoveNames[0];
            unsigned int count = 0;
            if (mShuffledMoveNames.size() != 0) {
                do {
                    unk93c[unk84] = mShuffledMoveNames[unk99c];
                    unsigned int size = mShuffledMoveNames.size();
                    unsigned int nextIdx = unk99c + 1;
                    unk99c = nextIdx % size;
                    DataArray *arr = GetMoveNameData(0);
                    int clipExists = arr->Node(4).Int(arr);
                    if (clipExists != 0)
                        break;
                    count++;
                } while (count < mShuffledMoveNames.size());
            }
        }
        if (unk44 == unk68 - 2) {
            Flow *flow = DataDir()->Find<Flow>("intro.flow", true);
            flow->Activate();
            QueueMovePromptVO();
        }
        if (unk44 == unk68 - 1) {
            unk50.push_back(-1);
            unk50.push_back(-1);
            unk50.push_back(-1);
            unk50.push_back(-1);
            unk50.push_back(-1);
            unk50.push_back(-2);
            unk50.push_back(-2);
            unk50.push_back(-2);
            unk50.push_back(-2);
            unk48.push_back(gNullStr);
            unk48.push_back(gNullStr);
            unk48.push_back(gNullStr);
            unk48.push_back(gNullStr);
            unk48.push_back(gNullStr);
            unk48.push_back(Symbol("bam_record1"));
            unk48.push_back(Symbol("bam_record2"));
            unk48.push_back(Symbol("bam_record3"));
            unk48.push_back(Symbol("bam_record4"));
            CountIn(16);
        }
        if (unk988 == 2 || unk988 == 1) {
            SetMovePrompt();
        }
        if (unk988 != 2)
            break;
        ShowGetReadyCard(Symbol("get_ready"), (SkeletonSide)unka0);
        break;
    case kBAMState_Recording: {
        if (unk44 == 0) {
            unk9b9 = false;
            for (ObjDirItr<DepthBuffer3D> it(mBAMVisualizerPanel->DataDir(), true);
                 it != nullptr; ++it) {
                it->unk18c.SetObjConcrete(NULL);
                it->SetShowing(true);
            }
        } else {
            for (ObjDirItr<DepthBuffer3D> it(mBAMVisualizerPanel->DataDir(), true);
                 it != nullptr; ++it) {
                if (std::strstr(it->Name(), "live")) {
                    it->SetShowing(DataVariable(Symbol("hide_bam_ghost")).Int() == 0);
                    it->unk18c.SetObjConcrete(NULL);
                } else {
                    it->unk18c.SetObjConcrete(unk40->GetPlayerPalette());
                }
            }
        }
        if (unk44 == 0) {
            unk40->StopPlayback();
            unk40->SetFreestyleMove(unk84);
            static Message startMessage("bustamove_start_create", 0);
            startMessage[0] = DataNode(unka0);
            TheHamProvider->Handle(startMessage, false);
            unk40->ClearRecording();
            unk40->StartRecording();
        }
        if (unk44 == 1) {
            unk40->ClearDancerTake();
            unk40->StartRecordingDancerTake();
            unk40->StartPlayback(true);
        }
        if (unk44 == 2) {
            unk40->StartRecording();
            unk40->StopPlayback();
            unk40->StartPlayback(true);
        }
        if (unk44 == 3) {
            unk40->StopRecording();
            unk40->StopPlayback();
            unk40->StartPlayback(true);
        }
        unk80 = 0.0f;
        unk5c = 0.0f;
        break;
    }
    case kBAMState_Playing: {
        for (ObjDirItr<DepthBuffer3D> it(mBAMVisualizerPanel->DataDir(), true);
             it != nullptr; ++it) {
            it->SetShowing(true);
        }
        if (unk44 == 0) {
            unk40->StopRecording();
            unk6c = 0;
        } else {
            unk40->StopPlayback();
            MoveRating rating = GetMoveRating(unk5c);
            ShowMoveRating(rating, unka0);
            if (rating == kMoveRatingSuperPerfect
                || (((bool *)&unk9a4)[!unk64] = false,
                    rating == kMoveRatingPerfect)) {
                unk6c++;
                int score =
                    (rating == kMoveRatingSuperPerfect) ? 50000 : 40000;
                IncreaseScore(!unk64, score);
                static Message playMsg("bustamove_move_matched", 0);
                playMsg[0] = DataNode(unk6c);
                TheHamProvider->Handle(playMsg, false);
            }
        }
        unk5c = 0.0f;
        unk40->StartPlayback(false);
        for (ObjDirItr<DepthBuffer3D> it(mBAMVisualizerPanel->DataDir(), true);
             it != nullptr; ++it) {
            it->unk18c.SetObjConcrete(unk40->GetPlayerPalette());
        }
        break;
    }
    case kBAMState_ShowMove: {
        for (ObjDirItr<DepthBuffer3D> it(mBAMVisualizerPanel->DataDir(), true);
             it != nullptr; ++it) {
            it->SetShowing(false);
        }
        unk40->StopRecording();
        unk40->StartPlayback(false);
        MILO_ASSERT(unk44 == 0, 0x328);
        unk48.push_back(gNullStr);
        break;
    }
    case kBAMState_PlayCountIn: {
        for (ObjDirItr<DepthBuffer3D> it(mBAMVisualizerPanel->DataDir(), true);
             it != nullptr; ++it) {
            it->SetShowing(false);
        }
        unk40->StopPlayback();
        if (unk988 > 3) {
            unk48.push_back(gNullStr);
        }
        if (unk988 == 3) {
            unk50.push_back(-1);
            unk50.push_back(-1);
            unk50.push_back(-1);
            unk50.push_back(unk84 - 1);
            unk50.push_back(unk84 - 1);
            unk50.push_back(unk84 - 1);
            unk50.push_back(unk84 - 1);
            unk48.push_back(gNullStr);
            unk48.push_back(gNullStr);
            unk48.push_back(gNullStr);
            unk48.push_back(gNullStr);
            unk48.push_back(gNullStr);
            unk48.push_back(gNullStr);
            unk48.push_back(gNullStr);
            const char *sideStr =
                (SkeletonSide)unka0 == kSkeletonLeft ? "left" : "right";
            PlayVO(Symbol(MakeString("nar_bam_%s_needstorepeat", sideStr)));
            CountIn(8);
        }
        if (unk988 != 2)
            break;
        ShowGetReadyCard(Symbol("get_ready_to_dance"), (SkeletonSide)unka0);
        break;
    }
    case kBAMState_RecordCountIn: {
        for (ObjDirItr<DepthBuffer3D> it(mBAMVisualizerPanel->DataDir(), true);
             it != nullptr; ++it) {
            it->unk18c.SetObjConcrete(NULL);
            it->SetShowing(true);
        }
        if (unk44 == 0) {
            unk93c[unk84] = mShuffledMoveNames[unk99c];
            unsigned int nextIdx = unk99c + 1;
            unk970 = false;
            unsigned int size = mShuffledMoveNames.size();
            unk99c = nextIdx % size;
            if (unk7c) {
                unk40->PlaybackComplete();
                MoveRating rating = GetMoveRating(unk5c);
                ShowMoveRating(rating, unka0);
                if (rating == kMoveRatingSuperPerfect
                    || (((bool *)&unk9a4)[!unk64] = false,
                        rating == kMoveRatingPerfect)) {
                    unk6c++;
                    int score =
                        (rating == kMoveRatingSuperPerfect) ? 50000 : 40000;
                    IncreaseScore(!unk64, score);
                    static Message matchedMessage("bustamove_move_matched", 0);
                    matchedMessage[0] = DataNode(unk6c);
                    TheHamProvider->Handle(matchedMessage, false);
                }
                if (unk6c > 0) {
                    static Message successMessage("bustamove_successfully_matched");
                    TheHamProvider->Handle(successMessage, false);
                    mStatusLabel->SetTextToken(Symbol("bam_matched"));
                } else if (unk6c == 0) {
                    SetRoundFailure();
                    Symbol failed("bam_failed");
                    mStatusLabel->SetTextToken(failed);
                    HamPlayerData *playerData = TheGameData->Player(unk64);
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
                int *pRetries = &(&unk94c)[unk64];
                int retries = *pRetries;
                if (retries < unk954) {
                    *pRetries = retries + 1;
                    unk970 = true;
                }
            }
            if (unk7c || !unk970) {
                unk64 = !unk64;
            }
            unk5c = 0.0f;
            if (unk84 == 4) {
                unk70 = kBAMState_ShowMoveSequenceSetup;
                unk48.push_back(gNullStr);
            }
            if (unk958 != -1.0f) {
                TheMaster->GetAudio()->SetLoop(unk958, unk95c);
            }
        }
        if (unk988 == 4 && unk84 != 4) {
            QueueMovePromptVO();
            unk50.push_back(-1);
            unk50.push_back(-1);
            unk50.push_back(-1);
            unk50.push_back(-1);
            unk50.push_back(-2);
            unk50.push_back(-2);
            unk50.push_back(-2);
            unk50.push_back(-2);
            unk48.push_back(gNullStr);
            unk48.push_back(gNullStr);
            unk48.push_back(gNullStr);
            unk48.push_back(gNullStr);
            unk48.push_back(Symbol("bam_record1"));
            unk48.push_back(Symbol("bam_record2"));
            unk48.push_back(Symbol("bam_record3"));
            unk48.push_back(Symbol("bam_record4"));
        }
        if (unk988 == 3) {
            CountIn(8);
        }
        if (unk988 == 2 || unk988 == 1) {
            SetMovePrompt();
        }
        if (unk988 != 2)
            break;
        ShowGetReadyCard(Symbol("get_ready"), (SkeletonSide)unka0);
        break;
    }
    case kBAMState_FailureToBust:
        if (unk44 == 0) {
            static Message failMessage("bustamove_fail_bust");
            TheHamProvider->Handle(failMessage, false);
            unk6c = 0;
            if (!unk970) {
                PlayVO(Symbol("nar_bam_gen_fail"));
            } else {
                const char *sideStr =
                    (SkeletonSide)unka0 == kSkeletonLeft ? "left" : "right";
                PlayVO(Symbol(MakeString("nar_bam_gen_second_fail_%s", sideStr)));
            }
            float beat = MsToBeat(TheMaster->StreamMs());
            int beatInt;
            if (beat > 0.0f) {
                beatInt = (int)(beat + 0.5f);
            } else {
                beatInt = (int)(beat - 0.5f);
            }
            float beatF = (float)beatInt;
            TheMaster->GetAudio()->SetLoop(beatF, beatF + 8.0f);
            float beat2 = MsToBeat(TheMaster->StreamMs());
            int beat2Int;
            if (beat2 > 0.0f) {
                beat2Int = (int)(beat2 + 0.5f);
            } else {
                beat2Int = (int)(beat2 - 0.5f);
            }
            unk968 = beat2Int + 7;
        }
        break;
    case kBAMState_End:
        if (unk44 == 0) {
            static Symbol score("score");
            int score0 =
                TheGameData->Player(0)->Provider()->Property(score)->Int();
            int score1 =
                TheGameData->Player(1)->Provider()->Property(score)->Int();
            int winner = -1;
            if (score0 > score1) {
                winner = 0;
            }
            if (score1 > score0) {
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
                    it->unk18c.SetObjConcrete(NULL);
                }
                RndAnimatable *numPlayers =
                    DataDir()->Find<RndAnimatable>("num_players.anim", true);
                numPlayers->SetFrame(1.0f, 1.0f);
                RndAnimatable *vizNumPlayers =
                    mBAMVisualizerPanel->DataDir()->Find<RndAnimatable>("num_players.anim", true);
                vizNumPlayers->SetFrame(1.0f, 1.0f);
                unk64 = winner;
            } else {
                for (ObjDirItr<DepthBuffer3D> it(mBAMVisualizerPanel->DataDir(), true);
                     it != nullptr; ++it) {
                    it->SetShowing(false);
                }
            }
            {
                int i = 0;
                do {
                    if (((bool *)&unk9a4)[i]) {
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
        if (unk44 == 3) {
            TheGamePanel->SetGameOver(true);
            TheMaster->GetAudio()->SetPaused(true);
        }
        break;
    case kBAMState_ShowMoveSequenceSetup:
        if (unk44 == 0) {
            for (ObjDirItr<DepthBuffer3D> it(mBAMVisualizerPanel->DataDir(), true);
                 it != nullptr; ++it) {
                it->SetShowing(false);
            }
        }
        if (unk988 > 3) {
            unk48.push_back(gNullStr);
        }
        if (unk988 == 3) {
            static Message bothMessage("bustamove_both_dance");
            TheHamProvider->Handle(bothMessage, false);
            PlayVO(Symbol("nar_bam_trans"));
            unk48.push_back(gNullStr);
            unk48.push_back(gNullStr);
            unk48.push_back(gNullStr);
            unk50.push_back(-1);
            unk50.push_back(-1);
            unk50.push_back(-1);
            Symbol stay_on_bam_play_sym("bam_final_sequence");
            unk96c = DataVariable(stay_on_bam_play_sym).Int();
            if (unk96c == 0) {
                unk96c = 1;
            }
            switch (unk96c) {
            case 3: {
                std::vector<int> shuffled1;
                GetShuffledInts(shuffled1, 4);
                std::vector<int> shuffled2;
                GetShuffledInts(shuffled2, 4);
                if (shuffled1[3] == shuffled2[0]) {
                    int *p = &shuffled1[0];
                    int n = 4;
                    do {
                        unk50.push_back(*p);
                        unk50.push_back(*p);
                        p++;
                        n--;
                    } while (n != 0);
                }
                {
                    int *p = &shuffled2[0];
                    int n = 2;
                    do {
                        unk50.push_back(*p);
                        unk50.push_back(*p);
                        p++;
                        n--;
                    } while (n != 0);
                }
                {
                    int k = 0;
                    do {
                        unk50.push_back(shuffled2[(k + 2) % 4]);
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
                        unk50.push_back(*p);
                        unk50.push_back(*p);
                        p++;
                        n--;
                    } while (n != 0);
                }
                {
                    int *p = &shuffled1[0];
                    int n = 4;
                    do {
                        unk50.push_back(*p);
                        unk50.push_back(*p);
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
                        unk50.push_back(shuffled1[r]);
                        n--;
                    } while (n != 0);
                }
                {
                    int *p = &shuffled1[0];
                    int n = 4;
                    do {
                        unk50.push_back(*p);
                        unk50.push_back(*p);
                        p++;
                        n--;
                    } while (n != 0);
                }
                {
                    int k = 0;
                    do {
                        unk50.push_back(shuffled1[(k + 2) % 4]);
                        k++;
                    } while (k < 4);
                }
                break;
            }
            }
            CountIn(8);
        }
        if (unk988 != 2)
            break;
        ShowGetReadyCard(Symbol("get_ready"), kSkeletonLeft);
        ShowGetReadyCard(Symbol("get_ready"), kSkeletonRight);
        break;
    case kBAMState_ShowMoveSequence:
        if (unk44 == 0) {
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
            crowdAudio->Animate(0, false, 0, nullptr, kEaseLinear, 0, false);
        }
        if (unk44 < 16) {
            for (ObjDirItr<DepthBuffer3D> it(mBAMVisualizerPanel->DataDir(), true);
                 it != nullptr; ++it) {
                it->unk18c.SetObjConcrete(unk40->GetPlayerPalette());
                it->SetShowing(true);
            }
            unk40->SetFreestyleMove(unk50.front());
            unk40->StopPlayback();
            unk40->StartPlayback(false);
        }
        if (unk44 > 0) {
            bool sentMsg = false;
            int player = 0;
            float *scores = (float *)&unk90;
            do {
                MoveRating rating = GetMoveRating(*scores);
                ShowMoveRating(rating, TheGameData->Player(player)->Side());
                if (rating != kMoveRatingSuperPerfect) {
                    if (player != (&unk9a8)[unk50.front()]) {
                        ((bool *)&unk9a4)[player] = false;
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
        if (unk44 == 11 && (unk96c == 1 || unk96c == 3)) {
            PlayVO(Symbol("nar_bam_finale_fast"));
        }
        unk90 = 0;
        unk94 = 0;
        break;
    default:
        break;
    }

end_handling:
    // End handling - check for recording trigger
    int loopTrigger = 0;
    bool isRecording = (mState == kBAMState_Recording);
    if (isRecording && unk44 == 2 && currentBeat == 1) {
        loopTrigger = 1;
    }
    if (isRecording && unk44 == 2 && currentBeat == 2) {
        loopTrigger = 2;
    }
    if (isRecording && unk44 == 2 && currentBeat == 3) {
        loopTrigger = 3;
    }

    if (loopTrigger != 0) {
        unk930 = 0.0f;
        unk92c = loopTrigger;
    }
}
