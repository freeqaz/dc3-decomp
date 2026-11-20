// nativeui widgets: ScrollStrip (the list logic), ListView, Shelf, TextField,
// TabStrip, HintBar, Label, Image, Panel. Assertions are on state and on
// display-list commands; nothing is rasterised.

#include "nativeui_test_support.h"

#include "nativeui/Focus.h"
#include "nativeui/HintBar.h"
#include "nativeui/Image.h"
#include "nativeui/Label.h"
#include "nativeui/Backdrop.h"
#include "nativeui/Layout.h"
#include "nativeui/ListView.h"
#include "nativeui/Panel.h"
#include "nativeui/ScrollStrip.h"
#include "nativeui/Shelf.h"
#include "nativeui/TabStrip.h"
#include "nativeui/TextField.h"
#include "nativeui/Utf8.h"

using namespace nativeui;
using namespace nativeui_test;

// ---------------------------------------------------------------------------
// ScrollStrip
// ---------------------------------------------------------------------------

namespace {

    // header, item, item, header, item, item, item: headers 30, items 50.
    ScrollStrip SectionedStrip() {
        ScrollStrip strip;
        strip.SetItems(
            { { 30, false },
              { 50, true },
              { 50, true },
              { 30, false },
              { 50, true },
              { 50, true },
              { 50, true } },
            0.0f
        );
        return strip;
    }

    void SettleStrip(ScrollStrip &strip) {
        for (int i = 0; i < 600 && !strip.AtRest(); ++i) {
            strip.Advance(1.0f / 60.0f, { 16.0f, 0.05f });
        }
    }

}

TEST(UiCoreScrollStrip, ItemsAreLaidEndToEndWithTheGap) {
    ScrollStrip strip;
    strip.SetItems({ { 10, true }, { 20, true }, { 30, true } }, 5.0f);
    EXPECT_EQ(strip.Count(), 3);
    EXPECT_FLOAT_EQ(strip.Start(0), 0.0f);
    EXPECT_FLOAT_EQ(strip.Start(1), 15.0f);
    EXPECT_FLOAT_EQ(strip.Start(2), 40.0f);
    EXPECT_FLOAT_EQ(strip.End(2), 70.0f);
    EXPECT_FLOAT_EQ(strip.ContentExtent(), 70.0f);
}

TEST(UiCoreScrollStrip, SelectionSkipsHeadersAndDoesNotWrap) {
    ScrollStrip strip = SectionedStrip();
    EXPECT_EQ(strip.Selected(), 1)
        << "starts on the first selectable item, not the header";
    EXPECT_FALSE(strip.Step(-1)) << "nothing selectable above: no wrap";
    EXPECT_EQ(strip.Selected(), 1);
    EXPECT_TRUE(strip.Step(1));
    EXPECT_EQ(strip.Selected(), 2);
    EXPECT_TRUE(strip.Step(1));
    EXPECT_EQ(strip.Selected(), 4) << "the header at 3 is skipped";
    EXPECT_TRUE(strip.Step(-1));
    EXPECT_EQ(strip.Selected(), 2);
    EXPECT_FALSE(strip.Select(3)) << "a header cannot be selected";
    EXPECT_FALSE(strip.Select(99));
    EXPECT_TRUE(strip.SelectLast());
    EXPECT_EQ(strip.Selected(), 6);
    EXPECT_FALSE(strip.Step(1)) << "nothing below: no wrap";
    EXPECT_FALSE(strip.SelectLast()) << "already there";
    EXPECT_TRUE(strip.SelectFirst());
    EXPECT_EQ(strip.Selected(), 1);
}

TEST(UiCoreScrollStrip, NewItemsKeepTheSelectionWhenTheyCan) {
    ScrollStrip strip = SectionedStrip();
    strip.Select(4);
    strip.SetItems(
        { { 50, true }, { 50, true }, { 50, true }, { 50, true }, { 50, true } }, 0.0f
    );
    EXPECT_EQ(strip.Selected(), 4);
    strip.SetItems({ { 30, false }, { 50, true } }, 0.0f);
    EXPECT_EQ(strip.Selected(), 1);
    strip.SetItems({ { 30, false } }, 0.0f);
    EXPECT_EQ(strip.Selected(), -1);
    EXPECT_FALSE(strip.Step(1));
    EXPECT_FALSE(strip.Page(1));
    strip.SetItems({}, 0.0f);
    EXPECT_EQ(strip.Selected(), -1);
    EXPECT_FLOAT_EQ(strip.ContentExtent(), 0.0f);
}

TEST(UiCoreScrollStrip, ScrollsJustEnoughToShowTheSelection) {
    ScrollStrip strip;
    strip.SetItems(std::vector<StripItem>(100, { 40, true }), 0.0f);
    strip.SetViewport(200.0f, 40.0f); // five rows, one kept clear at each end
    EXPECT_FLOAT_EQ(strip.ScrollTarget(), 0.0f);
    strip.Select(3); // rows 3 and 4 fit without scrolling
    EXPECT_FLOAT_EQ(strip.ScrollTarget(), 0.0f);
    strip.Select(4); // row 4 ends at 200; with a 40 margin the view must reach 240
    EXPECT_FLOAT_EQ(strip.ScrollTarget(), 40.0f);
    strip.Select(50);
    EXPECT_FLOAT_EQ(strip.ScrollTarget(), 50 * 40.0f + 40.0f + 40.0f - 200.0f);
    strip.Select(49); // still inside the margins: no scroll at all
    EXPECT_FLOAT_EQ(strip.ScrollTarget(), 50 * 40.0f + 40.0f + 40.0f - 200.0f);
    strip.Select(10); // going up: the row's start, less the margin
    EXPECT_FLOAT_EQ(strip.ScrollTarget(), 10 * 40.0f - 40.0f);
    strip.SelectLast();
    EXPECT_FLOAT_EQ(strip.ScrollTarget(), strip.MaxScroll());
    EXPECT_FLOAT_EQ(strip.MaxScroll(), 100 * 40.0f - 200.0f);
    strip.SelectFirst();
    EXPECT_FLOAT_EQ(strip.ScrollTarget(), 0.0f);

    // The position follows on a spring and lands exactly.
    strip.SnapScroll();
    strip.Select(8);
    EXPECT_FLOAT_EQ(strip.ScrollTarget(), 9 * 40.0f + 40.0f - 200.0f);
    EXPECT_FLOAT_EQ(strip.Scroll(), 0.0f) << "selecting does not move it; time does";
    strip.Advance(1.0f / 60.0f, { 16.0f, 0.05f });
    EXPECT_GT(strip.Scroll(), 0.0f);
    EXPECT_LT(strip.Scroll(), strip.ScrollTarget());
    SettleStrip(strip);
    EXPECT_EQ(strip.Scroll(), strip.ScrollTarget());
}

TEST(UiCoreScrollStrip, ALongJumpStartsNearItsTarget) {
    ScrollStrip strip;
    strip.SetItems(std::vector<StripItem>(100, { 40, true }), 0.0f);
    strip.SetViewport(200.0f, 0.0f);
    const float longest = kLongestGlideInViewports * 200.0f;
    strip.Select(7); // 120 away: an ordinary glide from where it is
    EXPECT_FLOAT_EQ(strip.ScrollTarget(), 120.0f);
    EXPECT_FLOAT_EQ(strip.Scroll(), 0.0f);
    strip.SelectLast(); // 3800 away: most of it happens at once
    EXPECT_FLOAT_EQ(strip.ScrollTarget(), 3800.0f);
    EXPECT_FLOAT_EQ(strip.Scroll(), 3800.0f - longest);
    strip.SelectFirst(); // and the same going back
    EXPECT_FLOAT_EQ(strip.Scroll(), longest);
    SettleStrip(strip);
    EXPECT_EQ(strip.Scroll(), 0.0f);
}

TEST(UiCoreScrollStrip, TheFirstSelectableItemShowsTheHeaderAboveIt) {
    ScrollStrip strip = SectionedStrip();
    strip.SetViewport(120.0f, 0.0f);
    strip.SelectLast();
    EXPECT_FLOAT_EQ(strip.ScrollTarget(), strip.MaxScroll());
    strip.SelectFirst(); // row 1 starts at 30; without the rule the header would be cut
                         // off
    EXPECT_FLOAT_EQ(strip.ScrollTarget(), 0.0f);
}

TEST(UiCoreScrollStrip, PagesMoveAboutOneViewport) {
    ScrollStrip strip;
    strip.SetItems(std::vector<StripItem>(100, { 40, true }), 0.0f);
    strip.SetViewport(200.0f, 0.0f);
    EXPECT_TRUE(strip.Page(1));
    EXPECT_EQ(strip.Selected(), 5);
    EXPECT_TRUE(strip.Page(1));
    EXPECT_EQ(strip.Selected(), 10);
    EXPECT_TRUE(strip.Page(-1));
    EXPECT_EQ(strip.Selected(), 5);
    strip.Select(98);
    EXPECT_TRUE(strip.Page(1));
    EXPECT_EQ(strip.Selected(), 99) << "a short last page still moves";
    EXPECT_FALSE(strip.Page(1));
    // A viewport smaller than one row still pages by a row.
    strip.SetViewport(10.0f, 0.0f);
    EXPECT_TRUE(strip.Page(-1));
    EXPECT_EQ(strip.Selected(), 98);
}

TEST(UiCoreScrollStrip, VisibleRangeIsWhatTheViewportTouches) {
    ScrollStrip strip;
    strip.SetItems(std::vector<StripItem>(100, { 40, true }), 0.0f);
    strip.SetViewport(200.0f, 0.0f);
    StripRange range = strip.VisibleRange(0.0f);
    EXPECT_EQ(range.first, 0);
    EXPECT_EQ(range.last, 5);
    range = strip.VisibleRange(100.0f);
    EXPECT_EQ(range.first, 0);
    EXPECT_EQ(range.last, 8); // 300 / 40, rounded up
    strip.Select(50);
    strip.SnapScroll(); // row 50 ends at 2040, so the view is 1840..2040
    range = strip.VisibleRange(0.0f);
    EXPECT_EQ(range.first, 46);
    EXPECT_EQ(range.last, 51);
    strip.Select(51);
    strip.SnapScroll();
    strip.Advance(0.0f, { 16.0f, 0.05f });
    EXPECT_EQ(strip.VisibleRange(0.0f).first, 47);
}

// ---------------------------------------------------------------------------
// ListView
// ---------------------------------------------------------------------------

namespace {

    const Size kListViewport { 300.0f, 400.0f };

    // A list of `count` rows, every `sectionEvery`-th a header (0 = no headers).
    // Each row is a label naming itself ("ROW 12" / "HEADER 10").
    struct TestList {
        int count;
        int sectionEvery;
        int built = 0;
        ListView list;

        TestList(int count, int sectionEvery = 0)
            : count(count), sectionEvery(sectionEvery),
              list(
                  [this](int index) {
                      return IsHeader(index) ? ListRowInfo { 30.0f, false }
                                             : ListRowInfo { 40.0f, true };
                  },
                  [this](int index) -> std::unique_ptr<Widget> {
                      ++built;
                      return std::make_unique<Label>(
                          (IsHeader(index) ? "Header " : "Row ") + std::to_string(index)
                      );
                  }
              ) {
            list.SetRowCount(count);
        }

        bool IsHeader(int index) const {
            return sectionEvery > 0 && index % sectionEvery == 0;
        }
    };

    bool Inside(const Rect &inner, const Rect &outer) {
        return inner.Left() >= outer.Left() && inner.Right() <= outer.Right()
            && inner.Top() >= outer.Top() && inner.Bottom() <= outer.Bottom();
    }

}

TEST(UiCoreListView, OnlyRowsNearTheViewportExist) {
    const Harness h;
    TestList t(1000);
    const DisplayList first = h.Frame(t.list, kListViewport);
    // 400 tall, 40 per row: 10 visible, plus half a viewport of overscan below.
    EXPECT_EQ(t.list.BuiltRowCount(), 15);
    EXPECT_EQ(t.built, 15);
    EXPECT_EQ(CommandsOf<TextRun>(first).size(), 10u) << "only rows in view are painted";
    EXPECT_TRUE(HasText(first, "ROW 0"));
    EXPECT_FALSE(HasText(first, "ROW 10"));

    // Walk the whole list: the number of live rows never grows.
    int mostAlive = 0;
    for (int i = 0; i < 999; ++i) {
        ASSERT_TRUE(t.list.HandleEvent(Act(Action::Down)));
        h.Frame(t.list, kListViewport, 1.0f / 60.0f);
        mostAlive = std::max(mostAlive, t.list.BuiltRowCount());
    }
    EXPECT_LE(mostAlive, 21) << "10 visible + 5 above + 5 below + 1 partial";
    EXPECT_EQ(t.list.SelectedIndex(), 999);
    // And no row was built more than a couple of times on the way down.
    EXPECT_LE(t.list.TotalRowsBuilt(), 1100);

    // A jump to the far end builds the rows around it, not the rows between.
    TestList jump(1000);
    h.Frame(jump.list, kListViewport);
    jump.list.HandleEvent(Act(Action::End));
    const DisplayList settled = h.Settle(jump.list, kListViewport);
    EXPECT_LE(jump.list.BuiltRowCount(), 21);
    EXPECT_LT(jump.list.TotalRowsBuilt(), 60);
    EXPECT_TRUE(HasText(settled, "ROW 999"));
    EXPECT_LE(CommandsOf<TextRun>(settled).size(), 11u);
}

TEST(UiCoreListView, TheSelectionScrollsIntoViewOnASpring) {
    const Harness h;
    TestList t(200);
    FocusManager focus;
    focus.SetFocus(&t.list);
    h.Settle(t.list, kListViewport);
    EXPECT_FLOAT_EQ(t.list.ScrollOffset(), 0.0f);

    for (int i = 0; i < 15; ++i) {
        t.list.HandleEvent(Act(Action::Down));
    }
    EXPECT_EQ(t.list.SelectedIndex(), 15);
    EXPECT_FLOAT_EQ(t.list.ScrollOffset(), 0.0f)
        << "an event changes state; time moves the scroll";

    h.Frame(t.list, kListViewport, 1.0f / 60.0f);
    const float early = t.list.ScrollOffset();
    EXPECT_GT(early, 0.0f);
    h.Frame(t.list, kListViewport, 1.0f / 60.0f);
    EXPECT_GT(t.list.ScrollOffset(), early) << "it eases there over several frames";
    EXPECT_FALSE(t.list.AtRest());

    const DisplayList settled = h.Settle(t.list, kListViewport);
    // Row 15 ends at 640; the theme's margin stays visible below it.
    const float margin = h.theme.metric.listScrollMargin;
    EXPECT_FLOAT_EQ(t.list.ScrollOffset(), 16 * 40.0f + margin - 400.0f);
    EXPECT_TRUE(Inside(t.list.HighlightRect(), t.list.Bounds()));
    EXPECT_TRUE(HasText(settled, "ROW 15"));
    EXPECT_TRUE(t.list.AtRest());
    // At rest, another frame changes nothing.
    EXPECT_EQ(Describe(h.Frame(t.list, kListViewport, 1.0f / 60.0f)), Describe(settled));
}

TEST(UiCoreListView, TheHighlightBarGlidesBetweenRows) {
    const Harness h;
    TestList t(50);
    FocusManager focus;
    focus.SetFocus(&t.list); // only a focused list fills its bar
    h.Settle(t.list, kListViewport);
    const Rect before = t.list.HighlightRect();
    EXPECT_EQ(before, (Rect { 0, 0, 300, 40 }));

    t.list.HandleEvent(Act(Action::Down));
    h.Frame(t.list, kListViewport, 1.0f / 60.0f);
    const Rect during = t.list.HighlightRect();
    EXPECT_GT(during.y, 0.0f);
    EXPECT_LT(during.y, 40.0f) << "between the old row and the new one";

    const DisplayList settled = h.Settle(t.list, kListViewport);
    EXPECT_EQ(t.list.HighlightRect(), (Rect { 0, 40, 300, 40 }));
    // The bar is drawn under the rows: a soft glow, then the bar, then text.
    const std::vector<RoundRectFill> fills = CommandsOf<RoundRectFill>(settled);
    ASSERT_GE(fills.size(), 2u);
    EXPECT_GT(fills[0].softness, 0.0f);
    EXPECT_EQ(fills[1].rect, (Rect { 0, 40, 300, 40 }));
    EXPECT_FLOAT_EQ(fills[1].radius, h.theme.radius.small);
    EXPECT_TRUE(std::holds_alternative<ClipPush>(settled.commands.front()));
    EXPECT_TRUE(std::holds_alternative<ClipPop>(settled.commands.back()));
    EXPECT_TRUE(ClipsBalanced(settled));
}

// The selection rule (nativeui/Decor.h): a list fills its highlight only while
// it has focus; a list that merely remembers its selection outlines it.
TEST(UiCoreListView, OnlyAFocusedListFillsItsHighlight) {
    const Harness h;
    TestList t(10);
    const auto onBar = [&](const DisplayList &list) {
        int fills = 0;
        for (const RoundRectFill &fill : CommandsOf<RoundRectFill>(list)) {
            fills += fill.rect == t.list.HighlightRect() ? 1 : 0;
        }
        return fills;
    };
    const DisplayList idle = h.Settle(t.list, kListViewport);
    EXPECT_EQ(onBar(idle), 0) << "no fill without focus";
    const std::vector<RoundRectStroke> marker = CommandsOf<RoundRectStroke>(idle);
    ASSERT_EQ(marker.size(), 1u);
    EXPECT_EQ(marker[0].rect, t.list.HighlightRect());
    EXPECT_EQ(marker[0].colors.topLeft, h.theme.color.selectionMarker);

    FocusManager focus;
    focus.SetFocus(&t.list);
    const DisplayList focused = h.Settle(t.list, kListViewport);
    // A glow, then the bar; and one stroke, the rim, not the marker.
    ASSERT_EQ(onBar(focused), 2);
    const RoundRectFill glow = Nth<RoundRectFill>(focused, 0);
    const RoundRectFill bar = Nth<RoundRectFill>(focused, 1);
    EXPECT_FLOAT_EQ(glow.softness, 2.0f * h.theme.metric.glowSoftness)
        << "a ramp centred on the bar's edge: no flat plateau beside the bar";
    EXPECT_EQ(glow.rect, bar.rect);
    EXPECT_FLOAT_EQ(bar.softness, 0.0f);
    EXPECT_EQ(bar.colors.topLeft, h.theme.color.highlightStart);
    EXPECT_EQ(bar.colors.topRight, h.theme.color.highlightEnd);
    const std::vector<RoundRectStroke> rim = CommandsOf<RoundRectStroke>(focused);
    ASSERT_EQ(rim.size(), 1u);
    EXPECT_EQ(rim[0].colors.topLeft, h.theme.color.highlightRim);
}

// Rows do not stop dead at the list's edge: they fade into the well's colour,
// and only at an edge that has more rows beyond it.
TEST(UiCoreListView, RowsFadeOutOnlyAtAnEdgeWithMoreRowsBeyondIt) {
    const Harness h;
    const auto fades = [&](const DisplayList &list) {
        std::vector<RoundRectFill> out;
        for (const RoundRectFill &fill : CommandsOf<RoundRectFill>(list)) {
            if (fill.rect.height == h.theme.metric.edgeFade) {
                out.push_back(fill);
            }
        }
        return out;
    };
    TestList t(50);
    const std::vector<RoundRectFill> atTop = fades(h.Settle(t.list, kListViewport));
    ASSERT_EQ(atTop.size(), 1u) << "more rows below only";
    EXPECT_FLOAT_EQ(atTop[0].rect.Bottom(), kListViewport.height);
    EXPECT_FLOAT_EQ(atTop[0].colors.topLeft.a, 0.0f) << "clear towards the rows";
    EXPECT_GT(atTop[0].colors.bottomLeft.a, 0.5f) << "the well's colour at the edge";

    for (int i = 0; i < 3; ++i) {
        t.list.HandleEvent(Act(Action::PageDown));
    }
    EXPECT_EQ(fades(h.Settle(t.list, kListViewport)).size(), 2u) << "rows both ways";

    t.list.HandleEvent(Act(Action::End));
    const std::vector<RoundRectFill> atEnd = fades(h.Settle(t.list, kListViewport));
    ASSERT_EQ(atEnd.size(), 1u) << "more rows above only";
    EXPECT_FLOAT_EQ(atEnd[0].rect.y, 0.0f);
    EXPECT_FLOAT_EQ(atEnd[0].colors.bottomLeft.a, 0.0f);

    TestList fits(3);
    EXPECT_TRUE(fades(h.Settle(fits.list, kListViewport)).empty()) << "nothing beyond either edge";
}

// The backdrop is ambient: it moves with the clock, never keeps a screen from
// settling, never reaches full strength, and a period of zero stops it.
TEST(UiCoreBackdrop, DriftsWithTheClockStaysDimAndNeverKeepsAScreenAwake) {
    const Harness h;
    const Size view { 1280.0f, 720.0f };
    Backdrop backdrop;
    const DisplayList first = h.Frame(backdrop, view);
    EXPECT_TRUE(backdrop.AtRest());
    const std::vector<RoundRectFill> fills = CommandsOf<RoundRectFill>(first);
    ASSERT_GT(fills.size(), 3u);
    EXPECT_EQ(fills[0].rect, (Rect { 0, 0, 1280, 720 })) << "the wash";
    EXPECT_LT(fills[0].colors.bottomLeft.a, 0.9f) << "translucent: the game moves under it";
    for (std::size_t i = 1; i < fills.size(); ++i) {
        const CornerColors &c = fills[i].colors;
        const float strongest =
            std::max(std::max(c.topLeft.a, c.topRight.a), std::max(c.bottomLeft.a, c.bottomRight.a));
        EXPECT_LE(strongest, 0.5f + 1e-4f) << "pool or band " << i << " at more than half strength";
    }

    const DisplayList later = h.Frame(backdrop, view, 3.0f);
    EXPECT_NE(Describe(first), Describe(later)) << "it drifts";
    EXPECT_TRUE(backdrop.AtRest());

    // Time, not frames: three seconds in 180 steps end where one step does.
    Backdrop stepped;
    DisplayList last = h.Frame(stepped, view);
    for (int i = 0; i < 180; ++i) {
        last = h.Frame(stepped, view, 3.0f / 180.0f);
    }
    EXPECT_NEAR(Nth<RoundRectFill>(last, 1).rect.x, Nth<RoundRectFill>(later, 1).rect.x, 0.05f);
    EXPECT_NE(Nth<RoundRectFill>(later, 1).rect.x, Nth<RoundRectFill>(first, 1).rect.x);

    // One whole period later it is back where it began.
    Backdrop whole;
    h.Frame(whole, view);
    const DisplayList around = h.Frame(whole, view, h.theme.motion.ambientPeriod);
    EXPECT_NEAR(Nth<RoundRectFill>(around, 1).rect.x, Nth<RoundRectFill>(first, 1).rect.x, 0.05f);

    // A period of zero: it stands still.
    Harness still;
    still.theme.motion.ambientPeriod = 0.0f;
    Backdrop frozen;
    EXPECT_EQ(Describe(still.Frame(frozen, view)), Describe(still.Frame(frozen, view, 5.0f)));
}

TEST(UiCoreListView, SectionHeadersAreRowsTheSelectionSkips) {
    const Harness h;
    TestList t(40, 4); // header at 0, 4, 8, ...
    const DisplayList first = h.Settle(t.list, kListViewport);
    EXPECT_EQ(t.list.SelectedIndex(), 1);
    EXPECT_TRUE(HasText(first, "HEADER 0"));

    std::vector<int> visited;
    t.list.onSelectionChanged = [&](int index) { visited.push_back(index); };
    for (int i = 0; i < 6; ++i) {
        EXPECT_TRUE(t.list.HandleEvent(Act(Action::Down)));
    }
    EXPECT_EQ(visited, (std::vector<int> { 2, 3, 5, 6, 7, 9 }));
    EXPECT_FALSE(t.list.Select(8)) << "a header cannot be selected directly either";

    // Rows have their own heights: header 30, items 40. Row 9 starts at
    // 3 headers + 6 items = 330, and the highlight takes that row's shape.
    h.Settle(t.list, kListViewport);
    const Rect highlight = t.list.HighlightRect();
    EXPECT_FLOAT_EQ(highlight.y + t.list.ScrollOffset(), 330.0f);
    EXPECT_FLOAT_EQ(highlight.height, 40.0f);

    // Home shows the header above the first item again.
    EXPECT_TRUE(t.list.HandleEvent(Act(Action::Home)));
    const DisplayList home = h.Settle(t.list, kListViewport);
    EXPECT_EQ(t.list.SelectedIndex(), 1);
    EXPECT_FLOAT_EQ(t.list.ScrollOffset(), 0.0f);
    EXPECT_TRUE(HasText(home, "HEADER 0"));
}

TEST(UiCoreListView, NavigationKeysAndNoWrap) {
    const Harness h;
    TestList t(100);
    h.Settle(t.list, kListViewport);

    EXPECT_FALSE(t.list.HandleEvent(Act(Action::Up)))
        << "top row: Up is left for the screen";
    EXPECT_TRUE(t.list.HandleEvent(Act(Action::PageDown)));
    EXPECT_EQ(t.list.SelectedIndex(), 10) << "400 / 40 rows";
    EXPECT_TRUE(t.list.HandleEvent(Act(Action::PageDown)));
    EXPECT_EQ(t.list.SelectedIndex(), 20);
    EXPECT_TRUE(t.list.HandleEvent(Act(Action::PageUp)));
    EXPECT_EQ(t.list.SelectedIndex(), 10);
    EXPECT_TRUE(t.list.HandleEvent(Act(Action::End)));
    EXPECT_EQ(t.list.SelectedIndex(), 99);
    EXPECT_FALSE(t.list.HandleEvent(Act(Action::Down))) << "bottom row: no wrap";
    EXPECT_EQ(t.list.SelectedIndex(), 99);
    EXPECT_TRUE(t.list.HandleEvent(Act(Action::End)))
        << "End is the list's key even when already there";
    EXPECT_TRUE(t.list.HandleEvent(Act(Action::Home)));
    EXPECT_EQ(t.list.SelectedIndex(), 0);

    int activated = -1;
    t.list.onActivated = [&](int index) { activated = index; };
    t.list.Select(7);
    EXPECT_TRUE(t.list.HandleEvent(Act(Action::Accept)));
    EXPECT_EQ(activated, 7);

    // Not the list's business.
    EXPECT_FALSE(t.list.HandleEvent(Act(Action::Left)));
    EXPECT_FALSE(t.list.HandleEvent(Act(Action::Back)));
    EXPECT_FALSE(t.list.HandleEvent(Type(U'a')));
    EXPECT_TRUE(t.list.IsFocusable());
}

TEST(UiCoreListView, ReloadRebuildsRowsAndJumps) {
    const Harness h;
    TestList t(100);
    h.Settle(t.list, kListViewport);
    t.list.Select(60);
    h.Settle(t.list, kListViewport);
    EXPECT_GT(t.list.ScrollOffset(), 0.0f);

    // The data shrinks under the list: selection falls back, nothing animates.
    t.count = 5;
    t.list.SetRowCount(5);
    EXPECT_EQ(t.list.BuiltRowCount(), 0);
    const DisplayList after = h.Frame(t.list, kListViewport);
    EXPECT_EQ(t.list.SelectedIndex(), 0);
    EXPECT_FLOAT_EQ(t.list.ScrollOffset(), 0.0f);
    EXPECT_EQ(t.list.HighlightRect(), (Rect { 0, 0, 300, 40 }));
    EXPECT_EQ(t.list.BuiltRowCount(), 5);
    EXPECT_EQ(CommandsOf<TextRun>(after).size(), 5u);

    // An empty list paints only its clip and takes no events.
    t.list.SetRowCount(0);
    const DisplayList empty = h.Frame(t.list, kListViewport);
    EXPECT_EQ(empty.commands.size(), 2u);
    EXPECT_EQ(t.list.SelectedIndex(), -1);
    EXPECT_FALSE(t.list.HandleEvent(Act(Action::Down)));
    EXPECT_FALSE(t.list.HandleEvent(Act(Action::Accept)));
}

// ---------------------------------------------------------------------------
// Shelf
// ---------------------------------------------------------------------------

namespace {

    std::unique_ptr<Shelf> MakeShelf(int count, int *built = nullptr) {
        auto shelf =
            std::make_unique<Shelf>([built](int index) -> std::unique_ptr<Widget> {
                if (built) {
                    ++*built;
                }
                return std::make_unique<Image>(ImageHandle {
                    100u + static_cast<std::uint32_t>(index) });
            });
        shelf->SetCardCount(count);
        return shelf;
    }

    const ImageQuad *FindImage(const std::vector<ImageQuad> &images, std::uint32_t id) {
        for (const ImageQuad &image : images) {
            if (image.image.id == id) {
                return &image;
            }
        }
        // Not there: fail the test, and hand back an empty quad so that the
        // caller's next line does not crash the run.
        static const ImageQuad missing {};
        ADD_FAILURE() << "no image with id " << id << " was drawn";
        return &missing;
    }

}

TEST(UiCoreShelf, SelectsSidewaysWithoutWrapping) {
    const Harness h;
    auto shelf = MakeShelf(5);
    h.Settle(*shelf, { 600.0f, 200.0f });
    std::vector<int> visited;
    int activated = -1;
    shelf->onSelectionChanged = [&](int index) { visited.push_back(index); };
    shelf->onActivated = [&](int index) { activated = index; };

    EXPECT_EQ(shelf->SelectedIndex(), 0);
    EXPECT_FALSE(shelf->HandleEvent(Act(Action::Left))) << "first card: no wrap";
    EXPECT_TRUE(shelf->HandleEvent(Act(Action::Right)));
    EXPECT_TRUE(shelf->HandleEvent(Act(Action::Right)));
    EXPECT_TRUE(shelf->HandleEvent(Act(Action::End)));
    EXPECT_FALSE(shelf->HandleEvent(Act(Action::Right))) << "last card: no wrap";
    EXPECT_TRUE(shelf->HandleEvent(Act(Action::Accept)));
    EXPECT_EQ(visited, (std::vector<int> { 1, 2, 4 }));
    EXPECT_EQ(activated, 4);
    EXPECT_FALSE(shelf->HandleEvent(Act(Action::Down)))
        << "vertical moves belong to the screen";
}

TEST(UiCoreShelf, TheSelectedCardGrowsAndGetsARing) {
    const Harness h;
    const Metrics &metric = h.theme.metric;
    auto shelf = MakeShelf(5);
    FocusManager focus;
    focus.SetFocus(shelf.get());
    DisplayList list = h.Settle(*shelf, { 800.0f, 200.0f });

    std::vector<ImageQuad> images = CommandsOf<ImageQuad>(list);
    ASSERT_EQ(images.size(), 5u);
    const ImageQuad *selected = FindImage(images, 100);
    const ImageQuad *neighbour = FindImage(images, 101);
    ASSERT_TRUE(selected && neighbour);
    EXPECT_FLOAT_EQ(selected->rect.width, metric.shelfCardWidth * metric.shelfFocusScale);
    EXPECT_FLOAT_EQ(neighbour->rect.width, metric.shelfCardWidth);
    // Grown about its centre: it stays centred on its slot.
    EXPECT_FLOAT_EQ(selected->rect.Center().x, metric.shelfCardWidth * 0.5f);
    EXPECT_EQ(images.back().image.id, 100u) << "the selected card is painted on top";
    const std::vector<RoundRectStroke> rings = CommandsOf<RoundRectStroke>(list);
    ASSERT_EQ(rings.size(), 1u);
    EXPECT_EQ(rings[0].colors.topLeft, h.theme.color.accent);
    EXPECT_EQ(rings[0].rect, selected->rect);

    // Move right: for a moment both cards are part-grown, then the new one is.
    shelf->HandleEvent(Act(Action::Right));
    list = h.Frame(*shelf, { 800.0f, 200.0f }, 1.0f / 60.0f);
    images = CommandsOf<ImageQuad>(list);
    EXPECT_GT(FindImage(images, 100)->rect.width, metric.shelfCardWidth);
    EXPECT_GT(FindImage(images, 101)->rect.width, metric.shelfCardWidth);
    list = h.Settle(*shelf, { 800.0f, 200.0f });
    images = CommandsOf<ImageQuad>(list);
    EXPECT_FLOAT_EQ(FindImage(images, 100)->rect.width, metric.shelfCardWidth);
    EXPECT_FLOAT_EQ(
        FindImage(images, 101)->rect.width, metric.shelfCardWidth * metric.shelfFocusScale
    );
    EXPECT_TRUE(ClipsBalanced(list));
}

TEST(UiCoreShelf, WithoutFocusTheSelectionIsOnlyAQuietRing) {
    const Harness h;
    auto shelf = MakeShelf(5);
    const DisplayList list = h.Settle(*shelf, { 800.0f, 200.0f });
    const std::vector<ImageQuad> images = CommandsOf<ImageQuad>(list);
    ASSERT_EQ(images.size(), 5u);
    EXPECT_FLOAT_EQ(FindImage(images, 100)->rect.width, h.theme.metric.shelfCardWidth);
    const std::vector<RoundRectStroke> rings = CommandsOf<RoundRectStroke>(list);
    ASSERT_EQ(rings.size(), 1u);
    EXPECT_EQ(rings[0].colors.topLeft, h.theme.color.selectionMarker);
    EXPECT_EQ(rings[0].rect, FindImage(images, 100)->rect);
    EXPECT_TRUE(CommandsOf<RoundRectFill>(list).empty()) << "no glow";
    // The clip leaves the glow's reach around the shelf.
    const ClipPush clip = Nth<ClipPush>(list, 0);
    EXPECT_EQ(clip.rect, (Rect { 0, 0, 800, 200 }).Inflated(h.theme.metric.glowSoftness));
}

TEST(UiCoreShelf, ScrollsAndBuildsOnlyNearbyCards) {
    const Harness h;
    int built = 0;
    auto shelf = MakeShelf(200, &built);
    const Size viewport { 600.0f, 200.0f };
    h.Settle(*shelf, viewport);
    const int stride = static_cast<int>(h.theme.metric.shelfCardWidth + h.theme.space.md);
    EXPECT_LE(shelf->BuiltCardCount(), 900 / stride + 2);
    EXPECT_FLOAT_EQ(shelf->ScrollOffset(), 0.0f);

    for (int i = 0; i < 120; ++i) {
        shelf->HandleEvent(Act(Action::Right));
    }
    const DisplayList list = h.Settle(*shelf, viewport);
    EXPECT_EQ(shelf->SelectedIndex(), 120);
    EXPECT_GT(shelf->ScrollOffset(), 0.0f);
    EXPECT_LE(shelf->BuiltCardCount(), 1200 / stride + 2);
    EXPECT_LT(built, 60) << "a jump builds the cards at each end, not the 120 between";
    // The selected card is on screen.
    const ImageQuad *selected = FindImage(CommandsOf<ImageQuad>(list), 220);
    ASSERT_TRUE(selected);
    EXPECT_GE(selected->rect.Left(), 0.0f);
    EXPECT_LE(selected->rect.Right(), 600.0f);
}

// ---------------------------------------------------------------------------
// TextField
// ---------------------------------------------------------------------------

TEST(UiCoreTextField, EditingWorksInWholeCharacters) {
    TextEdit edit;
    EXPECT_TRUE(edit.Insert(U'a'));
    EXPECT_TRUE(edit.Insert(0xE9)); // é, two bytes
    EXPECT_TRUE(edit.Insert(0x65E5)); // 日, three bytes
    EXPECT_TRUE(edit.Insert(0x1F3B5)); // 🎵, four bytes
    EXPECT_EQ(edit.text, "a\xC3\xA9\xE6\x97\xA5\xF0\x9F\x8E\xB5");
    EXPECT_EQ(edit.cursor, edit.text.size());

    EXPECT_TRUE(edit.Backspace()); // the whole emoji goes
    EXPECT_EQ(edit.text, "a\xC3\xA9\xE6\x97\xA5");
    EXPECT_TRUE(edit.MoveLeft()); // before 日
    EXPECT_EQ(edit.cursor, 3u);
    EXPECT_TRUE(edit.MoveLeft()); // before é
    EXPECT_EQ(edit.cursor, 1u);
    EXPECT_TRUE(edit.Insert(U'Z')); // in the middle
    EXPECT_EQ(edit.text, "aZ\xC3\xA9\xE6\x97\xA5");
    EXPECT_EQ(edit.cursor, 2u);
    EXPECT_TRUE(edit.Delete()); // é, both bytes
    EXPECT_EQ(edit.text, "aZ\xE6\x97\xA5");
    EXPECT_TRUE(edit.MoveRight());
    EXPECT_EQ(edit.cursor, 5u);
    EXPECT_FALSE(edit.MoveRight());
    EXPECT_FALSE(edit.Delete());
    EXPECT_TRUE(edit.MoveHome());
    EXPECT_FALSE(edit.MoveHome());
    EXPECT_FALSE(edit.MoveLeft());
    EXPECT_FALSE(edit.Backspace());
    EXPECT_TRUE(edit.MoveEnd());
    EXPECT_EQ(edit.cursor, 5u);
    // Control characters are not text.
    EXPECT_FALSE(edit.Insert(U'\n'));
    EXPECT_FALSE(edit.Insert(0x7F));
    EXPECT_FALSE(edit.Insert(0x1B));
    EXPECT_EQ(utf8::Count(edit.text), 3u);
}

TEST(UiCoreTextField, EventsEditTheTextAndReportChanges) {
    TextField field("Search");
    std::vector<std::string> changes;
    field.onChanged = [&](const std::string &text) { changes.push_back(text); };

    EXPECT_TRUE(field.HandleEvent(Type(U's')));
    EXPECT_TRUE(field.HandleEvent(Type(0xE9)));
    EXPECT_TRUE(field.HandleEvent(Key(Edit::Backspace)));
    EXPECT_TRUE(field.HandleEvent(Type(U't')));
    EXPECT_EQ(field.Text(), "st");
    EXPECT_EQ(changes, (std::vector<std::string> { "s", "s\xC3\xA9", "s", "st" }));

    // Cursor movement is not a change; neither is an edit with nothing to do.
    changes.clear();
    EXPECT_TRUE(field.HandleEvent(Key(Edit::CursorHome)));
    EXPECT_EQ(field.Cursor(), 0u);
    EXPECT_TRUE(field.HandleEvent(Key(Edit::Backspace)))
        << "edit keys are the field's, even at the start";
    EXPECT_TRUE(field.HandleEvent(Key(Edit::CursorRight)));
    EXPECT_EQ(field.Cursor(), 1u);
    EXPECT_TRUE(changes.empty());
    EXPECT_TRUE(field.HandleEvent(Key(Edit::Delete)));
    EXPECT_EQ(field.Text(), "s");
    EXPECT_EQ(changes, (std::vector<std::string> { "s" }));

    // Arrow actions move the cursor while they can, then fall through so
    // focus can leave.
    EXPECT_TRUE(field.HandleEvent(Act(Action::Left)));
    EXPECT_FALSE(field.HandleEvent(Act(Action::Left)));
    EXPECT_TRUE(field.HandleEvent(Act(Action::Right)));
    EXPECT_FALSE(field.HandleEvent(Act(Action::Right)));
    EXPECT_TRUE(field.HandleEvent(Act(Action::Home)));
    EXPECT_TRUE(field.HandleEvent(Act(Action::End)));
    EXPECT_FALSE(field.HandleEvent(Act(Action::Accept)));
    EXPECT_FALSE(field.HandleEvent(Act(Action::Down)));
    EXPECT_FALSE(field.HandleEvent(Type(U'\t')))
        << "a control character is not consumed";

    changes.clear();
    field.SetText("abc");
    EXPECT_EQ(field.Cursor(), 3u);
    EXPECT_TRUE(changes.empty()) << "SetText is the program speaking, not the player";
}

TEST(UiCoreTextField, PaintsPlaceholderTextAndCursor) {
    const Harness h;
    const Theme &theme = h.theme;
    TextField field("Search");
    const Size viewport { 200.0f, theme.metric.controlHeight };

    // Empty and unfocused: the placeholder, dimmed, and no cursor.
    DisplayList list = h.Settle(field, viewport);
    std::vector<TextRun> texts = CommandsOf<TextRun>(list);
    ASSERT_EQ(texts.size(), 1u);
    EXPECT_EQ(texts[0].text, "SEARCH");
    EXPECT_EQ(texts[0].color, theme.color.textDisabled);
    EXPECT_EQ(CommandsOf<RoundRectFill>(list).size(), 1u) << "the field, no cursor";
    EXPECT_EQ(Nth<RoundRectStroke>(list, 0).colors.topLeft, theme.color.fieldBorder);
    EXPECT_TRUE(ClipsBalanced(list));

    // Focused, with text: shown in the theme's case, cursor after it.
    FocusManager focus;
    focus.SetFocus(&field);
    field.HandleEvent(Type(U's'));
    field.HandleEvent(Type(0xE9));
    list = h.Settle(field, viewport);
    texts = CommandsOf<TextRun>(list);
    ASSERT_EQ(texts.size(), 1u);
    EXPECT_EQ(texts[0].text, "S\xC3\x89");
    EXPECT_EQ(texts[0].color, theme.color.textPrimary);
    EXPECT_EQ(field.Text(), "s\xC3\xA9") << "what was typed is what is stored";
    EXPECT_EQ(Nth<RoundRectStroke>(list, 0).colors.topLeft, theme.color.accent);
    const std::vector<RoundRectFill> fills = CommandsOf<RoundRectFill>(list);
    ASSERT_EQ(fills.size(), 3u) << "the focus glow, the field, the cursor";
    EXPECT_GT(fills[0].softness, 0.0f);
    const RoundRectFill &cursor = fills[2];
    const TextStyle &style = theme.type.body;
    const float twoGlyphs = MeasureWidth(h.text, style, "S\xC3\x89");
    EXPECT_FLOAT_EQ(cursor.rect.x, theme.space.md + twoGlyphs);
    EXPECT_FLOAT_EQ(cursor.rect.width, theme.metric.cursorWidth);
    EXPECT_EQ(cursor.colors.topLeft, theme.color.accent);

    // Cursor in the middle: after one character.
    field.HandleEvent(Key(Edit::CursorLeft));
    list = h.Settle(field, viewport);
    EXPECT_FLOAT_EQ(
        Last<RoundRectFill>(list).rect.x,
        theme.space.md + MeasureWidth(h.text, style, "S")
    );

    // Longer than the field: the text slides left so the cursor stays inside.
    for (int i = 0; i < 40; ++i) {
        field.HandleEvent(Type(U'w'));
    }
    field.HandleEvent(Key(Edit::CursorEnd));
    list = h.Settle(field, viewport);
    const RoundRectFill scrolledCursor = Last<RoundRectFill>(list);
    const ClipPush clip = Nth<ClipPush>(list, 0);
    EXPECT_LE(scrolledCursor.rect.Right(), clip.rect.Right() + 0.01f);
    EXPECT_GE(scrolledCursor.rect.Left(), clip.rect.Left());
    EXPECT_LT(Nth<TextRun>(list, 0).origin.x, 0.0f);
}

// ---------------------------------------------------------------------------
// TabStrip
// ---------------------------------------------------------------------------

TEST(UiCoreTabStrip, ShoulderButtonsCycleArrowsStopAtTheEnds) {
    TabStrip tabs({ "Title", "Artist", "Skill" });
    std::vector<int> changes;
    tabs.onChanged = [&](int index) { changes.push_back(index); };
    EXPECT_EQ(tabs.SelectedIndex(), 0);

    EXPECT_TRUE(tabs.HandleEvent(Act(Action::NextTab)));
    EXPECT_TRUE(tabs.HandleEvent(Act(Action::NextTab)));
    EXPECT_TRUE(tabs.HandleEvent(Act(Action::NextTab))); // wraps to 0
    EXPECT_TRUE(tabs.HandleEvent(Act(Action::PrevTab))); // wraps to 2
    EXPECT_EQ(changes, (std::vector<int> { 1, 2, 0, 2 }));

    EXPECT_FALSE(tabs.HandleEvent(Act(Action::Right))) << "last tab: arrows do not wrap";
    EXPECT_TRUE(tabs.HandleEvent(Act(Action::Left)));
    EXPECT_TRUE(tabs.HandleEvent(Act(Action::Left)));
    EXPECT_FALSE(tabs.HandleEvent(Act(Action::Left))) << "first tab";
    EXPECT_EQ(tabs.SelectedIndex(), 0);
    EXPECT_FALSE(tabs.HandleEvent(Act(Action::Down)));
    EXPECT_FALSE(tabs.Select(0));
    EXPECT_FALSE(tabs.Select(3));
    EXPECT_TRUE(tabs.Select(2));
    EXPECT_EQ(changes.back(), 2);
}

TEST(UiCoreTabStrip, PaintsLabelsAndAGlidingIndicator) {
    const Harness h;
    const Theme &theme = h.theme;
    TabStrip tabs({ "Title", "Artist", "Skill" });
    const Size viewport { 600.0f, theme.metric.controlHeight };
    DisplayList list = h.Settle(tabs, viewport);

    const std::vector<TextRun> texts = CommandsOf<TextRun>(list);
    ASSERT_EQ(texts.size(), 3u);
    EXPECT_EQ(texts[0].text, "TITLE");
    EXPECT_EQ(texts[0].color, theme.color.textPrimary)
        << "the selected tab is the bright one";
    EXPECT_EQ(texts[1].color, theme.color.textSecondary);
    EXPECT_LT(texts[0].origin.x, texts[1].origin.x);
    EXPECT_LT(texts[1].origin.x, texts[2].origin.x);

    // The indicator sits under the selected tab, exactly as wide as it.
    const float firstWidth =
        MeasureWidth(h.text, theme.type.body, "TITLE") + 2.0f * theme.space.md;
    const float secondWidth =
        MeasureWidth(h.text, theme.type.body, "ARTIST") + 2.0f * theme.space.md;
    RoundRectFill bar = Last<RoundRectFill>(list);
    EXPECT_FLOAT_EQ(bar.rect.x, 0.0f);
    EXPECT_FLOAT_EQ(bar.rect.width, firstWidth);
    EXPECT_FLOAT_EQ(bar.rect.Bottom(), theme.metric.controlHeight);
    EXPECT_FLOAT_EQ(bar.rect.height, theme.metric.tabIndicatorHeight);

    tabs.HandleEvent(Act(Action::NextTab));
    list = h.Frame(tabs, viewport);
    list = h.Frame(tabs, viewport, 1.0f / 60.0f);
    bar = Last<RoundRectFill>(list);
    EXPECT_GT(bar.rect.x, 0.0f);
    EXPECT_LT(bar.rect.x, firstWidth + theme.space.sm) << "on its way to the second tab";
    EXPECT_FALSE(tabs.AtRest());

    list = h.Settle(tabs, viewport);
    bar = Last<RoundRectFill>(list);
    EXPECT_FLOAT_EQ(bar.rect.x, firstWidth + theme.space.sm);
    EXPECT_FLOAT_EQ(bar.rect.width, secondWidth);
    EXPECT_EQ(Nth<TextRun>(list, 1).color, theme.color.textPrimary);
}

// ---------------------------------------------------------------------------
// HintBar
// ---------------------------------------------------------------------------

TEST(UiCoreHintBar, DrawsEachHintAsAPillAndItsText) {
    const Harness h;
    const Theme &theme = h.theme;
    HintBar bar({ { "A", "Select" }, { "Enter", "Search" } });
    const Size wanted = bar.Measure(h.Ctx(), Constraints {});
    EXPECT_FLOAT_EQ(wanted.height, theme.metric.hintGlyphHeight);
    const DisplayList list = h.Frame(bar, wanted);

    const std::vector<RoundRectStroke> pills = CommandsOf<RoundRectStroke>(list);
    const std::vector<TextRun> texts = CommandsOf<TextRun>(list);
    ASSERT_EQ(pills.size(), 2u);
    ASSERT_EQ(texts.size(), 4u);
    EXPECT_EQ(
        TextsOf(list), (std::vector<std::string> { "A", "SELECT", "ENTER", "SEARCH" })
    );
    // A single letter sits in a circle; a word stretches its pill.
    EXPECT_FLOAT_EQ(pills[0].rect.width, pills[0].rect.height);
    EXPECT_FLOAT_EQ(pills[0].radius, pills[0].rect.height * 0.5f);
    EXPECT_GT(pills[1].rect.width, pills[1].rect.height);
    // Left to right, each label inside its pill, and the bar as wide as it asked.
    EXPECT_GE(texts[0].origin.x, pills[0].rect.Left());
    EXPECT_LE(texts[0].origin.x, pills[0].rect.Right());
    EXPECT_GT(texts[1].origin.x, pills[0].rect.Right());
    EXPECT_GT(pills[1].rect.Left(), texts[1].origin.x);
    const float lastEnd =
        texts[3].origin.x + MeasureWidth(h.text, theme.type.hint, "SEARCH");
    EXPECT_FLOAT_EQ(lastEnd, wanted.width);
    EXPECT_FALSE(bar.IsFocusable());
}

// ---------------------------------------------------------------------------
// Label, Image, Panel
// ---------------------------------------------------------------------------

TEST(UiCoreLabel, MeasuresItsTextAndFollowsTheThemeCase) {
    const Harness h;
    const TextStyle &body = h.theme.type.body;
    Label label("D\xC3\xA9j\xC3\xA0 Vu");
    const Size wanted = label.Measure(h.Ctx(), Constraints {});
    EXPECT_FLOAT_EQ(wanted.width, MeasureWidth(h.text, body, "D\xC3\x89J\xC3\x80 VU"));
    EXPECT_FLOAT_EQ(wanted.height, h.text.Metrics(body.font, body.size).lineHeight);

    const DisplayList list = h.Frame(label, wanted);
    const TextRun run = Nth<TextRun>(list, 0);
    EXPECT_EQ(run.text, "D\xC3\x89J\xC3\x80 VU");
    EXPECT_EQ(run.font, body.font);
    EXPECT_FLOAT_EQ(run.size, body.size);
    EXPECT_FLOAT_EQ(run.letterSpacing, body.tracking * body.size);
    EXPECT_EQ(run.color, h.theme.color.textPrimary);
    EXPECT_FLOAT_EQ(run.origin.x, 0.0f);
    EXPECT_FLOAT_EQ(
        run.origin.y,
        CenteredBaseline(h.text.Metrics(body.font, body.size), 0.0f, wanted.height)
    );
}

TEST(UiCoreLabel, CutsWithAnEllipsisAndAligns) {
    const Harness h;
    const TextStyle &caption = h.theme.type.caption;
    Label narrow(
        "Macarena (Bayside Boys Mix)", &Typography::caption, &Palette::textSecondary
    );
    DisplayList list = h.Frame(narrow, { 120.0f, 30.0f });
    TextRun run = Nth<TextRun>(list, 0);
    EXPECT_EQ(run.text.substr(run.text.size() - 3), "...");
    EXPECT_EQ(run.text.substr(0, 4), "MACA");
    EXPECT_LE(MeasureWidth(h.text, caption, run.text), 120.0f);
    EXPECT_EQ(run.color, h.theme.color.textSecondary);

    Label right("Hi", &Typography::caption, &Palette::accent, TextAlign::Right);
    list = h.Frame(right, { 120.0f, 30.0f });
    run = Nth<TextRun>(list, 0);
    EXPECT_FLOAT_EQ(run.origin.x, 120.0f - MeasureWidth(h.text, caption, "HI"));
    EXPECT_EQ(run.color, h.theme.color.accent);

    Label centred("Hi", &Typography::caption, &Palette::accent, TextAlign::Center);
    list = h.Frame(centred, { 120.0f, 30.0f });
    EXPECT_FLOAT_EQ(
        Nth<TextRun>(list, 0).origin.x,
        (120.0f - MeasureWidth(h.text, caption, "HI")) * 0.5f
    );

    Label empty("");
    EXPECT_TRUE(h.Frame(empty, { 120.0f, 30.0f }).commands.empty());
    centred.SetText("Bye");
    EXPECT_TRUE(HasText(h.Frame(centred, { 120.0f, 30.0f }), "BYE"));
}

TEST(UiCoreImage, ShowsAPlaceholderUntilTheImageArrivesThenFadesItIn) {
    const Harness h;
    const Size viewport { 80.0f, 60.0f };
    Image image;
    DisplayList list = h.Settle(image, viewport);
    ASSERT_EQ(list.commands.size(), 1u);
    const RoundRectFill placeholder = std::get<RoundRectFill>(list.commands[0]);
    EXPECT_EQ(placeholder.rect, (Rect { 0, 0, 80, 60 }));
    EXPECT_EQ(placeholder.colors.topLeft, h.theme.color.imagePlaceholder);
    EXPECT_FLOAT_EQ(placeholder.radius, h.theme.radius.small);

    image.SetImage(ImageHandle { 42 });
    EXPECT_FALSE(image.AtRest());
    h.Frame(image, viewport);
    list = h.Frame(image, viewport, h.theme.motion.normal * 0.5f);
    ASSERT_EQ(list.commands.size(), 2u)
        << "placeholder underneath, image fading in over it";
    const ImageQuad fading = std::get<ImageQuad>(list.commands[1]);
    EXPECT_EQ(fading.image, ImageHandle { 42 });
    EXPECT_GT(fading.tint.a, 0.0f);
    EXPECT_LT(fading.tint.a, 1.0f);

    list = h.Settle(image, viewport);
    ASSERT_EQ(list.commands.size(), 1u);
    const ImageQuad shown = std::get<ImageQuad>(list.commands[0]);
    EXPECT_EQ(shown.tint, h.theme.color.imageTint);
    EXPECT_EQ(shown.uv, (Rect { 0, 0, 1, 1 }));
    EXPECT_EQ(shown.rect, (Rect { 0, 0, 80, 60 }));

    // Taking the image away goes straight back to the placeholder.
    image.SetImage(ImageHandle {});
    list = h.Frame(image, viewport);
    ASSERT_EQ(list.commands.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<RoundRectFill>(list.commands[0]));
}

TEST(UiCoreImage, AnImagePresentFromTheStartDoesNotFade) {
    const Harness h;
    Image image(ImageHandle { 7 }, &Palette::imagePlaceholder, &Radii::medium);
    image.SetUv({ 0.25f, 0.0f, 0.5f, 1.0f });
    EXPECT_TRUE(image.AtRest());
    const DisplayList list = h.Frame(image, { 80.0f, 60.0f });
    ASSERT_EQ(list.commands.size(), 1u);
    const ImageQuad shown = std::get<ImageQuad>(list.commands[0]);
    EXPECT_FLOAT_EQ(shown.tint.a, 1.0f);
    EXPECT_FLOAT_EQ(shown.radius, h.theme.radius.medium);
    EXPECT_EQ(shown.uv, (Rect { 0.25f, 0.0f, 0.5f, 1.0f }));
    // It has no size of its own.
    EXPECT_EQ(image.Measure(h.Ctx(), Constraints {}), (Size { 0.0f, 0.0f }));
    EXPECT_EQ(
        image.Measure(h.Ctx(), Constraints::Loose({ 30.0f, 20.0f })),
        (Size { 30.0f, 20.0f })
    );
}

TEST(UiCorePanel, PaintsHaloFillBorderThenChild) {
    const Harness h;
    const Theme &theme = h.theme;
    PanelStyle style;
    style.border = &Palette::panelBorder;
    style.halo = PanelStyle::Halo::Shadow;
    Panel panel(
        style,
        std::make_unique<Padding>(Insets::All(10.0f), std::make_unique<Label>("Hi"))
    );
    const DisplayList list = h.Frame(panel, { 200.0f, 100.0f });

    ASSERT_EQ(list.commands.size(), 4u);
    const RoundRectFill shadow = std::get<RoundRectFill>(list.commands[0]);
    EXPECT_FLOAT_EQ(shadow.softness, theme.metric.shadowSoftness);
    EXPECT_EQ(shadow.colors.topLeft, theme.color.shadow);
    EXPECT_GT(shadow.rect.Bottom(), 100.0f) << "the shadow falls below the panel";
    const RoundRectFill fill = std::get<RoundRectFill>(list.commands[1]);
    EXPECT_EQ(fill.rect, (Rect { 0, 0, 200, 100 }));
    EXPECT_FLOAT_EQ(fill.radius, theme.radius.large);
    EXPECT_FLOAT_EQ(fill.softness, 0.0f);
    EXPECT_EQ(fill.colors.topLeft, theme.color.panelTop);
    EXPECT_EQ(fill.colors.topRight, theme.color.panelTop);
    EXPECT_EQ(fill.colors.bottomLeft, theme.color.panelBottom);
    const RoundRectStroke border = std::get<RoundRectStroke>(list.commands[2]);
    EXPECT_EQ(border.colors.topLeft, theme.color.panelBorder);
    EXPECT_FLOAT_EQ(border.thickness, theme.metric.borderWidth);
    EXPECT_EQ(std::get<TextRun>(list.commands[3]).text, "HI");
}

TEST(UiCorePanel, StylesAreTokensAndAnEmptyPanelFillsItsSpace) {
    const Harness h;
    const Theme &theme = h.theme;
    Panel plain;
    EXPECT_EQ(
        plain.Measure(h.Ctx(), Constraints::Loose({ 50.0f, 40.0f })),
        (Size { 50.0f, 40.0f })
    );
    DisplayList list = h.Frame(plain, { 50.0f, 40.0f });
    ASSERT_EQ(list.commands.size(), 1u) << "no halo, no border by default";

    PanelStyle glow;
    glow.halo = PanelStyle::Halo::Glow;
    glow.fillTop = &Palette::background;
    glow.fillBottom = &Palette::background;
    glow.radius = &Radii::none;
    Panel glowing(glow);
    list = h.Frame(glowing, { 50.0f, 40.0f });
    ASSERT_EQ(list.commands.size(), 2u);
    const RoundRectFill halo = std::get<RoundRectFill>(list.commands[0]);
    EXPECT_EQ(halo.colors.topLeft, theme.color.accentGlow);
    EXPECT_FLOAT_EQ(halo.softness, theme.metric.glowSoftness);
    const RoundRectFill fill = std::get<RoundRectFill>(list.commands[1]);
    EXPECT_EQ(fill.colors.topLeft, theme.color.background);
    EXPECT_FLOAT_EQ(fill.radius, 0.0f);

    // A different theme restyles the same widget.
    Harness other;
    other.theme.color.background = Color::FromRgb8(0x102030);
    list = other.Frame(glowing, { 50.0f, 40.0f });
    EXPECT_EQ(
        std::get<RoundRectFill>(list.commands[1]).colors.topLeft,
        Color::FromRgb8(0x102030)
    );
}
