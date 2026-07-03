export const meta = {
  name: 'revcomp-refs-study',
  description: 'Study reverse-compilation reference repos + papers, synthesize a reference index and raw material for an MVP roadmap',
  phases: [
    { title: 'Study', detail: 'one agent per reference repo -> structured reusability report' },
    { title: 'Papers', detail: 'web research on key papers/blogs -> notes' },
    { title: 'Synthesize', detail: 'build reference index + cross-cutting findings + prioritized experiment list' },
  ],
}

const REFS = '/home/free/code/milohax/reverse-compiler-refs'
const PROJECT = `decomp-synth's goal: recover C++ source that, compiled by the original MSVC Xbox360 PowerPC backend (c2.dll), reproduces the target binary byte-identically. The reframe is "matching = inverse compilation": find a behaviour-equivalent source point whose compilation lands in the target's exact preimage. The compiler is the scarce budget; everything else (SMT/Datalog/ML/e-graphs) exists to avoid calling it and to prove floors. Lenses: (1) deductive root-cause + provenance (what feeds what; where the first divergence is), (2) narrow SMT decision-oracles (invert ONE modeled compiler decision, e.g. FPR decl order), (3) ML compiler-distance ranker over logged attempts, (4) local e-graph orbit generation, (5) a compiler "atlas" built from controlled micro-experiments, (6) typed PPC instruction facts, (7) MSVC/PE/PDB + Xbox360 platform facts. Target Rust-first core consuming objdiff-core + powerpc-rs + dtk/jeff.`

// name, path under REFS, the lens it primarily feeds, focused study questions
const REPOS = [
  { name: 'decomp-permuter', lens: 'baseline-permuter',
    focus: 'src/permuter.py, src/randomizer.py, the perm/ randomizer transforms, import.py, scorer, compile-failure handling, PPC/arch support',
    qs: 'What behaviour-neutral transforms does it implement and HOW are they represented/applied? How does it score candidates and handle compile failures? What PPC support exists? Critically: what does it NOT do that our typed-contract/root-cause/learning design adds (it is random/manual macro search)? What concrete code or transform list is worth porting vs what we already surpass?' },
  { name: 'objdiff', lens: 'diff-engine',
    focus: 'objdiff-core/src (diff/, obj/, arch/ppc), the public diff_objs API, ObjectDiff/SymbolDiff/InstructionDiff types, match%/normalized score fields, arg-diff + branch-from/to, data diffs',
    qs: 'Map the objdiff-core Rust public API our core should consume to build a RootCauseRecord: which types/functions give instruction diff rows, match & normalized scores, arg-diff indices, branch relationships, and data-symbol diffs? PPC arch support status? How does objdiff-pairfix fork differ (funclet pairing)? Is linking objdiff-core as a crate dependency clean?' },
  { name: 'asm-differ', lens: 'root-cause',
    focus: 'diff.py alignment/normalization, register-swap classification, mismatch presentation, the PPC arch config',
    qs: 'How does it align and normalize asm, classify register swaps, and present mismatches? What alignment/normalization heuristics are worth replicating in our rule-based root-mismatch classifier? PPC specifics?' },
  { name: 'm2c', lens: 'skeleton-bootstrap',
    focus: 'PPC backend quality, output conventions/style, how it structures recovered C',
    qs: 'How capable is its PPC->C backend? Could its output seed the "semantic skeleton" (the held-fixed compute) for functions we have not started? What are its output conventions and known limitations for matching-decomp bootstrapping?' },
  { name: 'decomp-toolkit', lens: 'project-infra',
    focus: 'object/COFF handling, project metadata, symbol/split handling, PPC code processing, analysis passes; ALSO read ../jeff (rjkiv fork: XEX/XPDB, powerpc 0.4.1 dep, objdiff-core fork) to compare',
    qs: 'What object/COFF/XEX handling, project metadata, symbol/split, and PPC-code-processing crates/APIs could our Rust core depend on for project integration and header/cross-function routing? How does the jeff fork (sibling symlink ../jeff) extend dtk for Xbox360 (XEX/XPDB)? Should we depend on jeff as a crate, factor shared code out, or path/patch it?' },
  { name: 'powerpc-rs', lens: 'ppc-facts',
    focus: 'the powerpc disassembler crate + powerpc-asm assembler, instruction/opcode/operand types, register-class + CR + branch-target classification, VMX128 recognition',
    qs: 'Map the API for decoding PPC into typed opcodes/operands, classifying register classes (GPR/FPR/VR), CR usage, branch targets, immediates, memory refs, and recognizing VMX128/Xenon instructions. Crate name(s) and version? Assembler usable for probe generation? Completeness for MSVC Xbox360 PPC? This is the base for our typed PPC instruction facts.' },
  { name: 'egg', lens: 'orbit-egraph',
    focus: 'EGraph/Language/Rewrite/Runner/Extractor/CostFunction API, defining a small expr language, custom cost extraction, performance, egg vs egglog',
    qs: 'How would we use egg for LOCAL compare/branch + integer-expression orbit generation with a CUSTOM cost = predicted target-codegen distance (NOT smallest expr)? Map the API to define a tiny language, add behaviour-neutral rewrites, run saturation bounded, and extract top-K by custom CostFunction. Is egg right vs a hand-rolled bounded DAG for our MVP? Note egg vs egglog tradeoffs.' },
  { name: 'souffle', lens: 'provenance-datalog',
    focus: 'relation/.decl model, fact ingestion (CSV/TSV), execution modes (interpreter vs compiled C++), embedding/invoking, performance',
    qs: 'For root-mismatch provenance (Def/Use/Feeds/Mismatch/RootMismatch fixpoint): how do we feed facts and run it (souffle binary vs compiled C++ vs interpreter)? Rust interop options? At our scale (one function, dozens-hundreds of instrs) is Souffle worth it vs a plain Rust fixpoint for the MVP? When would it pay off?' },
  { name: 'souper', lens: 'smt-oracle',
    focus: 'candidate generation, SMT query construction (Z3/CVC), the result cache (Redis/sqlite), pruning, the dataflow/synthesis pipeline',
    qs: 'Souper is an LLVM-IR superoptimizer using SMT. We want its PATTERN, not its IR: extract local context -> parameterize -> encode rule -> solve -> verify once. How does it build candidate exprs, query the solver, and CACHE results? What is reusable as a mental model (and any code) for our narrow SMT decision-oracles like decl-order / stack-slot coloring inversion?' },
  { name: 'alive2', lens: 'smt-refinement',
    focus: 'bounded refinement-check encoding, poison/undef/memory modeling, how a transform is proven semantics-preserving',
    qs: 'Alive2 does bounded refinement checking of LLVM transforms via SMT. We want the IDEA: bounded LOCAL refinement to validate that a "move" is behaviour-neutral. Is any of this reusable for enforcing our move semantic_preservation_claim, or is it conceptual-only? Be honest. What is the minimal version of "bounded equivalence check" we could build?' },
  { name: 'stoke', lens: 'cost-function',
    focus: 'cost function design (correctness + perf terms, Hamming-style distance), MCMC proposal distribution over program edits, validation harness',
    qs: 'STOKE = stochastic superoptimization over x86. We INVERT the objective: match ONE specific target, not minimize cost. Steal: their cost-function shaping (correctness + distance terms) and MCMC proposal design over edits. What concretely maps to our compiler-distance score and to proposing moves? Conceptual-only or any reusable structure?' },
  { name: 'compilergym', lens: 'rl-env',
    focus: 'Env API, observation/action/reward/done spaces, the service/backend architecture, available compiler tasks',
    qs: 'We want CompilerGym\'s ENVIRONMENT ABSTRACTION to model our planner loop (state = function+diff+root; action = typed move; reward = root movement + match delta; done = 100% or proven floor). Map the Env/observation/reward API and the service architecture. Which abstractions are worth copying for our learning loop? Is the framework worth a dependency or just a design reference?' },
  { name: 'opentuner', lens: 'autotuning',
    focus: 'ask/tell loop, the AUC-bandit ensemble over multiple search techniques, ConfigurationManipulator search-space representation; ALSO skim ../nevergrad (gradient-free optimizers) if present',
    qs: 'OpenTuner: ask/tell autotuning with a bandit that allocates trials across an ENSEMBLE of search techniques. How does the bandit allocate budget and how is the search space represented? How would this map to allocating our scarce COMPILE budget across competing moves/oracles? Note any nevergrad (sibling ../nevergrad) optimizers worth knowing. Dependency or design reference?' },
  { name: 'rosette', lens: 'solver-aided-dsl',
    focus: 'symbolic values + holes, assert/verify/synthesize forms, solver backends, the symbolic-eval design',
    qs: 'Rosette = solver-aided programming (symbolic eval, holes, synthesize/verify) in Racket. We will NOT use Racket. Extract the DESIGN PATTERN (symbolic values, assertions, holes, solver backend) we would replicate with Z3 in Rust/Python to build tiny DSLs that solve move parameters (e.g. declaration order). What is the minimal architecture to copy?' },
  { name: 'XenonRecomp', lens: 'xbox-platform',
    focus: 'PPC instruction semantics tables/handlers, VMX128 handling, XEX parsing, ABI/runtime assumptions, generated-C++ structure; ALSO ../idaxex (XEX loader) if present',
    qs: 'XenonRecomp statically recompiles Xbox360 XEX -> C++. We want PLATFORM FACTS: which PPC instruction-semantics tables / VMX128 handling / XEX-structure parsing are directly reusable (or transcribable) for our PPC facts and object loading? How does idaxex (sibling ../idaxex) parse XEX? Reusable code vs reference?' },
  { name: 'recompiler', lens: 'xbox-platform',
    focus: 'PPC semantics decoding, Xbox360 support, runtime assumptions (kernel/gfx/fs/input), generated-C++ structure; ALSO ../rexglue-sdk (Xbox360 recomp runtime) if present',
    qs: 'rexdex/recompiler is a generic static recompiler incl. Xbox360; rexglue-sdk (sibling ../rexglue-sdk) is an Xbox360 recomp runtime. Mostly platform-correctness reference: note any PPC-semantics tables, XEX handling, or runtime facts worth cross-checking against XenonRecomp. Anything directly reusable, or reference-only?' },
  { name: 'XEXLoaderWV', lens: 'xbox-loader',
    focus: 'XEX header/section/import parsing logic (Java, Ghidra extension)',
    qs: 'How does it parse XEX headers/sections/imports for Xbox360? Is the XEX format logic / notes reusable for our object/project layer (even if we reimplement in Rust)? Capture concrete format facts (header fields, compression, import tables).' },
  { name: 'msvc-pe-pdb', lens: 'msvc-pe-facts', path: REFS + '/microsoft-pdb',
    focus: 'Read ../microsoft-pdb (official PDB format), ../pdb-decompiler, ../pecoff + ../pe-parse (PE/COFF), ../pe-unwind-info, ../mwcc-debugger. Several are siblings of microsoft-pdb in the refs dir.',
    qs: 'Which of these MSVC/PE/PDB tools help us (a) reverse-engineer c2.dll decisions, (b) parse /FAcs + /Z7 CodeView for source->instruction attribution, or (c) map PDB symbol/type info to ground-truth struct layouts and local-variable register/stack homes (we already use a /Z7 CodeView recompile in our stack-layout tooling)? Which feed INSTRUCTION_ATTRIBUTION / the compiler atlas? Be explicit about LOW-relevance ones (e.g. pe-unwind-info is x64; our target is PPC; mwcc is MetroWerks/RB3).' },
  { name: 'decomp.me', lens: 'corpus-storage',
    focus: 'the scratch/compiler data model (backend), how it stores source+target+compiler+flags+score, the compiler-invocation sandbox, objdiff usage',
    qs: 'decomp.me is a web scratchpad + public corpus of compiler-matching scratches. What is its data model for (source, target, compiler, flags, score)? How does it sandbox compiler invocations and use objdiff? Is any of its backend a reusable pattern for our experiment/atlas storage and our compile-job scheduling? Note decomp-me-mcp exists.' },
]

const PAPERS = [
  { name: 'synthesis_superopt_egraph',
    title: 'Synthesis / superoptimization / e-graph papers',
    targets: 'PrediPrune (ML-driven pruning to reduce SMT verification overhead in Souper); the STOKE "Stochastic Superoptimization" paper; the egg / equality-saturation paper (egraphs-good); "E-Path: Equality Saturation for Control-Flow Graphs" (extending eq-sat to CFGs); Souper. Search by TITLE not by any arXiv ID I give you (some IDs may be wrong/hallucinated) and FLAG any paper you cannot actually find.',
    angle: 'For each: the core idea in 2-3 lines; what is DIRECTLY applicable to our preimage/orbit framing (rank-before-expensive-verify, cost-function design, eq-sat for local orbits, eq-sat for CFG); concrete techniques to steal; and caveats / where it does NOT transfer to byte-exact matching of an opaque backend.' },
  { name: 'llm_decomp_and_framing',
    title: 'LLM decompilation + matching-decomp methodology',
    targets: 'PCodeTrans (translate decompiled pseudocode to compilable+executable equivalents via recompilation + dynamic validation feedback); the "ChatGPT isn\'t a decompiler... yet" blog by Stephen Jayakar (PowerPC/Melee experience); Decompedia (decomp.wiki) matching-decomp framing; general matching-decomp methodology (decomp-permuter writeups, objdiff workflow). Search by TITLE and FLAG anything you cannot find.',
    angle: 'For each: core idea; the RIGHT role for LLMs in our system (reviewer / proposal helper / skeleton-wrong detector -- NOT equivalence verifier); the feedback-driven recompile+validate loop design; and concrete cautions from real PPC matching-decomp experience.' },
]

function studyPrompt(r) {
  const path = r.path || (REFS + '/' + r.name)
  const report = REFS + '/_reports/' + r.name + '.md'
  return `You are a reverse-engineering + compiler-tooling analyst studying ONE reference repo for the decomp-synth project.

PROJECT CONTEXT: ${PROJECT}

REPO: \`${r.name}\` at local path: \`${path}\`
PRIMARY LENS it feeds: ${r.lens}
FOCUS your reading on: ${r.focus}

FIRST: check the path exists (e.g. \`ls\` / Read the README). If it is missing or empty, the clone failed -- do a focused WebSearch/WebFetch on the repo's README/docs instead and set availability="clone_failed_web_only". Otherwise availability="studied".

Read the README and SAMPLE the most relevant source (5-15 files max -- do NOT read everything; you want the API shape and the reusable parts, not a full audit).

ANSWER THESE QUESTIONS: ${r.qs}

Be concrete and skeptical. Distinguish "directly reusable code/API we can link/call/port (give file paths + symbol names)" from "conceptual takeaway only". Call out where this tool would be a TRAP or over-engineering for our MVP scale. Remember the north star: minimize calls to the real compiler; everything is judged by whether it makes the next compile smarter or proves a floor.

THEN write a markdown report to \`${report}\` with sections:
# ${r.name}
- **What it is** / **License & build** / **Key files (paths)** / **Directly reusable for decomp-synth** / **Conceptual takeaways** / **Integration notes (Rust-first core)** / **Gotchas / caveats** / **Recommendation + why**

FINALLY return the StructuredOutput object. report_path = \`${report}\`.`
}

const STUDY_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  properties: {
    name: { type: 'string' },
    availability: { type: 'string', enum: ['studied', 'clone_failed_web_only', 'empty'] },
    one_liner: { type: 'string' },
    license: { type: 'string' },
    language_build: { type: 'string' },
    primary_lens: { type: 'string' },
    reusable_now: { type: 'array', items: { type: 'string' }, description: 'concrete code/APIs/paths we could link/port/call' },
    conceptual_takeaways: { type: 'array', items: { type: 'string' } },
    integration_notes: { type: 'string', description: 'Rust-first core integration: link / vendor / port / reimplement / reference' },
    gotchas: { type: 'array', items: { type: 'string' } },
    recommendation: { type: 'string', enum: ['link-now', 'vendor-or-port', 'study-deeper', 'reference-only', 'skip'] },
    rec_reason: { type: 'string' },
    report_path: { type: 'string' },
  },
  required: ['name', 'availability', 'one_liner', 'primary_lens', 'reusable_now', 'conceptual_takeaways', 'integration_notes', 'recommendation', 'rec_reason', 'report_path'],
}

function paperPrompt(p) {
  const report = REFS + '/papers/' + p.name + '.md'
  return `You are a research analyst for the decomp-synth project.

PROJECT CONTEXT: ${PROJECT}

TOPIC: ${p.title}
SOURCES TO FIND & READ: ${p.targets}
ANGLE: ${p.angle}

Use WebSearch + WebFetch. Search by TITLE; do not trust any arXiv ID blindly. If a source cannot be found, say so explicitly rather than inventing content. Prefer primary sources (papers, official docs, author blogs).

Write a markdown report to \`${report}\` with, per source: a citation (title + URL you actually fetched), 2-3 line summary, "directly applicable to decomp-synth", "techniques to steal", and "caveats / does-not-transfer". End with a short "cross-cutting implications for our MVP" section.

Return the StructuredOutput.`
}

const PAPER_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  properties: {
    topic: { type: 'string' },
    sources_found: { type: 'array', items: { type: 'string' }, description: 'title + URL actually fetched' },
    sources_not_found: { type: 'array', items: { type: 'string' } },
    techniques_to_steal: { type: 'array', items: { type: 'string' } },
    mvp_implications: { type: 'array', items: { type: 'string' } },
    report_path: { type: 'string' },
  },
  required: ['topic', 'sources_found', 'techniques_to_steal', 'mvp_implications', 'report_path'],
}

// ---- run, capped at 6 concurrent to honour the stability preference ----
async function runLimited(items, makeThunk, limit) {
  const out = []
  for (let i = 0; i < items.length; i += limit) {
    const batch = items.slice(i, i + limit)
    const res = await parallel(batch.map((it, j) => () => makeThunk(it, i + j)))
    out.push(...res)
  }
  return out
}

phase('Study')
log(`Studying ${REPOS.length} reference repos (waves of 6)...`)
const studyResults = (await runLimited(
  REPOS,
  (r) => agent(studyPrompt(r), { label: 'study:' + r.name, phase: 'Study', schema: STUDY_SCHEMA }),
  6,
)).filter(Boolean)

phase('Papers')
log(`Researching ${PAPERS.length} paper/blog topics...`)
const paperResults = (await runLimited(
  PAPERS,
  (p) => agent(paperPrompt(p), { label: 'paper:' + p.name, phase: 'Papers', schema: PAPER_SCHEMA }),
  6,
)).filter(Boolean)

phase('Synthesize')
const studyJson = JSON.stringify(studyResults, null, 1)
const paperJson = JSON.stringify(paperResults, null, 1)
const synthPrompt = `You are the synthesis lead for the decomp-synth reverse-compilation reference review.

PROJECT CONTEXT: ${PROJECT}

You have ${studyResults.length} per-repo reusability reports and ${paperResults.length} paper notes. The full markdown reports are in \`${REFS}/_reports/*.md\` and \`${REFS}/papers/*.md\` -- READ the ones you need for detail. Also READ the existing plan docs so your synthesis aligns and CORRECTS them where reports contradict:
- /home/free/code/milohax/decomp-synth/docs/architecture/INVERSE_COMPILATION.md
- /home/free/code/milohax/decomp-synth/docs/plans/reverse-compilation/README.md
- /home/free/code/milohax/decomp-synth/docs/plans/reverse-compilation/ROADMAP.md

Structured study results (JSON):
${studyJson}

Structured paper results (JSON):
${paperJson}

DO TWO THINGS:

1) WRITE the reference index to \`${REFS}/README.md\`. It must contain:
   - A 1-paragraph purpose statement (what this dir is, that it is symlinks to local clones + shallow clones).
   - A table: Repo | Path | Lens it feeds | Reusable now? | Recommendation (link-now/vendor-or-port/study-deeper/reference-only/skip) | one-line why.
   - A short "Papers" subsection with the found citations.
   - A "Wiring map" subsection: for each lens (root-cause/provenance, SMT oracle, ML ranker, orbit/e-graph, compiler atlas, PPC facts, MSVC/PE/PDB facts, Xbox platform, diff engine, baseline permuter) name the 1-2 repos that feed it.

2) RETURN the StructuredOutput with cross-cutting findings I will use to write the MVP validation roadmap. Be opinionated and concrete:
   - link_now: tools to integrate immediately into the Rust core (with the specific API/crate).
   - vendor_or_port / reference_only / skip: same.
   - plan_corrections: places where the reports REFINE or CONTRADICT the existing plan docs (e.g. "Souffle is overkill at our scale, use Rust fixpoint until X"; "egg vs hand-rolled DAG"; objdiff-core/jeff/powerpc-rs dependency reality).
   - prioritized_experiments: an ORDERED list (cheapest validation / highest information-gain first) of MVP experiments that gather data to de-risk the build. Each: id, title, hypothesis, uses (which repos/lenses), cost (rough compiles/effort), data_yield (what we learn), validates (which framework claim / which architecture layer).
   - cross_cutting_themes.
   Cover the external-feedback experiment ideas too: (A) compiler atlas for ONE lever (e.g. FPR decl order) learning its decision boundary; (B) rule-based root-mismatch classifier; (C) e-graph compare/branch expression orbit; (D) gradient-boosted/logistic ranker from logged attempts; plus ablation-attribution mode, style embeddings, counterfactual-objdiff outcome classes, idiom cards, move-synthesis-from-solved-pairs.`

const SYNTH_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  properties: {
    link_now: { type: 'array', items: { type: 'string' } },
    vendor_or_port: { type: 'array', items: { type: 'string' } },
    reference_only: { type: 'array', items: { type: 'string' } },
    skip: { type: 'array', items: { type: 'string' } },
    plan_corrections: { type: 'array', items: { type: 'string' } },
    prioritized_experiments: {
      type: 'array',
      items: {
        type: 'object',
        additionalProperties: false,
        properties: {
          id: { type: 'string' },
          title: { type: 'string' },
          hypothesis: { type: 'string' },
          uses: { type: 'string' },
          cost: { type: 'string' },
          data_yield: { type: 'string' },
          validates: { type: 'string' },
        },
        required: ['id', 'title', 'hypothesis', 'uses', 'cost', 'data_yield', 'validates'],
      },
    },
    cross_cutting_themes: { type: 'array', items: { type: 'string' } },
    readme_path: { type: 'string' },
  },
  required: ['link_now', 'vendor_or_port', 'reference_only', 'skip', 'plan_corrections', 'prioritized_experiments', 'cross_cutting_themes', 'readme_path'],
}

const synth = await agent(synthPrompt, { label: 'synthesize', phase: 'Synthesize', schema: SYNTH_SCHEMA })

return { studyCount: studyResults.length, paperCount: paperResults.length, synth }
