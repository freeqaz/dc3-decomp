# Target Facts Layer

This document defines the missing integration contract for the synthesis
engine: how target-side evidence is normalized into machine-usable facts that
proposal generation, search, and validation can all consume.

It sits between:

- the high-level engine roadmap in
  `docs/plans/synthesis-engine/ROADMAP.md`
- the beam-search controller in
  `docs/plans/permuter/BEAM_SOLVER.md`
- source-grounding from
  `docs/plans/synthesis-engine/INSTRUCTION_ATTRIBUTION.md`
- compiler-behavior priors from
  `docs/plans/synthesis-engine/COMPILER_ATLAS.md`
- historical strategy priors from
  `docs/plans/synthesis-engine/PATTERN_MINING.md`

## Purpose

The target-facts layer exists to stop guidance from living as raw text blobs
and pattern-local heuristics.

Instead of asking:

- "What did Ghidra say?"
- "What did m2c say?"
- "Which pattern felt relevant?"

the engine should be able to ask:

- "What control-flow shape is likely here?"
- "Which source region does this hypothesis apply to?"
- "How confident is that hypothesis?"
- "Which sources agree or disagree?"
- "What proposal families does this support?"
- "What validators should become stricter for this region?"

This is the layer that turns guidance into search infrastructure.

## Design Goals

The target-facts model should be:

- normalized: the same kind of hypothesis should look the same no matter which
  source produced it
- regional: facts should point to source regions or target instruction regions
  whenever possible
- multi-source: facts should carry evidence from Ghidra, m2c, objdiff,
  attribution, atlas, and historical mining
- uncertainty-aware: confidence and disagreement must be explicit
- consumable: proposal generators, beam ranking, and validators should all be
  able to use the same facts

## Non-Goals

This layer is not:

- a new decompiler
- a proof system
- a replacement for the existing `Diagnosis` object
- a giant unstructured JSON dump of every tool output

It is a normalized hypothesis layer above existing analyses.

## Core Model

The central object should be a `TargetFacts` bundle for one function.

Suggested shape:

```python
TargetFacts:
  symbol: str
  unit: str
  source_path: str
  target_identity: TargetIdentity
  regions: list[FactRegion]
  facts: list[TargetFact]
  global_facts: list[TargetFact]
  conflicts: list[FactConflict]
  provenance: list[FactEvidence]
```

### Target Identity

Stable metadata for the current target:

```python
TargetIdentity:
  target_instruction_count: int
  baseline_match_percent: float
  diagnosis_hash: str | None
  ghidra_available: bool
  m2c_available: bool
  rb3_available: bool
  has_listing_attribution: bool
```

### Fact Regions

Facts should attach to regions, not just whole functions.

```python
FactRegion:
  region_id: str
  source_file: str | None
  source_lines: tuple[int, int] | None
  target_offsets: tuple[int, int] | None
  base_offsets: tuple[int, int] | None
  mismatch_region_id: str | None
  attribution_confidence: float
  tags: set[str]
```

Regions may come from:

- mismatch clustering from objdiff
- `/FAs` listing attribution
- inferred target-side spans from Ghidra
- header/inlined regions when applicable

The engine should be allowed to keep facts at function scope when region
grounding is unavailable, but regional facts are preferred.

## Fact Schema

Every fact should use one normalized schema regardless of source.

```python
TargetFact:
  fact_id: str
  kind: FactKind
  region_id: str | None
  payload: dict
  confidence: float
  status: FactStatus
  evidence_ids: list[str]
  supported_proposals: list[str]
  supported_validators: list[str]
```

Where:

- `FactKind` is a closed enum for the initial engine
- `status` is one of:
  - `supported`
  - `tentative`
  - `conflicted`
  - `deprecated`

Suggested initial `FactKind` values:

- `control_shape`
- `call_order`
- `call_gating`
- `return_shape`
- `switch_shape`
- `guard_shape`
- `temp_pressure`
- `live_range_pressure`
- `type_shape`
- `register_pressure`
- `compiler_artifact`
- `mismatch_class`
- `region_priority`
- `proposal_prior`
- `validator_expectation`
- `no_touch_zone`

## Evidence Schema

Facts should never exist without provenance.

```python
FactEvidence:
  evidence_id: str
  source: EvidenceSource
  region_id: str | None
  raw_ref: str | None
  summary: str
  confidence: float
  extracted_at: datetime | None
```

Initial `EvidenceSource` values:

- `objdiff`
- `diagnosis`
- `ghidra`
- `m2c`
- `rb3`
- `asm_listing`
- `compiler_atlas`
- `pattern_mining`
- `manual_annotation`

The important rule is that proposal code should consume normalized facts, not
raw evidence, unless it is doing fact extraction itself.

## Conflict Model

Disagreement is useful information. The model should preserve it directly.

```python
FactConflict:
  region_id: str | None
  fact_kind: FactKind
  competing_fact_ids: list[str]
  severity: str              # low / medium / high
  resolution_policy: str     # preserve_both / prefer_high_conf / human_review
```

Examples:

- Ghidra says last call should be `Foo`, m2c says `Bar`
- atlas suggests signed comparison shape, m2c suggests unsigned
- historical strategy prior says declaration reorder often helps, but region
  attribution shows the real mismatch is call ordering

Conflicts should lower confidence and alter search ranking. They should not be
silently flattened away.

## Fact Kinds And Payloads

The initial system does not need every possible fact. It does need a small
stable vocabulary.

### Control Shape

```python
kind = "control_shape"
payload = {
  "shape": "if_chain" | "switch" | "guard" | "nested_if" | "single_return",
  "direction": "prefer_target" | "avoid_current",
}
```

Used by:

- `switch_if_convert`
- `guard_to_nested`
- `branch_polarity`
- `single_return`

### Call Order

```python
kind = "call_order"
payload = {
  "ordered_calls": ["Prepare", "Finish"],
  "preferred_last_call": "Finish",
  "tail_call_friendly": True | False,
}
```

Used by:

- `tail_call_reorder`
- `return_call_merge`
- future call-gating proposals

### Guard Shape / Call Gating

```python
kind = "guard_shape"
payload = {
  "guard_kind": "null_check" | "bool_gate" | "range_gate",
  "prefer_compact": True | False,
}
```

Used by:

- null-guard transforms
- `guard_to_nested`
- `early_return_merge`

### Temp / Live Range Pressure

```python
kind = "temp_pressure"
payload = {
  "pressure": "high" | "medium" | "low",
  "region_role": "expression" | "call_setup" | "return_path",
  "preferred_action": "introduce_temp" | "inline_temp" | "move_decl",
}
```

Used by:

- `variable_extraction`
- `declaration_movement`
- `statement_reorder`
- future register-pressure proposals

### Compiler Artifact

```python
kind = "compiler_artifact"
payload = {
  "artifact": "bool_materialization" | "tail_call_gate" | "signed_zero_test",
  "likely_compiler_induced": True,
}
```

Used by:

- atlas-backed deterministic proposals
- validator expectations
- unfixable/risk heuristics

### Proposal Prior

```python
kind = "proposal_prior"
payload = {
  "proposal_family": "declaration_reorder",
  "priority": 0.85,
  "reason": "pattern_mining+atlas_agreement",
}
```

Used by:

- beam seeding
- proposal budget allocation
- tie-breaking inside a region

### Validator Expectation

```python
kind = "validator_expectation"
payload = {
  "validator": "call_behavior" | "region_opcode_delta" | "type_stability",
  "expected_direction": "must_improve" | "must_not_regress",
}
```

Used by:

- candidate rejection
- lineage-local suppression
- selective expensive validation

## How Each Input System Contributes

The target-facts layer is not built by one tool. It is fused from several.

### Objdiff / Diagnosis

Primary contributions:

- mismatch regions
- mismatch classes
- cluster boundaries
- call-count deltas
- prologue/register-pressure signals
- region priority

Objdiff is the strongest source for "where does the function still differ?"
but weaker on "what exact source shape should produce the target?"

### Ghidra

Primary contributions:

- decompiled control-flow hypotheses
- likely last-call / return shape
- target-side source-like structure
- target region hints when no direct source grounding exists

Ghidra is useful as a structural oracle, but not as ground truth.

### m2c

Primary contributions:

- call ordering
- condition structure
- temp/live-range shape
- second-opinion confidence against Ghidra

m2c is especially valuable when it agrees with Ghidra or strongly conflicts in
a focused region.

### `/FAs` Instruction Attribution

Primary contributions:

- source-line grounding for our side
- mismatch-region scoping
- region-level impact measurement
- support for region-aware diversity

This is the system that makes target facts actionable at the source-edit level.

### Compiler Atlas

Primary contributions:

- deterministic source-feature hypotheses for a target instruction pattern
- compiler-artifact labels
- proposal families tied to concrete codegen signatures

Atlas entries should produce priors, not mandates.

### Pattern Mining

Primary contributions:

- historical proposal priors
- unit-aware confidence boosts
- negative priors from repeated failure
- mismatch-class-conditioned rankings

Mining should influence ordering and budget, not override stronger local facts.

## Fusion Rules

Fact extraction should not be a free-for-all. Use explicit fusion rules.

### Rule 1: Prefer Regional Over Global

If a fact can be grounded to a mismatch region or source region, that regional
fact should outrank a whole-function prior.

Example:

- global mining says `declaration_reorder` often helps this class
- region attribution says the live mismatch is a tail-call region

The regional call-order fact wins for that region.

### Rule 2: Agreement Raises Confidence

If two independent sources support the same normalized fact, confidence should
increase.

Examples:

- Ghidra and m2c agree on preferred last call
- atlas and diagnosis both indicate signed/unsigned mismatch

### Rule 3: Disagreement Is Preserved

Conflicting facts should remain visible, not be eagerly collapsed.

Search and validation should see:

- that there is a conflict
- where it applies
- which sources support each side

### Rule 4: Stronger Grounding Beats Abstract Priors

Ordering of evidentiary strength for phase 1:

1. region-grounded attribution + diagnosis
2. convergent Ghidra/m2c fact
3. compiler atlas prior
4. pattern-mining prior

This ordering is not absolute truth, but it is a good operational default.

### Rule 5: Facts Age By State Distance

Target-side facts are stable across a beam lineage, but state-derived summaries
must be recomputed as the source changes.

Stable:

- target call order hypothesis
- target control-shape hypothesis
- atlas proposal priors
- historical mining priors

State-derived:

- whether the current candidate already matches the target fact
- whether a region has improved or regressed
- whether a candidate moved toward or away from a supported hypothesis

## Consumption By Search

The target-facts layer should directly drive three engine subsystems.

### Proposal Generation

Proposal generators should query facts like:

- which regions deserve budget
- which proposal families are supported for this region
- which proposal families are discouraged by conflict or no-touch facts

This is how we stop sweeping patterns across whole functions blindly.

### Beam Selection

Beam ranking should use facts for:

- guidance agreement scoring
- region-novelty preservation
- rewarding candidates that improve distinct mismatch regions
- reducing confidence in candidates that move against strong facts

### Validation

Validators should use facts to know:

- which regions must not regress
- which properties should be checked for this candidate family
- when to run more expensive checks

## Region-Novelty And Diversity

A key use of target facts is preserving partial progress that would be invisible
to a scalar score.

The engine should track region-level outcomes such as:

```python
RegionProgress:
  region_id: str
  prior_mismatch_count: int
  current_mismatch_count: int
  delta: int
  dominant_type_before: str
  dominant_type_after: str
```

Beam diversity should preserve branches that:

- uniquely improve region A
- uniquely support one plausible control-shape hypothesis
- reveal that one guidance source is misleading

This is where target facts connect directly to the byte-equivalent-search
insight of preserving novel partial progress.

## Minimal Phase-1 Implementation

The first implementation does not need a giant ontology.

It should support:

1. regions from diagnosis + `/FAs` attribution
2. `control_shape`, `call_order`, `temp_pressure`, `proposal_prior`,
   `validator_expectation`, and `mismatch_class`
3. evidence from:
   - objdiff/diagnosis
   - Ghidra
   - m2c
   - asm listing attribution
   - pattern mining
4. conflict tracking between Ghidra and m2c
5. region-level budget/ranking hooks for beam search

That is enough to make the beam controller and constrained proposals much more
intelligent without boiling the ocean.

## Open Questions

### Schema Design

- Should facts stay as a generic `payload: dict`, or should the core fact kinds
  become strongly typed dataclasses early?
- How much normalization is enough before the model becomes too rigid?

### Region Mapping

- What is the best fallback when `/FAs` attribution is missing or ambiguous?
- How should inline/header regions be represented so local and cross-unit
  search can share the same model?

### Confidence

- Should confidence stay as a scalar, or should it be split into components:
  source reliability, grounding quality, and cross-source agreement?
- How aggressively should conflict lower proposal budgets?

### Search Integration

- Which proposal sources should consume facts directly in phase 1?
- Should fact-driven proposal generation happen lazily per region, or should
  the engine pre-materialize proposal suggestions up front?

### Validation

- Which validator expectations are practical before we have richer semantic
  analysis?
- How should a failed validator feed back into fact confidence or lineage-local
  suppression?

## Recommended Immediate Follow-On

After this doc, the next useful steps are:

1. add a `TargetFacts` datamodel in permuter core
2. implement fact extraction from diagnosis + Ghidra + m2c
3. implement region grounding from `/FAs` listing attribution
4. let beam seeding and proposal ranking consume `proposal_prior` and
   `call_order` / `control_shape` facts first

That gives the synthesis engine its first real integration layer.
