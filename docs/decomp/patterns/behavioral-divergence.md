# Behavioral Divergence: Metric-Invisible Bugs

**The class of bug that objdiff % cannot see.** These are wrong-behavior defects
where the normalized match is **neutral or near-neutral** (often already ≥95%, some
at 0% as unimplemented stubs) yet the compiled code does the wrong thing at runtime.
They do not show up as a match regression, so a permuter sweep, a "looks-matched"
review, or a green build will all pass them. They are found by **running the game**
and by **auditing source shape against the target's actual semantics** — not by
chasing %.

**Why this doc exists:** in one 2026-07 wave, eight of these shipped or were latent
in the native-critical camera / text / scoring paths (details below). Several were
*introduced* by earlier match-improving work (a permuter sweep, an og-port, an
asm-archaeology pass) precisely because the change was match-neutral. Read this
before trusting any "behavior-neutral" claim, and before certifying a residual as a
floor.

> This is the inverse of [harmful-avoid.md](harmful-avoid.md): that doc lists
> neutral source shapes that *lower* the metric. This doc lists source changes that
> keep the metric but *break behavior*. Both are cases where % is the wrong signal.

---

## The taxonomy (each is a real DC3 case)

| Sub-class | Metric effect | Runtime effect | DC3 case | Commit |
|---|---|---|---|---|
| Non-commutative operand swap | neutral (±0.1%) | value negated / wrong | `SetupCharacter` `z0 - k` → `k - z0` | fixed df487ac8 |
| Non-commutative **argument** swap | ~neutral | wrong call result | `FontMap3d::SetupCharacter` `XfmOnCircleEdge(pos,size)` | fixed fabe57a5 |
| Comparison direction / boundary | neutral | inverted branch / off-by-one | `SetPausedHelper` `<=` vs `>=` | fixed cbb7cc16 |
| Float reassociation | neutral | different rounding | `CamShotFrame::Interp` `(a+1)*b` vs `a*b+b` | fixed 059ba913 |
| Spurious / missing guard | small | code path skipped | `InsertMoveInSong` extra `if(anim)` | fixed cbb7cc16 |
| Dropped code block | moderate | feature silently dead | `ForeachKeyframe` missing `remove_keyframe` block | fixed 2c133d63 |
| Aliased self-clobber | 0% (stub) | degenerate output | `Transform::LookAt` writes `m.z`, reads aliased `up` | fixed a9fc8528 |
| Reversed container-op args | small | **UB / heap corruption** | `ResetDetectFrames` `erase(end(),begin())` | fixed f57f2307 |
| Stale iterator after realloc | small | **UB / walk-off** | `OnComputeCharWidths` iterator across `push_back` | fixed fabe57a5 |
| Wrong body of a 0%-stub | invisible (0% either way) | anything | `Transform::LookAt` (unpaired COMDAT) | fixed a9fc8528 |

---

## 1. Non-commutative operand & argument swaps

**The trap:** for a *commutative* op (`+ * & | ^`, and commutative calls `Min`/`Max`/
`Dot`), swapping operands is genuinely behavior-neutral and is a legitimate lever
(see [fixable-operators.md#commutative-operand-order](fixable-operators.md)). For a
**non-commutative** op it is a silent corruption that the metric usually cannot see:

- `a - b` ≠ `b - a` (negation) — `a / b`, `a % b`, `a << b` likewise.
- `f(x, y)` ≠ `f(y, x)` for any non-commutative callee — argument *position* is part
  of the contract.
- `a < b` ≠ `a > b`, and `a <= b` vs `a < b` is an off-by-one at the boundary.

### Why the metric misses it
A bare operand/arg swap frequently byte-matches the **branch polarity or register
shape** the target used, so the instruction stream aligns just as well (or better) —
the *values* flowing through are wrong but the *opcodes* match. Example — the
canonical DC3 case, introduced by a permuter sweep (`f5f704d6`), fixed in `df487ac8`:

```cpp
// SetupCharacter, glyph quad vertical extent (text lies in the XZ plane).
float z1 = z0 - _tmp1 * state.mSize;   // CORRECT: constant glyph height
float z1 = _tmp1 * state.mSize - z0;   // SWEEP "WIN": normalized-NEUTRAL @83.8%, but
                                       // reflects the quad -> zero height for centered
                                       // text -> ALL text invisible.
```

The subtraction is `a - (b*c)`; the "improvement" flipped it to `(b*c) - a`, the
negation. Match was 83.8% both ways. Every glyph collapsed to zero height on native
and web. See also `SetPausedHelper` (`SetGamePaused(paused, mState <= kGamePlaying)`
should be `mState >= kGamePlaying`, `cbb7cc16`) and `FontMap3d::SetupCharacter`
(`XfmOnCircleEdge(circlePos, size)` should be `XfmOnCircleEdge(size, circlePos)` —
the target loads `size` into the `circumference` param, `fabe57a5`).

### Detection
- **objdiff tell:** `diff_arg` on a `fsubs`/`subf`/`divw`/`cmpw` where the *operand
  registers* differ but the opcode matches — and the surrounding data-flow is a
  subtraction/division/comparison, not an abs/FMA context. Distinguish from the
  genuine commutative-swap floor: a commutative swap is on `fadds`/`fmuls`/`xor`/
  `add`/`or`/`and`/`mullw` with the *same registers both sides*
  ([unfixable-compiler.md#commutative-register-swap](unfixable-compiler.md)). If the
  op is non-commutative, an operand-order diff is a **bug lead, not a floor**.
- **Source tell:** any permuter/port/archaeology commit whose diff swaps operands of
  `-`, `/`, `%`, `<<`, `>>`, a comparison, or a function-call argument list. Grep the
  commit for these. `scripts/scan_behavioral_idioms.py` flags call-arg and
  comparison-direction changes between two revisions.
- **Rule:** treat any `X - Y` → `Y - X` (or `f(a,b)` → `f(b,a)`) that "improved"
  match as guilty until you prove the callee/op is commutative.

---

## 2. Float reassociation (rounding divergence)

Algebraically-equal float expressions round differently. The compiler will not
reassociate across a named temporary, so the *source grouping* is load-bearing.

```cpp
// CamShotFrame::Interp DOF, fixed 059ba913 (97.0 -> 99.5)
focusMult * focalDist + focalDist          // compiled as (focusMult+1)*focalDist
                                           //   (fadds+fmuls) — WRONG rounding
float scaledFocalDist = focusMult * focalDist;   // named temp blocks reassociation
scaledFocalDist + focalDist                //   -> fmuls+fadds, target's exact bits
```

This is the same family as [fixable-fsel-fma.md](fixable-fsel-fma.md) (FMA fusion)
and [fixable-operators.md#fma-expression-order](fixable-operators.md), but the
behavioral point is: on the native port the DOF blend now produces *identical bits*
to Xbox. **Detection:** `fadds`/`fmuls` vs `fmadds`/`fnmsubs` opcode swap, or a
`(k+1)*x` factoring the target wrote as `k*x + x`. Split the product into a named
temp.

---

## 3. Spurious / missing guards and dropped blocks

The most dangerous because the metric barely moves and the *feature just doesn't
work*.

- **Spurious guard** — `InsertMoveInSong` (`cbb7cc16`, 95.8 → 100%) had an
  `if (anim)` the target binary does not: both `anim->SetKeyVal` move-timeline writes
  were skipped whenever `SongAnim()` returned null. On the matched PPC path the guard
  is removed (the Xbox `SongAnim` never returns null); native keeps an explicit
  `if (!anim) return;` under `#ifdef HX_NATIVE` because the native `SongAnim`/`Find`
  *can* be null. This is the general shape for guard divergences: **match the target
  on the PPC path, guard defensively under `HX_NATIVE`.**
- **Dropped block** — `ForeachKeyframe` (`2c133d63`, 83.2 → 100%) was missing the
  entire `sRemoveFrame` consumption tail, so every DTA `remove_keyframe` was silently
  ignored (song-load midi parsers, perform scripts). Recovered from the target asm +
  RB3 reference.

**Detection:** a null-check / early-return / whole `if`-block present in our source
but absent in the target asm (or vice-versa). `run_diff_inspect mode=diagnose` shows
the extra/missing branch as an `insert`/`delete` cluster with a `diff_op` polarity
row — do not dismiss those as "block sinking" without confirming the block's *body*
exists on both sides. For unimplemented handlers, diff against the RB3/og-dc3
reference body, not against 0%.

---

## 4. Aliased self-clobber

A function that writes a member and then reads a same-typed reference parameter is a
hazard when callers pass **that very member** as the argument.

```cpp
// Transform::LookAt — the wrong decomp (fixed a9fc8528). Callers do:
//   xfm.LookAt(mLastTargetPos, xfm.m.z);   // `up` aliases xfm.m.z
void Transform::LookAt(const Vector3 &target, const Vector3 &up) {
    m.z.Set(target - v);   // clobbers m.z ...
    m.y = up;              // ... then reads `up` == m.z -> m.y == m.z: degenerate!
}
// CORRECT (target asm / og-dc3): write forward into m.y first, up into m.z:
    Subtract(target, v, m.y);
    m.z = up;
    Normalize(m, m);
```

The degenerate basis was re-derived from float noise by `Normalize`, producing a
chaotic, run-to-run **nondeterministic** camera roll. **Detection:** a member write
followed by a read of a `const T&` parameter, where callers pass the same member.
Grep callers for `foo.Method(..., foo.member)`. Nondeterministic runtime output
(differs across identical runs) is a strong tell for reading clobbered/uninitialized
state.

---

## 5. Container-op contract bugs (UB, not just wrong output)

These compile, often match well, and are **undefined behavior** on the native
host-STL — latent crashes and heap corruption, not merely wrong pixels.

- **Reversed range args** — `ResetDetectFrames` (`f57f2307`, 90.6 → 98.6):
  `mDetectFrames.erase(end(), begin())` had its arguments reversed. With `last <
  first`, STLport copies `[begin, finish)` onto `end` and runs destructors
  past-the-end whenever the vector is non-empty at reset (difficulty change /
  practice-mode). The target loads `begin` into r4, `end` into r5 — proving it was
  `clear()`/`erase(begin, end)`. `erase(X.end(), X.begin())` is **always** a bug.
- **Stale iterator across reallocation** — `OnComputeCharWidths` (`fabe57a5`): an
  iterator held across a `push_back` that can reallocate the vector → walk-off. Any
  iterator/reference/pointer into a `std::vector` is invalidated by a growth
  operation.

**Detection:** `scripts/scan_behavioral_idioms.py` greps for the reversed-`erase`
idiom (`erase(X.end(), X.begin())`) and for iterators used after a `push_back`/
`insert`/`resize` in the same scope. The reversed-`erase` check is exact (zero false
positives); the stale-iterator check is heuristic (review each hit).

**Caveat — the original game may have shipped the bug.** An exact reversed-`erase`
hit is not automatically a decomp mistake. `run_objdiff` first: `ResetDetectFrames`
was *our* transcription error (target proved `begin`/`end` order → fixed to `clear()`,
raising the match). But `DingoServer::AddDelayedCalls`
(`src/system/net/DingoSvr.cpp:129`) is **99.9% matched with the reversed `erase` in
place** — the retail binary genuinely contains `erase(end(), begin())`; it is a
faithful decomp of an original-game bug. **Do not "fix" a match-faithful instance on
the PPC path** — that diverges from the target. If native actually reaches it, guard
the safe form under `#ifdef HX_NATIVE` (here: `clear()`, the author's evident intent)
and leave the matched `#else` path intact. DingoServer is Xbox-Live networking not
reached on the native port, so it is left as-is. This is the same principle as the
`InsertMoveInSong` guard: **match the target on PPC, correct behavior only under
`HX_NATIVE`.**

---

## 6. Wrong body of a 0%-stub (invisible to the metric entirely)

`Transform::LookAt` was an **unpaired COMDAT stub** — the target emits it folded into
`ClipCollide.obj`, DC3 emits it out-of-line in `mtx.obj`, so objdiff never pairs it
and it reads **0% both before and after** any change to its body. A wrong body here
has *zero* metric signal. These are found only by runtime behavior or by auditing the
body against the target disassembly / a reference decomp. **Any function whose
run_objdiff verdict is `Stub (High)` / unpaired but which is actually called on native
is untrusted until its body is read against ground truth.** See
[at-limit-systemic.md](at-limit-systemic.md) and
[verifiable-icf.md](verifiable-icf.md) for ICF/COMDAT pairing.

---

## Anti-pattern: the "regalloc floor" false certification

When a diff shows many register renames, the tempting conclusion is "register-
allocation cascade → unfixable floor." In the 2026-07 wave, **five functions Opus
certified as regalloc floors were not** — a Fable retry seeded with the verifier's
per-instruction leads took them to 98.6 / 93.2 / 100 / 100 / 99.5 and found three of
the real bugs above. "Many renames" is frequently the *downstream cascade* of one
upstream source-shape cause (loop signedness, a hoisted reference, an operand order,
a template-overload pick).

**Certification standard** — before writing `at_limit`/floor, satisfy **one** of:
1. A **noted permuter/decomp-synth run** on the function (config + result: e.g. "beam,
   8 rounds / 80 variants / depth 4, 196 candidates, 0 > 98.58%"), OR
2. A **per-mismatch equivalence argument** covering *every* non-noise row in
   `run_diff_inspect mode=diagnose` — not a blanket "regalloc cascade." Chase each
   `cmplw` vs `cmpw` (signedness), each `li rN,0` vs `mr rN,r3` (different source
   value), each branch-polarity `diff_op` to a source cause first.

Accepted floor classes (identify per-instruction): same-register commutative swaps
([unfixable-compiler.md#commutative-register-swap](unfixable-compiler.md)), address-
relocation noise, MakeString template-hash noise, PGO block-sinking
([at-limit-systemic.md](at-limit-systemic.md)).

---

## Workflow: how to actually find these

1. **Instrument-first, gated.** Add `#ifdef HX_NATIVE` + `getenv("DC3_*_DIAG")`
   logging at the suspect layer and *run the game* (`scripts/dc3-agent-test.sh`, or
   headless with `MILO_SCREENSHOT_FRAMES`). The 2026-07 camera bug was localized by
   logging the shot's world-basis handedness (`upZ`, determinant) per frame; the text
   bug by logging glyph-quad bbox extents. Diagnostics stay in the tree — they are
   PPC-neutral (guarded) and reusable (`DC3_CAM_DIAG`, `DC3_ROOT_DIAG`,
   `DC3_TEXT_DIAG`, `DC3_FEK_DIAG`/`DC3_FEK_STUB` already landed).
2. **Test causality by stub/revert before deep work.** The `ForeachKeyframe`
   hypothesis for the camera flip was killed in one 5-minute run by an env-gated stub
   (`DC3_FEK_STUB=1`) that showed the symptom unchanged. Cheap disproof first.
3. **Audit source shape against ground truth**, not against %. Read the target asm
   (`run_diff_inspect mode=asm_listing`/`mismatches`), og-dc3
   (`/home/free/code/milohax/og-dc3-decomp`), and RB3 (`lookup_rb3`) for the *values*
   and *call order*, not just the opcodes.
4. **Retry certified floors with a stronger model, seeded with instruction leads.**
   Opus-breadth → strict verifier that rejects lazy floor certs → Fable retry on the
   rejects with the verifier's exact `idx N: opcode A vs B` leads inlined.
5. **Runtime-verify the fix visually.** Static review + green unit suite is
   insufficient for camera/UI/scoring/flow changes — screenshot and confirm pixels
   (see the top-level note in
   [../../native/](../../native/) and the runtime-verify memory).

---

## PPC-neutrality of the fixes

Every fix above is either entirely inside `#ifdef HX_NATIVE` (zero match% impact) or a
genuine decomp logic correction verified with `run_objdiff` on **main's own build**
(match% held or improved — most of these *raised* it). A fix that touches shared PPC-
matched code must be `run_objdiff`-checked with `project_dir` pointing at the working
tree before landing. Do not touch load-bearing native math
(`RndCam::GetViewProjectXfms` / `projMtx.y.y`, commit a16912fc).

---

## See Also

- [harmful-avoid.md](harmful-avoid.md) — the inverse: neutral shapes that *lower* the metric
- [fixable-operators.md](fixable-operators.md#commutative-operand-order) — when operand swaps *are* safe (commutative ops only)
- [unfixable-compiler.md](unfixable-compiler.md#commutative-register-swap) — the genuine same-register commutative-swap floor
- [fixable-fsel-fma.md](fixable-fsel-fma.md) — FMA fusion / reassociation control
- [fixable-comparison.md](fixable-comparison.md) — signed/unsigned and comparison-direction codegen
- [at-limit-systemic.md](at-limit-systemic.md) — ICF/COMDAT, block sinking, the floor classes you may legitimately certify
- `scripts/scan_behavioral_idioms.py` — codebase scanner for the cleanly-detectable idioms here
