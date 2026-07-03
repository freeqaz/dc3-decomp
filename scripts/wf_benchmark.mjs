export const meta = {
  name: 'loop-benchmark',
  description: 'Build a project-parameterized loop benchmark harness (precision/recall/F1 + throughput), run it on DC3, generate rb3-xenon frontier+sweep, then benchmark rb3-xenon + DC3-vs-rb3-xenon comparison',
  phases: [
    { title: 'BuildAndGen', detail: 'benchmark.py + DC3 run  ∥  rb3-xenon frontier+sweep datagen' },
    { title: 'CompareXenon', detail: 'benchmark rb3-xenon + DC3-vs-rb3-xenon comparison' },
  ],
}

const DS = '/home/free/code/milohax/decomp-synth'
const R = DS + '/tools/revcomp'
const D = DS + '/docs/plans/reverse-compilation/data'
const DC3 = '/home/free/code/milohax/dc3-decomp'
const XEN = '/home/free/code/milohax/rb3-xenon'

const COMMON = `Opus engineer on decomp-synth's inverse-compilation loop. Read ${DS}/docs/plans/reverse-compilation/LOOP_ARCHITECTURE.md + ${D}/INDEX.md first. decomp-synth is pip-installed (import decomp_synth); python3 = ${DC3}/venv/bin/python3 (sklearn/z3/numpy). NEVER git stash/commit/revert. Write your tool file EARLY and iterate (resilience). Every claim is a NUMBER with explicit numerator/denominator + n; be brutally honest about small-n and denominators.`

// ── A1: build the benchmark harness (project-parameterized) + run DC3 ──
const a1 = `${COMMON}

TASK — build the UNIFIED BENCHMARK HARNESS \`${R}/benchmark.py\` (argparse, PROJECT-PARAMETERIZED) and run it on DC3.
You OWN: ${R}/benchmark.py, ${D}/BENCHMARK_DC3.md, ${D}/benchmark_dc3.json.

It must take per-project paths (so it can run on DC3 AND rb3-xenon): --label, --decomp-db, --permuter-cache, --floor-labels,
--sweep-verdicts, --fixtures-db (optional), --historical-db (optional), --ranker (optional). Compute, each with explicit
numerator/denominator + n + caveats:
 1. FLOOR-ORACLE precision/recall/F1 (HEADLINE): positives = synthetic known-fixable fixtures (fixtures.db) that must NOT
    be floored + historical validated-recoverable; true floors = X3 structural labels (regalloc_normalized/icf/eh/stub).
    Score the loop's floor decision (X3 structural + X5 UNSAT + X6 + the sweep's FLOOR_PREDICTED). Report FALSE-FLOOR rate
    (known-fixable called floor) and FALSE-WORKABLE rate separately.
 2. ROOT-CAUSE accuracy + per-class P/R: confusion matrix on synthetic fixtures (induced class known); reuse eval_classifier.
 3. RANKER precision@K/recall@K (K=1,3,5) on fixtures with a known fix family (via move_ranker + ranker.npz); + AUC/Brier/ECE
    (move_ranker eval) + prior-divergence rate.
 4. ORACLE correctness: X5 fpr_solver round-trip (SAT recovers order; UNSAT only when truly unsat) + X6 verdicts vs ground
    truth — pass/fail counts.
 5. END-TO-END from --sweep-verdicts: close-rate + partial-rate over BOTH denominators — (a) full frontier, (b) the
    genuinely-workable&reproducing&authorable subset (exclude NON_REPRODUCING + NOT_AUTHORABLE + structural floors).
 6. THROUGHPUT: compiles/sec, compiles-per-close, compiles-per-workable, mean s/compile (parse sweep verdicts + score_cache
    timestamps + /tmp/claude/loop_sweep_full.log). Flag the ~18.5s/compile bottleneck.
Make it a re-runnable registry of benchmark "cases" so missing ground-truth (e.g. rb3-xenon has no fixtures) degrades to an
explicit "not measurable (no fixtures)" line, NOT a crash.

RUN on DC3: --label DC3-MSVC --decomp-db ${DC3}/decomp.db --permuter-cache ${DC3}/permuter_cache.db --floor-labels
${D}/X3_floor_labels.json --sweep-verdicts ${D}/sweep_verdicts_full.jsonl --fixtures-db ${R}/fixtures.db --historical-db
${R}/historical_fixtures.db --ranker ${R}/ranker.npz. Write BENCHMARK_DC3.md + benchmark_dc3.json. Return the StructuredOutput
with the headline scorecard.`

// ── A2: rb3-xenon frontier + bounded sweep (cross-MSVC generalization datagen) ──
const a2 = `${COMMON}

TASK — generate rb3-xenon's loop artifacts so we can benchmark a SECOND MSVC target (cross-target generalization test).
You OWN (additive --project/--paths args only; NEVER break DC3 defaults): ${R}/floor_census.py, ${R}/frontier_worklist.py,
${R}/run_loop.py, plus outputs ${D}/X3_floor_labels_xenon.json, ${D}/X3b_frontier_worklist_xenon.json,
${D}/sweep_verdicts_xenon.jsonl, ${D}/rb3xenon_datagen.md, ${R}/move_priors_xenon.json.

rb3-xenon is at ${XEN} (RB3 built with MSVC — same c2.dll family as DC3; 9,519 cpp; decomp.db 21M; permuter_cache.db 33M,
9,617 pattern_runs). KNOWN GAP: its decomp.db functions has_* symptom flags are UNPOPULATED (X1 finding) — so symptom
routing is weaker; rely on the LIVE guided diagnosis instead.

STEPS (be honest if a step isn't feasible):
 1. BUILD-ENV CHECK: from cwd=${XEN}, can decomp_synth.scorer.Scorer build+score ONE rb3-xenon function (pick a near-100 fn
    from its decomp.db)? If the rb3-xenon build env (ninja/objdiff/target objs) is NOT set up, REPORT that honestly and do
    only the zero-compile steps (2,3) — do not try to set up a foreign build system.
 2. X1 prior: run ${R}/mine_attempts.py on rb3-xenon (--specs "rb3xenon-MSVC:${XEN}/permuter_cache.db:${XEN}/decomp.db") and
    emit ${R}/move_priors_xenon.json (the deployable prior; note flags-unpopulated caveat).
 3. FLOOR CENSUS + WORKLIST: run floor_census.py + frontier_worklist.py against rb3-xenon (add --project/--decomp/--report
    /--build args as needed; rb3-xenon's report.json is under its build dir — find it). Emit X3_floor_labels_xenon.json +
    X3b_frontier_worklist_xenon.json. Report the frontier size + floor breakdown vs DC3's (48,413/1,356).
 4. BOUNDED SWEEP (only if step-1 build works): run_loop.py sweep on the rb3-xenon worklist, --bands ">=99" --limit 40,
    using the DC3-trained ${R}/ranker.npz (the generalization test — does a DC3-trained ranker transfer to another MSVC
    game?) AND log to a separate xenon attempts db. Add a --worklist/--project arg to run_loop if it's hardcoded to the DC3
    X3b path. Stream to sweep_verdicts_xenon.jsonl. Report closes/partials/floors + compiles + throughput.

Write ${D}/rb3xenon_datagen.md (build-env status, frontier size + floor breakdown vs DC3, sweep result if run, and the
honest cross-MSVC takeaway). Return the StructuredOutput.`

const A1_SCHEMA = { type:'object', additionalProperties:false, properties:{
  tool_path:{type:'string'}, report_path:{type:'string'},
  floor_precision:{type:'number'}, floor_recall:{type:'number'}, false_floor_rate:{type:'number'},
  rootcause_accuracy:{type:'number'}, ranker_p_at_5:{type:'number'},
  closerate_full:{type:'number'}, closerate_workable:{type:'number'},
  compiles_per_sec:{type:'number'}, headline:{type:'string'} },
  required:['tool_path','report_path','headline'] }

const A2_SCHEMA = { type:'object', additionalProperties:false, properties:{
  build_env_ok:{type:'boolean'}, frontier_size:{type:'integer'}, floor_breakdown:{type:'string'},
  sweep_ran:{type:'boolean'}, closed:{type:'integer'}, partial:{type:'integer'},
  report_path:{type:'string'}, worklist_path:{type:'string'}, verdicts_path:{type:'string'},
  cross_msvc_takeaway:{type:'string'}, headline:{type:'string'} },
  required:['build_env_ok','report_path','headline'] }

phase('BuildAndGen')
const [bench, xen] = await parallel([
  () => agent(a1, { label:'benchmark:build+DC3', phase:'BuildAndGen', schema:A1_SCHEMA }),
  () => agent(a2, { label:'rb3xenon:datagen', phase:'BuildAndGen', schema:A2_SCHEMA }),
])

phase('CompareXenon')
let cmp = null
if (bench && bench.tool_path) {
  const b = `${COMMON}

TASK — run the benchmark harness (${R}/benchmark.py, built this run by A1) on rb3-xenon, then write a DC3-vs-rb3-xenon
comparison. You OWN: ${D}/BENCHMARK_rb3xenon.md, ${D}/benchmark_rb3xenon.json, ${D}/BENCHMARK_COMPARISON.md.

rb3-xenon artifacts from A2: floor-labels ${D}/X3_floor_labels_xenon.json, sweep ${D}/sweep_verdicts_xenon.jsonl (if A2 ran
the sweep), prior ${R}/move_priors_xenon.json. A2 result: ${JSON.stringify(xen)}.

Run: python3 ${R}/benchmark.py --label rb3xenon-MSVC --decomp-db ${XEN}/decomp.db --permuter-cache ${XEN}/permuter_cache.db
--floor-labels ${D}/X3_floor_labels_xenon.json --sweep-verdicts ${D}/sweep_verdicts_xenon.jsonl --ranker ${R}/ranker.npz
(omit --fixtures-db/--historical-db — none exist for rb3-xenon; the harness must emit "not measurable (no fixtures)" for
those cases, not crash). Write BENCHMARK_rb3xenon.md + json.

Then write ${D}/BENCHMARK_COMPARISON.md: side-by-side DC3 vs rb3-xenon on every measurable metric (floor census/breakdown,
X1-prior effectiveness, end-to-end close/partial-rate, throughput), and the HONEST cross-MSVC takeaway: does the
DC3-built loop + DC3-trained ranker generalize to a different MSVC game, or is it DC3-specific? Note what couldn't be
measured on rb3-xenon and why (unpopulated flags, no fixtures). Return the StructuredOutput.`
  const B_SCHEMA = { type:'object', additionalProperties:false, properties:{
    xenon_report:{type:'string'}, comparison_report:{type:'string'},
    generalizes:{type:'string'}, headline:{type:'string'} }, required:['comparison_report','headline'] }
  cmp = await agent(b, { label:'benchmark:rb3xenon+compare', phase:'CompareXenon', schema:B_SCHEMA })
} else {
  log('A1 did not produce benchmark.py; skipping rb3-xenon benchmark.')
}
return { dc3_benchmark: bench, xenon_datagen: xen, comparison: cmp }
