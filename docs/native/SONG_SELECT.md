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
then the image's row for row (`SongSelectClassicFlowTest`).

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
| `native/dta/ui/song_select/song_select.dta` | overlay of the image's panel script (see below) |
| `src/` hooks, all under `HX_NATIVE` | `SongSortMgr::Handle`, `SongSortMgr::SongSortMgr`, `SongSortMgr::SetupQuasiRandomSongs`, `SongSort::BuildTree`, `NavListSort::GetNode`, `SongSortNode::Text`, `HamNavList::OnMsg(const ButtonDownMsg &)`, `GamePanel::Enter` |

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
- `SongSelectFlowTest` (3), `SongSelectHistoryFlowTest` (2),
  `SongSelectClassicFlowTest` (1): child `dc3-native` runs driven over the
  HTTP debug server, headless, disc songs only, each on its own save
  directory. They need `DC3_DTA_FLOW_TESTS` (on by default where the assets
  are).

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
