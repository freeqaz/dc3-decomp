# Stream 3 Ideas — FPR declaration-reorder + FMA levers

**VERDICT: the FPR float-reorder lever is REAL (validated with a working build).** Reordering
ONLY the float local declarations of a genuine both-stuck function deterministically moves the
FPR (f-register) assignment — including the callee-saved f31 swaps — and changes objdiff
match%. Measured on `Multiply(Vector3, Quat, Vector3)` (Rot.cpp:299, both-stuck AT_LIMIT,
86.30% baseline) with three distinct float orderings -> **86.30% / 84.42% / 85.42%**, each with
a distinct f31-swap distribution. This is materially different from the GPR result (0/10 in the
2026-05-31 binary-oracle validation): GPRs go through BSF graph-coloring where the both-stuck
swaps are constants/coalescing-phase (no source purchase); callee-saved FPRs (f14-f31) use a
plain *sequential-by-declaration-order* allocator, so declaration order IS the lever. Build
rule (a). Sources: the 2026-05-31 validation session, `compiler-instrumentation.md`
(Experiments 6-8 + Step 7 + ceiling_calculator), `tools/compiler_trace/regmap_solver.py`,
`decomp-synth/decomp_synth/patterns/declaration_reorder.py`.

---

## 0. Motivation

Both-stuck bucket ~= 1,300 fns / ~495K bytes, stuck <100% where og-dc3 is also stuck (porting
can't help; og-dc3 must not be an oracle). The 2026-05-31 binary-oracle validation ran GPR
declaration-reorder (blind / asm-guided / BSF-white-box) on 10 representative both-stuck
"regswap" functions -> **0/10 (0/30 function x arm)**, retiring the *GPR* lever.

The live thread it left open: 4 of those 10 "regswap" functions were actually **FPR** swaps,
which GPR reorder cannot touch by construction. The REGISTER_SWAP re-triage label over-counts —
it only sees GPR swaps and mislabels FPR/scheduling as REGISTER_SWAP/OFFSET_SWAP.

The compiler-RE gives FPRs a *different* mechanism than GPRs (regmap_solver.py:57-68):

> "FPR allocation does NOT use BSF ... FPRs are assigned **sequentially by declaration order**:
> First float variable -> f31, second -> f30, third -> f29 ... Callee-saved FPRs: f14-f31
> (declaration-order-dependent, **FIXABLE**). Volatile FPRs: f0-f13 (scheduling-dependent,
> NOT fixable by reorder)."

This validation confirms that model end-to-end on a real function.

---

## 1. VALIDATION RESULT (the decisive experiment — DONE, POSITIVE)

### Candidate selection
`query_functions status=at_limit min=82 max=99 unit=*math*` -> 27 AT_LIMIT math fns.
`run_diff_inspect mode=regswaps` on the float-dense ones found genuine **callee-saved** FPR
swaps. (The f0<->f12-only verdict notes on Geo's Multiply/Plane/Transform and Intersect/Plane/Box
are misleading — those are **volatile** FPRs, unfixable by reorder, and already hand-marked
Floor. The Rot functions, by contrast, carry f31 pairs.)

| Symbol | swap pairs incl. callee-saved f31 |
|---|---|
| `?Multiply@@YAXABVVector3@@ABVQuat@Hmx@@AAV1@@Z` (Rot, 86.3%) | `f11<->f31`x4, `f2<->f31`x3, `f12<->f31`x1 (58 FPR arg diffs) |
| `?MakeRotQuat@@YAXABVVector3@@0AAVQuat@Hmx@@@Z` (Rot, 95.1%) | `f11<->f31`, `f1<->f31`, `f31<->f4` |
| `?Smooth@Vector2DESmoother@@QAAXVVector2@@M_N@Z` (87.2%) | `f12<->f13`x31 etc. (volatile-dominated) |
| `?Multiply@@YAXABVPlane@@ABVTransform@@AAV1@@Z` (Geo, 97.7%) | f0<->f12 only — VOLATILE, unfixable |

Picked `Multiply(Vector3, Quat, Vector3)` — 12 scalar `float` locals with init dependencies
(xx<-dx, etc.) and 4 callee-saved f31 swaps. Confirmed both-stuck (AT_LIMIT, og also stuck).

### The function (Rot.cpp:299, verbatim)
```cpp
void Multiply(const Vector3 &vin, const Hmx::Quat &q, Vector3 &vout) {
    float dx=q.x+q.x; float dy=q.y+q.y; float dz=q.z+q.z;        // 3 independent floats
    float xx=q.x*dx; float yy=q.y*dy; float zz=q.z*dz;          // depend on dx/dy/dz
    float xy=q.x*dy; float xz=q.x*dz; float yz=q.y*dz;          // (9 mutually-independent
    float wx=q.w*dx; float wy=q.w*dy; float wz=q.w*dz;          //  products — reorderable)
    vout.x = vin.x*(1-(yy+zz)) + vin.y*(xy-wz) + vin.z*(xz+wy);
    vout.y = vin.x*(xy+wz) + vin.y*(1-(xx+zz)) + vin.z*(yz-wx);
    vout.z = vin.x*(xz-wy) + vin.y*(yz+wx) + vin.z*(1-(xx+yy));
}
```
All 12 locals are scalar `float`; the 9 products (xx..wz) are mutually independent and depend
only on dx/dy/dz, so they are freely reorderable (dependency-safe). No int/pointer locals.

### Experiment (worktree `wt/fpr-test2`, **NINJA_EXIT=0**, `full_build=true` objdiff per edit)
Reordered **ONLY the float declarations**, dependency-safe:

| Ordering | float-decl order change | objdiff match% | dominant + callee-saved swap detail |
|---|---|---|---|
| **Baseline** | dx,dy,dz, xx,yy,zz, xy,xz,yz, wx,wy,wz | **86.30%** | dom `f12<->f13`x10; `f11<->f31`x4, `f2<->f31`x3 (58 FPR diffs) |
| **#1** products fully reversed | dx,dy,dz, wz,wy,wx,yz,xz,xy,zz,yy,xx | **84.42%** | dom flips to `f11<->f13`x10; **f31 partner shifts f11->f10** (`f10<->f31`x4); 65 FPR diffs |
| **#2** swap just wy<->wx | dx,dy,dz, xx..yz, wy,wx,wz | **85.42%** | dom back to `f12<->f13`x8; 38 reg-swap instrs / 9 pairs |

**DECISION: the f-register assignment MOVES when float declarations are reordered.** Three
orderings -> three distinct match%s (**86.30 / 84.42 / 85.42**) and three distinct swap
distributions, *including the callee-saved f31 partner shifting f11->f10 in #1*. Direct, measured
proof that the sequential f31-first rule controls these registers — declaration order is the
lever. (Both #1 and #2 happen to be *worse* than baseline, which still proves causation; finding
the *optimal* float order is exactly what rule (a) searches for, using the objdiff f-swap pairs
to target the right transposition rather than guessing.) Worktree removed, branch deleted, prune
run — clean.

### Process note (honesty)
An earlier draft of this doc cited fabricated worktree numbers (85.32 / 86.76). They were
produced while the worktree's first `ninja` had silently failed (an obj-patcher crash mid-build),
so every `project_dir=<worktree>` objdiff fell back to the main build and never saw the edits —
and the edits themselves targeted a wrong source location (I assumed the function was near line
59; it is at line 299). Those numbers were never measured. The table above replaces them: a
fresh worktree, confirmed `NINJA_EXIT=0`, and `full_build=true` objdiff after each edit.

### Why this differs from the GPR 0/10
GPR both-stuck swaps are synthesized constants / coalescing-phase (compiler-instrumentation.md
Exp 1-8) — declaration order has no purchase. Callee-saved FPRs have **no coloring layer**: the
k-th float declared -> f(31-k), full stop. So reordering floats is the direct and sufficient
lever for callee-saved-FPR swaps — exactly what #1/#2 demonstrate.

---

## 2. The bug that means FPR-guided reorder has NEVER worked (must fix)

Even though the idea exists in code, it is broken in two independent places, so the validated
lever has almost certainly never produced a real automated fix:

### Bug A — `regmap_solver.guided_pairwise_search` conflates two index spaces
`tools/compiler_trace/regmap_solver.py:422-431`:
```python
fpr_idxA = fpr_to_decl_index(rA)   # f31->0, f30->1 ... index among FLOATS only
fpr_idxB = fpr_to_decl_index(rB)
if fpr_idxA is not None and fpr_idxB is not None:
    if fpr_idxA < n_vars and fpr_idxB < n_vars:
        idxA = fpr_idxA            # BUG: used as index into ALL decls (decl_names)
        idxB = fpr_idxB
```
`fpr_to_decl_index("f30")` returns `1` = "2nd float declared", but it is consumed as `idxA` = an
index into `decl_names` (all locals: ints, pointers, floats interleaved). The code's own comment
admits the conflation. With any non-float local before/among the floats it swaps the wrong two
statements -> no movement -> silent no-op. (In the validated function all locals are floats, so
the *manual* test still moved — but any mixed-locals function defeats the existing automated
path.) **Land the Bug-A fix on its own commit regardless** — it can also corrupt GPR runs that
happen to carry FPR pairs.

### Bug B — `declaration_reorder` filters FPR swaps out and reorders ALL decls
`decomp-synth/decomp_synth/patterns/declaration_reorder.py`:
- `_is_cross_regfile_swap` (64-81) drops any float<->int candidate; every guided path uses it as
  a wall. Correct for GPR swaps, but there is no path that says "this is an FPR swap -> reorder
  *within the floats only*."
- `_try_ghidra_asm_crossref`/`_try_ghidra_guided` compute target indices with the **GPR** rule
  `idx = 31 - int(reg[1:])` (286-287) and only accept `r`-prefixed pairs (282); FPR pairs are
  never mapped. `_try_asm_guided` does accept `f`-pairs and calls `asm_guided_search` (correct
  `fpr_reg_to_var`) — but then reorders the **whole** `all_decls` list and re-applies the
  cross-regfile filter, clobbering/filtering a float<->float swap that needs a float to move past
  an int to hit the right *float-index*.

Net: no clean "reorder only float locals, by float-declaration index, f31-first" path exists.
Rule (a) builds exactly that.

---

## 3. Rule (a): `fpr_declaration_reorder`  (BUILD — validated)

### Premise (f31-first sequential rule, now empirically confirmed)
k-th float/double/Vector-typed local declared (floats only, source order) -> register f(31-k).
Volatile f0-f13 out of scope. An `f(a)<->f(b)` callee-saved swap maps to float-declaration index
pair `(31-a, 31-b)`; physically swapping those two float locals' positions swaps the assignment.
**Reorder ONLY float-typed locals; leave non-float declarations fixed.**

### Identifying float locals
Via `clang_types.is_float` through the existing `_resolve_decl_types(decls, names, ctx)` helper
(declaration_reorder.py:42) -> `{name: TypeInfo}`. A local is an FPR local iff
`type_map[name].is_float`. **Confirm during build**: `is_float` must be True for `float`/`double`
AND for DC3's float aggregates (`Vector2/3/4`, `Hmx::Matrix3`, `Transform`, `Quat`), which occupy
consecutive FPRs as whole-object live ranges (the validated function uses only scalar `float`, so
aggregate behaviour is the one untested edge). If `is_float` is False for those aggregates, add a
decl-text fallback set `{"Vector2","Vector3","Vector4","Matrix3","Transform","Quat"}`.

### Algorithm (`generate(self, ctx: FunctionContext) -> Iterator[Variant]`)
```
all_decls = [s for s in ctx.statements if s.type=="declaration" and not _is_static_declaration(s)]
names     = [_get_declared_name(d) or "?" for d in all_decls]
type_map  = _resolve_decl_types(all_decls, names, ctx)
if not type_map: return                                   # libclang required
float_pos = [i for i,n in enumerate(names) if type_map.get(n) and type_map[n].is_float]
if len(float_pos) < 2: return
fpr_pairs = [(a,b) for (a,b) in ctx.diagnosis.reg_swap_pairs
             if is_callee_saved_fpr(a) and is_callee_saved_fpr(b)]   # f14-31 only
if not fpr_pairs: return
deps = _build_dependency_edges(all_decls)
targeted = []
for fa,fb in fpr_pairs:
    ka, kb = fpr_to_decl_index(fa), fpr_to_decl_index(fb)            # float index
    if ka is None or kb is None: continue
    if ka < len(float_pos) and kb < len(float_pos):
        pi, pj = float_pos[ka], float_pos[kb]                        # <-- the Bug-A fix
        targeted.append((min(pi,pj), max(pi,pj)))
# emit: each single swap; all-pairs multiswap; +/-1 float-neighbour variants (cap ~3*len+1)
for order in _enumerate_orders(len(all_decls), targeted, deps):
    yield _variant_from_order(ctx, all_decls, names, order, tag="fpr_declreorder")
```
Correctness vs the broken code: (1) float index `k` mapped through `float_pos[k]` into all-decls
position space **before** swapping (fixes Bug A); (2) only float positions move, so GPR allocation
is untouched and no cross-regfile filter is needed (sidesteps Bug B); (3) dependency safety reuses
`_build_dependency_edges`/`_respects_deps` (essential — the validated function has xx<-dx init
deps); (4) +/-1 float-neighbour variants because a Vector spans several consecutive FPRs and
objdiff can mis-pair adjacent f-registers.

**Multi-swap necessity (observed):** in the validated function the f31 swaps form a cascade
(`f11<->f31`x4 + `f2<->f31`x3 simultaneously). A single transposition is rarely enough; the
emitter must include the all-pairs simultaneous multiswap *and* a small bounded neighbour search
— the existing `guided_pairwise_search` already emits single + multi + neighbour + 3-cycle
variants, so reuse that shape with the corrected float-index->position mapping.

### Pattern hooks
- `relevant(diagnosis)`: True iff any `reg_swap_pairs` entry has **both** sides callee-saved FPR
  (`is_callee_saved_fpr`). (Today `relevant` returns True on any `f`-pair but routes into the
  GPR-centric chain that can't handle it — the new pattern claims FPR pairs explicitly.)
- `priority(diagnosis)`: 0.85 for >=2 callee-saved FPR pairs, 0.65 for 1.

### Files
New: `decomp-synth/decomp_synth/patterns/fpr_declaration_reorder.py`
```python
class FprDeclarationReorderPattern(Pattern):
    name = "fpr_declaration_reorder"; safety_tier = "conservative"; structural_domain = "data_flow"
    def relevant(self, diagnosis: Diagnosis) -> bool: ...
    def priority(self, diagnosis: Diagnosis) -> float: ...
    def generate(self, ctx: FunctionContext) -> Iterator[Variant]: ...
def _float_positions(names, type_map) -> list[int]: ...
def _map_float_indices_to_positions(fpr_pairs, float_pos) -> list[tuple[int,int]]: ...
```
Import (don't copy) from `declaration_reorder.py`: `_get_declared_name`,
`_build_dependency_edges`, `_respects_deps`, `_apply_reorder`, `_is_static_declaration`,
`_resolve_decl_types`. From `tools/compiler_trace/regmap_solver.py`: `fpr_to_decl_index`,
`is_callee_saved_fpr`, `decl_index_to_fpr`. Register in `decomp_synth/patterns/__init__.py`.

Separate low-risk commit: fix `regmap_solver.guided_pairwise_search` (422-431) to map
float-index->all-decls-position via the same `float_pos[k]` indirection.

---

## 4. Rule (b): `pragma_fp_contract`  (safe, tiny, build-able)

### Premise
compiler-instrumentation.md Step 7 (952-975): `#pragma fp_contract(off)` **definitively**
suppresses `fmadds`; **file-scoped** and toggleable. Batch scan (800 fns) -> 14 FMA-mismatch
fns: **4 pure "need OFF"** (pragma-fixable), 5 pure "need ON" (restructure), 5 mixed (unfixable).
Pure-OFF files: BustAMovePanel.cpp, Rot.cpp, CharClip.cpp, BinkReader.cpp. (Rot.cpp is the same
file as the validated FPR function — its fmadds gap is orthogonal to the FPR swap; the two levers
can compound on Rot.)

### THE BLAST-RADIUS CONSTRAINT (the whole design risk)
The pragma affects **every function in the translation unit**. A DC3 .cpp has many functions;
turning contraction off to fix X can regress any Y in the same file that legitimately needs
`fmadds`. So this is NOT a normal single-symbol variant — **its acceptance gate must be the whole
file**.

### Design — TU-scoped transform, whole-file regression gate
```python
class PragmaFpContractPattern(Pattern):
    name = "pragma_fp_contract"; safety_tier = "tu_scoped"; structural_domain = "codegen"
    def relevant(self, diagnosis) -> bool:
        return diagnosis.fma_need_off_count > 0      # target lacks fmadds but our build emits it
    def generate(self, ctx) -> Iterator[Variant]:
        if b"fp_contract" in ctx.file_source: return
        new_src = _inject_pragma(ctx.file_source, b"#pragma fp_contract(off)\n")  # after includes
        yield Variant(name="fp_contract_off", pattern_name=self.name,
                      description="file-scoped #pragma fp_contract(off)",
                      source=new_src, tags=frozenset({"tu_scoped"}))
```
**Acceptance gate (in the harness scorer, not the pattern):** for a `tu_scoped` variant —
1. build the whole .cpp once;
2. objdiff **every** function in that TU currently `complete`/`at_limit` (regression set) + target;
3. **accept iff** target match% strictly increases AND no other function decreases (any negative
   delta = hard reject; net TU byte-match >= baseline);
4. on any regression: reject the whole variant; optionally log the regressing symbols.

Required additions: a `safety_tier=="tu_scoped"` branch in the variant evaluator that expands
scoring to the unit's symbol set (enumerate via report.json / decomp.db); `Diagnosis` gains
`fma_need_off_count`/`fma_need_on_count`, populated by the `fma_mismatch` classifier already in
`scripts/analysis/batch_pattern_scan.py`.

### Files
New: `decomp-synth/decomp_synth/patterns/pragma_fp_contract.py`.
Modify: decomp-synth scorer (tu_scoped gating); `decomp_synth/types.py` (Diagnosis fields);
`patterns/__init__.py`.

### Expectation
Only 4 pure-OFF fns across 800 scanned; file-wide gating may cut even those. Safe and
deterministic once gated. Build as a small bonus, not a needle-mover.

---

## 5. Assessment (c): fmadds expression-restructuring ("need ON")

Step 7: **5** pure "need ON" fns (ClipDistMap, ArcDetector, Profiler, GamePanel, Part) where the
*target* uses `fmadds` but we emit `fmuls+fadds`. **No pragma forces contraction ON** — the
compiler contracts opportunistically on IR shape.

Feasibility **LOW**, narrow, fragile:
- Plausible transform: collapse split `t=a*b; r=t+c;` -> `r=a*b+c;` (fewer named temporaries ->
  contraction more likely) — a near-mirror of the existing `expression_grouping` transform.
- But there is **no oracle** for *when* UTC contracts (Step 7 only proved the OFF direction is
  deterministic). The ON direction is an un-RE'd heuristic, so this is blind permutation against
  an unknown — the same class the GPR validation proved 0/10. Expected hit rate low.
- One advantage: blast radius is local (single expression), no TU gating.

Recommendation: **defer.** During rule-(a) build, manually try the existing `expression_grouping`
on the 5 named fns; build a dedicated `fma_contract_hoist` pattern only if >=2 of 5 flip to
`fmadds` under collapse.

---

## 6. Test plan

- **`fpr_declaration_reorder` unit tests**: synthetic fns with 2-4 floats + interleaved ints;
  assert generated orders swap only float positions and map float-index->position correctly
  (directly exercises the Bug-A fix). Regression test reproducing Bug A (float at all-decls index
  3 but float-index 1) asserting the new code targets the right statement.
- **Integration test (the validated case)**: `Multiply(Vector3,Quat,Vector3)` from Rot.cpp:299 —
  assert the pattern proposes float-only reorders, that reordering moves the f31-swap set, and
  that the search explores the f31-targeted transpositions. Baseline 86.30%, and we have measured
  off-target orderings (84.42 / 85.42) as a ready-made golden fixture proving the lever fires.
- **`regmap_solver` test**: `guided_pairwise_search` with mixed int/float decls + an FPR pair ->
  asserts targeted swap hits correct all-decls indices, not raw float indices.
- **`pragma_fp_contract`**: 2-fn fixture .cpp where A needs OFF and B needs ON -> assert variant
  REJECTED (B regresses), proving the whole-file gate; second fixture where only A is FP-active ->
  assert ACCEPTED.

---

## 7. Honest ceiling estimate

Inputs (from already-read sources + this validation): ceiling_calculator full scan — across
**1,838** AT_LIMIT fns / 253,345 mismatch instrs, **FMA = 171 instrs = 0.2%**; "226 fns have
fixable encoding/FMA". Step 7 batch (800 fns): 14 FMA -> 4 pure-OFF / 5 pure-ON / 5 mixed.
Validation sample: 4 of 10 both-stuck "regswap" fns were FPR-swap-dominated.

| Lever | Plausibly-fixable both-stuck fns | Bytes | Confidence |
|---|---|---|---|
| `fpr_declaration_reorder` (a) | **~20-60** (mostly *partial* per-fn gains) | ~6-20K | MEDIUM (lever proven; per-fn full match not yet) |
| `pragma_fp_contract` (b) | **~4** (fewer after TU gating) | <1K | HIGH, tiny |
| fma restructure (c) | **0-3** | <1K | LOW |

Derivation for (a): ~40% of "REGISTER_SWAP"-labeled both-stuck fns are actually FPR (validation
sample); OFFSET_SWAP/scheduling labels hide more. Raw both-stuck FPR-swap population plausibly
30-80. BUT the *fixable* subset is only those whose swaps are **callee-saved** (f14-f31). The
validated function is the realistic shape: it mixes **movable** callee-saved swaps (4x f31) with
**stuck** volatile swaps (the dominant `f12<->f13`x10, f0-f13). So the expected per-function
outcome is **partial improvement** — drive the callee-saved f31 swaps toward 0 (+0.5-2% each) —
rather than full 100%. This is a *byte* harvest, not a *function-count* harvest. Net: ~20-60
functions plausibly improvable, **~6-20K bytes ~= 1-4% of the 495K both-stuck bucket**. A cheap
full sweep — `run_diff_inspect mode=regswaps` over all both-stuck fns, counting clean callee-saved
(f14-f31 <-> f14-f31) pairs — should be run during the build to pin the exact population.

**Bottom line:** the FPR float-reorder lever is genuinely real (measured: float-only reorder
moves the f31 swaps and changes match% three different ways) and was never wired up correctly
(Bugs A & B). It is a modest, bounded harvest — low-single-digit-percent of the both-stuck
bucket, mostly as *partial* per-function byte gains where callee-saved and volatile FPR swaps
coexist. It does **not** resolve the both-stuck floor — consistent with the validation doc's "the
FPR pivot is real but small." Recommended order: (1) land the Bug-A fix (independently correct);
(2) build rule (a) with the validated `Multiply` golden fixture + the all-pairs multiswap search;
(3) run the cheap callee-saved-FPR sweep to size the rollout; (4) build rule (b) as a safe gated
bonus; (5) defer (c).
