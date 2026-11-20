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
| `Theme.h` | `Palette` (with the backdrop's wash and band colours, `well`, the highlight and `selectionMarker` colours, the hint bar's button colours, and `star`), `Typography`, `Spacing`, `Radii`, `Motion`, `Metrics` (with the browsing sizes: `searchFieldWidth`, `sidePaneWidth`, `listSecondaryShare`, the difficulty meter cell and the star mark), tokens, `Dc3Theme()` |
| `Animation.h`, `Clock.h` | `Tween`, `Spring`, easing; the clock seam |
| `Input.h` | The event types |
| `Widget.h` | `Context`, `Constraints`, the `Widget` base class |
| `Layout.h` | `Row`, `Column`, `Stack`, `Padding`, `Align`, `SizedBox` |
| `Focus.h` | `FocusManager`, spatial neighbour search |
| `ScrollStrip.h` | The selection and scrolling rules `ListView` and `Shelf` share |
| `Label.h`, `Image.h`, `Panel.h` | Text line; image with placeholder and fade-in; card |
| `Backdrop.h` | A screen's own backdrop: a translucent wash, pools and thin bands in theme colours that drift with the clock. Ambient: it is always at rest, so it never keeps a screen from settling; `Motion::ambientPeriod` of 0 stops it |
| `Decor.h` | The selection rule in one place: `PaintSelectionBar`, `PaintSelectionRing`, `PaintFocusGlow` (filled only with focus, a quiet `selectionMarker` outline without), and `PaintEdgeFade` |
| `Icon.h`, `IconTile.h` | Icons as images: each icon has a fixed `ImageHandle` (`IconImage`), its shape is outlines in a unit square and `IconCoverage` rasterises it at any size; whoever resolves images recognises the handle with `IconOf`. `IconTile` is an icon on a gradient tile, where a row would have a picture |
| `ListView.h` | Virtualised vertical list with sections, a gliding highlight and edge fades (rows fade into the well only at an edge with more rows beyond it); `SetScrollOffset` / `ScrollTarget` restore and save a scroll position |
| `Shelf.h` | Horizontal row of cards |
| `TextField.h` | Single-line editing (`TextEdit` is the logic alone) |
| `TabStrip.h`, `HintBar.h` | Tabs with a gliding indicator; button hints (a one-letter glyph is a round button, in `Hint::tone` when given; a longer glyph is a keycap) |
| `Screen.h` | One screen: tree, focus, events, the frame |

The worked example of a whole screen is the song browser,
`native/src/songbrowser_ui/` ([SONG_BROWSER_UI.md](SONG_BROWSER_UI.md)); the mock
`SampleBrowser` that used to play that part was retired when it landed.

Tests are in `native/tests/test_nativeui_{core,layout,widgets,raster}.cpp`
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
            bounds, bounds.height * 0.5f, CornerColors::Solid(theme.color.keycapFill)
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

Build a tree, hand it to a `Screen`, wire the callbacks.
`songbrowser_ui/SongBrowserScreen.cpp` is the full example; the shape is:

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

## The look, and the rules that hold it

- **One filled highlight.** A widget fills its selection only while it has
  focus and outlines it otherwise; use `Decor.h` and do not paint a highlight
  by hand.
- **Readable sizes.** `Typography` has six styles. The two small ones
  (`label`, `hint`) are in the heavy face because its capitals are taller for
  the same size; no style may have capitals under 10 px at 720 rows
  (`kDc3MenuCapRatio`, `kDc3HeavyCapRatio`).
- **Short motion that eases out.** Tweens are `Motion::fast` 120 ms, `normal`
  180 ms and `slow` 320 ms; the springs are critically damped and within 1% of
  their target in 125 to 160 ms (`UiCoreTheme.MotionIsShortAndEasesOut`).
  Everything is driven by the clock, never by a frame count.
- **Nothing opaque over the game.** A screen's backdrop is a translucent wash;
  text is read against `Palette::well` or a `Panel`.

## Tests

```bash
ninja -C native/build milo-tests
native/build/milo-tests --gtest_filter='UiCore*'
ctest --test-dir native/build -R '^UiCore' --output-on-failure
```

The suites are named `UiCore*` (`NativeUi*` was already taken by
`NativeUiMaterialAssetTest`). They need no GPU and no game data.

To look at a whole screen without a GPU (the song browser's pictures):

```bash
SONG_BROWSER_UI_PNG_DIR=/tmp/ui native/build/milo-tests \
    --gtest_filter=SongBrowserUi.Pictures
```

That picture shows layout, colours and corners. Its text is a dot font and
its images are made-up gradients; it says nothing about how the game's fonts
or the GPU canvas will look.

## Host: the binding to the running game (`native/src/nativeui_host/`)

The core above knows no engine. `nativeui_host` is the other half: it draws a
`Screen` with the engine's canvas and the game's fonts, feeds it the game's
buttons and the keyboard's text, and loads its pictures. It is compiled into
`dc3-native`, `milo-tests` and `dc3-web` (`DC3_NATIVEUI_HOST_SOURCES` in
`native/CMakeLists.txt`).

| File | What it is |
|---|---|
| `NativeUiHost.h` | What a panel owns: `Enter`/`Tick`/`Draw`/`OnButtonDown`/`OnButtonUp`/`Exit`, text capture, per-frame statistics. No game headers |
| `CanvasRenderer.h` | `nativeui::Renderer` on `gfx/Canvas2D.h`. The header states every translation |
| `GlyphSource.h`, `BitmapGlyphSource.h`, `FontSet.h` | Where glyphs come from. `FontSet` is the `TextMeasurer`; measuring and drawing are one walk over the same glyph table, so a run is exactly as wide as it was measured. A sharper source (SDF, a rasterised TrueType) implements `GlyphSource` and nothing else changes |
| `GameFonts.h` | The game's `RndFont` atlases as glyph sources, loaded the way `UILabel` loads them |
| `ImageCache.h`, `GameImages.h` | Pictures by path through the game's loader; `ImageHandle`s that stay valid; an LRU bound |
| `InputBridge.h` | Pad buttons to `Action`s, the engine's text queue to `TextEvent`/`EditEvent`, held-direction repeat |
| `GameBindings.h` | The few lines that need game headers: `ButtonDownMsg` to the host, the UI clock, the output size |
| `HostedScreen.h` | What the host runs: events in, a display list out. A `nativeui::Screen` is one through the host's own adapter; `SongBrowserScreen` through `HostedScreenOf` or a class of the caller's |

### A panel that owns a host

```cpp
class SongBrowserPanel : public UIPanel {
    nativeui_host::GameFonts fonts_;              // Load() in Enter, Unload() in Exit
    nativeui_host::GameImageLoader loader_;
    nativeui_host::ImageCache images_{ loader_ };
    nativeui_host::NativeUiHost host_{ fonts_.Fonts(), &images_ };
    std::unique_ptr<nativeui::Screen> screen_;    // built from widgets, host_.Clock()

    void Enter() override {                       // fonts first: widgets measure with them
        fonts_.Load();
        screen_ = BuildScreen({ 1280, 720 }, fonts_.Fonts(), host_.Clock());
        host_.SetScreen(screen_.get());
        host_.Enter(nativeui_host::GameUiSeconds());
    }
    void Poll() override { host_.Tick(nativeui_host::GameUiSeconds()); }
    void Draw() override {                        // inside the panel's pass
        nativeui_host::SetOutputSize(host_);
        host_.Draw();
    }
    DataNode OnMsg(const ButtonDownMsg &msg) {    // true: the screen took it
        return nativeui_host::OnButtonDown(host_, msg) ? DataNode(1) : DataNode(kDataUnhandled, 0);
    }
    DataNode OnMsg(const ButtonUpMsg &msg) { nativeui_host::OnButtonUp(host_, msg); ... }
    void Exit() override { host_.Exit(); host_.SetScreen(nullptr); screen_.reset(); images_.Clear(); }
};
```

- **`Enter(seconds)`** starts the screen's entrance and the host clock.
  **`Tick(seconds)`** once a frame: advances the clock (a step is clamped to
  0..0.25 s, so a reset or stalled UI clock never runs animations backwards
  or skips them), polls the image cache, drains the text queue into the
  screen, fires held-direction repeats, and updates text capture.
  **`Draw()`** builds the frame and replays it through the canvas at
  1280x720 logical units; outside a render pass it draws nothing.
  **`Exit()`** releases the keyboard and any held direction.
- A screen's animations advance when the frame is **built** (in `Draw`), by
  the host clock's step since the last build. `Tick` without `Draw` moves no
  animation.
- `OnButtonDown` returns true for every button the UI has a meaning for,
  whether or not a widget acted on it, so the game's own menu does not also
  act on it. Square (X), Triangle (Y) and Start are not mapped and return
  false.
- `Stats()` after `Draw()`: display-list commands, canvas quads and draw
  calls, glyphs, placeholder glyphs, image quads with no texture yet, and
  microseconds to build the frame and to replay it (canvas flush included).

### Fonts

`GameFonts::Load()` loads `ui/resource/fonts/<name>.milo` with the game's
loader and takes the **generated** font of the `UILabelDir`
(`UILabelDir::FontObj("")`, what `UILabel::SetFontMat` asks for). The same
directory also holds the artist's source font (`... old kerning.font`); its
glyph table does not describe the generated atlas (measured: it draws `&`
for `C`).

Measured by `NativeUiHostGpuTest.GameFontsLoadWithMeasuredCells` (2026-11-20):

| `FontId` | Resource | Font object | Cell (texels) | Atlas | Glyphs | Cap top / baseline / descender (of the cell) |
|---|---|---|---|---|---|---|
| `kDc3MenuFont` (1) | `eagle-light(47)` | `Eagle-Light(47).font` | 69 x 78 | 1 page, 1024x1024 | 170 | 0.218 / 0.769 / 0.910 |
| `kDc3HeavyFont` (2) | `eagle_light_heavy` | `EagleLightHeavy(10_00)2x.font` | 83.5 x 89.5 | 1 page, 1024x1024 | 170 | 0.168 / 0.793 / 0.927 |
| `kDc3IconFont` (3) | `button_icon` | `icons_buttons.font` | 85 x 85 | 1 page, 512x512 | 21 | not measured (defaults 0.2 / 0.8 / 1.0); drawn with its own colours |

- Song select uses `eagle-light(47)` for every label (string scan of
  `song_select.milo` and the `ham_list_ribbon` milos), plus `default` and
  `icons`. No font has a kerning pair table; base kerning is 0.
- **A text size is the height of the font's cell in logical units** (the
  line box), not the cap height. Capitals of the menu font are 0.55 of the
  size: `body` (26) has 14.3-unit capitals. The cap line and baseline are
  measured from the atlas texels at load (ink rows of `H E T I` and of
  `p g y q j`), so a label is vertically centred on what is drawn.
- `Dc3Theme()` was **not** changed. Its sizes read well with the real font;
  the game's own song rows have taller capitals (about 18.7 units), which
  would be `body` 34.
- A character the font lacks is drawn as its base letter (`é` as `e`, then
  the other case), and failing that as an outlined box half the size wide.
  No UVs are ever invented. All 62 disc songs' titles and artists are
  covered by the menu font, as written and upper-cased (measured with the
  retired demo; the song browser's frames report `placeholder_glyphs` 0 on
  the 45 disc songs).
- `eagle-light-3d*` are `RndFont3d` (a mesh per character). `LoadGameFont`
  refuses a resource whose generated font is not a bitmap `RndFont`; a glyph
  source for them is future work.

**Sharpness.** The atlas cell is 78 texels high, so text is sharp while the
drawn cell is at most about 78 device pixels:

| Style (size) | 1280x720 | 1920x1080 | 2560x1440 |
|---|---|---|---|
| display (44) | 44 px, 0.56x | 66 px, 0.85x | 88 px, **1.13x** |
| title (34) | 34 px, 0.44x | 51 px, 0.65x | 68 px, 0.87x |
| body (26) | 26 px, 0.33x | 39 px, 0.50x | 52 px, 0.67x |
| hint (20) | 20 px, 0.26x | 30 px, 0.38x | 40 px, 0.51x |

Only display text at 1440 rows is magnified, by 13%. Seen in 4x crops of
the retired demo: at 1440 and 1080 rows the strokes are clean; at 720 rows the face
is so light that body strokes are about one pixel wide and grey rather than
white. That is the atlas being minified three times, not blur from
upscaling. A 4K output (2160 rows) would magnify display text 1.7x and body
text exactly 1.0x.

### Images

`ImageCache::Request(path)` returns an `ImageHandle` for a path (the same
handle every time). Loading is the game's: `TheLoadMgr.AddLoader` then
`RndTex::SetBitmap(FileLoader*)`, as `DynamicTex` does. Nothing blocks a
frame: at most `maxConcurrent` (4) loads in flight, at most
`maxFinishPerPoll` (2) textures created per `Poll()`. At most `capacity`
(64) textures are held; the least recently drawn goes first, never one drawn
since the last `Poll()`, and a dropped texture is deleted at the **next**
`Poll()` (the canvas resolves textures when it flushes). `onArrived` fires
once per arrival. A failed path draws the fallback
(`ui/image/dc_logo_keep.png`) and is not retried. `AlbumArtPath(song)` is
`HamSongMgr::GetAlbumArtPath`. An `ImageQuad` whose handle has no texture
yet draws nothing (the widget's placeholder shows through its fade).

Icons are images too. `IconTextures` (`nativeui_host/IconTextures.{h,cpp}`)
sits in front of the host's image source: a handle `nativeui::IconOf`
recognises gets a white texture whose alpha is `IconCoverage` at
`IconTexelSize`, made on first use and kept; every other handle goes on to the
`ImageCache`. The draw tints it, so one texture serves every colour.

### Keys and buttons

| Pad (keyboard) | `nativeui::Action` |
|---|---|
| D-pad / left stick (arrows) | `Up` `Down` `Left` `Right` |
| A (Enter) | `Accept` |
| B (Esc, Backspace) | `Back` |
| LB (Q) / RB (E) | `PrevTab` / `NextTab` |
| LT (Z) / RT (C) | `PageUp` / `PageDown` |
| Back (Tab) | `NextTab` |
| X, Y, Start (X, Y, Space) | not mapped: left to the game |

- **Repeat.** The host repeats a held direction itself: 0.35 s, then every
  0.08 s, on the host clock. A second press of a direction that is already
  held is taken to be the game's own auto-repeat and is swallowed. (The
  game's `JoypadClient` repeats D-pad buttons per `config/joypad.dta`,
  1000 ms then 80 ms; that is from the config and code, not measured in a
  run.)
- **Text.** While the focused widget answers `WantsTextInput()` (a
  `TextField` does) the host holds `TextInputSetCapture(true)`: typed
  characters and Backspace/Delete/Home/End reach the screen as `TextEvent` /
  `EditEvent`, and the letter keys stop being pad buttons. Left and Right
  arrive both as text-edit keys and as D-pad presses; the host keeps the
  pad's copy. `SetTextCapture(Always)` is type-to-search from anywhere, at
  the cost of the letter shortcuts; `Never` leaves the keyboard alone.
  Injected text (`TextInputInject*`, `POST /api/input/text`) is queued
  whatever the capture state.

### The screen that uses it

The song browser on song select
(`native/src/platform/SongBrowserPanel_Native.cpp`,
[../SONG_SELECT.md](../SONG_SELECT.md#the-song-browser)) is the host's one
user. It is not a panel of its own: it hangs on the image's
`SongSelectPanel` through `HX_NATIVE` hooks in that panel's `Enter`, `Exit`,
`Poll`, `Draw` and handlers, so the classic panel stays alive underneath. It
draws from the panel's `Draw()`, once a frame, inside the UI's render pass;
it sets `SetTextCapture(Always)`; and it puts its own `HostedScreen` between
the host and `SongBrowserScreen` to drop input while a pick or a leave is in
flight. `GET /api/songbrowser/state` and `POST /api/input/text`
(`docs/tools/HTTP_DEBUG_SERVER.md`) ask and drive it.

The `DC3_NATIVEUI_DEMO` overlay that used to show `SampleBrowser` over every
frame from the engine's overlay hook is gone, with its `/api/nativeui/state`
endpoint and its per-frame hook in `src/App.cpp`.

## What is deliberately not there yet

- **More than one screen of the game drawn with it.** Song select's browser
  is the only one.
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
- **A run in a browser.** `dc3-web` compiles and links the library and the
  host (`scripts/web/build.sh --debug`); it has not been opened in one.
