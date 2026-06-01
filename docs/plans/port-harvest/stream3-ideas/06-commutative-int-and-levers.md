# Stream 3 Idea 06 — INT commutative_op_order + new-lever survey

---

## ⚠️ CORRECTION (2026-06-01, later same day — the original verdict below was OVER-STATED for one class)

**The blanket "INT commutative_op_order is NOT source-fixable" claim is WRONG for the `subf`-based
integer ABS IDIOM.** That sub-case carries a real, behaviour-neutral SOURCE lever, proven through
the real toolchain AND banked on real functions. The rest of the original verdict (plain
`xor`/`add`/`or`/`and`/`mullw` 2-term spelling swaps with identical registers) STILL HOLDS — those
remain backend artifacts. But abs is a genuine exception, and it was mis-classified as part of the
floor.

### The lever: rewrite the open-coded abs idiom as the conditional-negate ternary

The two behaviour-identical integer-abs spellings lower to DIFFERENT commutative `xor` operand slot
order on cl.exe 16.00.11886 / c2.dll, **on the SAME physical registers**:

| Source spelling | Emitted (real `.cod`) | xor slot |
|---|---|---|
| `(v ^ (v >> 31)) - (v >> 31)` (open-coded bitwise) | `srawi r11,r10,31` ; `xor r10,r11,r10` | **SIGN first** |
| `v < 0 ? -v : v` (conditional-negate ternary) | `srawi r10,r11,31` ; `xor r11,r11,r10` | **VALUE first** |

The ternary selects the compiler's **intrinsic abs lowering**, which canonicalizes the commutative
slot to VALUE-first; the hand-rolled bitwise sequence canonicalizes to SIGN-first. The real DC3
target was compiled from the ternary/`abs()` form, so it emits VALUE-first; a decomp that
hand-rolled the bitwise idiom emits SIGN-first and objdiff flags it as a same-register
`COMMUTATIVE_OP_ORDER` xor swap — which IS fixable, by this rewrite.

This is NOT the generic operand-spelling swap (those stay inert — §3 below is still correct). It
works because it changes the *abs lowering selection*, not the operand spelling of a fixed sequence.

Proof fixtures (real toolchain, re-runnable per their headers):
`tools/compiler_trace/fixtures/abs_spellings.cpp` (the minimal A/B control: ternary→value-first,
bitwise→sign-first), `tools/compiler_trace/fixtures/commutative_regalloc_levers.cpp`
(`abs_inline`/`abs_sign_after` = SIGN-first `7d6a5278`, `abs_sign_first` = VALUE-first `7d4a5a78`),
and `tools/compiler_trace/fixtures/scrolltotarget_xor_levers{,2}.cpp` (the W2 ternary variant is
the one that flips both xors to value-first at fixed registers).

### The decl-order / symbol-ID sub-lever (equally-ready operands)

The breakthrough fixture also confirmed a finer mechanism for the equally-ready-operand case:
`abs_sign_first` (sign temp DECLARED before the value temp, re-rooted off the source expression so
it can precede the value) flips the slot vs `abs_sign_after`, with identical registers. So
declaration order of two equally-ready already-live xor operands IS a slot handle — BUT in practice
the much more robust and general handle is the ternary rewrite above (it does not require
re-rooting a branch-computed value, which `abs_sign_first` needed and which fails when the value is
conditional — see §boundary). The ternary is what the shipped pattern emits.

### fmuls volatile-vs-callee-saved split (carried from doc 05)

The fmuls "floor" is NOT monolithic: **callee-saved-FPR** fmuls/fmadds (f14-f31) IS reachable —
decl reorder flips which value loads into fr31 vs fr30 (the existing `fpr_declaration_reorder`
pattern, already banked +2.3% on `Rot::Multiply`). **Volatile-FPR** fmuls (f0-f13) is genuinely
floored (loads normalize; `fp_prod_ab/ba/spell` all emit `fmuls fr1,fr0,fr13`). So callee-saved FP
is already-harvested; volatile FP remains the real floor. (Spelling swap `a*b`↔`b*a` stays inert.)

### ScrollToTarget CONVERSION + harvest (verified, every % from `run_objdiff full_build`)

`UIListState::ScrollToTarget` (the decisive test) **CONVERTED 99.4% → 100.0%** — both its xor
slot mismatches (idx18 `xor r9,r3,r11`, idx19 `xor r8,r31,r10`) flipped to VALUE-first by collapsing
the open-coded abs temp chains to `adjusted < 0 ? -adjusted : adjusted` / `diff < 0 ? -diff : diff`.
This **REFUTES the floor for the abs class** and banks the bytes.

Full source-population harvest (the open-coded integer-abs idiom is rare — a `decomp_synth.pattern_sweep`
`ast`-mode scan of the whole sub-100% DB plus a source grep found exactly these sites):

| Function (unit) | Before | After | Note (all `full_build` verified) |
|---|---|---|---|
| `UIListState::ScrollToTarget` | 99.4% | **100.0%** | both xor slots flipped; banked on main |
| `DanceRemixer::JumpedMeasureAdd` | 94.2% | **100.0%** | abs-ternary fixed the whole r10↔r11 cascade; `step = count>0?1:-1` fixed the last cmpwi/li reorder; banked on main |
| `DanceRemixer::JumpedMeasureStepsBetween` | 93.0% | 97.8% | abs region fully matched (`(unsigned)>>31` logical → signed `srawi` abs); residual = unrelated r27↔r28 callee cascade + MakeString template arg diff; banked on main |
| `LocalizeSeparatedInt` (utl/Locale) | 95.3% | 97.2% | abs region matched; residual = `gLocalizeSepIdx` reloc/layout noise + digit-loop modulo `clrrwi/srwi`; banked on main |

**Harvest: 4 functions improved (2 to 100%), banked on main.** Note `LocalizeSeparatedInt` proved
the target genuinely used the signed-abs lowering: rewriting it as a plain `-num` (inside the
already-`negative` branch) DROPPED to 93.6% because the target emits the full `srawi/xor/subf`
abs sequence even there — the ternary `num < 0 ? -num : num` is the matching form, NOT `-num`.

### The precondition boundary (honest)

The lever bites when **the source contains an open-coded integer-abs computation of a single value
`v`** — either inline `(v ^ (v>>31)) - (v>>31)` or the split-temp chain
`s = v>>31; xr = v^s; absv = xr - s` — and that value's `abs` feeds a commutative `xor` whose slot
order is the mismatch. It does NOT help:
- plain (non-abs) `xor`/`add`/`or`/`and`/`mullw` same-register slot swaps (still the floor, §2/§3);
- functions where the dominant mismatch is an independent regalloc cascade or a template/call/global
  difference (it cleanly fixes the *abs sub-region* but leaves the rest — see StepsBetween/Locale);
- the `(unsigned)v >> 31` LOGICAL-shift variant — that is a DIFFERENT computation (sign bit 0/1, not
  the 0/-1 mask) and is correctly NOT matched (StepsBetween's source had this bug; the target wanted
  the signed-abs `srawi`, so the ternary fixed it).
The trivial 2-term fixtures (`prod_ab/ba`, `acc_a/acc_b`) did NOT flip — the compiler fully
canonicalizes those — confirming the lever needs (a) the abs *idiom selection* knob, not mere
operand reordering.

### Tooling delivered

`declaration_reorder` / `variable_extraction` / `first_use_reorder` / `commutative_swap` /
`temp_elimination` do NOT span this transform (confirmed: full `--patterns all` permuter run on
original ScrollToTarget found 0 wins; named-candidate run also 0). NEW opt-in pattern
**`int_abs_to_ternary`** (`decomp_synth/patterns/int_abs_to_ternary.py`, registered opt-in,
+ tests in `decomp_synth/tests/test_int_abs_to_ternary.py`, 14/14 pass) recognizes both the inline
and split-temp-chain abs shapes and emits the ternary rewrite; it scores ScrollToTarget 100.0% via
the permuter's own objdiff. Scanned/swept via `decomp_synth.pattern_sweep --qualify-patterns
int_abs_to_ternary --qualify-mode ast`.

**Net correction:** the abs sub-case is a +4-function harvest (2× 100%, 2× partial), a refuted
floor, and a new banked source lever. Everything else in the original verdict (below) stands.

---

**VERDICT (2026-06-01, authoritative — 7 real toolchain fixtures + 6 real-function objdiff
inspections): the INTEGER subset of objdiff's `COMMUTATIVE_OP_ORDER (LikelyFixable)` signal
(`xor`/`add`/`or`/`and`/`mullw`, the `subf`-based abs idiom) is NOT source-fixable, for the EXACT
same reason the FP subset isn't (doc 05).** The A/B operand order of a commutative integer op is a
backend, register-driven emission decision made AFTER register allocation — a function of which
physical GPR each operand lands in and which register is reused as the destination, NOT of the
source expression's operand spelling, statement order, or operand liveness. objdiff's
`COMMUTATIVE_OP_ORDER (LikelyFixable)` label is **misleading for integers too**. This closes the
frontier that doc 05 explicitly left open.

This is a **validated NEGATIVE** (the mission's success criterion): we have a precise mechanism and
hard evidence, so we can re-label the INT case and stop hand-chasing it.

The lever survey (§4) found **no new source-side lever** beyond what decomp-synth already
implements, and **no new compiler-introspection capability** beyond the existing (already
negative) BSF tracer. The two items with any residual leverage are both already-built and
already-swept: float-decl-reorder (exhausted, doc 01) and 3+-term commutative regrouping (already
in the permuter as `commutative_swap.py`). Honest harvest from this whole investigation: **0 new
functions / 0 new bytes**; the value is the re-label and the closed-out frontier.

---

## 1. The commutative_op_order population (INT vs FP breakdown)

`decomp.db` (`has_commutative_op_order=1`, the flag covers ALL commutative ops, int and FP):

| Slice | Count |
|---|---|
| total `has_commutative_op_order=1` | **209** |
| below 100% | **199** |
| in 99–100% band | **60** |
| in 95–100% band | **96** |

The DB stores only a boolean, not the mismatching opcode, so INT-vs-FP separation requires
inspecting the actual diffs (`run_diff_inspect mode=mismatches`). From a representative sweep of the
high-band, commutative-as-sole-signal set (`has_register_swap=0 AND has_offset_swap=0 AND
has_control_flow=0`, ordered by match%):

- The high band is **dominated by FP** functions whose lone mismatch is an `fmuls`/`fadds`/`fmadds`
  A/B swap — the doc-05 floor. Verified FP this session: `FastInvert` (fmuls), `Normalize`/`Quat`
  family, `PushClip@ClipPlayer` (fadds), `BuildSphereStratified` (fadds), `SetLocalRotIndex`
  (fmuls), `ReFitTextScroll` (fadds), `MoveToDeltaFacing` (fadds), `SetFrame@QuatKeys/FloatKeys`,
  `UpdateFloorSpotTransform` (fmuls), etc.
- The **genuine INT-opcode** commutative functions are the minority — the non-FP-math ones.
  Verified INT this session (all 6 are `xor`/`add`): see §2.

**Honest INT-only count:** the DB can't filter it directly, but from the representative inspection
the INT-opcode commutative functions are a small minority of the 199 (the bulk are the FP floor).
Sizing it precisely would require diffing all 199; the qualitative result is decisive enough that
it isn't worth the build cycles — **every INT case inspected has the same-register pure-swap
signature, i.e. the same non-fixable floor as FP.**

---

## 2. Same-registers-vs-different-registers triage (the critical distinction)

For each INT commutative mismatch: are the two source-operand registers the SAME on both sides
(pure A/B slot swap → backend-canonicalization floor, like FP) or DIFFERENT (a real regalloc/
decl-order lever)? **All six inspected INT cases are SAME-register pure swaps.**

| Function (unit) | % | idx | Target | Base (ours) | Regs same? | Class |
|---|---|---|---|---|---|---|
| `op0@ByteGrinder` | 99.6 | 21 | `xor r11,r10,r11` | `xor r11,r11,r10` | **SAME** | pure swap |
| `op6@ByteGrinder` | 99.6 | 22 | `xor r11,r10,r11` | `xor r11,r11,r10` | **SAME** | pure swap |
| `ScrollToTarget@UIListState` | 99.4 | 18 | `xor r9,r3,r11` | `xor r9,r11,r3` | **SAME** | pure swap (abs idiom) |
| `ScrollToTarget@UIListState` | 99.4 | 19 | `xor r8,r31,r10` | `xor r8,r10,r31` | **SAME** | pure swap (abs idiom) |
| `Equals@SongCollision` | 99.7 | 31 | `add r8,r7,r10` | `add r8,r10,r7` | **SAME** | pure swap (+ offset-shift cascade) |
| `Equals@SongCollision` | 99.7 | 35 | `add r11,r7,r11` | `add r11,r11,r7` | **SAME** | pure swap (+ offset-shift cascade) |
| `Create@RndBitmap` | 99.8 | 63 | `add r25,r28,r11` | `add r25,r11,r28` | **SAME** | pure swap |
| `Create@RndBitmap` | 99.8 | 114 | `add r25,r28,r25` | `add r25,r25,r28` | **SAME** | pure swap |
| `GetKey@LightPreset` | 99.9 | 100 | `add r10,r25,r29` | `add r10,r29,r25` | **SAME** | pure swap |
| `file_do@curl` | 99.4 | 160 | `add r11,r11,r10` | `add r11,r10,r11` | **SAME** | pure swap (+ frame-shift cascade) |

**Result: 10/10 INT commutative mismatches are SAME-register pure A/B swaps.** Zero
different-register cases. This is byte-for-byte the doc-05 FP signature: identical physical
registers on both sides, differing only in which source operand occupies the first slot. By the
doc-05 mechanism this is necessarily a post-register-allocation emission choice — source cannot
reach it without changing the register assignment itself.

Two of the functions (`SongCollision::Equals`, `file_do`) show the commutative `add` swap
**co-occurring with an offset/stack shift** in the same diagnose (e.g. SongCollision: dominant
offset delta +4 across the `lfs` loads AND the two `add` swaps). That is direct evidence the
commutative swap is a *downstream symptom* of a single upstream regalloc/scheduling/frame
difference, not an independent fixable lever — fixing the operand order in isolation is impossible
because there is nothing in the source that controls it.

---

## 3. Toolchain fixtures — what is / isn't source-controllable for INT commutative ops

Built `tools/compiler_trace/fixtures/int_commutative_operand_order.cpp` (mirrors the structure of
`fmuls_operand_order.cpp`). Compiled through the real toolchain (cl.exe 16.00.11886.00 / c2.dll via
32-bit wibo, project flags `/O1 /Oi /GR /EHsc` → per-function `/Ogsu`, `/FAcs` listing). **Every
hex below was read from the actual `.cod`** — re-runnable per the fixtures README.

| # | Probe (the source variable being tested) | Prediction | Observed (hex / mnemonic) | ✓/✗ |
|---|---|---|---|---|
| 1 | `i1_xor_ab` (`a^b`) vs `i1_xor_ba` (`b^a`), 2 ptr loads | spelling discarded → identical | both `7d635278` = `xor r3,r11,r10` | ✓ |
| 1b | `add` and `or` spelling (`a±b` vs `b±a`) | spelling discarded | add both `7c6b5214`; or both `7d635378` | ✓ |
| 2 | `i2_free` vs `i2_pinned` — **same source `a+b`**, different forced GPR assignment | slot follows register, not source | `i2_free` `7c6b5214` (`add r3,r11,r10`) vs `i2_pinned` `7c6a5a14` (`add r3,r10,r11`) — **opposite slot, identical product** | ✓ (decisive positive control) |
| 3 | `i3_dest_reuse_{add,xor}` (dest reg == operand `a`'s reg r3) | read dest-equal slot | `add r3,r3,r4` (`7c632214`), `xor r3,r3,r4` (`7c632278`) — dest-equal r3 is FIRST (matches FP rule) | ✓ |
| 4 | `i4_{a_live,b_live,a_last,b_last}` — vary which operand is live-after & store order | liveness does NOT move the slot for a fixed register assignment | a_live/a_last/b_last all `7c6a5a14`; b_live `7c6b5214` — bytes differ ONLY because liveness changed the LOAD ORDER (recolored the variable); the slot still tracks the physical register (value in r10 is always slot-1) | ✓ (killer test) |
| 5 | `i5_abs_{vs,sv}` — the exact ScrollToTarget abs idiom, `x^s` vs `s^x` | spelling discarded | both `7d6a1a78` = `xor r10,r11,r3` | ✓ |
| 6 | `i6_callee_saved` — a→r31, b→r30 (live across a call) | register-driven slot | `add r3,r30,r31` (`7c7efa14`); `xor r3,r30,r31` (`7fc3fa78`) — r30 first, r31 second (same shape as FP f5) | ✓ |
| 7 | `i7_shared_add` (`base+p[0]` vs `p[1]+base`) — shared-value position | shared position discarded | both adds `7d6b2214` = `add r11,r11,r4` (loaded value first, shared base second) | ✓ |

**Net: 7/7 fixtures confirm the model; zero contradictions.** Fixtures 1, 1b, 5, 7 prove source
spelling / shared-value position is discarded (byte-identical). Fixture 4 is the killer test:
varying operand liveness changes the bytes ONLY by recoloring the variable into a different
register (a regalloc lever), and the slot order still tracks the physical register, not the source
variable. Fixture 2 is the decisive positive control: the **same** source product `a+b` emits
**opposite** slot orders purely because the surrounding code forced a different value→GPR
assignment. This is precisely the FP result transposed to integers.

### The rule, stated for a human to apply by eye (INT)
Given a commutative integer op whose two source values landed in physical registers `rX`, `rY` with
destination `rD`:
- **Source operand spelling, statement order, and operand liveness do not affect the A/B slot
  order** (proven byte-identical / register-following). Do not try to flip it by editing the
  expression.
- The slot order is fixed by the **post-register-allocation value→GPR assignment + instruction
  schedule**. Liveness is a *register-allocation* lever (it moves the variable to a different
  register), not an *operand-order* lever.
- When the destination register equals one source operand's register, **our c2 build emits that
  dest-equal operand FIRST**; the real DC3 target frequently emits it SECOND on an otherwise-
  identical assignment (e.g. ScrollToTarget idx18 `xor r9,r3,r11`; SongCollision idx31
  `add r8,r7,r10`). That is the same backend-version canonicalization difference doc 05 found for
  fmuls — not a source choice.

### Diagnostic shortcut
A lone commutative INT mismatch (`xor`/`add`/`or`/`and`/`mullw`) with **identical registers on both
sides** = backend operand-order artifact (regalloc floor). Classify immediately as not-hand-fixable;
do not permute the expression. (Same heuristic as doc 05 for FP.)

---

## 4. Other-lever survey (ranked)

### Landscape data (the both-stuck floor)
Signal-flag histogram over the 2,255 `AT_LIMIT`, sub-100%, non-stub functions in `decomp.db`:

| Flag | Count | Share |
|---|---|---|
| `has_register_swap` | 999 | 44% |
| `has_control_flow` | 472 | 21% |
| `has_offset_swap` | 395 | 18% |
| `has_commutative_op_order` | 184 | 8% |
| `has_comparison_style` | 20 | <1% |
| `has_dead_store_elimination` | 9 | <1% |
| `has_prologue_mismatch` | 0 | 0% |

(register/offset/commutative co-occur heavily — they are different surface symptoms of the same
underlying regalloc+schedule+frame state, as the SongCollision and file_do cascades show.)

### What decomp-synth ALREADY covers (checked this session)
The permuter has **140+ patterns**. Critically:
- `commutative_swap.py` **already encodes the correct insight** — its docstring states *"Simple
  binary swaps (`a + b` → `b + a`) produce identical code on MSVC PPC"* and it ONLY emits 3+-term
  **regrouping** variants (`(a+b)+c` → `a+(b+c)` / reversed / permuted), which change register
  lifetimes/scheduling. It never attempts the inert 2-term swap. **There is no gap to fill for
  commutative operand order** — the naive swap is proven a no-op (this doc + doc 05), and the only
  thing that moves it (regrouping/regalloc perturbation) is already implemented.
- Signedness / type-width: `signed_unsigned.py`, `signed_unsigned_cast_polarity.py`,
  `type_width_change.py`, `sizeof_signed_cast.py`, `u8_to_unsigned_long.py`, `cast_insertion.py`.
- Comparison style: `comparison_flip.py`, `comparison_equivalence.py`, `branch_polarity.py`,
  `positive_branch_invert.py` (the `x>0` vs `x!=0` lever from CLAUDE.md is already automated).
- Declaration/statement ordering: `declaration_reorder.py`, `fpr_declaration_reorder.py`,
  `statement_reorder.py`, `assignment_reorder.py`, `first_use_reorder.py`,
  `parameter_live_range.py`, `member_init_reorder.py`.
- Control-flow shape: `branch_polarity.py`, `early_return_merge.py`, `guard_to_nested.py`,
  `loop_rotation_to_while.py`, `switch_if_convert.py`, `goto_to_*`, `single_return.py`, and ~20
  more. The control-flow lever space is already densely covered.

### Compiler-introspection levers (new flags / dumps) — checked, NEGATIVE
Tested whether c2.dll v16.00.11886 exposes any backend (regalloc/scheduling) dump we could exploit:
- `/FAcs` listings carry **only final instruction bytes** — no spill/coalesce/schedule annotations
  (verified by grep over a fresh `.cod`).
- Undocumented backend flags: `/d2dbAssemblyDumps` is silently accepted (rc=0) but emits nothing;
  `/d2:-listregalloc` is rejected by the backend (`C1007 unrecognized flag in 'p2'`, confirming c2
  is "p2" and rejects `/d2:` syntax). No accessible regalloc/schedule dump.
- The only white-box window into the backend remains the existing **BSF graph-coloring tracer**
  (`tools/compiler_trace/bsf_trace.py`) — and that was already run end-to-end and validated NEGATIVE
  for the GPR decl-reorder both-stuck set (0/10, see MEMORY `stream3_binary_oracle_negative`). There
  is no cheaper introspection lever; the expensive one already returned 0.

### Ranked levers

| Rank | Lever | New? | Expected leverage | Status |
|---|---|---|---|---|
| 1 | 3+-term commutative **regrouping** (perturb schedule via associativity) | No | LOW (already swept as part of beam search) | `commutative_swap.py` exists; keep |
| 2 | FPR float-decl-reorder | No | LOW (exhausted) | doc 01; built, 5 wins banked, EXHAUSTED |
| 3 | 2-term commutative operand swap (INT or FP) | — | **ZERO (proven inert)** | DO NOT BUILD — this doc + doc 05 |
| 4 | New c2 backend-dump flag | Probed | **ZERO (no such flag)** | NEGATIVE this session |
| 5 | comparison_style / signedness / control-flow restructuring | No | already-covered; case-by-case | densely covered by existing patterns |

**Concrete next experiment for the top item (rank 1, the only one with non-zero residual leverage):**
the both-stuck floor is dominated by `register_swap` (44%) + `control_flow` (21%) + `offset_swap`
(18%), which are *coupled* (one frame/regalloc difference cascades into all three). The single
highest-leverage probe left is NOT another operand-order lever but a **schedule-perturbation sweep**:
take the ~50 highest-band (≥99%) both-stuck functions whose lone cluster is a SAME-register swap
near a 3+-term commutative chain, run `commutative_swap.py` regrouping + `statement_reorder.py` +
`declaration_reorder.py` jointly through the beam (this is exactly what `permute` already does), and
measure the hit rate. **Expectation from this investigation: low** — the same-register signature
means the schedule is already canonical and a regrouping has to luck into the original's exact
coloring. If a focused beam over those 50 functions yields <2 wins, declare the commutative/swap
band a closed floor and re-label all `COMMUTATIVE_OP_ORDER` near-misses (FP and INT) as
regalloc-floor in the heuristic. (The prior GPR binary-oracle run already returned 0/10 on the
representative both-stuck regswap set, so the prior is strongly toward 0.)

---

## 5. Recommendations

1. **Re-label the heuristic for INT too.** `COMMUTATIVE_OP_ORDER` on integer opcodes
   (`xor/add/or/and/mullw`, abs-`subf`) with **identical registers on both sides** is NOT
   LikelyFixable — it is a post-regalloc backend canonicalization with no behaviour-neutral source
   lever, exactly like the FP case. The doc-05 recommendation to split FP vs INT can now be
   collapsed: **both are the same floor.** Update
   `docs/decomp/patterns/fixable-operators.md#commutative-operand-order` to carry the INT caveat.
2. **Do not build a 2-term commutative-swap pattern.** Proven inert for both INT and FP. The
   existing `commutative_swap.py` (3+-term regrouping only) is correct as-is.
3. **The both-stuck commutative band is closed.** The realistic harvest from the entire
   commutative_op_order signal (199 sub-100 functions) is 0 functions via direct operand-order
   edits; any residual wins come only from the already-implemented regrouping/decl-reorder beam,
   whose prior is near zero on same-register both-stuck functions.

---

## Evidence index (reproducible)
- Fixtures: `tools/compiler_trace/fixtures/int_commutative_operand_order.cpp` (+ README), 7
  fixtures, all PROVEN; recompile via the one-liner in the file header.
- Real-function objdiff: `run_diff_inspect mode=mismatches/regswaps/diagnose` on op0/op6
  (ByteGrinder), ScrollToTarget (UIListState), Equals (SongCollision), Create (RndBitmap), GetKey
  (LightPreset), file_do (curl) — all SAME-register pure swaps (§2 table).
- Source confirmation: `src/system/ui/UIListState.cpp:565` `ScrollToTarget` is the
  `(x^(x>>31))-(x>>31)` abs idiom; the mismatching `xor`s are `xor_adj`/`xor_dif`.
- Landscape: `decomp.db` flag histogram over 2,255 AT_LIMIT sub-100 non-stub functions.
- c2 flag probes: `/d2dbAssemblyDumps` (no-op), `/d2:-listregalloc` (C1007 rejected), `/FAcs` (no
  backend annotations) — all via `tools/compiler_trace/invoker.py`.
- Toolchain: c2.dll `build/compilers/X360/16.00.11886.00/c2.dll`, 32-bit wibo
  `/home/free/code/milohax/wibo/build/debug/wibo`.
