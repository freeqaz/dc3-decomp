# Save/Load field-order desync — the wire bytes land on the wrong member

**Taxonomy class 7** of
[`docs/sessions/2026-09-15-two-month-native-impact-bug-review.md`](../../sessions/2026-09-15-two-month-native-impact-bug-review.md).
14 known bugs. Detector added 2026-09-16:
**`scripts/analysis/serializer_field_trace.py`**.

## The mechanism

`BEGIN_SAVES` / `BEGIN_LOADS` blocks are a positional wire format. The stream
carries no field names, so the Nth value written is the Nth value read, and a
block whose field order or field *set* disagrees with the shipped image silently
writes every saved object's data onto the wrong members.

Two worked instances, both fixed:

- **`StreamRenderer::Save`** (`6fa132015`) — the rev-12 chain wrote
  `mPlayer1/2/3DepthColor` a second time (offsets `0x60/0x70/0x80`) where the
  image writes `mPlayer4/5/6DepthColor` (`0x90/0xa0/0xb0`). Players 4-6 never
  reached the stream at all, and on `Load` players 1-3 were re-read over them.
  Same instruction count, same call sequence, same shape — three immediates.
- **`CharFeedback::Load`** (`e1a9425e5`) — the `rev < 6` arm read four bytes
  fewer than the image, so `mFailTriggerSecs`, `mMinFailSecs` and `mFailMat`
  were all read from the wrong stream offset for any file saved at rev 3-5.

Both hid from routine matching work, and **two of the five real ones hid under a
*displayed* 100.0%** — see
[rounded-100-hides-real-bugs.md](rounded-100-hides-real-bugs.md). The canonical
ruler forgives register permutation, and a displayed percentage rounds; so
"it's at 100%" is not evidence about field identity.

## The instrument

`serializer_field_trace.py` disassembles each `Save`/`Load`/`Copy` body from our
COFF object and the same-named body in the paired dtk-split target object, and
compares the **ordered sequence of `this`-relative offsets**:

| sequences | bucket | meaning |
|---|---|---|
| equal | `IDENTICAL` | the same fields in the same order |
| same multiset, different order | `FIELD_ORDER_DIFF` | a permutation |
| multiset differs | `FIELD_SET_DIFF` | a field missing, duplicated, or at a different offset |

It runs no objdiff, reads no `report.json` and no `decomp.db`. No ruler, cache
or pairing heuristic sits between the bytes and the verdict.

### Four decoder facts, each of which produced a wrong answer first

These are recorded because each one was measured on this tree, and three of them
produce a *confident wrong result* rather than an error.

1. **The `this` register varies, and r31 is often the frame pointer.** r31 holds
   the receiver in `RndFlare`/`CharUpperTwist`/`StreamRenderer`; **r30** holds it
   in `CharFeedback` and `RndText`, where r31 is `addi r31, r1, -0x140`. Keying
   on r31 rebuilds the documented `FxSendChorus::Load` false positive, where
   `run_analyze_function` resolved pure `(r1)` stack slots against the class
   struct and sent a lane hunting a member that does not exist. Derive the
   receiver from the prologue; never assume.

2. **`bl __savegprlr_N` is not a call.** MSVC's register save/restore helpers
   preserve every argument register, and they sit at the *second instruction* of
   most serializers — before the `mr r31, r3` that parks the receiver. Treating
   them as ABI calls clears r3, so the `mr` parks nothing and **the entire
   function traces empty**. `CharUpperTwist::Load` and `StreamRenderer::Save`
   both came back with zero references and a clean bill of health. An empty
   trace compares equal to an empty trace: this is quiet success, the exact
   failure shape `coverage.py` exists to prevent. CLAUDE.md records the same
   defect in the unicorn harness, where it overstated real bugs ~8x.

3. **Addressing mode is not field identity.** The image computes
   `subi r11, r30, 0xd0` and stores at `D(r11)`; our build stores at
   `(D-0xd0)(r30)`. Identical addresses. Recording the `addi` as a reference
   made `RndText::Load` read as a field-set difference (`-0xa4` ours vs `-0xd0`
   target) on a function whose *own* base-register-aware enrichment reports
   "27 offset mismatches examined, 27 excluded as non-field". The tool now
   propagates a derived pointer's bias and attributes the reference to the
   load, not the address computation.

4. **Materialisation order is not wire order.** MSVC hoists operand addresses
   above the calls that consume them, **right to left**. In `DepthBuffer3D::Load`
   the image emits `subi r4,r31,0x193` / `subi r29,r31,0x191` /
   `subi r28,r31,0x192`, then `bl >>` / `mr r4,r28` / `bl >>` / `mr r4,r29` /
   `bl >>`. The wire order is `-0x193, -0x192, -0x191` — exactly ours — but the
   materialisation order reads as a swap of two adjacent bools. Sorted by
   materialisation this scored as the *strong* evidence shape
   (`STRADDLES-A-CALL`). The tool now orders references by the instruction that
   **consumes** them. This one idiom accounted for **3 of 5** findings before
   the fix, including the only two that looked like real bugs.

### Measured and refuted: the callee-name axis

Comparing the ordered `bl` callee names — the obvious complement — does not work
on this binary and must not be added back. `CharUpperTwist::Save` calls
`??$?6VRndTransformable@@...` in our object and `??$?6VRndCamAnim@@...` in the
target: one ICF-folded template body under two names, on a function that is
byte-identical. Callee-name work belongs to class 6, which has seven tools.

## What this tool CANNOT see

A zero from it is **not** an exhaustion proof. Read the coverage block.

1. **A stream read whose destination is a stack local.** This is not a corner
   case — it is *half of the two worked examples*. `CharFeedback::Load`'s
   missing four bytes went into a throwaway `int x`; the read touches no member,
   so it leaves no `this`-relative footprint and no member-offset instrument of
   any kind can see it. **Manual recognizer below.**
2. **A TU we do not build.** Our build covers 989 of 2,223 target objects
   (44.5%).
3. **Bodies whose receiver cannot be followed safely.** Each is a counted drop
   with its own reason, not a silent skip.
4. **Two references with no call between them.** The compiler may schedule those
   either way, so such a transposition is rendered `WEAK` and is not evidence on
   its own.
5. **A field whose offset we also get wrong.** Then the two sides disagree for a
   class-1 reason rather than a class-7 one; the row is still reported, and
   adjudication is the reader's.

### The manual recognizer for blind spot 1

For each rev-gated arm of a `Load`, the question is *how many bytes does this
arm consume*, not *which members does it touch*. So read the arm in the target
listing and count its stream operations:

- every `bl ?ReadEndian@BinStream@@QAAXPAXH@Z` with `li r5, N` consumes N bytes;
- every `bl ??5...` / `operator>>` consumes whatever that type reads;
- an address argument of `addi r4, r1, <slot>` — an **r1-relative** destination —
  is a read into a *local*, i.e. a discarded value. Those are exactly the ones
  this tool cannot see.

`CharFeedback::Load` has four sibling `int x` discards at frame slots
`0x60/0x64/0x68/0x6c` and we were spelling only three. The tell was an
`addi r4, r31, 0x64` feeding a `bl ReadEndian` in an arm where our source had no
read at all.

## How it was validated

There is **no live positive to point the instrument at.** `CharUpperTwist`'s
permuted Save/Load is the *shipped* behaviour — it was fixed by *restoring* the
permutation, and the source says so — and both worked examples above are fixed.
A selftest of the form "it still finds the known bug" is therefore impossible,
and a selftest that cannot fail is not a selftest.

So the control is **sabotage, two-sided, at source level**. Re-inject the
historical bug (spell `CharUpperTwist::Load` in declaration order instead of the
image's `mTwist2, mUpperArm, mTwist1`), rebuild, scan; then restore, rebuild,
scan:

| run | findings | denominator | the injected row |
|---|---|---|---|
| clean | 1 | 947 | — |
| sabotaged | **2** | 947 | `?Load@CharUpperTwist@@` `FIELD_ORDER_DIFF`, `STRADDLES-A-CALL` |
| restored | 1 | 947 | gone; output byte-identical to the clean run |

The denominator does not move, so the extra row is a detection and not a
coverage change. `scripts/analysis/serializer_field_trace.py --selftest` adds 25
in-process checks, each paired with its own negative control — including the
addressing-mode equivalence, the hoisted-materialisation idiom, and
`bl __savegprlr_N` — and the tool is registered in
`scripts/analysis/determinism_check.py` (agrees with itself across two
`PYTHONHASHSEED` values on non-empty output).

⚠ Run any *copied* script from a freshly created empty directory. These scanners
do `sys.path.insert(0, dirname(__file__))`, so a copy run out of `/tmp` puts
`/tmp` first on `sys.path` and a stray `/tmp/dis.py` shadows the stdlib — the
control then crashes, which looks exactly like "found nothing". The control
above sabotages the **source** and rebuilds, running the script from the
worktree, so it does not touch that trap; never send a control's stderr to
`/dev/null` either way.

## Result on the current tree

Whole binary, 2026-09-16, worktree `det-a-saveload`:

- **universe 2,189** `Save`/`Load`/`Copy` bodies our objects define
- **examined 947** (43.26%) against a same-named target body
- dropped 1,242, every one counted: 849 adjustor thunks, 307 with no target body
  of that name, 36 receiver-register-reused, 25 with no `this`-relative access,
  21 receiver never parked in a non-volatile, 3 in unpaired objects, 1
  undecodable instruction
- **`FIELD_SET_DIFF` 0 · `FIELD_ORDER_DIFF` 1**, and that one
  (`HamMove::Copy`) is refuted: 97.8% canonical, 4 rows, a scheduler hoist of
  `lwz r29, -0xf8(r31)` between two byte copies that the source already
  documents as such.

**No live class-7 bug is present in the 947 bodies this tool can see.** That is
a statement about 43.26% of our serializers and 44.5% of the binary's objects,
and it says nothing at all about blind spot 1, which is where
`CharFeedback::Load` lived.
