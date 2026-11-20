# The song browser screen

`native/src/songbrowser_ui/` is the natively drawn song browser: one object,
`SongBrowserScreen`, that turns a song catalog and a stream of input events
into a display list. It is built from the UI framework
([NATIVE_UI.md](NATIVE_UI.md)) and fed by the song data layer
([SONG_BROWSER_DATA.md](SONG_BROWSER_DATA.md)), and depends on nothing else:
no game header, no engine header, no renderer, no font code. The static
library `songbrowser_ui` links into `dc3-native`, `dc3-web` and `milo-tests`.

**What is not here.** The binding that draws a display list with the game's
renderer and fonts, and the game panel that creates the screen, feeds it
input and acts on a picked song, are separate work. Nothing in this page has
been seen with the game's fonts or over the game's real background; the
pictures come from a test-only software rasteriser with a dot font.

## Layout

A 1280x720 logical view, drawn over the game's moving streak background. The
screen never fills the view opaquely: it lays one scrim (`Palette::scrim`,
black at 42%) over the whole view and one more under the list, so the
background stays visible and the text stays readable.

| Part | What it holds |
|---|---|
| Top bar | `SONGS`; the sort tabs Title, Artist, Difficulty, Source, Decade; the search field, whose placeholder `TYPE TO SEARCH` tells a keyboard user they can just type |
| Recently Played | A shelf of album-art cards, newest first. Shown only with a play history and never during a search. It folds away while the list is being browsed, so the list gets the room, and unfolds when focus returns to it, the tabs or the field |
| List | Virtualised, sectioned by the sort. A row is art, title, artist, a seven-cell difficulty meter, and five star marks once the song has been played. During a search it is one flat ranked list under a `N SONGS MATCH` heading |
| Detail pane | For the selected song: large art, title, artist, album and year, length `m:ss`, BPM, the difficulty tier's name and meter, best stars and score (or `NOT PLAYED YET`), source game. It crossfades between songs |
| Hint bar | What the controls do right now; it changes with the focused part and with whether a query is typed |

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
title gets the rest and is cut with an ellipsis. In the detail pane the title
steps down from the title size to the body size before it is cut.

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

Sizes and colours come from the theme. This screen added `Palette::scrim`
and `Palette::star`, and to `Metrics`: `searchFieldWidth`, `sidePaneWidth`,
`listSecondaryShare`, `meterCellWidth/Height/Gap`, `starSize`, `starGap`.

## Cost

Measured in `milo-tests` (RelWithDebInfo, fake text measurer, this machine),
best of five batches of 200 frames:

| Catalog | Frame at rest | Frame while scrolling | Draw commands | Rows alive at most |
|---|---|---|---|---|
| 45 songs (the disc) | 4.0 µs | 4.1 µs | 149 | 23 |
| 121 songs (the real list) | 4.2 µs | 4.2 µs | 156 | 23 |
| 1,000 songs (synthetic) | 4.2 µs | 4.5 µs | 163 | 22 |

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

## Not done

- Stars are round marks: the display list has no polygon primitive.
- The detail pane's title is one line; a long title is cut rather than wrapped.
- Section headings scroll with the list; they do not stick to the top.
- `SongEntry::isCover` ("as made famous by") is not shown.
- BPM has no tab (see above); the data layer can arrange by it.
