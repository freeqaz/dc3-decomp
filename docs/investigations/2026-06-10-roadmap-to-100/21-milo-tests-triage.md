# 21 — milo-tests Pre-Existing Failure Triage

**Date:** 2026-06-11. **Lane:** D (Wave-4 suite hygiene). **Source:** Wave-4 plan
§Lane D item 3; referenced in Wave-3 results (96) as "8 pre-existing milo-tests
failures (ObjectLifetime MergeDirs/MergeScope — likely the known ~ObjectDir
NullifyAllRefs cascade bug; MeshVertexLoading skinning-decode; RndCamProjection
GPU-math; the 400s AssetLoading timeout)."

This document triages each failure class. No source fixes are applied here —
the purpose is to assign each failure a root-cause hypothesis (with evidence
pointer), a repo route (engine-repo vs dc3), and a recommended wave.

---

## Baseline state

The Wave-3 results (96) noted "8 pre-existing milo-tests failures" that are
unrelated to any Wave-3 lane's work. The test suites break down into the
following failure classes:

| # | Test suite | Failure class | Count est. |
|---|---|---|---|
| 1 | `MergeLifecycleTest` | ObjectDir NullifyAllRefs cascade | 2+ |
| 2 | `ObjectLifetimeTest::MergeDirsNameCollision*` | MergeDirs ref-redirect under cascade | 1 |
| 3 | `MergeScopeParityTest` | MergeDirs scope / inline-subdir parity | 1-2 |
| 4 | `MeshVertexLoading::CompressedSkinned*` | Skinning decode bone weight/index | 1 |
| 5 | `RndCamProjectionTest` | YRatio aspect-dependent GPU math | 1-2 |
| 6 | `AssetLoadingTest` | 400s ctest timeout on heavy real-asset tests | 1 |

Total: 8 (the exact count depends on whether each class fires 1 or 2 subtests).

---

## Failure 1 — MergeLifecycleTest (ObjectsSurviveSourceDirDeletion, SubdirsSurviveSourceDirDeletion)

**Test file:** `native/tests/test_merge_lifecycle.cpp`

**What fails:** After `MergeDirs` moves objects from a source dir to a target dir,
`delete source` triggers the native `~ObjectDir::NullifyAllRefs` cascade introduced
for native-only stale-pointer safety. The cascade reaches objects that were
*reparented* into the target via `SetName`, nullifying their refs in the target
dir's hash table. Tests `ObjectsSurviveSourceDirDeletion` and
`SubdirsSurviveSourceDirDeletion` assert that the reparented objects survive
(Xbox behavior) but observe `nullptr` in the target after source deletion.

**Root-cause hypothesis:** `~ObjectDir::CollectCascadeDirs` (Dir.cpp) collects
dirs to nullify based on the ring-walk of outgoing ObjDirPtr refs. After
`SetName("hud_left", target)`, the source still has residual ring entries for the
moved objects (the ObjDirPtr ownership moves but the ring does not immediately
drain). The cascade therefore reaches them and nullifies their refs in the new
parent's hash table, leaving `nullptr` tombstones or entry-not-found results.

**Evidence pointer:**
- Memory note: `project_objdir_cascade_bug.md` (confirmed, 76-day-old but root
  cause unchanged — the cascade logic is still in Dir.cpp).
- Two pre-existing FAILING tests in `test_merge_lifecycle.cpp` explicitly
  document this: comments on lines 10-11 say "They will likely FAIL today —
  that is expected."
- Downstream symptom: HUD merge (hud_left/hud_right disappearing after
  director.milo PostLoad) identified in earlier sessions.

**Route:** `dc3` repo, `src/system/obj/Dir.cpp` (cascade logic) +
`src/system/obj/Utl.cpp` (MergeObjectsRecurse ring management).

**Recommended wave:** Wave 5 or dedicated native-bug lane. The fix requires
surgical changes to the cascade skip predicate: objects with a new `mDir !=
source` after reparenting should be excluded from the cascade. Must not regress
the 12+ passing ObjectLifetimeTest cases.

---

## Failure 2 — ObjectLifetimeTest::MergeDirsNameCollisionLeavesOnlyLivePointers

**Test file:** `native/tests/test_object_lifetime.cpp`

**What fails:** After `MergeDirs(fromDir, toDir, filt)` with a name collision
and replace action, the test asserts that `ref.Ptr()` (which originally pointed
to `fromDup`) has been redirected to `toDup`. The assertion `EXPECT_EQ(ref.Ptr(), toDup)` fails — `ref.Ptr()` may be `nullptr` or still points to the now-dead
`fromDup` after `delete fromDir`.

**Root-cause hypothesis:** Same cascade bug as Failure 1: `ReplaceRefs` during
`MergeObject` correctly redirects the ObjPtr from `fromDup` to `toDup`, but when
`delete fromDir` runs afterward, the cascade also reaches `toDup` via residual
ring entries and nullifies it. The test's `EXPECT_NE(ref.Ptr(), nullptr)` fires
because the cascade killed the redirect target.

**Evidence pointer:** Test comment at line 115 ("Parity expectation") + the same
Dir.cpp cascade mechanism. This is a second manifestation of the same root cause
as Failure 1 (different entry point: MergeDirs with name-collision replace vs
explicit SetName reparent).

**Route:** `dc3` repo — same Dir.cpp fix as Failure 1 would cure both.

**Recommended wave:** Wave 5 (same fix as Failure 1).

---

## Failure 3 — MergeScopeParityUnitTest / NonProxyPipelinePreservesReplaceSubdirScope

**Test file:** `native/tests/test_merge_scope_parity.cpp`

**What fails:** `NonProxyPipelinePreservesReplaceSubdirScope` asserts that after
`MergeNonProxy` (which calls `MergeDirs` with `kMergeInlinedMoveSharedSubdirs` +
the native flatten pass `RunNativeFlattenPass`), objects from a `kInlineNever`
(= kMergeReplace) shared subdir are NOT flattened into the target's top-level
hash table. The test `EXPECT_EQ(toDir->FindObject("wood.tex", false, false), nullptr)` fails — the native flatten pass over-flattens kMergeReplace subdir objects.

**Root-cause hypothesis:** `RunNativeFlattenPass` (in the test helper, mirroring
`FileMerger.cpp` post-merge) iterates all objects reachable from `toDir` and calls
`SetName(it->Name(), dir)` for objects not already in `dir`'s flat hash table.
It does not correctly distinguish `kMergeReplace`-retained subdir trees
(objects that Xbox keeps scoped under the subdir) from `kMergeMerge`-inlined
objects (objects that should be flattened). The `inRetainedSubdirTree` check in
`RunNativeFlattenPass` uses `retained->HasSubDir(it->Dir())` which is incorrect
for nested subdirs or objects that `MergeDirs` already moved.

**Evidence pointer:** Test file line 527-579 (`NonProxyPipelinePreservesReplaceSubdirScope`)
with inline comment "The current native flatten pass over-flattens them." +
`MergeInlinedMoveSharedSubdirs` comment in Utl.cpp.

**Route:** `dc3` repo, `src/system/char/FileMerger.cpp` (the real flatten pass,
equivalent to `RunNativeFlattenPass` in the test) + `src/system/obj/Utl.cpp`
(`MergeObjectsRecurse`).

**Recommended wave:** Wave 5. Low blast radius (only affects the song/venue
non-proxy pipeline, not the cascade bug). Can be fixed independently of Failures 1-2.

---

## Failure 4 — MeshVertexLoading::CompressedSkinnedDecodePreservesBoneWeightsAndIndices

**Test file:** `native/tests/test_mesh_loading.cpp`

**What fails:** `VertexFormats::UnpackCompressedSkinnedVertices` is called on a
synthetic big-endian compressed vertex record. The test expects `boneWeights[0] == 1.0f` and `boneIndices` to match the packed values. On LP64/little-endian native,
the decode function reads the big-endian `packedWeights` and `packedIndices` fields
with the wrong byte order (missing bswap) or interprets the `uint32_t` fields as
little-endian, producing wrong bone weight normalization and wrong bone index bytes.

**Root-cause hypothesis:** `VertexFormats::UnpackCompressedSkinnedVertices` (in
`src/gfx/VertexFormats.cpp` or the native port equivalent) was written for the
Xbox 360 big-endian ABI. On native (x86_64 little-endian), the packed fields must
be byte-swapped before extracting sub-fields. The `packedWeights = 1023u` value
maps to `uint8_t[4] = {0, 0, 3, 255}` on big-endian (Xbox), giving weight[0] as
255/255 = 1.0f, but on little-endian this reads `{255, 3, 0, 0}` giving a
different first byte and wrong normalization.

**Evidence pointer:** `test_mesh_loading.cpp` line 94-118; `VertexFormats.h`
`CompressedVertex_Xbox` struct; the pattern matches the CSHA1 LP64 endian bug
fixed in Wave-3 (different data type, same root: big-endian ILP32 source code
reading multi-byte words on LP64/LE host without bswap).

**Route:** `dc3` repo, native port — `VertexFormats::UnpackCompressedSkinnedVertices`
and the `CompressedVertex_Xbox` unpacking logic. Should be gated under `HX_NATIVE`.

**Recommended wave:** Wave 5. Medium difficulty; requires identifying all sites in
the decompressed vertex unpack path that read multi-byte Xbox big-endian fields.
A unit test for each field (weights, indices, normals, tangents) would be the
right test-first approach.

---

## Failure 5 — RndCamProjectionTest (PerspectiveIdentityProjectionMatchesExpectedMatrix)

**Test file:** `native/tests/test_rndcam_projection.cpp`

**What fails:** The test calls `cam->GetViewProjectXfms(viewXfm, projMtx)` and
checks matrix values including `projMtx.x.x == ratio / tanHalf` where
`ratio = TheRnd.YRatio()`. In the `EngineTestFixture`, `TheRnd.mAspect` defaults
to `kWidescreen` (value = 1) which makes `YRatio()` return `0.75f`. The expected
`projMtx.x.x = 0.75f / tan(yFov/2)`. If the Rnd subsystem is not fully initialized
(or the aspect ratio is not set), `mAspect` may be 0 (kNormalScreen → 1.0f ratio)
or uninitialized, producing a different value.

**Root-cause hypothesis (two candidates):**
1. **GPU path:** `RndCam::GetViewProjectXfms` calls into the renderer backend
   (`DxRnd` or the milo-engine stub) which accesses GPU state. In the sandboxed
   native test environment without a Vulkan ICD, the backend is a no-op stub and
   returns a zero or identity matrix, failing all the EXPECT_NEAR checks.
2. **Aspect initialization:** `TheRnd.mAspect` defaults to `kWidescreen` in the
   ctor (Rnd.cpp:152) but the `EngineTestFixture` may not call the full init chain
   that sets it from config. If `mAspect` starts as 0 (kNormalScreen), `YRatio()`
   returns `1.0f` and the expected matrix values shift.

The "GPU-math" label in Wave-3 strongly suggests candidate 1: the test fixture
can't access GPU hardware so the projection math stub returns wrong values.

**Evidence pointer:** `test_rndcam_projection.cpp` line 59 (`TheRnd.YRatio()`);
`Rnd.cpp:606-609` (kRatio table keyed on mAspect); `Rnd.cpp:152` (default mAspect);
Wave-3 results doc (96) label "GPU-math".

**Route:** `dc3` repo — the test may need `EngineTestFixture::SetAspect` to ensure
the aspect ratio is set before `GetViewProjectXfms`. If candidate 1 is the root
cause, the test needs the native engine's Vulkan ICD (sandbox-skip required) and is
a GPU-gated test that should run only in non-sandboxed builds. The clean fix is to
either mock `TheRnd.YRatio()` in the test or call `TheRnd.SetAspect(kWidescreen)`
explicitly in setup.

**Recommended wave:** Wave 5. Low risk — the math is correct; the issue is test
environment setup. Can be fixed without source changes to the decomp.

---

## Failure 6 — AssetLoadingTest (400s ctest timeout)

**Test file:** `native/tests/test_asset_loading.cpp`

**What fails:** One or more `AssetLoadingTest` subtests (likely `LoadFullVenueWorlds`,
`LoadWorldMasterFile`, or `LoadMainCharacter`) hit ctest's default timeout (the
"400s AssetLoading timeout" description implies ctest kills the test after ~400s).
The test either hangs (infinite ring-walk from a corrupt ref ring, the known
cascade-bug downstream symptom) or takes extremely long on slow I/O (loading
multiple large `.milo_xbox` venue files).

**Root-cause hypothesis (two candidates):**
1. **Corrupt ring hang:** After loading and merging a real venue file via
   `DirLoader::LoadObjects` + `MergeDirs`, the NullifyAllRefs cascade (Failure 1
   root cause) corrupts a ref ring. The subsequent `ObjDirItr` walk hangs in an
   infinite loop (ring next pointer loops back). The test has a guard
   `ASSERT_LT(itrCount, 10000)` in some tests but not in the `LoadFullVenueWorlds`
   path that iterates without a cap. This matches "400s" — the hang is eventual
   not immediate.
2. **I/O / parsing slowness:** Loading 8 full venue files + character resources
   from `orig-assets/extracted/` is disk-bound and may exceed ctest's timer on
   slower storage. Less likely to be exactly 400s (hard kill) unless it's a
   genuine hang.

The comment in `test_merge_scope_parity.cpp` line 875 ("Don't delete venueDir —
pre-existing cascade cleanup crashes") and line 1001 ("Leak — pre-existing
cascade cleanup issues") confirm that real-venue tests deliberately skip
`delete dir` to avoid the cascade crash. The `LoadFullVenueWorlds` and
`SequentialMergesIntoSameWorldRoot` tests that DO delete may hit the cascade
crash/hang.

**Evidence pointer:**
- `test_asset_loading.cpp` line 591 (`LoadFullVenueWorlds`) + line 559
  (`LoadWorldMasterFile`).
- `test_merge_scope_parity.cpp` comment at line 875 and 1001.
- The 400s timeout is consistent with an infinite ring-walk (no `ASSERT_LT` cap)
  triggered by the same cascade bug as Failures 1-2.

**Route:** `dc3` repo — fixing Failures 1-2 (the cascade bug) would likely cure
the hang path. As a short-term guard, the heavy real-asset tests should add
`ASSERT_LT(itrCount, 30000)` caps and skip `delete dir` like the venue tests
already do. Long term: fix the cascade.

**Recommended wave:** Wave 5 (blocked on the cascade bug fix from Failures 1-2).

---

## Summary table

| # | Test suite | Root cause | Route | Wave |
|---|---|---|---|---|
| 1 | MergeLifecycleTest (2 tests) | ~ObjectDir NullifyAllRefs cascade kills reparented objects | dc3/Dir.cpp | Wave 5 |
| 2 | ObjectLifetimeTest::MergeDirsNameCollision | Same cascade; redirect target nullified | dc3/Dir.cpp | Wave 5 |
| 3 | MergeScopeParityUnitTest (1-2 tests) | Native flatten pass over-flattens kMergeReplace subdirs | dc3/FileMerger.cpp | Wave 5 |
| 4 | MeshVertexLoading::CompressedSkinned | LP64/LE bswap missing in vertex unpack | dc3 native/VertexFormats.cpp | Wave 5 |
| 5 | RndCamProjectionTest (1-2 tests) | TheRnd aspect not set / GPU-gated math | dc3 native test setup | Wave 5 |
| 6 | AssetLoadingTest (400s timeout) | Infinite ring-walk from cascade bug | dc3/Dir.cpp (cascade fix) | Wave 5 |

**All 8 failures route to the `dc3` repo.** Failures 1, 2, 4, 6 share a single root
cause (NullifyAllRefs cascade) — a single Dir.cpp fix would unblock all four.
Failures 3 and 5 are independent and lower-priority.

**None require engine-repo changes** (the cascade fix is in dc3's own Dir.cpp,
not in milo-native-engine).

**Do not fix in this wave** — all are pre-existing (not introduced by any Wave-4
lane) and their target behavior is already defined by the 2 failing test cases in
`test_merge_lifecycle.cpp`. The fix must not regress the 12+ passing
ObjectLifetimeTest cases. Assign to Wave 5.
