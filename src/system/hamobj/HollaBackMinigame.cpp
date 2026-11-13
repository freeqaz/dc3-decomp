#include "hamobj\HollaBackMinigame.h"
#include "MoveMgr.h"
#include "flow\Flow.h"
#include "flow\PropertyEventProvider.h"
#include "gesture\BaseSkeleton.h"
#include "hamobj\HamDirector.h"
#include "hamobj\HamGameData.h"
#include "hamobj\HamLabel.h"
#include "hamobj\HamMaster.h"
#include "hamobj\HamMove.h"
#include "hamobj\HamPlayerData.h"
#include "hamobj\MoveDir.h"
#include "math/Easing.h"
#include "midi\MidiParserMgr.h"
#include "obj\Data.h"
#include "obj\Dir.h"
#include "obj\Msg.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "rndobj\Dir.h"
#include "rndobj\Poll.h"
#include "rndobj\PropAnim.h"
#include "synth\SynthSample.h"
#include "ui\PanelDir.h"
#include "ui\UIPanel.h"
#include "utl/Loader.h"
#include "utl\Std.h"
#include "utl\TimeConversion.h"

// Global int (== 4) defined in RhythmDetectorGroup's translation unit (.data:0xF8,
// 0x82F0E8A4). OnBeat compares subMeasure against it; the original source name is
// unknown, so reference it as the linker label per the project convention.
extern int lbl_82F0E8A4;

#ifndef HX_NATIVE
// w8-b: orphan-instantiation anchor, and the body MUST be an ASSIGNMENT.
// 0x824EAD60 is an ICF fold of two _M_allocate_and_copy spellings: map 45453 has
// the PAVSymbol (non-const source) one in hamobj:HollaBackMinigame.obj, map 45452
// the PBVSymbol (const source) one in hamobj:HamNavList.obj, both at 0x824EAD60.
// symbols.txt names the folded address with the PBV spelling, so this object has
// to define the CONST-source instantiation to pair at all.  Refuted spelling:
// `std::vector<Symbol> copy(src);` emits only the PAV variant and left the row at
// 0%.  stl/_vector.c:170 is why -- `operator=` is the one path that reaches
// _M_allocate_and_copy through __CONST_CAST(const_pointer, __x._M_start);
// reserve() and the copy ctor both pass the non-const _M_start.
void Dc3W8bHollaProbe(const std::vector<Symbol> &src, std::vector<Symbol> &dst) { dst = src; }
#endif

void JumpToMeasure(float beat) {
    float ms = BeatToMs(beat * 4.0f);
    Hmx::Object *game = ObjectDir::Main()->Find<Hmx::Object>("game", true);
    if (game) {
        game->Handle(Message("jump", ms), true);
    }
}

HollaBackMinigame::HollaBackMinigame()
    : mActive(0), mSpecifyFirstMoveMeasure(-1), mInitialMoveCount(2), mWinLoopBeat(0), mPendingMusicJump(0),
      mSound(0) {}

HollaBackMinigame::~HollaBackMinigame() { EndMinigame(true); }

BEGIN_HANDLERS(HollaBackMinigame)
    HANDLE_ACTION(begin_minigame, BeginMinigame(_msg->Array(2)))
    HANDLE_ACTION(end_minigame, EndMinigame(false))
    HANDLE_ACTION(beat, OnBeat())
    HANDLE_EXPR(get_move_state, GetMoveState(_msg->Int(2)))
    HANDLE_ACTION(set_move_state, SetMoveState(_msg->Int(2), _msg->Sym(3)))
    HANDLE_ACTION(set_default_shot, SetDefaultShot())
    HANDLE_EXPR(get_first_move_idx, mFirstMoveIdx + 5)
    HANDLE_SUPERCLASS(RndPollable)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(HollaBackMinigame)
    SYNC_SUPERCLASS(RndPollable)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(HollaBackMinigame)
    SAVE_REVS(5, 0)
    SAVE_SUPERCLASS(RndPollable)
END_SAVES

BEGIN_COPYS(HollaBackMinigame)
    COPY_SUPERCLASS(RndPollable)
    CREATE_COPY(HollaBackMinigame)
END_COPYS

INIT_REVS(5, 0)

BEGIN_LOADS(HollaBackMinigame)
    LOAD_REVS(bs)
    ASSERT_REVS(5, 0)
    LOAD_SUPERCLASS(RndPollable)
END_LOADS

void HollaBackMinigame::Poll() {
    if (!TheLoadMgr.EditMode() && mActive) {
        RndPropAnim *anim =
            TheHamDirector->GetVenueWorld()->Find<RndPropAnim>("set_bid.anim", true);
        if (anim) {
            anim->Animate(0, false, 0, nullptr, kEaseLinear, 0, false);
        }

        if (mSound && !mSound->IsPlaying()) {
            EndShoutOut();
        }
        if (mPendingMusicJump) {
            Hmx::Object *game = ObjectDir::Main()->Find<Hmx::Object>("game", true);
            bool b20 = !game ? false : !game->Handle(Message("is_waiting"), true).Int();
            if (b20 && mPendingMusicJump) {
                JumpToMeasure(mInitialMusicJump);
                TheMaster->GetAudio()->SetPaused(true);
                mPendingMusicJump = false;
            }
        }
        if (mState == 0) {
            static Symbol holla_back_stage("holla_back_stage");
            static Symbol exit_title("exit_title");
            static Symbol start_score_move_index("start_score_move_index");
            static Symbol hide_hud("hide_hud");
            for (int i = 0; i < 2; i++) {
                HamPlayerData *hpd = TheGameData->Player(i);
                if (!hpd->IsPlaying()) {
                    hpd->Provider()->SetProperty(start_score_move_index, 1000);
                    hpd->Provider()->Export(Message(hide_hud, 0), true);
                    mHUDPanel
                        ->Find<RndPropAnim>(
                            hpd->Side() == kSkeletonRight ? "player_1_hud.anim"
                                                          : "player_2_hud.anim",
                            true
                        )
                        ->SetFrame(0, 1);
                }
            }
            if (!mIntroStarted && TheMaster->GetAudio()->IsReady() && mStartCountdown-- <= 0) {
                mIntroStarted = true;
                TheHamProvider->SetProperty(holla_back_stage, Symbol("title"));
                Hmx::Object *game = ObjectDir::Main()->Find<Hmx::Object>("game", true);
                if (game) {
                    game->Handle(Message("set_realtime", 1), true);
                }
                mSound = mHollabackHUD->Find<Sound>(
                    MakeString("%s.snd", mIntroShoutout.Str()), false
                );
                if (mSound) {
                    mSound->Play(0, 0, 0, nullptr, 0);
                }
                TheHamProvider->SetProperty("game_stage", Symbol("title"));
                TheHamDirector->GetVenueWorld()
                    ->Find<Flow>("animate_timeywimey.flow", true)
                    ->Activate();
                SetDefaultShot();
            } else if (mIntroStarted && TheMaster->GetAudio()->IsReady()) {
                if (mSound) {
                    SynthSample *sample = mSound->Sample();
                    float f23 = mSound->ElapsedTime() - 1.0f;
                    if (f23 >= sample->LengthMs() * (1.0f / 1000.0f))
                        goto next;
                }
                TheMaster->GetAudio()->SetPaused(false);
                Hmx::Object *game = ObjectDir::Main()->Find<Hmx::Object>("game", true);
                if (game) {
                    game->Handle(Message("set_realtime", 0), true);
                }
                SetNumMoves(mInitialMoveCount);
                TheHamProvider->SetProperty(holla_back_stage, exit_title);
                mGamePlaying = true;
                TheHamProvider->SetProperty("game_stage", Symbol("playing"));
                TheHamProvider->SetProperty("hide_venue", 0);
                SetState((State)1);
                SetDefaultShot();
            }
        }
    next:
        float curBeat = TheTaskMgr.Beat();
        HamMove *move =
            TheMoveMgr->FindHamMoveFromName(TheHamDirector->MoveNameFromBeat(curBeat, 0));
        if (mState == 0) {
            mHUDPanel->Find<UILabel>("song_name.lbl", true)
                ->SetPrelocalizedString(String("???"));
            mHUDPanel->Find<UILabel>("song_artist.lbl", true)
                ->SetPrelocalizedString(String("???"));
        }
    }
}

// AT_LIMIT candidate (lane w-hb-begin, 2026-11-10): 850/852 equal, 2 diff_arg,
// fuzzy 99.97652, 3408 = 3408, canonical 100.00000, zero rows otherwise.
// The two rows are the SAME defect: the operand order of one dead `lwzx`.
//   idx 119 (tgt 0x1dc) src line 223  mWinCamCuts.clear()   [top level]
//   idx 225 (tgt 0x384) src line 236  mWinShoutouts.clear() [inside if(a)]
//     target `lwzx r10, r11, r9`  vs  ours `lwzx r10, r9, r11`
//     (r9 = vector _M_start/induction pointer, r11 = _M_finish - _M_start)
// The other two of the four inlined clear() copy loops already match, and they
// bracket the two failures: idx 102 (tgt 0x198, line 222 mWinShoutouts.clear())
// and idx 264 (tgt 0x420, line 245 mWinCamCuts.clear()) are BOTH emitted by us
// exactly as the image has them. So the image itself is 3 diff-first / 1
// ptr-first across four textually identical call sites -- site D is the image's
// odd one out and site A is ours.
//
// Why no source handle exists (this is the argument, not a shrug):
//  1. The loop is provably ZERO-TRIP on BOTH sides at all four sites:
//     `subf rX,rY,rY` (=0), `stw`, `srawi. rX,rX,2`, `ble` -> always skips.
//     That is correct: clear() == _M_erase(_M_start,_M_finish), so the copy
//     count is _M_finish - _M_finish == 0. The `subf r11,<start>,<finish>`
//     feeding the loop is a DEAD leftover, which is why we also match the
//     image on it. Operand order on a dead value is c2's DAG/allocator
//     coin-flip, not a property of the text -- which the image's own 3/1 split
//     demonstrates.
//  2. The two failing sites have IDENTICAL shape to each other AND to site D
//     (same inline, same register roles r9=ptr / r11=diff, same 14 preceding
//     rows), yet the image orders them oppositely to each other. Nothing in the
//     source distinguishes them.
//
// Measured spellings (each one edit, rebuilt, name_check ruler; baseline
// equal=850 diff_arg=2 total=852 fuzzy=99.97652). Nothing beat baseline:
//   B+C -> erase(begin(),end())       830 eq / 24 rows, 852->854 (+2 ins +4 del), 99.12676
//   B only -> erase(begin(),end())    840 eq / 13 rows, 853 (+1 ins +2 del), 99.55164
//   C only -> erase(begin(),end())    840 eq / 13 rows, 853 (+1 ins +2 del), 99.54578
//   swap the two top-level clears     826 eq / 26 rows, 99.85798
//   mWinCamCuts.clear() first         826 eq / 26 rows, 99.85798
//   braces { } around B, around C,    INERT: 850 eq / 2 rows, 99.97652 (both)
//     and around both
//   hoist shootoutArr/camCutArr       INERT: 850 eq / 2 rows, 99.97652
//     out of if(a) to function scope
//   this->mWinShoutouts at B/C        INERT: 850 eq / 2 rows, 99.97652
//   (&mWinShoutouts)[0] at B/C        INERT: 850 eq / 2 rows, 99.97652
//   `unsigned numShoutouts`           848 eq / 4 rows (2 replace), 99.83568
//   swap the two static Symbol decls  842 eq / 10 rows, 99.92958
//   move camcuts clear+push_back     749 eq / 123 rows, 872 (+20 ins +20 del), 94.48474
//     below the static Symbol decls
//   move both clears above the       731 eq / 131 rows, 862, 97.30282
//     static Symbol decls
//   local vector refs at B/C          837 eq / 15 rows (-1 ins), 99.78873
// decomp-synth (permuter), --jobs 1, two runs, ledger checked: pass 1 with
// declaration_reorder/declaration_movement/statement_reorder/scope_narrowing/
// scope_widening/commutative_swap/member_ref_bind/value_address_caching/
// temp_elimination/reference_elimination/assignment_reorder/copy_member_reorder/
// loop_var_hoist/member_readback (313 viable variants), then the FULL 112-pattern
// set (549 viable variants). 862 viable variants total, 0 improved on baseline
// (0 in every depth of both runs). Source left untouched by both.
//
// The only remaining lever would be rewriting stlport's _M_erase to flip the
// dead add's operand order in the header -- which would move every vector clear
// in the binary, and would not reproduce the image's own 3/1 split anyway.
// Behaviour is unaffected either way: both rows are in code that never runs.
void HollaBackMinigame::BeginMinigame(DataArray *a) {
    if (!mActive) {
        unk484 = 0;
        unk47c = 0;
        mIntroStarted = false;
        static Symbol captured("captured");
        for (int i = 0; i < 0x40; i++) {
            mMoveStates[i] = captured;
        }
        TheHamDirector->StartStopVisualizer(false, 0);
        TheHamProvider->SetProperty("use_char_projection", 1);
        mSubStateIndex = 0;
        unk420 = 0;
        mStartCountdown = 10;
        unk46c = false;
        TheGameData->Player(0);
        TheGameData->Player(1);
        unk444 = false;
        mInitialMoveCount = 2;
        mMaxRoutineSize = 4;
        mSpecifyFirstMoveMeasure = -1;
        mInitialMusicJump = -1;
        mIntroShoutout = Symbol("hb_intro_70s");
        // Open (w25-rm): the four inlined clear() copy loops here and in the
        // `if (a)` arm emit `lwzx rD, base, diff` or `lwzx rD, diff, base`.
        // The image has diff first in the first three, base first in the last;
        // we have diff first only in the first. Same inlined source in all four;
        // both operands are colour candidates, so the order is set by context,
        // not spelling. In our dump the first loop is the one whose __n-like k04
        // local is dropped by the negative-benefit pass (it has one colour
        // candidate fewer); hypothesis: the image drops it in loops two and
        // three as well. Inert: local refs to either vector, unused named
        // locals before the loop (symbol ordinals unchanged).
        mWinShoutouts.clear();
        mWinCamCuts.clear();
        mWinCamCuts.push_back("practice_intro_skills");
        static Symbol initial_move_count("initial_move_count");
        static Symbol max_routine_size("max_routine_size");
        static Symbol win_shoutouts("win_shoutouts");
        static Symbol win_camcuts("win_camcuts");
        static Symbol initial_music_jump("initial_music_jump");
        if (a) {
            a->FindData(initial_move_count, mInitialMoveCount, false);
            a->FindData(max_routine_size, mMaxRoutineSize, false);
            a->FindData("intro_shoutout", mIntroShoutout, false);
            DataArray *shoutoutArr = a->FindArray(win_shoutouts, false);
            if (shoutoutArr) {
                mWinShoutouts.clear();
                int numShoutouts = shoutoutArr->Size();
                mWinShoutouts.reserve(numShoutouts);
                for (int i = 1; i < numShoutouts; i++) {
                    mWinShoutouts.push_back(shoutoutArr->Sym(i));
                }
            }
            DataArray *camCutArr = a->FindArray(win_camcuts, false);
            if (camCutArr) {
                mWinCamCuts.clear();
                int numCamCuts = camCutArr->Size();
                mWinCamCuts.reserve(numCamCuts);
                for (int i = 1; i < numCamCuts; i++) {
                    mWinCamCuts.push_back(camCutArr->Sym(i));
                }
            }
            if (a->FindData(initial_music_jump, mInitialMusicJump, false)) {
                mPendingMusicJump = true;
            }
            a->FindData("specify_first_move_measure", mSpecifyFirstMoveMeasure, false);
            TheHamProvider->SetProperty("merge_moves", 0);
        }
        SetNumMoves(mInitialMoveCount);
        mActive = true;
        TheMaster->AddSink(this, "beat");
        mHUDPanel = DataVariable("hud_panel").Obj<PanelDir>();
        mHollabackHUD = mHUDPanel->Find<RndDir>("holla_back_hud", true);
        if (mSound) {
            Flow *flow = mHollabackHUD->Find<Flow>("hide_shoutout.flow", false);
            if (flow) {
                flow->Activate();
            }
            mSound->Stop(nullptr, false);
            mSound = nullptr;
        }
        HamLabel *lbl = mHollabackHUD->Find<HamLabel>("shoutout.lbl", true);
        if (lbl) {
            lbl->SetPrelocalizedString(String(""));
        }
        static Symbol set_num_display("set_num_display");
        static Symbol set_card_move("set_card_move");
        static Symbol set_card_campaign_status_2("set_card_campaign_status_2");
        mFlashcardDockPanel =
            ObjectDir::Main()->Find<UIPanel>("flashcard_dock_panel", true);
        mFlashcardDockDataDir = mFlashcardDockPanel->DataDir();
        MoveDir *theMoveDir = TheHamDirector->GetMoveDir();
        mRoutineMoves.clear();
        for (int i = 0; i < mMaxRoutineSize; i++) {
            // Named, the member read is a symbol rather than an expression
            // temp, and the add takes it as its first operand (image order).
            int firstMeasure = mSpecifyFirstMoveMeasure;
            HamMove *move = theMoveDir->GetMoveAtMeasure(0, firstMeasure + i);
            bool found = false;
            FOREACH (it, mRoutineMoves) {
                if (*it == move) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                mRoutineMoves.push_back(move);
            }
        }
        mFlashcardDockPanel->Handle(Message(set_num_display, (int)mRoutineMoves.size()), true);
        for (int i = 0; i < mRoutineMoves.size(); i++) {
            mFlashcardDockPanel->Handle(Message(set_card_move, i, mRoutineMoves[i]), true);
            mFlashcardDockPanel->Handle(
                Message(set_card_campaign_status_2, i, captured), true
            );
        }
        static Symbol horz_layout("horz_layout");
        int numMoves = mRoutineMoves.size();
        if (numMoves <= 4) {
            mFlashcardDockDataDir->Find<Flow>("horz_layout4.flow", true)->Activate();
        } else {
            mFlashcardDockDataDir->Find<Flow>("horz_layout6.flow", true)->Activate();
        }
        mScoreLeft = mHUDPanel->Find<RndDir>("score_left", true);
        mScoreRight = mHUDPanel->Find<RndDir>("score_right", true);
        mScoreLeft->SetShowing(false);
        mScoreRight->SetShowing(false);
        TheHamDirector->SetPlayerSpotlightsEnabled(false);
        mFirstMoveIdx = mSpecifyFirstMoveMeasure;
        mLastMoveIdx = mSpecifyFirstMoveMeasure + mNumMoves;
        TheHamProvider->SetProperty("visible_flashcard_btm", mSpecifyFirstMoveMeasure);
        TheHamProvider->SetProperty("visible_flashcard_top", mLastMoveIdx);
        TheHamProvider->SetProperty("hide_venue", 1);
        TheHamDirector->UnselectVisualizerPostProc();
        TheHamDirector->GetVenueWorld()
            ->Find<Flow>("animate_timeywimey.flow", true)
            ->Activate();
        static Symbol start_score_move_index("start_score_move_index");
        static Symbol hide_hud("hide_hud");
        for (int i = 0; i < 2; i++) {
            HamPlayerData *hpd = TheGameData->Player(i);
            if (!hpd->IsPlaying()) {
                hpd->Provider()->SetProperty(start_score_move_index, 1000);
                hpd->Provider()->Export(Message(hide_hud, 0), true);
            }
        }
        SetDefaultShot();
        mState = -1;
        SetState((State)0);
    }
}

Symbol HollaBackMinigame::GetMoveState(int measure) const {
    static Symbol captured("captured");
    if (measure >= 0 && measure < 64) {
        return mMoveStates[measure];
    } else {
        MILO_NOTIFY(
            "HollaBackMinigame::GetMoveState(int measure = %d), measure not between 0 and 64",
            measure
        );
        return captured;
    }
}

void HollaBackMinigame::SetNumMoves(int num) {
    mNumMoves = num;
    TheHamProvider->SetProperty("visible_flashcard_btm", mSpecifyFirstMoveMeasure);
    TheHamProvider->SetProperty(
        "visible_flashcard_top", mSpecifyFirstMoveMeasure + num - 1
    );
    mFirstMoveIdx = mSpecifyFirstMoveMeasure - 4;
    mLastMoveIdx = mSpecifyFirstMoveMeasure + mNumMoves + 1;
}

void HollaBackMinigame::StartShoutOut(const char *cc) {
    HamLabel *label = mHollabackHUD->Find<HamLabel>("shoutout.lbl", true);
    if (label) {
        label->SetTextToken(Symbol(cc));
    }
    mSound = mHollabackHUD->Find<Sound>(MakeString("%s.snd", cc), false);
    if (mSound) {
        mSound->Play(0, 0, 0, nullptr, 0);
    }
    Flow *flow = mHollabackHUD->Find<Flow>("show_shoutout.flow", false);
    if (flow) {
        flow->Activate();
    }
}

// Residual (98.1): we form &TheHamDirector (lis+addi) in every arm because the
// tail needs it past the join; the image keeps only the @h half in r30 and
// loads @l(r30).  Lane history: docs/decomp/notes/system/hamobj/HollaBackMinigame.md
void HollaBackMinigame::SetDefaultShot() {
    if (TheGameData->Player(1)->IsPlaying()) {
        if (TheGameData->Player(0)->IsPlaying()) {
            if (TheGameData->Player(0)->Side() == kSkeletonRight) {
                TheHamDirector->Handle(
                    Message("force_shot", "practice_center_p21.shot"), true
                );
            } else {
                TheHamDirector->Handle(
                    Message("force_shot", "practice_center_p12.shot"), true
                );
            }
        } else {
            TheHamDirector->Handle(Message("force_shot", "practice_center_p2.shot"), true);
        }
    } else {
        TheHamDirector->Handle(Message("force_shot", "practice_center_p1.shot"), true);
    }
    RndPropAnim *anim = TheHamDirector->GetVenueWorld()->Find<RndPropAnim>(
        "bid_start_character_faded_out.anim", true
    );
    anim->Animate(0, false, 0, nullptr, kEaseLinear, 0, false);
}

void HollaBackMinigame::DecipherShoutOut(float pct) {
    if (pct < 0.25f) {
        StartShoutOut("hb_crazedeciphered0pct");
    } else if (pct < 0.33f) {
        StartShoutOut("hb_crazedeciphered25pct");
    } else if (pct < 0.5f) {
        StartShoutOut("hb_crazedeciphered33pct");
    } else if (pct < 0.66f) {
        StartShoutOut("hb_crazedeciphered50pct");
    } else if (pct < 0.75f) {
        StartShoutOut("hb_crazedeciphered66pct");
    } else {
        StartShoutOut("hb_crazedeciphered75pct");
    }
}

void HollaBackMinigame::WinShoutOut() {
    int nShoutouts = (int)mWinShoutouts.size();
    if (nShoutouts) {
        StartShoutOut(mWinShoutouts.front().Str());
    }
    int nCamCuts = (int)mWinCamCuts.size();
    if (nCamCuts) {
        TheHamDirector->Handle(Message("force_shot", mWinCamCuts.front().Str()), true);
    }
}

void HollaBackMinigame::EndShoutOut() {
    Flow *flow = mHollabackHUD->Find<Flow>("hide_shoutout.flow", false);
    if (flow) {
        flow->Activate();
    }
    mSound = nullptr;
    if (mState == 1) {
        SetDefaultShot();
        if (!mGamePlaying) {
            TheHamProvider->Export(Message("show_char_projection"), true);
        }
    }
}

float HollaBackMinigame::NailedMovesInRoutinePct() {
    static Symbol powered_up("powered_up");
    int nailedCount = 0;
    int numMoves = mRoutineMoves.size();
    MoveDir *theMoveDir = TheHamDirector->GetMoveDir();
    for (int moveIndex = 0; moveIndex < numMoves; moveIndex++) {
        HamMove *curMove = mRoutineMoves[moveIndex];
        int j = 0;
        if (mMaxRoutineSize > 0) {
            while (true) {
                HamMove *foundMove = theMoveDir->GetMoveAtMeasure(0, mSpecifyFirstMoveMeasure + j);
                // The image reads mMoveStates[] UNCONDITIONALLY (target rows
                // 45-51, before the `bne` at 52 that skips to the j++ path) and
                // tests the POSITIVE case, reading powered_up only on the arm
                // where the move already matched.  Written as the negated `||`
                // the state load is short-circuited behind the move compare and
                // MSVC may not hoist it.
                Symbol state = mMoveStates[mSpecifyFirstMoveMeasure + j];
                if (foundMove == curMove && state == powered_up) {
                    // Found a nailed move - mark all matching moves in routine
                    nailedCount++;
                    for (int k = 0; k < mMaxRoutineSize; k++) {
                        HamMove *checkMove = theMoveDir->GetMoveAtMeasure(0, mSpecifyFirstMoveMeasure + k);
                        if (checkMove == foundMove) {
                            mMoveStates[mSpecifyFirstMoveMeasure + k] = powered_up;
                        }
                    }
                    break;
                }
                j++;
                if (j >= mMaxRoutineSize) {
                    break;
                }
            }
        }
    }
    return (float)(s64)nailedCount / (float)(s64)numMoves;
}

void HollaBackMinigame::EndMinigame(bool b1) {
    if (mActive) {
        mActive = false;
        TheMaster->RemoveSink(this);
        TheHamProvider->RemoveSink(this);
        Hmx::Object *game = ObjectDir::Main()->Find<Hmx::Object>("game_panel", true);
        game->RemoveSink(this);
        mHUDPanel->Find<Flow>("unset_flashcards_mystery.flow", true)->Activate();
        mHUDPanel->Find<UILabel>("song_name.lbl", true)
            ->SetPrelocalizedString(String("???"));
        mHUDPanel->Find<UILabel>("song_artist.lbl", true)
            ->SetPrelocalizedString(String("???"));
        TheHamProvider->SetProperty("visible_flashcard_btm", -1);
        TheHamProvider->SetProperty("visible_flashcard_top", -1);
        static Symbol clear_all_flashcard_campaign_status(
            "clear_all_flashcard_campaign_status"
        );
        mHUDPanel->Handle(Message(clear_all_flashcard_campaign_status), true);
        TheHamProvider->SetProperty("use_char_projection", 0);
        TheHamProvider->SetProperty("use_char_projectionp2", 0);
        TheHamDirector->Handle(Message("force_shot", Symbol("")), true);
        if (mScoreLeft) {
            mScoreLeft->SetShowing(true);
        }
        if (mScoreRight) {
            mScoreRight->SetShowing(true);
        }
    }
}

// w20-t (99.4, branch-scan row 98 adjudicated ARTIFACT): the back-edge lands on
// the image's in-loop `lwz r8, 0x488(r30)` reload of mRoutineMoves._M_start
// (see w13-b below); the search loop stores nothing, same value.
void HollaBackMinigame::SetMoveState(int measure, Symbol state) {
    if (measure >= 0 && measure < 0x40) {
        static Symbol powered_up("powered_up");
        static Symbol set_card_campaign_status_2("set_card_campaign_status_2");
        MoveDir *theMoveDir = TheHamDirector->GetMoveDir();
        HamMove *move = theMoveDir->GetMoveAtMeasure(0, measure);
        if (mMoveStates[measure] != state) {
            if (state == powered_up) {
                bool b2 = false;
                for (u32 i = mSpecifyFirstMoveMeasure;
                     i < mSpecifyFirstMoveMeasure + mMaxRoutineSize;
                     i++) {
                    HamMove *curMove = theMoveDir->GetMoveAtMeasure(0, i);
                    if (curMove == move && mMoveStates[i] == powered_up) {
                        b2 = true;
                    }
                }
                if (!b2) {
                    mFlashcardDockPanel->SetShowing(true);
                    // RESIDUAL (w13-b, 99.40): the image reloads
                    // mRoutineMoves._M_start (lwz 0x488(r30)) at the top of every
                    // iteration of this search; we keep it live from the size().
                    // Same shape as MoveGraph::FindVariantPair.  Equivalent.
                    // w21-u (99.40, same 6 rows): `i < mRoutineMoves.size()`
                    // in the condition is 98.8 (spills a temp to 0x50); a
                    // `std::vector<HamMove *> &moves = mRoutineMoves;` binding
                    // is 97.8 (dead `addi r8, r30, 0x488` + spill); `int i` is
                    // byte-inert.  Behaviour re-checked against the listing:
                    // same size snapshot, same break target, same Message args.
                    // w21-au (still 99.40, same 6 rows): 16 more spellings, none
                    // above it -- inert: begin()[i], a const-vector cast on the
                    // read, `mRoutineMoves[i] == move`, an element local, `u32 i`
                    // outside the for, `for (i = 0, n = size(); ...)`, a while
                    // loop, `continue` on mismatch, `(int)i` in the Message.
                    // Worse: end()-begin() 98.8, int count/index 99.0, a const&
                    // binding 97.8, size() before SetShowing 96.4 (the image
                    // stores mShowing first), a find-then-test split 98.4, a
                    // panel local 98.2, size() in the condition 98.8 (the
                    // _M_start reload DOES appear, but with a stw spill to 0x50).
                    // Closest miss: size() through the member and the read
                    // through a `(const) std::vector<HamMove *> &` bound before
                    // the loop (or before SetShowing) stops the CSE with the
                    // size load, but MSVC then hoists the reference load above
                    // the loop (98.80, 5 rows: the lwz sits before `li r11, 0`
                    // instead of at the loop head); bound inside the loop body
                    // 98.1.
                    // w26-o: the reload is a CSE decision, not operand order --
                    // the loop body stores nothing, so c2 is free to keep the
                    // _M_start that size() already loaded.  A vector reference
                    // re-bound at the loop head is 97.7 (a dead `addi` plus a
                    // spill), a per-iteration `const HamMove *const &` element
                    // binding is 98.0, and `const u32 &numMoves`, a `const u32
                    // idx = i` and a plain element local are byte-inert.
                    u32 numMoves = mRoutineMoves.size();
                    for (u32 i = 0; i < numMoves; i++) {
                        if (move == mRoutineMoves[i]) {
                            mFlashcardDockPanel->Handle(
                                Message(set_card_campaign_status_2, i, powered_up), true
                            );
                            break;
                        }
                    }
                    mFlashcardDockDataDir->Find<Flow>("activate_popup.flow", true)->Activate();
                }
            }
            mMoveStates[measure] = state;
        }
    } else {
        MILO_NOTIFY(
            "HollaBackMinigame::SetMoveState(int measure = %d, Symbol state = '%s'), measure not between 0 and 64",
            measure,
            state.Str()
        );
    }
}

void HollaBackMinigame::SetState(State s) {
    if (mState != s) {
        static Symbol holla_back_stage("holla_back_stage");
        static Symbol enter_title("enter_title");
        static Symbol enter_instruction("enter_instruction");
        static Symbol enter_win("enter_win");
        static Symbol game_stage("game_stage");
        static Symbol intro("intro");
        static Symbol title("title");
        static Symbol playing("playing");
        MoveDir *theMoveDir = TheHamDirector->GetMoveDir();
        mState = s;
        mSubStateIndex = -1;
        switch (mState) {
        case 0:
            theMoveDir->ResetDetection();
            TheHamProvider->SetProperty(game_stage, title);
            mHUDPanel->Find<RndPropAnim>("song_overlay.anim", true)
                ->Animate(0, false, 0, nullptr, kEaseLinear, 0, false);
            TheHamProvider->SetProperty(holla_back_stage, enter_title);
            break;
        case 1:
            TheHamProvider->SetProperty(game_stage, playing);
            mHUDPanel->Find<Flow>("unset_flashcards_mystery.flow", true)->Activate();
            TheHamProvider->SetProperty(holla_back_stage, enter_instruction);
            break;
        case 2: {
            TheHamProvider->SetProperty("game_stage", Symbol("outro"));
            TheHamProvider->SetProperty(holla_back_stage, enter_win);
            mWinLoopBeat = 420;
            MidiParser *p = TheMidiParserMgr->GetParser("count_in_player");
            p->SetProperty("active", 0);
            TheMaster->GetAudio()->SetLoop(0, mWinLoopBeat * 4.0f);
            break;
        }
        }
        OnBeat();
    }
}

void HollaBackMinigame::OnBeat() {
    static Symbol holla_back_stage("holla_back_stage");
    static Symbol exit_title("exit_title");
    static Symbol exit_instruction("exit_instruction");
    static Symbol exit_win("exit_win");

    HamMaster *master = TheMaster;
    // Our build spills this pointer into the 0x50 temp slot (`stw r10, 0x50(r31)`)
    // and never reloads it -- the one extra instruction in this function.  It is the
    // dead home-slot store (see memory: pattern_dead_home_slot_store).  Refuted here:
    // reference binding, `const`, declaring it after `songPos`/`theMoveDir`, and
    // dropping the local entirely (that last one also drops the `addi r10, r30, 0x78`
    // the target does emit, so it scores worse: 99.856 -> 99.855).
    // w16-b: `master->TotalBeat2()` in the test (no local) deletes the image's
    // `addi r10, r30, 0x78` too; a `const SongPos &` local is identical to
    // this pointer. Same one-row residual either way.
    // w21-ax: stopped at 99.85694 (2 rows: this store + the add below). The
    // target DOES materialise `addi r10, r30, 0x78` (824EAEEC) and folds both
    // compare loads onto r30 (lfs 0x7c/0x64), so this local existed; only its
    // home store is ours. Also inert/worse: declaring it after the
    // GetCurrLoopBeats call / after currentBeat (99.5, store moves later), a
    // by-value SongPos copy (99.0, memcpy), `master->SongPos2().GetTotalBeat()`
    // in the test with no local (drops the addi). `currentBeat =
    // master->TotalBeat1()` makes the volatile re-reads below unnecessary (the
    // r29-based reload then differs by expression) but costs 17 regswap rows.
    // w26-g canonical decomposition: report.json charges exactly ONE unit of
    // 100 -- an insert, 100 of 69900 (99.85694) -- and it is the store below.
    // 0x50(r31) is written twice by the base (index 57 and 641) and READ
    // nowhere, so it cannot be a behaviour difference. The `add` operand swap
    // at index 194 is register-only and the canonical ruler forgives it. That
    // makes this the closest row in the 95-100 band: one dead store.
    // Refuted this lane, every one BIT-IDENTICAL (700 insns, canonical
    // 99.85694): `SongPos *a = &.., *b = &..;` on one declarator line (both
    // orders), `SongPos *const`, separate declaration + assignment, a
    // file-static helper returning &m->SongPos2(), an explicit cast, and
    // pointer-returning accessors added to HamMaster.h as a probe (reverted).
    // The store is a home INITIALISATION, not a spill: it survives with the
    // GetCurrLoopBeats call deleted entirely. Mechanism = the dead home-slot
    // store of an address-of-sub-object temporary (memory:
    // pattern_dead_home_slot_store) -- our c1 homes the `&master->SongPos2()`
    // temp, the image's c1 keeps it in a register and its copy is already dead
    // (`addi r10, r30, 0x78` has no reader in the image either), so this is the
    // inverse of the Spotlight::SyncProperty residual, where the IMAGE homes it
    // and we do not.
    // w26-n3 (9 more variants, canonical measured per-variant on the per-object
    // path, which is exact on this unit -- baseline 99.85694 reproduced):
    //   swap the two SongPos declarators            99.47639 (much worse)
    //   const SongPos *                             99.85694 (bit-identical)
    //   &TheMaster->SongPos2() instead of &master-> 99.85694 (bit-identical)
    //   separate declarator + assignment            99.85694 (bit-identical)
    //   implicit SongPos reference + `.` spelling   build error (the two uses
    //       below go through `songPos`, not `prevSongPos`; the binding itself
    //       was refuted by w26-g)
    //   a named `float prevBeat` before the branch  99.77110 (one extra row)
    // So the store is not reachable by declaring, binding or re-rooting the
    // local: our c1 gives the `&master->SongPos2()` temp a home even though the
    // value is already in a register and every read of it folds back to
    // 0x7c(r30).  The image emits the same dead `addi r10, r30, 0x78` and no
    // home, i.e. the difference is the home allocation itself, not the local.
    SongPos *prevSongPos = &master->SongPos2();
    SongPos *songPos = &master->SongPos1();
    MoveDir *theMoveDir = TheHamDirector->GetMoveDir();
    int loopStart, loopEnd;
    master->GetAudio()->GetCurrLoopBeats(loopStart, loopEnd);

    float currentBeat = songPos->GetTotalBeat();
    if (currentBeat < prevSongPos->GetTotalBeat()) {
        // Beat jumped backward - reset the midi parsers to the current beat. The
        // beat is re-read from memory for each message (the original reloads rather
        // than reusing currentBeat); the volatile read defeats CSE of the float.
        {
            DataNode resetBeat(*(volatile float *)&songPos->AccessTotalBeat());
            MidiParser *midiPlayer = TheMidiParserMgr->GetParser("midi_player");
            midiPlayer->Handle(Message("reset_to_beat", resetBeat), true);
        }
        {
            DataNode resetBeat(*(volatile float *)&songPos->AccessTotalBeat());
            MidiParser *countIn = TheMidiParserMgr->GetParser("count_in_player");
            countIn->Handle(Message("reset_to_beat", resetBeat), true);
        }
        theMoveDir->ResetDetection();
    }

    int beatInt = (int)currentBeat;
    unk420 = beatInt / 4;

    if (mSpecifyFirstMoveMeasure > 0 && mState == 1) {
        mSubStateIndex = (4 - mSpecifyFirstMoveMeasure) * 4 + beatInt;
    } else if (mState != 0) {
        mSubStateIndex = mSubStateIndex + 1;
    }

    int subStateIdx = mSubStateIndex;
    int subMeasure = subStateIdx / 4;
    int subBeatInMeasure = subStateIdx % 4;
    int currentBeatInMeasure = beatInt % 4;

    if (subStateIdx >= 0 && subBeatInMeasure != currentBeatInMeasure) {
        // Target has `add r11, r11, r10`, ours `add r11, r10, r11`.  MSVC canonicalises
        // the commutative operands: `= subStateIdx + (...)`, a named delta local, and
        // `-= subBeatInMeasure - currentBeatInMeasure` all emit byte-identical code.
        // Plain 2-term same-register swap = backend floor.
        // w21-ax: `mSubStateIndex += delta` (99.5, cmp/blt reshuffle) and
        // `mSubStateIndex = (cur - sub) + subStateIdx` (identical) also tried.
        subStateIdx += currentBeatInMeasure - subBeatInMeasure;
        mSubStateIndex = subStateIdx;
    }

    bool onDownbeat = currentBeatInMeasure == 0;

    if (mState == 1) {
        // Instruction phase
        if (mGamePlaying && subMeasure == 3 && currentBeatInMeasure == 0) {
            mGamePlaying = false;
            WorldDir *venueWorld = TheHamDirector->GetVenueWorld();
            venueWorld->Find<RndPropAnim>("bid_start_character_faded_out.anim", true)
                ->Animate(0.0f, false, 0.0f, 0, kEaseLinear, 0.0f, false);
            TheHamProvider->Export(Message("show_char_projection"), true);
        }
        if (subMeasure == 3 && currentBeatInMeasure == 3) {
            MidiParser *countIn = TheMidiParserMgr->GetParser("count_in_player");
            countIn->SetProperty("active", 0);
        }

        if ((onDownbeat || subMeasure < 0)) {
            if (subMeasure == 1) {
                MidiParser *countIn = TheMidiParserMgr->GetParser("count_in_player");
                countIn->SetProperty("active", 1);
                int sectionEnd = mSpecifyFirstMoveMeasure * 4;
                countIn->Handle(Message("set_section", sectionEnd - 4, sectionEnd), true);
            }
            if (subMeasure == 2) {
                TheMaster->GetAudio()->SetLoop(
                    (float)(mFirstMoveIdx + 1) * 4.0f,
                    (float)mLastMoveIdx * 4.0f
                );
            }
            if (subMeasure == lbl_82F0E8A4) {
                unk46c = true;
                TheHamDirector->SetPlayerSpotlightsEnabled(true);
                TheHamProvider->SetProperty(holla_back_stage, exit_instruction);
            }

            bool atEnd = unk46c && (unk420 == mSpecifyFirstMoveMeasure - 3 || unk420 == mLastMoveIdx);
            bool nearEnd = unk46c && (unk420 == mSpecifyFirstMoveMeasure - 3 || unk420 == mLastMoveIdx - 1);

            if (nearEnd) {
                TheHamProvider->Export(Message("hide_char_projection"), true);
            }
            if (atEnd) {
                TheHamDirector->SetPlayerSpotlightsEnabled(false);
                float pct = NailedMovesInRoutinePct();
                bool nailed = pct >= 1.0f;
                unk475 = nailed;
                if (nailed) {
                    mScoreLeft->SetShowing(true);
                    mScoreRight->SetShowing(true);
                    WinShoutOut();
                    Hmx::Object *songseq = ObjectDir::Main()->Find<Hmx::Object>("songseq", true);
                    songseq->Handle(Message("load_next_song_audio"), true);
                    for (int i = 0; i < 2; i++) {
                        HamPlayerData *hpd = TheGameData->Player(i);
                        // Provider() returns PropertyEventProvider*; the original upcasts to
                        // Hmx::Object* here so SetProperty/Export dispatch through that vtable.
                        Hmx::Object *provider = hpd->Provider();
                        provider->SetProperty("start_score_move_index", 1000);
                        provider->Export(Message("hide_hud", 0), true);
                    }
                    if (unk484 == 0) {
                        Hmx::Object *gamePanel = ObjectDir::Main()->Find<Hmx::Object>("game_panel", true);
                        gamePanel->Handle(Message("earn_acc", Symbol("acc_hollaback")), true);
                    }
                    SetState((State)2);
                } else {
                    DecipherShoutOut(pct);
                    unk484++;
                    SetState((State)1);
                }
            }
        }
    } else if (mState == 2) {
        // Win phase
        if (mSound && mSound->IsPlaying()) {
            return;
        }
        if (mWinShoutouts.size() > 1) {
            const char *sndName = MakeString("%s.snd", mWinShoutouts[1].Str());
            WorldDir *venueWorld = TheHamDirector->GetVenueWorld();
            mSound = venueWorld->Find<Sound>(sndName, false);
            if (mSound) {
                mSound->Play(0, 0, 0, nullptr, 0);
            }
        }
        TheHamDirector->SetPlayerSpotlightsEnabled(true);
        Hmx::Object *gamePanel = ObjectDir::Main()->Find<Hmx::Object>("game_panel", true);
        gamePanel->Handle(Message("win_hollaback"), true);
    }
}
