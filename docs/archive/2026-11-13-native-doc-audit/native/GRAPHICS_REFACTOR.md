# Graphics Refactor Roadmap

## Summary

The current native renderer is good enough to boot the game, render UI, and draw
venues and characters. Its architecture is not where we want it long-term.

Today the renderer is still shaped by extracted Xbox 360 behavior:

- one broad WGSL shader module handles many material behaviors
- material interpretation is partly heuristic and partly implicit
- shader selection is under-specialized even though the pipeline cache can already
  support better varianting
- pass boundaries exist in practice, but not yet as a deliberate renderer design

This roadmap explicitly redefines the native graphics stack as a **modern
renderer that consumes Milo asset data**, not as a faithful recreation of
Xbox-era shader architecture.

That means we should be willing to diverge when divergence improves:

- readability
- debuggability
- material expressiveness
- testability
- long-term rendering quality

The original engine remains an important source of asset semantics and baseline
visual intent, but it is not the architecture target.

## Current State

The native renderer currently has a working compatibility layer with the
following strengths:

- stable GPU upload path for meshes, textures, uniforms, and bind groups
- functional forward rendering path for standard materials, UI, and venue scenes
- support for skinning, fog, shadows, emissive/rim/specular/environment features
- enough flexibility in `PipelineKey` and the pipeline cache to support a more
  specialized renderer

The main architectural issues are:

- **Monolithic shading model**. The current shader source is still organized like
  a compatibility shader rather than a family of material models.
- **Implicit material contract**. `MaterialSetup` performs meaningful policy and
  heuristics, but that policy is not yet elevated into explicit native material
  categories.
- **Weak shader specialization**. The draw path still trends toward “standard
  shader plus branching” instead of selecting the right pipeline up front.
- **Pass boundaries are informal**. Opaque, transparent, UI, shadow, and post
  behaviors are still coupled more than they should be.
- **Renderer diagnostics are too thin**. When material resolution or shader
  behavior looks wrong, it still takes too much manual spelunking to explain why.

This document is the roadmap for moving from that compatibility shape to a
modern, explicit, native-first rendering architecture.

## Guiding Principles

1. **Asset compatibility is an input, not an architecture.**
   Milo material properties and textures should inform the native renderer, but
   they should not force us to preserve Xbox shader boundaries.

2. **Select material models and shader variants explicitly.**
   We should choose variants at material resolution / pipeline selection time,
   not inside hot fragment branches whenever that split is meaningful.

3. **Separate concerns cleanly.**
   Shared math, material resolution, shading logic, pass logic, and post-process
   logic should not be mixed together in one compatibility blob.

4. **Prefer native abstractions over legacy mirroring.**
   If a native material model or pass abstraction is easier to reason about than
   the original engine's shader organization, we should use the native one.

5. **Be compatible by default, not constrained by default.**
   Preserve original behavior where it is useful and low-cost. Diverge
   intentionally where doing so gives us a better renderer.

6. **Build observability in as part of the architecture.**
   The renderer should explain which material model, shader variant, and pass it
   selected and why.

## Phase 0: Stabilize the Current CPU->GPU Contract

### Goal

Document the renderer contract we have today before widening the architecture.

### Deliverables

- inventory of scene, material, object, and bone uniform usage
- explicit mapping from Milo material properties to current native renderer
  behavior
- inventory of all heuristics in material setup, including name-based and
  fallback lighting behavior
- clear list of fields that are:
  - authoritative
  - native-only policy
  - transitional debt
  - unused / removable later

### Success Criteria

- new graphics work stops reverse-engineering the same CPU->GPU contract
- future shader splits can reference one written contract instead of scattered
  code comments

### Non-Goals

- no behavior change
- no API redesign yet

## Phase 1: Split the Shader Architecture

### Goal

Break the monolithic shader into a proper shader family with shared utilities
and explicit entrypoints.

### Deliverables

- reorganize shader code into:
  - shared math/utilities
  - shared surface sampling
  - shared lighting helpers
  - variant-specific fragment entrypoints
- split material variants such as:
  - standard lit
  - skin
  - hair
  - text / special alpha path where justified
- move material-variant selection out of fragment-time branching and into
  pipeline selection
- keep shared bind groups and vertex paths where they still make sense

### Success Criteria

- no main lighting-path branching on material variation for standard/skin/hair
- pipeline specialization reflects real native shader variants
- shader source reads as a shader library, not as extracted microcode logic

### Non-Goals

- no physically based shading yet
- no renderer-wide pass redesign yet

## Phase 2: Define Native Material Models

### Goal

Replace the current “bag of toggles + heuristics” approach with explicit native
material models.

### Deliverables

- define native material model categories such as:
  - opaque lit
  - character skin
  - character hair
  - emissive / unlit
  - alpha-cut foliage / cloth
  - text / glyph
  - reflective / environment-driven
- make material resolution produce a native material model first, then fill GPU
  parameters from that model
- isolate heuristics behind named policies instead of mixing them through the
  parameter fill path
- add a debug-visible explanation for why a material resolved to a given model

### Success Criteria

- the renderer can explain a material in native terms without reading the shader
- shader selection, uniform packing, and feature enablement are all driven by
  the same resolved model

### Non-Goals

- no source asset migration
- no requirement that native material models match engine class boundaries

## Phase 3: Make the Renderer Pass-Oriented

### Goal

Evolve from a mostly single-pass forward compatibility pipeline into a renderer
with explicit pass ownership and responsibilities.

### Deliverables

- formalize the main passes:
  - shadow / depth
  - opaque
  - alpha-cut
  - transparent
  - UI / text
  - post-process
- define which native material models are legal in which pass
- pull pass-specific logic out of generic mesh draw code
- separate draw submission concerns from shading concerns

### Success Criteria

- pass behavior is explicit in code and documentation
- renderer code becomes simpler because each pass has fewer hidden assumptions
- future work such as batching, sorting, and post-process expansion has clean
  insertion points

### Non-Goals

- no forced move to deferred shading in this phase
- no full post-process overhaul yet

## Phase 4: Modernize Lighting and Surface Quality

### Goal

Use the refactored architecture to improve lighting and materials beyond Xbox-era
constraints.

### Deliverables

- choose the long-term native lighting model:
  - improved stylized forward model, or
  - hybrid PBR-inspired model adapted to DC3 assets
- replace ad hoc specular/rim/environment interactions with a more coherent
  surface model
- treat skin and hair as first-class native shading models rather than special
  branches inside a standard shader
- revisit normal, detail-normal, gloss, emissive, and reflection semantics under
  the new model
- define quality tiers if needed for native, headless, and web targets

### Success Criteria

- character materials have a documented modern shading model
- surface feature interactions are coherent instead of additive hacks
- visual quality improves while code clarity also improves

### Non-Goals

- exact Xbox visual parity
- full physically based asset authoring migration

## Phase 5: Add Tooling, Diagnostics, and Render Validation

### Goal

Make aggressive graphics iteration safe by giving the renderer strong
observability and regression detection.

### Deliverables

- material resolution diagnostics
- shader variant and pipeline cache introspection
- clearer shader compile error surfacing with material / pass context
- render-test coverage for representative native material models
- ability to inspect which pass, variant, and material model was selected
- documentation for how to add a new material model or shader variant

### Success Criteria

- graphics regressions are diagnosable without ad hoc logging sessions
- engineers can explain a wrong-looking material from renderer output alone
- visual regression testing covers core material categories

### Non-Goals

- no full editor tooling requirement
- no giant debug UI mandate before the underlying data is available

## Phase 6: Build an Asset-Aware Enhancement Layer

### Goal

Support richer native rendering behavior without requiring asset reauthoring.

### Deliverables

- explicit rules for asset-derived enhancements such as:
  - skin detection and treatment
  - eye / reflective material policies
  - venue-specific overrides
  - native-only fallback and upgrade logic
- clean separation between:
  - source asset facts
  - native heuristics
  - deliberate modern enhancements
- optional future path for supplemental native metadata if needed

### Success Criteria

- enhancements are explicit, auditable, and reversible
- asset compatibility remains strong without locking the renderer into old
  assumptions

### Non-Goals

- no mandatory asset migration
- no hidden behavior in unnamed heuristics

## Anchor Files

These files are the current architectural seams this roadmap expects to evolve
first:

- `native/shaders/standard.wgsl`
- `native/src/platform/MaterialSetup.cpp`
- `native/src/gfx/PipelineManager.cpp`

They should eventually represent:

- shader library and variant structure
- native material resolution and packing
- pipeline / pass specialization

## What Not To Do

- do not keep adding more material branches to the compatibility shader unless
  the branch is clearly transitional
- do not expand decomp-era engine shader enums just to model native-only shader
  variants
- do not hide renderer policy in unnamed heuristics
- do not treat “matches Xbox organization” as the default argument for keeping
  weak architecture

## Recommended Work Order

If we execute this roadmap incrementally, the most sensible order is:

1. finish the current contract write-up
2. split shader variants and make selection explicit
3. introduce native material models
4. formalize render passes
5. modernize lighting and surface behavior
6. deepen diagnostics and test coverage as each step lands

This keeps the renderer operational while continuously improving the
architecture instead of waiting for one risky rewrite.
