# OnBeat Runtime Validation Tooling Handoff (2026-02-08)

## Primary Context (Backlink)

This handoff extends:

- `docs/sessions/2026-02-08-onbeat-differential-runtime-validation-ideas.md`

That doc captures the validation concept and phases. This file captures what was verified during follow-up tooling research and what to implement next.

## What We Verified In This Session

### Repo paths and where relevant code actually is

- DC3 decomp project: `/home/free/code/milohax/dc3-decomp`
- jeff fork (dtk fork): `/home/free/code/milohax/jeff`
- objdiff fork: `/home/free/code/milohax/objdiff`
- wibo: `/home/free/code/milohax/wibo`
- xenia source used locally: `/home/free/code/milohax/vmx128-research/xenia-source`
- xenia docs/reference mirror: `/home/free/code/milohax/vmx128-research/xenia-reference`
- extra Xbox/XEX docs: `/home/free/code/milohax/xbox-reversing`

Important: there is no `/home/free/code/milohax/xenia` directory in this workspace; the active copy is `vmx128-research/xenia-source`.

### Build graph status in dc3-decomp

- `build.ninja` default target is progress/report generation, not final binary link.
- `tools/project.py` contains `LinkStep` infrastructure, but link rules and link target emission are currently commented out for this X360 project path.
- Current workflow is compile/split/diff oriented.

### jeff capabilities (today)

- Supports `xex split`, `xex extract`, `xex info`, `xex map`.
- Explicitly does **not** support full re-link to final runnable executable in current scope.
- Emits COFF objects and build config for split workflow.

### wibo capabilities (today)

- Runtime wrapper for 32-bit Windows command-line binaries on Linux/macOS.
- Used as compiler-wrapper path (`cl.exe`, potentially `link.exe`) in host tooling workflows.
- Not relevant for Xbox runtime execution itself.

### xenia capabilities relevant to this plan

- XEX patch loading exists in local source:
  - `UserModule::LoadFromFile` looks for `path + "p"` (for `default.xexp`) and applies patch if present.
  - Patch application path exists (`XexModule::ApplyPatch`).
  - Gate flag exists: `xex_apply_patches`.
- CPU breakpoint infrastructure exists:
  - `Breakpoint` class, processor breakpoint install/uninstall.
  - `--break_on_instruction` and conditional break flags.
- Trace plumbing exists:
  - `load_module_map` flag.
  - function/instruction trace components and tracing docs (`docs/instruction_tracing.md`).

### `OnBeat` probe anchors verified from map + symbols + objdiff

- Symbol: `?OnBeat@RhythmBattle@@AAAXXZ`
- Start VA: `0x824E0B40`
- Size: `0x407C` (`config/373307D9/symbols.txt`)
- End VA: `0x824E4BBC` (exclusive)
- Objdiff instruction count: `4170`

Practical breakpoint anchors:

- Entry capture: `0x824E0B40`
- Shared exit/epilog capture: `0x824E4BAC`
  - This is the common epilog path (`addi r1, r31, 0x750`) and is reached by both normal flow and early returns.
  - Many branch exits target this address, so MVP can use one exit breakpoint rather than dynamic return-site tracking.

Address normalization detail (important for probe tooling):

- Current `objdiff-cli -f json` instruction addresses for this function are section-relative offsets (`0x48C0..0x8938`), not absolute VAs.
- For this binary, VA normalization is:
  - `runtime_va = 0x824DC280 + objdiff_offset`
  - This maps:
    - `0x48C0 -> 0x824E0B40` (entry)
    - `0x892C -> 0x824E4BAC` (shared epilog `addi r1, r31, 0x750`)
    - `0x8938 + 4 -> 0x824E4BBC` (exclusive end)

### Xenia breakpoint/runtime constraints we confirmed

- `break_on_instruction` is a single cvar target (`--break_on_instruction=...`) and is not enough for multi-address probe plans by itself.
- Full capture requires explicit `Breakpoint` registration (`Processor::AddBreakpoint`) with callback logic.
- Breakpoint callback receives `ThreadDebugInfo*`, including `guest_context`, and can read guest memory through `processor->memory()->TranslateVirtual(...)`.
- `this` at `OnBeat` entry should be read from `guest_context.r[3]` (PPC calling convention).
- Important reliability caveat:
  - `Processor::UpdateThreadExecutionStates` notes guest context is only reliably up to date when `--debug` or `--store_all_context_values` is enabled.
  - Runtime validation runs should set one of these (prefer `--store_all_context_values` for lower debugger coupling).
- `--headless` exists (`src/xenia/kernel/kernel_flags.cc`) and should be used for scripted runs.
- Existing function tracing (`trace_functions`, `trace_function_data_path`) is primarily counters/coverage data, not structured `this`/field snapshots; it does not replace custom probe capture.
- Critical execution-model caveat for headless probes:
  - `Processor::OnThreadBreakpointHit` unconditionally suspends threads and sets execution state to paused after invoking the breakpoint callback.
  - So a callback alone is not sufficient for unattended capture; tooling must also drive `Continue()` (or add a dedicated non-pausing trace path in Xenia).

### dc3-decomp build/config constraints confirmed

- `configure.py` currently supports modes `configure` and `progress` only.
- `tools/project.py` has extension hooks via `custom_build_steps` (`pre-compile`, `post-compile`, `post-link`, `post-build`) that can host runtime-validation targets without reworking the whole build graph.
- X360 link emission remains commented out, so runtime lane should remain parallel to compile+objdiff for now.
- No existing runtime-validation implementation artifacts are present yet (`docs/runtime_validation/`, `tools/runtime_validation/`, comparator scripts).

### `jeff` status details confirmed in code

- Current XEX subcommands are: `disasm`, `extract`, `info`, `map`, `pdb`, `split`.
- README and code still mark full relink/final runnable image rebuild as out of scope.
- Additional caveat: LZX-compressed XEX is not currently supported in `jeff`; delta-compressed handling exists at parse level but patch-descriptor handling is still incomplete in parts of the parser.
- There is currently no dedicated `xexp`/patch generation command path in `jeff`; `DeltaPatchDescriptor` parsing is still tagged TODO in `src/util/xex.rs`.

### Determinism / replay constraints confirmed in Xenia code

- No first-class, user-facing savestate/replay pipeline is exposed in this source tree for our workflow yet.
  - `emulator.cc` only has a comment about potentially resetting clock base from save-state data in the future.
- Time behavior currently depends on runtime cvars:
  - `time_scalar` (`src/xenia/emulator.cc`)
  - `clock_no_scaling`, `clock_source_raw` (`src/xenia/base/clock.cc`)
- `--headless` is primarily a UI-prompt behavior (auto-select/default handling in XAM UI paths), not a separate no-window app mode.
  - There is only `XE_DEFINE_WINDOWED_APP(xenia, ...)` in `xenia_main.cc`.

### Current workspace reality check (for next session)

- `objdiff` currently reports `OnBeat` at `93.2%` in this working tree (not `93.3%`).
- Treat this as a moving local baseline; runtime-validation planning should key off semantic trace stability, not exact transient match%.

## Key Insight Shift

Given current dc3-decomp + jeff state, we should treat this as a tooling project we own:

1. Keep current obj-level diff loop untouched.
2. Add a runtime validation lane (trace compare).
3. Add either:
   - a minimal patch/XEXP path first (preferred), or
   - a larger full relink/repack path later.

## Highest-Leverage Workflow Improvements

## 1) Build a repeatable runtime semantic diff lane (highest ROI)

- Implement a stable JSONL schema for `OnBeat` entry/exit state snapshots.
- Build a comparator that finds first divergence with tolerance rules.
- Run this across deterministic scenario matrix.

Why first: independent of full relink and immediately useful for semantic confidence decisions near 90%+ match.

## 2) Use Xenia patch loading path (`.xexp`) for A/B without full relink

- Leverage existing `default.xex` + `default.xexp` loading path in Xenia.
- Prefer function-targeted patch payloads over “rebuild whole image” as first practical runtime replacement route.

Why second: lower scope than full linker reproduction and already aligned with emulator path.

## 3) Add automated probe hooks in Xenia for headless capture

- Add dedicated CLI/flag path to register guest breakpoints at function entry/exit and dump memory fields.
- Avoid interactive UI debugger dependency.

Why third: makes semantic validation scalable and CI-friendly.

## 4) Add a function-level mocked differential harness ("unit-style" fast iteration)

- Build a host-side, deterministic test harness for `RhythmBattle::OnBeat` that mocks external dependencies and compares state/event outputs quickly.
- Use this as a rapid triage loop for semantic bugs before running slower emulator/runtime A/B.
- Treat this as complementary tooling, not as final proof of equivalence.

Why fourth: highest iteration speed for logic debugging, but lower confidence than real runtime because mocks can be incomplete.

### Prior art / useful references

- D-Helix (function-level decompiler correctness via recompilation + symbolic differential checking):
  - https://www.usenix.org/conference/usenixsecurity24/presentation/zou
- angr hooks / SimProcedures (function substitution and external modeling):
  - https://docs.angr.io/en/v9.2.119/extending-angr/simprocedures.html
- SEDiff (scope-aware differential fuzzing for internal function models):
  - https://2022.esec-fse.org/details/fse-2022-research-papers/2/SEDiff-Scope-Aware-Differential-Fuzzing-to-Test-Internal-Function-Models-in-Symbolic
  - DOI: https://doi.org/10.1145/3540250.3549080

### What this fast-iteration piece would take (high-level)

1. Define seams for externals touched by `OnBeat`.
   - Wrap global/singleton interactions and heavy side-effect calls behind a small adapter interface (UI manager, Ham provider/director, message dispatch, etc.).
2. Build deterministic fixtures for object state.
   - Construct minimal `RhythmBattle` / `RhythmBattlePlayer` memory states needed by `OnBeat`.
   - Start from the existing offset-grounded state vector and expand only as needed.
3. Implement mock backends + event recorder.
   - Mocks return deterministic values and log ordered side effects (calls/messages/arguments) into a canonical event stream.
4. Add differential assertions.
   - Compare post-call state vector plus event stream against expected fixtures.
   - Include strict/epsilon/ignore policy alignment with runtime trace comparator.
5. Add scenario generator/fuzzer (targeted, not fully random).
   - Sweep branch-driving fields (`mFinale`, `mActive`, zone/jack state, beat window inputs) with bounded combinations.

### Practical implementation options

- Option A (preferred): source-level adapter injection
  - Smallest long-term maintenance cost.
  - Makes dependencies explicit and testable in normal C++ tests.
- Option B: link-time symbol interposition/mocks for selected externals
  - Faster to prototype, but brittle for larger refactors.
- Option C: trace-replay oracle mode
  - Feed harness with captured runtime snapshots/events and assert decomp output matches oracle expectations.
  - Best bridge between mocked and real-runtime lanes.

### Relative effort and expected ROI

- Phase 0: design + seam map (`OnBeat` only): Small (1-2 days)
- Phase 1: minimal harness with 2-3 scenarios: Medium (3-7 days)
- Phase 2: robust fixtures + event diff + targeted fuzz matrix: Medium/Large (1-3 weeks)
- Phase 3: generalized reusable harness for other hard functions: Large (2-6 weeks)

Expected payoff:

- Very fast local semantic triage loop (minutes/seconds vs emulator-heavy runs).
- Better root-cause isolation for logic issues.
- Still requires runtime A/B lane for final behavioral confidence.

## What We Need To Implement (by component)

## `jeff` (or sibling tool in same ecosystem)

Minimum additions:

- A patch artifact generator command (targeting `.xexp`/delta patch workflow) from:
  - base XEX/EXE
  - replacement code bytes/object contribution for selected functions
  - symbol/VA metadata
- Utility output: deterministic VA <-> section/file-offset mapping export for patch and probe plan generation.
- Optional: emit a probe-plan JSON from symbols/maps for specific function(s).

Stretch additions:

- “function patch pack” format for swapping one/few functions with trampoline stubs.
- direct tooling to produce `default.xexp` from modified image deltas.

## `ninja` / `configure.py` / `tools/project.py`

Add explicit targets for runtime validation pipeline:

- `runtime_patch_onbeat` (build replacement artifact)
- `runtime_probe_plan_onbeat` (emit addresses and field offsets used by tracer)
- `runtime_trace_orig` (run original trace capture)
- `runtime_trace_patched` (run patched trace capture)
- `runtime_compare_onbeat` (comparator report)

Current blocker to note:

- Link steps exist in code but are disabled in emitted graph for this X360 workflow; we need either:
  - patch-specific target path independent of full link, or
  - re-enable/extend link path with X360-specific outputs.

## `xenia` (local source: `vmx128-research/xenia-source`)

Add a non-interactive tracing feature layer:

- CLI flag(s) for guest breakpoints and callback-based dumps, e.g.:
  - `--trace_break_entry=0x824E0B40`
  - `--trace_break_exit=<computed return site(s) or call depth policy>`
  - `--trace_state_spec=<json spec>`
  - `--trace_out=<jsonl>`
- Memory read helpers in breakpoint callback path to serialize selected fields.
- Optional run metadata and deterministic replay controls where available.

Existing code that confirms feasibility:

- `src/xenia/cpu/breakpoint.h/.cc`
- `src/xenia/cpu/cpu_flags.cc` (`break_on_instruction`, break conditions)
- `src/xenia/kernel/user_module.cc` (`xex_apply_patches`, patch load of `path + "p"`)
- `src/xenia/cpu/xex_module.cc` (`ApplyPatch`)
- `src/xenia/cpu/processor.{h,cc}` (`Breakpoint` registration/callback surface)
- `src/xenia/cpu/thread_debug_info.h` (`guest_context` capture surface)
- `src/xenia/cpu/compiler/passes/context_promotion_pass.cc` (`store_all_context_values` behavior)

## `wibo`

- No runtime-emulation work needed.
- Only host-side helper if we decide to run additional Windows linker/packaging tools in automation.

## `dc3-decomp` fast-iteration harness lane (new)

Add a host-side validation lane for function-level mocked differential testing:

- `runtime_unit_onbeat` (run fixture-based mocked tests for `OnBeat`)
- `runtime_unit_onbeat_fuzz` (run bounded scenario matrix/fuzz sweep)
- `runtime_unit_onbeat_report` (emit first divergence in state/event format shared with runtime comparator)

Guardrails:

- Keep this lane explicitly labeled as "model-based confidence", not runtime equivalence.
- Reuse the same schema keys as emulator traces to keep tooling unified.

## Follow-Up Items (Prioritized)

## P0 (next session)

1. Create `docs/runtime_validation/onbeat_trace_schema.md` and lock field list/equality policy.
2. Implement `tools/compare_onbeat_traces.py` with strict/epsilon/ignore rules.
3. Add a decomp-side `DC3_DIFF_TRACE` emitter in `RhythmBattle::OnBeat` (entry + exit) as schema sanity harness.
4. Prototype Xenia headless capture with two breakpoints: entry `0x824E0B40` and shared epilog `0x824E4BAC` (single scenario), including explicit auto-continue behavior after each hit.
5. Wire capture run flags to include `--store_all_context_values` (or `--debug`) so register/context reads are trustworthy.
6. Write deterministic run protocol with currently available controls (fixed launch path/config + clock flags + input discipline), and explicitly document savestate gaps.

## P1

1. Build first patch artifact path targeting Xenia’s existing `.xexp` patch application flow.
2. Add ninja targets for orig trace, patched trace, compare.
3. Add probe-plan generator script (symbol/map -> breakpoints + watched fields).
4. Prototype `runtime_unit_onbeat` mocked harness with at least 2 deterministic fixtures (non-finale + finale).

## P2

1. Generalize runtime diff lane beyond `OnBeat`.
2. Optional objdiff metadata integration: mark “runtime-validated” functions in generated reports.
3. Consider full relink/repack roadmap only if patch lane proves insufficient.
4. Expand mocked harness into reusable per-function framework and add targeted fuzz matrix support.

## Open Questions to Resolve

1. Do we standardize on XEXP delta patch generation as the primary replacement mechanism?
2. Is fixed-epilog exit capture (`0x824E4BAC`) sufficient for `OnBeat`, or do we still need dynamic return-site/call-depth capture for edge cases?
3. Which determinism controls are mandatory before accepting mismatch reports as semantic?
4. Where should runtime validation artifacts live (`tmp/` vs checked-in `tools/runtime_validation/`)?

## Seed State Vector (offset-grounded)

Use these as initial schema fields for deterministic memory snapshots:

- `RhythmBattle` (`this`) key offsets:
  - `mPlayerOne @ 0x30`, `mPlayerTwo @ 0x44`
  - `mFinale @ 0xFA`, `mActive @ 0xFB`
  - `unkfc..unk102 @ 0xFC..0x102`
  - `mStartBeat @ 0x104`, `mEndBeat @ 0x108`, `unk114 @ 0x114`, `unk118 @ 0x118`
  - `unk124 @ 0x124`, `unk128 @ 0x128`, `unk140 @ 0x140`, `unk144 @ 0x144`, `unk148 @ 0x148`, `unk14c @ 0x14c`
- `RhythmBattlePlayer` (for both player pointers):
  - `unk260 @ 0x260`, `mInTheZone @ 0x268`, `unk26c @ 0x26c`
  - `unk280 @ 0x280`, `unk284 @ 0x284`
  - `unk2a4 @ 0x2A4`, `unk2a5 @ 0x2A5`

## Practical Learnings from This Session

- The original idea is solid, but implementation hinges on closing tooling gaps, not on more static analysis.
- Xenia already has useful hooks (patch loading + breakpoints); exploiting those is higher leverage than chasing full relink immediately.
- Current dc3-decomp graph is intentionally non-linking for X360 flow; runtime validation should be added as a parallel lane, not bolted onto objdiff loop first.
- `wibo` is not the bottleneck for runtime A/B; patch production and emulator capture automation are.
