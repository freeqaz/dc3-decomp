export const meta = {
  name: 'revcomp-mvp6',
  description: 'Final wave: X8 AUC-bandit budget allocator (offline-validated) + X9 Souffle-vs-Rust-fixpoint bake-off (stretch capstone)',
  phases: [{ title: 'Wave6', detail: 'X8 budget allocator + X9 Souffle bake-off, in parallel' }],
}

const DS = '/home/free/code/milohax/decomp-synth'
const DC3 = '/home/free/code/milohax/dc3-decomp'
const REFS = '/home/free/code/milohax/reverse-compiler-refs'
const DATA = DS + '/docs/plans/reverse-compilation/data'
const ROADMAP = DS + '/docs/plans/reverse-compilation/MVP_VALIDATION_ROADMAP.md'

const COMMON = `Staff-level decomp tooling engineer on decomp-synth's inverse-compilation MVPs. Read ${ROADMAP} and
${DATA}/INDEX.md first. Use \`python3\` (= ${DC3}/venv/bin/python3, has decomp_synth + z3); PYTHONPATH=${DS} if needed.
NEVER git stash/commit/revert. Build A+ quality. Every claim is a NUMBER; settle questions WITH DATA, not opinion.`

// ── X8: AUC-bandit budget allocator (port opentuner, warm-start X1, offline-validate) ──
const x8 = `${COMMON}

TASK X8 — port OpenTuner's AUC-bandit budget allocator and prove (offline) whether ADAPTIVE allocation beats STATIC
greedy routing across competing moves. You OWN: ${DS}/tools/revcomp/budget_allocator.py.

Context: Wave 5's closing sweep showed STATIC greedy routing (try the X1-prior's top-3 moves for the symptom) beat
blind 3.2× and closed MetagameRank in 12 compiles. X8 asks: does an adaptive AUC-bandit that REALLOCATES budget as
results arrive beat static greedy — especially when the X1 prior is MISCALIBRATED for a specific function?

BUILD: read ${REFS}/opentuner (bandittechniques.py — the AUCBanditQueue, ~150 LOC, MIT) and port it to a
BudgetAllocator<move> in budget_allocator.py: maintains a sliding-window AUC credit per move arm from a binary reward
(was_new_best), emits the next move to spend a compile on, warm-started by seeding arm use_counts/priors from the X1
move-prior (${DATA}/X1_move_priors_DC3-MSVC.json) to skip the cold-start "try every arm once" tax. Add hash_config-style
dedup (never spend a compile on a source you've already compiled — key on source hash).

VALIDATE OFFLINE (no/minimal compiles — this is the point): simulate the three policies on the empirical reward model
from the data. Build a Monte-Carlo harness that, per symptom flag, treats each move's X1 win-rate (and the historical
pattern_runs per-(symptom,pattern) win-rate) as the arm's reward probability, and measures EXPECTED COMPILES-TO-FIRST-WIN
under: (a) UNIFORM, (b) STATIC-GREEDY (X1 prior order), (c) AUC-BANDIT (warm-started). Run many trials; report the
distributions. Key questions to answer with numbers: does the bandit beat uniform (expected, given X1's long tail)?
does it beat static-greedy, and under what condition (when the per-function best move differs from the population
prior — simulate prior-miscalibration by perturbing the per-function best arm)? quantify the warm-start benefit
(compiles saved vs cold-start). Optionally do ONE small live check on a couple Wave-5 targets, but the offline
simulation is the deliverable.

Write ${DATA}/X8_budget_allocator.md: the allocator design, the uniform/greedy/bandit comparison (expected
compiles-to-win + when bandit wins), warm-start benefit, and a verdict on whether X8 belongs in the live beam (vs
static greedy being sufficient given Wave 5). Return the StructuredOutput.`

// ── X9: Souffle vs Rust/Python fixpoint bake-off (the user's stretch capstone) ──
const x9 = `${COMMON}

TASK X9 (stretch capstone) — settle the Souffle question WITH DATA, not opinion. The synthesis recommended deferring
Souffle permanently (no Rust binding; compiled mode IS an expensive-compiler call) and using a plain stratified
fixpoint. The user wants Souffle given a FAIR shot. Build the X2 root-cause provenance relations BOTH ways and bake
them off on real facts. You OWN: ${DS}/tools/revcomp/souffle_provenance.dl + ${DS}/tools/revcomp/fixpoint_provenance.py
+ a small runner.

THE RELATIONS (root-mismatch provenance, from objdiff instruction rows): Def(reg,instr), Use(reg,instr),
Feeds(src,dst) :- Def(r,src),Use(r,dst); Mismatch(instr,kind); RootMismatch(instr,kind) :- Mismatch(instr,kind), NOT
EXISTS j: (Mismatch(j,_), Feeds(j,instr)). Derive the facts from REAL fixtures: take a handful of divergent functions
(from ${DS}/tools/revcomp/fixtures.db or the historical validated table, or run Scorer.get_baseline(guided=True) on a
few X3b near-misses) and emit Def/Use/Mismatch facts using powerpc-rs / the objdiff instruction operands (read
decomp_synth/diagnosis.py + scorer._run_objdiff for the instruction-row shape).

IMPLEMENT BOTH:
 1. Souffle: write souffle_provenance.dl (.decl + the rules above). Get Souffle running — try \`apt-get install souffle\`
    (may need sudo; if unavailable) or build from ${REFS}/souffle (cmake; heavy — time-box it; if it won't build in a
    reasonable window, say so HONESTLY and fall back to running a minimal documented example / reasoning from the
    cloned source). Run it on the fact CSVs; capture RootMismatch output + wall-clock (incl. its compile step if using
    compiled mode).
 2. Hand-rolled: fixpoint_provenance.py — the same relations as a plain Python stratified fixpoint (<150 LOC). Run on
    the SAME facts; capture output + wall-clock.

BAKE-OFF (the deliverable): compare on the SAME fixtures — correctness (identical RootMismatch sets?), LOC, end-to-end
latency (Souffle incl. its .dl->C++ compile vs Python cold), setup/dependency cost, Rust-interop reality (Souffle =
subprocess + CSV only), and expressiveness (does the recursive Feeds/RootMismatch rule read better in Datalog?). Scale
note: our problem is ONE function, tens-hundreds of instructions. VERDICT with numbers: at this scale does Souffle earn
its place, or does the data confirm "defer, use the hand-rolled fixpoint"? Be fair to Souffle (note where it WOULD win
— cross-function/whole-program scale) and honest about what you actually managed to run.

Write ${DATA}/X9_souffle_bakeoff.md (both impls, the bake-off table, the honest verdict + the scale at which the
verdict would flip). Return the StructuredOutput.`

const S_X8 = { type: 'object', additionalProperties: false, properties: {
  tool_path: { type: 'string' }, report_path: { type: 'string' },
  bandit_beats_uniform: { type: 'boolean' }, bandit_beats_greedy_when: { type: 'string' },
  warmstart_benefit: { type: 'string' }, belongs_in_live_beam: { type: 'boolean' }, headline: { type: 'string' },
}, required: ['tool_path', 'report_path', 'bandit_beats_uniform', 'belongs_in_live_beam', 'headline'] }

const S_X9 = { type: 'object', additionalProperties: false, properties: {
  report_path: { type: 'string' }, souffle_actually_ran: { type: 'boolean' },
  outputs_identical: { type: 'boolean' }, souffle_latency_ms: { type: 'number' }, fixpoint_latency_ms: { type: 'number' },
  verdict: { type: 'string' }, scale_where_souffle_wins: { type: 'string' }, headline: { type: 'string' },
}, required: ['report_path', 'souffle_actually_ran', 'verdict', 'headline'] }

phase('Wave6')
const [x8r, x9r] = await parallel([
  () => agent(x8, { label: 'X8:budget-allocator', phase: 'Wave6', schema: S_X8 }),
  () => agent(x9, { label: 'X9:souffle-bakeoff', phase: 'Wave6', schema: S_X9 }),
])
return { x8_budget_allocator: x8r, x9_souffle_bakeoff: x9r }
