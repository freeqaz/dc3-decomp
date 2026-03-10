# Synthesis Engine Roadmap

This document describes a longer-term direction for turning the permuter into a
search-and-validation engine for source transformations.

It builds on the existing permuter roadmap in
`docs/plans/permuter/ARCHITECTURE_ROADMAP.md` and the beam-search design in
`docs/plans/permuter/BEAM_SOLVER.md`.

The core idea is to stop treating the permuter as primarily a bag of variant
generators and instead treat it as a constrained search system with:

- a model of what the target likely wants
- a first-class notion of searchable program state
- multiple proposal sources
- strong validation and suppression
- explicit budget management

## Goal

The goal is to build an engine that can explore a large space of possible
source transformations while staying:

- strongly guided by target-side evidence
- aggressively filtered by correctness and safety checks
- stateful across multiple rewrite steps
- inspectable enough that failures are debuggable

This is not “try more random edits.” It is an attempt to combine:

- diagnosis-guided search
- synthesis-style proposal generation
- translation-validation-style acceptance
- search policies that can survive local maxima

## Why This Direction

The current permuter is already stronger than a naive local rewriter, but it
still has a mostly proposal-centric architecture.

Today, the system is good at:

- generating local variants
- using some guidance (`ghidra`, `m2c`, `rb3`)
- scoring aggressively with objdiff
- learning limited pattern/tag history

It is still weak at:

- carrying multiple coherent source states at once
- understanding target intent as structured facts rather than raw text blobs
- making deterministic guidance-backed proposals a first-class search input
- verifying semantic plausibility beyond “it builds and scores better”
- remembering failure/success at the right granularity

If the long-term aim is a real transformation engine, those are the gaps to
close.

## Engine Model

The engine should be organized around five layers.

### 1. Target Facts

The engine needs a normalized, machine-usable target model derived from:

- objdiff diagnosis
- Ghidra decompilation
- m2c output
- RB3/source analogs
- future semantic summaries

These should become structured facts such as:

- likely terminal call ordering
- likely control-flow shape
- likely temp/live-range pressure points
- likely return/guard/switch form
- likely no-touch zones
- confidence and conflict markers across guidance sources

Without this layer, the engine keeps reinterpreting raw guidance text in
pattern-local code.

### 2. Searchable Program State

The engine should search over real reparsed states, not just source blobs.

Each state should carry:

- source bytes
- reparsed AST / `FunctionContext`
- auxiliary file edits when relevant
- score and diagnosis
- structural tags
- provenance chain
- lineage-local failure history
- state-derived guidance summaries

This is the minimum substrate for a true non-greedy search policy.

### 3. Proposal Market

All candidate-generation mechanisms should enter through one abstraction.

Proposal sources should include:

- ordinary patterns
- composed pairs and adaptive chains
- constrained synthesis
- deterministic guidance-backed edits
- later cross-unit/header-backed edits

This is the correct long-term interpretation of `--constrained`: not a mode,
but one producer in a broader proposal market.

### 4. Validator Stack

Acceptance should not rely on objdiff alone.

The engine should have a staged validator stack:

1. cheap structural/syntactic validity
2. compile/build success
3. objdiff improvement
4. mismatch-class improvement
5. stronger semantic checks where feasible

Over time, the engine should gain translation-validation-like checks for narrow
classes of rewrites, even if that starts with partial or bounded validation.

### 5. Search Controller

The controller should allocate budget across states and proposal sources.

The most likely near-term fit is:

- beam-style best-first search
- state re-diagnosis after surviving rewrites
- diversity preservation by structure, not only edit size
- lineage-local suppression instead of blunt global suppression

Greedy remains useful as a cheap baseline. Cross-unit exploration remains a
separate, opt-in tier.

## Biggest Expected Wins

If built well, the biggest gains are likely to come from:

### Search Before More Rules

The largest broad gain is probably not one more pattern, but a search kernel
that can:

- keep multiple plausible branches alive
- survive neutral intermediate rewrites
- re-rank after state changes
- spend build budget on good successors instead of random recombination

### Deterministic Guidance-Backed Proposals

The next biggest gain is to extract more deterministic proposals from target
facts instead of relying only on local pattern heuristics.

This includes:

- control-flow reshaping
- call-order proposals
- temp/live-range proposals
- typed/cast/sign proposals
- narrow register-pressure rewrites

### Better Semantic Filtering

Many bad variants should die before full scoring.

The engine needs stronger local summaries for:

- read/write effects
- aliasing risk
- call-side-effect classes
- path termination
- expression purity

This is one of the highest-leverage areas because it increases both quality and
effective search budget.

### Advanced m2c Integration

Advanced m2c support is likely a multiplier, not the core engine.

Its highest-value roles are:

- second-opinion structural guidance next to Ghidra
- conflict detection that lowers confidence in weak guidance
- better call ordering / gating hints
- better temp/live-range shape hints
- better condition-structure hints

This is broadly useful across many single-function targets.

### Advanced Cross-Unit Integration

Advanced cross-unit search is high ceiling but narrower in applicability.

It matters most for:

- inline/header tail-call and return-shape cases
- shared inline helper rewrites
- one-to-many source changes that benefit several caller functions

This should stay isolated, risk-aware, and probably worktree-backed.

## Additional Design Principles

The research points to a few design principles that should shape the engine
even before specific implementations exist.

### Preserve Novel Partial Progress

The engine should not keep only the globally best states.

It should also preserve states that improve distinct mismatch regions or
distinct structural hypotheses, even when they are not currently the top scalar
score. This is especially important for byte-equivalent search, where a branch
may be globally mediocre but uniquely good at one region of the function.

### Convert Guidance Into Facts, Not Special Cases

The strongest use of decompiler/compiler guidance is not “run more heuristics.”

It is to convert that guidance into:

- concrete structural hypotheses
- deterministic proposal families
- confidence/conflict markers
- verifier expectations

This is the architectural step that turns guidance from pattern-local tribal
knowledge into search infrastructure.

### Keep Equality-Space Exploration Local

Equality-saturation-style exploration is attractive, but it should be scoped to
small regions first.

Good early targets:

- condition expressions
- arithmetic/bitwise simplifications
- return-value reshaping
- short statement clusters

Whole-function equality-space exploration is likely too expensive and too hard
to control in phase 1.

### Use A Verifier Ladder, Not A Single Oracle

No one verifier is likely to be strong and cheap enough for the whole engine.

The design should assume layered acceptance:

- syntactic validity
- structural fact agreement
- build success
- objdiff delta
- bounded semantic checks
- selective expensive validation for top candidates

That is much closer to translation-validation practice than to trusting a
single heuristic score.

## Prior Art

The best model is a hybrid, not a copy of one system.

### STOKE

Search over program rewrites with a cost function and strong validation.

Why it matters here:

- shows that nontrivial program-search can find wins humans miss
- reinforces that search quality depends on the objective and validation
- reminds us that large search spaces need aggressive guidance

Reference:

- Schkufza, Sharma, Aiken. "Stochastic Superoptimization." ASPLOS 2013.
  https://cs.stanford.edu/people/eschkufz/docs/asplos_13.pdf

### Souper

Local superoptimization through synthesis and solver-backed validation.

Why it matters here:

- shows the value of deterministic synthesis over blind mutation
- suggests that local rewrite discovery and proof can scale when scoped well
- supports reframing constrained search as a proposal generator

Reference:

- Sasnauskas et al. "Souper: A Synthesizing Superoptimizer." 2017.
  https://arxiv.org/abs/1711.04422

### Sketch / CEGIS

Search over partial programs with validation-driven refinement.

Why it matters here:

- the engine can search much better when given structured holes, not just free
  mutation
- counterexamples and failed checks should prune families of bad candidates,
  not just one concrete variant
- the right analogy for `--constrained` is “structured synthesis under
  guidance,” not “special round-1 mode”

References:

- Solar-Lezama. "Program Synthesis by Sketching." PhD thesis, 2008.
  https://people.csail.mit.edu/asolar/papers/thesis.pdf
- Solar-Lezama. "The Sketching Approach to Program Synthesis." 2009.
  https://people.csail.mit.edu/asolar/papers/Solar-Lezama09.pdf

### Equality Saturation / egg

Keep many equivalent forms alive, then extract using a cost model.

Why it matters here:

- committing too early is often the real problem in rewrite systems
- representation matters: a good shared state space can be more powerful than a
  long list of handcrafted sequences
- extraction cost models are at least as important as rewrite generation

References:

- Tate et al. "Equality Saturation: A New Approach to Optimization." 2009.
  https://www.cs.cornell.edu/~ross/publications/eqsat/
- Willsey et al. "egg: Fast and Extensible Equality Saturation." 2020.
  https://arxiv.org/abs/2004.03082

### Guided Equality Saturation

Use guidance or sketches to make equality-saturation search tractable.

Why it matters here:

- guidance can narrow an otherwise explosive rewrite space
- it supports the idea of using Ghidra/m2c/diagnosis as guides, not just as
  passive hints
- it gives a concrete model for “proposal freedom constrained by target facts”

Reference:

- Steuwer et al. "Guided Equality Saturation." POPL 2024.
  https://steuwer.info/files/publications/2024/POPL-Guided-Equality-Saturation.pdf

### Translation Validation / Alive2

Verify transformations after the fact instead of blindly trusting them.

Why it matters here:

- correctness checks should be a first-class subsystem
- many transformations can be validated more cheaply than fully synthesized
- bounded or partial validation is still very valuable in practice

References:

- Stepp, Tate, Lerner. "Equality-Based Translation Validator for LLVM." CAV
  2011. https://www.cs.cornell.edu/~ross/publications/eqsat/eqsat_stepp_cav11.pdf
- Lopes et al. "Alive2: Bounded Translation Validation for LLVM." PLDI 2021.
  https://web.ist.utl.pt/nuno.lopes/pubs/alive2-pldi21.pdf
- Alive2 project:
  https://github.com/AliveToolkit/alive2

### Compiler-Aware Decompilation

Recovered structure is often wrong because compiler transformations distorted
the source shape before the decompiler saw it.

Why it matters here:

- supports the need for a canonical target-facts layer
- argues that “what the compiler likely did” should be first-class evidence
- reinforces that decompiler ASTs are not ground truth

Reference:

- Basque et al. "Ahoy SAILR! There is No Need to DREAM of C: A Compiler-Aware
  Structuring Algorithm for Binary Decompilation." USENIX Security 2024.
  https://adamdoupe.com/publications/sailr-usenix2024.pdf

### Decompiler Validation And Testing

Independent semantic checking matters because decompilers are often wrong in
ways that are not obvious from structure alone.

Why it matters here:

- agreement with one decompiler is not enough
- the verifier stack needs independent checks
- semantic-differencing-style validation is likely to catch issues that pure
  structural guidance will miss

Reference:

- Zou et al. "D-Helix: A Generic Decompiler Testing Framework Using Symbolic
  Differentiation." USENIX Security 2024.
  https://www.usenix.org/system/files/usenixsecurity24-zou.pdf

### Byte-Equivalent Decompilation Search

Search systems that target byte equivalence have a directly relevant lesson:
preserve states that match different unique parts of the target.

Why it matters here:

- diversity should be based on mismatch-class or region coverage, not just a
  scalar score
- global best-first alone can discard the branch that uniquely explains one
  important region

Reference:

- Schulte et al. "Evolving Byte-Equivalent Decompilation from Big Code."
  https://www.cs.unm.edu/~eschulte/data/bed-full.pdf

### Search-Guided Synthesis And Repair

Search quality improves when the system learns which partial programs and which
proposal families are likely to pay off.

Why it matters here:

- supports lineage-local memory and proposal ranking
- suggests that search history should become a first-class signal
- reinforces that proposal ordering matters as much as proposal availability

References:

- Alur et al. "Accelerating Search-Based Program Synthesis using Learned
  Probabilistic Models." PLDI 2018.
  https://www.cis.upenn.edu/~alur/PLDI18.pdf
- Shi et al. "CrossBeam: Learning to Search in Bottom-Up Program Synthesis."
  https://arxiv.org/abs/2203.10452
- Katis et al. "Counterexample Guided Inductive Synthesis Modulo Theories."
  CAV 2018.
  https://link.springer.com/chapter/10.1007/978-3-319-96145-3_15

## Tools And Systems To Study

These are not all immediate dependencies, but they are worth mapping to engine
subsystems.

### Likely High-Value

- `egg` / `egglog` for local equivalence-space exploration
- Z3/SMT-backed validation ideas by analogy with Alive2 and CEGIS(T)
- `angr` for IR-lifting and semantic-analysis experiments:
  https://docs.angr.io/advanced-topics/ir
- Coccinelle for C-level semantic patch ideas:
  https://coccinelle.gitlabpages.inria.fr/website/
- Clang Transformer / LibTooling for deterministic AST-backed rewrites:
  https://clang.llvm.org/docs/ClangTransformerTutorial.html

### Useful Secondary Opinions

- RetDec:
  https://github.com/avast/retdec
- McSema:
  https://github.com/lifting-bits/mcsema
- VerifOx / CPROVER-family ideas for path-wise symbolic checks:
  https://www.cprover.org/verifox

## Existing Tooling Inventory

These are the project tools and data sources that the synthesis engine builds
on. All paths are relative to the repo root.

### Compiler Trace & Assembly Analysis

| Tool | Path | Role |
|------|------|------|
| invoker.py | [`tools/compiler_trace/invoker.py`](../../tools/compiler_trace/invoker.py) | Wraps cl.exe with project flags, `/FAs` listing generation |
| asm_diff.py | [`tools/compiler_trace/asm_diff.py`](../../tools/compiler_trace/asm_diff.py) | Compiles two variants, normalizes listings, diffs output |
| asm_regmap.py | [`tools/compiler_trace/asm_regmap.py`](../../tools/compiler_trace/asm_regmap.py) | Extracts variable→register assignments from `/FAs` output |
| bsf_trace.py | [`tools/compiler_trace/bsf_trace.py`](../../tools/compiler_trace/bsf_trace.py) | GDB+Valgrind instrumentation of c2.dll register allocator |
| regmap_solver.py | [`tools/compiler_trace/regmap_solver.py`](../../tools/compiler_trace/regmap_solver.py) | Graph coloring simulation from BSF traces or listings |

### Analysis & Mining

| Tool | Path | Role |
|------|------|------|
| mine_patterns.py | [`scripts/analysis/mine_patterns.py`](../../scripts/analysis/mine_patterns.py) | Walks commit history, classifies source patterns per fix |
| diff_inspect.py | [`scripts/analysis/diff_inspect.py`](../../scripts/analysis/diff_inspect.py) | Deep mismatch analysis: diagnose, clusters, regswaps, asm_listing |
| reclassify_at_limit.py | [`scripts/analysis/reclassify_at_limit.py`](../../scripts/analysis/reclassify_at_limit.py) | Bulk re-diagnosis and classification of AT_LIMIT functions |
| compare_progress.py | [`scripts/analysis/compare_progress.py`](../../scripts/analysis/compare_progress.py) | Regression detection between baseline reports |
| ceiling_calculator.py | [`scripts/analysis/ceiling_calculator.py`](../../scripts/analysis/ceiling_calculator.py) | Theoretical match ceiling per unit |
| remaining_work.py | [`scripts/analysis/remaining_work.py`](../../scripts/analysis/remaining_work.py) | Stub analysis in near-complete units |

### Permuter Core

| Tool | Path | Role |
|------|------|------|
| scorer.py | [`scripts/permuter/scorer.py`](../../scripts/permuter/scorer.py) | Build pipeline, 3-layer dedup, parallel scoring |
| types.py | [`scripts/permuter/types.py`](../../scripts/permuter/types.py) | Diagnosis, FunctionContext, Variant, RoundHints dataclasses |
| batch_triage.py | [`scripts/permuter/batch_triage.py`](../../scripts/permuter/batch_triage.py) | 6-category mismatch classification |
| ghidra_preflight.py | [`scripts/permuter/ghidra_preflight.py`](../../scripts/permuter/ghidra_preflight.py) | Unfixable detection before permuting |
| constraint_solver.py | [`scripts/permuter/constraint_solver.py`](../../scripts/permuter/constraint_solver.py) | Deterministic edits from Ghidra + objdiff constraints |
| statement_effects.py | [`scripts/permuter/statement_effects.py`](../../scripts/permuter/statement_effects.py) | Per-statement read/write/call/control-flow analysis |
| hill_climber.py | [`scripts/permuter/hill_climber.py`](../../scripts/permuter/hill_climber.py) | Greedy iterative search (current default) |
| evolutionary.py | [`scripts/permuter/evolutionary.py`](../../scripts/permuter/evolutionary.py) | Population-based genetic search |
| composer.py | [`scripts/permuter/composer.py`](../../scripts/permuter/composer.py) | 2-stage composition + N-stage beam chains |

### Behavioral Verification

| Tool | Path | Role |
|------|------|------|
| unicorn bench.py | [`scripts/unicorn_runner/bench.py`](../../scripts/unicorn_runner/bench.py) | PPC32 emulation benchmark harness |
| unicorn comparator.py | [`scripts/unicorn_runner/comparator.py`](../../scripts/unicorn_runner/comparator.py) | Differential execution: decomp vs original |

### Documentation

| Document | Path | Content |
|----------|------|---------|
| TECHNICAL_NOTES.md | [`docs/decomp/TECHNICAL_NOTES.md`](../decomp/TECHNICAL_NOTES.md) | 38+ compiler behavior patterns |
| MSVC_X360_REGALLOC.md | [`docs/decomp/MSVC_X360_REGALLOC.md`](../decomp/MSVC_X360_REGALLOC.md) | c2.dll register allocator reverse engineering |
| Pattern INDEX.md | [`docs/decomp/patterns/INDEX.md`](../decomp/patterns/INDEX.md) | Master pattern index with ROI rankings |
| unfixable-compiler.md | [`docs/decomp/patterns/unfixable-compiler.md`](../decomp/patterns/unfixable-compiler.md) | 16 hard compiler-level patterns |
| at-limit-systemic.md | [`docs/decomp/patterns/at-limit-systemic.md`](../decomp/patterns/at-limit-systemic.md) | Project-wide systemic unfixable patterns |
| PERMUTER_ROI_ANALYSIS.md | [`docs/decomp/patterns/PERMUTER_ROI_ANALYSIS.md`](../decomp/patterns/PERMUTER_ROI_ANALYSIS.md) | Pattern coverage vs automation analysis |

### Data Sources

| Resource | Path | Content |
|----------|------|---------|
| decomp.db | `decomp.db` | Function registry (symbol, unit, match%, verdict) |
| permuter_cache.db | `permuter_cache.db` | Per-function (symbol, source_md5, score) history |
| report.json | `build/373307D9/report.json` | Current objdiff report (14MB) |
| baselines/ | `build/373307D9/baselines/` | 25 commit-stamped baseline snapshots |
| regswap_manifest.json | [`scripts/regswap_manifest.json`](../../scripts/regswap_manifest.json) | 709 known register swap patterns |

## Companion Documents

Detailed designs for the three new subsystems live in separate docs:

- **[COMPILER_ATLAS.md](COMPILER_ATLAS.md)** — Systematic micro-program
  compilation to map instruction patterns to source constructs. Builds the
  empirical foundation for target-driven proposals.

- **[PATTERN_MINING.md](PATTERN_MINING.md)** — Cross-function transfer
  learning from the 29,842 solved functions. Diagnosis fingerprinting,
  strategy records, similarity search.

- **[INSTRUCTION_ATTRIBUTION.md](INSTRUCTION_ATTRIBUTION.md)** — Connecting
  mismatched instructions to specific source lines via `/FAs` listings.
  Enables surgically targeted edits instead of broad pattern sweeps.

## Proposed Roadmap

The roadmap is organized into three tracks that can advance in parallel: the
**search track** (how the engine explores), the **knowledge track** (what the
engine knows about the compiler), and the **infrastructure track** (how the
engine observes and learns).

### Search Track

#### S1: Search Kernel

Build a beam-style search controller over reparsed single-function states.

Deliverables:

- first-class beam state model
- state-local proposal expansion
- state-local failure memory
- explainable survivor ranking
- replayable logs

#### S2: Constrained Proposal Engine

Replace the current narrow constrained prepass with a reusable proposal source.

Deliverables:

- proposal-source interface
- deterministic edit families from target facts
- lineage-local suppression for constrained proposals
- repeated constrained expansion during search

#### S3: Region-Aware Search

Use instruction attribution to scope and prioritize search.

Deliverables:

- region-aware pattern filtering (only apply patterns to mismatch regions)
- budget allocation proportional to region impact
- region-level improvement tracking across rounds
- region-diverse beam selection

#### S4: Cross-Unit Search Tier

Extend the engine to carefully handle shared-header and multi-symbol states.

Deliverables:

- separate cross-unit sub-search or sub-beam
- risk-aware prioritization
- worktree-backed execution
- multi-symbol validation and attribution

### Knowledge Track

#### K1: Compiler Atlas — Harvest

Convert the 50+ proven patterns from existing documentation into structured
atlas entries. No new compilation needed.

Deliverables:

- atlas schema and storage format
- 50-80 entries from existing TECHNICAL_NOTES.md and pattern docs
- reverse lookup by target instruction pattern

See [COMPILER_ATLAS.md](COMPILER_ATLAS.md) Phase 1.

#### K2: Compiler Atlas — Systematic Exploration

Build micro-program families for each codegen dimension. Compile all variants,
diff, record results.

Deliverables:

- micro-program generator script
- automated compilation and diffing pipeline (extending invoker.py + asm_diff.py)
- 200-400 new atlas entries from ~30 micro-program families
- negative results catalog (source changes that produce identical codegen)

See [COMPILER_ATLAS.md](COMPILER_ATLAS.md) Phase 2.

#### K3: Compiler Atlas — Interaction Discovery

Identify and test compound codegen effects (where two source features interact
to produce a specific instruction pattern).

Deliverables:

- interaction candidate identification from single-dimension atlas
- compound micro-programs for high-value interactions
- compound atlas entries with multi-feature requirements
- documented interaction chains (type + control flow, type + variable lifetime)

See [COMPILER_ATLAS.md](COMPILER_ATLAS.md) Phase 3.

#### K4: Target Facts Layer

Normalize guidance from all sources into reusable structured facts.

Deliverables:

- target-facts datamodel
- Ghidra fact extractor
- m2c fact extractor
- diagnosis fact extractor
- atlas-backed fact enrichment (instruction patterns → source hypotheses)
- confidence/conflict scoring across guidance sources

#### K5: Target-Driven Proposals

Use the atlas and target facts to generate proposals directly from mismatches,
rather than trying all patterns and hoping one hits.

Deliverables:

- mismatch-to-atlas lookup in the proposal pipeline
- atlas-guided proposal generation (targeted source edits)
- integration with beam search as a first-class proposal source
- feedback loop: successful proposals strengthen atlas entries

### Infrastructure Track

#### I1: Instruction Attribution Pipeline

Connect mismatched instructions to specific source lines via `/FAs` listings.

Deliverables:

- robust `/FAs` listing parser (handle inlines, macros, reordering)
- mismatch-to-source-line join with objdiff instruction output
- attributed mismatch reports (line-level, region-level)
- integration with diff_inspect.py (`--attributed` mode)

See [INSTRUCTION_ATTRIBUTION.md](INSTRUCTION_ATTRIBUTION.md) Phases 1-2.

#### I2: Cross-Function Strategy Database

Mine solved functions to build a transfer learning database.

Deliverables:

- diagnosis fingerprint schema
- strategy record and failure record schemas
- retroactive harvest from 25 cached baseline snapshots
- SQLite strategy database (target: 500-2000 records from history)

See [PATTERN_MINING.md](PATTERN_MINING.md) Phase 1.

#### I3: Live Strategy Recording

Extend the permuter to emit strategy records on every run.

Deliverables:

- automatic strategy/failure recording in hill climber and beam solver
- fingerprint computation at baseline time
- database growth with each permuter session

See [PATTERN_MINING.md](PATTERN_MINING.md) Phase 2.

#### I4: Strategy-Boosted Search

Use the strategy database to guide first-round pattern selection.

Deliverables:

- fingerprint similarity search (weighted Hamming)
- historical strategy lookup at round 1
- pattern boosting from k-nearest solved functions
- logging of which historical strategies influenced search

See [PATTERN_MINING.md](PATTERN_MINING.md) Phase 3.

#### I5: Validator Ladder

Add stronger validation and richer acceptance decisions.

Deliverables:

- mismatch-class delta tracking (not just overall score)
- region-level improvement tracking (from attribution)
- state-local semantic plausibility checks
- partial translation-validation-style checks for narrow transforms
- stronger logging on why candidates were rejected

#### I6: Evaluation And Benchmarking

Make search-policy work measurable and improvable.

Deliverables:

- benchmark corpus of plateaued-but-fixable functions
- replayable search traces
- comparative evaluation: greedy vs beam vs atlas-guided
- aggregate pattern effectiveness reports from strategy database
- benchmark categories keyed by mismatch class and structural failure mode

### Recommended Sequencing

The three tracks have some dependencies but are mostly independent:

```
Knowledge:  K1 ──── K2 ──── K3 ──── K4 ──── K5
                                     │        │
Search:     S1 ──────────── S2 ──── S3 ──── S4
                             │       │
Infra:      I1 ──── I2 ──── I3 ──── I4 ──── I5 ──── I6
```

**Immediate priorities** (can start in parallel):

- **K1** (atlas harvest) — pure documentation work, no new code
- **I1** (attribution pipeline) — extends existing tools, high diagnostic value
  even without search integration
- **I2** (strategy database schema + retroactive harvest) — uses existing
  mine_patterns.py output
- **S1** (beam search kernel) — already designed in BEAM_SOLVER.md

**First integration point**: When K1 + I1 + S1 are done, K4 can fuse them
into a target-facts layer that the beam search consumes.

**Second integration point**: When I2 + I3 are done, I4 connects strategy
lookup to the search loop.

## Open Questions

These are the biggest unresolved design questions, organized by subsystem.

### Compiler Atlas

- What storage format? SQLite enables rich queries; JSON/YAML enables easy
  editing and version control. The editorial workflow (human curation of
  entries) suggests a file format; the search-time lookup suggests a database.
  A hybrid may work: YAML source of truth, compiled to SQLite at build time.
- How should confidence work? An entry proven on 10 micro-programs is stronger
  than one proven on 1, but “proven” vs “inferred” may be the more useful
  distinction. Start with tiers (proven/inferred/negative), add counts later.
- How deep should interaction exploration go? Two-way interactions (type +
  operator) are tractable. Three-way interactions (type + operator + lifetime)
  may be combinatorially explosive. Use the single-dimension atlas to identify
  which dimensions actually share instruction patterns before expanding.
- Can we auto-generate micro-programs from the existing pattern docs? Many
  pattern docs already contain before/after code snippets that are essentially
  micro-programs. A parser could extract these and compile them automatically.

### Strategy Database

- How many distinct fingerprint clusters exist in the solved functions? If
  there are ~50 clusters, similarity search is straightforward. If there are
  thousands, we need embedding-based search or hierarchical clustering.
- How should header-driven regressions be handled? A strategy record from
  before a header change may not be valid after it. Options: record expiry,
  header-state hashing, or confidence decay based on age.
- Should the strategy database subsume permuter_cache.db? They store related
  data (per-function scoring history vs cross-function strategy records) and
  could share a schema.

### Instruction Attribution

- How reliable is `/FAs` attribution after `/O1` optimization? MSVC may
  scramble source annotations for heavily optimized code. Test on 20-30
  representative functions to assess noise level.
- Should attribution be stored persistently? It enables cross-session analysis
  but costs space. A cache keyed by (source_hash, compiler_flags) would allow
  reuse without unbounded growth.
- Can we get useful target-side attribution without debug info? Ghidra provides
  decompiled-line-level mapping, and surrounding matched instructions provide
  interpolation anchors. How accurate is this in practice?

### Search & Integration

- Is a beam over reparsed source states enough, or do we eventually want a more
  explicit equivalence representation for some rewrite classes?
- Should region-level improvement be a first-class beam ranking signal, or just
  a diversity tiebreaker?
- How should atlas-driven proposals interact with pattern-driven proposals?
  Should they compete for the same budget, or have separate allocations?
- When multiple systems agree (atlas suggests X, strategy DB suggests X,
  diagnosis suggests X), how aggressively should the engine concentrate budget?

### Validation

- What is the strongest practical validation we can do below full-machine
  semantics? Can we build a useful translation-validation-like layer over
  reduced IR or compiler-emitted listings?
- Can failed validation produce reusable counterexamples that prune families
  of bad candidates, not just one concrete variant?
- Which rewrite classes are worth special-case validation first? (Comparison
  changes and type casts are likely candidates — narrow, well-defined, and
  the atlas provides ground truth.)

### Cross-Unit Search

- Should cross-unit states compete directly with local states, or live in a
  separately budgeted search tier?
- How should we attribute gains and regressions across many affected symbols?
- What risk model should gate shared-header edits?

### Runtime And Tooling

- What caches are essential for acceptable runtime? The atlas lookup should be
  sub-millisecond; strategy DB similarity search should be <100ms; attribution
  compilation adds ~10-15% overhead per listing.
- How should worktree-backed execution integrate with the existing orchestrator
  pool?
- Which external tools are worth adopting directly versus studying only for
  design ideas?

## What Makes This World-Class

No existing decomp project has this combination:

1. **A compiler behavior atlas built from systematic experimentation.** Other
   projects document patterns as they encounter them. This project would have a
   proactively explored, queryable mapping of the compiler's decision space.

2. **Cross-function transfer learning.** Decomp permuters (N64, GC, Wii
   projects) treat each function independently. This project would learn from
   its own history — 29,842 solved functions as training data.

3. **Instruction-level source attribution integrated into search.** Other tools
   do binary diffing (objdiff, decomp.me) but don't close the loop to source
   lines. This project would trace mismatches to specific source expressions
   and apply targeted fixes.

4. **Beam search over program states with diagnosis-guided proposals.** Other
   permuters use random mutation or greedy hill climbing. This project would
   use structured, multi-state search with evidence-based proposal generation.

5. **Deep compiler reverse engineering (BSF tracing, c2.dll analysis)
   integrated into the search loop.** The register allocator behavior is
   already documented at the instruction level. Connecting that knowledge to
   the search engine turns it from documentation into automation.

The individual pieces exist in academia (STOKE for search, Alive2 for
validation, egg for equivalence saturation, SAILR for compiler-aware
decompilation). The novel contribution is combining them into a practical
tool that operates on a real codebase with a real proprietary compiler, using
empirical evidence from 30,000+ solved functions.

## Practical Next Steps

The immediate priorities that can start in parallel:

### Track 1: Atlas Harvest (K1) — 1-2 days

Convert existing pattern documentation into structured atlas entries. No new
compilation needed. This is documentation refactoring with high downstream
value.

Start with the 20 highest-impact patterns from `TECHNICAL_NOTES.md`:
comparison operators, boolean materialization, float literals, FMA ordering,
loop condition subtraction, NOR peephole, fsel templates.

### Track 2: Attribution Pipeline (I1) — 2-3 days

Build the `/FAs` listing parser and mismatch join. The infrastructure
(`invoker.py`, `asm_diff.py`, `diff_inspect.py`) already exists. The new work
is the structured parser, the objdiff join, and the `--attributed` mode.

Test on 20-30 functions spanning different mismatch classes. Assess attribution
reliability under `/O1` optimization.

### Track 3: Strategy Schema + Harvest (I2) — 1-2 days

Define the fingerprint, strategy record, and failure record schemas. Run
mine_patterns.py over the 25 cached baselines and produce the initial strategy
database.

Assess: how many distinct fingerprint clusters exist? What's the distribution
of winning patterns by cluster?

### Track 4: Beam Search (S1) — 3-5 days

Implement the beam search controller from the BEAM_SOLVER.md design. Run it
on a benchmark slice of 50 plateaued-but-fixable functions. Compare against
greedy.

### First Convergence

When K1 + I1 + S1 are done, build K4 (target facts layer) to fuse atlas
lookups and attributed mismatches into the beam search's proposal generation.
This is the moment the engine starts doing target-driven search instead of
blind pattern sweeps.
