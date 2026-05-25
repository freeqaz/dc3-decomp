# Unicorn Hostile-Mock Measurement Experiment

**Date**: 2026-05-14
**Phase**: 0 of the FP-reduction roadmap (`/home/free/.claude/plans/clever-humming-swing.md`)
**Status**: Complete — measurement gate triggers shelving of Phases 4, 7 and partial re-scoping of 6, 8.

## Goal

The roadmap hypothesized that ~233 of the 25,151 `EQUIVALENT/high` verdicts in `decomp.db` were structural false positives — functions where decomp and original both take a trivial early-exit path under zero-fill memory + zero-return mocks, despite asm match in the 50–80% band suggesting real divergence.

The proposed remedy (Phase 4 of the roadmap) was a one-byte trampoline-stub change from `li r3, 0; blr` to `li r3, -1; blr` that flips every `if (callFoo())` branch on both sides simultaneously. The Plan-agent review insisted on **measuring before building**: if the flip rate is low, the per-symbol-sentinel infrastructure (Phase 7) is over-engineered.

## Method

1. **Candidate sets** pulled from `decomp.db`:
   - **FP candidates** (n=116): `unicorn_verdict='EQUIVALENT' AND unicorn_confidence='high' AND current_percent BETWEEN 30 AND 85 AND size > 50` — the suspicious low-match-EQUIV band.
   - **TP holdout** (n=100): `current_percent >= 99 AND < 100 AND EQUIVALENT/high`, sampled uniformly across units — the codegen-noise band that should NOT flip.

2. **Worktree** at `/tmp/claude/unicorn-hostile-mock` with one change: `scripts/unicorn_runner/memory_map.py` `TRAMPOLINE_STUB` byte 3 changed from `0x00` to `0xFF` (i.e., `li r3, -1`). Symlinked `build/373307D9/src/` from the main repo so decomp .obj files were identical.

3. **Harness**: `cl_temp_files/phase0/run_experiment.py` ran each candidate once per repo with `zero fill, zero args, this=OBJECT_BASE`, reusing one `UnicornEngine` across the batch. Output written as JSON-lines for offline diff.

4. **Diff**: `cl_temp_files/phase0/analyze.py` compared verdicts pairwise per symbol. Pre-run pycaches were cleared on both sides — an early stale-cache scare nearly produced misleading "matched" stub bytes (logged in the harness).

## Results

| Set | n | Baseline EQUIV | Hostile EQUIV | Flipped | Flip rate |
|-----|---|----------------|---------------|---------|-----------|
| FP candidates | 116 | 112 | 112 | **0** | **0.0%** |
| TP holdout    | 100 | 96  | 96  | **0** | **0.0%** |

Zero verdict changes across either set. The flip rate is well below the 5% plan threshold.

### The hostile mock IS being used

Sanity-check evidence (`cl_temp_files/phase0/sanity_check.py`) confirms the hostile run is not a no-op:

- **Median hostile run is +8.2 ms slower** than baseline (mean +6.5 ms; max +28.1 ms).
- Top deltas: `Rand::Float` (+28 ms), `__partial_sort` (+24 ms), `RawAlloc` (+22 ms).
- 3-4x slowdown for ~10 functions indicates they enter deeper code paths under hostile mocks.
- Confirmed in-engine that the worktree's stub bytes at TRAMPOLINE_BASE are `38 60 FF FF 4E 80 00 20`.

### Candidate call density is non-trivial

`cl_temp_files/phase0/call_density.py` rules out "mocks don't matter because no calls":

- 85.2% of candidates have multiple REL24 calls (mean 11.3 calls/function).
- Only 8.7% had zero REL24 calls (those are leaf functions where the mock change is genuinely a no-op).
- Top candidates have 50–130 external calls — yet still EQUIVALENT in both runs.

## Interpretation

The hostile-mock change measurably alters control flow on both sides — but **symmetrically**. Decomp and original both flow into the new branches, both make the additional calls in the same order, both write the same memory. The hostile mock is a poor discriminator for the FP class we're worried about, because the decomp/orig pair share the same call graph and respond to mock returns identically.

This is a meaningful finding about what kinds of false positives the runner is structurally vulnerable to:

- **Asymmetric branching on call returns**: low probability. Both sides usually generate the same sequence of `bl` calls in the same order; flipping a return value moves them together.
- **Asymmetric branching on object/global memory contents**: not tested. This is what Phase 6 (sentinel-fill object memory) attacks — and remains unmeasured here.
- **Asymmetric null/wild-pointer access**: not tested. This is Phase 3 (unmapped-access fingerprint).
- **Cap-exhaustion masking divergence**: plausible. Several candidates run 3-4x longer under hostile mocks, suggesting they push toward the 50,000-instruction cap. If both sides truncate identically, divergence past the cap is invisible.

## Recommendation

Per the plan's gate (`fp_flip_pct < 5% → shelve Phases 6-9`), but with refinement based on which signal each phase targets:

### Shelve (confirmed low-yield against this FP class)
- **Phase 4** (hostile-mock probe run as standard schedule entry) — yields nothing on this corpus.
- **Phase 7** (per-symbol sentinel mocks via variable trampolines) — if uniform `-1` doesn't flip anything, per-callee sentinels won't either. The architectural cost (12-byte stubs, patcher.py refactor, vtable layout change) is not justified.

### Keep — these target different signals not measured here
- **Phase 0 deliverable**: this report.
- **Phase 1** (provenance & telemetry) — needed regardless; gives us the ability to re-measure as the signal evolves.
- **Phase 2** (cheap signal fixes) — **elevated priority** because:
  - Cap-exhaustion (2.2) is now an unverified-but-suggested cause of the symmetric-truncation FPs hinted at by the elapsed-time deltas.
  - Match-error PC sentinel check (2.1) is independently correct (Plan agent's "buried but possibly worth more than Phases 5-9 combined").
  - Size-mismatch guard (2.3) prevents the Reteleport-class bug.
  - Probe early-exit redesign (2.4) is structurally important regardless of FP rate.
  - MCP enum staleness fix (2.5) is an independent bugfix.
- **Phase 3** (unmapped-access fingerprint) — different signal (null/wild-pointer derefs), not exercised by hostile mocks. Keep.
- **Phase 6** (sentinel-fill object memory) — different signal (field-access offsets), the most likely place to surface remaining FPs. Keep.
- **Phase 8** (sentinel stack writes) — gated on Phase 6 producing sentinel-tagged values worth tracking.
- **Phase 5** (consumer tightening) — keep, but the threshold can be more conservative: the data here suggests EQUIV/high is more trustworthy than initially feared, so the demote-and-retest scope in Phase 5.3 may be smaller than planned (target shifts from "find FPs to rescue" to "find FPs the new signal layers expose").
- **Phase 9** (polish: f1 epsilon, r7-r10 capture, CR/XER) — ship opportunistically; low yield against the FP class but cheap to land.
- **Phase 10** (per-class vtable) — keep shelved.

### Updated roadmap order

1. Phase 1 (provenance) — needed before any other measurement.
2. Phase 2 (cheap signal fixes) — most likely to land actual wins given this result.
3. Phase 3 (unmapped fingerprint) — different signal axis.
4. Phase 6 (sentinel object memory) — different signal axis.
5. Re-measure with the new signal layers. If still <5% flip rate, accept the existing EQUIV/high pool as trustworthy and proceed to Phase 5.
6. Phase 5 (consumer tightening + demote-and-retest) — sized to whatever Phase 3+6 surfaces.
7. Phase 8 (sentinel stack writes) — only if Phase 6 produces enough sentinel-tagged writes to make this worthwhile.
8. Phase 9 (polish).

## Secondary finding: the existing EQUIV/high pool is more trustworthy than feared

The 233 "suspicious" functions in the 50-80% match band that prompted this whole roadmap appear to be largely real codegen-noise equivalents, not FPs. This is good news. The remaining FP-finding work should focus on **unfound** classes (null-deref masking, field-access offset bugs) rather than re-litigating the existing verdicts.

## Artifacts

- `cl_temp_files/phase0/fp_candidates.txt` — 116 FP candidates (pipe-delimited)
- `cl_temp_files/phase0/tp_holdout.txt` — 100 TP holdout (pipe-delimited)
- `cl_temp_files/phase0/baseline_{fp,tp}.jsonl` — baseline (`li r3, 0`) run results
- `cl_temp_files/phase0/hostile_{fp,tp}.jsonl` — hostile (`li r3, -1`) run results
- `cl_temp_files/phase0/run_experiment.py` — harness
- `cl_temp_files/phase0/analyze.py` — verdict diff
- `cl_temp_files/phase0/sanity_check.py` — elapsed-time delta proof
- `cl_temp_files/phase0/call_density.py` — REL24 counts per candidate
- Worktree at `/tmp/claude/unicorn-hostile-mock` — DELETABLE after the report lands.

The `cl_temp_files/phase0/` directory and worktree are scratch; the plan file should be the persistent reference.
