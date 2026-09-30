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

---

## OUTCOMES (recorded 2026-09-16, all lanes landed)

All four lanes landed on `main` with `--no-ff`, plus a fifth that the work
uncovered. Every lane reproduced for me independently before merging:
coverage block, determinism across two `PYTHONHASHSEED` values on non-empty
output, `honesty_lint` delta 0, and a two-sided sabotage control.

| lane | tool | coverage (with denominator) | result | merge |
|---|---|---|---|---|
| A | `serializer_field_trace` (new) | 947 / 2,189 bodies = 43.26% | `FIELD_SET_DIFF` 0, `FIELD_ORDER_DIFF` 1 (known-WEAK `HamMove::Copy`) | `205baacc3` |
| B | `this_offset_scan` (new) | 20,432 / 30,832 bodies = 66.27% | 1 candidate, **refuted** | `9e49dd942` |
| C | `bss_initializer_scan` | 3,294 → **15,464** / 16,238 = 20.29% → **95.23%** | 2 findings, both fixed | `f13e6f6de` |
| D | `mutable_float_audit` | target sites 92 → **268**; labels 63 → 116 | 0 disagreements | `d4f9703f7` |
| E | `access_specifier_scan` (unplanned) | mangled-symbol examined 19.21% → **26.05%** | findings 6 → 7 | `a252a2f16` |

### The one real game bug

**`g_LineBreakTable`** (`1426dec31`) — 146 zeroed entries against the image's
145 populated ones. Both helpers binary-search it for `c == entry.ch`; with
every `ch` zero, nothing matches, `result = 0` falls through, and
`CantStartLine`/`CantEndLine` returned **false unconditionally**. Every call
site was dead logic: our build never suppressed a break before `!` `)` `,` `.`
`:` `;` `?` or CJK closing punctuation, and never held one after `(` `[` `{`.
Live in the native port. Values transcribed programmatically and round-trip
verified against the listing; `.data` payload byte-equal to the image's 580 B;
`WordWrap_CanBreakLineAt` unchanged at 97.6%; native gate 437/437, 0 failed.

The array stays `[146]` against the image's 145 deliberately — the image's own
search bound (`hi = 0x91`) provably probes index 145, i.e. it reads four bytes
past its own table. `[145]` would reproduce that OOB read for real.

### Three claims that did NOT survive adjudication

- **`gPollToken`** — value claim correct, *impact* claim wrong. The token is
  only ever compared against itself as a re-entrance sentinel and `mLoadCount`
  is constructed to 0, so 0→1 and 1→2 are equivalent. Landed as fidelity
  (`674683a0c`), labelled as such. **A scan hit is a value claim, never a
  behaviour claim.**
- ~~**`CharMirror::Poll`** (lane B's only candidate) — filed as a −4 wrong field;
  objdiff shows **seven** `this`-relative rows shifted by a *uniform* `+0x30`,
  which is a class-layout divergence, not a substituted field. The tool's own
  anchor fit was 2/10 there.~~

  > ⚠ **RETRACTED 2026-09-30 — this refutation was wrong, and it was wrong in
  > the exact way CLAUDE.md warns about.** The "seven rows shifted by `+0x30`"
  > came from objdiff's *Offset Mismatches (resolved)* block, which **ignores
  > the base register**. The target parks `this` in **r23** (`mr r23, r3`) and
  > later does `addi r29, r23, 0x30` — so the target's `r29` is `&mBones`, not
  > `this`, and every `+0x30` row is the same field reached through a different
  > anchor. Lane B's scanner, which does track the base register, had already
  > dismissed exactly those rows for exactly that reason. The class layout
  > **agrees**; only our header's `// 0x50` comment on `mBones` is stale.
  >
  > The divergence lane B flagged is real once decoded with the right anchor:
  > the image starts the position loop at `it = mStart` (`extsw r10, r11`),
  > we start at `mStart + mOffsets[TYPE_POS]` (`add`). It is
  > **behaviour-neutral** — `CharBones::RecomputeSizes()` writes
  > `mOffsets[0] = 0` before anything else — so it is a matching residue, not a
  > native bug. (A naive "drop the add" spelling regressed 98.6% → 98.3% by
  > changing the loop's rotation; not pursued.)
- **The linker-map address join** (lane C's prescribed fix) — recovers **zero**
  rows, measured: of the 8,893 names the name join misses, 0 appear in
  `ham_xbox_r.map`. Function-local statics carry a per-TU scope ordinal and are
  unjoinable by name on principle. What works is joining through the *code that
  reads the static*.

### Open follow-up

~~**`CharMirror` layout.** The `+0x30` shift covers rows *before* `mBones` (the
image reads `0x14` where we read `0x44` = `mMirrorServo+0x8`), so a pure
`mBones` relocation (`0x38` → `0x50` = `+0x18`) does not explain it. Our
headers put `mBones` at `0x50`; RB2 DWARF says `0x38`. Every field access in
the class is affected. Not started.~~

> ⚠ **CLOSED 2026-09-30 — there is no layout divergence.** See the retraction
> above: the `+0x30` is an anchor (`r29 = this + 0x30 = &mBones` on the target
> side), and the offsets agree. The "open follow-up" was built on the same
> base-register-blind reading.
>
> **The same adjudication pass turned up a real bug that this doc had filed as
> neutral.** Lane B also reported `HamCamShot::SetPreFrame` reading `+0x18`
> where we read `+0x14`, and read it as `*mNextShotIt` vs `mCurrentShot`. That
> reading was the neutral half. `ObjPtrList::Node` has `next` at `+0x14` and
> `prev` at `+0x18`: the image's camera-shot **rewind loop steps backwards**
> and ours stepped **forwards**, because `ObjPtrList::iterator` had no
> `operator--` and the right spelling did not compile. Fixed (97.34% → 100.0%),
> see `docs/decomp/patterns/missing-container-operator-forces-wrong-spelling.md`.

### Tooling defects found in already-landed code

- **`run_diff_inspect(mode="asm_listing")` was dead tree-wide** (`f8b54c9e0`).
  It appended `/FAs` to the *metadata patcher's* argv — since 2026-08-31 the
  compile edge is `<cl> && <obj_build_metadata_patcher.py>` — so `cl.exe` was
  never asked for a listing and the mode reported "no output" instead of the
  refusal underneath. ⚠ **Needs an MCP server restart to reach callers.**
- **`access_specifier_scan` was examining a denominator computed from its own
  parser's output** (lane E). `rfind("@@")` both *dropped* symbols (parameter
  types carry their own `@@`) and *fabricated* member functions out of static
  data members — and the docstring defended it. Neither `find` nor `rfind` is
  correct; it needed a tokeniser.

### A denominator lesson this session paid for twice

Lane C's old universe was "symbols in `.bss`" but its join could reach 20% of
them. Lane E's old universe was *literally the set its parser succeeded on*, so
64,682 parse failures could never appear in it. **A denominator computed from
the classifier's own output can only ever report success** — and fixing it makes
the headline fraction *fall* (E: 48.23% → 7.65% of the true denominator), which
is the honest direction.

⚠ **I committed the same defect myself.** The figures I put in lane E's brief
("48,836 distinct `?`-mangled symbols, 52.4% garbage") came from an ad-hoc probe
that globbed `[:400]` of 990 objects — a truncated sample stated as a total. The
lane could not reproduce it, said so, and refused to substitute its own numbers
quietly. Re-measured untruncated: **107,493** mangled symbols, rfind-valid
**42,811** (exactly the old universe), find-valid 61,048. Direction right,
figures wrong.
