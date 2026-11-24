# Song select on the native port

The native port's song select has four things the Xbox 360 game does not, on
desktop and on the web. The matching build is untouched: everything is in
native-only files or under `HX_NATIVE`.

| | Image | Native |
|---|---|---|
| Top of the list | Playlists, Random Song, then the sort's sections | The same, with a **Recently Played** section (up to 6 songs, newest first) between Random Song and the first section, once a song has been played |
| Sorts (the sort button) | Skill Level → Title → Source (`by_difficulty`, `by_song`, `by_location`) | Skill Level → Title → **Artist** → Source (`by_artist` third) |
| Left / Right | nothing | first row of the previous / next section |
| LB / RB | nothing | one screenful (5 rows) up / down |
| Sort on the next launch | Skill Level | the one last chosen |

`DC3_CLASSIC_SONG_SELECT=1` turns all of it off at once: three sorts, no
Recently Played, the four buttons inert, the sort not remembered. The list is
then laid out like the image's list (`SongSelectClassicFlowTest`).

By default, desktop pad and keyboard players get the natively drawn **song
browser** over the same panel. The classic list stays loaded underneath it
and is the default for gesture and web play.

## Family-friendly song selection

The port defaults to a curated PG song list for free play. The disc and
official import packs use the same rating value for every song, so the filter
uses short names from the known catalog. Choose from 18 disc songs and up to
53 mounted import or DLC songs, including Macarena, Funkytown, Dancing Queen,
Waka Waka and The Safety Dance. Search, Recently Played, Random Song, the
random party pool and playlist selection use the same filter. Built-in
playlists omit filtered songs and empty playlists are hidden. Saved playlists
with filtered songs are hidden until the full catalog is enabled.

Set `DC3_PG_SONGS=0` before launch to use the full catalog. The classic
song-select switch controls the presentation independently. Story uses its
authored setlists. To verify songs after a transfer, launch once with the
full catalog enabled. See [Copy more Dance Central songs to the Mac](MACOS.md#copy-more-dance-central-songs-to-the-mac).

## The song browser

The screen is `songbrowser_ui::SongBrowserScreen`
([reference/SONG_BROWSER_UI.md](reference/SONG_BROWSER_UI.md)): a Recently
Played shelf, the list sectioned by the sort, type-to-search, a detail pane.
`native/src/platform/SongBrowserPanel_Native.cpp` attaches it to
`song_select_panel`.

### When it shows

| `DC3_SONG_BROWSER` | |
|---|---|
| `auto` | Shown unless somebody is steering the menus by hand; a tracked body alone is not that (the rule is below) |
| `1` (any other true value) | Always, on the visits that qualify |
| `0` | Never |
| unset | Desktop: `auto`. Web: `0`. **With `MILO_INPUT_SCRIPT` set: `0`** — a scripted run drives the classic list unless it asks for the browser |

`DC3_CLASSIC_SONG_SELECT=1` means never, whatever `DC3_SONG_BROWSER` says.
The decision is logged once: `DC3 SongBrowser: mode=auto|on|off (...)`.

**Which visits qualify.** Every visit of `song_select_panel`. The panel sits
on one screen, `song_select_screen`, and that screen's script resets the mode
to `perform` (leaving `playlist_perform`) before the panel enters, so the
panel is only ever entered to pick one song: Perform, Rehearse, Dance Battle.
Playlist building runs on another panel (`song_select_playlist_panel`) and
never reaches the browser's hooks. (The browser used to check
`NativeSongSelect::ShowsRecentlyPlayed()` as well; nothing could make it false
there, and the check is gone. That function is still what keeps the Recently
Played section out of the classic list while a playlist is being built.)

**The `auto` rule.** A tracked body is not intent: a person in view who
presses nothing keeps the browser for as long as they like. The classic list
takes over only on evidence that somebody is steering by hand, and the
keyboard or the pad takes the browser back at once.

The classic list takes over when one of these happens:

1. **A hand is held where the list takes it**, for 0.75 s of UI time. The
   test is the image's own: `DirectionGestureFilterSingleUser::IsValidSwipePosition`
   on the song list (`right_hand.hnl`) or the help bar's list
   (`left_hand.hnl`), for any tracked skeleton -- the list's hand outside the
   torso ellipse, which is what makes the list start reading a swipe. It is a
   pure function of the skeleton, so it can be asked while the game is in
   controller mode. The hold only counts once the hand has been seen away
   since the last press or typed character, so an arm that is already out
   when a key is pressed does not take the list back.
2. **Start**, when the game then leaves controller mode with a body in view
   (the image's own "hands have the menu" button).
3. **Song select was reached by hand**: the last menu choice was a gesture
   (`HamNavList::sLastSelectInControllerMode` is false), the game is not in
   controller mode and a body is tracked.

Why the hand's pose and not the image's controller-mode flag: that flag turns
off on a timer whenever a body is in view (5 s without a press), which is
exactly the "tracked body" the rule must not act on. The hand-valid flag the
list keeps for itself (`IsHandValid`) adds an arc-detector hysteresis and is
only updated out of controller mode, so it was not used either.

The browser comes back when:

- **a pad or key press arrives** (the game enters controller mode; under the
  image's rules that press only wakes the pad, as on a 360), or
- **a character is typed**. The engine's text capture stays on while the
  classic list is up for the hands, for exactly this; the character is typed
  into the search box.

While the browser is shown the game is **kept in controller mode** (entered,
and its idle timer restarted, every frame). So the hidden classic list never
answers to a hand, and no press is ever taken just to wake the pad: the first
Down after any idle moves one row.

With no camera (`DC3_CONTROLLER_MODE` resolves to `forced`) there are no
hands, controller mode cannot be left, and the browser is always the list.

The browser also stands aside, without changing who has the menu, while the
panel itself is not showing, an overlay panel is up, or a dialog is up.

### What it does to the classic panel

`song_select_panel` is loaded, entered, polled and exited exactly as before.
While the browser is shown:

- the panel's `Draw` draws the browser and nothing else, once a frame, in the
  UI's render pass (the first draw pass);
- `song_info_panel` is set not showing; the help bar and the cursor panel
  (the skeleton box top left and the player camera box top right) are not
  drawn, so the browser needs no layout insets for them; `background_panel`
  draws underneath;
- every pad press except Start goes to the browser and is not seen by the
  list, the help bar or the panel's script;
- the engine's text capture is on, so letters search instead of being pad
  buttons. It is off the moment the browser is hidden or the panel exits.
  Arrows, Enter, Esc and Tab are pad buttons and keep navigating.

**The same song both ways.** Whenever the browser's detail song changes, the
classic list's highlight is put on that song's own row
(`{song_select_panel highlight_right_list_item ROW}`, the list uncollapsed
first). A command row is followed the same way: with the browser on Random
Song or Playlists the classic highlight is on that row, so the game does
there what it does on the classic row (no preview plays), and a hand that
takes the menu finds the classic list on the same row. That is also what keeps the game's preview and song info following
the browser: the classic highlight settles, the panel's own
`show_full_song_details` runs, `SongPreview` retargets. Left alone, that
prepares a preview stream on every arrow press (measured: five presses four
frames apart, five streams), because the classic list sends its
highlight-settled message at once whenever the move does not scroll it. So
`SongPreview::Poll` waits in `kIdle` while the browser's selection moved less
than 0.3 s of UI time ago (`gNativeSongPreviewHold`, `HX_NATIVE`): five quick
presses start one preview, the last song's. When the browser comes back after the classic list was used, it selects
the song the classic highlight is on (a highlight on Playlists, Random Song
or a header leaves the browser where it was).

**Pick** runs `{song_select_panel special_select_song SONG}`, the handler the
image's voice command uses, which selects the song's own row and runs the
list's select chain. `SongBrowserInSongSelectTest` compares the outcome with
an A press on the classic list for the same song: the same next screen
(`multiuser_screen`) and the same `{meta_performer get_song}`, from the
shelf, the list and a search result.

**Back** with no query typed runs the panel's `on_back` (after uncollapsing,
so it leaves instead of opening the list): `move_back_quickplay`, to
`choose_mode_screen`. With a query typed, Back clears the query first.

**Random Song and Playlists** (`songbrowser_ui::Delegate::onCommand`) select
the classic list's own row: `{song_select_panel select_right_list_item ROW}`,
the handler the voice commands use, with ROW from
`{song_offer_provider first_data_index random_song|playlists}`. What follows is
`SongFunctionNode::OnSelect`, the same as an A press on that row
(`TheCommandsDoWhatTheClassicRowsDo` compares the two engines through the
HTTP route; `TheCommandRowsAreChosenWithThePad` does it with Up and A on the
browser's own rows):

- Playlists goes to the playlist tutorial check and then
  `song_select_playlist_screen`, in `song_select_playlist` mode, with
  `back_screen` set to `song_select_screen`. **Back from the playlist screen
  returns to song select, picking one song again, and the browser is the
  list again.**
- Random Song picks `TheHamSongMgr.GetRandomSong()` and goes on as after any
  pick (`multiuser_screen`, then the game).

The browser draws the two as rows above the first section, in every sort
and never during a search. Arrival never lands on one; Up from the first
song reaches them. Leaving through Playlists and coming back returns to the
Playlists row.

The classic list has both rows in Perform, only Random Song in Dance Battle
and neither in Rehearse, and the browser draws exactly the ones this visit's
classic list has (`songbrowser_ui::Environment::commands`, asked of the
classic list when the screen is built and checked again every frame). A
command whose row this visit does not have is refused with the error cue; the
state endpoint lists the ones that exist (`commands`) and the ones drawn
(`command_rows`). `POST /api/songbrowser/command` runs the same function.

**Sounds** are the list's own (`right_hand.hnl`'s ribbon resource): a move
that changes the song plays the highlight sound through the highlight itself;
a move that does not (onto the tabs, a clear) plays the same sound directly;
a sort change plays the list's select sound; Accept with nothing to pick
plays `invalid_select.flow`; a pick plays what the select chain plays; leaving
with Back adds none, as the pad's B does not.

**Sort.** The tab is saved as the classic sort name where one exists
(`by_song`, `by_artist`, `by_difficulty`, Source as `by_location`) and read
back on the next visit. Decade is saved as `by_decade`, which the classic
list does not have: `SongSortMgr::SavedSortIndex` answers 0 for a name it
does not know, so the classic list opens on Skill Level and the file is left
alone until one of the two writes a sort again.

**State.** `SaveState()` on exit, restored on the next enter in the same
run: sort, focused part, selected song and card, query, scroll. The catalog
is refilled from the game on every enter and when the song list's size
changes while shown.

**Covers.** A disc song's cover is the game's own path
(`HamSongMgr::GetAlbumArtPath`). A downloaded song's is not: the game puts the
package's drive (`cnt00000003:`) on that path only while the package is
mounted, which it does for one song at a time (the classic list shows a
downloaded song's art only then: `refresh_album_art` in `ui/meta.dta`). The
browser shows a shelf and a list of covers at once, so each DLC root gets a
drive of its own for the whole run (`dlcroot0:`, `dlcroot1:`, ... in
`ContentMgr_Native.cpp`) and the catalog gives a downloaded song's cover as
`dlcroot0:/<package>/songs/<song>/<song>_keep.png`
(`CoverPath` in `SongCatalogSource_Native.cpp`). On the web the same path is
fetched as `dlc/<package>/songs/<song>/gen/<song>_keep.png_xbox`. A cover that
cannot be read draws the game's logo (`ui/image/dc_logo_keep.png`) and counts
in the state's `images_failed`.

**Hints** name the device last used: the keyboard's names (ENTER, ESC, TAB)
when the last input came from the keyboard or no real pad has been used in
this run, the pad's (A, B, LB RB) once a real pad button arrives. They change
in place (`SongBrowserScreen::SetHintGlyphs`). The engine merges keyboard-made
buttons into pad 0's and a `ButtonDownMsg` does not say where it came from, so
the browser asks GLFW, when a press arrives, whether the real gamepad has a
button down (`HamUI::OnMsg`'s `HX_NATIVE` hook); typed text is always the
keyboard. A press injected over HTTP counts as the keyboard's unless it says
`"device":"pad"`. Limits: a key pressed while a pad button is held reads as
the pad; the web has no GLFW and always reads as the keyboard. The engine
query that would make this exact is in the handoff notes (per pad, which
buttons the keyboard map and the gamepad each contributed to the last poll).

### Not done

- The bright ribbons of the classic picture are not drawn while the browser
  is: it draws its own backdrop (the game's indigo, drifting slowly) over
  `background_panel`.
- With `DC3_SONG_BROWSER=1` a hand never gets the classic list.
- The dialog case has a test
  (`SongBrowserSwitchFlowTest.ADialogOverSongSelectTakesThePresses`: the
  browser stands aside, comes back on the same song, and A on the dialog ends
  where it ends over the classic list). An overlay panel over song select has
  none.
- A package directory with a capital letter in its name (one not made by
  `scripts/dlc/import.sh`, which makes lower-case names) shows the logo in
  place of its songs' covers, and the log says so when the package is found:
  a path on the root's drive is lower-cased by the game's path normalisation.
  Measured with the megapack linked as `MegaPack`: the song is listed, picked
  and reaches `multiuser_screen`; its cover is the logo.
- Seen while writing that test, on the classic list too, so not the
  browser's: A on an `error_message` dialog over song select dismisses the
  dialog and also opens Playlists. Not looked into.

## Keys

| Pad | Keyboard (desktop and web) | Song select |
|---|---|---|
| D-pad up / down | Up / Down (web also W / S) | one row |
| D-pad left / right | Left / Right (web also A / D) | previous / next section |
| LB / RB | Q / E | a screenful up / down |
| Y | Y | next sort; the list comes back collapsed to its headers, as in the image |
| B | Esc, Backspace | in a collapsed list: open it. Otherwise back |
| A | Enter | select |

The keyboard map is the engine's (`../milo-native-engine/src/platform/Joypad_Native.cpp`,
`kKeyBindings`, and `InitWebInput` for the web). The web's touch d-pad sends
the arrow keys, so Left/Right work there. It has no Y button, so a phone
without a keyboard cannot change the sort (not done here: the touch layout is
checked by `scripts/web/phone.mjs`).

The help bar at the bottom names the sorts as it always did: the current one
first, then the next two. Four names do not fit (the fourth is cut off by the
edge of the screen at 1280x720), so the one after those is not shown until the
sort button is pressed. There is no on-screen hint for Left/Right or LB/RB:
the help bar's slots are in the `.milo`, and none is free.

## Recently Played

- **What counts.** A song is the newest entry the moment gameplay is entered
  (`GamePanel::Enter`), in any mode: Perform, Rehearse, Dance Battle, a
  playlist, a party game, the story. Finishing is not required. It is written
  to disk at that moment, not at exit.
- **Where it shows.** Wherever the full song list is shown to pick one song:
  the modes whose `song_select_mode` is `song_select_quickplay`. Seen at
  runtime in Perform, Rehearse and Dance Battle; the party games read the same
  setting and were not driven. Playlist building (`song_select_playlist`)
  keeps the image's list, so a playlist is put together from one row per song
  (seen at runtime: no section there with a history on disk).
- **In every sort**, between Random Song and the sort's first section,
  collapsed or open. Six rows at most. A song that is not in the list right
  now (a DLC pack that is no longer mounted) is skipped, and the next one
  shows instead: the file keeps 24. DLC songs are entered by the same short
  name as disc songs (seen at runtime with `milkshake`).
- **A row there is the song's row.** It is a second `SongSortNode` over the
  same `SongRecord`, so title, stars, album art, preview and select are the
  same code as the song's own row.
- **The song has two rows.** That is decided as follows:
  - *A name means the song's own row.* `NavListSort::GetNode` skips the
    Recently Played copies, so everything that looks a song up by name
    answers with its own row and its own header: `data_index`,
    `first_data_index`, `get_header_symbol_from_child_symbol`, the voice
    command that selects a song by name, `special_select_song`.
  - *The highlight is put back by path, not by index.* The image's script
    keeps the list index across a song. With the section growing while the
    song is played that index is another row afterwards, so the panel's exit
    saves the row as the path `NavListNode::GetID` writes (section, header,
    song) and its enter finds that row again, at the same height on screen.
    Played from its own row, the highlight returns to its own row; played
    from the Recently Played row, it returns to that row (now the first of
    the section). If the song has dropped out of the section, its own row.
  - *Random Song draws each song once.* `SetupQuasiRandomSongs` skips the
    copies.
  - Opening song select for the first time in a launch with a history puts
    the highlight where the image puts it: on the third row, which is now the
    Recently Played header instead of the first difficulty header.
- **Profiles.** The history belongs to the machine, not to a profile:
  whoever is signed in sees the same list. The profile save is a fixed-size
  image (`SongStatusMgr::SaveSize`) and is not touched. Its own last-played
  time is only stamped when a Perform song is finished and scored, which is
  not "has reached gameplay".
- **Under the classic switch** the history is still recorded, only not shown,
  so the list is right when the switch is turned back off.

## Sort by artist

The image names this sort (`by_artist` in the locale, half a script branch
for it) but contains no `SongSortByArtist`. This one is new.

- Rows are ordered by artist, then title, with a leading "The" skipped and
  names that start with a digit or a symbol first.
- **Each row names its artist** in the row's `header_text` slot, the small
  line above the title (the image never fills it for a song). The "as made
  famous by" line is left empty under this sort, as it would sit in the same
  place.
- **Headers are letter ranges**: whole letters, joined with the next until a
  section holds four songs, labelled `B - C` or `M`; names that start with a
  digit are `123`. The 45 disc songs make 9 sections (`123 - A`, `B - C`,
  `D - E`, `F - J`, `K - L`, `M`, `N - S`, `T - U`, `V`). A header per artist
  was the alternative. On the disc no two songs have the same artist text
  (Usher's two are "Usher" and "Usher ft. will.i.am"), so that would have been
  a header above every song, and a collapsed list as long as an open one.
  With the DLC on this box mounted (121 songs) the same rule gives 17
  sections of 4 to 11 songs, most of them a single letter.
- The sections are the same collapsed and open (the image's own script
  expected two different groupings for `by_artist` and a handler that does
  not exist to translate between them; that branch is gone from the overlay).
- The song card's top line is the artist in the other sorts and the title in
  this one. That is the image's code (`SongSortNode::Text`, the `artist`
  slot), which already knew the name `by_artist`.
- Playlist building has its own rotation of three sort names in the image's
  script and is unchanged, so this sort is not reachable there.

How it looks (1280x720 and 1024x576, same layout at both): the artist line is
the same size and weight as the title and sits a few pixels to its left; the highlight
bar is one line tall and covers the title only; under a header the first
artist line is close to the header text. Nothing overlaps or is cut through.
Artists and titles longer than about 20 characters end in "…", as titles
always did ("LIL JON & THE EAST SID…").

## Left/Right and LB/RB

- A section is the rows from one header to the next. The rows above the first
  header (Playlists, Random Song) are a section too. In a collapsed list every
  header is its own section, so Left/Right step one header at a time.
- **Right**: the first row of the next section, which is its header, brought
  to the top of the list. In the last section: stays.
- **Left**: the first row of the previous section. In the first section: its
  first row.
- **LB / RB**: five rows up / down with the highlight kept at its height on
  screen, stopping at the ends.
- `HamNavList::OnMsg(ButtonDownMsg)` asks the list's provider where the
  button goes (`nav_jump_row`), after its own checks that the list is in
  controller mode, not animating, enabled and has the focus. Only song
  select's provider (`song_offer_provider`) answers, so every other list
  leaves the buttons unhandled as before, and song select's own list ignores
  them while its script has switched it off (`disable_navigation`, which it
  does under its overlays).
- Playlist building's add-songs list is fed by the same provider, so the four
  buttons work there too (seen at runtime). The list of playlists is not, and
  ignores them.

## What is saved

`$DC3_SAVE_DIR/song_select.txt` (default `~/.local/share/dc3/saves/`), written
whenever it changes, through a temporary file and a rename:

```
dc3-song-select 1
sort by_artist
played ymca
played starships
```

`played` lines are newest first, at most 24. Unknown lines are skipped; a file
that does not start with `dc3-song-select` is treated as empty and left alone.
Delete the file to forget both. On the web the file lives wherever the save
directory does, so it survives a reload only when the save directory is
persistent there.

## Where the code is

| | |
|---|---|
| `native/src/platform/SongSelect_Native.{h,cpp}` | `RecentlyPlayedHeaderNode`, `ArtistCmp`, `SongSortByArtist`; the native members of `SongSort`, `SongSortMgr` and `HamNavList`; `Dc3ClassicSongSelect()` |
| `native/src/platform/SongSelectState_Native.{h,cpp}` | the file |
| `native/src/platform/SongBrowserPanel_Native.cpp` | the song browser on `song_select_panel`: when it shows, pick, back, sounds, the classic highlight, `GET /api/songbrowser/state`. Linked into `dc3-native`, `dc3-web` and `milo-tests` only |
| `native/src/platform/SongBrowserHooks_Native.h` | the seven functions the panel code calls; forwarders in `SongSelect_Native.cpp`, no-ops in a binary without the browser (`milo-viewer`, the export tools) |
| `native/dta/ui/song_select/song_select.dta` | overlay of the image's panel script (see below) |
| `src/` hooks, all under `HX_NATIVE` | `SongSortMgr::Handle`, `SongSortMgr::SongSortMgr`, `SongSortMgr::SetupQuasiRandomSongs`, `SongSort::BuildTree`, `NavListSort::GetNode`, `SongSortNode::Text`, `HamNavList::OnMsg(const ButtonDownMsg &)`, `GamePanel::Enter` |
| `src/` hooks of the browser, all under `HX_NATIVE` | `SongSelectPanel::Enter`, `Exit`, `Poll`, `Draw` (new override) and first in its handlers; `HelpBarPanel::Draw`; `ShellInput::Draw`; `HamNavList::NativePlayRibbonSound` (declaration) |

The overlay replaces `ui/song_select/song_select.dta` whole. It differs from
the original in four handlers:

| Handler | Change |
|---|---|
| `enter` | the first time, start in `{song_offer_provider saved_sort_index}`; the help bar's names from `{song_offer_provider sort_labels}`; `{song_offer_provider restore_focus $this}` |
| `update_sort_label` | the three hard-coded rotations of the sort names become `{song_offer_provider sort_labels}`; `{song_offer_provider save_sort}` |
| `exit` | `{song_offer_provider save_focus {right_hand.hnl get_selected}}` |
| `uncollapse_headers` | the `by_artist` branch (which calls a handler the image does not have) is removed |

Under the classic switch `saved_sort_index` is 0, `sort_labels` is the image's
three names, and `save_sort`, `save_focus` and `restore_focus` do nothing.

## Tests

`native/tests/test_native_song_select.cpp`:

- `NativeSongSelectStateTest` (5): the file, against a scratch path.
- `NativePgSongsFlowTest` (1) checks the default filter in the browser,
  classic list, Recently Played, search and random picks. The other CTest
  flows use `DC3_PG_SONGS=0` so they can exercise the full catalog.
- `SongSelectFlowTest` (3), `SongSelectHistoryFlowTest` (2),
  `SongSelectClassicFlowTest` (1): child `dc3-native` runs driven over the
  HTTP debug server, headless, disc songs only, each on its own save
  directory. They need `DC3_DTA_FLOW_TESTS` (on by default where the assets
  are).
- `SongBrowserInSongSelectTest` (3), `SongBrowserSwitchFlowTest` (12 where
  the DLC megapack is imported, 11 elsewhere): the
  song browser in the same kind of run. The first asks for it
  (`DC3_SONG_BROWSER=1`); the second covers the switches, playlist building,
  the two commands against a second engine on the classic list, Rehearse and
  Dance Battle, the hint names, the preview count, a dialog over song select,
  and a stand-in body that
  presses nothing for 60 s of game time under `DC3_CONTROLLER_MODE=faithful`.
  `ADownloadedSongShowsItsOwnCoverAndIsPickedAsOnTheClassicList` is the one
  test with downloaded songs on (Milkshake, from a one-package root): its
  cover is read through the root's drive and loads, and A on it ends on the
  same screen with the same song as A on its row of the classic list. It is
  compiled in only where configure finds the package (`DC3_DLC_TESTS`).
- `SongBrowserHandsTest` (1): the whole `auto` rule against a synthetic person
  (`scripts/song_browser_hands.py` on `scripts/synthetic_kinect.py`, no input
  script): standing idle, an arm passing through, an arm held, dropped, a
  press, a typed character with the arm still out, Start.
- `GestureMenusTest` (`native/tests/test_native_gesture_menus.cpp`) asserts
  the browser was hidden on a song select reached by gesture.
- `NativeUiHostGpuTest.SongBrowserRunsInTheEngine`
  (`native/tests/test_nativeui_host_gpu.cpp`): a rendering run at 1280x720;
  reads the frame's pixels where the state endpoint says the highlight and the
  search field are. Needs a GPU.

**Why the older tests still drive the classic list.** Every one of these runs
sets `MILO_INPUT_SCRIPT` (the fixture's `RunEngine`), and so does every input
script, and an unset `DC3_SONG_BROWSER` with `MILO_INPUT_SCRIPT` set means the
classic list (`ResolveMode` in `SongBrowserPanel_Native.cpp`). That is the one
place it is pinned. A run that drives song select over HTTP with no input
script and no `DC3_SONG_BROWSER` gets the browser.

```bash
ninja -C native/build milo-tests dc3-native
ctest --test-dir native/build -R SongSelect --output-on-failure
DC3_TEST_SHOW_DRIVER=1 DC3_DTA_FLOW_TESTS=1 \
    native/build/milo-tests --gtest_filter='SongSelect*'   # prints what each run was judged on
```

(`scripts/native_test.sh` runs them with everything else; with `-R` its skip
ratchet compares a subset against the whole suite's budget.)

The classic test compares the list with
`native/tests/data/song_select/classic_rows_by_*.txt`, dumped from a build of
the commit before this work.

To read a row's text without a screenshot the tests use
`{song_offer_provider visible_text LIST}` (`HamNavList::NativeVisibleText`):
`ROW|SLOT|TEXT` for every label on screen that has text.
