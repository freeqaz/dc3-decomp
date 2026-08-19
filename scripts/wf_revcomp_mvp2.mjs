export const meta = {
  name: 'revcomp-mvp2',
  description: 'Wave 2: X3 floor census (near-zero compile) + fixture harness pilot (X2 substrate) + X2 classifier eval',
  phases: [
    { title: 'Build+Census', detail: 'X3 floor census + fixture harness pilot, in parallel' },
    { title: 'Eval', detail: 'X2 classifier accuracy + X1-prior top-K recall on the pilot fixtures' },
  ],
}

const DS = '/home/free/code/milohax/decomp-synth'
const DC3 = '/home/free/code/milohax/dc3-decomp'
const DATA = DS + '/docs/plans/reverse-compilation/data'
const ROADMAP = DS + '/docs/plans/reverse-compilation/MVP_VALIDATION_ROADMAP.md'

const COMMON = `You are a staff-level decomp tooling engineer working on decomp-synth's inverse-compilation MVPs.
Read ${ROADMAP} first (the X1-X9 ladder + reference verdicts). decomp-synth is a pip-installed package
('import decomp_synth'); if import fails, set PYTHONPATH=${DS}. Tooling goes in ${DS}/tools/revcomp/;
data reports go in ${DATA}/. Be concrete, skeptical, and DATA-driven: every claim backed by a number.
Do NOT git stash / commit / revert in any repo (concurrent agents share these trees). Build A+ quality:
argparse CLI, clean schema, reusable. Report what FAILED honestly (a data-quality finding is a finding).`

const x3 = `${COMMON}

TASK X3 — build a (near) compile-free FLOOR CENSUS for DC3.
Question: of the functions NOT at 100%, what fraction are non-source-authorable FLOORS (no behaviour-neutral
C++ edit can move them), classified by cause? A floor proven without a compile is the biggest budget win.

Floor signals, cheapest first:
 1. objdiff 'match_percent_normalized==100' while fuzzy match<100 = pure regalloc/scheduling floor (FREE certificate).
    Find how the project runs objdiff-cli (read ${DS}/decomp_synth/scorer.py::_run_objdiff and
    ${DC3}/scripts/orchestrator); the JSON may expose normalized %. Diff already-built base vs target objs
    (build/373307D9/...). Do NOT rebuild the world.
 2. CodeView S_FRAMEPROC flags: fPogoOn (PGO -> block-sinking floor), fHasEH/fHasSEH (EH funclet stub).
    Read ${DC3}/scripts/analysis/codeview_locals.py + stack_layout.py to learn the CodeView decode API and
    WHERE CodeView lives. If a /Z7 recompile is required, do it for a SAMPLE only and extrapolate — never
    thousands of recompiles. State clearly which signals are zero-compile vs sampled.
 3. Existing decomp.db.functions labels (read the schema): is_stub, verdict, reachable_100,
    merged_symbol_count/has_linker_merged, has_prologue_mismatch. Memory documents ~361 block-sinking
    (PGO-gated) + ~1,367 EH funclet stubs + ICF merged_<addr> — CROSS-CHECK your counts against these.

Build ${DS}/tools/revcomp/floor_census.py (argparse, --sample N). Classify each stuck symbol into a
floor_kind in {regalloc_normalized, pgo_block_sink, eh_funclet, icf_merged, stub, none_workable, unknown}.
Emit ${DATA}/X3_floor_labels.json (per-symbol) + write ${DATA}/X3_floor_census.md with: total functions,
total stuck (<100), counts+% per floor_kind, which signals were free vs sampled, and the cross-check vs the
documented 361/1367 numbers. Headline: what % of stuck functions are provably non-authorable floors.

Return the StructuredOutput.`

const fixtures = `${COMMON}

TASK — build the GROUND-TRUTH FIXTURE HARNESS (the substrate that makes every other MVP measurable) and run a PILOT.
The principle: a matched (100%) function + ONE behaviour-neutral transform -> a controlled divergence whose KNOWN
FIX is the inverse transform. This yields labeled test cases where we know the answer.

FIRST read these to learn the exact idiom (do not reinvent): ${DS}/decomp_synth/scorer.py (Scorer context manager:
get_baseline(guided=True) -> match% and .diagnosis; score(variant)/score_batch(variants, workers<=6) -> ScoreResult),
${DS}/decomp_synth/scan_and_permute.py and pattern_sweep.py (how a symbol is resolved to source_path+unit and how
patterns are applied), ${DS}/decomp_synth/patterns/ (behaviour-neutral transforms; Variant has .source bytes),
and ${DS}/decomp_synth/types.py (Variant, ScoreResult, Diagnosis).

PILOT (run in the MAIN ${DC3} repo — Scorer is concurrency-safe: working copies + per-file locks + restores; it is
literally how sweeps run in main. Do NOT touch real source permanently, do NOT git stash):
 1. Select ~30 matched, source-authorable functions from ${DC3}/decomp.db.functions
    (current_percent>=100 AND NOT is_stub AND NOT has_linker_merged AND COALESCE(excluded,0)=0), diverse across
    symptom flags (has_register_swap/has_control_flow/has_commutative_op_order/...), with a resolvable .cpp on disk
    and modest size. Resolve source_path + unit the same way scan_and_permute does.
 2. For each: Scorer.get_baseline(guided=True). CONFIRM baseline ~100 (>=99.5). If a "matched" fn does NOT reproduce
    100% on disk, RECORD it (reproducibility data) and skip. (Try scripts/clean_stale_objects.sh --dry-run note only;
    do not force global rebuilds.)
 3. Generate SYNTHETIC divergences: apply a handful of behaviour-neutral patterns (decl reorder, statement reorder,
    commutative swap, comparison/branch polarity, signed/unsigned, temp extraction — pick ~6 reliable ones) to the
    matched source; score each; KEEP variants that drop below 100 (a controlled divergence). For each kept divergence,
    re-score guided to capture the induced Diagnosis. Record a fixture row.
 4. Store fixtures in ${DC3}/fixtures.db (or ${DS}/tools/revcomp/fixtures.db — your call, document it) via a clean
    schema: id, symbol, unit, source_path, baseline_pct, perturbation_pattern (=the KNOWN-FIX family, since reverting
    restores 100), perturbed_pct, induced_diagnosis_json, perturbed_source_b64 (or md5), created_at.
 5. VALIDATE round-trip on ~5 fixtures: re-applying the inverse (restoring the matched body) returns to 100% — proves
    the label. Report pass/fail.
 6. Document a --historical mode (pre-match git revision as a real divergence) as a STUB/plan; do not need to run it.

Build ${DS}/tools/revcomp/revfix.py (argparse: --build-synthetic --pilot N --validate --db PATH). Write
${DATA}/X2_fixture_pilot.md: #matched tested, #reproduced 100, #divergences generated, induced-diagnosis-class
distribution, round-trip result, and any non-reproducing matched fns (a data-quality finding). Keep the pilot to
~30 functions (this validates the harness; it is not a full sweep). Return the StructuredOutput.`

const CENSUS_SCHEMA = {
  type: 'object', additionalProperties: false,
  properties: {
    tool_path: { type: 'string' }, report_path: { type: 'string' }, labels_json: { type: 'string' },
    total_functions: { type: 'integer' }, total_stuck: { type: 'integer' },
    by_floor_kind: { type: 'array', items: { type: 'object', additionalProperties: false,
      properties: { kind: { type: 'string' }, count: { type: 'integer' }, pct_of_stuck: { type: 'number' }, zero_compile: { type: 'boolean' } },
      required: ['kind', 'count', 'pct_of_stuck', 'zero_compile'] } },
    cross_check_notes: { type: 'string' }, headline: { type: 'string' },
  },
  required: ['tool_path', 'report_path', 'total_stuck', 'by_floor_kind', 'headline'],
}

const FIXTURE_SCHEMA = {
  type: 'object', additionalProperties: false,
  properties: {
    tool_path: { type: 'string' }, fixtures_db: { type: 'string' }, report_path: { type: 'string' },
    matched_tested: { type: 'integer' }, matched_reproduced_100: { type: 'integer' },
    divergences_generated: { type: 'integer' },
    induced_class_distribution: { type: 'array', items: { type: 'string' } },
    roundtrip_validated_ok: { type: 'integer' }, roundtrip_validated_total: { type: 'integer' },
    nonreproducing_symbols: { type: 'array', items: { type: 'string' } },
    headline: { type: 'string' },
  },
  required: ['tool_path', 'fixtures_db', 'report_path', 'matched_tested', 'matched_reproduced_100', 'divergences_generated', 'headline'],
}

phase('Build+Census')
const [census, fix] = await parallel([
  () => agent(x3, { label: 'X3:floor-census', phase: 'Build+Census', schema: CENSUS_SCHEMA }),
  () => agent(fixtures, { label: 'X2:fixture-harness', phase: 'Build+Census', schema: FIXTURE_SCHEMA }),
])

phase('Eval')
let evalRes = null
if (fix && fix.fixtures_db && fix.divergences_generated > 0) {
  const x2eval = `${COMMON}

TASK X2 — EVALUATE the root-mismatch classifier against the ground-truth fixtures the harness just built.
Fixtures DB: ${fix.fixtures_db} (rows: symbol, perturbation_pattern = KNOWN-FIX family, induced_diagnosis_json, ...).
The induced perturbation family is the GROUND-TRUTH mismatch class for that fixture.

Evaluate ${DS}/decomp_synth/classifier.py (classify_mismatches over a Diagnosis; see diagnosis.py::diagnose_baseline)
and ${DS}/decomp_synth/floor_signatures.py (detect_floor):
 1. For each fixture, feed the induced Diagnosis to the classifier. Build a confusion matrix: predicted
    MismatchClassification vs the true perturbation family. Report overall accuracy + per-class precision/recall.
 2. FALSE-FLOOR RATE (critical): how often does detect_floor / classify call a KNOWN-FIXABLE synthetic divergence an
    unfixable floor? Each such case is a tool bug we must know about.
 3. X1-prior top-K recall: load ${DATA}/X1_move_priors_DC3-MSVC.json. For each fixture, does the X1 move-prior rank the
    known-correct fix family in its top-K (K=3,5) for the fixture's symptom flag(s)? This ties X1 (ranker) + X2
    (root-cause) together — it is the routing-validation result.

Build ${DS}/tools/revcomp/eval_classifier.py. Write ${DATA}/X2_classifier_eval.md (confusion matrix, accuracy,
false-floor rate, top-K recall, and concrete tool bugs found). Return the StructuredOutput.`
  const EVAL_SCHEMA = {
    type: 'object', additionalProperties: false,
    properties: {
      tool_path: { type: 'string' }, report_path: { type: 'string' },
      fixtures_used: { type: 'integer' }, accuracy: { type: 'number' },
      false_floor_rate: { type: 'number' }, x1_topk5_recall: { type: 'number' },
      tool_bugs_found: { type: 'array', items: { type: 'string' } }, headline: { type: 'string' },
    },
    required: ['report_path', 'fixtures_used', 'accuracy', 'false_floor_rate', 'headline'],
  }
  evalRes = await agent(x2eval, { label: 'X2:classifier-eval', phase: 'Eval', schema: EVAL_SCHEMA })
} else {
  log('Skipping X2 eval: fixture harness produced no divergences (see its report).')
}

return { census, fixtures: fix, classifier_eval: evalRes }
