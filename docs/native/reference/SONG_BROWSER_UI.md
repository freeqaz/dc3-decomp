# The song browser screen

`native/src/songbrowser_ui/` is the natively drawn song browser: one object,
`SongBrowserScreen`, that turns a song catalog and a stream of input events
into a display list. It is built from the UI framework
([NATIVE_UI.md](NATIVE_UI.md)) and fed by the song data layer
([SONG_BROWSER_DATA.md](SONG_BROWSER_DATA.md)), and depends on nothing else:
no game header, no engine header, no renderer, no font code. The static
library `songbrowser_ui` links into `dc3-native`, `dc3-web` and `milo-tests`.

**In the game.** `native/src/platform/SongBrowserPanel_Native.cpp` creates
the screen on `song_select_panel`, runs it through `nativeui_host` (the
game's menu font, the engine's 2D canvas, album art through `ImageCache` with
fallback art) and acts on a picked song. When it is shown instead of the
classic list, and what pick, back and the sounds do, is in
[../SONG_SELECT.md](../SONG_SELECT.md#the-song-browser). The pictures this
page's tests write come from a test-only software rasteriser with a dot font;
pictures from the running game are made with
`GET /api/screenshot` on a run with `DC3_SONG_BROWSER=1`.

## Layout

A 1280x720 logical view, drawn over the game's moving streak background and
hung on the two hairlines of the game's own frame (`Metrics::frameTop` 83.5,
`Metrics::frameBottom` 637): the top bar sits above the upper line, the hint
bar below the lower one, and the list and the detail pane between them with
`Spacing::md` of padding off each line. The pane therefore always ends above
the hint bar's divider.

The screen never fills the view opaquely. It draws its own backdrop first
(`nativeui::Backdrop`): a translucent indigo wash (`Palette::backdropTop` to
`backdropBottom`), two soft pools of light and seven thin bands in the game's
blue, violet and magenta that drift with the clock over
`Motion::ambientPeriod` (48 s; 0 stops them). No pool or band is ever stronger
than half its colour. The list then sits in a well of indigo glass
(`Palette::well`, with the faint `wellBorder`), which is what the row text is
read against. The same layout is drawn at every output size: the canvas
scales the 1280x720 view.

| Part | What it holds |
|---|---|
| Top bar | `SONGS`; the sort tabs Title, Artist, Difficulty, Source, Decade; the search field, whose placeholder `TYPE TO SEARCH` tells a keyboard user they can just type |
| Recently Played | A shelf of album-art cards, newest first. Shown only with a play history and never during a search. It folds away while the list is being browsed, so the list gets the room, and unfolds when focus returns to it, the tabs or the field |
| List | Virtualised, sectioned by the sort. Above the first section sit the visit's command rows (see below). A song row is art, title, artist, a seven-cell difficulty meter, and five stars once the song has been played (filled for earned, outlined for not). During a search it is one flat ranked list under a `N SONGS MATCH` heading, with no command rows. Rows fade into the well at an edge that has more rows beyond it |
| Detail pane | A glass panel. For the selected song: large art with the source game on it, the title (one line, or two when it is long), artist, album and year, the difficulty tier's name and meter, best stars and score (or `NOT PLAYED YET`), length `m:ss`, BPM. For a command row: its icon on a tile, its name and a short description. While a search matches nothing: how to search. It crossfades between them |
| Hint bar | What the controls do right now; it changes with the focused part and with whether a query is typed. A one-letter glyph is a round button in the pad's colour (`buttonAccept`, `buttonBack`), a longer one a keycap; all text is in one colour and the heavy face |

**Why Decade has a tab and BPM does not.** The catalog spans five decades and
"something from the 80s" is a way people pick a song. BPM in ten-beat bands
says little, the difficulty sort already orders songs by effort, the BPM is on
the detail pane, and a sixth tab would crowd the search field at 1280 wide. A
saved `State` that names an arrangement without a tab opens on Title.

**Sections.** By title and by artist, letters with fewer than four songs are
merged into ranges (`123 - B`, `O - R`), so a heading never introduces one
row. Tier and source names come from the data layer's own section titles;
this library holds no second list of them.

**Long text.** Each row has fixed columns measured from the right (stars,
meter, the artist column at `Metrics::listSecondaryShare` of the row); the
title gets the rest and is cut with an ellipsis. In the detail pane a title that fits is one line at the title size; a longer
one is wrapped onto two lines at the body size (`nativeui::WrapLines`), and
only a title too long for two lines is cut. A recent card's title wraps onto
two lines under its art in the same way.

## Command rows

`Environment::commands` names the command rows this visit has, in the order
shown: Perform has `RandomSong` and `Playlists`, Dance Battle only
`RandomSong`, Rehearse none (an empty vector: no command rows at all). They
are rows of the list above the first section in every sort, and are absent
during a search and when no song is installed. Each has an icon tile where a
song has its art and a tagline where a song has its artist; the detail pane
shows its description.

- Accept on one calls `Delegate::onCommand(command)` and picks no song.
- Arrival never lands on one: with no saved place the list opens on the first
  song. A saved `State` with `selectedCommand` set returns to that row if the
  visit has it and to the first song otherwise.
- Left and Right treat them as one more section above the first; Home goes to
  the first of them; Up from the first leaves the list as before.
- A search started from a command row returns to it when the search is cleared.

## The selection rule

Only the region that has focus fills its highlight (`nativeui/Decor.h`). The
list's bar is filled blue to violet with a glow while the list has focus;
while focus is on the shelf, the tabs or the search field the list only
outlines the row it remembers (`Palette::selectionMarker`). The shelf's card
has an accent ring and glow with focus and a quiet outline without; the tabs'
underline and the field's border follow the same rule.

## Readability

- No text style has capitals under 10 px at 720 rows: label 16.5 and hint 17
  in the heavy face (capitals 0.625 of the size: 10.3 and 10.6 px), caption 20
  in the menu face (0.55: 11 px), body 26 (14.3 px). At 1080 rows multiply by
  1.5, at 1440 by 2. `UiCoreTheme.NoStyleHasCapitalsUnderTenPixelsAt720Rows`.
- Contrast, measured by `SongBrowserUi.TextKeepsItsContrastOverTheBackdrop`
  (every text run against the brightest pixel behind it, four moments of the
  backdrop's drift, over a stand-in for the game's streaks): worst text off
  the highlight 5.2:1 (a shelf caption, 76% white), worst on the focused bar
  4.8:1, the dark letter on a button disc 5.0:1. The test holds 4.5:1, 3:1
  and 4.5:1.

## Input

Every action is a `nativeui::Action`; the host maps buttons and keys to them.

| Event | In the list | On the shelf | On the tabs | In the search field |
|---|---|---|---|---|
| Up | Previous row; from the first row, to the shelf (the tabs with no history, the field during a search) | To the tabs | — | — |
| Down | Next row | To the list | To the shelf, or the list | To the shelf or the list; into the results during a search |
| Left / Right | Previous / next section (nothing during a search) | Previous / next card | Previous / next sort; Right from the last tab goes to the field | Move the cursor; Left at the start goes to the tabs |
| NextTab / PrevTab | Change the sort, wrapping, from anywhere | same | same | same |
| PageUp / PageDown | Page the list | — | — | — |
| Home / End | First / last row | First / last card | — | Cursor to the start / end |
| Accept | Pick the song (`onPick`) | Pick the song | — | Go into the results |
| Back | A query is typed: clear it. Otherwise leave (`onBack`) | same | same | same |
| Typed text | Goes to the field and moves focus there | same | same | Edits the query |
| Backspace / Delete | Edit the query, if one is typed | same | same | Edit the query |

A space cannot start a query. Navigation between the parts is explicit, not
spatial: the screen decides where Up and Down go, because the shelf may be
folded away and have no place on screen.

**Focus on arrival.** The saved `State` if one is given; otherwise the first
Recently Played card when there is a play history; otherwise the first row of
the list. A saved focus on the shelf with no history left falls back the same
way.

## Search

- Typing anywhere searches. The first match is selected and the detail pane
  shows it at once; focus stays in the field so typing continues.
- Results are the data layer's ranking, flat, with no sections.
- What matched is drawn in the accent colour. The data layer reports matches
  as byte ranges in the text as written; the row cuts the text at those ranges
  first and changes the case of each piece on its own, so the ranges stay on
  the right glyphs even when upper-casing changes the glyph count (`ß` → `SS`).
  `will i am` lights `WILL.I.AM`, dots included.
- On the selected row of a focused list the accent colour would vanish into
  the accent-coloured selection bar, so there the match turns white and the
  rest of the title steps back to the secondary colour.
- A song matched only on its album shows the album in place of the artist,
  highlighted, so the row says why it is there.
- Nothing matches: the list's area says `NO SONGS MATCH` and
  `NOTHING FOUND FOR <query>`, the detail pane fades away, Down and Accept do
  nothing and the hint bar offers only `CLEAR SEARCH`.
- Back (or deleting the whole query) returns to the arrangement as it was: the
  song selected before the search, at the same scroll position. Back also
  returns focus to the part the search was started from; deleting by hand
  leaves focus in the field. A second Back leaves the screen.

## Motion

Everything is driven by the clock handed to the screen, never by a frame
count, and everything comes to rest: `IsSettled()` is true when the next frame
would equal the last. The highlight and the scroll are springs; the tab
indicator glides; the focused card grows; the shelf folds over
`motion.normal`; the detail pane crossfades over `motion.normal`, each song's
text and art fading as one so the pane never shows one song's title over
another's art; the screen fades in on arrival over `motion.slow`.

Album art is asked for every frame, for the songs on screen only. A null
handle draws a placeholder; art that turns up later fades in. `IsSettled()`
does not know about art still loading, so the host builds a frame when a
texture finishes.

## The outward interface

`native/src/songbrowser_ui/SongBrowserScreen.h` is the whole of it; its header
comment repeats the input map and the rules.

```cpp
songbrowser_ui::Delegate delegate;
delegate.onPick = [](const std::string &shortName) { /* start the song */ };
delegate.onBack = [] { /* leave the screen */ };
delegate.onCue  = [](songbrowser_ui::Cue cue) { /* play the menu sound */ };
delegate.artFor = [](const songbrowser::SongEntry &song) { return /* ImageHandle */; };

songbrowser_ui::SongBrowserScreen browser(
    catalog, delegate, songbrowser_ui::Environment { measurer, clock }, savedOrNull);

// each frame
for (const nativeui::Event &event : events) browser.HandleEvent(event);
nativeui::Replay(browser.BuildFrame(), renderer);

// on the way out
saved = browser.SaveState();
```

| Member | Meaning |
|---|---|
| `Delegate::onPick(shortName)` | A song was picked, from the shelf, the list or the results |
| `Delegate::onBack()` | Back with no query typed |
| `Delegate::onCue(Cue)` | `Move`, `Select`, `Tab`, `Back`, `Error`: where the menu sounds belong. Nothing is played while the screen arranges itself |
| `Delegate::artFor(song)` | The song's album art, or the null handle while it loads. Must be cheap: a few dozen calls a frame |
| `Environment` | The text measurer and the clock (both borrowed), the viewport, the theme, the hint glyph names for the device in use, the shelf's card limit |
| `State` | Sort, focused part, selected song and card (by short name), query, scroll position, and where clearing the query returns to |
| `SetCatalog(catalog)` | Call after any change to the catalog (a song played, a new score, a pack installed), before the next event or frame. Keeps the sort, the query and the selected songs that still exist |
| `SaveState()` / `RestoreState(state)` | Round trip of `State` |
| `WantsTextInput()` | Always true: the host should capture the keyboard for text |
| `IsSettled()` | No animation is running |

The catalog is borrowed and must outlive the screen. Callbacks run inside
`HandleEvent`; none may destroy the screen.

## Files

| File | What it is |
|---|---|
| `SongBrowserScreen.{h,cpp}` | The screen: the tree, the navigation rules, search, state |
| `BrowserModel.{h,cpp}` | The list's rows (headings and songs, or search results) for the current sort and query |
| `SongWidgets.{h,cpp}` | `SongRow`, `SectionHeading`, `RecentCard`, `SegmentMeter`, `StarMarks`, `ArtImage` |
| `DetailPane.{h,cpp}` | The detail pane and its crossfade |
| `MatchText.{h,cpp}` | Fitting and painting a line with highlighted byte ranges |
| `Containers.{h,cpp}` | `EventScope` (events its child did not use), `Reveal` (folds its child away), `Notice` (a centred message) |

Sizes and colours come from the theme. This screen added `Palette::star`,
and to `Metrics`: `searchFieldWidth`, `sidePaneWidth`,
`listSecondaryShare`, `meterCellWidth/Height/Gap`, `starSize`, `starGap`.

## Cost

Measured in `milo-tests` (RelWithDebInfo, fake text measurer, this machine),
best of five batches of 200 frames:

| Catalog | Frame at rest | Frame while scrolling | Draw commands | Rows alive at most |
|---|---|---|---|---|
| 45 songs (the disc) | 4.3 µs | 4.5 µs | 171 | 22 |
| 121 songs (the real list) | 4.3 µs | 4.4 µs | 171 | 22 |
| 1,000 songs (synthetic) | 4.3 µs | 5.4 µs | 178 | 21 |

Before the 2026-11-20 look pass (backdrop, command rows, icons, edge fades)
the same test read 4.0 / 4.2 / 4.2 µs at rest, 4.1 / 4.2 / 4.5 µs scrolling
and 149 / 156 / 163 commands.

Rows exist only for the part of the list on or near the screen, whatever the
catalog's size; a row's text is fitted once per width, not once per frame. The
figures exclude the renderer and real text measurement.

## Tests and pictures

```bash
ninja -C native/build milo-tests
native/build/milo-tests --gtest_filter='SongBrowserUi*'

SONG_BROWSER_UI_PNG_DIR=/tmp/ui native/build/milo-tests \
    --gtest_filter=SongBrowserUi.Pictures
```

`native/tests/test_song_browser_ui.cpp` drives the screen on the real
121-song fixture with a manual clock, the fixed-advance measurer and the
recorded display list. The pictures are drawn over a stand-in streaked
backdrop; their text is a dot font (it has no `<` or `>`, so the section hint
shows two boxes) and their album art is made-up gradients.

## Seen in the running game

1280x720, 1920x1080 and 2560x1440, the 45 disc songs, a five-song history
(the pictures are not kept in the repo):

- The menu font draws every label; no missing-glyph box (the state
  endpoint's `placeholder_glyphs` is 0, asserted by
  `NativeUiHostGpuTest.SongBrowserRunsInTheEngine`). Text is sharp at all
  three sizes; the smallest (the row's artist, the card's second line, the
  hint labels) is readable at 720 rows.
- The shelf shows four cards with a five-song history; the fifth, which
  would partly fit left of the detail pane, is not drawn, and nothing says
  there is more to the right. Card titles longer than about 11 characters
  are cut ("BOOM BOOM P…"), as are artists ("DEV FT. THE CA…").
- The detail pane's last line (the source game) sits on the pane's bottom
  edge with no margin under it.
- A played song's stars are five small outline circles (`o o o o o`), filled
  or not; they read as placeholders.
- The selection bar is purple while focus is elsewhere and blue with a glow
  while the list is focused; on the purple bar the matched part of a search
  result stays accent blue and is legible.
- In a search the hint bar names keys (`ENTER`, `ESC`); otherwise pad buttons
  until something has been typed.

## Not done

- Stars are round marks: the display list has no polygon primitive.
- The detail pane's title is one line; a long title is cut rather than wrapped.
- Section headings scroll with the list; they do not stick to the top.
- `SongEntry::isCover` ("as made famous by") is not shown.
- BPM has no tab (see above); the data layer can arrange by it.
- The classic list's Playlists and Random Song rows have no counterpart.
- No sign on the shelf that more cards lie to the right.
