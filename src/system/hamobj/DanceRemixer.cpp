#include "hamobj\DanceRemixer.h"
#include "meta_ham\MetagameStats.h"
#include "MoveMgr.h"
#include "hamobj\HamDirector.h"
#include "hamobj\HamMaster.h"
#include "hamobj\HamAudio.h"
#include "hamobj\HamGameData.h"
#include "hamobj\HamMove.h"
#include "hamobj\MoveDetector.h"
#include "hamobj\MoveDir.h"
#include "hamobj\MoveGraph.h"
#include "obj\Data.h"
#include "obj\Msg.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "os\Debug.h"
#include "os\System.h"
#include "utl\TimeConversion.h"

MetagameStats::FavoriteStat::FavoriteStat() {}

DanceRemixer::DanceRemixer() {}
DanceRemixer::~DanceRemixer() { HandleType(Message("deinit")); }

Symbol OnMoveVariantFromHamMove(const DataArray *array) {
    MILO_ASSERT(array->Size() == 3, 0x21C);
    DanceRemixer *remixer = array->Obj<DanceRemixer>(0);
    HamMove *move = array->Obj<HamMove>(2);
    MILO_ASSERT(move, 0x21F);
    const MoveVariant *mv = remixer->MoveVariantFromHamMove(move);
    MILO_ASSERT(mv, 0x221);
    return mv ? mv->Name() : "";
}

BEGIN_HANDLERS(DanceRemixer)
    HANDLE_ACTION(reset, Reset())
    HANDLE_ACTION(post_move_finished, PostMoveFinished())
    HANDLE_ACTION(set_jump, SetJump(_msg->Int(2), _msg->Int(3)))
    HANDLE_ACTION(clear_jump, ClearJump())
    HANDLE_EXPR(jump_from_beat, mFromMeasure * 4)
    HANDLE_EXPR(jump_to_beat, mToMeasure * 4)
    HANDLE_EXPR(jump_from_measure, mFromMeasure + 1)
    HANDLE_EXPR(jump_to_measure, mToMeasure + 1)
    HANDLE_EXPR(jumped_beat, JumpedBeat(_msg->Float(2)))
    HANDLE_EXPR(jumped_measure, JumpedMoveIdx(_msg->Int(2) - 1) + 1)
    HANDLE_EXPR(jumped_measure_add, JumpedMeasureAdd(_msg->Int(2), _msg->Int(3)))
    HANDLE_EXPR(
        jumped_measure_steps_between,
        JumpedMeasureStepsBetween(_msg->Int(2), _msg->Int(3), _msg->Int(4))
    )
    HANDLE_EXPR(scored_measure, ScoredDanceMeasure(_msg->Int(2), _msg->Int(3)))
    HANDLE_ACTION(set_unscored_measure, SetUnscoredMeasure(_msg->Int(2), _msg->Int(3)))
    HANDLE_ACTION(
        set_unscored_measure_range,
        SetUnscoredMeasureRange(_msg->Int(2), _msg->Int(3), _msg->Int(4))
    )
    HANDLE_ACTION(clear_unscored_measure, ClearUnscoredMeasure(_msg->Int(2), _msg->Int(3)))
    HANDLE_ACTION(
        clear_unscored_measure_range,
        ClearUnscoredMeasureRange(_msg->Int(2), _msg->Int(3), _msg->Int(4))
    )
    HANDLE_ACTION(clear_unscored_measures, mUnscoredMeasures[_msg->Int(2)].clear())
    HANDLE_EXPR(move_variant_from_ham_move, OnMoveVariantFromHamMove(_msg))
    HANDLE_EXPR(measures_total, mTotalMeasures)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

void DanceRemixer::SetJump(int from, int to) {
    ClearJump();
    // Plain int locals, not references to the members: the image compares the
    // two just-computed REGISTER values (cmpw r11, r10 between the two stores)
    // and forms fromBeat from the same register, where a reference makes every
    // read reload 0x4c(r31).  The else arm below does read the members back --
    // it is spelled with mFromMeasure/mToMeasure for exactly that reason.
    int fromMeasure = from - 1;
    int toMeasure = to - 1;
    mFromMeasure = fromMeasure;
    mToMeasure = toMeasure;
    // No named fromBeat: the image hoists the slwi AND its extsw above the
    // branch and leaves only std/lfd/fcfid/frsp in each arm, which is the CSE
    // of the conversion's 64-bit input, not a named int.
    if (fromMeasure == toMeasure) {
        TheMaster->GetAudio()->SetLoop((float)(toMeasure * 4), (float)(fromMeasure * 4));
    } else {
        float fromMs = BeatToMs((float)(fromMeasure * 4));
        float toMs = BeatToMs((float)(mToMeasure * 4));
        float jumpOffset = SystemConfig("synth", "crossfade_beats")->Float(1);
        float crossfadeMs = BeatToMs((float)(mFromMeasure * 4) + jumpOffset);
        TheMaster->GetAudio()->SetCrossfadeJump(fromMs, toMs, crossfadeMs - fromMs);

        mJumpMap[mToMeasure] = mFromMeasure;
        if (mFromMeasure > 0 && mToMeasure > 0) {
            mJumpMap[mFromMeasure - 1] = mToMeasure - 1;
        }

        float curBeat = TheTaskMgr.Beat();
        // The +4 and the +1 are NOT folded in the image (addi r11, r11, 0x4 then
        // a separate addic. r11, r11, 0x1): the end index is its own named
        // quantity and the count is an inclusive end - start + 1.
        int endIdx = (int)curBeat / 4 + 4;
        int startIdx = mFromMeasure - 1;
        int count = endIdx - startIdx + 1;
        if (count > 0) {
            // moveIdx's copy into a callee-saved register sits BETWEEN the two
            // guards in the image (ble / mr r25, r10 / cmpwi / ble / mr r24,
            // r11), so its declaration sits between the two tests.  The tests
            // are not written as one && : MSVC folds a second identical test
            // away (measured -- a for-loop entry test disappears entirely),
            // and the outer one is free, being the addic. that forms count.
            int moveIdx = startIdx;
            if (0 < (int)count) {
                int remaining = count;
                do {
                    MILO_ASSERT(ValidMoveIdx(moveIdx), 0x16d);
                    for (int p = 0; p < 2; p++) {
                        SelectMove(p, moveIdx);
                    }
                    moveIdx = JumpedMeasureAdd(moveIdx + 1, 1) - 1;
                    remaining--;
                } while (remaining != 0);
            }
        }
    }
}

void DanceRemixer::ClearJump() {
    mFromMeasure = -1;
    mToMeasure = -1;
    mJumpMap.clear();
    if (TheMaster && TheMaster->GetAudio()) {
        TheMaster->GetAudio()->ClearLoop();
    }
}

void DanceRemixer::SetUnscoredMeasure(int x, int y) { mUnscoredMeasures[x].insert(y); }
void DanceRemixer::ClearUnscoredMeasure(int x, int y) { mUnscoredMeasures[x].erase(y); }

BEGIN_PROPSYNCS(DanceRemixer)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(DanceRemixer)
    SAVE_SUPERCLASS(Hmx::Object)
END_SAVES

BEGIN_COPYS(DanceRemixer)
    COPY_SUPERCLASS(Hmx::Object)
    CREATE_COPY(DanceRemixer)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mTotalMeasures)
        for (int i = 0; i < 2; i++) {
            COPY_MEMBER(mUnscoredMeasures[i])
        }
        COPY_MEMBER(mPendingVariants)
        COPY_MEMBER(mNeedsUpdate)
        COPY_MEMBER(mFromMeasure)
        COPY_MEMBER(mToMeasure)
        COPY_MEMBER(mJumpMap)
    END_COPYING_MEMBERS
END_COPYS

BEGIN_LOADS(DanceRemixer)
    Hmx::Object::Load(bs);
END_LOADS

void DanceRemixer::Init(int x) {
    if (TheMoveMgr->MoveParents().size() == 0) {
        MILO_FAIL("Failed to load move graph for: %s\n", TheGameData->GetSong().Str());
    }
    mTotalMeasures = x;
    for (int i = 0; i < 2; i++) {
        TheMoveMgr->mMoveParents[i].resize(mTotalMeasures);
        TheMoveMgr->mPreferredVariants[i].resize(mTotalMeasures);
        TheMoveMgr->mRoutineMeasures[i].resize(mTotalMeasures);
    }
    ClearJump();
    HandleType(Message("post_init"));
}

void DanceRemixer::Reset() {
    mPendingVariants.clear();
    mNeedsUpdate = false;
    ClearJump();
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < mTotalMeasures; j++) {
            TheMoveMgr->mMoveParents[i][j] = 0;
            TheMoveMgr->mPreferredVariants[i][j] = 0;
            TheMoveMgr->mRoutineMeasures[i][j] = std::make_pair<const MoveVariant *, const MoveVariant *>(0, 0);
        }
        mUnscoredMeasures[i].clear();
    }
    TheMoveMgr->mRoutineLoaded = 1;
    HandleType(Message("post_reset"));
}

DataNode DanceRemixer::OnMovePassed(DataArray *a) {
    int i2 = a->Int(2);
    HamMove *move = a->Obj<HamMove>(3);
    Symbol s4 = a->ForceSym(4);
    MovePassed(i2, move, s4);
    return 0;
}

void DanceRemixer::PostMoveFinished() {
    // The image keeps BOTH the quotient and the +1 alive in callee-saved
    // registers (`addze r25, r10` then `addi r21, r25, 0x1`) and uses them at
    // different sites: r21 (moveIdx) is the ScoredDanceMeasure key, r25
    // (measure) is the mRoutineMeasures subscript -- `slwi r10, r25, 3`. The
    // JumpedMoveIdx argument is spelled `moveIdx - 1` rather than `measure`,
    // which is why the image also emits `subi r30, r21, 0x1` instead of
    // reusing r25.
    int measure = (int)TheTaskMgr.Beat() / 4;
    int moveIdx = measure + 1;
    UpdateHamDirector();
#ifdef HX_NATIVE
    // DECOMP-INTRODUCED GUARD, now correctly scoped (w7-ai). This used to be a
    // `TheHamDirector ? ... : nullptr` ternary in the shipping path, which the
    // image does not have: it loads TheHamDirector and dereferences it
    // unconditionally (`lwz r11, TheHamDirector` then `lwz r28, 0x328(r11)`,
    // no cmplwi/beq between them). The null test belongs on the native side
    // only, where TheHamDirector really can be absent.
    if (!TheHamDirector) return;
#endif
    MoveDir *moveDir = TheHamDirector->GetMoveDir();
#ifdef HX_NATIVE
    if (!moveDir) return; // No move directory loaded (no Kinect)
#endif
    MoveAsyncDetector *detector = moveDir->GetAsyncDetector();
#ifdef HX_NATIVE
    if (!detector) return;
#endif
    for (int i = 0; i < 2; i++) {
        auto _tmp1 = JumpedMoveIdx(moveIdx - 1);
        auto scored = ScoredDanceMeasure(i, _tmp1 + 1);
        if (scored) {
            detector->DisableAllDetectors();
            break;
        }
    }
    for (int i = 0; i < 2; i++) {
#ifdef HX_NATIVE
        // moveIdx is derived from the wall-clock beat, so at/after the final
        // move boundary it can reach one past mRoutineMeasures' extent
        // (mTotalMeasures). The unchecked [] then reads a garbage MoveVariant*
        // and Find() faults on its name — observed SIGSEGV on long headless
        // runs that idle at song end.
        if (measure < 0 || measure >= (int)TheMoveMgr->mRoutineMeasures[i].size())
            continue;
#endif
        if (ScoredDanceMeasure(i, moveIdx)) {
            const MoveVariant *mv = TheMoveMgr->mRoutineMeasures[i][measure].first;
            if (mv) {
                const char *hamMoveName = mv->HamMoveName().Str();
                HamMove *move = moveDir->Find<HamMove>(hamMoveName, false);
                if (move) {
                    detector->EnableDetector(move);
                    moveDir->SetCurrentMove(i, move);
                } else {
                    MILO_NOTIFY(
                        "Ham move %s missing, possibly not loaded yet. From move variant %s",
                        hamMoveName,
                        mv->Name()
                    );
                }
            }
        }
    }
}

bool DanceRemixer::ScoredDanceMeasure(int x, int y) const {
    return mUnscoredMeasures[x].find(y) == mUnscoredMeasures[x].end();
}

void DanceRemixer::UpdateHamDirector() {
    for (int i = 0; i < 2; i++) {
        const auto &mvs_list = TheMoveMgr->mRoutineMeasures[i];
        for (int j = 0; j < mvs_list.size(); j++) {
            if (j <= mFromMeasure || j >= mToMeasure) {
                std::pair<const MoveVariant *, const MoveVariant *> mvs = mvs_list[j];
                if (mvs.first) {
                    mNeedsUpdate |= mPendingVariants.insert(mvs.first).second;
                }
                if (mvs.second && mvs.second != mvs.first) {
                    mNeedsUpdate |= mPendingVariants.insert(mvs.second).second;
                }
            }
        }
    }
    if (mNeedsUpdate && TheHamDirector->IsMoveMergerFinished()) {
        TheHamDirector->LoadRoutineBuilderData(mPendingVariants, true);
        mNeedsUpdate = false;
    }
}

int DanceRemixer::JumpedMoveIdxAdd(int idx, int add) const {
    return JumpedMeasureAdd(idx + 1, add) - 1;
}

void DanceRemixer::SelectMove(int, int) {}

float DanceRemixer::JumpedBeat(float beat) const {
    int fromBeat = mFromMeasure * 4;
    int toBeat = mToMeasure * 4;
    if ((int)beat < fromBeat) {
        return beat;
    }
    if ((int)beat < toBeat) {
        int jumpSize = toBeat - mFromMeasure * 4;
        if ((int)beat < toBeat - (jumpSize >> 1)) {
            return (float)jumpSize + beat;
        }
        return beat - (float)jumpSize;
    }
    if (toBeat >= fromBeat) {
        return beat;
    }
    // w7-bb FLOOR at 90.89% (4 rows, 824F02AC-824F02CC).  Everything through
    // 824F02A8 matches; the residual is that the image reuses the SINGLE
    // conversion scratch slot -0x10(r1) for both int->float conversions
    // (std r11,-0x10 / lfd f0,-0x10 / std r10,-0x10 / lfd f13,-0x10) where we
    // spill the second one to -0x8(r1), which also flips the fcfid/frsp
    // scheduling into an f0<->f13 swap.  That is a frame-temp allocation
    // decision, not an expression shape: three spellings were all byte-inert
    // at 90.88636 -- splitting into two statements (float shifted = beat -
    // (float)fromBeat; return shifted + (float)toBeat), reversing the fadds
    // operands ((float)toBeat + (beat - (float)fromBeat)), and hoisting
    // (int)beat into a named local used by all three compares.
    return (beat - (float)fromBeat) + (float)toBeat;
}

int DanceRemixer::JumpedMoveIdx(int idx) const { return Round(JumpedBeat(idx * 4)) / 4; }

int DanceRemixer::JumpedMeasureAdd(int measure, int count) const {
    int step = count > 0 ? 1 : -1;
    int absCount = count < 0 ? -count : count;
    for (int i = 0; i < absCount; i++) {
        measure = JumpedMoveIdx(measure + step - 1) + 1;
    }
    return measure;
}

int DanceRemixer::JumpedMeasureStepsBetween(int from, int to, int direction) const {
    MILO_ASSERT(direction == 1 || direction == -1, 0x1bd);
    int count = 0;
    while (from != to) {
        count += direction;
        if ((count < 0 ? -count : count) > mTotalMeasures * 2) {
            TheDebug.Fail(MakeString("JumpedMeasureDifference: can't get from measure %d to measure %d\n", from, to), nullptr);
        }
        from = JumpedMeasureAdd(from, direction);
    }
    return count;
}

const MoveVariant *DanceRemixer::MoveVariantFromHamMove(const HamMove *aHamMove) const {
    MILO_ASSERT(aHamMove, 0x1f7);
    Symbol moveName = aHamMove->Name();

    for (int i = 0; i < 2; i++) {
        const auto &measures = TheMoveMgr->mRoutineMeasures[i];
        for (unsigned int j = 0; j < measures.size(); j++) {
            if (JumpedMoveIdx(j) == (int)j) {
                // Through locals: the image loads `.first` once (`lwzx r3, r5,
                // r7`) and forms the element address alongside it (`add r11,
                // r5, r7`), then reads `.second` off that address (`lwz r3,
                // 0x4(r11)`) and returns the loaded value straight from r3.
                // Writing `measures[j].first` again in the return re-indexes:
                // MSVC emits a fresh `slwi r11, r4, 0x3` + `lwzx` per exit,
                // seven extra instructions across the two return sites.
                const MoveVariant *first = measures[j].first;
                if (first && first->HamMoveName() == moveName) {
                    return first;
                }
                const MoveVariant *second = measures[j].second;
                if (second && second->HamMoveName() == moveName) {
                    return second;
                }
            }
        }
    }

    // Fall back to searching the move graph's variant map
    const std::map<Symbol, MoveVariant *> &variants = TheMoveMgr->mMoveGraph.MoveVariants();
    for (std::map<Symbol, MoveVariant *>::const_iterator it = variants.begin();
         it != variants.end(); ++it) {
        if (it->second->HamMoveName() == moveName) {
            return it->second;
        }
    }

    return nullptr;
}

const MoveParent *DanceRemixer::GetMoveParent(int x, int y) {
    return TheMoveMgr->CurParents(x)[y];
}

void BuildSetOfPrevAdjacentMoveParents(
    std::set<const MoveParent *> &s1, const std::set<const MoveParent *> &s2
) {
    std::set<const MoveParent *>::const_iterator it = s2.begin();
    std::set<const MoveParent *>::const_iterator end = s2.end();
    while (it != end) {
        const MoveParent *moveParent = *it;
        const std::vector<const MoveParent *> &prevAdjs = moveParent->PrevAdjacents();
        for (unsigned int i = 0; i < prevAdjs.size(); i++) {
            const MoveParent *prevAdj = prevAdjs[i];
            if (!prevAdj->HasRestMoveVariant() && !prevAdj->HasFinalMoveVariant()) {
                s1.insert(prevAdj);
            }
        }
        ++it;
    }
}

void DanceRemixer::SetUnscoredMeasureRange(int x, int y, int z) {
    for (int i = y; i <= z; i++) {
        mUnscoredMeasures[x].insert(i);
    }
}

void DanceRemixer::ClearUnscoredMeasureRange(int x, int y, int z) {
    for (int i = y; i < z; i++) {
        mUnscoredMeasures[x].erase(i);
    }
}

// Adds a move to the routine at the given measure, then propagates it to all jump targets
// originating from that measure (if any are registered in mJumpMap)
void DanceRemixer::AddRoutineMove(
    int player, int measure, const MoveParent *moveParent, const MoveVariant *moveVariant
) {
    // Set the move at the initial measure
    TheMoveMgr->mMoveParents[player][measure] = moveParent;
    TheMoveMgr->mPreferredVariants[player][measure] = moveVariant;
    TheMoveMgr->FillInRoutineAt(player, measure);
    TheMoveMgr->InsertMoveInSong(TheMoveMgr->mRoutineMeasures[player][measure].first, measure, player);

    // Propagate the same move along the jump chain from this measure
    std::map<int, int>::iterator it = mJumpMap.find(measure);
    while (it != mJumpMap.end()) {
        int jumpTarget = it->second;
        if (jumpTarget >= mTotalMeasures) {
            MILO_NOTIFY(
                "Jump target to index %d is out of bounds of the song (0 to %d)!",
                jumpTarget,
                mTotalMeasures - 1
            );
            return;
        }
        // Apply the same move at the jump target measure
        measure = jumpTarget;
        TheMoveMgr->mMoveParents[player][jumpTarget] = moveParent;
        TheMoveMgr->mPreferredVariants[player][jumpTarget] = moveVariant;
        TheMoveMgr->FillInRoutineAt(player, jumpTarget);
        TheMoveMgr->InsertMoveInSong(TheMoveMgr->mRoutineMeasures[player][jumpTarget].first, jumpTarget, player);
        it = mJumpMap.find(measure);
    }
}
