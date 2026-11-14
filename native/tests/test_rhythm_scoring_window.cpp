// RhythmBattlePlayer's scoring window before the first SetWindow (PLAN H-51).
//
// The bug: "new rhythm detector window w/o finalization [garbage] to
// [180.0, 184.0]" on every perform song (MEASURED: [-0.0,-0.0] on DC1 songs,
// [-34734488944640.0,0.0] on disc songs).  RhythmBattlePlayer::Poll passes
// mScoringWindowStart / mScoringWindowEnd to RhythmDetector::GetRecord as soon
// as mWindowElapsedTime reaches 1, before RhythmBattle::Poll's first SetWindow,
// and the image's constructor never writes them (100% matched, no store to
// 0x28c / 0x290).  Heap garbage is then a "new window", which unfinalizes the
// record, so the first real window notifies.  The native constructor sets both
// to -1 under HX_NATIVE, the "no window" RhythmDetector::ClearData writes.
//
// Both tests construct over memory filled with the measured garbage pattern,
// so a constructor that leaves the fields alone fails them.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <new>

#include "test_helpers.h"

#define private public
#define protected public
#include "hamobj/RhythmBattlePlayer.h"
#include "hamobj/RhythmDetector.h"
#undef private
#undef protected

namespace {

class RhythmScoringWindowTest : public EngineTestFixture {};

// The disc songs' garbage word (0xD5FCBA07 = -3.47e13).
const uint32_t kGarbage = 0xD5FCBA07u;

struct PoisonedPlayer {
    void *mem;
    RhythmBattlePlayer *p;
    PoisonedPlayer() {
        mem = ::operator new(sizeof(RhythmBattlePlayer));
        uint32_t *w = static_cast<uint32_t *>(mem);
        for (size_t i = 0; i < sizeof(RhythmBattlePlayer) / 4; i++)
            w[i] = kGarbage;
        p = ::new (mem) RhythmBattlePlayer();
    }
    ~PoisonedPlayer() {
        p->~RhythmBattlePlayer();
        ::operator delete(mem);
    }
};

TEST_F(RhythmScoringWindowTest, ConstructorLeavesNoGarbageWindow) {
    PoisonedPlayer pp;
    EXPECT_EQ(pp.p->mScoringWindowStart, -1.0f);
    EXPECT_EQ(pp.p->mScoringWindowEnd, -1.0f);
}

// End to end on the real RhythmDetector: StartRecording's ClearData, then
// Poll's GetRecord with the not-yet-set window, then the first real window.
// The early call must leave the record finalized -- that is what keeps the
// real window from notifying.
TEST_F(RhythmScoringWindowTest, EarlyGetRecordLeavesTheClearedRecordFinalized) {
    PoisonedPlayer pp;
    RhythmDetector *rd = new RhythmDetector();
    rd->ClearData();
    ASSERT_TRUE(rd->mRecordData.mFinalized);
    rd->GetRecord(pp.p->mScoringWindowStart, pp.p->mScoringWindowEnd, false, Symbol(), nullptr);
    EXPECT_TRUE(rd->mRecordData.mFinalized)
        << "window [" << rd->mRecordData.mWindowStart << "," << rd->mRecordData.mWindowEnd
        << "]: the next real window would notify";
    // control: a real window does unfinalize (so the check above can fail)
    rd->GetRecord(180.0f, 184.0f, false, Symbol(), nullptr);
    EXPECT_FALSE(rd->mRecordData.mFinalized);
    EXPECT_EQ(rd->mRecordData.mWindowStart, 180.0f);
    delete rd;
}

} // namespace
