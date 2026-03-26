# Eye Tracking: WorldCamInterest Missing After Venue Merge

**Date**: 2026-03-25
**Status**: Root cause identified, fix pending

## Problem

Characters' eyes don't look at the camera during gameplay. They look at each other (other dancers' eye interest objects) but never at the camera, unlike the original Xbox behavior.

## Investigation Summary

### The Eye System

DC3's eye tracking is driven by `CharEyes` (src/system/char/CharEyes.cpp), which has two targeting paths:

1. **Camera weight path** (line 1345): When `mCamWeight` (a `CharWeightSetter`) has weight > 0, eyes interpolate toward `cam->WorldXfm().v`. This path gets the camera from `TheWorld->Cam()` → `RndCam::Current()` → `TheRnd.GetDefaultCam()`.

2. **Interest object path** (line 1358): When `camWeight == 0`, eyes look at `CharInterest` objects collected from the scene.

### What We Found

- **`mCamWeight` is intentionally empty** in the character binary data (ref=''). DC3 characters were NOT authored with a CharWeightSetter for camera tracking. This is the same on Xbox.

- **The interest system IS working**: 4 interests are loaded (angel_eyes.intr, aubrey_eyes.intr, male_backup_eyes.intr ×2), all parented to `bone_head.mesh` on other dancers.

- **Every venue has `WorldCamInterest.intr`**: A `CharInterest` object exists in every venue .milo file (rollerrink, dclive, houseparty, streetside, dci, throneroom, etc.). This object is parented to the camera and represents the camera position for the eye tracking interest system.

### Root Cause

**`WorldCamInterest.intr` is not surviving the venue merge process.**

The interest collection happens in two places:
- `HamWardrobe::SyncInterestObjects(ObjectDir *dir)` — collects from venue dir + character subdirs
- `Character::FindInterestObjects(ObjectDir *dir)` — same iteration pattern

Both use `ObjDirItr<CharInterest>` to iterate over CharInterest objects. The `WorldCamInterest.intr` exists in the venue's .milo binary data, but after the venue is merged into the gameplay WorldDir, the CharInterest object is not present in the merged directory — so the iterator never finds it.

### Expected Behavior (Xbox)

1. Venue .milo loads → WorldDir contains `WorldCamInterest.intr` (CharInterest parented to venue camera)
2. `SyncInterestObjects` / `FindInterestObjects` iterates venue dir → finds `WorldCamInterest.intr` along with character eye interests
3. Character eyes receive 5+ interests: other dancers' eyes AND the camera interest
4. During gameplay, `CharEyes::Poll()` picks `WorldCamInterest.intr` as the current interest periodically → eyes look toward camera position
5. The interest's world position follows the camera automatically through the transform parent chain

### Where the Merge Happens

The venue merge flow:
1. `HamDirector` loads the venue via `FileMerger`
2. `FileMerger` loads the venue .milo and calls `MergeDirs` to transfer objects into the gameplay WorldDir
3. `MergeDirs` (in `src/system/obj/Dir.cpp`) iterates source objects and transfers them to the target directory
4. After merge, `HamDirector::VenueEnter()` calls `dir->Enter()` on the venue
5. `WORLD_SETUP_CHARACTERS` DTA macro runs, and `SyncInterestObjects` collects interests

The bug is in step 3: `MergeDirs` is not transferring `WorldCamInterest.intr` (a CharInterest, which is a RndTransformable subclass) from the venue .milo into the merged world.

### venue DTA Property: lookat_cameras

The venue type definition in `world_objects.dta` has:
```
(venue
   (lookat_cameras 1)
   ...
   (help "Do the guys look at the cameras in this venue? if false, they can still
          be forced to look at the camera through song anim keyframes or shot-specific lookats"))
```

This property is DTA-only (not consumed by C++ code). It's a content flag — when true, `WorldCamInterest.intr` exists in the venue and characters should look at it.

### Files Involved

| File | Role |
|------|------|
| `src/system/char/CharEyes.cpp` | Eye tracking Poll, interest selection |
| `src/system/char/CharEyes.h` | CharEyes class, mCamWeight, mInterests |
| `src/system/char/CharInterest.h/.cpp` | Interest point definition |
| `src/system/char/CharLookAt.h/.cpp` | IK-based look-at for individual eyes |
| `src/system/char/Character.cpp` | FindInterestObjects, SetInterestObjects, ValidateInterest |
| `src/system/hamobj/HamWardrobe.cpp` | SyncInterestObjects (collects interests from venue) |
| `src/system/hamobj/HamDirector.cpp` | VenueEnter, venue loading |
| `src/system/char/FileMerger.cpp` | .milo merge system |
| `src/system/obj/Dir.cpp` | MergeDirs — object transfer between directories |
| `orig-assets/extracted/world/worldbase.dta` | WORLD_SETUP_CHARACTERS macro |
| `orig-assets/extracted/world/world_objects.dta` | Venue type definition, lookat_cameras |

### Venue Interest Objects (from .milo data)

| Venue | WorldCamInterest variants | Other interests |
|-------|--------------------------|-----------------|
| rollerrink | .intr, A, B, C | angel_eyes, aubrey_eyes |
| dclive | .intr, A, C, D | bodie_eyes, emilia_eyes, CharInterest_boomy, CharInterest_LookUp |
| houseparty | .intr, A, B, D | lilt_eyes, taye_eyes |
| streetside | .intr, A, B, C, D | glitch_eyes, mo_eyes |
| dci | .intr, A, B, C, D | bodie_eyes, glitch_eyes, lilt_eyes, lima_eyes, mo_eyes, rasa_eyes, taye_eyes, video_screen |
| throneroom | .intr, A, B, C, D | oblio_eyes, tan_eyes, CharInterest_down/lookUp |
| shared | .intr | (none) |

### Fix Strategy

Ensure the MergeDirs process transfers `CharInterest` objects (specifically `WorldCamInterest.intr`) from the venue .milo into the merged WorldDir. This may involve:

1. Checking if MergeDirs has a type filter that skips CharInterest
2. Checking if the CharInterest is in a subdir that doesn't get traversed
3. Checking if the transform parent chain (CharInterest → RndCam) causes issues during merge

Once `WorldCamInterest.intr` is present in the merged world, the existing `SyncInterestObjects` / `FindInterestObjects` code will automatically collect it, and `CharEyes::Poll()` will include it in the interest selection pool — making characters periodically look at the camera.

---

## Unit Test Spec: WorldCamInterest.intr Survival Through Venue Merge

### Test file
`native/tests/test_venue_interest_merge.cpp` — add to `milo-tests` source list in `native/CMakeLists.txt` (~line 1645).

### Fixture
Use `EngineTestFixture` (from `test_helpers.h`) which initializes the engine headlessly via `EnsureEngineInit()`. Required because `ObjDirItr<CharInterest>` needs RTTI, `MergeDirs` relies on hash tables and ref rings, and `RndTransformable` needs the Rnd subsystem.

Reuse helpers from `test_merge_scope_parity.cpp`: `GetMiloLibRoot()`, `TryLoadStandalone()`, `MergeNonProxy()`, `MergeProxy()`, `VerifyAllRingsInDir()`.

### Tier 1: Synthetic Tests (no assets, always runs)

#### Test 1: `SyntheticCharInterestSurvivesNonProxyMerge`
Verify a programmatically created CharInterest transfers from source to target dir during non-proxy merge.

1. Create source and target `ObjectDir`
2. Create `CharInterest` via `Hmx::Object::New<CharInterest>()`, set name `"WorldCamInterest.intr"` in source
3. Run `MergeNonProxy(sourceDir, targetDir)`, delete source
4. **Assert**: `targetDir->FindObject("WorldCamInterest.intr")` is not null
5. **Assert**: `dynamic_cast<CharInterest*>(found)` succeeds
6. **Assert**: `ObjDirItr<CharInterest>(targetDir, true)` finds at least 1 object

#### Test 2: `SyntheticCharInterestSurvivesProxyMerge`
Verify CharInterest survives proxy merge path (venue as subdir).

1. Create `worldRoot`, staging dir, and `venueDir` named "test_venue"
2. Create CharInterest `"WorldCamInterest.intr"` in venueDir
3. Run `MergeProxy(venueDir, worldRoot)`
4. **Assert**: `ObjDirItr<CharInterest>(worldRoot, true)` finds the interest (recursive)

#### Test 3: `SyntheticInterestCollectionMatchesSyncPattern`
Core bug scenario — replicate the exact `SyncInterestObjects` iteration pattern after merge.

1. Create source with CharInterest `"WorldCamInterest.intr"` + `"dancer_eyes.intr"`
2. Run `MergeNonProxy(sourceDir, targetDir)`, delete source
3. Replicate HamWardrobe::SyncInterestObjects collection:
   ```cpp
   ObjPtrList<CharInterest> interests(owner);
   for (ObjDirItr<CharInterest> it(targetDir, true); it != nullptr; ++it)
       interests.push_back(it);
   ```
4. **Assert**: List contains exactly 2 items
5. **Assert**: One named `"WorldCamInterest.intr"`, one named `"dancer_eyes.intr"`

#### Test 4: `SyntheticInterestTransformParentSurvivesMerge`
Verify transform parent relationship (CharInterest → camera) is preserved.

1. Create source with `RndTransformable` "fake_cam.trans" at position (100, 200, 300)
2. Create `CharInterest` "WorldCamInterest.intr", parent to fake_cam via `SetTransParent()`
3. Merge, delete source
4. **Assert**: Interest's `TransParent()` is not null
5. **Assert**: Interest's `TransParent()` points to the target's copy of fake_cam
6. **Assert**: Interest's `WorldXfm().v` reflects parent position (~100, 200, 300)

#### Test 5: `SyntheticFileMergerFilterDoesNotSkipCharInterest`
Verify that `FileMerger::MergeAction` filter skips bones/spots but NOT CharInterest.

1. Create source with: CharInterest `"WorldCamInterest.intr"`, objects named `"spot_light01"`, `"bone_head"`
2. Merge into target
3. **Assert**: `"WorldCamInterest.intr"` IS in target
4. **Assert**: `"spot_light01"` is NOT in target (filtered by MergeAction)
5. **Assert**: `"bone_head"` is NOT in target (filtered by MergeAction)

### Tier 2: Real Asset Tests (requires MILO_LIB, GTEST_SKIP if unavailable)

#### Test 6: `RealVenueHasWorldCamInterestInSource`
Load each venue .milo_xbox, verify WorldCamInterest.intr exists before any merge.

#### Test 7: `RealVenueWorldCamInterestSurvivesProxyMerge`
Load venue, proxy-merge into worldRoot, verify WorldCamInterest.intr findable via `ObjDirItr<CharInterest>`.

#### Test 8: `RealVenueWorldCamInterestSurvivesNonProxyMerge`
Same as Test 7 but non-proxy path.

#### Test 9: `RealVenueInterestCollectionPattern`
Full replication of `SyncInterestObjects` pattern on a real venue (glitterati). Verify WorldCamInterest.intr is among collected interests.

#### Test 10: `RealVenueWorldCamInterestParent`
Load glitterati, merge, verify WorldCamInterest.intr's `TransParent()` is not null (should be parented to venue camera).

### What this test suite proves

When all tests pass:
1. `CharInterest` objects are not filtered by MergeDirs or FileMerger's merge filter
2. Both proxy and non-proxy merge paths preserve CharInterest objects
3. `ObjDirItr<CharInterest>` finds them after merge
4. The exact `SyncInterestObjects` collection pattern collects WorldCamInterest.intr
5. Transform parent chains survive merge (so camera-following works)
6. Real venue .milo files contain the expected WorldCamInterest.intr objects

If any test fails, it pinpoints exactly where in the pipeline the CharInterest is lost.

### Key reference files

| File | Role |
|------|------|
| `native/tests/test_merge_scope_parity.cpp` | Pattern source for merge test helpers |
| `native/tests/test_helpers.h` | EngineTestFixture, EnsureEngineInit |
| `src/system/hamobj/HamWardrobe.cpp:293` | SyncInterestObjects (the real collection code) |
| `src/system/char/Character.cpp:850` | FindInterestObjects |
| `src/system/char/FileMerger.cpp:395` | MergeAction filter |
| `src/system/obj/Utl.cpp:417` | MergeDirs |
| `src/system/obj/Utl.cpp:91` | MergeObject |
