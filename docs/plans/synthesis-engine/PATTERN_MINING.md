# Cross-Function Pattern Mining

This document describes how to mine the project's 29,842 solved functions to
build a transfer learning database: when the engine encounters a new function,
it should be able to look up similar previously-solved functions and try their
winning strategies first.

This extends the existing
[`scripts/analysis/mine_patterns.py`](../../scripts/analysis/mine_patterns.py)
(commit-history pattern extraction) and the diagnosis infrastructure in
[`scripts/permuter/types.py`](../../scripts/permuter/types.py) and
[`scripts/permuter/batch_triage.py`](../../scripts/permuter/batch_triage.py).

## The Opportunity

29,842 functions are at 100% match. Each one was solved — either by manual
editing or by the permuter. The source changes that got them to 100% encode
implicit knowledge about how MSVC PPC behaves:

- "Functions with this mismatch profile needed this specific transformation"
- "Functions in this unit tend to have this class of issue"
- "This diagnosis signature responds to declaration reorder 40% of the time"

That knowledge currently lives in:

- Human memory (the MEMORY.md notes)
- Commit messages (sparse, not machine-readable)
- mine_patterns.py output (git-history-based, batch-oriented)
- Permuter cache (per-function score history, not cross-function)

None of these are queryable at search time. The engine cannot ask: "What worked
on similar functions?"

## What Already Exists

- **[`scripts/analysis/mine_patterns.py`](../../scripts/analysis/mine_patterns.py)** —
  walks commit history, diffs consecutive baseline reports, classifies which
  source patterns were applied. This is the closest existing tool but it works
  on historical snapshots, not on live search queries.

- **[`scripts/permuter/batch_triage.py`](../../scripts/permuter/batch_triage.py)** —
  classifies functions into NOISE_ONLY / REGSWAP_ONLY / REGSWAP_PLUS /
  STRUCTURAL / UNFIXABLE / MIXED. This is the diagnosis taxonomy but it does
  not link to winning strategies.

- **[`scripts/analysis/reclassify_at_limit.py`](../../scripts/analysis/reclassify_at_limit.py)** —
  re-diagnoses AT_LIMIT functions with fresh objdiff data. Could be extended to
  also diagnose COMPLETE functions retroactively.

- **[`scripts/analysis/compare_progress.py`](../../scripts/analysis/compare_progress.py)** —
  regression detection between baseline reports. Used for before/after analysis.

- **[`scripts/permuter/types.py`](../../scripts/permuter/types.py)** — defines
  `Diagnosis`, `FunctionContext`, `Variant`, `RoundHints` and other core
  dataclasses that the fingerprinting system would extend.

- **[`scripts/permuter/scorer.py`](../../scripts/permuter/scorer.py)** — build
  pipeline with 3-layer dedup (source hash, persistent SQLite cache, obj hash).
  The persistent cache in `permuter_cache.db` contains raw scoring data.

- **`decomp.db`** — function metadata (symbol, unit, match%, verdict). The
  central function registry.

- **`build/373307D9/baselines/`** — 25 commit-stamped baseline report
  snapshots (each ~14MB). The raw material for retroactive mining.

## Design

### Diagnosis Fingerprinting

The key insight is that functions cluster by their mismatch profiles. Two
functions with similar diagnosis signatures are likely to need similar fixes.

A diagnosis fingerprint is a compact, comparable representation of a function's
mismatch profile. It should capture:

```
DiagnosisFingerprint:
  prologue_delta: int          # GPR save count difference (ours - target)
  fpr_delta: int               # FPR save count difference
  has_regswap: bool            # any register swap pairs
  regswap_type: str            # "callee_only", "volatile_only", "mixed"
  cluster_count: int           # number of contiguous insert/delete regions
  cluster_sizes: list[int]     # sorted sizes of clusters (top 3)
  dominant_diff_ops: set[str]  # top 3 diff op categories
  offset_pattern: str          # "uniform_shift", "scattered", "none"
  noise_ratio: float           # fraction of diffs that are addr_reloc noise
  instruction_count: int       # total target instructions (function size)
  has_switch: bool             # switch table present
  has_float_ops: bool          # any FPR instructions
  call_count_delta: int        # difference in call instruction count
```

Two functions with the same fingerprint (or fingerprints within a distance
threshold) are in the same "mismatch class."

### Strategy Records

For each solved function, record what fixed it:

```
StrategyRecord:
  symbol: str
  unit: str
  initial_fingerprint: DiagnosisFingerprint
  initial_match_pct: float
  final_match_pct: float
  winning_patterns: list[str]     # ordered sequence of applied patterns
  winning_tags: set[str]          # structural tags from winning variants
  rounds_to_solve: int
  total_variants_tried: int
  source_diff_summary: str        # high-level description of what changed
  timestamp: datetime
```

These records form the strategy database. Given a new function's fingerprint,
the engine queries for the nearest known strategy records and tries those
patterns first.

### Retroactive Mining

Most of the 29,842 solved functions were fixed manually, not by the permuter.
To build strategy records for these, we need retroactive analysis:

1. **Git archaeology**: For each function that reached 100%, find the commit
   that achieved it. Diff the source before and after. Classify the change
   using the same pattern taxonomy the permuter uses.

2. **Synthetic re-diagnosis**: For functions fixed long ago, we can't recover
   the original diagnosis (the function was already at lower match% in the
   earliest baseline). But we can examine the source diff and infer which
   pattern category it falls into:
   - Declaration reorder → `declaration_reorder`
   - Type change (signed/unsigned) → `signed_unsigned`
   - Control flow restructure → `branch_polarity`, `guard_to_nested`, etc.
   - Comparison change → `comparison_equivalence`
   - Float literal change → `float_double_literal`

3. **Baseline pair analysis**: The 25 cached baseline reports in
   `build/373307D9/baselines/` provide before/after snapshots.
   `mine_patterns.py` already diffs these. Extend it to produce
   `StrategyRecord` objects instead of summary counts.

### Similarity Search

Given a new function's diagnosis fingerprint, find the k-nearest strategy
records. Distance metric options:

**Weighted Hamming distance** (simplest):
- Each fingerprint field contributes a weighted 0/1 match score
- High weight: prologue_delta, cluster_count, dominant_diff_ops
- Low weight: instruction_count, noise_ratio
- Fast, interpretable, good enough for a first pass

**Embedding distance** (richer):
- Encode fingerprints as fixed-length vectors
- Use cosine similarity or L2 distance
- Allows continuous features (noise_ratio, match_pct) to contribute smoothly
- More complex, needs tuning

Start with weighted Hamming. Graduate to embeddings if the simpler approach
plateaus.

### Query-Time Integration

When the permuter starts working on a function:

1. Compute diagnosis fingerprint from baseline objdiff
2. Query strategy database for k-nearest records (k=5-10)
3. Extract winning patterns from those records
4. Boost those patterns in the first round's budget allocation
5. If multiple records agree on the same pattern sequence, try that sequence
   first as a composed chain

This does not replace the existing diagnosis-driven pattern selection. It adds
a historical signal on top of it. When the diagnosis says "try 8 patterns" and
the strategy database says "3 of those 8 have historically worked on similar
functions," the engine should try those 3 first.

### Confidence Calibration

Not all strategy records are equally informative:

- A record from a function with the same unit and similar instruction count is
  highly relevant
- A record from a distant unit with very different structure is weak evidence
- Records that agree (multiple solved functions with the same fingerprint used
  the same pattern) are much stronger than single records

Confidence scoring:

```
base_confidence = 1.0
if same_unit: += 0.5
if instruction_count within 20%: += 0.3
if cluster_sizes match (top 3): += 0.4
if multiple records agree on pattern: *= (1 + 0.2 * agreement_count)
```

### Failure Records

Equally important: record what did NOT work. When the permuter exhausts its
budget on a function without improvement, record:

```
FailureRecord:
  symbol: str
  fingerprint: DiagnosisFingerprint
  patterns_tried: list[str]
  best_delta: float              # best improvement seen (may be 0)
  rounds_attempted: int
  verdict: str                   # "plateau", "unfixable", "timeout"
```

Failure records prevent the engine from repeating losing strategies on similar
functions. If 10 functions with fingerprint F all failed with pattern P, don't
try P on the 11th.

## Mining Dimensions

Beyond per-function strategy records, the database should support aggregate
queries:

### Pattern Effectiveness By Mismatch Class

```sql
SELECT pattern, mismatch_class,
       COUNT(*) as attempts,
       SUM(CASE WHEN delta > 0 THEN 1 ELSE 0 END) as wins,
       AVG(delta) as avg_improvement
FROM strategy_records
GROUP BY pattern, mismatch_class
ORDER BY wins DESC;
```

This answers: "How effective is `declaration_reorder` on REGSWAP_PLUS
functions?" and "Which pattern has the highest win rate on STRUCTURAL
functions?"

### Unit-Level Patterns

Some units have systematic codegen quirks (e.g., all functions in a TU share
the same inlining context):

```sql
SELECT unit, pattern, COUNT(*) as wins
FROM strategy_records
WHERE final_match_pct = 100.0
GROUP BY unit, pattern
ORDER BY unit, wins DESC;
```

This answers: "In UIList.cpp, what pattern most commonly reaches 100%?"

### Temporal Patterns

Patterns that worked early in the project may not work now (headers have
changed, PCH has evolved). Track strategy record timestamps and weight recent
records higher.

### Interaction Patterns

Some patterns only work in combination:

```sql
SELECT sr1.pattern as first_pattern,
       sr2.pattern as second_pattern,
       COUNT(*) as combo_wins
FROM strategy_records sr1
JOIN strategy_records sr2
  ON sr1.symbol = sr2.symbol
  AND sr1.round < sr2.round
WHERE sr2.final_match_pct = 100.0
GROUP BY sr1.pattern, sr2.pattern
HAVING combo_wins >= 3
ORDER BY combo_wins DESC;
```

This answers: "Which two-pattern combos most frequently solve functions?"

## Relationship To Existing Systems

### mine_patterns.py

mine_patterns.py is the predecessor. It already diffs baselines and classifies
changes. The new system extends it by:

- Producing structured StrategyRecord objects, not just summary counts
- Linking records to diagnosis fingerprints for similarity search
- Making records queryable at search time, not just for batch analysis

### Permuter RoundHints

RoundHints tracks within-function pattern performance (wins, failures, tag
deltas). The strategy database is the cross-function analog: what RoundHints
learns about one function, the strategy database learns across all functions.

### batch_triage.py

batch_triage.py classifies functions into 6 categories. The fingerprint system
is a refinement: same idea, finer resolution. The triage categories become
high-level bins; fingerprints provide within-bin discrimination.

### Compiler Atlas

The compiler atlas (COMPILER_ATLAS.md) maps instruction patterns to source
features. The strategy database maps diagnosis profiles to winning strategies.
They complement each other:

- Atlas: "target instruction X means source probably has feature Y"
- Strategy DB: "functions with diagnosis profile D are solved by pattern P"

The atlas provides the *what*; the strategy DB provides the *how often* and
*in what context*.

## Implementation Plan

### Phase 1: Schema and Retroactive Harvest

1. Define StrategyRecord and FailureRecord schemas in `types.py` or a
   dedicated `strategy_db.py`
2. Define DiagnosisFingerprint with the fields listed above
3. Extend mine_patterns.py to produce StrategyRecords from baseline diffs
4. Populate initial database from the 25 cached baseline snapshots

Deliverable: SQLite database with ~500-2000 strategy records from history.

### Phase 2: Live Recording

1. Extend the hill climber and beam solver to emit StrategyRecords on
   completion (success or failure)
2. Record fingerprint at baseline time (already computed as Diagnosis)
3. Store in the same SQLite database
4. Every permuter run grows the database

Deliverable: Strategy database grows automatically with each permuter session.

### Phase 3: Query-Time Lookup

1. Implement fingerprint similarity search (weighted Hamming)
2. Add a `--strategy-boost` flag (default: on) to the permuter
3. At round 1, query k-nearest strategies and boost matching patterns
4. Log which historical strategies influenced the search

Deliverable: Permuter uses historical knowledge to guide first-round search.

### Phase 4: Aggregate Analysis

1. Build the SQL queries listed under Mining Dimensions
2. Generate periodic reports: pattern effectiveness by class, unit-level
   patterns, interaction patterns
3. Use reports to tune pattern priorities and follow-up maps in the permuter
4. Identify systematic gaps (mismatch classes with no winning strategies)

Deliverable: Data-driven pattern prioritization.

## Expected Value

### Cold-Start Acceleration

New functions that match a known mismatch class should be solved faster.
Instead of trying 50 variants in round 1, the engine tries the 3-5 patterns
that historically work on similar functions. If those hit, the function is
solved in 1 round instead of 3-5.

Conservative estimate: 2-3x speedup on functions that match known classes.

### Gap Identification

Mismatch classes with zero winning strategies are the hardest targets. The
strategy database makes these visible:

- "47 functions with fingerprint F have never been improved by any pattern"
- "The STRUCTURAL class with cluster_count >= 5 has a 3% win rate"

This focuses future pattern development on the gaps with the most remaining
functions.

### Pattern Retirement

Patterns with consistently zero win rate across many fingerprint classes should
be candidates for retirement or demotion. The strategy database provides the
evidence base for this decision.

### Composition Discovery

The interaction query (which two-pattern combos work together) can
automatically discover follow-up chains that are currently hand-curated in the
`_FOLLOW_UP_MAP`. If the data shows that `variable_extraction` followed by
`declaration_reorder` wins 40% of the time on REGSWAP_PLUS functions, that
should be a first-class composed chain in the permuter, not a static entry in
a dictionary.

## Open Questions

- How many distinct fingerprint clusters exist in the 29,842 solved functions?
  If the answer is "50", the database is highly compressed and very useful. If
  the answer is "10,000", similarity search needs a more sophisticated distance
  metric.

- Should the strategy database live alongside decomp.db or separately? It has
  different access patterns (similarity search vs exact key lookup).

- How should we handle header-driven regressions? A strategy record from before
  a header change may not be valid after it. Should records expire, or should
  they carry a header-state hash?

- How much retroactive mining is possible given the git history? Functions
  fixed in the very first commits may not have baseline snapshots to diff
  against.
