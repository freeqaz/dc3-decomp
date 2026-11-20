# Song browser data

The data and logic a song browser needs: the list of songs, a search over it,
and the ways of arranging it. No drawing and no input. Code in
`native/src/songbrowser/` (namespace `songbrowser`); tests in
`native/tests/test_song_browser.cpp`.

| File | What | Depends on |
|------|------|------------|
| `SongText.{h,cpp}` | Folding for search, sort keys, UTF-8 decoding | standard C++ |
| `SongCatalog.{h,cpp}` | `SongEntry`, `SongCatalog`, recents | standard C++ |
| `SongSearch.{h,cpp}` | `SongSearch`, `SearchResult` | standard C++ |
| `SongSections.{h,cpp}` | `Arrange`, `SongList`, `Section` | standard C++ |
| `SongCatalogJson.{h,cpp}` | JSON for the three above | standard C++ |
| `SongCatalogSource_Native.{h,cpp}` | Fills a catalog from the running game; body of `GET /api/songs` | the game |

Everything but the last file is plain data in, plain data out, and is tested
without an engine. The sources are compiled into every native target through
`DC3_SONGBROWSER_SOURCES` in `native/CMakeLists.txt`.

## Which songs are listed

The same songs today's song select lists. That rule is
`SongSortMgr::RebuildSongRecordMap` (`src/lazer/meta_ham/SongSortMgr.cpp`):
every song of `TheHamSongMgr.GetRankedSongs()` (a song whose `rank` is not 0)
whose metadata is not fake (`HamSongMetadata::IsFake`) and whose content is
unlocked (`TheProfileMgr.IsContentUnlocked(shortName)`).

`FillCatalogFromGame` runs the same loop with the same three tests. It does
not read the manager's own map: that map is rebuilt only when song select is
entered, and the sorts hold pointers into it. The engine-run test compares the
two lists (see Tests).

`IsContentUnlocked` needs `TheAccomplishmentMgr`, which does not exist while
the game is starting; `GameCatalogReady()` is false until it does.

Measured on this machine's assets, 2026-11-20: 45 songs with `DC3_DLC=0` (the
disc), 121 with the two installed Dance Central 2 packs (45 `ham3`, 44 `ham2`,
32 `ham2_dlc`).

## Fields

One `SongEntry` a song.

| Field | Source |
|-------|--------|
| `id` | `HamSongMetadata::ID()` |
| `shortName` | `TheHamSongMgr.GetShortNameFromSongID(id)`, as `SongRecord` |
| `title`, `artist`, `album` | `Title()`, `Artist()`, `Album()`: the metadata's own strings, as song select shows them (not locale tokens) |
| `year` | `YearReleased()` |
| `bpm` | `Bpm()` |
| `lengthMs` | `LengthMs()` |
| `rank` | `Rank()`, the number the difficulty sort orders by |
| `difficultyTier` | `TheHamSongMgr.RankTier(rank)`, 0..6 on the installed songs; -1 for none |
| `origin` | `GameOrigin()`: `ham1`, `ham2`, `ham3`, `ham2_dlc`, ... |
| `source` | `origin` through the game's location rule (`ConvertGameOriginSymbolToEnum`): `ham1`, `ham2`, `ham3`, anything else is `DLC` |
| `onDisc`, `isCover` | `IsOnDisc()`, `IsCover()` |
| `albumArtPath` | `TheHamSongMgr.GetAlbumArtPath(shortName)`, e.g. `songs/reallove/reallove_keep.png` |
| `bestScore`, `bestStars` | The active profile's best run over every difficulty (`SongStatusMgr::GetSongStatus(id).GetBestSongStatusData()`), once it has a score; 0 without a profile |
| `played` | The song has a score, or is in the play history |
| `locked` | Fails only the unlock test; such songs are in the catalog only when asked for (`includeLocked`) |
| `recentRank` | Place in the play history, 0 the newest; -1 when not in it |

The catalog also carries the play history and the section names in the game's
language (`song_tier_0`..`song_tier_6`, `ham1`, `ham2`, `ham3` from the locale;
the code has English defaults for the rest).

### Text encoding

The song lists are UTF-8. Every title, artist and album of the 121 installed
songs is plain ASCII (measured; `SongBrowserCatalogTest.TheFixtureIsTheRealList`
fails when that stops being true of the fixture). Accent handling is therefore
tested on names written in the tests, not on real ones. A byte that is not
valid UTF-8 counts as one unknown character.

## Recently played

`SongCatalog::SetRecents(newestFirst)` takes the play history
(`NativeSongSelectState::History()`: newest first, no duplicates, at most 24;
a song is added when it reaches gameplay). `Recents(maxRows)` answers the
installed, unlocked songs of that history in order. A name that is not in the
catalog (a pack that was removed) is skipped and stays in the history;
`recentRank` counts it, so ranks do not shift when the pack comes back.

## Search

`SongSearch::Search(query, maxRows)`. The search holds a folded copy of the
catalog; call `Rebuild` when the catalog or the play history changes.

### Folding

Both the query and the songs are folded the same way:

- Case is ignored.
- Accented Latin letters (U+00C0..U+017F) are their base letters: `é` is `e`,
  `ß` is `ss`, `Æ` is `ae`.
- `&` is the word `and`, with or without spaces around it: `S & M`, `s&m` and
  `s and m` are the same.
- `$` beside a letter is `s` (`Ke$ha` is `kesha`); `!` before a letter is `i`
  (`P!nk` is `pink`). Neither occurs in the installed songs.
- Every other punctuation mark is folded two ways, and each song field is kept
  both ways: as a word break (`will.i.am` is `will i am`) and dropped
  (`will.i.am` is `william`). A query is folded the second way and split on
  spaces, so `will i am`, `william` and `will.i.am` all find the song.
- Initials written apart are one word when dropped: `J. J. Fad` is `jj fad`.

### What matches

Every word of the query must be found in the title, the artist or the album;
different words may be in different fields. The kinds of match, best first
(`MatchClass`):

| Class | Meaning | Real example |
|-------|---------|--------------|
| `title_prefix` | the title starts with it | `ymca` finds "Y.M.C.A." |
| `title_word` | a later word of the title starts with it | `saxobeat` finds "Mr. Saxobeat" |
| `artist_prefix` | the artist starts with it | `neyo` finds Ne-Yo's "Closer" |
| `artist_word` | a later word of the artist starts with it | `william` finds "OMG" (Usher ft. will.i.am) |
| `album` | the album, or a word of it, starts with it | `rihanna loud` |
| `substring` | inside a word, any field | |
| `fuzzy` | found only by forgiving a mistake | `rihana`, `macarana` |

A leading "The" does not count against a prefix, in any field: `trammps` and
`the trammps` are both `artist_prefix` for The Trammps, `humpty` is
`title_prefix` for "The Humpty Dance".

A row's class is the worst of its words' classes, unless the whole query, as
one run, is found at least as well; then it is that (`s and m` is
`title_prefix` for "S & M", though `m` alone is a later word).

In a query of more than one word, `ft`, or any start of `featuring` from two
letters on (`fe`, `feat`), is satisfied by a song whose artist has a
`ft`/`feat`/`featuring` credit, however that song spells it: `usher feat
pitbull`, `usher ft pitbull` and `usher featuring pitbull` find the same song.

### Mistakes

A word that is found nowhere may still be found by forgiveness:

- Never for a word under 3 characters (`SongSearch::kMinFuzzyWord`).
- The first character must be right and at the start of a word of the song.
- From 4 characters (`kMinEditWord`): one wrong letter, one missing, one extra
  or two neighbours swapped (`rihana`, `rihannna`, `rihamna`, `rihnana`).
- Otherwise the word's letters in order within twice its length (`rhn` finds
  Rihanna; `blackeyedpeas` finds Black Eyed Peas).

Measured over the 121 songs: no one- or two-letter query has a fuzzy row; over
all 17,576 three-letter queries the most fuzzy rows is 14 (`fta`) and the mean
0.26.

### Order

By class; then recently played, newest first; then title order (the by-title
arrangement's order, which is total, so the order of rows never depends on the
order songs were added in).

### Where it matched

Each row has the field its class came from and a list of `MatchRange { field,
begin, length }`. `begin` and `length` are **bytes of the original UTF-8
string**, not characters: `deja` on "Déjà Vu" is `title` 0, 6. A range never
splits a character, ranges of one field do not overlap or touch, and they are
ordered by field, then position. `ymca` on "Y.M.C.A." is 0, 7: the dots inside
are in the range, the last one is not.

### Not searching

A query with nothing to search for (empty, spaces, only punctuation) answers
`searching == false` and no rows: the caller shows the browse list. A query
that finds nothing answers `searching == true` and no rows.

### Cost

Measured by `SongBrowserSearchTest.FiveHundredSongsTakeWellUnderAMillisecond`
(500 songs: the real 121 repeated, 15 queries, `RelWithDebInfo`, this
machine): 0.12 ms a search on average, 0.16 ms the slowest (`rihana`). The test
prints the figure and fails at 1 ms.

## Arrangements

`Arrange(catalog, arrangement, options)` answers a `SongList`: `rows` (catalog
indices) and `sections` (`token`, `title`, `firstRow`, `count`). Sections cover
the rows in order.

| Arrangement | Sections | Order inside | The game's rule |
|-------------|----------|--------------|-----------------|
| `kByTitle` | one per first letter; digits and symbols together as `123`, first | title, then artist | `AlphaKeyStrCmp(title, false)` (by_song) |
| `kByArtist` | one per first letter of the artist, article skipped; `123` first | artist (article skipped), then title | `ArtistCmp` in `SongSelect_Native.cpp` |
| `kByDifficulty` | `song_tier_N`, lowest first; no tier (`no_part`) last | rank, a rank of 0 last, then title | `DifficultyCmp` (by_difficulty) |
| `kBySource` | `ham1`, `ham2`, `ham3`, `DLC` | title | `LocationCmp` (by_location) |
| `kByDecade` | `decade_1990`; no year (`decade_unknown`) last | year, then title | none |
| `kByBpm` | `bpm_120` (120-129); no BPM (`bpm_unknown`) last | BPM, then title | none |

Sort keys are the game's: `AlphaKeySkip` (skip a leading `(`; for artists also
a leading "The ", "A ", "An "), then a case-blind byte comparison, so a digit
sorts before a letter and punctuation is not removed. The first-letter rule is
`ArtistCmp::SortCharRank` (0 for anything but a letter).

`ArrangeOptions`:

- `recentRows`: when above 0 and something installed was played, the list
  starts with a `recently_played` section of up to that many songs. Those are
  second rows: each song still has its own row further down, and
  `RowForSong` answers that one.
- `minSectionSongs`: for the two letter arrangements, letters are joined until
  a section has that many songs, labelled `A - C`; a short last section joins
  the one before. 4 is what today's artist sort does. The default, 1, is one
  section a letter.

`RowForSong(shortName)` is the song's row, `SectionForRow(row)` its section,
`RowForLetter(letter)` the first row whose title (artist, in `kByArtist`)
starts with that letter; any character that is not a letter asks for `123`.

### Where it is not today's list

- By title, "(When You Gonna) Give It Up to Me" is under W. The game's by_song
  list orders it as a W but shows it in its first section: its header reads the
  raw first character, its order skips the `(`.
- By title, the default is one section a letter; the game's by_song joins
  letters.
- An accented first letter is its base letter here (`É` under E). The game's
  comparison puts it first, under `123`. No installed song has one.
- Songs with the same title: artist, then short name. The game leaves it open.

With `minSectionSongs = 4` the by-artist list is the native artist sort row
for row and header for header, and by difficulty and by source are the game's
lists row for row (the engine-run test checks all three, and by title apart
from the one song above).

## `GET /api/songs`

See [HTTP_DEBUG_SERVER.md](../../tools/HTTP_DEBUG_SERVER.md#songs). The
catalog, a search (`?q=`) or an arrangement (`?sort=`), built from the game on
each request.

## Tests

`native/tests/test_song_browser.cpp`, in `milo-tests`:

- `SongBrowserTextTest`, `SongBrowserCatalogTest`, `SongBrowserSearchTest`,
  `SongBrowserSectionsTest`, `SongBrowserJsonTest`: no engine. Most run on
  `native/tests/data/song_browser/catalog.tsv`, the 121 songs a running
  `dc3-native` answered on `GET /api/songs`.
- `SongBrowserFlowTest` (with `DC3_DTA_FLOW_TESTS=1`): one headless
  `dc3-native`, disc songs. `GET /api/songs` has the songs of song select's
  list; each arrangement is compared with the game's own sort; a song taken to
  gameplay is then the first recent.

```bash
cd native/build
./milo-tests --gtest_filter='SongBrowser*'
DC3_DTA_FLOW_TESTS=1 ./milo-tests --gtest_filter='SongBrowserFlowTest.*'
```
