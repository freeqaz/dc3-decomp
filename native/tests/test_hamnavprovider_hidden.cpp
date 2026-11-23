// HamNavProvider::DataSymbol must not walk past mNavItems when a row is hidden.
//
// Regression test for the SIGSEGV on the SECOND entry to perform_pause_screen
// (HamNavProvider::DataSymbol <- UIListProvider::DataIndex <- HamNavList::Handle
// <- UIPanel::Enter), 2026-11-23, lane A80.  Reproduced on both the pre-A77 and
// the A77 binaries, so it is not a renderer bug.
//
// The chain:
//   * ui/pause/pause.dta's enter runs
//       {set [practice_option_index] {right_hand.hnl data_index pausecommand_practice}}
//     and, at beginner difficulty, then hides that row (set_hidden).
//   * UIListProvider::DataIndex(sym) asks DataSymbol(i) for every
//     i < NumData(), and HamNavProvider::NumData() is mNavItems.size() --
//     hidden rows included.
//   * DataSymbol(idx) treats idx as a VISIBLE-row index: it bumps idx once per
//     hidden row it passes.  With one row hidden, DataSymbol(NumData() - 1)
//     walks to mNavItems[size], past the end.  The image (0x82486BF8, 100%
//     matched) has the same unbounded walk; native reads heap garbage there,
//     keeps finding "hidden" bytes and runs into unmapped memory.
//
// The test makes the overrun observable without a crash: it leaves a stale
// item with a recognisable label in the vector's spare capacity (push six,
// pop one), so an unbounded walk RETURNS that stale label instead of gNullStr.
#include "test_helpers.h"

#include "hamobj/HamNavProvider.h"
#include "utl/Symbol.h"

class HamNavProviderHiddenTest : public EngineTestFixture {};

TEST_F(HamNavProviderHiddenTest, DataSymbolStaysInsideTheItemsWhenARowIsHidden) {
    HamNavProvider *prov = Hmx::Object::New<HamNavProvider>();
    ASSERT_NE(prov, nullptr);
    std::vector<HamNavProvider::NavItem> &items = prov->Items();

    const char *labels[] = { "resume", "restart", "practice", "options", "quit" };
    items.reserve(8);
    for (const char *l : labels) {
        items.push_back(HamNavProvider::NavItem());
        items.back().mLabel = l;
    }
    // A stale element in spare capacity: what an unbounded walk would read.
    items.push_back(HamNavProvider::NavItem());
    items.back().mLabel = "stale_past_end";
    items.back().mHidden = false;
    items.pop_back();
    ASSERT_EQ(items.size(), 5u);
    ASSERT_GE(items.capacity(), 6u);

    // Control: nothing hidden, the visible index is the item index.
    EXPECT_EQ(prov->DataSymbol(4), Symbol("quit"));
    EXPECT_EQ(prov->DataIndex(Symbol("practice")), 2);

    // pause.dta hides the practice row at beginner difficulty.
    items[2].mHidden = true;
    EXPECT_EQ(prov->DataSymbol(2), Symbol("options")); // visible-row mapping
    EXPECT_EQ(prov->DataSymbol(3), Symbol("quit"));
    EXPECT_EQ(prov->DataSymbol(4), Symbol(gNullStr))
        << "DataSymbol(4) with one row hidden walked past mNavItems.size() and "
           "returned a label from beyond the end of the vector";

    // The second pause entry's data_index: the hidden row is not found, and
    // nothing past the end is read on the way.
    EXPECT_EQ(prov->DataIndex(Symbol("practice")), -1);
    EXPECT_EQ(prov->DataIndex(Symbol("stale_past_end")), -1);

    delete prov;
}
