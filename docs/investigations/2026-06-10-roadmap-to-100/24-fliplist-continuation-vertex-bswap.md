# 24 — Flip-list Continuation + Vertex-Unpack Bswap (Wave-5 Lane C)

**Date:** 2026-06-11. **Lane:** Wave-5 C (flip-list continuation → adjudication +
fixes-with-tests; vertex-unpack byteswap fix).
**Worktree:** `/home/free/code/milohax/wt-wave5-c-fliplist-bswap` · **Branch (dc3):**
`wave5/c-fliplist-bswap`.
**Engine fix branch:** `wave5/vertex-unpack-bswap` in
`/home/free/code/milohax/milo-native-engine` (the real loader lives in the shared
engine; dc3 only carries an orphan mirror — see §2).
**Input flip list:** `data/unicorn_refresh_main_d5491b67.json` (main-plane,
331 flips), minus the 10 wave-4 dispositions in `22-fliplist-adjudication.md`.
**Build plane for all match%:** worktree `run_objdiff` (PPC `.obj`), `project_dir`
= the worktree above. Final certification is on `main` after sync.

---

## Headline

**Part 1 — flip-list (all 30 priority rows adjudicated, 0 real PPC bugs found):**
the two priority classes the plan named — `cap_exhausted_decomp` (19 rows in this
dataset) and `cap_exhausted_orig` (11 rows) — were adjudicated **row-by-row with
asm-grounded diagnosis (`run_diff_inspect mode=diagnose` on the worktree plane).**
**28 of 30 are `diff_op: none`** (no operation/branch/compare logic divergence) —
pure register-allocation / instruction-scheduling / FPR-coalescing / save-restore
fusion floors. **The 2 rows that *do* carry a `diff_op` (OnAddSink ble↔blt,
DingoJob::AddContent addi↔subi) were both run to ground as compiler artifacts, not
behavioral bugs** (a comparison-fusion artifact entangled in a regswap cascade, and
a `__FILE__`/MakeString template-instantiation length floor, respectively).

**Decisive finding (confirms and extends wave-4 doc 22):** `cap_exhausted` is a
**fixture artifact**, not a loop-logic-bug signal. The unicorn probe zero-fills /
0xCD-fills the object and passes zero args; functions that loop over a count read
from a filled member (`NumVerts()`, array sizes, ring lengths) run a *different*
number of iterations on the two sides **only because register-allocation differences
change which garbage register holds the count** — exhausting the instruction cap on
one side. The control-flow OPS are identical (`diff_op: none`), so realistic-input
behavior is bit-identical. `HasVert@PatchVerts` is the clean proof: its entire diff
is callee-saved-vs-volatile register allocation (r30/r31 spill vs r7/r4 keep); the
search-loop comparison logic matches exactly.

**Part 2 — MeshVertexLoading bswap (doc 21 Failure 4): FIXED + GREEN.** The native
compressed-vertex unpack path silently truncated every position to ~0. Root-fixed in
the shared engine loader (host-endian-agnostic raw big-endian word reads, no
x86-special-casing). **MeshVertexLoading 2 fails → 0 fails; 7/7 green** (5 pre-existing
+ 2 new regression tests). This also fixes **real rendering correctness** for every
compressed Xbox mesh (positions were collapsing to the origin in `MeshGpuCache`).

---

## Part 2 — Vertex-unpack byteswap (the real bug)

### Root cause

`VertexFormats::UnpackCompressedVertices` / `UnpackCompressedSkinnedVertices`
reinterpret-cast the raw on-disc Xbox blob as `CompressedVertex_Xbox*` and passed
its **members** to byte-swap helpers:

```cpp
static float UnpackFloat_BE(int bits) { ... __builtin_bswap32((unsigned)bits) ... }
...
const CompressedVertex_Xbox& cv = cverts[i];
gv.pos[0] = UnpackFloat_BE(cv.mPosX);   // <-- cv.mPosX is `float`
```

`cv.mPosX` is declared **`float`** in the struct. On disc it holds a big-endian
IEEE-754 word; reinterpreted as a native (LE) `float` it is a tiny denormal
(`1.0f` BE = `0x3F800000` → LE-read ≈ `4.6e-41`), and passing it to
`UnpackFloat_BE(int)` **implicitly converts that float to `int`, truncating it to
0**. Every compressed position decoded to 0. The integer fields (`mColor`,
`mNormal`, …) were fine — they are `int`/`unsigned int`, read as a native LE word,
then bswapped back to BE, which is correct.

Confirmed by the failing test output: `out.pos[0..2]` read `0` instead of `1/2/3`.

### Fix (engine `wave5/vertex-unpack-bswap`)

Stop type-punning the struct. Read every 32-bit field from the raw byte buffer as a
**host-endian word assembled MSB-first** (`LoadBE32`, byte-by-byte — correct on any
host endianness, **not x86-special-cased**) and feed the already-correct host word
to the unpack helpers (which no longer bswap). Per-field byte offsets are named
(`kCV_PosX … kCV_BoneWeight`, mirroring the struct) and the record stride is
`sizeof(CompressedVertex_Xbox)` (36). The web/WASM build shares this engine file, so
the fix is endian-correct generically.

`__builtin_bswap32` is fully removed from the compressed path (was 7 call sites).

### Test serializer bug (test-side)

`test_mesh_loading.cpp::SerializeCompressedVertexBE` had the *mirror* mistake:
`PutBE32(buf, (uint32_t)cv.mPosX)` — casting the float `1.0f` to integer `1` before
BE-serializing, so even a correct loader would read garbage positions. Fixed the
position fields to `PutBEFloat(buf, cv.mPosX)` (IEEE bits), keeping the integer-field
casts (those fields are genuine packed ints).

### Tests (all green)

- `MeshVertexLoading.CompressedSkinnedDecodePreservesBoneWeightsAndIndices` —
  was FAIL (pos read 0), now PASS.
- `MeshVertexLoading.CompressedSkinningMatchesCpuSkinningForSyntheticBones` — was
  FAIL, now PASS (the serializer fix).
- **NEW** `MeshVertexLoading.CompressedPositionDecodesBigEndianFloatBytes` — pins the
  raw big-endian byte→float decode directly with hand-crafted bytes
  (`-1.5f`/`256.0f`/`0.125f`), asserting the on-disc bytes are the BE IEEE pattern
  and that they decode exactly. Host-endian agnostic regression guard.
- **NEW** `MeshVertexLoading.CompressedDecodeWalksRecordStrideForMultipleVerts` —
  pins the per-record stride arithmetic (`offset = i * 36`) over a 2-vertex blob.

**Result: 7/7 MeshVertexLoading green** (was 5/7).

---

## Part 1 — flip-list adjudication (30 priority rows, all FALSE)

Method per row: `run_diff_inspect mode=diagnose` (worktree plane) → read the
`diff_op` count + root-cause histogram. `diff_op: none` = no logic divergence =
floor. Any `diff_op != none` was opened with full asm context and a source
experiment to confirm real-vs-artifact.

### cap_exhausted_decomp (19 rows — decomp side hit the cap; "real-bug" class label)

| Fn | Unit | norm% | diff_op | Verdict |
|---|---|---|---|---|
| `CharBonesSamples::Relativize` | char/CharBonesSamples | 97.2 | none | FALSE — FPR f28↔f29 + `__savefpr_28`/`__restfpr_28` inline-vs-call prologue floor |
| `CharDriverMidi::OnMidiParserGroup` | char/CharDriverMidi | 98.2 | none | FALSE — r28↔r29 GPR regswap floor |
| `CharLookAt::Highlight` | char/CharLookAt | 92.1 | none | FALSE — GPR regswap + scheduling |
| `FlowRun::OnTargetDirChange` | flow/FlowRun | 96.0 | none | FALSE — 1 regswap + 1 stack-slot, near-trivial floor |
| `DirectionGestureFilterDoubleUser::Update` | gesture/DirectionGestureFilter | 80.3 | none | FALSE — r28↔r30 callee-saved regswap cascade |
| `HamIKEffector::ComputeHandPullAndQuat` | hamobj/HamIKEffector | 94.2 | none | FALSE — FPR regswap + scheduling |
| `HamRegulate::Regulate` | hamobj/HamRegulate | 87.5 | none | FALSE — FPR f0↔f13 + offset-shift + fsel-scheduling floor |
| `RhythmBattlePlayer::OnReset` | hamobj/RhythmBattlePlayer | 95.9 | none | FALSE — store-scheduling + small regswap |
| `DirLoader::WriteTypeMemDump` | obj/DirLoader | 95.9 | none | FALSE — signed/unsigned cmp on swapped reg (addic./cmplwi) floor |
| `Hmx::Object::OnAddSink` | obj/Object | 97.5→96.1 | **1 (ble↔blt)** | FALSE — see §candidate A (comparison-fusion artifact in regswap cascade) |
| `RndMesh::SetVolume` | rndobj/Mesh | 92.3 | none | FALSE — +16 stack-shift cascade + FPR regswap |
| `RndTexRenderer::DrawToTexture` | rndobj/TexRenderer | 92.3 | none | FALSE — split-FMA (`fmadds` vs `fmuls`+`fadds`) + 75-pair FPR coalescing floor |
| `RndText::FontMap::AllocateMeshes` | rndobj/Text | 89.6 | none | FALSE — r30↔r31 GPR cascade + signed/unsigned cmp |
| `RndText::BuildFontMaps` | rndobj/Text | 90.8 | none | FALSE — 2 regswaps + deletes, near-trivial floor |
| `RndText::QueueBlacklightPacket` | rndobj/Text | 96.1 | none | FALSE — static-symbol reloc + 2 deletes |
| `RndTransAnim::MakeTransform` | rndobj/TransAnim | 98.4 | none | FALSE — FPR f0↔f12/f12↔f13 volatile-FPR floor |
| `FixVertOrder` | rndobj/Utl | 79.2 | none | FALSE — f0↔f12 + r10↔r11 cascade + ld/lfs scheduling |
| `op9` (ByteGrinder) | synth/ByteGrinder | 96.3 | none | FALSE — single regswap + 1 delete, near-trivial floor |
| `UIFontImporter::HandmadeFontChanged` | ui/UIFontImporter | 93.8 | none | FALSE — regswap + reloc + deletes |

### cap_exhausted_orig (11 rows — orig side hit the cap; labelled "unfixable artifact")

| Fn | Unit | norm% | diff_op | Verdict |
|---|---|---|---|---|
| `ClipPlayer::AnnotatePractice` | hamobj/ClipPlayer | 99.2 | none | FALSE — 2-instr load-pair reorder (TheHamDirector/TheLoadMgr) floor |
| `SongCollision::Update` | hamobj/SongCollision | 94.2 | none | FALSE — r28↔r29 + r15↔r16 cascade + string-reloc |
| `DingoJob::AddContent` | net/DingoJob | 79.1 | **1 (addi↔subi)** | FALSE — see §candidate B (`__FILE__`/MakeString length floor) |
| `SetSystemArgs` | os/System | 87.8 | none | FALSE — 27-instr GPR regswap + linker-label base-select floor |
| `PatchVerts::HasVert` | rndobj/Mesh | 81.4 | none | FALSE — callee-saved(r30/r31) vs volatile(r7/r4) allocation (clean proof) |
| `RndParticleSys::UpdateRelativeXfm` | rndobj/Part | 99.97 | none | FALSE — 4 stack-slot offset diffs only |
| `Rnd::TestPoint` | rndobj/Rnd | 98.6 | none | FALSE — r28↔r29 + load-scheduling |
| `WahEffect::Process` | synth/WahEffect | 92.7 | none | FALSE — FPR cascade + `stfsu`/`stfsx` loop-induction codegen floor |
| `UILabel::DrawShowing` | ui/UILabel | 95.9 | none | FALSE — instruction-scheduling reorder around `Style()` + signed/unsigned cmp |
| `MemMgr::MemPopTemp` | utl/MemMgr | 91.1 | none | FALSE — inline-vs-call tail (deletes) |
| `MemMgr::MemPushTemp` | utl/MemMgr | 83.3 | none | FALSE — inline-vs-call tail (4 deletes) |

### Candidate A — `Object::OnAddSink` (ble↔blt) — FALSE (artifact)

96.1%; idx 10 `cmpwi (3 vs 4)` + idx 11 `ble↔blt` + idx 12 extra `cmpwi 4`. Looked
like the `Size() >= 4` (line 802) vs `Size() > 3` comparison-style lever. **Tested
the rewrite `>= 4` → `> 3`: match REGRESSED 96.1% → 81.9%** (cascaded a 40-instr
bl↔mr replace cluster). Reverted. The `ble`/`blt` is how the compiler *fuses* the
adjacent `Size()>=4` (802) and `Size()>4` (803) range checks under the dominant
r21↔r22 / r23↔r24 register-swap cascade — a scheduling/fusion artifact, not a
source-spellable comparison bug. `Object.cpp` left **unchanged**.

### Candidate B — `DingoJob::AddContent` (addi↔subi) — FALSE (floor)

78.2%; idx 87 `addi r11,r11,0x1` vs `subi r11,r11,0x1`, surrounded by a 15-instr
insert/delete cascade. The Function-Call Diff shows the divergence is a **MakeString
template-instantiation mismatch**: target instantiates `MakeString<char[8],…>` while
base instantiates `MakeString<char[14],…>` / `<…,char[12]>`. The source line is
`_MemAllocTemp(size + 1, __FILE__, 0x6D, "", 0)` — **`__FILE__` expands to a
different-length absolute path** in the original Xbox build vs ours, changing the
`char[N]` template arg and the inlined string-pointer arithmetic (the `addi`/`subi`
±1). This is the known `__FILE__`/MakeString-length floor (memory:
`pattern_export_vs_handle` / call_arg-noise family) — **not source-fixable**.
`DingoJob.cpp` left **unchanged**.

---

## Rows adjudicated (count) and acceptance

- **30 of 30 priority rows adjudicated with asm evidence** (target was ≥15) — 19
  `cap_exhausted_decomp` + 11 `cap_exhausted_orig`. **All FALSE** (0 real PPC bugs):
  28 `diff_op: none` codegen floors + 2 `diff_op` rows run to ground as artifacts.
- **MeshVertexLoading green** (7/7; was 5/7) — the bswap fix with 2 new regression
  tests.
- No PPC source edits landed (the one experiment, OnAddSink `>3`, regressed and was
  reverted) → PPC match is **held** for every touched unit. The only source change is
  the **engine native loader** (host-only data path) + **test** edits.

## Recommendation for refresh_frontier.py auto-classification

The wave-4 doc-22 conclusion is reinforced and should be promoted to a mechanical
rule: **`cap_exhausted_decomp`/`cap_exhausted_orig` flips on a function whose objdiff
`diff_op == none` are fixture artifacts, not candidate bugs** (the loop trip-count
diverges only because register allocation moves a zero-filled count into a different
garbage register; the control-flow ops are identical). This single gate would have
auto-classified **30/30** rows here as artifacts, leaving the candidate set empty —
matching the asm reality. The current classifier routes all `cap_exhausted_*` to
`candidate_bug` (refresh_frontier.py:324-327), which over-reports by ~30 here.
Suggested: when applying a fresh refresh, down-rank `cap_exhausted_*` to
`fixture_artifact_caplimit` unless objdiff reports `diff_op > 0` AND the diff_op
survives a `>=`↔`>` / branch-polarity source experiment.

---

## Risks

- The vertex bswap fix lives in the **shared engine** (`milo-native-engine`
  `wave5/vertex-unpack-bswap`). dc3 also carries an **orphan mirror**
  (`native/src/gfx/VertexFormats.cpp`) that is **not in any build target** (the build
  compiles the engine copy via `libmilo-engine.a`). The mirror was kept byte-in-sync
  so it does not drift; the orchestrator must land the engine branch and bump
  `MILO_ENGINE_PIN` in `native/CMakeLists.txt` for the fix to take effect in CI/web.
- The `__FILE__`/MakeString floor (DingoJob) will keep re-appearing in any future
  flip-list as a `cap_exhausted_orig` row; it is honest/unfixable — do not chase it.
- `OnAddSink` stays a documented 96.1% regswap floor with a `ble`/`blt` fusion symptom
  — do not "fix" the comparison (proven to regress).
