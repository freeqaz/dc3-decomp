export const meta = {
  name: 'revcomp-mvp4',
  description: 'Wave 4: X5 SMT FPR decl-order inversion + CEGIS floor proof, and X6 alive2-shape behaviour-neutrality oracle',
  phases: [{ title: 'Wave4', detail: 'X5 SMT inversion + X6 neutrality oracle, in parallel (z3-based, near-zero compile)' }],
}

const DS = '/home/free/code/milohax/decomp-synth'
const DC3 = '/home/free/code/milohax/dc3-decomp'
const DATA = DS + '/docs/plans/reverse-compilation/data'
const ROADMAP = DS + '/docs/plans/reverse-compilation/MVP_VALIDATION_ROADMAP.md'

const COMMON = `Staff-level decomp tooling engineer on decomp-synth's inverse-compilation MVPs. Read ${ROADMAP} and
${DATA}/INDEX.md first. Use \`python3\` (resolves to ${DC3}/venv/bin/python3 which has decomp_synth + z3 4.16.0
installed); if import decomp_synth fails set PYTHONPATH=${DS}. NEVER git stash/commit/revert (read-only git only).
Build A+ quality, argparse CLIs, real unit-style validation. Report every claim as a NUMBER; a refuted hypothesis
or a counterexample is a SUCCESS (the whole point is to prove things, not to hand-wave).`

// ── X5: SMT inversion of the FPR decl-order lever + CEGIS UNSAT floor proof ──
const x5 = `${COMMON}

TASK X5 — prove "solve, don't search" on the FPR declaration-order lever, and produce the project's first machine-checked
FLOOR PROOF. You OWN: ${DS}/tools/revcomp/fpr_solver.py.

INPUT MODEL (from X4, already validated — read ${DATA}/X4_fpr_atlas.{md,json}): MSVC assigns callee-saved FPRs f31-DOWN
by FIRST-WRITE order of float locals. Closed form: a variable whose first-write rank is k (k=1 is the first float
written) lands in FPR (32 - k) for the first up-to-18 callee-saved slots (f31, f30, ...). first-USE/liveness has ZERO
effect. So the TARGET binary's FPR homes for a function's float values directly encode a constraint on first-write order.

BUILD a z3-based solver (read decomp_synth/constraint_solver.py first for the existing hand-rolled decl-order logic to
align naming, but implement the SMT version with the z3 python module):
 1. Model: integer rank var per float local (1..N), all-different (a permutation). Constraint fpr(v) == 32 - rank(v)
    for each v whose target FPR home is known. Dependency edges: if v's initializer reads w, then rank(w) < rank(v)
    (w must be first-written before v) — i.e. data-dependency partial order on first-write.
 2. SOLVE: given a target FPR->value assignment, solve for the rank permutation (the source declaration/first-write
    order) in ONE z3 check. Emit the recovered order as a concrete source-edit recipe.
 3. CEGIS / UNSAT floor proof: construct a target FPR assignment that is INCONSISTENT with the dependency partial order
    (e.g. it demands w land in a lower FPR than v while v depends on w). z3 must return UNSAT -> that is a
    machine-checked PROOF that no declaration order reproduces the target for this lever = a real floor. Print the
    unsat core. (Optionally add a CEGIS loop shape per rosette's verify=solve-of-negation, but a direct UNSAT is the
    core deliverable.)

VALIDATE (acceptance):
 a. Round-trip on the X4 synthetic fixture: take a known permutation's observed FPR homes as the "target", solve,
    confirm the solver recovers EXACTLY that permutation in ONE solve (not a search). Do this for several permutations.
 b. UNSAT case: confirm the dependency-violating target returns UNSAT with a sensible unsat core.
 c. Quantify solve-vs-search: 1 z3 solve + 1 confirming compile vs the historical beam cost for FPR-order mismatches
    (cite X1/X1b numbers: blind cpw ~398 for register_swap). State the compile-budget saving.
 d. BONUS (only if time): find a REAL frontier function (from fixtures.db / historical / a >=99% near-miss with an FPR
    decl-order mismatch), solve the order, splice via revfix/Scorer, and confirm 100% — a real end-to-end win. If none
    is readily found, say so; do NOT spend many compiles hunting.

Write ${DATA}/X5_smt_inversion.md (the model, round-trip results, the UNSAT floor proof + core, the savings number, any
real-function application). Return the StructuredOutput.`

// ── X6: behaviour-neutrality oracle (alive2-shape, pre-compiler gate) ──
const x6 = `${COMMON}

TASK X6 — build a tiny SMT behaviour-neutrality oracle that proves/refutes whether a "move" is behaviour-preserving,
WITHOUT a compile. This validates Principle 4 (precondition > symptom): a move that is NOT neutral should never be
tried (it would either break behaviour or its "win" is a coincidence). You OWN: ${DS}/tools/revcomp/neutrality_oracle.py.

Using the z3 python module, encode each move's LHS and RHS as z3 BITVECTOR expressions over 32-bit ints (model C int
semantics: two's complement, arithmetic vs logical shift, signed vs unsigned compares) and decide:
  forall inputs.  pre(inputs) -> (lhs(inputs) == rhs(inputs))
returning one of: PROVEN_NEUTRAL (unconditional), NEEDS_PRECONDITION (neutral only under a derivable pre — report the
pre), or REFUTED (+ a concrete counterexample input). Implement via verify = check-unsat-of-negation (rosette shape).

MOVES TO TEST (the high-value conditional/integer family from the patterns + memory):
 - int-abs open-coded \`(v ^ (v>>31)) - (v>>31)\` vs ternary \`v<0 ? -v : v\` vs builtin abs — read
   ${DS}/decomp_synth/patterns/int_abs_to_ternary.py (a KNOWN-REAL win, e.g. ScrollToTarget 99.4->100,
   JumpedMeasureAdd 94.2->100). Expect PROVEN_NEUTRAL (they must be equivalent, else the real wins are inexplicable).
 - \`x > 0\` vs \`x != 0\`: expect REFUTED for SIGNED x (counterexample x=-1) but NEEDS_PRECONDITION x>=0 / PROVEN for
   UNSIGNED x. This is the canonical precondition case (CLAUDE.md known pattern: unsigned x>0 vs x!=0).
 - signed/unsigned reinterpret on a compare (cmpw vs cmplw equivalence under a value-range precondition).
 - DeMorgan: \`!(a && b)\` vs \`!a || !b\` (PROVEN_NEUTRAL over booleans).
 - commutative swaps (a+b vs b+a, a*b vs b*a) PROVEN_NEUTRAL; and a NON-neutral trap (a-b vs b-a) REFUTED.

VALIDATE (acceptance): the int_abs forms prove NEUTRAL; signed \`x>0\`/\`x!=0\` is REFUTED with counterexample x=-1 and
PROVEN under unsigned/precondition; \`a-b\` vs \`b-a\` REFUTED. Report how this gates the permuter: a move that is
REFUTED or only-NEEDS_PRECONDITION (without the pre holding) should be SUPPRESSED pre-compile — quantify how many of
decomp-synth's "semantic bet" patterns this could certify or reject. Tie to the X1 dead-weight finding (some patterns
emit non-neutral / non-compiling variants).

Write ${DATA}/X6_neutrality_oracle.md (per-move verdict + counterexamples + the pre-compiler gating implication).
Return the StructuredOutput.`

const S_X5 = { type: 'object', additionalProperties: false, properties: {
  tool_path: { type: 'string' }, report_path: { type: 'string' },
  recovered_order_in_one_solve: { type: 'boolean' }, roundtrip_permutations_ok: { type: 'integer' },
  unsat_floor_demonstrated: { type: 'boolean' }, solve_vs_search_savings: { type: 'string' },
  real_function_applied: { type: 'boolean' }, headline: { type: 'string' },
}, required: ['tool_path', 'report_path', 'recovered_order_in_one_solve', 'unsat_floor_demonstrated', 'headline'] }

const S_X6 = { type: 'object', additionalProperties: false, properties: {
  tool_path: { type: 'string' }, report_path: { type: 'string' }, moves_tested: { type: 'integer' },
  proven_neutral: { type: 'array', items: { type: 'string' } },
  needs_precondition: { type: 'array', items: { type: 'string' } },
  refuted_with_counterexample: { type: 'array', items: { type: 'string' } },
  headline: { type: 'string' },
}, required: ['tool_path', 'report_path', 'moves_tested', 'proven_neutral', 'refuted_with_counterexample', 'headline'] }

phase('Wave4')
const [x5r, x6r] = await parallel([
  () => agent(x5, { label: 'X5:smt-fpr-inversion', phase: 'Wave4', schema: S_X5 }),
  () => agent(x6, { label: 'X6:neutrality-oracle', phase: 'Wave4', schema: S_X6 }),
])
return { x5_smt_inversion: x5r, x6_neutrality: x6r }
