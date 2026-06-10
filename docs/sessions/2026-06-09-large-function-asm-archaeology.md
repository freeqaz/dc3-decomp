# 2026-06-09 — Large-Function ASM Archaeology

Pushing the largest sub-100% functions forward with a mix of hand-decompilation,
background "asm-archaeology" agents, and the decomp-synth permuter. Companion to the
xenia/IK work in the other 2026-06-09 doc.

## Headline results (all on `main`)

| Function | Unit | Before | After | Driver | Commit |
|---|---|---|---|---|---|
| `MoveDir::UpdateOverlay` | hamobj/MoveDir | 60.7% | **86.1%** | agent deep-dive | `dbde6f3b` |
| `RndAmbientOcclusion::Tessellate` | rndobj/AmbientOcclusion | 63.8% | **87.3%** | agent deep-dive | `7a30ad8c` |
| `RndAmbientOcclusion::DistanceSH` | rndobj/AmbientOcclusion | 58.2% | **97.3%** | side effect of above | `7a30ad8c` |
| `BustAMovePanel::Poll` | lazer/game/BustAMovePanel | 87.6% | **92.5%** | hand + curated permuter | `f1ba7a97`, `c1fbb36d` |
| `StreamRenderer::DrawToTexture` | gesture/StreamRenderer | 85.6% | **86.5%** | hand + curated permuter | `7211c1b1`, `6842d29a` |
| `CSHA1::Transform` | math/SHA1 | 55.0% | **55.7%** | permuter only | `979aabcc` |
| `RndTexRenderer::DrawToTexture` | rndobj/TexRenderer | 91.6% | 91.6% | **refuted — at-limit** | — |

7 commits: `f1ba7a97` … `7a30ad8c`. decomp.db synced (`sync_match_percent.py --build --promote`).

## 10 real logic bugs fixed (target asm contradicted decomp behavior)

These matter for **native-port correctness**, not just match%:

1. **`std::sort(priEnd, priBegin)` reversed args** in Tessellate — face priorities were
   sorted over an inverted (empty) range, i.e. *never sorted*. Target instantiates
   `sort<Key<float>*>(begin, end)`.
2. **`hide_hud` sent via `Handle` instead of `Export`** in BustAMovePanel::Poll. `Handle`
   = `DataNode Handle(DataArray*, bool)` sret @ vtable+0x14; `Export` = `void Export(...)`
   @ vtable+0x38. Discarded-result sends are often really `Export`. **Per-site** — verify
   against the bctrl shape (RhythmBattle::OnBeat's 22 Handles are genuine).
3. **Wrong `GetScore` flag** (`true` vs target `false`) → `mCurrentMoveScore` computed wrong.
4. **`TheRnd.Width()` should be `TheRnd.YRatio()`** at the halfWidth + viz-rect sites in
   UpdateOverlay; the skeleton viz rect was wrongly square (`vizHeight=vizWidth`).
5. **`DrawDetectedBar(mirrored, false)` should be `(false, true)`** — never dims, always %.
6. **Second `DrawOverlayBar` plotted `mCurMoveSmoothers[0].Level()`** but target reads
   `mLastPollMs` (a 16ms-budget bar).
7. **Three wrong colors** in UpdateOverlay (threshold lines, overlay bars, bg-rect alpha).
8. **Ham1 detect-loop `colY` reset every node iteration** → all cells drawn at the same Y;
   target advances per node.
9. **`errColor` channels swapped**: target `(1-errFrac, errFrac, 0, 0.5)`.
10. **Fabricated debug strings**: SetUpWorkingMat / Tessellation-pass text reconstructed
    from `.rdata` byte length (0x50 vs the wrong 0x43).

Plus: **`StreamRenderer::SetUpWorkingMat`** was a one-line decomp stub that got inlined;
the target binary contains the real 84-byte anon-namespace function. Decoded its body from
target asm (`GetWork()` + `SetBlend(kBlendSrc)` + `SetZMode(kZModeDisable)` +
`SetTexWrap(kTexWrapClamp)`). It reports 0% only due to the anon-namespace-hash pairing
artifact, but the caller's `bl` + result registers now match.

## The winning technique: ASM-archaeology agents

The big unlock. A background `general-purpose` agent in its own worktree, given the right
prompt, took two 60%-class functions to 86–87% in ~50–65 min each (~400K tokens each) —
**far beyond what the permuter achieves on structural gaps** (permuter found +0.2–1.0 on
the same functions). Memory: [[feedback-asm-archaeology-agents]].

Why it works: big sub-90% functions are full of **decomp artifacts** that mark wrong source
shapes — `x/x`, `0.0f/denom`, `!(!x)`, `if (this)`, fabricated strings, channel-wise copy
hacks, cached accessor locals where the target re-derefs inline. An agent that reads target
asm region-by-region and rewrites the *natural* construct recovers huge chunks.

**Prompt recipe** (all ingredients required):
1. Orchestrator MCP tools (`run_objdiff`, `run_diff_inspect`) with an explicit
   "ALWAYS pass `project_dir=<worktree>` or your edits aren't tested" warning.
2. `scripts/analysis/diff_inspect.py --compare-asm --range N-M` usage + column/marker
   legend (left=TARGET, right=OURS; `-`=target-only/missing, `+`=ours-only/extra).
3. The raw target asm path: `build/373307D9/asm/<unit>.s` (search `.fn "<mangled>"`) —
   ground truth when compare-asm windows are noisy. (Note: system `objdump` can't read the
   MSVC-PPC COFF objects; use the `.s` listings instead.)
4. **Starting intel** from a parent-side `stack-layout` run — name the guilty slots/vars
   (SWAPPED pairs → decl reorder; TGT_ONLY = local we elide; the hot S=NN slot = a var the
   target spills every iteration).
5. The **lever list** (see below).
6. One-edit → `run_objdiff` → keep/revert discipline; no-hack constraints; MILO_ASSERT
   untouched; readable names.
7. Report format demanding final %, exact edit list, real logic bugs, refuted hypotheses.

**Validation/harvest after the agent returns:**
- `run_objdiff` in the worktree to confirm the final %.
- `scripts/measure_progress.sh --functions --detailed --current-dir <worktree> HEAD` to
  catch unit/header regressions. **Caveat:** functions changed in a commit the worktree
  *predates* show as false regressions (e.g. SHA1 looked −0.7 because the worktree branched
  before that commit). Verify suspicious rows against the actual baseline JSON.
- `git -C <worktree> diff > /tmp/x.patch; git apply /tmp/x.patch` onto main, re-run
  `run_objdiff` to confirm it reproduces, then commit.

## Lever catalog (validated this session + prior)

| Lever | Tell | Fix |
|---|---|---|
| **Export vs Handle** | discarded-result virtual call w/ insert/delete around bctrl; `Handle`=sret r3=&ret; `Export`=void r3=this | swap to `Export(Message("m",0), b)` — **per-site, verify asm** [[pattern-export-vs-handle]] |
| **Unnamed-temp ctor-return** | named local / implicit conv pins a stack slot (addi); explicit `f(Symbol("x"))` reuses ctor return (mr) | inline named single-use Symbol/Message locals [[pattern-unnamed-temp-ctor-return]] |
| **Inline vs cached accessor** | target has a shared temp-home slot stored many×; ours caches `GetGeomOwner()` base ptr | call `mesh->Faces()/Verts()` inline everywhere (Tessellate) |
| **Signedness** | `srawi` (ours) vs `extrwi`/`rlwinm` (target); byte tests `clrlwi 24` | unsigned var / `bool` locals (StreamRenderer streamFlags) |
| **Ternary-of-temps** | target constructs BOTH ctors back-to-back then pointer-selects | `X x = cond ? X(a) : X(b)` (replaces channel-wise hack) |
| **Decl reorder** | stack-layout SWAPPED pairs | reorder paired local declarations |
| **else-if vs two ifs** | branchless `xoris/addc/subfe` (ours) vs target `b` past 2nd compare | `else if` chain |
| **unsigned member field** | target stores 65535 not −1 into a short | `short` → `unsigned short` (Edge midpoints) |

## Permuter (decomp-synth) notes

- Single fn: `venv/bin/python -m decomp_synth.scan_and_permute --symbol '<mangled>'
  --max-rounds 10 --max-variants 100 --plateau-limit 3 --chain-depth 5`. Run in a worktree
  (`scripts/setup_worktree.sh`), sandbox disabled (BSF needs ptrace).
- Multi-symbol batch: repeat `--symbol` flags. Reports per-fn old→new + winning pattern.
- **Structural gaps are NOT its strength** — it found only +0.2–1.0 on functions where the
  agents found +20+. Use it for the *residue* after hand/agent work, or near-100 cleanup.
- **Curate every win.** A `nullins` win fabricated a `mCam &&` null-check in StreamRenderer
  that scored +0.1 by alignment luck — the target asm has NO such test (mCam only loaded for
  SetTargetTex/Select). **Rule: verify every null_guard_insert/nullins win against target
  asm before landing.** Also: de-noising a win (renaming temps, unsplitting casts) can
  silently drop the gain — re-measure after cosmetic cleanup; if the gain only survives with
  the noise verbatim, reject it. [[sweep-harvest-curation]]
- SHA1's +0.7 was real (decl/store reorder chains); its residue is a whole-function
  scheduling/regalloc cascade across the 80 unrolled rounds — likely a floor.

## Future candidates (by leverage)

**Highest leverage — same-unit callees that block already-improved functions:**
- `RndAmbientOcclusion::BlendVert` (56%, 0x... static) — wrong volatile clobber mask (ours
  f3–f5, target r7–r10; target implies `Set()`-style grouped loads, all reads before writes
  per field group). **Drives Tessellate's residual r10/r11 cascade** → fixing it lifts two
  functions. Best next target.
- `RndAmbientOcclusion::BurnTransform` (43%), `SmoothResults` (63%) — same TU, same
  accessor-inline + grouped-load idioms that worked on Tessellate should transfer.

**Big structural gaps (agent-shaped, 40–80%, the sweet spot proven this session):**
- `Invert(Matrix4&, Matrix4&)` (2800B, 67.3%) — math, likely cofactor/adjugate expansion
  with a different temp shape; high-value (shared helper).
- `ArcDetector::UpdateOverlay` (2160B, 64.5%) — sibling of MoveDir::UpdateOverlay; the same
  inline-expression + YRatio + color-literal levers likely apply directly.
- `OnComputeCharWidths@RndText` (2116B, 68.2%), `ResetNormals` (1980B, 67.5%),
  `HamSkeletonConverter::Set` (1928B, 73.6%).
- `Spotlight::BuildBeam` / `BuildNGCone` / `RenderConeDefs@NgSpotlightDrawer` (66–67%) —
  cluster of related beam-geometry functions; one root cause may span them.
- `ConstructMesh@HamRibbon` (53.5%), `UpdateChase@RndRibbon`/`@HamRibbon` (72–74%) — ribbon
  cluster.
- `CallCheatScript@CheatsManager` (55.4%), `BuildVisit`/`MakeBSPTree` BSP cluster (66–77%).

**Big near-misses (permuter / small-edit territory, structural already mostly there):**
- `RhythmBattle::OnBeat` (16.5KB, 96.6%) — **confirmed at-limit this session** (Export swap
  regressed it, full permuter found +0.01). Leave it.
- `BustAMovePanel::OnBeat` (12KB, 97.5%), `SetState@SaveLoadManager` (99.05%),
  `json_tokener_parse_ex` (94.35%), `Spotlight::SyncProperty` (99.8%).

## Tools used / reference

- **Orchestrator MCP**: `run_objdiff` (source of truth %), `run_diff_inspect` modes
  `stack-layout` (slot diff + CodeView var names — the decisive tool), `attributed`
  (mismatch regions → source lines), `clusters`, `mismatches`, `diagnose`, `asm_listing`.
- `scripts/analysis/diff_inspect.py --compare-asm --range N-M --project-dir <dir>` — the
  side-by-side workhorse.
- `build/373307D9/asm/<unit>.s` — raw disassembled target (ground truth).
- `scripts/setup_worktree.sh <path> <branch>` — agent/permuter isolation.
- `scripts/measure_progress.sh --current-dir <wt> HEAD` — regression sweep.
- `scripts/sync_match_percent.py --build --promote` — rebuild report.json + DB.
- og-dc3-decomp sibling (`../og-dc3-decomp`) has full `.s` listings too — cross-ref source.

## Related memory
[[feedback-asm-archaeology-agents]] · [[pattern-export-vs-handle]] ·
[[pattern-unnamed-temp-ctor-return]] · [[sweep-harvest-curation]] ·
[[reference_og_dc3_decomp]]
