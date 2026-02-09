# RhythmBattle::OnBeat Differential Runtime Validation Ideas (2026-02-08)

## Why This

`RhythmBattle::OnBeat` is currently `93.3%` match and likely at practical codegen limit.  
The open question is behavioral equivalence: does decomp `OnBeat` produce the same gameplay state transitions as the original binary?

This doc proposes concrete ways to answer that with runtime evidence.

## Target Function Facts

- Symbol: `?OnBeat@RhythmBattle@@AAAXXZ`
- Original address: `0x824E0B40` (from `orig/373307D9/ham_xbox_r.map`)
- Source location: `src/system/hamobj/RhythmBattle.cpp:683`

## Validation Goal

For the same starting state and same inputs, compare original-vs-decomp execution and detect first semantic divergence.

Success criterion:
- No divergence in selected state vector for N consecutive `OnBeat` calls across representative scenarios.

## What To Compare

Use values that are externally meaningful and available from object memory (not compiler-local temporaries).

### RhythmBattle state (from `this`)

- `mFinale`, `mActive`, `unkfc`, `unkfd`, `unkfe`, `unkff`, `unk100`, `unk101`, `unk102`
- `mStartBeat`, `mEndBeat`, `unk114`, `unk118`
- `unk124`, `unk128`, `unk140`, `unk144`, `unk148`, `unk14c`
- `mLeader`
- pointers: `mPlayerOne`, `mPlayerTwo`

### Per-player state (for both players)

- `mInTheZone` / `InTheZone()`
- `unk260`, `unk26c`, `unk280`, `unk284`
- `unk2a4`, `unk2a5`

### Optional event-level signals (higher confidence, higher effort)

- Symbols/messages sent via `focusPanel->HandleType(...)`
- Symbols/messages sent via `TheHamProvider->Handle(...)`
- calls to `SetActive`, `SetInTheZone`, `SetWindow`, `SwagJacked`, `SwagJackedBonus`

## Proposed Approaches

## 1) MVP: Emulator breakpoint probe + memory snapshot diff (recommended first)

Idea:
- Run original and decomp-patched binaries in emulator under identical scenario.
- Break on `OnBeat` entry and function return.
- Read memory fields from `RhythmBattle` and both `RhythmBattlePlayer` objects.
- Write canonical JSONL records per call (`entry` + `exit`).
- Compare traces offline.

Why this is good:
- No invasive source edits required.
- Works directly against the real binary behavior.
- Compares semantic state rather than assembly form.

Requirements:
- Reliable way to run both binaries (original and patched build) in same emulator workflow.
- Scriptable breakpoint/read-memory API (or emulator fork tooling).
- Stable pointer discovery for the live `RhythmBattle` instance.

Key implementation notes:
- At function entry, capture `this` from `r3` (PowerPC `thiscall` convention).
- At function return, capture by same `this` pointer.
- Normalize floats with epsilon and ignore pointer identity in diffs (except null/non-null and stable binding checks).

## `.obj` + MSVC-Specific Ideas

Because this repo already produces MSVC/Xbox360 `.obj` files per TU, we can use them directly to make validation more systematic.

### A) Auto-generate probe plans from `.obj` + symbols

Inputs:
- `build/373307D9/src/system/hamobj/RhythmBattle.obj` (decomp object)
- `orig/373307D9/ham_xbox_r.map` + `config/373307D9/symbols.txt`

Use:
- Build a script that extracts:
  - function entry address (`OnBeat`: `0x824E0B40` in original),
  - top outbound callees from this function,
  - static-local ctor helper symbols tied to `OnBeat`.
- Emit a probe plan JSON:
  - entry/exit breakpoint addresses,
  - optional callee breakpoints for event-level call counting.

Benefit:
- Repeatable instrumentation setup, less manual debugger work.

### B) Call-sequence differential without replacing code

Instead of immediately trying to run a patched binary:
- Keep original binary running.
- Compare two traces from two binaries (when patched binary path exists), or compare against expected “decomp-derived call envelope” first.
- Track ordered calls/call-counts for high-signal functions:
  - `SetInTheZone`, `SetActive`, `SetWindow`,
  - `SwagJacked`, `SwagJackedBonus`,
  - provider/panel handle paths.

Benefit:
- Strong semantic signal even when internal locals differ.

### C) Function-level replacement concepts (if patched run is required)

If we need true original-vs-decomp runtime A/B and there is no full relink target today:
- Option C1: function trampoline patch at `0x824E0B40` to alternate implementation.
- Option C2: binary patch workflow that replaces just `RhythmBattle.obj` contributions.

Both require additional tooling and verification, but `.obj` artifacts reduce ambiguity:
- MSVC mangled symbols are stable.
- section/relocation info in `.obj` gives precise dependency map.

Important:
- Current ninja graph appears compile+diff oriented (no explicit final relink target), so this is likely a tooling project, not a quick script.

## 2) Source-instrumented semantic trace (decomp side only, fast to build)

Idea:
- Add a `#ifdef DC3_DIFF_TRACE` logger in `OnBeat` (entry/exit) that dumps the same state vector.
- Use this as a cheap harness while building the emulator-side probe.

Why useful:
- Rapidly validates trace schema and log format.
- Lets us test comparator tooling before emulator integration is complete.

Caveat:
- Alone, this does not prove original parity; it only prepares the pipeline.

## 3) Deep call/event differential (highest confidence, highest cost)

Idea:
- In addition to state snapshots, capture ordered event stream:
  - provider/focus-panel messages (symbol names + key args),
  - critical gameplay calls (`SetInTheZone`, `SetActive`, etc.).
- Compare both state and event order.

Why useful:
- Catches “same final state but different transient behavior” issues.

Caveat:
- Significantly more engineering and hooking complexity.

## Where This Fits In Decomp Lifecycle

Dynamic analysis should be treated as a semantic validation layer, not a replacement for decompilation or codegen matching tools.

Practical stack:
1. `ghidra/m2c`: recover candidate logic and structure.
2. Dynamic analysis: validate runtime behavior against original binary.
3. `objdiff`/permuter: tune source/codegen for assembly convergence.

### Why It Is Valuable

- It separates semantic bugs from codegen noise.
- It reduces false confidence from high match percentages with residual behavioral uncertainty.
- It gives a concrete stop/go signal before declaring difficult functions `AT_LIMIT`.
- It catches regressions when source-shape changes are made for assembly matching.

### ROI By Match Stage

1. `0-30%` (from scratch): low-to-medium ROI.
- Useful when static analysis cannot explain control-flow/state behavior.
- Usually too expensive as a default first step.

2. `30-70%`: medium ROI.
- Good for validating state-machine direction and major branch semantics.
- Prevents investing in matching the wrong logic.

3. `70-95%`: highest ROI.
- Best point to detect subtle semantic drift before heavy asm tuning.
- Especially useful for large gameplay/stateful functions.

4. `95-100%`: targeted ROI.
- Helps decide whether remaining diffs are semantic or compiler-driven.
- Supports confident `AT_LIMIT` decisions.

### Recommended Use Policy

Use dynamic analysis first for:
- Large branch-heavy gameplay functions.
- Functions with persistent control-flow/call-count mismatches near high match%.
- Functions being considered for `AT_LIMIT` with unresolved semantic doubt.

## Determinism Plan

Differential validation is only credible if runs are deterministic enough.

Minimum controls:
- Same song/chart/mode setup.
- Same initial save state or checkpoint.
- Same input stream (or neutralized input path).
- Fixed timing cadence for beat advancement (or replay from save-state just before beat boundary).

Practical strategy:
- Build a small scenario matrix (e.g. non-finale, finale, mind-control transitions, swag-jack path).
- For each scenario, create a “pre-OnBeat” save-state and replay a fixed number of beats.

## Trace Format (proposal)

One JSON object per event:
- `run_id`
- `binary_kind` (`orig` or `decomp`)
- `beat_index`
- `phase` (`entry` or `exit`)
- `this_ptr`
- `state`: canonical key/value map for RhythmBattle + both players
- `timestamp_us` (optional)

Comparator output:
- first mismatch location (`beat_index`, `phase`, `field`)
- expected vs actual
- previous 1-2 records of context

## Suggested Implementation Phases

1. Define schema + comparator first.
- Implement `compare_onbeat_traces.py` with epsilon handling and allowlists.

2. Implement decomp-side trace emission behind `DC3_DIFF_TRACE`.
- Use this to harden schema and tooling.

3. Implement emulator probe for original binary at `0x824E0B40`.
- Capture entry/exit snapshots into same schema.

4. Add decomp-patched binary probe run.
- Produce paired traces and compare.

5. Expand scenario matrix and add event-level stream if needed.

## Risks / Unknowns

- Emulator automation API limitations for breakpoints/memory reads.
- Availability of robust “patched binary” workflow for side-by-side runs.
- Non-determinism from timing/input/async systems causing false positives.
- Internal pointer churn requiring stable identity mapping in comparator.

## TODO Research Questions (for later)

1. Emulator API surface:
- What is the best scriptable path for breakpoints + memory reads on Xenon codepaths?
- Can we automate entry/exit capture at `0x824E0B40` with low overhead?

2. Patched binary path:
- Is there an existing workflow in-project or upstream tooling to rebuild/relink a runnable image from decomp `.obj` outputs?
- If not, what is the lightest viable function-patch approach?

3. Symbol/address robustness:
- Can we derive all required probe addresses from `ham_xbox_r.map` + `symbols.txt` only?
- Do we need runtime signature scanning as fallback?

4. Determinism:
- Best method for repeatable input/timing around beat boundaries?
- Save-state strategy for scenario replay stability?

5. Comparator policy:
- Which fields should be strict-equal vs epsilon vs ignored (pointer identity)?
- How to classify “benign drift” vs true semantic divergence?

## Path To “High Confidence”

To claim high behavioral confidence for `OnBeat`:
- pass state-vector diff across all core scenarios with zero mismatches for a meaningful beat window, and
- investigate any mismatches to root cause (true semantic issue vs deterministic noise).

Recommended bar:
- at least 5 scenario classes x 200+ `OnBeat` calls each, with repeat runs showing stable results.

## Concrete Next Step (Low-Risk)

Start with MVP tooling only:
- trace schema doc + comparator script,
- proof-of-concept emulator probe against original `0x824E0B40`,
- one scenario end-to-end before widening scope.
