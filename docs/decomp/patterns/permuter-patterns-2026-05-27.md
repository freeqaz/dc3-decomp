# Permuter Patterns — Session 2026-05-27

New permuter patterns shipped this session and proposal-stage candidates harvested from sweep agents' reflections. Companion to the broader [INDEX.md](INDEX.md) catalogue.

## Tooling fixes that landed today

These are not patterns themselves but unblock other patterns. All in main.

| Commit | Change | Effect |
|---|---|---|
| `d400c3a7` | `clang_types`: per-project compdb resolution | libclang now resolves types on RB3 as well as DC3 |
| `a829a164` | `clang_types`: TU parse fix (`--`, `-Xclang -include-pch` stripping) | libclang's `_IDX.parse` no longer fails silently |
| `c1c898a1` | `argument_swap`: type+overload guards | fail rate 54.5% → ~20%, 19 wins preserved |
| `ecc73720` | `symbol_str_compare`: positive-Symbol gate (fail-open → fail-closed) | fail rate 91.9% → ~0%, no longer emits `.Str()` on `int` |
| `296c5769` | `const_ref_swap`: `argument_list`/call/`nullptr` guards | fail rate 38.2% → ~5% |
| `0bdd0b4c` | `signed_unsigned`: pointer-identifier guard | skips `(int)ptr` casts that would never compile |
| `6eb49b01` | `permuter.json compiler=msvc` (DC3) | DC3 `varext` emits `auto _tmp =` (was emitting `int _tmp = <ptr>`) |
| `44a763e8` | `setup_worktree`: CoW reflinks | 2.9 GiB apparent → 1.2 MiB exclusive per worktree |

## New patterns shipped

### `reference_elimination_chain` — commit `070f3288`
- **File**: `scripts/permuter/patterns/reference_elimination_chain.py`
- **Trigger**: same diagnosis gates as `reference_elimination` (callee-saved regswap + clusters).
- **Generator**: collects all eliminable `auto& ref = ...` declarations, applies the first, re-parses, finds the next candidate, repeats up to depth 4. Emits depth-2/3/4 variants in a single sweep round.
- **Why it exists**: `WorldCrowd::SetFullness` needed three manual refelim sweeps to climb 83.1 → 91.0%. The chain pattern collapses that into one variant.
- **Validated wins**: BandPatchMesh::FindXfm (RB3, partial — see notes below); fires on RndText::WrapText producing depth-2/3/4 variants.
- **Caveat surfaced**: chained refelims can produce pointer-aliasing (RB3 FindXfm aliased `endFace` onto `foundFace`, generating an always-true `endFace == endFace` guard). The chain pattern itself is safe; the downstream alias check needs tightening (see proposed `pointer_reuse_alias_guard` below).

### `loop_body_assign_hoist` — commit `d2c072dc`
- **File**: `scripts/permuter/patterns/loop_body_assign_hoist.py` (260 lines, 20 tests)
- **Trigger**: insert/delete clusters near a `bl` inside a loop body, with `mr rN, rM` register-move signal adjacent to the call.
- **Generator**: scans `for`/`while`/`do` bodies for `X = Y;` simple assignment that FOLLOWS a call statement, where neither statement reads what the other writes and the assignment's RHS is not the call's return value. Hoists the assignment ABOVE the call. Also handles 2-step lookahead (assignment 2 positions after call) — the exact Bitmap::Load shape.
- **Why it exists**: `RndBitmap::Load` 94.3 → **100%** was a manual one-line fix the permuter missed — moving `workingMip = newMip` before `newMip->Create()` in the mip-chain loop. MWCC/MSVC schedules the assignment early to free a register before the call.
- **Validated**: `test_bitmap_load_shape_exact` reproduces the pre-fix Bitmap::Load source verbatim and asserts the pattern emits the correct hoisted variant.

### `signed_unsigned_cast_polarity` — commit `c97c5b09` (registration: see __init__.py)
- **File**: `scripts/permuter/patterns/signed_unsigned_cast_polarity.py` (34 tests)
- **Trigger**: diff_op pair is in the polarity-flip set `{bge↔ble, blt↔bgt, bge↔blt, ble↔bgt}` OR a `cmpw↔cmplw` / `cmpwi↔cmplwi` pair appears alongside a `bge/ble/blt/bgt` branch in the same diff. Does NOT fire on `beq↔bne` (those have no polarity).
- **Generator**: walks only `<`/`<=`/`>`/`>=` comparisons (never `==`/`!=`). Applies `(unsigned int)` and `(int)` casts to left, right, or both operands. Uses libclang when available to skip casts that wouldn't actually flip signedness. Reuses `signed_unsigned`'s pointer-identifier guard.
- **Why it exists**: RB3 sweep agents repeatedly saw `bge↔ble` polarity flips where `signed_unsigned` either over-fired (low priority 0.4) or didn't differentiate. This pattern fires at priority 0.7 specifically for the polarity-flip subset.
- **Composition note**: deliberately overlaps with `signed_unsigned` on `<`/`>`. The batch runner deduplicates variants by content hash, so no wasted compiles. The tight relevance gate + equality skip prevents most overlap in practice.

## Proposal-stage patterns

Reflected back by sweep agents this session. Each names the unlocked target(s) and the trigger shape. Pick from highest leverage first.

### High leverage (multiple targets, clear shape)

1. **`mwcc_regorder_probe`** — RB3 mwcc analog of `declaration_reorder`, but specifically targets `this->member` access ordering (MWCC's callee-saved allocator goes in declaration order). Probe 5–10 orderings per function automatically. Would unlock band3's regswap-blocked functions (IsSpotlightGem 98.1%, DrawTrackMasks 96.3%, UpdateLeftyFlip 96.8%, SetupGems 95.4%, GetBestHit 99.1%, ScoreSinger 99.0%) that currently fall back from BSF because BSF is cl.exe/wibo-specific.

2. **`bool_materialize_guard`** — Detect compound expressions like `selected + (!!gathering - firstShowing)` where `!!boolExpr` is needed to coerce a bool to int. Without `!!` the compiler emits `cmpwi cr6, r11, 0; bne ...; (no materialize)`; with it, emits the 4-instruction `li 0; beq; li 1; clrlwi.` sequence. Pattern would wrap eligible bool operands. Targets HamNavList::UpdateGestures (cluster 4 at idx 266-269) and likely many similar arithmetic-with-bool sites.

3. **`pointer_reuse_alias_guard`** (safety, not a win pattern) — Verify that when `reference_elimination_chain` or `reference_elimination` aliases a sentinel pointer onto a result pointer (e.g. `foundFace = endFace`), the sentinel is not READ again after the alias. Currently allows transformations like `endFace = ...; ... ; endFace == endFace` which always-true and break logic. Filter at variant-emission time.

4. **`fabs_vs_fabsf` swap** — MWCC on PPC generates identical code for `fabs(x)` and `fabsf(x)` on a `float` since the PPC `fabs` instruction is type-agnostic. A trivial swap pattern would have caught the Rot.cpp and BandPatchMesh wins without needing a full variant search. Low cost, high precision.

5. **`int_to_float_batch_reorder`** — Targets CharBonesSamples::EvaluateChannel's `comp >= kCompressVects` branch where target collapses load+convert per-element but our base loads ALL ints first then converts ALL. Pattern: extract each `(short*)p` operand to a local `int` BEFORE the `(float)` cast, splitting declaration from use by one statement.

### Medium leverage (single known target, but precise shape)

6. **`loop_var_hoist`** — Hoist `const` values computed in loop headers before the loop body. GemManager DrawTrackMasks / SetupGems show `missing_guard` clusters inside loops that resolve when loop-invariant computations move outside.

7. **`virtual_call_force`** — When a virtual method on a reference parameter is called in a loop and the permuter sees the call get devirtualized (no vtable dispatch), insert a `static_cast<T*>(&ref)->method()` indirection to force vtable emission. Targets MergeObjectsRecurse (70%).

8. **`bool_pointer_normalize_suppressor`** — When `(long)ptr & mask` generates `cntlzw/extrwi` boolean-normalization sequences, try `reinterpret_cast<int>(ptr) & mask` or `(uintptr_t)ptr & mask`. Targets obj/Utl::ReloadObjectType (93.9%).

9. **`spill_promoting`** — When the base spills a parameter early to stack but the target keeps it in a register, insert a `volatile T spilled = arg;` to force the spill slot. Targets obj/Utl::ReloadObjectType and similar.

10. **`stack_frame_local_array_hoisting`** — Move large local arrays declared inside conditional blocks to function scope. Targets HamSkeletonConverter::Set (worldJoints[kNumJoints] declared inside an `if`).

11. **`nested_scope_register_pressure`** — Move float variables declared inside nested scope but used across multiple sub-calls to function scope to reduce register pressure. Same HamSkeletonConverter::Set context (pelvisX/Y/Z, 3 extra callee-saved GPRs).

### Diagnostic-only (classify, don't fix)

12. **`static_init_guard_protocol_detector`** — Detect MSVC V1 (raw guard int, `| 1` to set) vs V2 (thread-safe `$S1` counter) static init guard mismatches. Both are AT_LIMIT — pattern would auto-classify and save investigation time. Targets CheckShadow (72.7%).

13. **`bool_width_coercion`** — When a bool-returning function's result is shifted into a u64 bitfield, the compiler may generate an extra `rldicl/clrlwi` zero-extend depending on whether it treats the bool as 1-bit (mb=33) or 8-bit (mb=40). AT_LIMIT classifier. Targets CalcShaderOpts (94.9%).

14. **`subic_subfe_bool_mask_idiom`** — The `subic/subfe/extsw/neg` 4-instruction sequence is an equivalent unfixable form of the existing `subfic/subfe` BOOLEAN_NEGATION class. Currently only the 2-instruction form is detected; the 4-instruction form is left as `LikelyFixable (Medium)` and wastes investigation time.

15. **`cross_class_method_substitution_diagnoser`** — When the target calls `C2::method()` but source calls `C1::method()` (same return type), the permuter silently produces 0 improvement. New diagnosis mode would classify this as `WRONG_METHOD_CALL` and direct to Ghidra-guided research. Targets HamNavList::UpdateGestures (CUgtFilter::gathering vs UIListState::ScrollPastMinDisplay).

## Validated wins from this session — bullet list

DC3 main:
- `RndBitmap::Load` 94.3 → **100%** (loop-body-assign-hoist, manual)
- `WorldCrowd::SetFullness` 83.1 → 91.0% (triple refelim — motivated the chain pattern)
- `RndMesh::OnSync` 84.2 → 93.9% (do/while loop restructure ported from RB3)
- `Spotlight::BuildCone` 81.9 → 87.3% (condition_arithmetic + type_width_change)
- `RndText::FitTextScroll` 68.8 → 72.7% (Ghidra-guided manual)
- `RndText::DrawShowing` 69.4 → 72.9% (Ghidra-guided manual)
- `RndSpline::SyncDeformedDummyCtrlPoints` 73.5 → 83.7% (varinline)
- `RndSpline::SyncDeformedCtrlPoints` 54.7 → 60.5% (cmpeq)
- `CharBonesSamples::EvaluateChannel` 83.8 → 84.1% (type_width_change)
- `HamSkeletonConverter::Set` 73.3 → 73.6% (member_ref_bind + chain compose)
- 6 incidental wins on Bitmap/Crowd/Spotlight/ClipDistMap/MemHeap/CharBones contexts

RB3 main:
- `BuildChordMesh` 96.0 → **100%** (const Hmx::Color32& binding)
- `EndianFixBase` 33.7 → **61.0%** (declaration_movement + statement reorder)
- `Rot::Multiply` 82.5 → 84.9% (RB3 sweep wave 3)
- `UtilDrawPlane` 89.4 → 90.9% (initializer_literal + Dot argument swap)
- `VocalTrack::PrepareNoteTubes` 91.2 → 91.9% + UpdateLyricZ
- `BandHeadShaper::AddDegrees` 98.4 → 98.9%
- `MemInit` 97.6 → 98.0%, `MemPrintOverview` 99.5 → 99.8%
- `Rot::{MakeScale, RotateAboutZ, Invert, FastInvert, MakeRotQuat}` (+0.04–+0.85%)
- `BandPatchMesh::WorkVerts::ExtendTwin` 71.5 → 72.6%
- `Dxt1Compress::storedxtencodedblock` 91.7 → 91.9%
- `GemManager::AddChordBracket` 89.9 → 90.2%

## See also

- [INDEX.md](INDEX.md) — full pattern catalogue with ROI table
- `permuter-sweep-landscape` (memory) — where wins come from, AT_LIMIT reality, tooling state
- objdiff audit findings: see session notes — top PR rec is `frame_size` in `PrologueMismatchInfo` + `typed_args` lookup in `compute_call_diff`
