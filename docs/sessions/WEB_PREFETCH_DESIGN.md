# Web Build: Pre-Fetch Strategy Design

**Date**: 2026-03-19
**Status**: Research & Design (no code changes)

## 1. Problem Statement

The DC3 web build (Emscripten/WASM) loads `.milo_xbox` assets on-demand via HTTP fetches. The engine's `AsyncFile::Init()` contains a spin loop (`while (!_OpenDone()) ;`) that blocks the main thread. The current solution uses ASYNCIFY to yield during this loop, but this has several drawbacks:

- **ASYNCIFY code-size overhead**: instrumentation inflates the WASM binary
- **Stack size constraints**: requires `ASYNCIFY_STACK_SIZE=262144`
- **Latency**: each file fetch still blocks the calling code path for the full network round-trip
- **Fragility**: any uncovered spin loop or assertion can deadlock the browser

A cleaner approach: **pre-fetch all needed files into Emscripten's MEMFS before the engine requests them**, so `fopen()` succeeds immediately and no async workarounds are needed.

## 2. Asset Loading Architecture (Findings)

### 2.1 Boot Bundle (Already Working)

At boot, `WebAssetsFetchBundle()` downloads ALL `.dta`/`.dtb` config files as a single binary bundle from `/api/bundle`. This is fast (~0.6s for 492 files) and non-blocking (runs before engine init).

**Config files are NOT the problem.** The problem is `.milo_xbox` binary assets fetched on-demand during gameplay loading.

### 2.2 The FileMerger Pipeline

The engine uses a centralized `FileMerger` system to orchestrate content loading. `HamDirector` owns three FileMergers:

| Merger | Purpose | Files |
|--------|---------|-------|
| `mMerger` | Main content pipeline | Song .milo, venue world, visualizer, HUD |
| `mMoveMerger` | Dance move animations | Per-song charclips, hammoves, transition clips |
| `mGameModeMerger` | Game mode HUD | HUD panels for specific game modes |

Loading is event-driven via DTA scripts. The sequence for a song:

```
1. OnLoadSong(songPath, bpm, ...)
   ├─ mMerger->Select("song", songPath)
   ├─ mMerger->StartLoad()
   └─ [DTA handlers fire]

2. OnFileLoaded("song")
   ├─ TheHamWardrobe->LoadCharacters(outfit1, outfit2, crew1, crew2, ...)
   ├─ mMerger->Select("viz", "ui/visualizer/visualizer.milo")
   ├─ mMerger->Select("venue", "world/{venue}/{venue}.milo")
   ├─ mGameModeMerger->StartLoad()
   └─ mMerger->StartLoad()

3. OnFileLoaded("venue")
   └─ mVenue = loaded WorldDir

4. OnPopulateMoves()  [after venue loaded]
   ├─ Read move_data.dta to find clip/move names
   ├─ For each move instance:
   │   ├─ mMoveMerger->Append("modular_song_data/charclips/{clip}.milo")
   │   ├─ mMoveMerger->Append("modular_song_data/hammoves/{move}.milo")
   │   └─ mMoveMerger->Append("modular_song_data/transition_charclips/{trans}.milo")
   └─ mMoveMerger->StartLoad()
```

### 2.3 Character Loading

`HamWardrobe::LoadCharacters()` triggers:

| Component | File Pattern | Triggered By |
|-----------|-------------|--------------|
| Main outfit | `{outfitModel}` (from DTA lookup) | `HamCharacter::OnConfigureFileMerger` |
| Viseme | `{charViseme}` (from DTA lookup) | Same handler |
| Campaign VO | `sfx/loc/eng/campaign/{vo}.milo` | Same handler, if present |
| Backup dancer outfit | `char/main/backup/{outfit}.milo` or `char/main/dancer/{outfit}.milo` | `HamWardrobe::LoadCharacters` |
| Crowd clips | via `crowd_clips.fm` FileMerger | `HamWardrobe::LoadCrowdClips` |

### 2.4 Venue Sub-files

Venue worlds reference sub-dirs (component .milo files) that are loaded as proxy sub-directories. The `DirLoader` traverses these when parsing the venue's `.milo_xbox` header. Sub-files follow the pattern:

```
world/{venue}/gen/{venue}.milo_xbox           (main venue)
world/{venue}/gen/{venue}_buildings.milo_xbox (buildings component)
world/{venue}/gen/{venue}_sky.milo_xbox       (skybox)
world/{venue}/gen/{venue}_set.milo_xbox       (set dressing)
world/{venue}/gen/{venue}_chairs.milo_xbox    (furniture)
world/{venue}/gen/{venue}_table_glasses.milo_xbox
...
```

### 2.5 File Loading Timing Diagram

```
BOOT ─────────────────────── MENUS ─────────── SONG LOAD ──────── GAMEPLAY
│                            │                  │                  │
├─ Bundle (DTA/DTB)          │                  │                  │
│  492 files, ~0.6s          │                  │                  │
│                            │                  │                  │
├─ UI panels (helpbar,       │                  │                  │
│  meta panels, etc.)        │                  │                  │
│  ~5-10 .milo_xbox          │                  │                  │
│                            │                  │                  │
│                            ├─ Background      │                  │
│                            │  menu .milos     │                  │
│                            │  (on navigation) │                  │
│                            │                  │                  │
│                            │                  ├─ Song .milo      │
│                            │                  ├─ Venue .milo     │
│                            │                  │  + sub-files     │
│                            │                  ├─ Visualizer      │
│                            │                  ├─ Character .milos│
│                            │                  ├─ Crowd clips     │
│                            │                  ├─ Move data .milos│
│                            │                  │  (charclips,     │
│                            │                  │   hammoves,      │
│                            │                  │   transitions)   │
│                            │                  └─ HUD .milo       │
│                            │                                     │
```

## 3. Can We Predict Files Deterministically?

### 3.1 What We Know at Song Selection Time

When the user selects a song, the game knows:
- **Song ID** -> maps to a `.milo` path (song data)
- **Venue name** -> maps to `world/{venue}/gen/{venue}.milo_xbox`
- **BPM** -> determines tempo category (slow/medium/fast)
- **Character outfits** -> from player profile selections
- **Game mode** -> determines which HUD to load

### 3.2 What We Cannot Predict Without Parsing

**Sub-file references in .milo_xbox headers**: When a venue `.milo_xbox` is loaded, the `DirLoader` parser discovers sub-directory references (buildings, sky, set, etc.) embedded in the binary. These are file paths stored inside the `.milo_xbox` container. We cannot know them without parsing the header.

**Move data**: The `OnPopulateMoves()` function reads `move_data.dta` to find per-song clip/move names. These depend on the song's `move_instance` animation keys, which are stored inside the song `.milo`. We cannot enumerate them without loading the song first.

**Character outfit model paths**: The outfit symbol (e.g., `mo01`) maps to a model path via `GetOutfitModel()`, which looks up DTA config. This lookup is deterministic from the outfit symbol, but the DTA config is parsed at runtime.

### 3.3 Assessment

**Partially deterministic.** We can predict ~60% of files from inputs alone (venue, song, visualizer, HUD, skeleton). The remaining ~40% (venue sub-files, per-song move clips, character outfit model paths) require either:
1. Parsing .milo_xbox headers server-side, or
2. Pre-computing a dependency manifest from known game data

## 4. Proposed Architecture

### 4.1 Three-Tier Pre-Fetch Strategy

```
┌─────────────────────────────────────────────────────────┐
│  TIER 1: Boot Bundle (existing)                         │
│  All DTA/DTB config files. Single HTTP request.         │
│  Timing: Before engine init.                            │
│  Status: Already implemented.                           │
└─────────────────────────────────────────────────────────┘
                          │
                          v
┌─────────────────────────────────────────────────────────┐
│  TIER 2: Common Assets Pre-Fetch                        │
│  Shared UI panels, skeleton, crowd base, character base │
│  Timing: After boot, during menu navigation.            │
│  Trigger: Background download starts at BOOT_RUNNING.   │
└─────────────────────────────────────────────────────────┘
                          │
                          v
┌─────────────────────────────────────────────────────────┐
│  TIER 3: Song-Specific Directory Fetch                  │
│  Venue + sub-files, song data, moves, characters.       │
│  Timing: When song is selected (before loading screen). │
│  Trigger: Intercept at OnLoadSong or screen transition. │
└─────────────────────────────────────────────────────────┘
```

### 4.2 Tier 2: Common Assets

Files that are loaded regardless of song selection and can be downloaded while the user navigates menus:

```
# UI system panels
ui/gen/*.milo_xbox

# Shared character skeleton
char/main/gen/main.milo_xbox

# Skeleton clips (shared across all characters)
skeleton_clips.milo

# Shared crowd animation base
(crowd_clips.fm targets — venue-independent crowd anims)

# Visualizer
ui/visualizer/gen/visualizer.milo_xbox

# Latency test
test/latency.milo  (only if testing)
```

**Estimated size**: 20-40 MB (need to measure; see Section 7.1).

**Implementation**: Start background `emscripten_fetch()` calls for these files immediately after `BOOT_RUNNING` state. Use `WebAssetsFetchByPath()` (already exists) in non-blocking mode. The engine will find them in MEMFS when it asks.

### 4.3 Tier 3: Song-Specific Pre-Fetch

When a song is selected, we know the venue and can pre-fetch a directory of files:

```
# Venue world + all sub-files
world/{venue}/gen/*.milo_xbox

# Song data
songs/{song}/**/*.milo_xbox

# Character outfits (derivable from profile + DTA config)
char/main/backup/gen/*.milo_xbox  (for selected outfits)
char/main/dancer/gen/*.milo_xbox  (for backup dancers)

# Move data (per-song)
modular_song_data/charclips/gen/*.milo_xbox
modular_song_data/hammoves/gen/*.milo_xbox
modular_song_data/transition_charclips/gen/*.milo_xbox
```

**Challenge**: We don't know which specific charclips/hammoves until `OnPopulateMoves()` runs. But we can use the **directory prefix approach**: fetch ALL files under `modular_song_data/` for the current song's move set.

## 5. Server API Design

### 5.1 New Endpoint: `/api/manifest?prefix=<dir>`

Filter the manifest by directory prefix. Returns only files under the given path.

```http
GET /api/manifest?prefix=world/dci/gen/
```
```json
{
  "files": [
    {"path": "world/dci/gen/dci.milo_xbox", "size": 8234567},
    {"path": "world/dci/gen/dci_buildings.milo_xbox", "size": 2345678},
    {"path": "world/dci/gen/dci_sky.milo_xbox", "size": 1234567}
  ],
  "count": 3,
  "total_size": 11814812
}
```

**Implementation** (in `server.py`): Parse `prefix` query param from URL, filter `os.walk()` results. Minimal change to existing `_serve_manifest()`.

### 5.2 New Endpoint: `/api/bundle?prefix=<dir>`

Like the existing `/api/bundle` but filtered by prefix. Downloads all files under a directory as a single binary bundle, using the same packed format.

```http
GET /api/bundle?prefix=world/dci/gen/
```

Returns: binary bundle containing only files matching the prefix.

**Why a bundle**: Reduces HTTP round-trips. Loading 15 venue sub-files as individual fetches means 15 TCP connections; a single bundle is one connection with better compression potential.

### 5.3 New Endpoint: `/api/deps/<file>` (Future)

Server-side .milo_xbox header parser that extracts sub-file references. Returns the dependency tree for a given .milo_xbox file.

```http
GET /api/deps/world/dci/gen/dci.milo_xbox
```
```json
{
  "file": "world/dci/gen/dci.milo_xbox",
  "subdirs": [
    "world/dci/gen/dci_buildings.milo_xbox",
    "world/dci/gen/dci_sky.milo_xbox",
    "world/dci/gen/dci_set.milo_xbox"
  ]
}
```

**Complexity**: Requires a Python .milo_xbox parser (parse the ChunkStream header, extract subdir FilePaths). This is a nice-to-have — the prefix-based approach (Section 5.1/5.2) covers most cases without needing to parse binary formats.

### 5.4 Static Dependency Map (Alternative)

Pre-compute a JSON mapping from `(song, venue)` -> list of all required files. Generate it offline by running the engine in a headless mode and logging all `AsyncFile::_OpenAsync()` calls.

```json
{
  "songs/YMCA/YMCA.milo": {
    "venue": "dci",
    "files": [
      "world/dci/gen/dci.milo_xbox",
      "world/dci/gen/dci_buildings.milo_xbox",
      "songs/YMCA/gen/YMCA.milo_xbox",
      "modular_song_data/charclips/gen/ymca_clip1.milo_xbox",
      ...
    ]
  }
}
```

Serve as `/api/deps-map.json`. Client fetches it once and uses it for all pre-fetch decisions.

**Tradeoff**: Requires maintaining the map when songs/venues change. Best for a fixed song list (which DC3 has).

## 6. Code-Level Integration Points

### 6.1 Where to Hook Tier 2 (Background Pre-Fetch)

**File**: `native/src/main_web.cpp`

Add a new boot state between `BOOT_GPU_READY` and `BOOT_RUNNING`:

```
BOOT_GPU_READY -> BOOT_PREFETCH -> BOOT_RUNNING
```

In `BOOT_PREFETCH`:
1. Fetch the filtered manifest for common assets (`/api/manifest?prefix=ui/gen/` etc.)
2. Start background `emscripten_fetch()` calls for each file
3. Transition to `BOOT_RUNNING` immediately (don't wait — these are background)

Alternatively, start pre-fetches from within `BOOT_RUNNING` after the first few frames, to avoid delaying first paint.

### 6.2 Where to Hook Tier 3 (Song-Specific Pre-Fetch)

**Option A: Hook `LoadMgr::sFileOpenCallback`**

The `LoadMgr::AddLoader()` already calls `sFileOpenCallback(file.c_str())` for every file the engine requests. On native desktop, this callback exists but isn't used for web. We could:

1. Set `sFileOpenCallback` to a function that records requested paths
2. On the first song load, log ALL paths the engine requests
3. Use this to build the static dependency map (Section 5.4)

**Option B: Hook `HamDirector::OnLoadSong()`**

This is the earliest point where we know the song + venue. Add an `#ifdef HX_WEB` block that:

1. Queries `/api/manifest?prefix=world/{venue}/gen/` to get venue file list
2. Queries `/api/manifest?prefix=songs/{song}/gen/` for song files
3. Fetches all matching files via `WebAssetsFetchByPath()` (non-blocking)
4. Proceeds with the normal loading flow

The key insight: **we don't need to wait for pre-fetches to complete.** The engine will request these files via `AsyncFile::_OpenAsync()`. If the file is already in MEMFS (pre-fetch arrived), `fopen()` succeeds immediately. If not yet arrived, the existing ASYNCIFY yield handles it. Pre-fetch is an **optimization**, not a requirement.

**Option C: Intercept UIScreen transition to game_screen**

The `UI` system transitions screens via `SetScreen()`. When transitioning to `game_screen`, we could start pre-fetches. This is later than OnLoadSong but still before the loading screen starts polling.

### 6.3 Recommended Hook: OnLoadSong + Background Tier 2

```cpp
// In HamDirector::OnLoadSong(), after FilePathTracker setup:
#ifdef HX_WEB
    // Pre-fetch venue files while the engine sets up the loading pipeline
    const char *venue = TheGameData->Venue().Str();
    if (venue && *venue) {
        WebPrefetchDirectory(MakeString("world/%s/gen/", venue));
    }
    // Pre-fetch song files
    const char *songPath = a->Str(2);
    if (songPath && *songPath) {
        const char *songBase = FileGetBase(songPath);
        WebPrefetchDirectory(MakeString("songs/%s/", songBase));
    }
    // Pre-fetch modular song data (all clips/moves — we don't know which yet)
    WebPrefetchDirectory("modular_song_data/");
#endif
```

`WebPrefetchDirectory()` would be a new function in `WebAssets.cpp` that:
1. Fetches `/api/manifest?prefix={dir}` synchronously (small JSON, fast)
2. Starts non-blocking `emscripten_fetch()` for each file in the manifest
3. Returns immediately (does not wait for fetches to complete)

### 6.4 AsyncFile Fallback

The existing `AsyncFile_Native.cpp` on-demand fetch remains as a fallback:

```cpp
virtual void _OpenAsync() {
    mFp = fopen(mFilename.c_str(), fmode);
#ifdef __EMSCRIPTEN__
    if (!mFp && !(mMode & 0x300)) {
        // File not in MEMFS yet — fetch it now (ASYNCIFY yields)
        if (WebAssetsFetchSync(mFilename.c_str())) {
            mFp = fopen(mFilename.c_str(), fmode);
        }
    }
#endif
}
```

With effective pre-fetching, this fallback rarely triggers. When it does, ASYNCIFY handles it gracefully.

## 7. Tradeoffs and Risks

### 7.1 Total Asset Size

Need to measure, but rough estimate based on typical DC3 content:

| Category | Estimated Files | Estimated Size |
|----------|----------------|----------------|
| UI panels (.milo_xbox) | ~15 | ~10 MB |
| Venue world + sub-files | ~8 per venue | ~15-25 MB |
| Character outfits | ~4-6 per song | ~8-15 MB |
| Song data | ~3 | ~5-10 MB |
| Modular song data (clips/moves) | ~20-50 per song | ~10-30 MB |
| Crowd clips | ~5-10 | ~5-10 MB |
| Visualizer | 1 | ~2-5 MB |
| **Total per song** | | **~55-105 MB** |

**Background pre-fetch of ALL assets** (every venue, every song) would be hundreds of MB — too much for a speculative download. The tiered approach is necessary.

### 7.2 Risk: Over-Fetching Modular Song Data

The `modular_song_data/` directory likely contains clips for ALL songs, not just the selected one. Fetching the entire directory could download 50-200 MB of unused clips.

**Mitigation**: Use the static dependency map (Section 5.4) to fetch only the clips needed for the selected song. Or, accept the over-fetch for simplicity — a single bundle request is faster than computing exact dependencies.

**Alternative mitigation**: Organize the server's modular_song_data by song, e.g., `/api/manifest?prefix=modular_song_data/&song=YMCA`. Requires server-side knowledge of song-to-clip mapping.

### 7.3 Risk: Race Condition Between Pre-Fetch and Engine

If the engine requests a file before the pre-fetch completes:
- With ASYNCIFY: engine yields, pre-fetch eventually writes to MEMFS, engine retries `fopen()` — works
- Without ASYNCIFY: `fopen()` fails, `WebAssetsFetchSync()` triggers a synchronous fetch — works but blocks

**Conclusion**: The race is benign. Pre-fetch is an optimization; the fallback path handles misses.

### 7.4 Risk: MEMFS Memory Pressure

All fetched files live in MEMFS (RAM). A 100 MB venue + song load on top of the WASM heap could hit browser memory limits on low-end devices.

**Mitigation**: Track total MEMFS usage. Purge old venue/song files when loading new ones (the engine's `FileMerger::Clear()` deletes objects but doesn't free MEMFS files). Add a `WebAssetsEvict(prefix)` function that deletes MEMFS files matching a pattern.

### 7.5 Risk: Bundle Size vs. Latency

A single bundle request for a venue (15-25 MB) has lower overhead than 8 individual fetches but higher latency-to-first-byte. The engine can start parsing the first file while others download if we use individual fetches.

**Recommendation**: Use individual `emscripten_fetch()` calls (not bundles) for Tier 3. This lets the engine start processing files as they arrive. Bundle format is best for Tier 1 (many small config files).

## 8. Implementation Plan

### Phase 1: Server-Side Prefix Filtering (1-2 hours)

1. Add `prefix` query parameter to `/api/manifest` in `server.py`
2. Add prefix-filtered `/api/bundle` endpoint
3. Test with `curl /api/manifest?prefix=world/dci/gen/`

### Phase 2: WebPrefetchDirectory() (2-3 hours)

1. Add `WebPrefetchDirectory(const char *prefix)` to `WebAssets.cpp`:
   - Fetch `/api/manifest?prefix={prefix}` (synchronous XHR — small JSON)
   - Start non-blocking `emscripten_fetch()` for each file
   - Return immediately
2. Add `WebAssetsEvict(const char *prefix)` for MEMFS cleanup
3. Test: verify files appear in MEMFS before engine requests them

### Phase 3: Tier 2 — Background Common Assets (1-2 hours)

1. Add `BOOT_PREFETCH` state to `main_web.cpp` boot state machine
2. Pre-fetch common UI panels, skeleton, visualizer during menu navigation
3. Measure: how many on-demand fetches are eliminated?

### Phase 4: Tier 3 — Song-Specific Pre-Fetch (2-3 hours)

1. Add `#ifdef HX_WEB` block in `HamDirector::OnLoadSong()` to pre-fetch:
   - Venue directory
   - Song directory
   - Modular song data (or use static dep map)
2. Measure: loading time with vs. without pre-fetch
3. Optionally: build static dependency map by logging `AsyncFile::_OpenAsync()` paths

### Phase 5: Remove ASYNCIFY (Optional, Advanced)

If pre-fetching is reliable enough that `WebAssetsFetchSync()` is never called:
1. Remove `-sASYNCIFY` from CMakeLists.txt
2. Replace `WebAssetsFetchSync()` with a hard failure (or remove on-demand fetch entirely)
3. WASM binary size decreases, performance improves
4. **Risk**: any missed pre-fetch path causes a hard failure instead of yielding

**Recommendation**: Keep ASYNCIFY as a safety net even with pre-fetching. The code-size cost is acceptable; the reliability gain is significant.

### Phase 6: Static Dependency Map (Optional, Quality)

1. Run the engine headless for each song, log all file opens
2. Generate `deps-map.json`
3. Serve from `/api/deps-map.json`
4. Client fetches it once at boot, uses it for precise Tier 3 pre-fetch
5. Eliminates over-fetching of `modular_song_data/`

## 9. Metrics to Track

- **Pre-fetch hit rate**: % of `AsyncFile::_OpenAsync()` calls where file is already in MEMFS
- **Loading time**: wall-clock time from song selection to `GamePanel::PollForLoading() == state 4`
- **On-demand fetch count**: number of `WebAssetsFetchSync()` calls per song load (goal: 0)
- **MEMFS usage**: peak memory consumption after loading a song
- **Bundle download time**: total time for Tier 2 + Tier 3 pre-fetches to complete

## 10. Summary

The pre-fetch strategy is a **three-tier optimization** layered on top of the existing ASYNCIFY on-demand fetch:

1. **Tier 1** (existing): DTA/DTB config bundle at boot
2. **Tier 2** (new): Background download of common UI/character assets during menus
3. **Tier 3** (new): Directory-based pre-fetch of venue/song/move assets when song is selected

The key architectural insight is that pre-fetching is **opportunistic, not mandatory**. The ASYNCIFY fallback handles any missed files. This means we can ship pre-fetching incrementally — each tier reduces loading latency without requiring the others.

The server changes are minimal (prefix filtering on existing endpoints). The client changes are localized to `WebAssets.cpp` (new `WebPrefetchDirectory()` function) and two hook points (`main_web.cpp` for Tier 2, `HamDirector.cpp` for Tier 3).
