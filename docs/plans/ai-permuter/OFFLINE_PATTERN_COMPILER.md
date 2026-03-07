# AI Offline Pattern Compiler

## Summary

This project uses AI offline to expand the deterministic permuter, not to replace it in the hot path.

The core idea:

1. Mine decomp fixes from git history
2. Normalize them into structured "before -> after" examples
3. Use AI to cluster examples, explain the underlying compiler/codegen pattern, and propose deterministic automation
4. Have a human review and implement the resulting permuter rule and tests

This is a better MVP than an online AI advisor because it improves the permuter permanently. Every successful outcome becomes reusable automation instead of a one-off model suggestion.

## Problem

The current permuter already handles many bounded search-space problems well. The bottleneck is not "trying more random variants." It is:

- discovering recurring fix patterns from manual work
- noticing when multiple seemingly different fixes are actually the same codegen lever
- translating that insight into a robust deterministic pattern with tests

Today that translation happens manually and inconsistently. Valuable fixes are spread across:

- git history
- commit messages
- session notes
- pattern docs
- engineer memory

This project turns that historical record into a pipeline for proposing new permuter patterns and improving existing ones.

## Goal

Produce a repeatable offline workflow that converts historical decomp fixes into:

- proposed new permuter rules
- suggested expansions to existing rules
- candidate tests and fixtures
- updated pattern documentation

Success is measured by deterministic permuter improvements, not by model cleverness.

## Non-Goals

- No AI in the compile loop
- No AI-generated source edits applied directly to target functions
- No model dependency in `batch_auto.py` or normal hill climbing
- No fully autonomous commits of new permuter rules
- No attempt to automate fixes that clearly require header, file-order, or semantic refactors outside the current permuter scope

## Why This Is The Right MVP

It fits the actual leverage point in this repo:

- The project already has a strong deterministic permuter framework
- Many successful fixes repeat across functions and units
- The expensive part is pattern discovery and generalization
- New deterministic patterns compound in value over time

It also avoids the main failure modes of an online advisor:

- brittle free-form edits
- per-function API dependence
- hard-to-measure novelty claims
- expensive repeated calls on cases the permuter already covers

## Product Shape

The offline pattern compiler is a pipeline, not a runtime subsystem.

```text
git history / notes / docs
          |
          v
   example extraction
          |
          v
   normalization + labeling
          |
          v
   AI analysis
   - cluster similar fixes
   - infer common transformation
   - identify detection signals
   - draft deterministic rule shape
          |
          v
   review artifact
   - proposed pattern or rule expansion
   - candidate fixtures
   - risks / exclusions
          |
          v
   engineer implements pattern + tests
          |
          v
   validation on historical examples + live sweep
```

## Primary Outputs

For each discovered pattern family, the system should produce a reviewable artifact with:

- pattern name
- concise description of the codegen lever
- representative historical examples
- source-side detection heuristics
- diagnosis-side trigger signals
- proposed transformation shapes
- exclusions and known failure cases
- recommendation:
  - new pattern
  - extend existing pattern
  - docs only
  - not automatable

The key is that the output is aimed at an engineer writing Python pattern code and tests.

## Inputs

### Required

- git commits that changed decomp source
- file diffs for those commits
- function-level before/after source
- match percentage delta if available

### High-value optional

- commit messages
- PR descriptions
- session notes / MEMORY docs
- objdiff diagnosis before the fix
- Ghidra output captured near the fix
- existing pattern docs in `docs/decomp/patterns/`

## Architecture

### 1. Example Extractor

Build a tool that scans git history and extracts likely decomp-fix examples.

Selection heuristics:

- commit touched source under decomp-managed units
- small function-body-local edits
- commit improved match percentage or changed verdict
- commit message references match/permuter/decomp/pattern/fix

Output:

```json
{
  "commit": "abc123",
  "file": "src/system/rndobj/Text.cpp",
  "function": "RndText::WrapText",
  "before_source": "...",
  "after_source": "...",
  "changed_lines": [[45, 52]],
  "commit_message": "permuter: fix bool materialization in WrapText",
  "metadata": {
    "unit": "system/rndobj/Text",
    "verdict_before": "AT_LIMIT",
    "verdict_after": "WORKABLE",
    "percent_before": 58.3,
    "percent_after": 72.1
  }
}
```

This stage should stay deterministic and auditable.

### 2. Normalizer

Normalize raw examples into a canonical representation so similar fixes can be compared.

The normalizer should not treat raw source diff as the main truth. The right canonical unit is the **compiler effect** of the edit.

Source diff alone is too shallow:

- it overfits to syntax
- it misses when different source edits trigger the same codegen behavior
- it does not tell us why the edit helped matching

Assembly diff alone is also insufficient:

- permuter rules must still fire on source structure
- assembly-only clustering loses the source-side trigger we need to implement the rule

The normalized representation should therefore be a hybrid record with three views:

1. **Source edit view**
   - what changed in the function AST
   - which source anchors were touched
   - what high-level edit shape occurred
2. **Compiler effect view**
   - what changed in generated code because of the edit
   - which opcode families appeared or disappeared
   - whether branch structure, prologue pressure, or instruction ordering changed
3. **Target-relative improvement view**
   - which mismatch signals existed before
   - which signals disappeared after
   - how much match percentage improved

This gives the AI a representation centered on "what lever changed compiler behavior" rather than "what line text changed."

#### Normalization pipeline

```text
historical fix example
    |
    v
extract before/after function source
    |
    v
build before revision and after revision
    |
    v
collect objdiff before vs target
collect objdiff after vs target
optional objdiff before vs after
    |
    v
derive source edit shape + compiler effect signature
    |
    v
store normalized example
```

#### What the normalizer computes

- isolate function-local edits
- strip irrelevant whitespace/comment noise
- identify AST nodes touched
- classify edit shape:
  - operator change
  - cast insertion
  - declaration move
  - block reorder
  - condition restructuring
  - temp extraction/elimination
  - literal type change
- compute before/after diagnosis summaries relative to the target binary
- compute a **compiler effect signature**
- link the effect signature to one or more known compiler levers when possible

#### Compiler effect signature

The compiler effect signature is the canonical normalization artifact.

It is a compact summary of how the edit changed generated code, for example:

- branch count delta
- introduced opcode families
- removed opcode families
- comparison signedness changes
- prologue save-count delta
- control-flow shape delta
- instruction-cluster changes
- call appearance/disappearance
- stack-slot or lifetime pressure changes when observable

Example:

```json
{
  "edit_shape": "bool_cast_on_rhs_of_and",
  "source_anchors": {
    "node_types": ["binary_expression", "cast_expression"],
    "operators": ["&&", ">"]
  },
  "compiler_effect": {
    "branch_count_delta": -1,
    "introduced_opcodes": ["subfc", "eqv", "addze", "clrlwi"],
    "removed_opcodes": ["ble"],
    "control_flow_shape": "short_circuit_to_materialized_bool",
    "prologue_delta": {
      "gpr": 0,
      "fpr": 0
    }
  },
  "target_relative": {
    "before_match": 87.0,
    "after_match": 100.0,
    "resolved_diagnosis_signals": ["bool_materialization_sequence"]
  }
}
```

The normalization target is not "identical source edits." It is "equivalent compiler effects with compatible source triggers."

#### Normalized example schema

The stored example should have explicit sections for source, assembly effect, and target-relative diagnosis.

```json
{
  "example_id": "wraptext_boolmat_001",
  "commit": "abc123",
  "symbol": "RndText::WrapText",
  "unit": "system/rndobj/Text",
  "source_file": "src/system/rndobj/Text.cpp",
  "source_edit": {
    "changed_lines": [[67, 67]],
    "ast_edit_shapes": ["cast_insertion", "condition_rewrite"],
    "anchors": [
      {
        "kind": "binary_expression",
        "operator": "&&"
      },
      {
        "kind": "binary_expression",
        "operator": ">"
      }
    ],
    "before_snippet": "if (a && x > 1) { ... }",
    "after_snippet": "if (a && (bool)(x > 1)) { ... }"
  },
  "compiler_effect": {
    "opcode_families_added": ["subfc", "eqv", "addze", "clrlwi"],
    "opcode_families_removed": ["ble"],
    "comparison_mode_delta": null,
    "branch_count_delta": -1,
    "cluster_delta": -1,
    "control_flow_shape_delta": "short_circuit_to_materialized_bool",
    "prologue_delta": {
      "gpr": 0,
      "fpr": 0
    }
  },
  "target_relative": {
    "percent_before": 87.0,
    "percent_after": 100.0,
    "verdict_before": "WORKABLE",
    "verdict_after": "COMPLETE",
    "diagnosis_signals_before": [
      "branch_mismatch",
      "bool_materialization_sequence"
    ],
    "diagnosis_signals_resolved": [
      "bool_materialization_sequence"
    ]
  },
  "lever_hypotheses": [
    {
      "lever": "boolean_materialization",
      "confidence": 0.95
    }
  ]
}
```

#### Source diff vs assembly diff

For this project:

- source diff is a supporting signal
- assembly effect is the primary normalization signal
- target-relative diagnosis resolution is the ground-truth value signal

That means historical examples should ideally be rebuilt and rescored so the system can compare:

- before source vs target
- after source vs target
- optionally before source vs after source

This also means examples that cannot be rebuilt should be retained, but marked lower confidence.

#### Compiler lever catalog

Do not try to model the compiler globally. That is too broad and not actionable.

Instead, maintain a small catalog of **compiler levers**: observable source-level changes that tend to produce stable codegen effects.

Examples:

- boolean materialization
- signed vs unsigned compare selection
- short-circuit vs nested-branch shape
- variable lifetime affecting callee-saved register pressure
- float literal address-caching vs immediate/FPR usage
- declaration order affecting register allocation
- block movement that changes scheduling and stack pressure

Each lever should capture:

- source-side triggers
- assembly signatures
- diagnosis signals
- exclusions
- known examples
- current automation status:
  - covered
  - partially covered
  - uncovered
  - not automatable

This should live in both:

- human docs, for shared understanding
- machine-readable data, for normalization and clustering

Suggested locations:

- `docs/decomp/compiler-levers/*.md`
- `scripts/permuter/data/compiler_levers.yaml`

The normalizer should map each example to one or more candidate levers with confidence. The AI analyst then reasons over those hypotheses rather than starting from scratch every time.

### 3. AI Analyst

This is where AI is useful.

Given a batch of normalized examples, ask the model to:

- cluster examples that share the same underlying codegen mechanism
- distinguish superficial syntax differences from real pattern differences
- map clusters to existing pattern docs/rules where possible
- identify candidate trigger signals from objdiff diagnosis and source AST
- propose deterministic rule boundaries
- call out examples that are likely not automatable

The model should not output final Python code as the primary artifact. It should output a pattern design brief.

Example output:

```json
{
  "cluster_name": "bool_materialization_rhs_of_and",
  "existing_pattern": "bool_materialize",
  "recommendation": "extend_existing",
  "summary": "Wrapping the RHS comparison of a short-circuit && with (bool) triggers branchless boolean materialization while preserving short-circuit behavior.",
  "source_signals": [
    "binary_expression with operator &&",
    "RHS is comparison expression"
  ],
  "diagnosis_signals": [
    "target contains subfc/eqv/addze sequence",
    "control-flow mismatch around short-circuit branch"
  ],
  "transforms": [
    "wrap RHS comparison in (bool)"
  ],
  "exclusions": [
    "skip if RHS already explicitly cast",
    "skip if semantics would change due to overloaded operators"
  ],
  "supporting_examples": ["ex1", "ex7", "ex12"]
}
```

### 4. Review Artifact Generator

Convert AI analysis into a human-facing review doc or JSON report.

For each proposed pattern, include:

- confidence
- novelty
- estimated ROI
- number of supporting examples
- likely implementation complexity
- suggested test fixture list
- suggested benchmark functions for validation

This is the decision point for whether engineering time should be spent.

### 5. Engineer-in-the-Loop Implementation

An engineer reviews the artifact and decides one of:

- implement new pattern
- expand existing pattern
- update docs only
- reject as non-generalizable

The implementation itself remains deterministic and test-driven.

### 6. Validation

Every accepted proposal must prove itself against:

- historical examples used to derive it
- nearby holdout examples from history that were not shown to the model
- a live batch sweep on relevant current functions

If it only matches the training examples and does not generalize, it was pattern overfitting, not a successful compiler insight.

## High-Level Implementation Groundwork

This project should start by building deterministic plumbing first. The AI layer only becomes useful once the corpus and normalization artifacts are reliable.

### Data model

Recommended core artifact types:

- `RawHistoryExample`
  - one candidate fix mined from history
- `BuiltExample`
  - a raw example with reconstructed before/after build artifacts
- `NormalizedExample`
  - the canonical source + compiler-effect + target-relative record
- `LeverCatalogEntry`
  - one known compiler lever and its metadata
- `PatternProposal`
  - one AI-generated design brief for deterministic automation

### Suggested module layout

```text
scripts/permuter/offline_pattern_compiler/
  extract_history.py
  rebuild_examples.py
  normalize_examples.py
  classify_levers.py
  cluster_examples.py
  generate_report.py
  schemas.py
  storage.py

scripts/permuter/data/
  compiler_levers.yaml

artifacts/offline_pattern_compiler/
  raw_examples/
  built_examples/
  normalized_examples/
  reports/
```

### Stage 1: Historical example extraction

Responsibilities:

- walk git history over a bounded range
- find candidate source commits
- isolate likely function-local fixes
- capture before/after function bodies
- store deterministic raw examples

Key requirement:

This stage must be reproducible from git SHA inputs alone.

### Stage 2: Rebuild and score examples

Responsibilities:

- checkout or materialize before/after source states
- build the affected object
- run objdiff against the current target
- capture diagnosis summaries for before and after states
- record percent delta and resolved mismatch signals

This stage is essential because many important historical fixes are only meaningful when viewed through the target-relative assembly delta.

### Stage 3: Normalize examples

Responsibilities:

- parse source into AST
- extract changed node kinds and anchor points
- derive edit shapes
- compress before/after diagnosis into compiler effect signatures
- attach lever hypotheses from the lever catalog

The output of this stage should be the main input to AI analysis.

### Stage 4: AI-assisted clustering and proposal generation

Responsibilities:

- group examples by likely underlying lever
- identify whether a cluster maps to an existing pattern
- draft a proposal for:
  - new pattern
  - extension to existing pattern
  - docs-only pattern
  - non-automatable bucket

This stage should consume normalized examples in batches, not raw diffs.

### Stage 5: Human review and deterministic implementation

Responsibilities:

- review generated proposals
- select high-ROI candidates
- implement or extend pattern code
- add focused tests from supporting examples

### Stage 6: Holdout and live validation

Responsibilities:

- test against withheld historical examples
- test against current live functions in relevant diagnosis categories
- confirm the proposal generalizes beyond the original cluster

## Implementation Principles

### Build a dataset first

Do not start with prompts. Start with the corpus, schema, and effect signatures.

### Prefer effect-family features over raw instruction streams

Raw instruction listings are too noisy as the primary clustering representation. Normalize them into effect families:

- branch polarity changes
- compare signedness changes
- prologue pressure changes
- block order changes
- call-shape changes
- literal materialization changes

### Keep confidence and provenance everywhere

Every derived field should carry enough provenance to audit:

- source SHA
- build success/failure
- diagnosis source
- confidence for lever mapping
- whether the example was fully rebuilt or only partially inferred

### Separate pattern discovery from pattern implementation

The offline compiler should produce design briefs, not land code automatically.

## First Concrete Deliverables

The first implementation pass should aim to produce these artifacts:

1. `NormalizedExample` schema checked into the repo
2. `compiler_levers.yaml` with an initial seed list of known levers
3. history extractor over a bounded commit range
4. rebuild-and-score pipeline for extracted examples
5. first normalized corpus of 50-100 examples
6. first report showing:
   - repeated clusters
   - likely existing-pattern coverage
   - likely deterministic gaps

That is enough to validate the project direction before adding any sophisticated AI analysis.

## MVP Scope

The MVP should be intentionally narrow.

### In Scope

- mining a bounded set of recent git history
- extracting 50-200 candidate fixes
- clustering them into recurring pattern families
- producing human-reviewable design briefs
- converting 1-3 high-confidence proposals into real permuter changes

### Out of Scope

- full repo history mining on day 1
- automatic code generation and commit creation
- online advisory during hill climbing
- analysis of fixes that require header or whole-file structural edits

## Recommended MVP Phases

### Phase 0: Corpus Build

Build the historical example corpus.

Deliverables:

- extractor script
- normalized example schema
- first dataset of recent fix commits

Success criteria:

- at least 50 usable function-local examples
- less than 20% obvious junk examples in the corpus

### Phase 1: Pattern Discovery Reports

Run AI over the corpus and produce cluster reports.

Deliverables:

- ranked candidate pattern families
- mapping of examples to existing permuter rules
- explicit list of likely gaps

Success criteria:

- at least 5 plausible pattern proposals
- at least 2 proposals judged genuinely useful by a permuter maintainer

### Phase 2: Human Implementation Trial

Take the top 1-3 proposals and implement them manually.

Deliverables:

- pattern code or rule expansions
- regression tests
- validation report against holdout examples

Success criteria:

- at least 1 proposal generalizes into a real, shippable deterministic improvement

### Phase 3: Steady-State Workflow

Run the compiler periodically against new history.

Deliverables:

- recurring report cadence
- backlog of candidate pattern work
- feedback loop from accepted/rejected proposals

## Evaluation

Measure the project by engineering outcomes:

- number of accepted pattern proposals
- number of new deterministic patterns shipped
- increase in permuter hit rate on relevant diagnosis categories
- number of repeated manual fix types converted into automation
- reduction in time-to-pattern from "first successful manual fix" to "landed deterministic rule"

Do not measure success by:

- number of model-generated ideas
- clustering quality in isolation
- token counts

## Key Design Principles

### AI proposes abstractions, not production edits

The model is best used to compress many historical fixes into a reusable theory. That theory is then implemented in code by a human.

### Ground everything in real history

Use git history as the source of truth. If a proposed pattern is not backed by repeated real fixes, it is speculation.

### Keep the deterministic system as the product

The shipped artifact is a better permuter, not an AI workflow.

### Require holdout validation

If a candidate pattern cannot explain new examples outside the prompt set, it is not ready.

## Missing Pieces To Build

- a commit-to-function example extractor
- a rebuild-and-score pipeline for before/after historical examples
- a normalized decomp-fix dataset format
- a compiler effect signature schema
- a compiler lever catalog
- a small taxonomy of edit shapes and diagnosis signals
- a report format for pattern proposals
- a benchmark set of historical fixes reserved for holdout validation

## Risks

### 1. History is noisy

Many commits will mix refactors, style cleanup, and decomp fixes.

Mitigation:

- prefer small commits
- prefer function-local diffs
- filter aggressively
- keep extraction deterministic and inspectable

### 2. AI clusters by syntax, not mechanism

The model may group superficially similar edits that are not driven by the same compiler behavior.

Mitigation:

- include diagnosis signals when available
- require supporting examples across multiple functions
- validate on holdout examples

### 3. Pattern proposals are too broad

The system may recommend rules that overfire and generate low-value variants.

Mitigation:

- require explicit exclusions
- require diagnosis-side triggers
- test on negative examples

### 4. The output is still too vague to implement

Mitigation:

- force the artifact format to include:
  - source signal
  - diagnosis signal
  - transform shape
  - exclusions
  - example fixtures

## Alternatives Considered

### Online AI advisor

Rejected as the MVP because it creates per-function runtime dependence and does not compound into deterministic infrastructure.

### AI-generated pattern code directly

Possible later, but not for MVP. It skips the important design-review step and makes it too easy to land brittle heuristics.

### Purely manual mining of git history

Valuable but lower leverage. Humans are good at validating patterns, but not at scanning hundreds of fixes for latent common structure.

## Open Questions

- What subset of git history has the highest signal-to-noise ratio for initial mining?
- Do we have enough historical metadata to recover before/after match percentages reliably?
- Should the first corpus include only merged fixes that reached 100%, or also partial improvements?
- How should we label examples that changed behavior for reasons unrelated to codegen?
- When should a proposal become "docs only" versus "implement a real pattern"?
- Should we allow the model to draft test fixtures, or only identify candidate ones?

## Recommendation

Build this before any online AI advisor.

It is lower risk, easier to evaluate, and much more aligned with the long-term goal of decompiling DC3 efficiently. If it works, it strengthens the permuter permanently. If it fails, it will still leave behind useful structured history, better pattern docs, and a clearer view of which fixes truly resist deterministic automation.
