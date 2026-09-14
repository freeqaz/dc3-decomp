#include "hamobj\SuperEasyRemixer.h"
#include "OriginalChoreoRemixer.h"
#include "SuperEasyRemixer.h"
#include "hamobj\Difficulty.h"
#include "hamobj\HamDirector.h"
#include "hamobj\HamGameData.h"
#include "hamobj\HamSupereasyData.h"
#include "hamobj\MoveGraph.h"
#include "hamobj\MoveMgr.h"
#include "obj\Data.h"
#include "obj\Dir.h"
#include "obj/Object.h"
#include "os\Debug.h"
#include "utl\DataPointMgr.h"
#include "world\Dir.h"

SuperEasyRemixer::SuperEasyRemixer() {}

BEGIN_HANDLERS(SuperEasyRemixer)
    HANDLE_SUPERCLASS(OriginalChoreoRemixer)
    HANDLE_EXPR(super_easy_data_error, mDataError)
    HANDLE_ACTION(
        send_downgrade_datapoint,
        SendDowngradeDatapoint(
            _msg->Sym(2),
            _msg->Int(3),
            _msg->Sym(4),
            _msg->Int(5),
            _msg->Int(6),
            _msg->Int(7),
            _msg->Str(8)
        )
    )
END_HANDLERS

BEGIN_PROPSYNCS(SuperEasyRemixer)
    SYNC_SUPERCLASS(OriginalChoreoRemixer)
END_PROPSYNCS

BEGIN_SAVES(SuperEasyRemixer)
    SAVE_SUPERCLASS(OriginalChoreoRemixer)
END_SAVES

BEGIN_COPYS(SuperEasyRemixer)
    COPY_SUPERCLASS(OriginalChoreoRemixer)
    CREATE_COPY(SuperEasyRemixer)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mSuperEasyParents)
        COPY_MEMBER(mSuperEasyVariants)
    END_COPYING_MEMBERS
END_COPYS

BEGIN_LOADS(SuperEasyRemixer)
    OriginalChoreoRemixer::Load(bs);
END_LOADS

void SuperEasyRemixer::Reset() { OriginalChoreoRemixer::Reset(); }

void SuperEasyRemixer::Init() {
    OriginalChoreoRemixer::Init();
#ifdef HX_NATIVE
    // Guard: if move graph failed to load, OriginalChoreoRemixer::Init()
    // returned early and mTotalMeasures is uninitialized. Skip the rest.
    if (TheMoveMgr->MoveParents().size() == 0)
        return;
#endif
    SaveSuperEasyMoveParents();
    for (Difficulty d = EasiestDifficulty(); d != kNumDifficulties;
         d = DifficultyOneHarder(d)) {
        std::vector<const MoveParent *> &parents = GetMoveParentsByDifficulty(d);
        if (parents.size() != mTotalMeasures) {
            unsigned int numParents = parents.size();
            MILO_NOTIFY(
                "this song has wrong number of measures in %s track (has %d, want %d)",
                DifficultyToSym(d).Str(),
                numParents,
                mTotalMeasures
            );
        }
    }
    DumpSongLayout();
}

std::vector<const MoveParent *> &SuperEasyRemixer::GetMoveParentsByDifficulty(int diff) {
    if (diff == kDifficultyBeginner) {
        return mSuperEasyParents;
    } else
        return OriginalChoreoRemixer::GetMoveParentsByDifficulty(diff);
}

std::vector<const MoveVariant *> &SuperEasyRemixer::GetMoveVariantsByDifficulty(int diff
) {
    if (diff == kDifficultyBeginner) {
        return mSuperEasyVariants;
    } else
        return OriginalChoreoRemixer::GetMoveVariantsByDifficulty(diff);
}

bool InsertVariants(std::set<const MoveVariant *> &vars, Symbol name) {
    const MoveVariant *mv = TheMoveMgr->Graph().FindMoveByVariantName(name);
    const MoveParent *mp = mv ? mv->Parent() : nullptr;
    if (!mp) {
        return false;
    } else {
        for (int i = 0; i < (int)mp->Variants().size(); i++) {
            const MoveVariant *v = mp->Variants()[i];
            vars.insert(v);
        }
        return true;
    }
}

void SuperEasyRemixer::DumpSongLayout() {
#ifdef HX_NATIVE
    // Guard: on native, move data may not be fully loaded when Init runs.
    // Skip dump if any difficulty track is empty.
    for (Difficulty d = EasiestDifficulty(); d != kNumDifficulties; d = DifficultyOneHarder(d)) {
        if (GetMoveParentsByDifficulty(d).empty()) return;
    }
#endif
    MILO_LOG("\tSUPEREASY\t\tEASY\t\tMEDIUM\t\tHARD\n");
    String str;
    // RESIDUAL (w7-ak, 93.79 canonical): 32 rows, two causes.
    // (1) The image carries `(i-1)*4` as the loop's induction variable (`li r25,
    //     -0x4` in the preheader, `addi r27, r25, 0x4` for the `[i]` index) and
    //     derives the MakeString argument from it as `addi r11, r23, 0x2` after
    //     `subi r23, r20, 0x1` -- i.e. `i - 1` is materialised FIRST. NEGATIVE
    //     RESULT: hoisting it into a named `int prev = i - 1;` used for both
    //     `prev + 2` and the two `[prev]` subscripts does NOT reproduce the
    //     strength reduction (we still emit `slwi r26, r20, 2` and subtract) and
    //     costs raw 93.07 -> 92.9 for a flat canonical.
    // (2) The image keeps &TheDebug in r28 AND home-stores it to 0x58(r31),
    //     reloading it for the final MILO_LOG; our MakeString temp takes 0x58 and
    //     &TheDebug lives in r14 with no home store.
    for (int i = 0; i < mTotalMeasures; i++) {
        str = MakeString("%d", i + 1);
        for (Difficulty d = EasiestDifficulty(); d != kNumDifficulties;
             d = DifficultyOneHarder(d)) {
            str += "\t";
            str += GetMoveParentsByDifficulty(d)[i]->Name();
            Difficulty next = DifficultyOneHarder(d);
            if (next != kNumDifficulties) {
                str += "\t";
                if (i - 1 >= 0) {
                    if (TheMoveMgr->HasVariantPair(
                            GetMoveParentsByDifficulty(next)[i - 1],
                            GetMoveParentsByDifficulty(d)[i]
                        )) {
                        str += "<";
                    } else {
                        str += "_";
                    }
                    if (TheMoveMgr->HasVariantPair(
                            GetMoveParentsByDifficulty(next)[i],
                            GetMoveParentsByDifficulty(d)[i - 1]
                        )) {
                        str += ">";
                    } else {
                        str += "_";
                    }
                }
            }
        }
        if (i == mFromMeasure) {
            str += "\tjump_from";
        }
        if (i == mToMeasure) {
            str += "\tjump_to";
        }
        str += "\n";
        MILO_LOG(str.c_str());
    }
}

void SuperEasyRemixer::SaveSuperEasyMoveParents() {
    mSuperEasyVariants.clear();
    mSuperEasyParents.clear();
    mSuperEasyVariants.reserve(mTotalMeasures);
    mSuperEasyParents.reserve(mTotalMeasures);
    // `bool`, not `int`: the image tests it with `clrlwi. r11, r19, 24` at
    // 0x824F6FB4 and 0x824F7070 -- a byte extract -- and materialises it with
    // `li r19, 0x1` / `li r19, 0x0`.
    bool ok = true;
    HamSupereasyData *data =
        ObjDirItr<HamSupereasyData>(TheHamDirector->GetMoveDir(), false);
    if (data) {
        if (data->mRoutine.size() != mTotalMeasures) {
            MILO_FAIL(
                "HamSuperEasyData has wrong number of measures in routine in song '%s'",
                TheGameData->GetSong().Str()
            );
        }
        // NEGATIVE RESULT (w7-bg): LoadAllVariants floors at 99.57% canonical,
        // 4 charged rows of 239.  Two of them are the vector-size load order:
        // the image loads `_M_start` (0x0) and then `_M_finish` (0x4)
        //   824F6D5C lwz r11, 0x0(r28) / 824F6D64 lwz r10, 0x4(r28)
        //           / subf r10, r11, r10 / divw r10, r10, r25
        // while `size()` as spelled in stl/_vector.h (`_M_finish - _M_start`)
        // makes MSVC load 0x4 first.  The third is an extra `lwz r11, 0x0(r29)`
        // at the top of the loop body: the image keeps `_M_start` live in r11
        // from the bottom-of-loop size computation and reuses it for
        // `mRoutine[i]`, we reload it.
        // Refuted: `i < data->mRoutine.end() - data->mRoutine.begin()`,
        // which was the obvious way to flip the operand order -> 98.46%, worse.
        // The SAME (0x0,0x4) signature shows up on SaveSuperEasyMoveParents
        // (98.44%), GetRows in net_ham/ChallengeSystemJobs (98.97%) and
        // MoveGraph::FindVariantPair (98.10%), so it is a property of how
        // stlport's `size()` is spelled, not of this call site.
        for (int i = 0; i < data->mRoutine.size(); i++) {
            HamSupereasyMeasure &curMeasure = data->mRoutine[i];
            // The image FALLS BACK from `preferred` to `first`.  At 0x824F6F08
            // it loads `preferred` (0x8) into r4 and compares it to gNullStr;
            // on the null path 0x824F6F18 loads `first` (0x0) into the SAME r4
            // and compares it the same way; and FindMoveByVariantName at
            // 0x824F6F3C is then handed whatever r4 holds -- it never re-reads
            // 0x8(r11).  Only when both are null does it take the
            // push_back(nullptr) path at 0x824F6F28.
            //
            // We used to pass `preferred` unconditionally, so every altRev-0
            // song (whose `preferred` is never written during load) MILO_FAILed
            // on an empty symbol.  The HX_NATIVE guard that used to sit here --
            // a three-level preferred/first/second fallback -- was a workaround
            // for exactly this missing branch, and is now redundant: the
            // image's own two-level fallback covers it, and a measure with
            // neither symbol set legitimately has no move.
            Symbol name = curMeasure.preferred;
            if (name.Null() && (name = curMeasure.first).Null()) {
                mSuperEasyVariants.push_back(nullptr);
            } else {
                const MoveVariant *mv = TheMoveMgr->Graph().FindMoveByVariantName(name);
                if (!mv) {
                    MILO_FAIL(
                        "'%s' HamSupereasyData has move '%s' at index %d not found in move graph",
                        TheGameData->GetSong().Str(),
                        name,
                        i
                    );
                    ok = false;
                    break;
                }
                mSuperEasyVariants.push_back(mv);
            }
        }
        if (ok) {
            for (int i = 0; i < mSuperEasyVariants.size(); i++) {
                MoveParent *parent = nullptr;
                if (mSuperEasyVariants[i]) {
                    parent = mSuperEasyVariants[i]->Parent();
                }
                mSuperEasyParents.push_back(parent);
            }
            BridgeGapsInMoveParents(3);
        }
    } else {
        ok = false;
        MILO_NOTIFY("No HamSupereasyData found for song '%s'", TheGameData->GetSong());
    }
    if (!ok) {
        MILO_NOTIFY("Supereasy will use the easy track for '%s'", TheGameData->GetSong());
        mSuperEasyParents = GetMoveParentsByDifficulty(kDifficultyEasy);
        mSuperEasyVariants = GetMoveVariantsByDifficulty(kDifficultyEasy);
    }
    mDataError = !ok;
}

void SuperEasyRemixer::LoadAllVariants() {
    std::set<const MoveVariant *> vars;
    // `Symbol song`, and `song.Str()` at each use -- NOT a `const char *`
    // local.  Str() returns by value, so every MakeString argument (which
    // binds `const char *const &`) gets its OWN materialised temporary; the
    // image stores the cached symbol word into a fresh frame slot right
    // before each call (`stw r21, 0x5c/0x60/0x64(r31)` in
    // build/373307D9/asm/system/hamobj/SuperEasyRemixer.s) and its frame is
    // 0x10 larger than ours for exactly those extra slots.  A named
    // `const char *` local is an lvalue: MSVC homes it once and passes that
    // one address everywhere, which is what we used to emit.
    Symbol song = TheGameData->GetSong();
    if (TheMoveMgr->MoveParents().size() == 0) {
        MILO_FAIL("Failed to load move graph for: %s\n", song.Str());
#ifdef HX_NATIVE
        return; // No move graph — skip variant loading to avoid null deref
#endif
    }
    DataArray *layout = TheMoveMgr->Graph().Layout();
    if (!layout) {
        MILO_FAIL("couldn't load layout for: %s", song.Str());
#ifdef HX_NATIVE
        return; // No layout — skip to avoid null deref on layout->FindArray()
#endif
    }
    for (int i = 0; i < 3; i++) {
        Symbol diffSym = DifficultyToSym((Difficulty)i);
        DataArray *a = layout->FindArray(diffSym, true)->Array(1);
        if (a->Size() == 0) {
            MILO_FAIL(
                "%s's %s layout is not stored in its move graph", song.Str(), diffSym.Str()
            );
        }
        for (int j = 0; j < a->Size(); j++) {
            Symbol s = a->Sym(j);
            if (!InsertVariants(vars, s)) {
                MILO_NOTIFY(
                    "%s's %s layout, at index %d, (%s) not found in move graph",
                    song.Str(),
                    diffSym.Str(),
                    j,
                    s.Str()
                );
            }
        }
    }
    WorldDir *world = TheHamDirector->GetWorld();
    MILO_ASSERT(world, 0x12B);
    ObjectDir *hamMoves = world->Find<ObjectDir>("moves", true);
    MILO_ASSERT(hamMoves, 0x12D);
    HamSupereasyData *data = ObjDirItr<HamSupereasyData>(hamMoves, false);
    if (data) {
        // NEGATIVE RESULT (w7-bg): LoadAllVariants floors at 99.57% canonical,
        // 4 charged rows of 239.  Two of them are the vector-size load order:
        // the image loads `_M_start` (0x0) and then `_M_finish` (0x4)
        //   824F6D5C lwz r11, 0x0(r28) / 824F6D64 lwz r10, 0x4(r28)
        //           / subf r10, r11, r10 / divw r10, r10, r25
        // while `size()` as spelled in stl/_vector.h (`_M_finish - _M_start`)
        // makes MSVC load 0x4 first.  The third is an extra `lwz r11, 0x0(r29)`
        // at the top of the loop body: the image keeps `_M_start` live in r11
        // from the bottom-of-loop size computation and reuses it for
        // `mRoutine[i]`, we reload it.
        // Refuted: `i < data->mRoutine.end() - data->mRoutine.begin()`,
        // which was the obvious way to flip the operand order -> 98.46%, worse.
        // The SAME (0x0,0x4) signature shows up on SaveSuperEasyMoveParents
        // (98.44%), GetRows in net_ham/ChallengeSystemJobs (98.97%) and
        // MoveGraph::FindVariantPair (98.10%), so it is a property of how
        // stlport's `size()` is spelled, not of this call site.
        for (int i = 0; i < data->mRoutine.size(); i++) {
            // BEHAVIOURAL FIX 2026-09-14 (w7-q): this read `.second` (offset
            // 0x4, "MoveVariant to use for transition OUT of measure").  The
            // image reads offset 0x8 -- `.preferred`, "Preferred MoveVariant
            // for this measure".  build/373307D9/asm/system/hamobj/
            // SuperEasyRemixer.s, inside the mRoutine loop (element stride
            // 0xc, so the three Symbols are at 0x0/0x4/0x8):
            //     mulli r10, r29, 0xc
            //     add   r8,  r10, r11
            //     lwz   r30, 0x8(r8)      ; .preferred   <-- not 0x4
            //     cmplw cr6, r30, r9      ; vs gNullStr
            //     bne   cr6, ...
            //     lwzx  r30, r10, r11     ; .first  (offset 0x0) fallback
            // This is the same preferred -> first order the sibling loader
            // above documents for this very function.  Reading `.second` made
            // every supereasy measure request the transition-out variant.
            Symbol name = data->mRoutine[i].preferred;
            if (name.Null())
                name = data->mRoutine[i].first;
            if (!InsertVariants(vars, name)) {
                MILO_NOTIFY(
                    "%s's supereasy layout, at index %d, (%s) not found in move graph",
                    song.Str(),
                    i,
                    name.Str()
                );
            }
        }
    } else {
        MILO_NOTIFY("No HamSupereasyData found for song '%s'", TheGameData->GetSong());
    }
    TheHamDirector->LoadRoutineBuilderData(vars, true);
}

void SuperEasyRemixer::SendDowngradeDatapoint(
    Symbol s1, int i2, Symbol s3, int i4, int i5, int i6, Symbol s7
) {
    if (i6 > 0) {
        static Symbol se_song("se_song");
        static Symbol se_player("se_player");
        static Symbol se_mode("se_mode");
        static Symbol se_shortened("se_shortened");
        static Symbol se_freestyle("se_freestyle");
        static Symbol se_measure("se_measure");
        static Symbol se_move("se_move");
        SendDataPoint(
            "super_easy_downgrade",
            se_song,
            s1,
            se_player,
            i2,
            se_mode,
            s3,
            se_shortened,
            i4,
            se_freestyle,
            i5,
            se_measure,
            i6,
            se_move,
            s7
        );
    }
}
