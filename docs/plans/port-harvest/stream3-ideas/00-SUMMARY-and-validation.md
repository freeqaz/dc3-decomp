# Stream 3 ideas — consolidated findings + real-function validation (2026-05-31)

Three Opus planners investigated three candidate source levers for the both-stuck
bucket (~1,300 fns / ~495K bytes, where og-dc3 is also stuck so porting can't help;
og-dc3 must not seed synthesis). Each did design + a cheap binary-oracle experiment.
This doc records what was BUILT and the **end-to-end validation against real
both-stuck functions** (the ship test the individual idea docs scoped but did not run).

## The three levers and their verdicts

| Idea | Lever | In-vitro causation | Real both-stuck validation | Verdict |
|------|-------|-------------------|----------------------------|---------|
| 03 | **first-use order** (swap independent use-statements, decls fixed) | **YES** — `r27<->r28` (A1), `r29<->r31` (A2), independently reproduced | **1 applied+verified** (System_Xbox `GetSystemLanguage` +0.3% raw, run_objdiff-confirmed); 0 others of 12 (~150 variants) | Real mechanic, small yield on callee-saved-user-local shape; ship opt-in + targeted-scan |
| 02 | deep c2.dll live-memory IG read (diagnostic, not a transform) | n/a | classified real swaps: **≈0% initial-coloring, ≈100% coalescing/recoloring/const** | Explains why 01-GPR and 03 fail; no transform |
| 01 | **FPR float-decl reorder** (sequential f31-first allocator) | **YES** — Rot.cpp Multiply, FPR swap set shifts under float-only reorder | **1 applied+verified** (Rot::Multiply `qxqy<->vinx` **86.3→88.6%, +2.3%**, independently reproduced via MCP `run_objdiff --full-build`) | Real — biggest single win of the three; pattern needs brute-force re-architecture to auto-fire (see 01 doc CORRECTION) |

## first_use_reorder — built, verified in vitro; real-function validation IN PROGRESS

Built `decomp_synth/patterns/first_use_reorder.py` (opt-in). The first-use lever is
**genuine and independently verified**: swapping two independent use-statements with
declarations byte-identical flips GPRs in the controlled fixtures (`r29<->r31`,
reproduced by me via `diff-asm`, not taken on faith). Mechanism: c2.dll colors in
use-site order; declaration order only moves the symbol-ID tie-break (which fires
only when ops are equally ready) — so first-use is the dimension `declaration_reorder`
is structurally blind to.

**Applicability on real both-stuck functions: confirmed** (12–29 independent statement
pairs per function across the 11-function B_PERMUTER sample), so the pattern generates
real candidates.

**Scored sweep result: 0/12 functions got an applied improvement.** Ran
`first_use_reorder` (opt-in, `--no-apply`, full build + objdiff scoring) on 12 diverse
B_PERMUTER-tier both-stuck regswap functions. 10 built and scored real variants (~150
total, 7–24 per function); `math/Geo` generated 0 qualifying variants; `meta_ham/SongSort`
diagnosed as pure reloc noise (0 diff_ops / 0 GPR swaps). **No function reached the apply
threshold.** Best deltas seen:

- `os/System_Xbox` `GetSystemLanguage` (`r17<->r26`/`r17<->r20` callee-saved swaps):
  **+0.20%** — a genuine above-noise hit. Swapping the `jpn`/`swe` `static Symbol`
  declarations (`firstuse_7`) was **APPLIED to main and independently verified via
  `run_objdiff`** (clean HEAD-worktree baseline vs edited main, same metric):
  - baseline (HEAD): 97.4% normalized / 95.0% raw — 50 diff_arg, 32 REGISTER_SWAP insns
  - edited (jpn/swe): **97.5% normalized / 95.3% raw — 48 diff_arg, 30 REGISTER_SWAP insns**
  - the second independent +0.20% swap the sweep found (`firstuse_9`, dut/fin) does **NOT
    compose** — applying both yields the identical 95.3%/48/30, i.e. they resolve the same
    coloring decision. So `jpn/swe` alone captures the full win and is the only edit in main.
  This is exactly the predicted profile: a swap of initial-coloring-phase user variables.
  It confirms the lever *does* bite on the right-shaped function (callee-saved user-local
  swaps), cracking ~2 of the 32 swap insns — the residual r11<->r17 (16 insns) is the hard
  floor. **Actionable corollary: target functions whose swaps are callee-saved user-locals,
  not r3-r10 call-ABI swaps — those are the first_use-crackable shape.**
- `hamobj/CamShotCatVO` +0.03%, `char/CharClipDisplay` +0.01% — objdiff relocation jitter,
  below threshold.
- Everything else: negative, "same", or "No improvements found".

Units swept: CamShotCatVO, CharClip, LightPreset, CharClipDisplay, HamAudio, HamDirector,
StandardStream, Geo (0 variants — "not relevant"), SongSort (noise-only, nothing to
permute), DirLoader, System_Xbox (+0.20%).

This is a true scored result — the pattern *does* build and score real candidates (not a
no-op); they essentially never move the residual swaps. Caveat: clean per-function CLI
sweep, stale `.permuter.lock` files cleared between batches.

## Why — converging mechanistic explanation (idea 02, from live c2.dll memory)

The deep-instrumentation agent read c2.dll's interference-graph nodes at the coloring
breakpoints and classified a real both-stuck function's swaps by phase. On a real
function an aggressive source transform moved the **initial-coloring** phase by 603
BSF calls but left **coalescing (9/9) and recoloring (271/271) byte-identical** — and
the surviving swaps live in coalescing/recoloring (call-ABI argument registers,
synthesized constants). first-use and declaration order only reach the
initial-coloring phase, so they cannot touch the residual swaps. ≈0% of residual
both-stuck GPR swaps are source-controllable.

## Conclusion: the GPR both-stuck bucket is a genuine register-allocation floor

Three independent lines of evidence now converge:

1. `declaration_reorder`: 0/30 (2026-05-31 binary-oracle validation)
2. deep c2.dll live-memory phase classification: ≈0% initial-phase / ≈100% hard floor
   (coalescing/recoloring/synthesized-constant)
3. `first_use_reorder` scored on 12 real both-stuck regswap functions: 0/12 reached the
   apply threshold (~150 variants built+scored); the single above-noise hit was a +0.20%
   nudge on one initial-coloring-phase function — the lever bites on the rare right-shaped
   case but does not crack the bucket.

These are GPR-specific. The remaining live lever is **FPR** (idea 01) — a *different*
allocator (sequential, no coalescing layer) with measured causation; the rule is being
built and will be validated separately. Its honest ceiling is small (~1–4% of the
bucket), so it is a byte harvest, not a floor-breaker.

## What shipped to decomp-synth

- `patterns/first_use_reorder.py` (opt-in): applies the independent-statement-swap
  mechanic gated on `reg_swap_pairs` (the existing `statement_reorder` covers the same
  mechanic but `.relevant()` doesn't fire on pure regswaps). Kept opt-in because the
  yield on this bucket is ~0; enable with `--patterns first_use_reorder` for the rare
  function whose swap is genuinely initial-coloring-phase.
- `patterns/fpr_declaration_reorder.py` + `regmap_solver` Bug-A fix: pending (idea 01).

## Honest takeaway

We did not crack the GPR both-stuck floor — three independent lines of evidence now say
it is a real register-allocation floor (coalescing/recoloring/synthesized-constant,
not source-controllable). We *did* (a) discover + verify a real new lever (first-use)
that will help the rare initial-phase function, (b) prove via live compiler-memory why
the bucket is a floor, and (c) open the FPR sub-lever with measured causation. The
remaining legitimate yield is the small FPR harvest, not the 495K-byte GPR floor.
