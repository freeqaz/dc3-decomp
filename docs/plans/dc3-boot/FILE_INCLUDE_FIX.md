# Fix __FILE__ in Headers via Include Path Cleanup

## Problem

MSVC sets `__FILE__` based on how it resolves include paths. Headers found via
`/I src` (absolutized to `/home/free/.../src`) get Linux host paths as `__FILE__`,
while the original binary has Windows paths like `e:\lazer_build_gmc1\system\src\utl/StlAlloc.h`.

This causes wrong `MakeString<char[N]>` template instantiations (N = `strlen(__FILE__)+1`),
producing different `bl` targets in assert paths. ~200 functions dropped from 100% due to this.

## Root Cause

`/I src` in `cflags_includes` gets absolutized to a Linux path by `absolutize_include()` in
`project.py`. Headers resolved through this path get Linux `__FILE__` values instead of the
correct Windows-mapped paths from `/I e:/lazer_build_gmc1/system/src`.

## Fix

### Step 1: Remove `/I src` from cflags_includes

**File**: `tools/defines_common.py`

Remove the `/I src` entry. All includes should resolve through the Windows-mapped paths:
- `/I e:/lazer_build_gmc1/system/src` (maps to `src/system/`)
- `/I e:/lazer_build_gmc1/lazer/src` (maps to `src/lazer/`)

### Step 2: Fix includes that relied on `/I src`

These includes use `system/` or `lazer/` prefix which only resolves via `/I src`.
They need to drop the prefix so they resolve via the Windows-mapped `/I` paths.

#### Category A: `src/system/` files including with `system/` prefix (self-referential)
Strip `system/` prefix — resolves via `/I e:/lazer_build_gmc1/system/src`.

| File | Old Include | New Include |
|------|------------|-------------|
| `src/system/stlport/stl/_alloc.h:731` | `<system/utl/StlAlloc.h>` | `<utl/StlAlloc.h>` |
| `src/system/utl/StlAlloc.h:2` | `<system/utl/MemMgr.h>` | `<utl/MemMgr.h>` |
| `src/system/utl/DebugMeter.cpp:3` | `<system/rndobj/Rnd.h>` | `<rndobj/Rnd.h>` |
| `src/system/utl/DebugMeter.h:2` | `<system/math/Color.h>` | `<math/Color.h>` |
| `src/system/os/PlatformMgr_Xbox.cpp:7` | `"system/utl/GlitchFinder.h"` | `"utl/GlitchFinder.h"` |
| `src/system/synth/WavReader.h:4` | `"system/os/File.h"` | `"os/File.h"` |
| `src/system/synth/WavReader.h:5` | `"system/utl/FileStream.h"` | `"utl/FileStream.h"` |
| `src/system/synth/WavReader.h:6` | `"system/utl/WaveFile.h"` | `"utl/WaveFile.h"` |
| `src/system/synth_xbox/PitchDetector.cpp:1` | `"system/synth_xbox/PitchDetector.h"` | `"PitchDetector.h"` |
| `src/system/synth_xbox/PitchDetector.cpp:2` | `"system/utl/MemMgr.h"` | `"utl/MemMgr.h"` |
| `src/system/synth_xbox/PitchDetector.h:3` | `"system/synth_xbox/FftIpp.h"` | `"FftIpp.h"` |
| `src/system/synth_xbox/Synapse_dsp.h:4` | `"system/stlport/stl/_vector.h"` | `"stlport/stl/_vector.h"` |
| `src/system/net/curl/lib/urldata.h:142` | `"system/zlib/zlib.h"` | `"zlib/zlib.h"` |

#### Category B: `src/lazer/` files including with `lazer/` prefix (self-referential)
Strip `lazer/` prefix — resolves via `/I e:/lazer_build_gmc1/lazer/src`.

| File | Old Include | New Include |
|------|------------|-------------|
| `src/lazer/game/SongDB.cpp:1` | `"lazer/game/SongDB.h"` | `"game/SongDB.h"` |
| `src/lazer/game/Shuttle.cpp:1` | `"lazer/game/Shuttle.h"` | `"game/Shuttle.h"` |
| `src/lazer/game/HamUserMgr.cpp:1` | `"lazer/game/HamUserMgr.h"` | `"game/HamUserMgr.h"` |
| `src/lazer/game/GameMode.cpp:1` | `"lazer/game/GameMode.h"` | `"game/GameMode.h"` |
| `src/lazer/game/HamUser.cpp:1` | `"lazer/game/HamUser.h"` | `"game/HamUser.h"` |
| `src/lazer/game/BustAMovePanel.h:9` | `"lazer/meta_ham/HamPanel.h"` | `"meta_ham/HamPanel.h"` |
| `src/lazer/game/BustAMovePanel.cpp:14` | `"lazer/game/GameMode.h"` | `"game/GameMode.h"` |
| `src/lazer/game/BustAMovePanel.cpp:16` | `"lazer/meta_ham/HamPanel.h"` | `"meta_ham/HamPanel.h"` |
| `src/lazer/game/BustAMovePanel.cpp:36` | `"lazer/game/Game.h"` | `"game/Game.h"` |
| `src/lazer/meta_ham/HamSongMetadata.cpp:1` | `"lazer/meta_ham/HamSongMetadata.h"` | `"meta_ham/HamSongMetadata.h"` |
| `src/lazer/meta_ham/SigninScreen.cpp:1` | `"lazer/meta_ham/SigninScreen.h"` | `"meta_ham/SigninScreen.h"` |
| `src/lazer/meta_ham/SongRecord.cpp:2` | `"lazer/meta_ham/HamSongMgr.h"` | `"meta_ham/HamSongMgr.h"` |
| `src/lazer/meta_ham/SongRecord.h:3` | `"lazer/meta_ham/HamSongMetadata.h"` | `"meta_ham/HamSongMetadata.h"` |
| `src/lazer/meta_ham/Award.cpp:1` | `"lazer/meta_ham/Award.h"` | `"meta_ham/Award.h"` |
| `src/lazer/meta_ham/AccomplishmentCategory.h:3` | `"lazer/meta_ham/Award.h"` | `"meta_ham/Award.h"` |
| `src/lazer/meta_ham/AccomplishmentCategory.cpp:1` | `"lazer/meta_ham/AccomplishmentCategory.h"` | `"meta_ham/AccomplishmentCategory.h"` |
| `src/lazer/meta_ham/AccomplishmentCategory.cpp:3` | `"lazer/meta_ham/Award.h"` | `"meta_ham/Award.h"` |
| `src/lazer/meta_ham/AccomplishmentGroup.cpp:1` | `"lazer/meta_ham/AccomplishmentGroup.h"` | `"meta_ham/AccomplishmentGroup.h"` |
| `src/lazer/meta_ham/AccomplishmentConditional.h:3` | `"lazer/meta_ham/Accomplishment.h"` | `"meta_ham/Accomplishment.h"` |
| `src/lazer/meta_ham/AccomplishmentConditional.cpp:1` | `"lazer/meta_ham/AccomplishmentConditional.h"` | `"meta_ham/AccomplishmentConditional.h"` |
| `src/lazer/meta_ham/AccomplishmentConditional.cpp:4` | `"lazer/meta_ham/Accomplishment.h"` | `"meta_ham/Accomplishment.h"` |
| `src/lazer/meta_ham/HelpBarPanel.h:3` | `"lazer/meta_ham/HamPanel.h"` | `"meta_ham/HamPanel.h"` |
| `src/lazer/meta_ham/HamSongMgr.h:4` | `"lazer/meta_ham/Playlist.h"` | `"meta_ham/Playlist.h"` |
| `src/lazer/meta_ham/PlaylistSongProvider.cpp:1` | `"lazer/meta_ham/PlaylistSongProvider.h"` | `"meta_ham/PlaylistSongProvider.h"` |
| `src/lazer/meta_ham/ChallengeSortMgr.cpp:7` | `"lazer/game/GameMode.h"` | `"game/GameMode.h"` |
| `src/lazer/meta_ham/ChallengeSortNode.cpp:20` | `"lazer/net_ham/RockCentral.h"` | `"net_ham/RockCentral.h"` |
| `src/lazer/meta_ham/CampaignEra.cpp:1` | `"lazer/meta_ham/CampaignEra.h"` | `"meta_ham/CampaignEra.h"` |
| `src/lazer/meta_ham/Accomplishment.cpp:1` | `"lazer/meta_ham/Accomplishment.h"` | `"meta_ham/Accomplishment.h"` |
| `src/lazer/meta_ham/AccomplishmentOneShot.cpp:1` | `"lazer/meta_ham/AccomplishmentOneShot.h"` | `"meta_ham/AccomplishmentOneShot.h"` |
| `src/lazer/meta_ham/AccomplishmentOneShot.cpp:6` | `"lazer/meta_ham/AccomplishmentConditional.h"` | `"meta_ham/AccomplishmentConditional.h"` |
| `src/lazer/meta_ham/HamSongMgr.cpp:1` | `"lazer/meta_ham/HamSongMgr.h"` | `"meta_ham/HamSongMgr.h"` |
| `src/lazer/meta_ham/HamSongMgr.cpp:9` | `"lazer/meta_ham/Playlist.h"` | `"meta_ham/Playlist.h"` |
| `src/lazer/meta_ham/SongSortMgr.cpp:6` | `"lazer/game/GameMode.h"` | `"game/GameMode.h"` |
| `src/lazer/meta_ham/SongSortMgr.cpp:18` | `"lazer/meta_ham/MetaPerformer.h"` | `"meta_ham/MetaPerformer.h"` |
| `src/lazer/meta_ham/HamUI.cpp:1` | `"lazer/meta_ham/HamUI.h"` | `"meta_ham/HamUI.h"` |
| `src/lazer/meta_ham/HamPanel.cpp:1` | `"lazer/meta_ham/HamPanel.h"` | `"meta_ham/HamPanel.h"` |
| `src/lazer/meta_ham/HamPanel.cpp:4` | `"lazer/meta_ham/HamUI.h"` | `"meta_ham/HamUI.h"` |

#### Category C: Cross-tree includes
These are trickier — files in one tree including from another.

| File | Old Include | New Include | Notes |
|------|------------|-------------|-------|
| `src/lazer/meta_ham/WeightInput.h:5` | `"system/world/Instance.h"` | `"world/Instance.h"` | lazer→system, resolves via `/I e:/.../system/src` |
| `src/lazer/meta_ham/WeightInput.h:7` | `"system/ui/UIListProvider.h"` | `"ui/UIListProvider.h"` | lazer→system |
| `src/system/meta/FixedSizeSaveable.h:2` | `"lazer/meta_ham/HamMemcardAction.h"` | `"meta_ham/HamMemcardAction.h"` | system→lazer, resolves via `/I e:/.../lazer/src` |

#### Category D: Top-level src/ files
| File | Old Include | New Include | Notes |
|------|------------|-------------|-------|
| `src/ChecksumData_xbox.cpp:1` | `"system/math/FileChecksum.h"` | `"math/FileChecksum.h"` | Resolves via `/I e:/.../system/src` |
| `src/link_glue.cpp:15-24` | `"system/..."` | Strip `system/` prefix | 10 includes, all system headers |

### Step 3: Rebuild and verify

```bash
# Reconfigure to regenerate build.ninja without /I src
./scripts/build/configure.sh
# Full rebuild needed since include paths changed
ninja
```

### Step 4: Verify __FILE__ strings in output

```bash
strings build/373307D9/src/system/utl/MemMgr.obj | grep -i stlalloc
# Should show: e:\lazer_build_gmc1\system\src\utl/StlAlloc.h (not Linux path)
```

## Expected Impact

- Correct `__FILE__` in all headers → correct `MakeString<char[N]>` instantiations
- Recover ~200 functions that dropped from 100%
- Reduce .text size delta (fewer wrong template instantiations)
- Net improvement of ~1-2pp in overall fuzzy match
