# Pattern Reference Index

Quick reference for all documented decompilation patterns in DC3 (Dance Central 3), targeting Xbox 360 / MSVC (PowerPC).

> **Data source:** `decomp.db` — 50,981 functions (34,215 non-excluded). 92.8% COMPLETE, 7.2% AT_LIMIT, 0.04% remaining (14 stubs).
> **Last updated:** 2026-05-27

## Corrections — read before trusting an older section

- **2026-08-04 — the AT_LIMIT + `REGISTER_SWAP` bucket is 836 functions, and its verdict
  is wrong at least 30% of the time.** Blind stratified audit (selection rule fixed before
  any diff was looked at: 6 match bands, ordered within each by `md5(symbol)`, max one
  function per unit) scored **3/10**. The earlier "3 of 3 sampled functions had source
  fixes" figure was a *selected* sample — those functions were chosen because someone was
  already working on them — and was never a rate; do not re-cite it. Full detail and the
  fields you cannot trust are in the
  [AT_LIMIT Breakdown](#at_limit-breakdown) below and in
  [../../sessions/2026-08-04-regswap-atlimit-sweep.md](../../sessions/2026-08-04-regswap-atlimit-sweep.md).
  The routing rule that came out of it is
  [fixable-liveness.md: Triage Split](fixable-liveness.md#triage-split-statement-level-vs-within-one-expression).

- **2026-08-04 — base *declaration order* does not control base *offsets*.** MSVC hoists
  the polymorphic (vfptr-bearing) base to offset 0 whichever order the bases are written
  in, so an MI base-adjustment tell in the target proves a base's offset and says nothing
  about the declaration list. Measured with `cl.exe /d1reportAllClassLayout`; both orders
  of `class String` give a byte-identical dump. Affects
  [Inheritance offset shift](fixable-struct-layout.md#inheritance-offset-shift); the
  correction and the whole-build fingerprint of a construction-order-only edit are in
  [fixable-struct-layout.md: Base Declaration Order](fixable-struct-layout.md#base-declaration-order-does-not-set-base-offsets-msvc-hoists-the-polymorphic-base-to-0).

- **2026-08-03 — register swaps are symptoms, and declaration reorder is the wrong axis for them.**
  See **[fixable-liveness.md](fixable-liveness.md)**. Measured on 3 functions
  (`ObjectDir::Iterate` 99.4%→**100%**, `RndText::FitTextScroll` 92.7%→98.2%,
  `RndText::SizeCheck` 96.5%→99.1%): 12+ declaration-reorder variants produced
  **byte-identical** output and a 65-candidate permuter sweep found 0 improvements,
  yet every function had a one-to-three-line fix on the **liveness** or **scheduling**
  axis that flipped all its register swaps at once.
  **Scoping/packing changes move stack slots; liveness/scheduling changes move
  registers.** Affects
  [Variable Declaration Order](fixable-declarations.md#variable-declaration-order),
  [Register Allocation](unfixable-compiler.md#register-allocation), and the
  `REGISTER_SWAP` routing in [PERMUTER_ROI_ANALYSIS.md](PERMUTER_ROI_ANALYSIS.md).
  Also promotes a third floor-evidence condition: **Ghidra-decompile the target** and
  name the construct.

## DC3 ↔ RB3 doc filename divergence

The two decomps document the same shared Milo engine but **use different filenames for
the same documents**. Tools and prose that link across repos silently 404 when they
assume the other repo's name:

| Topic | DC3 (this repo) | RB3 (`../rb3/`) |
|-------|-----------------|-----------------|
| Permuter ROI / pattern automation | `PERMUTER_ROI_ANALYSIS.md` | `permuter-roi.md` |
| Systemic at-limit classes | `at-limit-systemic.md` | `at-limit-mwcc.md` |

This was not hypothetical: objdiff-cli's `REGISTER_SWAP` hint pointed at
`permuter-roi.md` / `at-limit-mwcc.md`, so **the hint most likely to mislead someone was
also linking nowhere in this repo**. The compilers differ too (DC3 = MSVC, RB3 =
MetroWerks), so an RB3 filename in a DC3 hint is usually a sign the *content* was
written for the other backend as well — check before trusting it.

**Fixed in objdiff 2026-08-04.** objdiff-cli no longer hardcodes one repo's filenames:
links are named by what they explain and resolved through a per-project table, with the
project detected from this repo's `docs/decomp/patterns/` directory (marker filename).
Nothing has to be passed on the command line. An unrecognised repo gets only links the
two projects agree on, rather than a guess. Consequences here: the ROI links now say
`PERMUTER_ROI_ANALYSIS.md`, the at-limit links resolve to `unfixable-compiler.md` /
`at-limit-systemic.md` instead of `at-limit-mwcc.md`, and `fixable-liveness.md` is
linked directly from `REGISTER_SWAP` and `OFFSET_SWAP`.

Re-check after renaming or restructuring any file in this directory:

```bash
python3 ../objdiff/scripts/check_doc_links.py --dc3 . --rb3 ../rb3
```

It resolves every URL objdiff would emit (`objdiff-cli doc-links -f json`) against both
working trees — file must exist, `#anchor` must match a real heading. 30/30 here,
25/25 in RB3 as of 2026-08-04.

When adding a cross-repo link by hand, verify the filename against the repo you're
linking into, not the one you're writing in.

### Anchor contract (tool-referenced — do not rename)

objdiff-cli renders `[docs]` links that resolve against **this** repo. These anchors are
load-bearing; restructure the prose around them freely, but keep the headings stable or
get objdiff re-pointed first:

| Anchor | Used for | Status |
|--------|----------|--------|
| [`fixable-declarations.md#pre-compute-references-before-clobbering-calls`](fixable-declarations.md#pre-compute-references-before-clobbering-calls) | `REGISTER_SWAP` **primary** (rendered link), `PROLOGUE_MISMATCH` | Stable — extended in place 2026-08-03, not moved |
| [`fixable-declarations.md#offset-swap`](fixable-declarations.md#offset-swap) | `OFFSET_SWAP` | Stable — deliberately stays in declarations; scoping/packing moves stack slots |
| [`PERMUTER_ROI_ANALYSIS.md#instruction-scheduling`](PERMUTER_ROI_ANALYSIS.md#instruction-scheduling) | `REGISTER_SWAP` secondary | Added 2026-08-03. Re-pointed 2026-08-04 — objdiff now emits this path for DC3 (was `permuter-roi.md#…`, an RB3 name) |
| [`fixable-liveness.md#diagnostic-order-for-a-register-swap-residual`](fixable-liveness.md#diagnostic-order-for-a-register-swap-residual) | `REGISTER_SWAP`, `PROLOGUE_MISMATCH` | Added 2026-08-04. Replaces the RB3-only `permuter-roi.md#register-allocation-cascades` |
| [`fixable-liveness.md#lever-4-scope-a-declaration-into-the-block-that-uses-it-stack-lever-not-a-register-lever`](fixable-liveness.md#lever-4-scope-a-declaration-into-the-block-that-uses-it-stack-lever-not-a-register-lever) | `OFFSET_SWAP` secondary | Added 2026-08-04. Replaces the RB3-only `permuter-roi.md#stack-slot-inversion` |

All three original anchors are asserted by unit test in objdiff
(`doc_link_tests::dc3_first_url_anchor_contract`) — objdiff renders only the **first**
URL of each pattern inline, so those are the ones that must not drift.

## Session catalogues

- [permuter-patterns-2026-05-27.md](permuter-patterns-2026-05-27.md) — 3 new patterns (reference_elimination_chain, loop_body_assign_hoist, signed_unsigned_cast_polarity), 15 proposal-stage patterns, tooling fixes (libclang+guards), validated wins on both DC3 and RB3.

## Behavioral divergence (metric-invisible bugs) — read first when a "matched" function misbehaves at runtime

The class objdiff % **cannot see**: wrong-behavior defects at neutral/near-neutral
match (some at 0% as unpaired stubs). Several were *introduced* by match-neutral
"wins" (permuter sweeps, ports, archaeology). See
**[behavioral-divergence.md](behavioral-divergence.md)**.

| Sub-class | Metric effect | DC3 case | Detection |
|-----------|---------------|----------|-----------|
| Non-commutative operand swap (`a-b`→`b-a`, `/`, `%`, `<<`) | neutral | `SetupCharacter` z1 (all text invisible) | `diff_arg` on non-commutative op ≠ commutative-swap floor |
| Non-commutative argument swap `f(a,b)`→`f(b,a)` | ~neutral | `FontMap3d` `XfmOnCircleEdge` | commit greps; `scan_behavioral_idioms.py` |
| Comparison direction / boundary `<=`↔`>=` | neutral | `SetPausedHelper` | branch-polarity vs body mismatch |
| Float reassociation `(k+1)*x` vs `k*x+x` | neutral | `Interp` DOF rounding | fadds/fmuls ↔ fmadds swap; split into temp |
| Spurious/missing guard, dropped block | small | `InsertMoveInSong`, `ForeachKeyframe` | insert/delete cluster w/ polarity row |
| Aliased self-clobber | 0% (stub) | `Transform::LookAt` | `foo.Method(…, foo.member)`; nondeterministic runtime |
| Reversed container args `erase(end,begin)` | small | `ResetDetectFrames` (**UB**) | `scan_behavioral_idioms.py` (exact) |
| Stale iterator across realloc | small | `OnComputeCharWidths` (**UB**) | iterator used after push_back |
| Wrong body of 0%-stub | invisible | `Transform::LookAt` | unpaired COMDAT; audit vs target asm |

Includes the **"regalloc floor" false-certification** anti-pattern + the floor-cert
standard, and the instrument→stub-test→audit→retry→runtime-verify workflow.

## Fixable Patterns

These patterns can often be fixed with source changes. Sorted by ROI (impact x success rate).

| Pattern | Impact | Success | File |
|---------|--------|---------|------|
| Struct Layout (Padded Arrays) | +5-40% | 100% | [fixable-struct-layout.md](fixable-struct-layout.md#padded-vector3-arrays-16-byte-stride) |
| Explicit Destructor | +37-70% | 100% | [fixable-declarations.md](fixable-declarations.md#explicit-destructor) |
| noreturn Attribute | +38% | 100% | [fixable-casting.md](fixable-casting.md#noreturn-attribute) |
| Float/Double Separation | +80% | 95% | [fixable-casting.md](fixable-casting.md#floatdouble-separation) |
| fsel Clamp/Min/Max Templates | +5-44% | HIGH | [fixable-fsel-fma.md](fixable-fsel-fma.md#fsel-via-clampminmax-templates) |
| __fsel Intrinsic | +1-10% | MEDIUM | [fixable-fsel-fma.md](fixable-fsel-fma.md#fsel-via-__fsel-intrinsic) |
| #pragma fp_contract (FMA) | +1-12% | HIGH | [fixable-fsel-fma.md](fixable-fsel-fma.md#fma-control-via-pragma-fp_contract) |
| FMA Expression Order | +1-75% | 98% | [fixable-operators.md](fixable-operators.md#fma-expression-order) |
| Signed/Unsigned Cast | +1-50% | 100% | [fixable-comparison.md](fixable-comparison.md#signedunsigned-cast) |
| MILO_NOTIFY vs MILO_NOTIFY_ONCE | +10-35% | HIGH | [fixable-declarations.md](fixable-declarations.md#milo_notify-vs-milo_notify_once) |
| alloca vs _alloca | +10-15% | 100% | [fixable-declarations.md](fixable-declarations.md#alloca-vs-_alloca-intrinsic-stack-allocation) |
| Pre-Compute References Before Calls | +5-18% | HIGH | [fixable-declarations.md](fixable-declarations.md#pre-compute-references-before-clobbering-calls) |
| Iterator Dereference Caching | +5-10% | LOW (~20%) | [fixable-declarations.md](fixable-declarations.md#iterator-dereference-caching) |
| Variable Extraction | +1-35% | 95% | [fixable-declarations.md](fixable-declarations.md#variable-extraction) |
| Explicit Conditional vs Max() | +35% | HIGH | [fixable-control-flow.md](fixable-control-flow.md#explicit-conditional-vs-max) |
| Explicit Float Cast | +35% | HIGH | [fixable-casting.md](fixable-casting.md#explicit-float-cast) |
| MakeString Template Type Mismatch | +5-21% | HIGH | [fixable-casting.md](fixable-casting.md#makestring-template-type-mismatch-milo-macro-arguments) |
| ObjPtr DeferOwner Constructor | +16% | HIGH | [fixable-declarations.md](fixable-declarations.md#objptr-constructor-deferred-owner-initialization) |
| Manual Handler Extraction | +3-5% | HIGH | [fixable-macros.md](fixable-macros.md#manual-handler-extraction) |
| Loop Condition Subtraction (subf.) | +2-3% | HIGH | [fixable-loop-condition.md](fixable-loop-condition.md#subtract-and-record-subf-loop-condition) |
| Unsigned Zero Comparison | +0.4-1.3% | 95% | [fixable-comparison.md](fixable-comparison.md#unsigned-zero-comparison) |
| Operator Overload Selection | +1-2% | 100% | [fixable-operators.md](fixable-operators.md#operator-overload-selection) |
| Inline Assignment | +1-2% | 95% | [fixable-operators.md](fixable-operators.md#inline-assignment) |
| Negation Splitting (fneg/frsp) | +3-4% | HIGH | [fixable-operators.md](fixable-operators.md#negation-splitting-fnegfrsp-scheduling) |
| Byte Mask Extraction (rlwimi) | +5-30% | HIGH | [fixable-operators.md](fixable-operators.md#byte-mask-extraction-rlwimi) |
| NOR Peephole Prevention (u32 Widening) | +1-14% | HIGH | [fixable-operators.md](fixable-operators.md#nor-peephole-prevention-u32-widening) |
| u8 Intermediate Scheduling | +0.7-1% | MEDIUM | [fixable-operators.md](fixable-operators.md#u8-intermediate-variables-for-shift-scheduling) |
| Early Return Destructor Path Separation | +10-16% | HIGH | [fixable-control-flow.md](fixable-control-flow.md#early-return-for-destructor-path-separation) |
| Ternary vs If-Else | +5-10% | 75% | [fixable-control-flow.md](fixable-control-flow.md#ternary-vs-if-else) |
| IsNaN vs Threshold Check | +3-5% | HIGH | [fixable-comparison.md](fixable-comparison.md#isnan-vs-threshold-check) |
| Iterator Index Comparison | +40-100% | HIGH | [fixable-comparison.md](fixable-comparison.md#iterator-index-comparison) |
| Split && into Nested If | +5-18% | HIGH | [fixable-control-flow.md](fixable-control-flow.md#split--into-nested-if) |
| Avoid Unnecessary dynamic_cast | +6% | HIGH | [fixable-casting.md](fixable-casting.md#avoid-unnecessary-dynamic_cast-getobj-vs-objt) |
| Function Definition Order ($S#) | +3-5% | 100% | [fixable-declarations.md](fixable-declarations.md#function-definition-order-tu-wide-static-guard-counters) |
| Hoist Loop Variable for sret | +6% | HIGH | [fixable-declarations.md](fixable-declarations.md#hoist-loop-variable-for-sret-register-matching) |
| Goto-Based Loop (Deferred Assignment) | +2-3% | MEDIUM | [fixable-control-flow.md](fixable-control-flow.md#goto-based-loop-for-deferred-assignment) |
| Live-Range Shortening (aggregate projection) | +0.6% (→100%) | n=1 | [fixable-liveness.md](fixable-liveness.md#lever-1--live-range-shortening-read-the-args-back-out-of-the-aggregate-you-just-built) |
| Call Through Cached Local (not member re-load) | +4% | n=1 | [fixable-liveness.md](fixable-liveness.md#lever-2--call-through-the-cached-local-dont-re-load-the-member-at-the-call-site) |
| Schedule-Then-Polarity (float compares) | +2.6% | n=1 | [fixable-liveness.md](fixable-liveness.md#lever-3--fix-the-schedule-first-then-the-comparison-polarity) |
| Scope Declaration Into Using Block (stack packing) | +1.5% | n=1 | [fixable-liveness.md](fixable-liveness.md#lever-4--scope-a-declaration-into-the-block-that-uses-it-stack-lever-not-a-register-lever) |
| Variable Declaration Order | +1-88% | 30% (**not for register swaps** — see [correction](#corrections--read-before-trusting-an-older-section)) | [fixable-declarations.md](fixable-declarations.md#variable-declaration-order) |
| Bodyless Copy Constructor | +100% (0→100) | 100% | [fixable-copy-ctor.md](fixable-copy-ctor.md) |
| Inline Constructor Location | +5-10% | 100% | [fixable-inline-boundary.md](fixable-inline-boundary.md#inline-constructor-in-header-vs-out-of-line-in-cpp) |
| Sort Comparator Inline Location | +30-50% | 100% | [fixable-inline-boundary.md](fixable-inline-boundary.md#sort-comparator-inline-location-stdsort--std__median) |
| Inline Boundary Cascade (ICF) | varies | requires diagnose | [fixable-inline-boundary.md](fixable-inline-boundary.md#inline-boundary-cascade-icf-merge-of-out-of-line-accessor) |
| Inline-Level Counting (parameter home area) | +0.5-1%, collapses whole clusters | n=1, signal unambiguous | [fixable-inline-boundary.md](fixable-inline-boundary.md#inline-level-counting-via-the-parameter-home-area) |
| Manual Helper Inlining | +2-12% | HIGH | [fixable-control-flow.md](fixable-control-flow.md#manual-helper-inlining-reverse-inline-a-trivial-helper) |
| Static Variable Type in MakeString Args | +1-2% | HIGH | [fixable-casting.md](fixable-casting.md#sub-pattern-static-variable-type-in-makestring-args) |
| Bool-Returning Call Coercion Defeats Shared Tail-Call | +2-3% | HIGH | [fixable-control-flow.md](fixable-control-flow.md#bool-returning-call-coercion-defeats-shared-tail-call) |
| Bool→Int Normalization via `b != 0` | +1-2% | HIGH | [fixable-casting.md](fixable-casting.md#boolint-normalization-via-b--0) |
| Local Pointer Reload to Break Member-Address Reuse | varies (regalloc cluster) | MEDIUM | [fixable-declarations.md](fixable-declarations.md#local-pointer-reload-to-break-member-address-reuse) |

### Additional Fixable Patterns

| Pattern | File |
|---------|------|
| sizeof() Signedness | [fixable-casting.md](fixable-casting.md#sizeof-signedness) |
| Loop Counter Signedness | [fixable-comparison.md](fixable-comparison.md#loop-counter-signedness) |
| String Iteration Signedness | [fixable-comparison.md](fixable-comparison.md#string-iteration-signedness) |
| empty() vs size() == 0 | [fixable-comparison.md](fixable-comparison.md#empty-vs-size) |
| Comparison Style | [fixable-comparison.md](fixable-comparison.md#comparison-style) |
| Initializer Literals | [fixable-declarations.md](fixable-declarations.md#initializer-literals) |
| Static Variable Scope | [fixable-declarations.md](fixable-declarations.md#static-variable-scope) |
| Braced vs Braceless If (Scope Counter) | [fixable-declarations.md](fixable-declarations.md#braced-vs-braceless-if-scope-counter) |
| Static Variable Naming | [fixable-declarations.md](fixable-declarations.md#static-variable-naming) |
| Static Symbol Order | [fixable-declarations.md](fixable-declarations.md#static-symbol-order) |
| Function Definition Order (TU-Wide $S#) | [fixable-declarations.md](fixable-declarations.md#function-definition-order-tu-wide-static-guard-counters) |
| Hoist Loop Variable for sret | [fixable-declarations.md](fixable-declarations.md#hoist-loop-variable-for-sret-register-matching) |
| Iterator Dereference Caching | [fixable-declarations.md](fixable-declarations.md#iterator-dereference-caching) |
| Boolean Init from Existing Register | [fixable-declarations.md](fixable-declarations.md#boolean-init-from-existing-register) |
| Offset Swap | [fixable-declarations.md](fixable-declarations.md#offset-swap) |
| sret Return Value Tracing | [fixable-declarations.md](fixable-declarations.md#sret-return-value-tracing) |
| Avoid Unnecessary dynamic_cast | [fixable-casting.md](fixable-casting.md#avoid-unnecessary-dynamic_cast-getobj-vs-objt) |
| Signed Pointer Comparison Cast | [fixable-casting.md](fixable-casting.md#signed-pointer-comparison-cast) |
| Loop Structure | [fixable-control-flow.md](fixable-control-flow.md#loop-structure) |
| Split && into Nested If | [fixable-control-flow.md](fixable-control-flow.md#split--into-nested-if) |
| Goto-Based Loop for Deferred Assignment | [fixable-control-flow.md](fixable-control-flow.md#goto-based-loop-for-deferred-assignment) |
| Sequential If vs If-Else | [fixable-control-flow.md](fixable-control-flow.md#sequential-if-vs-if-else) |
| Single Return for Branch Direction | [fixable-control-flow.md](fixable-control-flow.md#single-return-for-branch-direction) |
| Branch Polarity Steering | [fixable-control-flow.md](fixable-control-flow.md#branch-polarity-steering-beqbne-blebge) |
| Early Return Destructor Path Separation | [fixable-control-flow.md](fixable-control-flow.md#early-return-for-destructor-path-separation) |
| Boolean Index | [fixable-operators.md](fixable-operators.md#boolean-index) |
| Bitwise Alignment | [fixable-operators.md](fixable-operators.md#bitwise-alignment) |
| Commutative Operand Order | [fixable-operators.md](fixable-operators.md#commutative-operand-order) |
| Comparison Operand Order | [fixable-operators.md](fixable-operators.md#comparison-operand-order) |
| Bool Mask (`clrlwi`) | [fixable-bool-mask.md](fixable-bool-mask.md#step-1-detect) |
| extrwi vs rlwinm Encoding | [fixable-bool-mask.md](fixable-bool-mask.md#extrwi-vs-rlwinm-bit-test-encoding) |
| MILO_NOTIFY vs MILO_NOTIFY_ONCE | [fixable-declarations.md](fixable-declarations.md#milo_notify-vs-milo_notify_once) |
| MakeString Template Type Mismatch | [fixable-casting.md](fixable-casting.md#makestring-template-type-mismatch-milo-macro-arguments) |
| Manual Handler Extraction | [fixable-macros.md](fixable-macros.md#manual-handler-extraction) |
| Static Symbol Order in Handlers | [fixable-macros.md](fixable-macros.md#static-symbol-declaration-order-in-handlers) |
| ObjPtr DeferOwner Constructor | [fixable-declarations.md](fixable-declarations.md#objptr-constructor-deferred-owner-initialization) |
| IsNaN vs Threshold Check | [fixable-comparison.md](fixable-comparison.md#isnan-vs-threshold-check) |
| Iterator Index Comparison | [fixable-comparison.md](fixable-comparison.md#iterator-index-comparison) |

---

## Hard Patterns

These patterns resist simple source-level fixes. Each documents what would be needed (c2.dll patch, volatile intermediates, etc.). Verify the pattern truly applies before moving on.

| Pattern | Prevalence | Typical Gap | File |
|---------|------------|-------------|------|
| Linker Merged (ICF) | ~350 functions | 0.5-3% | [verifiable-icf.md](verifiable-icf.md#linker-merged-icf) (verify first) |
| ~~MakeString Array-Size ICF~~ | ~~2,550+ functions~~ | ~~1-5%~~ | [verifiable-icf.md](verifiable-icf.md#makestring-array-size-icf-resolved) — **Resolved** in objdiff (2026-03-03) |
| LTCG/Global Pooling | varies | 0.5-1% | [verifiable-icf.md](verifiable-icf.md#ltcg-global-pooling) |
| Float Constant Pooling | common | 1-2 instr | [verifiable-icf.md](verifiable-icf.md#float-constant-pooling) |
| Register Allocation | 836 flagged (≥30% mislabeled) | 1-3% | [unfixable-compiler.md](unfixable-compiler.md#register-allocation) (mechanism understood) — **work [fixable-liveness.md](fixable-liveness.md) first**; the swap is *always* a symptom, and the flag itself is unreliable ([breakdown](#at_limit-breakdown)) |
| Dead Store Elimination / Destructor Merging | RAII wrappers | 1-2% | [unfixable-compiler.md](unfixable-compiler.md#dead-store-elimination--destructor-merging) |
| Anonymous Namespace Hash | common | 0.5-3% | [unfixable-compiler.md](unfixable-compiler.md#anonymous-namespace-hash-mismatch) |
| Build Env Regression (Headers) | rare | 5-10% | [unfixable-compiler.md](unfixable-compiler.md#build-environment-regression-from-unrelated-headers) |
| ASSERT_REVS Scheduling | ~10% | ~0.8-0.9% | [unfixable-compiler.md](unfixable-compiler.md#assert_revs-scheduling) |
| fmadds vs Separate Ops (mixed) | float math | 1-3% | [unfixable-compiler.md](unfixable-compiler.md#fmadds-vs-separate-ops) — try [fixable-fsel-fma.md](fixable-fsel-fma.md) first |
| fsel Register Pressure | float clamp | 5-20% | [unfixable-compiler.md](unfixable-compiler.md#fsel-register-pressure) — needs c2.dll patch |
| Boolean Negation (subfic/subic) | ptr→bool sites | 3-8% | [unfixable-compiler.md](unfixable-compiler.md#boolean-negation-subfic-vs-subic) |
| Commutative Register Swap | float ops | <1% | [unfixable-compiler.md](unfixable-compiler.md#commutative-register-swap) |
| 64-bit Extraction | rare | ~5% | [unfixable-compiler.md](unfixable-compiler.md#64-bit-extraction) |
| Stack Spill Scheduling | high register pressure | ~1-2% | [unfixable-compiler.md](unfixable-compiler.md#stack-spill-scheduling) |
| Store-then-Reload Scheduling | global store sites | 0.5-1% | [unfixable-compiler.md](unfixable-compiler.md#store-then-reload-scheduling) |
| Address Relocation Noise | ~150 AT_LIMIT functions | 0.5-2% | [unfixable-compiler.md](unfixable-compiler.md#address-relocation-noise) |
| Static Guard Naming (`??_B` vs `$S`) | TUs with few-static funcs | 1-3% | [unfixable-compiler.md](unfixable-compiler.md#static-guard-naming-convention-_b-vs-s) |
| 16-byte Member Self-Copy Regalloc | Color/Vector4 sites | 0.2-0.5% | [unfixable-compiler.md](unfixable-compiler.md#self-copy-of-16-byte-member-regalloc-coin-flip) |
| BEGIN_HANDLERS Static-Init Guard Elision | Handle() w/ many `_NEW_STATIC_SYMBOL` | ~0.05-0.1% | [unfixable-compiler.md](unfixable-compiler.md#begin_handlers-static-init-guard-elision) |

---

## Harmful Patterns

These patterns make matches **worse**. Avoid them.

| Pattern | Effect | File |
|---------|--------|------|
| Member Aliasing | -6% | [harmful-avoid.md](harmful-avoid.md#member-aliasing) |
| Child Pointer in Loop | -6.5% | [harmful-avoid.md](harmful-avoid.md#child-pointer-in-loop) |
| Iterator Address-Of (`&*iter`) | -3-5% | [harmful-avoid.md](harmful-avoid.md#iterator-address-of-iter) |
| End Iterator Explicit | -0.5% | [harmful-avoid.md](harmful-avoid.md#end-iterator-explicit) |
| Constructor Zero-Init That Doesn’t Exist in Target | -2% to -6% | [harmful-avoid.md](harmful-avoid.md#constructor-zero-init-that-doesnt-exist-in-target) |

---

## Quick Decision Tree

> **Note (2026-03-03):** 34,201 of 34,215 non-excluded functions are done (92.8% COMPLETE, 7.2% AT_LIMIT). Only 14 remain as unimplemented stubs. This decision tree remains useful for future work if new functions are added or compiler tooling changes.

```
Match% < 50%?
  → Likely missing implementation. Check RB3 reference, Ghidra decompilation.

Match% 50-80%?
  → Structural issues. Try control flow, variable declarations.

Match% 80-95%?
  → Fine-tuning. Check comparison patterns, casting, operator selection.
  → Prologue mismatch with _RtlCheckStack12? Try _alloca instead of alloca.
  → High diff_arg with low diff_op? Likely address relocation noise — may be AT_LIMIT.

Match% 95-99%?
  → Check for hard patterns first (register swap, merged symbols, address noise).
  → Register swaps? Do NOT start with variable reorder — see fixable-liveness.md.
    Check __savegprlr_NN delta (live set), swap-cycle length (live range),
    producer instruction index (schedule) first.
  → If no hard patterns: try variable reorder, inline assignment.
  → run_diff_inspect mode=diagnose to separate fixable from unfixable diffs.

Match% 99%+ but not 100%?
  → Often hard patterns (linker-merged, register allocation), but verify first.
  → Use run_recon to check for LINKER_MERGED calls.
  → A pure register rotation with identical instruction counts/sizes is a liveness
    signal, not a floor — ObjectDir::Iterate went 99.4% → 100% from one line after
    8 declaration variants and a 65-candidate sweep came back empty.
  → Only mark "at limit" after verification; otherwise keep investigating.
```

### Prologue Hints

When the prologue (function entry) differs significantly:
- **`_RtlCheckStack12` in target:** Use `_alloca` (intrinsic) instead of `alloca` (CRT wrapper)
- **Stack frame size differs:** Check for missing/extra local variables
- **Different save/restore pattern:** May indicate compiler optimization requiring c2.dll patch

**Tip:** When running `objdiff-cli diff --verdict`, the output now shows:
- 💡 Match guidance hints based on percentage
- 📖 Links to pattern documentation for each detected pattern
- Analysis summary showing patterns checked and unattributed mismatches
- Verdict factors table explaining the classification

---

## Finding Targets in decomp.db

Query the database to find functions matching specific patterns or criteria:

```sql
-- Functions that CAN reach 100% (no unfixable patterns)
SELECT symbol, current_percent, unit
FROM functions
WHERE reachable_100 = 1
  AND current_percent < 100
  AND excluded = 0
ORDER BY current_percent DESC
LIMIT 20;

-- High-impact functions (many callers, worth fixing first)
SELECT symbol, fan_in, current_percent
FROM functions
WHERE fan_in >= 5
  AND current_percent < 100
  AND excluded = 0
ORDER BY fan_in DESC
LIMIT 20;

-- Fresh targets (never attempted, high match)
SELECT symbol, current_percent, unit
FROM functions
WHERE attempt_count = 0
  AND current_percent >= 90
  AND excluded = 0
ORDER BY current_percent DESC
LIMIT 20;

-- Type anchors (constructors/destructors for class validation)
SELECT symbol, current_percent
FROM functions
WHERE (is_constructor = 1 OR is_destructor = 1)
  AND current_percent < 100
  AND excluded = 0
ORDER BY current_percent DESC
LIMIT 20;
```

See [DATABASE_SCHEMA.md](../../reference/DATABASE_SCHEMA.md) for full schema documentation.

---

## Statistics (2026-03-03)

From `decomp.db` — 50,981 total functions (16,766 excluded SDK/library):

| Metric | Value |
|--------|-------|
| Non-excluded functions | 34,215 |
| COMPLETE (100% match) | 31,740 (92.8%) |
| AT_LIMIT (unfixable) | 2,461 (7.2%) |
| Remaining workable | 14 (0.04%) |

**Report-based metrics** (from `report.json`, uses objdiff fuzzy match):

| Metric | Value |
|--------|-------|
| Fuzzy match | **54.06%** |
| Complete units | 969 / 2,224 |
| Matched functions | 29,637 / 48,234 (61.4%) |

### AT_LIMIT Breakdown

The 894 AT_LIMIT functions break down by dominant blocking pattern:

| Pattern | Est. Count | Typical Gap | Notes |
|---------|-----------|-------------|-------|
| LINKER_MERGED (ICF) | ~350 | 0.5-3% | Merged `bl` targets from Identical COMDAT Folding |
| REGISTER_SWAP | **836** (measured 2026-08-04, not ~250) | 1-3% | **Verdict wrong ≥30% of the time** — blind stratified audit scored 3/10. See the note under this table. |
| Address relocation noise | ~150 | 0.5-2% | `lis`/`addi` pairs at different global addresses |
| ASSERT_REVS / INIT_REVS scheduling | ~80 | 0.8-0.9% | Instruction scheduling around rev macros |
| BOOL_MASK (insert direction) | ~30 | 1-2% | Compiler `clrlwi` insertion we can't remove |
| Store-then-reload scheduling | ~20 | 0.5-1% | Target stores to global then reloads; our compiler keeps register |
| Mixed / multiple patterns | ~80 | varies | Combination of above patterns |

> The "Est. Count" column is a 2026-03 estimate and has not been re-derived except for
> the `REGISTER_SWAP` row. Treat the others as order-of-magnitude only.

#### `decomp.db` is not trustworthy for this bucket (measured 2026-08-04)

The population is `verdict='AT_LIMIT' AND has_register_swap=1 AND is_stub=0 AND
excluded=0` — **836 functions** at audit time (834 today; it drifts as wins land). Every
item below is measured on a blind stratified sample, not speculated:

| Field | Finding |
|---|---|
| `verdict='AT_LIMIT'` | Wrong **≥30%** of the time here. 3 of 10 blind-sampled functions had real, byte-exact source fixes. |
| `tier=` | **No discriminative power, and mildly ANTI-correlated.** 9/10 sampled were `tier=A_HAND_FIXABLE` and 8 of those 9 failed; the single `tier=B_PERMUTER` was a win. `share=` is likewise unrelated to outcome. |
| `current_percent` | **Stale by up to 12 points** (DB 94.94 vs measured 86.1; DB 53.47 vs measured 41.1). Do not band on it — re-measure with `mcp__orchestrator__run_objdiff` passing `project_dir`. |
| `has_prologue_mismatch` | **Identically 0 for every row** — the detector never populated it. The "PROLOGUE_MISMATCH is a positive indicator" heuristic cannot be applied from the DB at all. |
| `has_register_swap=1` | Can be **pure noise**. One sampled function was 88 inserts / 88 deletes — an incomplete reconstruction, not a swap case. |

Budget a follow-up sweep at **~1 win per 3 functions**, scoped to the statement-level half
of the bucket ([Triage Split](fixable-liveness.md#triage-split-statement-level-vs-within-one-expression)).
One of the three blind wins, `HDCache::WriteDone`, was a **live bug** — see
[behavioral-divergence.md](behavioral-divergence.md).

### Fine-Tuning Success Rates (90%+ to 100%)

From 143 successful fine-tuning attempts (90%+ start, 100% end):

| Pattern | Wins | Share |
|---------|------|-------|
| Variable extraction | 60 | 42% |
| Unsigned/signed comparison | 43 | 30% |
| Inline assignment | 32 | 22% |
| Operator overload | 20 | 14% |
| Float expression | 14 | 10% |
| Declaration reorder | 13 | 9% |
| Ternary / if-else | 12 | 8% |
| FMA expression order | 6 | 4% |
| Destructor | 3 | 2% |

> Totals exceed 100% because some fixes combine multiple patterns.

---

## See Also

- [fixable-struct-layout.md](fixable-struct-layout.md) — Padded arrays, struct offset verification, stride mismatches; **base declaration order does not set base offsets** (MSVC hoists the polymorphic base to 0)
- [fixable-comparison.md](fixable-comparison.md) — Signed/unsigned, empty vs size, zero-check
- [fixable-casting.md](fixable-casting.md) — Float cast, noreturn, float/double, sizeof
- [fixable-control-flow.md](fixable-control-flow.md) — Max/Min explicit, ternary vs if/else, loop structure
- [fixable-declarations.md](fixable-declarations.md) — Variable extraction, declaration order, destructor
- [fixable-liveness.md](fixable-liveness.md) — **Register-swap levers**: live-range shortening, call-through-the-cached-local, schedule-then-polarity, scope-into-using-block; negative-result table; strengthened floor-evidence standard
- [fixable-fsel-fma.md](fixable-fsel-fma.md) — fsel intrinsic, Clamp templates, #pragma fp_contract
- [fixable-operators.md](fixable-operators.md) — FMA order, operator overload, inline assignment
- [fixable-bool-mask.md](fixable-bool-mask.md) — Bool mask (`clrlwi`) fixes
- [fixable-inline-boundary.md](fixable-inline-boundary.md) — Inline ctor location, sort comparator inlining, ICF cascade fix
- [unfixable-compiler.md](unfixable-compiler.md) — Hard patterns: register swap, ASSERT_REVS, fmadds, fsel pressure, guard naming
- [TECHNICAL_NOTES.md: Offset Diagnosis](../TECHNICAL_NOTES.md#offset-mismatch-diagnosis-off--n) — How to diagnose `[off:-N]` mismatches (class vs stack)
- [TECHNICAL_NOTES.md: MSVC Encoding](../TECHNICAL_NOTES.md#msvc-mangled-number-encoding) — Decode MakeString template sizes
- [verifiable-icf.md](verifiable-icf.md) — ICF, LTCG, float constant pooling
- [harmful-avoid.md](harmful-avoid.md) — Member aliasing, child pointer in loop
- [behavioral-divergence.md](behavioral-divergence.md) — **Metric-invisible bugs**: non-commutative swaps, float reassociation, dropped guards, aliased self-clobber, reversed container args, wrong 0%-stub bodies; the "regalloc floor" false-cert anti-pattern
- [PERMUTER_ROI_ANALYSIS.md](PERMUTER_ROI_ANALYSIS.md) — Pattern automation ROI rankings, permuter coverage gaps
