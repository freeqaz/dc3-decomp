export const meta = {
  name: 'revcomp-mvp5',
  description: 'Wave 5: routed closing sweep on the >=99% frontier (the payoff) + X7 hand-rolled compare/branch orbit A/B',
  phases: [{ title: 'Wave5', detail: 'routed closing sweep + X7 orbit, in parallel (compile-based, Scorer in main, NO commit)' }],
}

const DS = '/home/free/code/milohax/decomp-synth'
const DC3 = '/home/free/code/milohax/dc3-decomp'
const DATA = DS + '/docs/plans/reverse-compilation/data'
const ROADMAP = DS + '/docs/plans/reverse-compilation/MVP_VALIDATION_ROADMAP.md'

const COMMON = `Staff-level decomp tooling engineer on decomp-synth's inverse-compilation MVPs. Read ${ROADMAP},
${DATA}/INDEX.md, and the relevant data reports first. Use \`python3\` (= ${DC3}/venv/bin/python3, has decomp_synth +
z3 4.16); if import fails set PYTHONPATH=${DS}. Compile/score = decomp_synth.scorer.Scorer (concurrency-safe: working
copies + per-file locks + restores — it NEVER mutates real source, which is how sweeps already run in main).
HARD RULES: do NOT git stash/commit/revert/checkout (concurrent agents + a concurrent win-committer share ${DC3});
report verified wins as HARVEST CANDIDATES, never commit them. Build A+ quality. Every claim is a NUMBER.`

// ── Agent A: the routed closing sweep (the payoff — try to MATCH real functions) ──
const closingSweep = `${COMMON}

TASK — ROUTED CLOSING SWEEP: use the validated MVP stack to try to CLOSE real near-misses to 100% (the strongest
possible validation + real decomp progress). You OWN: ${DS}/tools/revcomp/closing_sweep.py + its report.

TARGETS: ${DATA}/X3b_frontier_worklist.json — take the TOP ~25 by normalized% in the >=99% band (closest to done).
Each row has symbol, unit, normalized, symptom_flags, recommended_moves (the X1 prior's top moves for its symptom).

FIRST read how the permuter is driven: ${DS}/decomp_synth/scan_and_permute.py, pattern_sweep.py, patterns/__init__.py
(pattern registry + generate(diagnosis, source) contract), and revfix.py (how it resolves source_path+unit and drives
Scorer). Reuse this machinery; do not reinvent the build loop.

PER TARGET:
 1. Resolve source_path + unit; open Scorer; get_baseline(guided=True) -> current fuzzy% + diagnosis. (If it doesn't
    reproduce its claimed %, record + skip — the X2 reproducibility gate.)
 2. ROUTE: pick the moves to try from recommended_moves (+ the live diagnosis). For semantic-bet moves, GATE with the
    X6 oracle (import tools/revcomp/neutrality_oracle): skip REFUTED moves and NEEDS_PRECONDITION moves whose pre isn't
    established. For FPR decl-order mismatches, you MAY use tools/revcomp/fpr_solver to solve the order directly.
 3. Run the routed moves' generate() -> variants -> Scorer.score_batch (workers<=6). Did any reach 100.000%?
 4. Record: symbol, baseline%, best%, closed(bool), compiles_spent, winning_pattern, and the winning source diff.

CONTROL (quantify the routing benefit on REAL closing): for ~8 of the targets, ALSO run an UNROUTED pass (default
pattern set / order, same compile budget cap) and compare compiles-to-first-win and close-rate routed vs unrouted.

DELIVERABLE: ${DS}/tools/revcomp/closing_sweep.py (argparse, --top N, --control, --max-variants-per-move K) +
${DATA}/X5b_routed_closing_sweep.md: #targets, #CLOSED to 100% (list them with the winning edit as harvest
candidates), compiles spent, routed-vs-unrouted efficiency, and which floor signals (X3/X5/X6) correctly predicted the
non-closers. Keep it bounded (~25 targets) and DO NOT commit any source change. Return the StructuredOutput.`

// ── Agent B: X7 hand-rolled compare/branch orbit + top-K gate, A/B vs per-transform ──
const orbit = `${COMMON}

TASK X7 — build the hand-rolled compare/branch expression ORBIT generator (NOT egg yet — the egg-adoption gate) and
A/B it against the existing per-transform generation. You OWN: ${DS}/tools/revcomp/orbit.py + its report.

Read ${DS}/docs/plans/reverse-compilation/ORBIT_GENERATION.md (the spec), the compare/branch patterns in
${DS}/decomp_synth/patterns/ (branch_polarity, comparison_*, signed_unsigned, bool_*, ternary_*), and the
decomp-permuter perm_condition/perm_commutative semantics in /home/free/code/milohax/reverse-compiler-refs/decomp-permuter
(src/randomizer.py / perm_* — use as the rewrite-correctness spec).

BUILD a bounded local orbit (hand-rolled DAG/worklist, NOT egg): enumerate behaviour-neutral compare/branch rewrites of
a function's condition region — polarity invert + body swap, DeMorgan, commute, x==0<->!x, signed/unsigned cast
placement, ternary<->if, bool materialization. Compose them locally (this is the key egg question: do they compose?).
Rank candidate extractions by a cost = the X1 move-prior (${DATA}/X1_move_priors_DC3-MSVC.json) for the active symptom
(predicted target-codegen distance), and GATE each rewrite with the X6 neutrality oracle (only behaviour-neutral
rewrites enter the orbit). Compile only the TOP-K (K~3-8) via Scorer.

TARGETS: compare/branch-shaped near-misses — from ${DATA}/X3b_frontier_worklist.json filter to has_comparison_style /
has_control_flow / has_offset_swap rows (and/or run a guided diagnosis to find branch/compare diffs). Pick ~12.

A/B: for each target, compare ORBIT (top-K ranked, gated) vs the EXISTING per-transform generation (run the same
compare/branch patterns the normal way) on: compiles spent, best% reached, #closed, and root movement. Measure whether
local composition lands the target form that single transforms miss (the orbit's reason to exist), and whether
transforms compose enough to ever justify adopting egg (state the egg-adoption verdict).

DELIVERABLE: ${DS}/tools/revcomp/orbit.py (argparse) + ${DATA}/X7_orbit.md: the A/B table (orbit vs per-transform:
compiles, best%, closes), whether composition helped, the egg-adoption verdict, and any real closes (harvest
candidates, NOT committed). Bounded (~12 targets). Scorer in main, NO commit. Return the StructuredOutput.`

const S_SWEEP = { type: 'object', additionalProperties: false, properties: {
  tool_path: { type: 'string' }, report_path: { type: 'string' }, targets: { type: 'integer' },
  closed_to_100: { type: 'integer' }, compiles_spent: { type: 'integer' },
  closed_symbols: { type: 'array', items: { type: 'string' } },
  routed_vs_unrouted: { type: 'string' }, floor_signal_accuracy: { type: 'string' }, headline: { type: 'string' },
}, required: ['tool_path', 'report_path', 'targets', 'closed_to_100', 'headline'] }

const S_ORBIT = { type: 'object', additionalProperties: false, properties: {
  tool_path: { type: 'string' }, report_path: { type: 'string' }, targets: { type: 'integer' },
  orbit_closes: { type: 'integer' }, pertransform_closes: { type: 'integer' },
  composition_helped: { type: 'boolean' }, egg_adoption_verdict: { type: 'string' }, headline: { type: 'string' },
}, required: ['tool_path', 'report_path', 'targets', 'orbit_closes', 'pertransform_closes', 'egg_adoption_verdict', 'headline'] }

phase('Wave5')
const [sweep, orb] = await parallel([
  () => agent(closingSweep, { label: 'X5b:routed-closing-sweep', phase: 'Wave5', schema: S_SWEEP }),
  () => agent(orbit, { label: 'X7:compare-branch-orbit', phase: 'Wave5', schema: S_ORBIT }),
])
return { routed_closing_sweep: sweep, orbit: orb }
