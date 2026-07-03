export const meta = {
  name: 'loop-plan',
  description: 'Plan the production inverse-compilation loop: 5 Opus lens-designers (deduction/Z3/ML/egraph/integration) review the docs + X1-X9 data, then synthesize one architecture + ordered build tasks',
  phases: [
    { title: 'Design', detail: 'deduction · constraint-solving · ML · e-graph · integration — in parallel' },
    { title: 'Synthesize', detail: 'unify into LOOP_ARCHITECTURE.md + ordered build-task breakdown' },
  ],
}

const DS = '/home/free/code/milohax/decomp-synth'
const RC = DS + '/docs/plans/reverse-compilation'
const ARCH = DS + '/docs/architecture'
const DATA = RC + '/data'
const REFS = '/home/free/code/milohax/reverse-compiler-refs'
const DESIGN = RC + '/design'

const COMMON = `You are an Opus staff architect designing ONE lens of decomp-synth's production "inverse-compilation
loop". GOAL of the loop: for each workable frontier function, root-cause the first divergence, route to the moves /
oracles that can fix it, gate semantic bets, spend the scarce COMPILE budget only on ranked top-K candidates, and
PROVE floors instead of burning compiles — closing real matches. The MVP ladder X1-X9 is DONE with data; you are
designing how to assemble the validated pieces into a production loop.

RE-READ (the user asked to review the docs again):
- ${ARCH}/INVERSE_COMPILATION.md  (preimage/orbit framing, 3 lenses, Layer 0-4 architecture, floor oracle)
- ${RC}/MVP_VALIDATION_ROADMAP.md  (esp. the "Results & Build Recommendation — X1-X9 COMPLETE" capstone)
- ${DATA}/INDEX.md  (per-experiment headline data) + the specific ${DATA}/X*.md for your lens
- your lens's sub-plan doc (named below)
Tools already built live in ${DS}/tools/revcomp/. Reference shelf + per-repo reports: ${REFS}/ (_reports/, README.md).

Be concrete and DATA-DRIVEN: cite the X-experiment numbers. Honor the data even when it complicates the user's wish
(e.g. X7 said egg composition didn't pay, X9 said Souffle is 3-4 orders slower at our scale) — design the RIGHT role
for each tool, not a strawman and not a rejection. Specify: components (reuse existing tool vs new code + file paths),
the exact interfaces/records they exchange, integration points with the other lenses, what's Python-now vs Rust-later,
and open questions. Write your design to ${DESIGN}/<lens>.md, then return the StructuredOutput.`

const LENSES = [
  { key: 'deduction', label: 'design:deduction-datalog',
    doc: `${RC}/PROVENANCE_AND_FRONTIERS.md`,
    body: `LENS: DEDUCTION (root-cause + provenance). Design the loop's root-cause engine: the Def/Use/Feeds/RootMismatch
fixpoint that localizes the FIRST divergence and prunes irrelevant moves. Reuse ${DS}/tools/revcomp/fixpoint_provenance.py
(X9 proved the 24-line hand-rolled fixpoint is 3-4 orders faster than Souffle at our one-function scale) as the FAST
default; specify EXACTLY when Souffle (built, ${DS}/tools/revcomp/souffle_provenance.dl) earns its place (cross-function /
whole-program provenance, ICF/call-graph closure) and how it'd plug in there. Define the RootCauseRecord (root instr,
mismatch class, source region, eligible moves, frontier_kind) that the ML ranker + oracles + planner consume. Account
for X2's fix-family ceiling (opcode diff can't recover statement_reorder/variable_extraction) and X3's floor classes.
Read ${DATA}/X2_classifier_eval_v2.md, X3_floor_census.md, X9_souffle_bakeoff.md.` },
  { key: 'constraint-solving', label: 'design:z3-oracles',
    doc: `${RC}/DECISION_ORACLES.md`,
    body: `LENS: CONSTRAINT SOLVING (Z3 decision-oracle registry). Design the oracle registry that plugs into the loop:
the X5 FPR-decl-order solver (${DS}/tools/revcomp/fpr_solver.py — solve-in-one + UNSAT floor proof) and the X6
neutrality oracle (${DS}/tools/revcomp/neutrality_oracle.py — pre-compile gate) as the first two oracles, plus a concrete
shortlist of NEW narrow Z3 oracles worth modeling (stack-slot coloring, branch fusion, GPR-given-decl-order) — and which
to DEFER (general regalloc coalescing = backend floor per Stream3). Define the DecisionOracle interface
(observes/controls/required_facts/model_kind/confidence/emits/frontier_kind) and how an oracle returns: a solved
source edit, a neutrality gate verdict, or a UNSAT floor proof. Read ${DATA}/X4_fpr_atlas.md, X5_smt_inversion.md,
X6_neutrality_oracle.md. Scope SMT to narrow modeled decisions only.` },
  { key: 'ml', label: 'design:ml-ranker',
    doc: `${RC}/LEARNING_LOOP.md`,
    body: `LENS: MACHINE LEARNING (the ranker + the rich git-history dataset). Design (1) the RICH DATASET transformed
from git history: take the X2b corpus (${DS}/tools/revcomp/historical_fixtures.db — 1,336 before/after fixtures, the
'validated' score-swept table) + pattern_runs (142K) + climb_variant + the functions symptom flags, and define an
ML-ready schema: features (root mismatch class, symptom flags, function size/shape, move) -> labels (move-helped,
delta, reached-100, induced class). Include the move-synthesis-from-solved-pairs angle (what transform connects
before->after). (2) The SMALL MODEL: type (gradient-boosted trees / logistic — NOT a neural decompiler; X8 showed a
frequency prior + bandit already work), prediction heads (P(move helps | root class), P(no-asm), P(reaches-100)), how
it RANKS moves in the loop (replacing/augmenting the X1 Wilson prior + X8 bandit warm-start), and calibration. Respect
the fix-family ceiling (the model can't recover what the opcode diff doesn't encode -> may need source-side features).
Note sklearn is NOT installed (propose: install scikit-learn, or numpy-only GBT/logistic). Read X1_ranker_mining.md,
X1b_routing_savings_DC3.md, X8_budget_allocator.md, X2b_score_sweep.md.` },
  { key: 'egraph', label: 'design:egraph-orbit',
    doc: `${RC}/ORBIT_GENERATION.md`,
    body: `LENS: E-GRAPHS / ORBIT. The user explicitly wants e-graphs IN the loop. Design the orbit enumerator's role
HONESTLY given X7's data (composition helped 0/12; best% non-monotone in rule count; the value was the X6 neutrality
gate + X1-ranked top-K, NOT a smarter extractor; same rule at different sites gives opposite results so
enumeration+compile-verify discriminates, not a cost model). Recommend the integration: extend the existing Python
bounded-DAG orbit (${DS}/tools/revcomp/orbit.py) vs adopt a real e-graph (egglog-python bindings, or egg via a small
Rust/PyO3 bridge — egg is cloned Rust at ${REFS}/egg; read ${REFS}/_reports/egg.md). Design egg's role as an
equality-saturation ENUMERATOR + equivalence ORACLE feeding X6-gated, ML-ranked top-K compiles, and the decision gate
for WHEN saturation beats bounded enumeration (the byte/bit second-MVP family). Be concrete about the Python-runnable
path now vs the Rust path later. Read ${DATA}/X7_orbit.md.` },
  { key: 'integration', label: 'design:integration-architect',
    doc: `${ARCH}/INVERSE_COMPILATION.md`,
    body: `LENS: INTEGRATION ARCHITECT (the loop orchestrator = INVERSE_COMPILATION Layer 4 planner). Design the end-to-end
loop that ties the other four lenses together: floor-gate (X3 structural + X5 UNSAT + X6 neutrality — three independent
signals) -> root-cause (deduction) -> rank moves (ML) -> solve modeled levers / gate semantic bets (Z3 oracles) ->
orbit-enumerate local regions (e-graph) -> budget-allocate compiles (X8 bandit, ${DS}/tools/revcomp/budget_allocator.py)
-> compile-verify (Scorer) -> cache negatives -> emit win OR floor-proof. Define the data flow + the shared records
(RootCauseRecord, MoveContract, DecisionOracleResult, AttemptRecord, FrontierRecord), the per-function control flow,
how it reuses ${DS}/tools/revcomp/closing_sweep.py (the X5b stack that already closed MetagameRank), what's Python-now
vs Rust-later (per RUST_IMPLEMENTATION.md: objdiff-core + powerpc-rs), and how the loop SWEEPS the X3b worklist
(${DATA}/X3b_frontier_worklist.json, 1,356 targets). Read MVP_VALIDATION_ROADMAP.md capstone, X5b_routed_closing_sweep.md,
${RC}/RUST_IMPLEMENTATION.md, MOVE_CONTRACTS.md.` },
]

const DESIGN_SCHEMA = {
  type: 'object', additionalProperties: false,
  properties: {
    lens: { type: 'string' }, report_path: { type: 'string' }, design_summary: { type: 'string' },
    components: { type: 'array', items: { type: 'object', additionalProperties: false, properties: {
      name: { type: 'string' }, what: { type: 'string' }, reuses: { type: 'string' }, new_code: { type: 'string' },
    }, required: ['name', 'what'] } },
    interfaces: { type: 'array', items: { type: 'string' } },
    integration_points: { type: 'array', items: { type: 'string' } },
    python_now_rust_later: { type: 'string' },
    open_questions: { type: 'array', items: { type: 'string' } },
  },
  required: ['lens', 'report_path', 'design_summary', 'components', 'integration_points'],
}

phase('Design')
const designs = (await parallel(LENSES.map(l => () =>
  agent(`${COMMON}\n\nYOUR LENS DOC: ${l.doc}\n\n${l.body}\n\nWrite to ${DESIGN}/${l.key}.md.`,
    { label: l.label, phase: 'Design', schema: DESIGN_SCHEMA })
))).filter(Boolean)

phase('Synthesize')
const synthPrompt = `You are the Opus integration lead. Unify the ${designs.length} lens designs into ONE production
loop architecture + an ordered, parallelizable BUILD-TASK breakdown. Read the five ${DESIGN}/*.md design notes and the
capstone in ${RC}/MVP_VALIDATION_ROADMAP.md.

Structured designs:
${JSON.stringify(designs, null, 1)}

WRITE ${RC}/LOOP_ARCHITECTURE.md: (a) the loop's end-to-end control flow + ASCII diagram (Layer 0-4); (b) the shared
record schemas; (c) per-component spec (reuse vs new, file paths); (d) the three-lens integration (deduction/Z3/ML +
e-graph orbit) with the DATA-justified role of each tool (incl. the honest egg/Souffle scope); (e) the rich-dataset +
small-model plan; (f) what's Python-now vs Rust-later; (g) the sweep plan over the X3b worklist.

RETURN the StructuredOutput: an ORDERED list of build tasks (each: id, title, component, what-to-build, owns_files,
depends_on, lens, parallel_group) that I will hand to build subagents — designed so independent tasks have DISJOINT
file ownership and can run in waves of <=6. Also: dataset_tasks, model_tasks, the loop_orchestrator task, and the
sweep task, plus any tool to install (scikit-learn? egglog? egg PyO3?). Be decisive; resolve egg-vs-DAG and
Datalog-vs-fixpoint WITH the data.`

const SYNTH_SCHEMA = {
  type: 'object', additionalProperties: false,
  properties: {
    architecture_doc: { type: 'string' },
    build_tasks: { type: 'array', items: { type: 'object', additionalProperties: false, properties: {
      id: { type: 'string' }, title: { type: 'string' }, component: { type: 'string' }, what: { type: 'string' },
      owns_files: { type: 'array', items: { type: 'string' } }, depends_on: { type: 'array', items: { type: 'string' } },
      lens: { type: 'string' }, parallel_group: { type: 'integer' },
    }, required: ['id', 'title', 'what', 'owns_files', 'lens', 'parallel_group'] } },
    tools_to_install: { type: 'array', items: { type: 'string' } },
    sequencing_notes: { type: 'array', items: { type: 'string' } },
    headline: { type: 'string' },
  },
  required: ['architecture_doc', 'build_tasks', 'headline'],
}

const synth = await agent(synthPrompt, { label: 'synthesize:loop-architecture', phase: 'Synthesize', schema: SYNTH_SCHEMA })
return { designs: designs.length, synth }
