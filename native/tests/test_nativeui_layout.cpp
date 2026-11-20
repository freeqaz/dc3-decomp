// nativeui layout, focus and Screen: the two layout passes, event routing and
// bubbling, spatial focus movement, and the per-frame loop on an injected clock.

#include "nativeui_test_support.h"

#include "nativeui/Clock.h"
#include "nativeui/Focus.h"
#include "nativeui/Layout.h"
#include "nativeui/Screen.h"

using namespace nativeui;
using namespace nativeui_test;

namespace {

    std::unique_ptr<Box>
    MakeBox(float width, float height, bool focusable = false, std::string name = "") {
        return std::make_unique<Box>(Size { width, height }, focusable, std::move(name));
    }

}

// ---------------------------------------------------------------------------
// Constraints
// ---------------------------------------------------------------------------

TEST(UiCoreLayout, ConstraintsClampAndDeflate) {
    const Constraints loose = Constraints::Loose({ 100.0f, 50.0f });
    EXPECT_EQ(loose.Constrain({ 200.0f, 20.0f }), (Size { 100.0f, 20.0f }));
    const Constraints tight = Constraints::Tight({ 100.0f, 50.0f });
    EXPECT_EQ(tight.Constrain({ 1.0f, 999.0f }), (Size { 100.0f, 50.0f }));
    EXPECT_EQ(tight.Loosened().minWidth, 0.0f);
    const Constraints inner = tight.Deflated(Insets::All(10.0f));
    EXPECT_EQ(inner.maxWidth, 80.0f);
    EXPECT_EQ(inner.maxHeight, 30.0f);
    const Constraints open;
    EXPECT_FALSE(open.HasBoundedWidth());
    EXPECT_EQ(open.Biggest(), (Size { 0.0f, 0.0f }));
    EXPECT_EQ(loose.Biggest(), (Size { 100.0f, 50.0f }));
}

TEST(UiCoreLayout, AWidgetNeverMeasuresOutsideItsConstraints) {
    const Harness h;
    Box greedy({ 500.0f, 500.0f });
    EXPECT_EQ(
        greedy.Measure(h.Ctx(), Constraints::Loose({ 100.0f, 50.0f })),
        (Size { 100.0f, 50.0f })
    );
    EXPECT_EQ(greedy.MeasuredSize(), (Size { 100.0f, 50.0f }));
}

// ---------------------------------------------------------------------------
// Row / Column
// ---------------------------------------------------------------------------

TEST(UiCoreLayout, RowPlacesFixedChildrenWithAGap) {
    const Harness h;
    Row row(10.0f, CrossAlign::Start);
    Box &a = row.Add(MakeBox(30, 20));
    Box &b = row.Add(MakeBox(50, 40));
    Box &c = row.Add(MakeBox(20, 10));
    EXPECT_EQ(
        row.Measure(h.Ctx(), Constraints {}), (Size { 120.0f, 40.0f })
    ); // 30+10+50+10+20
    h.Frame(row, { 300.0f, 100.0f });
    EXPECT_EQ(a.Bounds(), (Rect { 0, 0, 30, 20 }));
    EXPECT_EQ(b.Bounds(), (Rect { 40, 0, 50, 40 }));
    EXPECT_EQ(c.Bounds(), (Rect { 100, 0, 20, 10 }));
}

TEST(UiCoreLayout, FlexibleChildrenShareWhatIsLeft) {
    const Harness h;
    Row row(10.0f, CrossAlign::Stretch);
    Box &fixed = row.Add(MakeBox(40, 20));
    Box &one = row.Add(MakeBox(0, 20), 1.0f);
    Box &two = row.Add(MakeBox(0, 20), 2.0f);
    h.Frame(row, { 300.0f, 100.0f });
    // 300 - 40 fixed - 2 gaps of 10 = 240, split 1:2.
    EXPECT_EQ(fixed.Bounds(), (Rect { 0, 0, 40, 100 }));
    EXPECT_EQ(one.Bounds(), (Rect { 50, 0, 80, 100 }));
    EXPECT_EQ(two.Bounds(), (Rect { 140, 0, 160, 100 }));
    // A row with a flexible child asks for the whole width it may have.
    EXPECT_EQ(row.Measure(h.Ctx(), Constraints::Loose({ 300.0f, 100.0f })).width, 300.0f);
}

TEST(UiCoreLayout, CrossAxisAlignment) {
    const Harness h;
    for (const auto &[align, expectedY, expectedHeight] :
         { std::tuple { CrossAlign::Start, 0.0f, 20.0f },
           std::tuple { CrossAlign::Center, 40.0f, 20.0f },
           std::tuple { CrossAlign::End, 80.0f, 20.0f },
           std::tuple { CrossAlign::Stretch, 0.0f, 100.0f } }) {
        Row row(0.0f, align);
        Box &box = row.Add(MakeBox(30, 20));
        h.Frame(row, { 300.0f, 100.0f });
        EXPECT_EQ(box.Bounds(), (Rect { 0, expectedY, 30, expectedHeight }));
    }
}

TEST(UiCoreLayout, ColumnIsARowTurnedOnItsSide) {
    const Harness h;
    Column column(5.0f); // stretches across by default
    Box &top = column.Add(MakeBox(30, 20));
    Box &middle = column.Add(MakeBox(0, 0), 1.0f);
    Box &bottom = column.Add(MakeBox(10, 30));
    h.Frame(column, { 200.0f, 100.0f });
    EXPECT_EQ(top.Bounds(), (Rect { 0, 0, 200, 20 }));
    EXPECT_EQ(middle.Bounds(), (Rect { 0, 25, 200, 40 })); // 100 - 20 - 30 - 2*5
    EXPECT_EQ(bottom.Bounds(), (Rect { 0, 70, 200, 30 }));
}

// ---------------------------------------------------------------------------
// Stack, Padding, Align, SizedBox
// ---------------------------------------------------------------------------

TEST(UiCoreLayout, StackOverlaysItsChildren) {
    const Harness h;
    Stack stack;
    Box &small = stack.Add(MakeBox(30, 80));
    Box &wide = stack.Add(MakeBox(90, 20));
    EXPECT_EQ(stack.Measure(h.Ctx(), Constraints {}), (Size { 90.0f, 80.0f }));
    const DisplayList list = h.Frame(stack, { 200.0f, 100.0f });
    EXPECT_EQ(small.Bounds(), (Rect { 0, 0, 200, 100 }));
    EXPECT_EQ(wide.Bounds(), (Rect { 0, 0, 200, 100 }));
    EXPECT_EQ(list.commands.size(), 2u); // painted in order, the later on top
}

TEST(UiCoreLayout, PaddingInsetsItsChild) {
    const Harness h;
    auto inner = MakeBox(40, 30);
    Box &box = *inner;
    Padding padding({ 1.0f, 2.0f, 3.0f, 4.0f }, std::move(inner));
    EXPECT_EQ(padding.Measure(h.Ctx(), Constraints {}), (Size { 44.0f, 36.0f }));
    h.Frame(padding, { 100.0f, 100.0f });
    EXPECT_EQ(box.Bounds(), (Rect { 1, 2, 96, 94 }));
}

TEST(UiCoreLayout, AlignPlacesAChildAtItsOwnSize) {
    const Harness h;
    auto inner = MakeBox(40, 30);
    Box &box = *inner;
    Align bottomRight({ 1.0f, 1.0f }, std::move(inner));
    h.Frame(bottomRight, { 100.0f, 100.0f });
    EXPECT_EQ(box.Bounds(), (Rect { 60, 70, 40, 30 }));

    auto second = MakeBox(40, 30);
    Box &centred = *second;
    Align centre({}, std::move(second));
    h.Frame(centre, { 100.0f, 100.0f });
    EXPECT_EQ(centred.Bounds(), (Rect { 30, 35, 40, 30 }));
}

TEST(UiCoreLayout, SizedBoxFixesTheDimensionsItIsGiven) {
    const Harness h;
    SizedBox both(40.0f, 30.0f);
    EXPECT_EQ(both.Measure(h.Ctx(), Constraints {}), (Size { 40.0f, 30.0f }));
    // The parent's limits still win.
    EXPECT_EQ(
        both.Measure(h.Ctx(), Constraints::Loose({ 25.0f, 100.0f })),
        (Size { 25.0f, 30.0f })
    );

    SizedBox widthOnly(40.0f, std::nullopt, MakeBox(999, 17));
    EXPECT_EQ(widthOnly.Measure(h.Ctx(), Constraints {}), (Size { 40.0f, 17.0f }));
    h.Frame(widthOnly, { 40.0f, 17.0f });
    EXPECT_EQ(widthOnly.Children().front()->Bounds(), (Rect { 0, 0, 40, 17 }));
}

TEST(UiCoreLayout, NestedContainersComposeAndScaleWithTheViewport) {
    const Harness h;
    // A header over a body, the body split into a flexible list and a fixed pane.
    const auto build = [](Box *&list, Box *&pane) {
        auto body = std::make_unique<Row>(10.0f, CrossAlign::Stretch);
        list = &body->Add(MakeBox(0, 0), 1.0f);
        pane = &body->Add(MakeBox(100, 0));
        auto column = std::make_unique<Column>(10.0f);
        column->Add(MakeBox(0, 40));
        column->Add(std::move(body), 1.0f);
        return std::make_unique<Padding>(Insets::All(20.0f), std::move(column));
    };
    Box *list = nullptr;
    Box *pane = nullptr;
    auto small = build(list, pane);
    h.Frame(*small, { 1280.0f, 720.0f });
    EXPECT_EQ(list->Bounds(), (Rect { 20, 70, 1130, 630 }));
    EXPECT_EQ(pane->Bounds(), (Rect { 1160, 70, 100, 630 }));
    // Nothing assumes 1280x720.
    auto large = build(list, pane);
    h.Frame(*large, { 1920.0f, 1080.0f });
    EXPECT_EQ(list->Bounds(), (Rect { 20, 70, 1770, 990 }));
    EXPECT_EQ(pane->Bounds(), (Rect { 1800, 70, 100, 990 }));
}

// ---------------------------------------------------------------------------
// Focus and event routing
// ---------------------------------------------------------------------------

TEST(UiCoreFocus, EventsGoToTheFocusedWidgetThenBubbleUp) {
    std::vector<std::string> log;
    Box root({ 100, 100 }, false, "root");
    Box &middle = root.Add(MakeBox(100, 100, false, "middle"));
    Box &leaf = middle.Add(MakeBox(100, 100, true, "leaf"));
    Box &other = root.Add(MakeBox(100, 100, true, "other"));
    for (Box *box : { &root, &middle, &leaf, &other }) {
        box->log = &log;
    }

    FocusManager focus;
    focus.SetFocus(&leaf);
    EXPECT_TRUE(leaf.HasFocus());
    EXPECT_FALSE(other.HasFocus());

    // Nobody consumes: leaf, then each ancestor, and never the sibling.
    EXPECT_FALSE(focus.Dispatch(Act(Action::Accept)));
    EXPECT_EQ(log, (std::vector<std::string> { "leaf", "middle", "root" }));

    // An ancestor consumes: routing stops there.
    log.clear();
    middle.onEvent = [](const Event &) { return true; };
    EXPECT_TRUE(focus.Dispatch(Act(Action::Accept)));
    EXPECT_EQ(log, (std::vector<std::string> { "leaf", "middle" }));

    // The focused widget consumes: nobody else hears of it.
    log.clear();
    leaf.onEvent = [](const Event &event) {
        return std::holds_alternative<TextEvent>(event);
    };
    EXPECT_TRUE(focus.Dispatch(Type(U'x')));
    EXPECT_EQ(log, (std::vector<std::string> { "leaf" }));
}

TEST(UiCoreFocus, OnlyOneWidgetHasFocusAndOnlyFocusableOnes) {
    Box a({ 10, 10 }, true);
    Box b({ 10, 10 }, true);
    Box plain({ 10, 10 }, false);
    FocusManager focus;
    EXPECT_FALSE(focus.Dispatch(Act(Action::Accept))); // nothing focused: nothing happens
    focus.SetFocus(&a);
    focus.SetFocus(&b);
    EXPECT_FALSE(a.HasFocus());
    EXPECT_TRUE(b.HasFocus());
    focus.SetFocus(&plain); // ignored
    EXPECT_EQ(focus.Focused(), &b);
    focus.SetFocus(nullptr);
    EXPECT_FALSE(b.HasFocus());
    EXPECT_EQ(focus.Focused(), nullptr);
}

TEST(UiCoreFocus, FocusMovesToTheNearestWidgetThatLinesUp) {
    const Harness h;
    // Row 1:  [search          ]            (right half)
    // Row 2:  [tabs  ]                      (left half)
    // Row 3:  [list            ][pane ]
    Column column(10.0f, CrossAlign::Start);
    Row &first = column.Add(std::make_unique<Row>(0.0f));
    first.Add(MakeBox(200, 40));
    Box &search = first.Add(MakeBox(200, 40, true, "search"));
    Box &tabs = column.Add(MakeBox(100, 40, true, "tabs"));
    Row &third = column.Add(std::make_unique<Row>(10.0f));
    Box &list = third.Add(MakeBox(200, 200, true, "list"));
    Box &pane = third.Add(MakeBox(100, 200, true, "pane"));
    h.Frame(column, { 400.0f, 400.0f });

    EXPECT_EQ(FindFocusNeighbour(column, list, Direction::Up), &tabs);
    EXPECT_EQ(FindFocusNeighbour(column, tabs, Direction::Down), &list);
    EXPECT_EQ(FindFocusNeighbour(column, list, Direction::Right), &pane);
    EXPECT_EQ(FindFocusNeighbour(column, pane, Direction::Left), &list);
    // The pane is under the search box; tabs are nearer but off to the side.
    EXPECT_EQ(FindFocusNeighbour(column, pane, Direction::Up), &search);
    EXPECT_EQ(FindFocusNeighbour(column, tabs, Direction::Up), &search);
    EXPECT_EQ(FindFocusNeighbour(column, list, Direction::Down), nullptr);
    EXPECT_EQ(FindFocusNeighbour(column, list, Direction::Left), nullptr);
    EXPECT_EQ(FirstFocusable(column), &search);

    FocusManager focus;
    focus.SetFocus(&list);
    EXPECT_TRUE(focus.MoveFocus(column, Direction::Right));
    EXPECT_EQ(focus.Focused(), &pane);
    EXPECT_FALSE(focus.MoveFocus(column, Direction::Right));
    EXPECT_EQ(focus.Focused(), &pane);
}

// ---------------------------------------------------------------------------
// Screen
// ---------------------------------------------------------------------------

namespace {

    struct ScreenFixture {
        FixedAdvanceMeasurer text;
        ManualClock clock;
        Screen screen { { 400.0f, 300.0f }, Dc3Theme(), text, clock };
        Box *top = nullptr;
        Box *bottom = nullptr;

        ScreenFixture() {
            auto column = std::make_unique<Column>(10.0f, CrossAlign::Start);
            top = &column->Add(MakeBox(100, 50, true, "top"));
            bottom = &column->Add(MakeBox(100, 50, true, "bottom"));
            screen.SetRoot(std::move(column));
        }
    };

}

TEST(UiCoreScreen, RoutesEventsThenMovesFocusThenAsksTheScreen) {
    ScreenFixture f;
    EXPECT_EQ(f.screen.Focused(), f.top) << "focus starts on the first focusable widget";
    f.screen.BuildFrame();

    // Unconsumed Down moves focus; a consumed one would not.
    EXPECT_TRUE(f.screen.HandleEvent(Act(Action::Down)));
    EXPECT_EQ(f.screen.Focused(), f.bottom);
    f.bottom->onEvent = [](const Event &) { return true; };
    EXPECT_TRUE(f.screen.HandleEvent(Act(Action::Up)));
    EXPECT_EQ(f.screen.Focused(), f.bottom)
        << "the widget kept the event, so focus stays";
    f.bottom->onEvent = nullptr;

    // Nothing below, and nobody is listening: unhandled.
    EXPECT_FALSE(f.screen.HandleEvent(Act(Action::Down)));
    EXPECT_FALSE(f.screen.HandleEvent(Act(Action::Back)));

    // The screen's hook sees only what fell through, pointer events included.
    std::vector<Event> fellThrough;
    f.screen.onUnhandledEvent = [&](const Event &event) {
        fellThrough.push_back(event);
        return std::holds_alternative<ActionEvent>(event);
    };
    EXPECT_TRUE(f.screen.HandleEvent(Act(Action::Back)));
    EXPECT_FALSE(f.screen.HandleEvent(PointerEvent { { 5.0f, 6.0f }, true }));
    EXPECT_TRUE(f.screen.HandleEvent(Act(Action::Up))); // moves focus: not unhandled
    ASSERT_EQ(fellThrough.size(), 2u);
    EXPECT_EQ(std::get<ActionEvent>(fellThrough[0]).action, Action::Back);
    EXPECT_EQ(std::get<PointerEvent>(fellThrough[1]).position, (Vec2 { 5.0f, 6.0f }));
    EXPECT_EQ(f.screen.Focused(), f.top);
}

TEST(UiCoreScreen, AFrameLaysTheTreeOutInTheViewport) {
    ScreenFixture f;
    const DisplayList &list = f.screen.BuildFrame();
    EXPECT_EQ(f.top->Bounds(), (Rect { 0, 0, 100, 50 }));
    EXPECT_EQ(f.bottom->Bounds(), (Rect { 0, 60, 100, 50 }));
    ASSERT_EQ(list.commands.size(), 2u);
    EXPECT_EQ(std::get<RoundRectFill>(list.commands[1]).rect, (Rect { 0, 60, 100, 50 }));
    EXPECT_TRUE(f.screen.AtRest());
    f.screen.SetRoot(nullptr);
    EXPECT_TRUE(f.screen.BuildFrame().commands.empty());
    EXPECT_EQ(f.screen.Focused(), nullptr);
}

TEST(UiCoreScreen, TheEntranceRunsOnTheClockAndEndsExactlyInPlace) {
    ScreenFixture f;
    const Theme theme = Dc3Theme();
    f.screen.PlayEnter();
    EXPECT_FALSE(f.screen.AtRest());

    // First frame: no time has passed, so the screen is fully transparent.
    EXPECT_TRUE(f.screen.BuildFrame().commands.empty());

    // Half way through: partly faded in, still below its place.
    f.clock.Advance(theme.motion.slow * 0.5);
    const RoundRectFill midway =
        std::get<RoundRectFill>(f.screen.BuildFrame().commands.at(0));
    EXPECT_GT(midway.colors.topLeft.a, 0.0f);
    EXPECT_LT(midway.colors.topLeft.a, 1.0f);
    EXPECT_GT(midway.rect.y, 0.0f);
    EXPECT_LT(midway.rect.y, theme.space.xl);
    EXPECT_FALSE(f.screen.AtRest());

    // With the clock stopped, frames do not advance anything.
    const std::string frozen = Describe(f.screen.BuildFrame());
    EXPECT_EQ(Describe(f.screen.BuildFrame()), frozen);

    f.clock.Advance(theme.motion.slow);
    const RoundRectFill arrived =
        std::get<RoundRectFill>(f.screen.BuildFrame().commands.at(0));
    EXPECT_EQ(arrived.rect, (Rect { 0, 0, 100, 50 }));
    EXPECT_EQ(arrived.colors.topLeft.a, 1.0f);
    EXPECT_TRUE(f.screen.AtRest());
}
