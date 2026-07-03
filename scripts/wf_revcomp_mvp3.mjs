export const meta = {
  name: 'revcomp-mvp3',
  description: 'Wave 3: fix classifier false-floors (validated on fixtures) + full historical corpus score-sweep + X4 FPR decl-order atlas',
  phases: [{ title: 'Wave3', detail: 'classifier-fix + historical score-sweep + X4 FPR atlas, in parallel' }],
}

const DS = '/home/free/code/milohax/decomp-synth'
const DC3 = '/home/free/code/milohax/dc3-decomp'
const DATA = DS + '/docs/plans/reverse-compilation/data'
const ROADMAP = DS + '/docs/plans/reverse-compilation/MVP_VALIDATION_ROADMAP.md'

const COMMON = `Staff-level decomp tooling engineer on decomp-synth's inverse-compilation MVPs. Read ${ROADMAP} and
${DATA}/INDEX.md first. decomp-synth is pip-installed ('import decomp_synth'); if import fails set
PYTHONPATH=${DS}. Compile/score primitive: decomp_synth.scorer.Scorer (concurrency-safe: working copies +
per-file locks + restores). NEVER git stash/commit/revert/checkout in any repo (concurrent agents share the trees;
read-only git only). Build A+ quality. Report every claim as a NUMBER; failures/data-gaps are findings.`

// ── Agent 1: fix the classifier false-floor bug (NO compiles) ──
const fixClassifier = `${COMMON}

TASK — FIX the classifier false-floor bug found by X2 eval, validated against the ground-truth fixtures.
You OWN ONLY: ${DS}/decomp_synth/classifier.py and ${DS}/tools/revcomp/eval_classifier.py. Do NOT touch revfix.py
or historical_fixtures.py (another agent owns them this wave).

THE BUG (measured: 10.3% false-floor rate, 7/68 fixtures): classify_mismatches() hard-codes both-volatile
(r0-r12 / f0-f13) register swaps as fixable="no" (conf 0.95). compute_fixability_score then weights "no" at 0, so a
function whose real source edit CASCADED into volatile regswaps + unclassified replace/cluster churn gets fixability
0.00 and is ABANDONED — even though the inverse source transform demonstrably fixes it. Confirmed cases:
CharBones::AddBoneInternal, DanceRemixer::JumpedMeasureAdd, DirUnloader::DirUnloader, FreestyleMove::~FreestyleMove,
SharedGroup::TryEnter, Voice::blockingStart.

FIX (principled, minimal, STRICT IMPROVEMENT — only relaxes an over-aggressive floor):
 - Read classifier.py fully (esp. classify_mismatches, compute_fixability_score, is_fpr_cascade_dominated,
   count_multi_instruction_fpr_swap_pairs) and the Diagnosis dataclass (decomp_synth/diagnosis.py + types.py) to learn
   the available signals (reg_swap_pairs, diff_ops, clusters, offset_deltas, prologue, etc.).
 - Define "structural churn present" from the Diagnosis (e.g. insert/delete clusters exist, OR there are
   non-regswap diff_ops / replace churn, OR another fixable/maybe category fired). When a both-volatile regswap
   co-occurs with structural churn AND the function is NOT is_fpr_cascade_dominated (the genuine FPR-cascade floor,
   pairs>=10), classify it fixable="maybe" (lower confidence) instead of "no", with a detail noting it is plausibly
   downstream of a source edit. KEEP the hard "no" for PURE volatile-regswap functions (no churn) and for
   fpr_cascade_dominated ones — do not make genuine compiler-internal floors look fixable.

VALIDATE (this is the acceptance gate — iterate until it passes): run ${DS}/tools/revcomp/eval_classifier.py against
${DS}/tools/revcomp/fixtures.db. Required: false-floor rate drops substantially (target: the 7 known-fixable cases
are no longer abandoned; aim <=3%). Symptom-axis accuracy must NOT regress. Report BEFORE vs AFTER numbers for:
false-floor rate, symptom accuracy, and confirm no genuine pure-regalloc floor became "fixable" (sanity-check a few
pure-volatile-regswap diagnoses still score ~0). If you also cheaply tighten the assert_line_delta / missing_guard
over-firing without hurting recall, do it; else just report them.

Write ${DATA}/X2_classifier_eval_v2.md (before/after table + what changed + residual fix-family-ceiling note).
Return the StructuredOutput.`

// ── Agent 2: full historical corpus + score-sweep validation (compiles) ──
const histSweep = `${COMMON}

TASK — scale the historical fixture corpus to the FULL git history, then SCORE-SWEEP validate it.
You OWN: ${DS}/tools/revcomp/historical_fixtures.py and ${DS}/tools/revcomp/revfix.py and the historical DB.

STEP 1 (full extract, READ-ONLY git): the corpus is currently 805 fixtures (93 match + ~400 src commits). Scale to
the full history by running historical_fixtures.py --all-src --max-commits 0 --max-diff-lines 100. CRITICAL: run it
with cwd=${DC3} (the decomp project root resolution walks up from cwd and needs ${DC3}/objdiff.json; running from
${DS} fails with RepoRootNotFound). Use PYTHONPATH=${DS}. Confirm the corpus grows well beyond 805; report the count.

STEP 2 (fix the noise-counter persistence bug): revfix.summarize_diagnosis drops noise_total/noise_explained, so
detect_floor's ADDRESS_RELOCATION_NOISE arm is untestable. Persist those two counters in the stored diagnosis JSON.

STEP 3 (SCORE-SWEEP validation via Scorer SPLICE — this is the key half): for a bounded sample (~150 fixtures,
prioritize currently_100=1 and diverse edit-kinds), validate each by SPLICING the stored before_source / after_source
function body into the CURRENT tree (NOT git checkout — splicing controls for build-env drift) and scoring via
decomp_synth.scorer.Scorer: confirm before -> <100% (record actual %), after -> ~100% (record %), and capture the
guided Diagnosis for the BEFORE state (the induced symptom). Reuse revfix.py's Scorer integration / byte-range splice;
read it first. Store results in a new table 'validated' (qualified_name, symbol, commit, before_pct, after_pct,
before_diagnosis_json, edit_kind, validated_bool) in the historical DB. Run in the MAIN ${DC3} repo (Scorer-safe).
Keep the sample bounded (~150) for wall-clock; use score_batch where possible.

Write ${DATA}/X2b_score_sweep.md: full corpus size, #score-swept, #before<100 confirmed, #after>=99.5 confirmed,
before-% distribution, induced-diagnosis-class distribution, #non-reproducing (build-env drift / splice failures),
and a COMPARISON of the historical induced-class distribution vs the synthetic fixtures (X2_fixture_pilot.md).
Return the StructuredOutput.`

// ── Agent 3: X4 FPR declaration-order atlas micro-experiment (compiles) ──
const fprAtlas = `${COMMON}

TASK X4 — the flagship modeled-lever micro-experiment: map the FPR DECLARATION-ORDER decision boundary.
You OWN: ${DS}/tools/revcomp/fpr_atlas.py. Read first: decomp_synth/constraint_solver.py (it already has FPR
decl-order logic: _decl_order_var_hypotheses, _resolve_decl_order, _decl_order_edits_for_vars), compiler_atlas.py,
and decomp_synth/scorer.py (Scorer; get_shape_facts uses /FAcs via tools/compiler_trace/invoker.py CompilerInvoker).

HYPOTHESIS: MSVC assigns callee-saved FPRs (f14-f31) sequentially in source DECLARATION ORDER for float locals, so
a small batch of controlled compiles that permute the declaration order of a float-heavy function's locals exposes a
clean, learnable map decl-order -> FPR assignment — enough to later invert in ONE Z3 solve (X5).

METHOD:
 1. Pick a fixture: a matched (100%) function with >=3 reorderable float/double local declarations (find candidates
    via decomp.db has_float* flags, or the existing fixtures.db, or grep src for functions with several float locals).
    Confirm baseline 100% via Scorer. If no clean real candidate, construct a small synthetic float-heavy TU compiled
    with the project's cl.exe flags (read scorer/_extract_compile_cmd / CompilerInvoker for the recipe).
 2. Enumerate declaration-order permutations of the float locals (cap at a sane number, e.g. <=24 perms). For each,
    compile and read the resulting callee-saved FPR (f14-f31) assignment for each variable — via /FAcs listing
    (CompilerInvoker.compile_with_asm '/FAcs' then parse) and/or objdiff instruction operands / powerpc-rs facts.
 3. ANALYZE: is the map decl-order -> FPR assignment deterministic and sequential (declare-me-Nth -> f(31-k))? Or is
    it perturbed by use-order / liveness? Record as a structured atlas/ExperimentRecord. Establish a de-salted
    content-addressed compile cache for the enumeration (sha256 of source inputs -> result) so reruns are free.

DELIVERABLE: ${DS}/tools/revcomp/fpr_atlas.py (argparse) + ${DATA}/X4_fpr_atlas.md reporting: the chosen fixture,
#compiles spent, the decl-order->FPR table, whether the boundary is clean/sequential (and any exceptions), and a
verdict: is a closed-form Rust model extractable that an SMT oracle could invert in one solve (-> X5)? Keep it to
~30-100 compiles. Run in MAIN ${DC3} (Scorer-safe). Return the StructuredOutput.`

const S_FIX = { type: 'object', additionalProperties: false, properties: {
  report_path: { type: 'string' }, false_floor_before: { type: 'number' }, false_floor_after: { type: 'number' },
  accuracy_before: { type: 'number' }, accuracy_after: { type: 'number' },
  genuine_floor_preserved: { type: 'boolean' }, change_summary: { type: 'string' }, headline: { type: 'string' },
}, required: ['report_path', 'false_floor_before', 'false_floor_after', 'genuine_floor_preserved', 'headline'] }

const S_HIST = { type: 'object', additionalProperties: false, properties: {
  report_path: { type: 'string' }, corpus_total: { type: 'integer' }, score_swept: { type: 'integer' },
  before_lt100_confirmed: { type: 'integer' }, after_ge995_confirmed: { type: 'integer' },
  nonreproducing: { type: 'integer' }, induced_class_distribution: { type: 'array', items: { type: 'string' } },
  headline: { type: 'string' },
}, required: ['report_path', 'corpus_total', 'score_swept', 'before_lt100_confirmed', 'after_ge995_confirmed', 'headline'] }

const S_FPR = { type: 'object', additionalProperties: false, properties: {
  tool_path: { type: 'string' }, report_path: { type: 'string' }, fixture: { type: 'string' },
  compiles_spent: { type: 'integer' }, boundary_clean: { type: 'boolean' },
  closed_form_extractable: { type: 'boolean' }, exceptions: { type: 'string' }, headline: { type: 'string' },
}, required: ['report_path', 'fixture', 'compiles_spent', 'boundary_clean', 'closed_form_extractable', 'headline'] }

phase('Wave3')
const [clf, hist, fpr] = await parallel([
  () => agent(fixClassifier, { label: 'fix:classifier-false-floor', phase: 'Wave3', schema: S_FIX }),
  () => agent(histSweep, { label: 'X2b:historical-score-sweep', phase: 'Wave3', schema: S_HIST }),
  () => agent(fprAtlas, { label: 'X4:fpr-atlas', phase: 'Wave3', schema: S_FPR }),
])
return { classifier_fix: clf, historical_sweep: hist, fpr_atlas: fpr }
