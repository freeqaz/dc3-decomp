# Decomp Tooling Options For Code Writing (2026-02-08)

## Context

This note follows:

- `docs/sessions/2026-02-08-onbeat-differential-runtime-validation-ideas.md`
- `docs/sessions/2026-02-08-onbeat-runtime-validation-tooling-handoff.md`

Those docs already cover the runtime validation lane well (Xenia breakpoint probes, `.xexp` patch direction, trace schema/comparator, and mocked differential harness).

This note focuses on additional tooling that helps at the **actual C++ code-authoring phase**, beyond `m2c/ghidra` and `objdiff`.

## Current Baseline (Already Covered)

Existing docs already include:

1. Runtime semantic diff lane (entry/exit snapshots + comparator).
2. Patch-based A/B path (`default.xexp`) rather than full relink as first target.
3. Xenia automation hooks for headless capture.
4. Optional fast mocked harness for rapid semantic triage.

## Additional Tooling Options For Code Writing

## 1) C++ permuter-lite for targeted source rewrites (highest practical ROI)

Goal:
- Automatically generate and score safe source variants for known match patterns.

Scope:
- Constrained transformations only (not open-ended synthesis), for example:
  - bool shaping (`!!x`, signed/unsigned compare form, branch inversion form),
  - guard placement/hoisting,
  - declaration and temporary-lifetime ordering,
  - expression splitting/combining patterns.

Why it helps:
- Replaces manual trial-and-error during late-stage matching.
- Keeps authoring loop fast by running many small variants against objdiff.

## 2) BinDiff/BinExport correlation lane

Goal:
- Use binary-level function matching to find nearest structural analogs and guide source shape decisions.

Use cases:
- Large functions where control flow is right but local layout/call envelope is drifting.
- Recovering intent from similar already-matched functions.

Why it helps:
- Adds structural context that instruction-only diffing cannot provide.

## 3) QEMU TCG plugin tracer (secondary runtime backend)

Goal:
- Add a generic, scriptable tracing backend independent of Xenia internals.

Use cases:
- Cross-checking runtime captures.
- Memory access/event instrumentation when emulator-specific hooks are limiting.

Why it helps:
- Reduces single-backend risk and improves instrumentation flexibility.

## 4) angr differential harness (selective use)

Goal:
- Symbolic/differential exploration around hard branch divergences with modeled externals.

Use cases:
- High-value debugging when branch semantics are unclear and mocks are needed.

Why it helps:
- Strong diagnosis for path divergence, but cost/complexity is high.

## 5) Capstone + Unicorn micro-sandbox

Goal:
- Fast local execution sandbox for PPC snippets/basic blocks to test small transformations.

Use cases:
- Verifying register/memory effects of candidate rewrites before editing large functions.

Why it helps:
- Very fast inner-loop checks for small code-shape hypotheses.

## 6) Binary Ninja IL scripting as a second static oracle

Goal:
- Use BNIL/MLIL scripts for alternate dataflow and condition recovery.

Use cases:
- Cases where Ghidra/m2c output is ambiguous or inconsistent.

Why it helps:
- Independent decomp perspective can unblock authoring decisions.

## 7) RetDec as occasional triage only

Goal:
- Third static opinion for specific functions.

Constraint:
- Keep non-critical; treat as supplementary due to maintenance/state limitations.

## Recommended Priority

1. Build **C++ permuter-lite** first.
2. Add **BinDiff/BinExport lane** second.
3. Keep **QEMU tracer backend** as medium-term instrumentation hardening.
4. Use **angr/Unicorn/BNIL** selectively for difficult cases.

Rationale:
- The first two directly improve day-to-day code writing and source-shape convergence.
- Runtime lanes already have a strong plan; biggest remaining gap is automated authoring assistance.

## Practical Next Step

Implement a minimal `tools/permuter_lite/` prototype with:

1. One function target input.
2. 5-10 rewrite rules.
3. Batch variant generation.
4. Objdiff scoring and top-k report.

Then add a small BinDiff integration script that maps low-confidence functions to nearest matched analogs for review.

## References

- BinDiff: <https://github.com/google/bindiff>
- RetDec: <https://github.com/avast/retdec>
- angr SimProcedures: <https://docs.angr.io/en/v9.2.106/extending-angr/simprocedures.html>
- angr arch support (`archinfo`): <https://api.angr.io/projects/archinfo/en/latest/api.html>
- QEMU TCG plugins: <https://www.qemu.org/docs/master/devel/tcg-plugins.html>
- Capstone architecture support: <https://www.capstone-engine.org/arch.html>
- Unicorn: <https://www.unicorn-engine.org/>
- Binary Ninja features: <https://binary.ninja/features/>
