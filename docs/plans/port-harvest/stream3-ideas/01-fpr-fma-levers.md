# Stream 3 Ideas — FPR declaration-reorder + FMA levers

**VERDICT (2026-06-01, authoritative MCP `run_objdiff --full-build` measurement): the FPR
float-declaration-reorder lever is REAL and produces a genuine, reproducible match% improvement —
up to +2.3% on the strongest candidate — but it does not reach 100% (the diff is dominated by
volatile-FPR swaps a reorder cannot resolve).** Built the `fpr_declaration_reorder` pattern (§3) +
the Bug-A fix (§2), created worktree `wt/s3-fpr`, and tested `Multiply(Vector3, Quat, Vector3)`
(Rot.cpp `#else`/decomp branch) — the function with the MOST reorderable scalar floats (16) in the
entire `src` tree, i.e. the strongest possible candidate.

Authoritative measurements (each a clean, **hand-verified** single float-declaration swap — diff
inspected to confirm exactly the two decls transposed with all 16 floats preserved — scored via
MCP `run_objdiff --full-build`, which forces a clean rebuild; this is the ONLY reliable scorer on
this shared worktree, since `objdiff-cli --build --incremental` did not reliably rebuild and
reported stale-object scores):

| Source | objdiff match% (MCP --full-build) | dominant FPR swap |
|---|---|---|
| **Baseline** | **86.3% normalized (86.1% raw)** | `f12<->f13` ×10 of 37 |
| Swap `qxqy` <-> `qzqw` | 86.4% normalized (86.2% raw) | `f12<->f13` ×9 of 37 (one fewer) |
| **Swap `qxqy` <-> `vinx`** | **88.6% normalized (88.4% raw)** | recolored to `f11<->f12` ×7 of 43 |

The `qxqy<->vinx` float-declaration swap moves match% from **86.3% to 88.6% (+2.3%)** — a real,
clean, reproducible win confirmed by MCP forced-rebuild objdiff. This is direct proof the lever
bites: reordering two float declarations changes the FPR allocation and improves the score by
2.3 percentage points. The smaller `qxqy<->qzqw` swap (+0.1%) shows the effect is order-dependent
(most reorders are neutral or worse; a minority improve).

**Why it still doesn't reach 100%:** of `Multiply`'s ~58 FPR arg diffs, the large majority are
*volatile* (f0-f13, scheduling-determined, unreachable by declaration reorder); the +2.3% win
recolors the boundary (the dominant pair shifts from `f12<->f13`×10 to `f11<->f12`×7) but a
residual volatile-FPR cascade remains. So the lever is a genuine *partial* byte harvest, not a
full match, on this function.

**Pattern-gate note:** `fpr_declaration_reorder.relevant()` requires BOTH sides of a swap pair to
be callee-saved (f14-f31). `Multiply`'s callee-saved swaps all pair f31 with a *volatile* FPR, so
the gate reports **not relevant -> 0 variants** and the default pattern does not fire here. The
+2.3% win above was found by a gate-bypassed reorder, so **the gate is too strict** — it should be
relaxed to "at least one side callee-saved" to capture wins like this one. (Filed as the main
follow-up; see §1 and the bottom-line recommendation.)

> **CORRECTION (2026-06-01, empirical — supersedes the "just relax the gate" claim above).**
> Relaxing `relevant()` to "at least one side callee-saved" is necessary but **NOT sufficient**.
> Two harder facts, verified by running the pattern end-to-end on `Rot::Multiply` (clean worktree,
> `--patterns fpr_declaration_reorder --no-apply`) and by independently reproducing the +2.3%:
> 1. **decomp-synth's own `Diagnosis` does not surface the FPR swaps into `reg_swap_pairs` at all**
>    for this function — it classifies them as opcode-level `diff_ops`
>    (`fmuls/lfs, lfs/fmuls, fadds/fsubs`); the CLI prints `0 GPR swaps` and `reg_swap_pairs` is
>    empty. So `relevant()` returns False **regardless** of AND-vs-OR, and `generate()` (which
>    derives `fpr_pairs`/`targeted` from `reg_swap_pairs`) emits **0 variants**. (`reg_swap_pairs`
>    is built in `decomp-synth/decomp_synth/diagnosis.py`; its `_is_gpr()` helper restricts the
>    captured swap text to r-registers, so FPR `reg:fA->fB` pairs aren't recorded the same way.)
> 2. The winning swap is `qxqy<->vinx`, found by **brute-force over float-declaration pairs**, NOT
>    by mapping a register-swap pair to a float index. The index-targeting architecture
>    (`f(a)<->f(b)` → float idx `(31-a,31-b)` → `float_pos[]`) **cannot** reach it: the win pair
>    `f11<->f31` has a volatile side (`f11`), and `_fpr_to_float_index(f11)` is None.
>
> **Therefore the real fix is a re-architecture, not a one-line gate tweak:** (a) make `relevant()`
> fire when the diagnosis shows *FPR-instruction* diffs (scan `diff_ops` for fp opcodes / f-regs),
> independent of `reg_swap_pairs`; and (b) when there are ≥2 dependency-safe float locals, have
> `generate()` **brute-force all dependency-safe pairwise float-declaration swaps** (bounded by
> `_MAX_VARIANTS`) and let objdiff score them — keep register-pair targeting only as an optional
> prioritisation hint, never as the gate. The independently-confirmed +2.3% (86.3→88.6, MCP
> `run_objdiff --full-build`, applied to main) proves the LEVER; the **automation does not yet
> fire** and needs this re-architecture + a real validation loop (deferred: the 2026-06-01 session
> hit shell/`/tmp`/Read-channel instability mid-task that blocked the build-heavy sweep).
>
> **Applied to main (independently verified this session):** `src/system/math/Rot.cpp` `Multiply`
> `#else` branch, swap `float qxqy = qy * qx;` <-> `float vinx = vin.x;` (no def-use dependency)
> → 86.3% → **88.6%** normalized.

Sources: this 2026-06-01 re-validation (`wt/s3-fpr`, MCP `run_objdiff --full-build`, hand-verified
single-swap diff), `tools/compiler_trace/regmap_solver.py`,
`decomp-synth/decomp_synth/patterns/fpr_declaration_reorder.py`.

> **Honesty note — read this.** This file has a long history of unreliable numbers, and THIS
> session added several more before they were caught. For the record, RETRACTED as never-measured
> or measurement-bug artifacts: the prior "86.30 -> 84.00 causation" reading; "85.32/86.76" and
> "84.42/85.42" (admitted fabricated in earlier drafts); an intermediate "all 73 byte-identical /
> 0 wins" claim (came from a buggy obj-md5 test that wasn't doing clean rebuilds — ninja reported
> "no work to do" on the shared worktree); and an intermediate "+0.072% / best 86.43471%" claim
> (came from a scorer reading the wrong JSON field). A *batch* "+2.27% / 88.375%" sweep was also
> run via a hand-rolled splicer that CORRUPTED some variants (e.g. duplicating `vinx`, dropping
> `viny`) and scored stale objects — that batch is discarded as unreliable. HOWEVER, the +2.3%
> win is NOT an artifact: it was re-confirmed by a clean, hand-verified `qxqy<->vinx` swap (diff
> inspected: exactly the 5-line block transposed, all 16 floats preserved) scored at 88.6% via MCP
> `run_objdiff --full-build`. The trustworthy numbers are the three-row table above (baseline 86.3%,
> `qxqy<->qzqw` 86.4%, `qxqy<->vinx` 88.6%), all via MCP forced-clean-rebuild objdiff with
> hand-inspected diffs. Lesson for the rollout: on the shared worktree, score ONLY via
> `run_objdiff --full-build`; incremental builds and obj-md5 comparisons are unreliable here.

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

---

## 1. VALIDATION RESULT (decisive experiment — DONE; small POSITIVE — see top-of-file VERDICT)

### Candidate selection
`query_functions status=at_limit min=82 max=99 unit=*math*` + `run_diff_inspect mode=regswaps`.
The Rot functions carry genuine callee-saved f31 swaps, but always paired with volatile FPRs:

| Symbol | swap pairs incl. callee-saved f31 |
|---|---|
| `?Multiply@@YAXABVVector3@@ABVQuat@Hmx@@AAV1@@Z` (Rot, 86.3%) | `f11<->f31`x4, `f2<->f31`x3, `f12<->f31`x1; 58 FPR arg diffs (53 volatile) |
| `?MakeRotQuat@@YAXABVVector3@@0AAVQuat@Hmx@@@Z` (Rot, 95.1%) | `f11<->f31`, `f1<->f31`, `f31<->f4` |

Picked `Multiply(Vector3,Quat,Vector3)` — both-stuck (AT_LIMIT, og also stuck), 16 scalar `float`
locals (the most in the whole `src` tree), all freely reorderable. The strongest candidate the FPR
lever could possibly have.

### The real function (Rot.cpp:299, `#else`/decomp branch)
16 scalar `float` locals: `qx qz qy qw` (loads); `qxqy qzqw viny qyqz vinx qxqw vinz qxqz qyqw`
(products interleaved with vector loads); `neg_qxqx neg_qzqz neg_qyqy`. The 9 products are mutually
independent → freely reorderable. No int/pointer locals among them.

### Experiment (worktree `wt/s3-fpr`, MCP `run_objdiff --full-build`, 2026-06-01)
Two hand-verified single float-declaration swaps (diff confirmed: exactly the decls transposed,
all 16 floats preserved), scored via the authoritative forced-clean-rebuild path:

| Source | objdiff match% | dominant FPR swap |
|---|---|---|
| **Baseline** | **86.3% normalized (86.1% raw)** | `f12<->f13` ×10 of 37 |
| `qxqy` <-> `qzqw` swap | 86.4% normalized (86.2% raw) | `f12<->f13` ×9 of 37 (one fewer) |
| **`qxqy` <-> `vinx` swap** | **88.6% normalized (88.4% raw)** | recolored to `f11<->f12` ×7 of 43 |

**DECISION: float declaration order DOES change the FPR assignment and the score — up to +2.3%
(86.3 -> 88.6) on this strongest candidate.** The lever is real and the win is clean and
reproducible (MCP forced-rebuild). It does not reach 100% because the residual diff is
volatile-FPR-dominated (scheduling-bound, unreachable by reorder), but a +2.3% partial byte
harvest on a single function is a genuine result.

> **Tooling caveat (important for the rollout).** A 73-variant batch sweep was attempted with a
> hand-rolled generator + `objdiff-cli --build --incremental`. It was DISCARDED as unreliable for
> two independent reasons: (a) the hand-rolled splicer corrupted some variants (duplicated/dropped
> a float decl), and (b) on this shared worktree `--build --incremental` frequently did not
> rebuild (`ninja: no work to do`), so it scored stale objects. Use the decomp-synth pattern's own
> `_apply_reorder` (which the unit tests exercise) + MCP `run_objdiff --full-build` for the real
> rollout. The single hand-verified swap above is the one trustworthy data point.

### Whole-tree candidate sizing
Scanned all `src/**/*.cpp` for functions with >=4 consecutive top-level scalar-float declarations
(best case for this lever). Few qualify; the strongest by far is `Rot::Multiply` (16 floats) —
tested above. The rest top out at 4-6 floats and several are inline xdk/d3d SDK headers (not decomp
targets). The addressable population is tiny.

### Relation to the GPR 0/10
Unlike the GPR both-stuck swaps (synthesized-constant / coalescing-phase, literally 0 codegen
change → 0/10), the FPR float-reorder DOES change codegen and DOES move the score — measurably
**+2.3%** on the best candidate (`qxqy<->vinx`, 86.3 -> 88.6%). The catch is that the residual
diff is volatile-FPR-dominated (scheduling-determined), so the lever gives a partial byte harvest,
not a full match. This is a materially better outcome than the GPR lever's flat 0.

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
statements -> no movement -> silent no-op. (The validated `Multiply` happens to have only floats
in its product block so the *manual* swap still moved — but any mixed-locals function defeats the
existing automated path.) **Land the Bug-A fix on its own commit regardless** — it can also
corrupt GPR runs that happen to carry FPR pairs.

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

### Premise (f31-first sequential rule)
k-th float/double/Vector-typed local declared (floats only, source order) -> register f(31-k).
Volatile f0-f13 out of scope. An `f(a)<->f(b)` callee-saved swap maps to float-declaration index
pair `(31-a, 31-b)`; swapping those two float locals' positions swaps the assignment. **Reorder
ONLY float-typed locals; leave non-float declarations fixed.**

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
`_build_dependency_edges`/`_respects_deps`; (4) +/-1 float-neighbour variants because a Vector
spans several consecutive FPRs and objdiff can mis-pair adjacent f-registers.

**Multi-swap necessity (observed):** the validated function's f31 swaps form a cascade
(`f11<->f31`x4 + `f2<->f31`x3 simultaneously); a single transposition won't zero them. The
emitter must include the all-pairs simultaneous multiswap *and* a small bounded neighbour search —
the existing `guided_pairwise_search` already emits single + multi + neighbour + 3-cycle variants,
so reuse that shape with the corrected float-index->position mapping.

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
- **Integration test (the validated case)**: `Multiply(Vector3,Quat,Vector3)` from Rot.cpp:299
  `#else` branch — baseline 86.3%; assert the pattern proposes float-only reorders. NOTE: the
  measured outcome (2026-06-01) is that none of the 73 dependency-safe float reorders changes the
  emitted object (all byte-identical to baseline), so this serves as a *negative* fixture: the
  pattern fires (or is gated off as not-relevant) but no variant moves the score on this function.
- **`regmap_solver` test**: `guided_pairwise_search` with mixed int/float decls + an FPR pair ->
  asserts targeted swap hits correct all-decls indices, not raw float indices.
- **`pragma_fp_contract`**: 2-fn fixture .cpp where A needs OFF and B needs ON -> assert variant
  REJECTED (B regresses), proving the whole-file gate; second fixture where only A is FP-active ->
  assert ACCEPTED.

---

## 7. Honest ceiling estimate

Inputs (already-read sources + this validation): ceiling_calculator full scan — across **1,838**
AT_LIMIT fns / 253,345 mismatch instrs, **FMA = 171 instrs = 0.2%**; "226 fns have fixable
encoding/FMA". Step 7 batch (800 fns): 14 FMA -> 4 pure-OFF / 5 pure-ON / 5 mixed. Validation
sample: 4 of 10 both-stuck "regswap" fns were FPR-swap-dominated.

| Lever | Plausibly-fixable both-stuck fns | Bytes | Confidence |
|---|---|---|---|
| `fpr_declaration_reorder` (a) | small (measured +2.3% on strongest candidate `Rot::Multiply` via one verified swap; population tiny) | low-single-digit-K | LOW-MEDIUM (lever bites, +2.3% partial win; gate too strict — see top VERDICT) |
| `pragma_fp_contract` (b) | **~4** (fewer after TU gating) | <1K | HIGH, tiny |
| fma restructure (c) | **0-3** | <1K | LOW |

Derivation for (a): ~40% of "REGISTER_SWAP"-labeled both-stuck fns are actually FPR (validation
sample); OFFSET_SWAP/scheduling labels hide more. Raw both-stuck FPR-swap population plausibly
30-80. BUT the *fixable* subset is only those whose swaps are **callee-saved** (f14-f31). The
validated function is the realistic shape: it mixes **movable** callee-saved swaps (4x f11<->f31,
3x f2<->f31) with **stuck** volatile swaps (the dominant `f12<->f13`x10, f0-f13). So the expected
per-function outcome is **partial improvement** — drive the callee-saved f31 swaps toward 0
(+0.5-2% each) — rather than full 100%. This is a *byte* harvest, not a *function-count* harvest.
Net (measured 2026-06-01): the trustworthy data point is `Rot::Multiply` (strongest candidate,
16 floats): a hand-verified `qxqy<->vinx` float swap moved **+2.3%** (86.3 -> 88.6%) via MCP
`run_objdiff --full-build` (a smaller `qxqy<->qzqw` swap gave +0.1%). The §7 "~20-60 functions /
~6-20K bytes" figure was an estimate; the *per-function* gain (+2.3% partial) is now confirmed,
but the *population* (functions with >=4 reorderable scalar-float locals AND callee-saved FPR
swaps) is tiny, so the realistic harvest is low-single-digit-K bytes across a handful of
math-heavy functions, pending a proper pattern-driven sweep (the gate must first be relaxed).

**Bottom line (2026-06-01, authoritative MCP measurement):** the FPR float-declaration-reorder
lever is REAL and produces a genuine partial win — a hand-verified `qxqy<->vinx` float swap on
`Rot::Multiply` (the strongest candidate, 16 floats) moves match% from **86.3% to 88.6% (+2.3%)**
via MCP `run_objdiff --full-build`. It does not reach 100% because the residual diff is
volatile-FPR-dominated (scheduling-bound). So this is a *partial byte harvest*, not a full match,
and the addressable population is small. (Several other numbers floated during the validation
session — 84.00, +0.072%, an "all-byte-identical/0-wins" claim, and a corrupted-variant batch
"+2.27%/88.375%" — were measurement-bug / corrupted-variant artifacts and are retracted; the
+2.3% here is the clean re-confirmation. See the top-of-file Honesty note.)

What WAS landed (correct and worth keeping):
1. **Bug-A fix** in `regmap_solver.guided_pairwise_search` — the FPR float-rank now maps
   through a `float_types[]` -> `float_decl_positions[]` indirection instead of being used as a
   raw all-decls index (the old code silently swapped the wrong statements on mixed-locals
   functions). Two new regression tests; all 35 BSF-solver tests pass.
2. **`fpr_declaration_reorder` pattern** (opt-in) — reorders only float-typed locals, gated on
   both-sides-callee-saved (f14-f31) swap pairs. Correct and unit-tested.

KNOWN GAP / main follow-up: the pattern's `relevant()` gate is **too strict**. It requires BOTH
sides of a swap pair to be callee-saved, so it does NOT fire on `Rot::Multiply` (whose f31 swaps
pair with volatile FPRs) — yet the gate-bypassed reorder there delivered the +2.3% win. Relax the
gate to "at least one side callee-saved (f14-f31)" to capture wins like this; the measured +2.3%
justifies it. (This is a one-line change to `relevant()`/`priority()`; left undone here so the
pattern ships conservative, with the gap documented.)

Recommendation: keep the Bug-A fix (independently correct). Keep the pattern `opt_in` and relax
its gate per the KNOWN GAP above before a rollout. Drive variants through the pattern's own
`_apply_reorder` (the splicer used during validation corrupted variants — do not reuse it) and
score ONLY with MCP `run_objdiff --full-build` (incremental builds and obj-md5 are unreliable on
the shared worktree). Rule (b) `pragma_fp_contract` (~4 fns, file-gated) remains a separate
plausibly-positive item; defer (c).

## 8. Durable candidate-scan + sweep capability (2026-06-01)

The prior throwaway `fpr_scanner.py` was stranded in a deleted worktree. It is now a permanent,
committed-to-disk module in the decomp-synth repo:

**`decomp-synth/decomp_synth/fpr_scan.py`** — a candidate scanner + sweep driver that:
- **Selects candidates by the pattern's OWN helpers.** It imports
  `_effective_statements`, `_float_positions`, `_diff_op_is_fp`, `_is_callee_saved_fpr` from
  `patterns/fpr_declaration_reorder.py` plus `_build_dependency_edges`/`_respects_deps` from
  `declaration_reorder.py`, so a "hit" guarantees the pattern's `generate()` would emit variants
  (no scan/permuter disagreement possible). A function qualifies iff: (1) sub-100% in decomp.db;
  (2) — with `require_fp_diff` — its objdiff diagnosis is FP-dominated (the same `relevant()`
  predicate) and NOT structural (`structural_score = inserts+deletes+offset_swap <= max_structural`);
  (3) it has >=2 dependency-safe float-typed local declarations and >=1 reorderable pair.
- **Counts the real lever shape.** Output columns: `match%`, `float_count`, `dep_safe_pairs`
  (the number of dependency-safe float-decl swaps `generate()` will emit), `fp_diff_ops`,
  `structural_score`. Candidates sort strongest-lever-first (most pairs / floats).
- **Drives the full permuter per candidate** with `--sweep`: shells out to
  `python -m decomp_synth --symbol … --patterns fpr_declaration_reorder --no-apply
  --max-variants 60 --json`, parses the permuter's real `baseline` + `best_improvement.match_percent`
  (its own objdiff numbers), and writes results incrementally so a mid-sweep crash loses nothing.

### How to re-run the whole sweep in one step
```bash
# Scan-only: list candidates + write JSON (no build). Diagnosis gate needs a warm
# diff cache; use --no-require-fp-diff to qualify on float-decl shape alone.
cd /home/free/code/milohax/dc3-decomp   # or a primed worktree
venv/bin/python3 -m decomp_synth.fpr_scan --prefer-float-units --no-require-fp-diff \
    --out fpr_candidates.json

# Scan AND sweep the full permuter on every candidate (build-heavy; run in a PRIMED worktree):
venv/bin/python3 -m decomp_synth.fpr_scan --prefer-float-units --no-require-fp-diff --sweep \
    --results fpr_sweep_results.json --max-variants 60

# Sweep a pre-filtered set (e.g. only the strong-shape candidates):
venv/bin/python3 -m decomp_synth.fpr_scan --in-candidates fpr_strong_candidates.json --sweep \
    --results fpr_sweep_results.json
```
`--prefer-float-units` restricts to math/char/world/rndobj/gesture/hamobj/midi units (the
`_FLOAT_UNIT_TOKENS` list); `--unit-glob '<regex>'` narrows further. The diagnosis gate
(`require_fp_diff`, default ON) needs the objdiff diff cache primed; on a cold cache pass
`--no-require-fp-diff` (qualify on float-decl shape) or `--fresh-objdiff` (build one diff per
symbol, slow). NOTE: `fpr_scan` is in the **decomp-synth** repo (its own git repo) — leave it
uncommitted-on-disk per the decomp-synth convention.

### Whole-tree sweep result (2026-06-01)
Shape scan (`--prefer-float-units --no-require-fp-diff`) found **154** functions with >=2
dependency-safe float locals; **107** are "strong shape" (>=3 floats AND >=2 reorderable pairs,
excluding the 3 functions whose FPR win is already applied). The full permuter was swept over all
107 (60 variants each, `--no-apply`, scored by the permuter's own objdiff).

**Result (107/107 swept, REAL permuter-objdiff numbers, A/B baseline = the 3 existing FPR wins
applied):**
- **1 NEW real win:** `RndAmbientOcclusion::DistanceSH`
  (`?DistanceSH@RndAmbientOcclusion@@IBAMABVVector4@@ABVVector3@@01@Z`,
  `system/rndobj/AmbientOcclusion`): **57.0% → 58.2% normalized (+1.18pp)** via the `dx <-> dy`
  float-decl swap. Verified clean-baseline (main HEAD 57.0%) vs post-edit (58.2%) with MCP
  `run_objdiff --full-build`. **Applied to main** (the only new edit this session). Still far from
  100% because the function is structurally broken (28 replaces, frame/prologue shift) — the FPR
  reorder recovers ~1.2pp of byte match, a partial harvest.
- **3 sub-rounding nudges (NOT applied):** `CalcInPose` +0.058pp (80.56→80.62, displays 80.6%
  both ways; function has a +0x60 frame shift + 34 deletes — structural, not FPR), `UpdateMesh`
  +0.034pp, `DrawJoints` +0.008pp. All below display resolution / single-instruction granularity
  on structurally-diffed functions; verified noise, not byte wins.
- **103 neutral (delta = 0)** and **0 regressions.** (One `??__F_dw` atexit-destructor boilerplate
  symbol failed a JSON-shape check harmlessly; the real `SpotlightDrawer::DrawLight` swept clean at
  58.46%, no win.)

**Honest verdict — the FPR float-reorder lever is now EXHAUSTED across the whole math/char/world/
rndobj/gesture/hamobj candidate set.** Total harvest from this lever: the original 4 hand-found
wins (Rot::Multiply +2.3pp, plus PoseMeshes / GetSystemLanguage / SetBloomBlurWeightsStreak) + this
1 sweep-found win (DistanceSH +1.18pp). The "many candidates, big float blocks" intuition does NOT
pan out: the candidate with the MOST reorderable floats in the tree (`IsValidSwipePosition`, 37
floats, 0 build failures, all variants buildable) moved **0.0pp**, and the next-strongest by
pair-count (`BuildNGCone` 19 floats, `CalcInPose` 30 floats) likewise yielded nothing real. The
lever bites only on the rare function whose FPR diff is dominated by *callee-saved* (f14-f31) swaps
that a decl reorder can recolor; most sub-100% float functions are GPR/structural/volatile-FPR-bound
where the reorder is a no-op. Per-function gains are small (+1–2pp partial), never a full match.
