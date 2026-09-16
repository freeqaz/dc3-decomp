# Detector build-out — four lanes against the classes with no tooling

**Written 2026-09-16.** Companion to
[2026-09-15-two-month-native-impact-bug-review.md](2026-09-15-two-month-native-impact-bug-review.md),
which established the gap this plan closes.

## Why these four

The taxonomy counted 524 net behavioural bugs. **350 of them — 67% — are in
classes 1–4, which have no detector between them.** Wrong-callee (97 bugs) has
seven tools. Tooling went where tooling was easy, not where the bugs are.

But "no detector" is not by itself a reason to build one. The organising
principle is narrower and it matters:

> **A detector pays only where the bug can score 100%.**

Classes 2 (inverted condition), 3 (loop bound) and 4 (arithmetic) mostly surface
as ordinary objdiff mismatch rows — a flipped `beq`, a different `cmplwi`
immediate and a substituted `fnmsubs` all cost points, and an immediate or
displacement diff survives normalization. Those get caught by routine matching
work. Their *residue* is the instances sitting inside functions that already
read 100%.

The classes worth a tool are the ones the ruler structurally cannot see:

| Class | Why invisible | Lane |
|---|---|---|
| 7 — Save/Load field-order | both sides do the same *shape* of reads; only the wire bytes differ | **A** |
| 1 — wrong field, same width | identical instruction, plausible-but-wrong offset | **B** |
| 10 — uninit / missing store | objdiff scores code, never a static's initial bytes | **C** |
| 5 — wrong `.data` constant | `lfs` is byte-identical whichever value is behind it | **D** |

## The contract every lane owes

Non-negotiable, because this repo has shipped six instruments that lied and each
one had already been used to declare an area exhausted:

1. **`CoverageReport` from `scripts/analysis/coverage.py`.** Declare the
   universe, route *every* discard through `cov.drop(reason)`, `cov.emit()` as
   the exit code. A bare `continue` is the defect this module exists to catch.
   Read its docstring before starting.
2. **A two-sided sabotage control.** Not "the sabotaged run reports a finding" —
   the *pair*: clean → N findings, sabotaged → N+1, and the row must land in the
   right bucket. One-sided controls pass when the harness is broken.
   ⚠ Run the sabotaged copy from a **freshly created empty directory**. These
   scripts do `sys.path.insert(0, dirname(__file__))`, so running a copy from
   `/tmp` puts `/tmp` first on `sys.path` and a scratch `/tmp/dis.py` shadows the
   stdlib — the control then *crashes*, which looks exactly like *found nothing*.
   Never send a control's stderr to `/dev/null`. This cost a cycle on 2026-09-16.
3. **Register in `scripts/analysis/determinism_check.py`** and show it agrees
   with itself across two `PYTHONHASHSEED` values on non-empty output.
   `sorted()` every `glob` feeding a `setdefault` — unsorted glob + first-write-
   wins is the `scope_index_census` defect and two tools still had it today.
4. **`honesty_lint.py` must gain no new errors.** Baseline is **2**, both
   pre-existing `LIKE 'sqlite_%'` in `scripts/orchestrator/tests/test_fresh_db_schema.py`
   (`8fcac464f`). Do not "fix" those; they are someone else's ratchet.
5. **A pattern doc** in `docs/decomp/patterns/` stating the mechanism, what the
   tool cannot see, and the manual recognizer for the part it cannot reach.
6. **State the denominator in the summary line.** If your summary can be printed
   without knowing how many rows you never looked at, it is a sample presented
   as a total.

Work in a worktree: **`scripts/setup_worktree.sh <bare-name>`** (bare name, not a
path; ~13 s, configures ninja and the native build). `decomp.db` does not exist
there by design — pass
`--db /home/free/code/milohax/dc3-decomp/decomp.db` explicitly. Always pass
`project_dir` to orchestrator MCP tools.

**Reference implementations, both landed today:** `access_specifier_scan.py`
(built to this contract from scratch) and `bss_initializer_scan.py` (retrofitted).
Read one before writing yours.

---

## Lane A — Save/Load field-order desync (class 7)

**Known:** 14 bugs. **Population:** 278 paired Save/Load classes. **Tooling:** none.

Rev-gated `BEGIN_LOAD`/`BEGIN_SAVES` blocks whose field order doesn't match the
target's byte layout. Both sides perform the same shape of reads, so the **wire
bytes land on the wrong member** — `StreamRenderer::Save` wrote
`mPlayer1/2/3DepthColor` twice and silently lost players 4–6; `CharFeedback::Load`
skipped a 4-byte int on the rev<6 path and desynced every field after it.

**Two traps, both already paid for:**
- ⚠ **The `this` register varies.** r31 in `RndFlare`/`CharUpperTwist`, **r30** in
  `RndText` where r31 holds a member object. A detector keyed naively on offsets
  rebuilds the documented `FxSendChorus::Load` false positive, where
  `run_analyze_function` resolved a pure `(r1)` stack slot against the class
  struct and sent a lane hunting a member that does not exist. Identify the base
  register from the prologue; never assume.
- ⚠ **There is no live positive.** `CharUpperTwist`'s permutation is the *shipped*
  one — it was fixed by restoring the permutation. So the selftest cannot be "find
  the known bug"; it must be sabotage-based: permute two adjacent fields in a
  known-good `Load`, confirm detection, restore, confirm clean.

Two of the five real ones hid under a **displayed 100.0%**, so treat "100% + a
Save/Load function" as inherently suspicious rather than as evidence.

## Lane B — Wrong struct field at 100% (class 1)

**Known:** 121 bugs — the second-largest class. **Tooling:** none that is safe.

A same-width, same-shape wrong field costs nothing: `RndFlare::Load` and
`CharUpperTwist::Load` both hid under a rendered 100.0. Compare the sequence of
`this`-relative load/store offsets between our object and the target listing for
functions at or near 100%, and report only where the *resolved field* differs.

- ⚠ Do **not** reuse `run_analyze_function`'s "Offset Mismatches (resolved)"
  block. It **ignores the base register** and renders a pure `(r1)` stack-slot
  diff as a wrong-field finding, indistinguishable from a true positive. That
  block is a lead, never a finding. Your tool must do better or it is not worth
  having.
- Start from the highest-confidence slice — functions reading 100% normalized
  with 0 mismatch rows — because that is precisely where the ruler offers no
  protection, and a finding there is a finding nothing else can produce.

## Lane C — `bss_initializer_scan` blind spots (class 10)

**Known:** 16 bugs. **Tooling:** exists, instrumented today, **examines 20.29%**
(3,295 of 16,238). The other 12,939 are counted as
`no-name-match-in-paired-target` but not reached.

Two structural blind spots, each with a **real bug that walked through it**:
- **Target-side `lbl_*`.** dtk names a function-local static `lbl_<addr>`, so
  there is no name to join on. `Game::Poll::sLastBeat` (`88c3d9c20c`) escaped
  here; `mutable_float_audit` found it as `lbl_82F1A524`. **Fix: join by address
  via the linker map**, not by name.
- **Cross-TU placement.** `GainEffect::sGain` (`df13adcd1c`) had a correctly
  *named* target symbol, but our definition sat in `Mic.cpp` and the pairing is
  per-object, so the two never met. **Fix: a whole-binary join**, not per-pair.

Ground truth for a non-vacuous selftest: both escapees are *fixed* in source now,
so they will not re-fire. Validate by sabotage, and additionally assert the
examined fraction **rises** — that number is the deliverable.

⚠ `12cff454d` declares this class closed citing *"the scan now returns zero hits
over 980 object pairs."* That is this instrument at 20% coverage. The class is
**not** closed.

## Lane D — `mutable_float_audit`: target side + unmangled statics (class 5)

**Known:** 54 bugs. **Tooling:** our-side walker fixed today (DISAGREE 4 → 0,
three of four rows proven artifacts). Two gaps remain, both measured:

- **The target parser has the same defect the our-side walker just had.**
  `data_float_labels.collect()` finds loads in the folded form
  (`lfs fN, lbl@l(rX)`) — 292 sites. But the target *also* materialises bases:
  **29** `addi`-materialised references reach `.data` float labels, and **6 float
  labels are reachable ONLY that way**, so the parser never sees them. Confirmed
  example: `lbl_82F16D28` is referenced from
  `?BlurSurface@RndSoftParticleBuffer@@AAAXXZ` via `addi r25, r11, lbl@l`.
  Port the instruction-walk logic from `our_float_statics` to the target side.
- **34 of our 114 distinct `.data` float statics are UNMANGLED** file-scope
  statics (`gSuperscriptScale`, `gRemoteGain`, `gNoiseThreshold`, …). The
  named-symbol join requires a mangled `@4MA`/`@3MA` shape and the target names
  these `lbl_<addr>`, so they are covered by **neither** join. This, not the
  addressing mode, is the larger remaining hole.

⚠ Preserve the `SAME VALUES, DIFFERENT LOAD ORDER` bucket and its meaning: our
MSVC and the image schedule loads of one static group differently, so an
*ordered* comparison manufactures disagreements. A wrong value cannot hide there
— entry requires equal multisets — and the sabotage control proves it
(`sSwipeEllipseWidth` 0.9 → 0.42 lands in DISAGREE, not order-only).

---

## Out of scope, deliberately

- **Class 2 (128 bugs, largest).** Genuinely hard to automate: the tell is branch
  polarity against the target, which is already a mismatch row when it costs
  points. Worth a design pass after these four land, not a speculative build now.
- **Class 9 (OOB write, 9 bugs).** Too few and too varied to generalise.
- The two pre-existing `honesty_lint` E1 errors. Not ours.
