# nativeui: the natively drawn UI framework (core)

`native/src/nativeui/` is the foundation the rebuilt menus stand on: a small
retained widget tree that turns input events and elapsed time into a list of
drawing commands. It is plain C++17 in namespace `nativeui`, built as its own
static library (`nativeui` in `native/CMakeLists.txt`), and it depends on
nothing else in the repo: no game objects, no engine GPU code, no GLFW, no
third-party code.

This page describes the **core**: everything that is pure logic. Drawing the
commands, measuring real fonts, loading images and reading the keyboard are
the integration layer's job, behind the five seams listed below. Until that
layer exists the library is linked only into `milo-tests`.

```
input events ─┐
              ├─> Screen ──> widget tree ──> DisplayList ──> Renderer (seam) ──> pixels
clock (seam) ─┘                  │
                     Theme, TextMeasurer (seam)
```

## The fixed points

1. **Coordinates** are logical: origin top-left, y down, floats. The game's
   space is 1280x720 and nothing assumes that size; the sample is tested at
   1280x720 and 1920x1080.
2. **Painting** never touches a GPU. Widgets paint into a `DisplayList` of six
   plain commands through a `Painter`, which applies the current layer
   (offset, uniform scale, opacity) as it records. A list therefore holds
   final coordinates and final colours, and a `Renderer` only has to draw what
   it is told, in order.
3. **Text** is never rasterised here. A `TextMeasurer` answers widths and font
   metrics; `Text.h` does single-line fitting with an ellipsis, alignment and
   upper-casing as a style option, all in whole UTF-8 characters.
4. **Theme**: every colour, size, radius and duration is a design token in one
   `Theme` value passed down the tree. Widgets hold *which* token they use
   (`&Palette::accent`), never a value. `Dc3Theme()` carries values sampled
   from the game's song select, with the sample coordinates in comments.
5. **Animation** is in seconds from an injected clock, never in frames: tweens
   (fixed duration, easing) and a closed-form critically damped spring. Both
   land on their target exactly and then report being at rest.
6. **Layout** is two passes over the retained tree: measure with constraints,
   then arrange. Arrange never measures again.
7. **Input** is one `Event` variant. The focused widget gets it first, then
   its ancestors; a directional action nobody consumed moves focus; what is
   left goes to the screen's own hook.
8. **A `Screen`** owns the tree, takes events, reads the clock and returns the
   frame's display list.

## The seams

These are the only things the integration layer implements or produces.

| Seam | Header | Contract |
|---|---|---|
| `Renderer` | `Renderer.h` | One `Draw` per command kind plus `PushClip`/`PopClip`. `Replay(list, renderer)` sends a list to it. `RecordingRenderer` is the test implementation. |
| `TextMeasurer` | `Text.h` | `AdvanceWidth(font, size, utf8)` with no tracking, and `Metrics(font, size)` (ascent, descent, line height). Must agree with what the renderer draws. `FixedAdvanceMeasurer` is the test implementation. |
| `ImageHandle` | `DisplayList.h` | An opaque `uint32` id; 0 is "no image". The framework never loads or sizes images. |
| `Event` | `Input.h` | `ActionEvent`, `TextEvent`, `EditEvent`, `PointerEvent` (reserved). The integration layer translates keys, pads and gestures into these. |
| `Clock` | `Clock.h` | `NowSeconds()`, monotonic. `ManualClock` is the test implementation. |

### The display-list commands

| Command | Fields | Meaning |
|---|---|---|
| `RoundRectFill` | rect, radius, four corner colours, softness | A filled rounded rectangle; colours blend across it. `softness` is the width of the alpha ramp centred on the edge; 0 is an ordinary one-pixel antialiased edge. Glows and shadows are fills with a large softness. |
| `RoundRectStroke` | rect, radius, four corner colours, thickness, softness | A band `thickness` wide just inside the same edge. |
| `ImageQuad` | rect, image, uv rect, tint, radius | An image (uv 0..1, origin top-left) times a tint, with rounded corners. |
| `TextRun` | origin, font, size, colour, letter spacing, text | One line. `origin` is the left end of the baseline; the text is already in its display case and already cut to fit. |
| `ClipPush` | rect | Restrict drawing to a rectangle. Already intersected with the enclosing clips, so a renderer can use it as given. |
| `ClipPop` | | Undo the last `ClipPush`. |

Colours are sRGB, 0..1, straight alpha. `Describe(list)` prints a list one
command per line; that text is the golden format the tests use.

## File map

| File | What it holds |
|---|---|
| `Geometry.h`, `Color.h` | `Vec2`, `Size`, `Insets`, `Rect`; `Color`, `Mix` |
| `Utf8.h` | Decode, encode, step by whole characters, upper-case (Latin) |
| `Text.h` | `TextStyle`, the `TextMeasurer` seam, `FitLine`, alignment, baselines |
| `FixedAdvanceMeasurer.h` | The fake measurer the tests use |
| `DisplayList.h` | The commands, `Describe`, `Layer`, `Painter` |
| `Renderer.h` | The `Renderer` seam, `Replay`, `RecordingRenderer` |
| `Theme.h` | `Palette`, `Typography`, `Spacing`, `Radii`, `Motion`, `Metrics`, tokens, `Dc3Theme()` |
| `Animation.h`, `Clock.h` | `Tween`, `Spring`, easing; the clock seam |
| `Input.h` | The event types |
| `Widget.h` | `Context`, `Constraints`, the `Widget` base class |
| `Layout.h` | `Row`, `Column`, `Stack`, `Padding`, `Align`, `SizedBox` |
| `Focus.h` | `FocusManager`, spatial neighbour search |
| `ScrollStrip.h` | The selection and scrolling rules `ListView` and `Shelf` share |
| `Label.h`, `Image.h`, `Panel.h` | Text line; image with placeholder and fade-in; card |
| `ListView.h` | Virtualised vertical list with sections and a gliding highlight |
| `Shelf.h` | Horizontal row of cards |
| `TextField.h` | Single-line editing (`TextEdit` is the logic alone) |
| `TabStrip.h`, `HintBar.h` | Tabs with a gliding indicator; button hints |
| `Screen.h` | One screen: tree, focus, events, the frame |
| `SampleBrowser.h` | A mock song browser, the worked example |

Tests are in `native/tests/test_nativeui_{core,layout,widgets,sample}.cpp`
with helpers in `nativeui_test_support.h`. `nativeui_raster.h` is a software
rasteriser and PNG writer for looking at a layout; it is a test tool only.

## How a frame works

`Screen::BuildFrame()` runs four passes over the tree, in this order:

1. **Advance**(`dt`): animations move.
2. **Measure**(constraints): each widget says how big it wants to be.
3. **Arrange**(bounds): each widget is given its rectangle and places its
   children. Lists create the rows they now need here.
4. **Paint**: each widget records commands.

Every pass gets a `Context` (the theme and the text measurer). Events get no
context and only change state; the next frame shows the result.

## How to write a widget

Derive from `Widget` and override what you need. Read every colour and size
from `ctx.theme`.

```cpp
// A pill with a short text in it.
class Badge : public Widget {
public:
    explicit Badge(std::string text) : text_(std::move(text)) {}

protected:
    Size OnMeasure(const Context &ctx, const Constraints &) override {
        const TextStyle &style = ctx.theme.type.caption;
        const float textWidth =
            MeasureWidth(ctx.text, style, ApplyCase(style.textCase, text_));
        // The base class fits the answer to the constraints.
        return { textWidth + 2.0f * ctx.theme.space.sm, ctx.theme.metric.hintGlyphHeight };
    }

    void OnPaint(const Context &ctx, Painter &painter) const override {
        const Theme &theme = ctx.theme;
        const TextStyle &style = theme.type.caption;
        const Rect &bounds = Bounds();
        painter.FillRoundRect(
            bounds, bounds.height * 0.5f, CornerColors::Solid(theme.color.accentDim)
        );
        const FittedLine line = FitLine(
            ctx.text, style, text_, bounds.width - 2.0f * theme.space.sm, theme.type.ellipsis
        );
        const FontMetrics metrics = ctx.text.Metrics(style.font, style.size);
        painter.DrawText(
            { bounds.x + theme.space.sm, CenteredBaseline(metrics, bounds.y, bounds.height) },
            style, theme.color.textPrimary, line.text
        );
    }

private:
    std::string text_;
};
```

The rest of the contract:

- **Children**: take ownership with `Adopt(std::move(child))`. In
  `OnMeasure`, call `child->Measure(ctx, constraints)`; in `OnArrange`, call
  `child->Arrange(ctx, rect)` using `child->MeasuredSize()`. The default
  `OnPaint` paints the children.
- **Animation**: keep a `Tween` or `Spring` as a member, advance it in
  `OnAdvance(ctx, dt)` with parameters from `ctx.theme.motion`, and return
  false from `IsAtRest()` while it moves. When the widget first gets a size,
  snap instead of animating from zero.
- **Input**: override `IsFocusable()` and `HandleEvent()`. Return true only
  for an event you used. Returning false for an arrow you cannot act on (Up
  on the first row) is what lets focus leave the widget. `HasFocus()` says
  whether to draw the focused look.
- **Painting**: balance every `PushClip`/`PushLayer` with its pop. Anything
  drawn inside a layer is moved, scaled and faded by it.
- **Tests**: `Harness` in `nativeui_test_support.h` runs the passes on a bare
  widget and returns the display list; assert on commands and on state.
  Break the widget once and see the test go red before you trust it.

## How to write a screen

Build a tree, hand it to a `Screen`, wire the callbacks. `SampleBrowser.cpp`
is the full example; the shape is:

```cpp
Screen screen({ 1280.0f, 720.0f }, Dc3Theme(), measurer, clock);
const Spacing &space = screen.GetTheme().space;

auto list = std::make_unique<ListView>(
    [&](int row) { return ListRowInfo { rowHeight(row), !isHeader(row) }; },
    [&](int row) { return buildRow(row); }   // called only for rows near the viewport
);
ListView *songs = list.get();
songs->SetRowCount(rowCount);
songs->onSelectionChanged = [&](int row) { showDetail(row); };

auto page = std::make_unique<Column>(space.md, CrossAlign::Stretch);
page->Add(std::make_unique<Label>("Songs", &Typography::display));
page->Add(std::move(list), 1.0f);            // flex 1: takes the remaining height
page->Add(std::make_unique<HintBar>(std::vector<Hint> { { "A", "Select" } }));

screen.SetRoot(std::make_unique<Padding>(Insets::All(space.xl), std::move(page)));
screen.SetFocus(songs);
screen.onUnhandledEvent = [&](const Event &event) { /* type-to-search, Back */ return false; };

// Every frame:
screen.HandleEvent(ActionEvent { Action::Down });   // for each input event
Replay(screen.BuildFrame(), renderer);
```

Things the sample shows that a real screen will want:

- **Type to search from anywhere**: text and edit events nobody consumed are
  forwarded to the search field from `onUnhandledEvent`, so focus can stay on
  the list.
- **Screen-specific layout numbers** (a column width) are named constants at
  the top of the screen's file; everything else comes from the theme.
- **Data changes**: call `ListView::SetRowCount` (or `Reload`) after changing
  what the row callbacks describe. The list rebuilds its rows and jumps
  rather than animating.
- **Redraw only when needed**: `Screen::AtRest()` is true when the next frame
  would be identical to the last.

## Behaviour worth knowing

- **Lists do not wrap.** Up on the first row and Down on the last are not
  consumed, which is how focus leaves a list. Section headers are rows that
  the selection skips.
- **Scrolling** is the smallest move that shows the selection with the
  theme's margin; the first and last items pin it to the ends. The position
  follows on a spring, but never from more than 1.5 viewports away
  (`kLongestGlideInViewports`): End on a long list moves most of the way at
  once, so it is as quick as a short move and the rows between are never
  built.
- **Virtualisation**: a list holds the rows in view plus half a viewport each
  side. 1,000 rows keep about 20 alive.
- **Focus movement** is spatial: the nearest focusable widget whose centre is
  past this one's edge in that direction, with sideways distance counted
  double.
- **Images**: an image present when the widget is made shows at once; one
  that arrives later (`SetImage`) fades in over its placeholder.
- **A shelf** draws up to the theme's glow softness outside its bounds, so
  its first and last cards can grow and glow. Leave that much room around it.

## Tests

```bash
ninja -C native/build milo-tests
native/build/milo-tests --gtest_filter='UiCore*'
ctest --test-dir native/build -R '^UiCore' --output-on-failure
```

The suites are named `UiCore*` (`NativeUi*` was already taken by
`NativeUiMaterialAssetTest`). They need no GPU and no game data.

To look at the sample screen without a GPU:

```bash
NATIVEUI_SAMPLE_PNG=/tmp/sample_layout.png native/build/milo-tests \
    --gtest_filter=UiCoreSample.TheLayoutCanBeLookedAtAsAPicture
```

That picture shows layout, colours and corners. Its text is a dot font and
its images are made-up gradients; it says nothing about how the game's fonts
or the GPU canvas will look.

## What is deliberately not there yet

- **A renderer, a font measurer, image loading, key and pad translation.**
  These are the integration layer. Nothing in the game uses this library yet.
- **Pointer input.** `PointerEvent` exists so the event type will not change;
  no widget handles it and there is no hit testing.
- **Multi-line text**, wrapping, and mixed styles in one line.
- **Layout caching.** Every frame measures and arranges the whole tree. The
  trees are small and lists are virtualised; add dirty tracking when a
  profile asks for it.
- **Rows that know they are selected.** The list draws the highlight; a row
  cannot yet change its own look when selected.
- **A cursor blink**, text selection, clipboard, and input-method composition
  in `TextField`.
- **The background streaks** of the game's menus. The theme carries their
  colours (`streakBlue`, `streakMagenta`); nothing draws them.
- **Upper-casing beyond Latin** (ASCII, Latin-1, Latin Extended-A).
- **Screen-to-screen transitions.** A screen has an entrance (`PlayEnter`);
  there is no navigation stack.
- **A web build of the library.** It is plain C++17 and is declared before
  the Emscripten branch in CMake so that `dc3-web` can link it, but that has
  not been built.
