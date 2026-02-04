# DC3 Decomp Project State Report

Generated: 2026-01-25

## 1. Git Status

### Current Branch
- **Branch:** `freeqaz/wip-lazer-meta-ham`
- **Status:** 10 commits ahead of `origin/freeqaz/wip-lazer-meta-ham`

### Modified Files (Unstaged)
5 files with uncommitted changes:

| File | Change Summary |
|------|----------------|
| `src/lazer/meta_ham/FitnessCalorieSortMgr.h` | Added `protected:` access specifier for members |
| `src/system/char/ClipDistMap.cpp` | Type fixes: `std::vector<Node>` to `std::vector<ClipDistMap::Node>`, added explicit `(double)` casts in floor() calls |
| `src/system/flow/FlowSetProperty.cpp` | Removed 28-line block of animated/blended property handling code in `Execute()` |
| `src/system/meta/StorePreviewMgr.cpp` | Changed `size_t pos` to `unsigned int pos` |
| `src/system/utl/Locale.cpp` | Major refactor - removed ~136 lines of `Locale::Init()` implementation, leaving only asserts |

### Recent Commits (on this branch)
```
3d7e5eb WIP: Uncommitted work snapshot before stash merge
71ae1ed Restructure function to use early return for fmt check
89ead95 Initialize ret at function level and consolidate returns
1bef8fc Swap bpp and fmt variable declaration order
912a461 Use local ret variables and breaks instead of direct returns
514c5ca Invert condition and reorder switches in DxRnd::D3DFormatForBitmap
da0eb87 Restructure DxRnd::D3DFormatForBitmap to improve control flow matching
7d05c22 Improve match for DxRnd::D3DFormatForBitmap by reordering variable declarations
9d5fcf4 WIP: Progress on multiple functions across meta_ham, gesture, rndobj, and UI
2aa3795 Implement CharLookAt::Load (98.4% match)
```

---

## 2. Build System Status

### Build Files Present
| File | Size | Last Modified |
|------|------|---------------|
| `build.ninja` | 747,321 bytes | Jan 24 23:51 |
| `.ninja_deps` | 44 bytes | Jan 24 23:51 |
| `.ninja_log` | 3,306 bytes | Jan 24 23:51 |

**Status:** Build system is configured and ready.

---

## 3. Progress Report Status

**`build/373307D9/report.json`:** NOT FOUND

The progress report file does not exist. This typically means:
- A build has not been run recently, OR
- The build completed but didn't generate the report

To generate: `ninja build/373307D9/report.json`

---

## 4. objdiff-cli Status

**Location:** `./bin/objdiff-cli` (exists, 155 bytes - likely a wrapper script)

Cannot run report summary since `build/373307D9/report.json` does not exist.

---

## 5. Current Work: Sound::Play Function

**File:** `/home/free/code/milohax/dc3-decomp/src/system/synth/Sound.cpp`

**NOTE:** The Sound.cpp file shows as modified in the original status, but it appears the working tree has been cleaned or the change is only in the stash. The stash contains the Sound::Play implementation work.

The `Sound::Play()` function implementation in the stash handles:
1. **Delayed playback** (when `delayMs > 0.0f`):
   - Creates `DelayArgs` struct with all parameters
   - Adds to `mDelayArgs` vector
   - Calls `StartPolling()`

2. **Immediate playback via SynthSample** (when `mSynthSample` exists):
   - Creates new `SampleInst` via `NewInst()`
   - Configures: event receiver, volume, pan, speed (with transpose calculation)
   - Optionally sets ADSR envelope
   - Sets send, reverb mix, reverb enable
   - Plays with fader volume
   - Adds to `mSamples` vector

3. **Immediate playback via MoggClip** (fallback):
   - Sets event receiver, volume, pan
   - Plays with fader volume

---

## 6. Git Stash Contents

### Current Stash Entries
Only **1 stash entry** exists (stash@{0}):

**stash@{0}**
- **Message:** `WIP on freeqaz/wip-lazer-meta-ham: 3d7e5eb WIP: Uncommitted work snapshot before stash merge`
- **Size:** 1,153 lines of diff
- **Exported to:** `/tmp/claude/stash0_current.patch`

### Stash Contents Summary (25 files changed, ~733 insertions)

#### Lazer/Game Module
| File | Changes |
|------|---------|
| `BustAMovePanel.cpp` | +50 lines - `SetFlashcardImage()` implementation |
| `PartyModeMgr.cpp` | +44 lines - `FinalizePlaytestParty()` implementation |
| `PartyModeMgr.h` | +1 line - declaration |

#### Lazer/Meta HAM Module
| File | Changes |
|------|---------|
| `AccomplishmentManager.cpp` | +45 lines |
| `HamStoreProvider.cpp` | +53 lines |
| `MainMenuPanel.cpp` | +4 lines |
| `MainMenuPanel.h` | +2 lines |
| `SkeletonChooser.cpp` | +116 lines |
| `SkeletonChooser.h` | +1 line |

#### System/Char Module
| File | Changes |
|------|---------|
| `CharBonesMeshes.cpp` | +22 lines |
| `CharClip.cpp` | +5 lines |
| `CharClipGroup.cpp` | +124 lines |
| `CharClipGroup.h` | +1 line |
| `CharDriver.cpp` | +1 line |
| `CharEyes.cpp` | +47 lines |
| `CharEyes.h` | +1 line |
| `CharInterest.cpp` | +46 lines |
| `CharLipSync.cpp` | +157 lines |
| `CharLipSync.h` | +4 lines |

#### System/Math Module
| File | Changes |
|------|---------|
| `Easing.h` | +2 lines |
| `Rand.h` | +1 line |
| `Rot.cpp` | +18 lines |
| `Rot.h` | +3 lines |

#### Other
| File | Changes |
|------|---------|
| `StorePreviewMgr.cpp` | -1 line |
| `Sound.cpp` | +33/-15 lines |

---

## 7. Backup Files (.bak)

### Source Code Backups
| File | Description |
|------|-------------|
| `/home/free/code/milohax/dc3-decomp/src/system/gesture/FreestyleMotionFilter.cpp.bak` | Gesture system |
| `/home/free/code/milohax/dc3-decomp/src/system/hamobj/RhythmDetector.cpp.bak` | HAM object system |
| `/home/free/code/milohax/dc3-decomp/src/system/net/curl/lib/speedcheck.c.bak` | Network/curl |
| `/home/free/code/milohax/dc3-decomp/src/system/rndobj/Mat_NG.cpp.bak` | Render object materials |
| `/home/free/code/milohax/dc3-decomp/src/system/ui/UIScreen.cpp.bak` | UI system |
| `/home/free/code/milohax/dc3-decomp/src/system/utl/MBT.cpp.bak` | Utility |
| `/home/free/code/milohax/dc3-decomp/src/system/world/SpotlightDrawer_NG.cpp.bak` | World rendering |
| `/home/free/code/milohax/dc3-decomp/MetaPerformer.cpp.bak` | Meta performer (root level) |

### Config Backups
| File |
|------|
| `/home/free/code/milohax/dc3-decomp/objdiff.json.bak` |

### Ghidra Internal Backups
| File |
|------|
| `/home/free/code/milohax/dc3-decomp/ghidra_projects/DC3/DC3.rep/versioned/~index.bak` |
| `/home/free/code/milohax/dc3-decomp/ghidra_projects/DC3/DC3.rep/idata/~index.bak` |

---

## 8. Exported Patches

Stash contents have been exported to patch files for preservation:

| Patch File | Size | Contents |
|------------|------|----------|
| `/tmp/claude/stash0_current.patch` | 1,153 lines | Full stash@{0} - main WIP with 25 files |

---

## Summary

The project is in an active development state with:

1. **Stashed work:** Significant batch of decomp work (733 insertions across 25 files) - now exported to `/tmp/claude/stash0_current.patch`
2. **Uncommitted changes:** 5 files with various fixes (type corrections, access specifiers)
3. **Build system:** Configured but report.json not generated
4. **Branch status:** 10 commits ahead of remote, needs push

### Key Functions in Stash
- `BustAMovePanel::SetFlashcardImage()` - Flashcard display logic
- `PartyModeMgr::FinalizePlaytestParty()` - Party mode setup
- `CharLipSync` - 157 lines of lip sync implementation
- `CharClipGroup` - 124 lines of clip group handling
- `SkeletonChooser` - 116 lines
- Various character animation functions

### Recommended Next Steps
1. Build the project to verify changes compile: `ninja`
2. Generate progress report: `ninja build/373307D9/report.json`
3. Consider applying the stash to recover the work: `git stash apply stash@{0}`
4. Commit or stage the 5 modified files
5. Push the 10 local commits to remote
