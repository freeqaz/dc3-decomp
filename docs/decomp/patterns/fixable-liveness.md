# Fixable Patterns: Liveness and Scheduling (Register-Swap Levers)

> **This file is a correction, not just an addition.** The pre-existing guidance
> pointed at [Variable Declaration Order](fixable-declarations.md#variable-declaration-order)
> as the primary handle for register-swap (`REGISTER_SWAP` / REGSWAP) diffs. On the
> three functions measured here that lever was **inert**: 12+ hand variants produced
> *byte-identical* `.obj` output, and a beam-search permuter sweep returned zero
> improvements. What actually moved the registers was changing **liveness** (what is
> live across a call) and **scheduling** (where a value is materialized relative to
> its consumer).
>
> **Measured:** 2026-08-02/03. All percentages via `run_objdiff` with `project_dir`
> pointed at the worktree.

---

## The Rule

**In all three functions, the register swaps were symptoms, not causes.** No register
name was ever permuted. Every swap in a function flipped *at once* when the underlying
cause was fixed — the register assignment is downstream of the interference graph, and
the interference graph is downstream of live ranges and schedule.

The boundary that matters, and the core contribution of this page:

| You change… | It moves… | Diff signal it addresses |
|-------------|-----------|--------------------------|
| **Scoping / packing** — which block a declaration lives in, braces, named-vs-temp | **Stack slots**: frame size, slot order, packing | `OFFSET_SWAP`, `[off:-N]` shifts on `r1`, `mode=stack-layout` SHIFTED/SWAPPED rows |
| **Liveness / scheduling** — what is carried across a call, where a value is computed | **Registers**: which callee-saved register holds what, how many are saved | `REGISTER_SWAP` clusters, `__savegprlr_NN` delta |

If your residual is register swaps, declaration *order* is the wrong axis of the two —
reach for Levers 1-3. If your residual is offset shifts on stack locals, declaration
*scope* is the right axis — that's Lever 4, which is a stack lever that happens to live
on this page because it was found in the same function.

### Scope and honesty

This is **n = 3** functions (one taken to 100%, two to 98-99%), all in
`src/system/obj/` and `src/system/rndobj/`. The per-function results below are measured.
The *rule* above — that scoping moves stack and liveness moves registers — is a
generalization from three data points plus the c2.dll allocator mechanism already
documented in [unfixable-compiler.md](unfixable-compiler.md#register-allocation);
treat it as a strong working hypothesis, not an established conversion rate. No win
rate is claimed. Do not cite this page as "liveness edits fix REGSWAP N% of the time."

**Update 2026-08-04.** The *symptom* claim — not the levers — has since been tested much
harder: a seven-lane sweep of the AT_LIMIT + `REGISTER_SWAP` bucket triaged 31 functions
and register swaps were symptoms in **100% of cases, without exception**, including two
functions that reached byte-exact 100% with no register-motivated edit at all. What that
sweep also showed is that the *cause* is frequently not liveness either — it is just as
often control flow, an inline-level count, or a signed/unsigned compare. So read this page
as "do not chase the register", not as "the cause is always liveness". The routing rule
that came out of it is the [Triage Split](#triage-split-statement-level-vs-within-one-expression)
below. Session log:
[2026-08-04-regswap-atlimit-sweep.md](../../sessions/2026-08-04-regswap-atlimit-sweep.md).

---

## Lever 1 — Live-Range Shortening: Read the Args Back Out of the Aggregate You Just Built

**Impact:** +0.6% (99.4% → **100%**)
**Success Rate:** unknown (1 for 1)
**Time:** 5 minutes once diagnosed

### Symptom

A loop body calls a function while several callee-saved values are live. The diff is a
pure register **rotation** across the whole function with **no instruction-stream
change** — same opcodes, same order, same count, only operand registers differ.

`ObjectDir::Iterate` (`src/system/obj/Dir.cpp`) at 99.4%: a 4-cycle rotation over
`{var, b, arr, s2}`.

```
target: r25=var  r26=b   r27=arr  r29=s2
base:   r25=s2   r26=var r27=b    r29=arr
```

17 swapped registers, 153/153 instructions otherwise equal, both sizes 612 bytes.
A rotation like this (not a 2-cycle swap) is the tell that the *set* of simultaneously
live values differs, not just their colors.

### Why It Works

`key` is built from `first` and `s2` on the line before the call, and MSVC hoists that
`std::make_pair` store into the loop preheader. Reading the call's arguments out of
`key` rather than out of the locals ends `s2`'s live range **at the pair store** instead
of carrying `s2` across the `IsASubclass` call inside the loop. One fewer value live
across the call = a different interference graph = the target's color→register mapping.

The change is a provable no-op: `key = std::make_pair(first, s2)` is constructed on the
preceding line and neither component is modified in between.

### Fix

```cpp
Symbol first;
for (ObjDirItr<Hmx::Object> it(this, b); it != nullptr; ++it) {
    bool bbb;
    first = it->ClassName();
    std::pair<Symbol, Symbol> key = std::make_pair(first, s2);
    std::map<std::pair<Symbol, Symbol>, bool>::iterator superclassIt =
        sSuperClassMap.find(key);
    if (superclassIt == sSuperClassMap.end()) {
        // BEFORE (99.4%) — carries s2 across the call
        bbb = IsASubclass(first, s2);

        // AFTER (100%) — s2's live range ends at the pair store
        bbb = IsASubclass(key.first, key.second);

        sSuperClassMap[key] = bbb;
    } else
        bbb = superclassIt->second;
    // ...
}
```

Commit `2a1b14b6`.

### Detection

- `run_diff_inspect mode=regswaps` shows a rotation of length ≥ 3 (not a 2-cycle).
- `run_diff_inspect mode=clusters` shows the swap spanning the whole function with zero
  insert/delete clusters — instruction counts and sizes identical.
- A value that is *both* consumed by a pre-call expression *and* passed to the call is
  the candidate: it is redundantly live if the pre-call expression already stored it
  somewhere addressable.

### Generalization (conjecture)

Any place we spell an argument as the original local while the target spells it as a
projection of an already-materialized aggregate (`pair`, small struct, a member the code
just wrote) is the same move. Related: [Local Pointer Reload to Break Member-Address
Reuse](fixable-declarations.md#local-pointer-reload-to-break-member-address-reuse) does
the same thing from the other direction — it *shortens* a live range by forcing a reload
rather than by re-projecting an aggregate.

---

## Lever 2 — Call Through the Cached Local; Don't Re-Load the Member at the Call Site

**Impact:** +4.0% (92.7% → 96.7%)
**Success Rate:** unknown (1 for 1)
**Time:** 5 minutes

### Symptom

The function already has a local caching a member pointer, but a later call site spells
the member path again (`mStyles[0].mFont->CharAdvance(...)` instead of
`font->CharAdvance(...)`). The re-load costs a whole callee-saved register — the
prologue helper differs:

```
target: bl __savegprlr_22
base:   bl __savegprlr_23      <-- we burn one more callee-saved GPR
```

…and that single extra register cascades into ~40 register swaps across three pairs
(r27↔r28, r22↔r23, f12↔f13) in `RndText::FitTextScroll`
(`src/system/rndobj/Text.cpp`).

### Why It Works

The target keeps `mStyles[0].mFont` in **one** callee-saved register across the
intervening `DecodeUTF8` call and dispatches through it. Our re-load forces the compiler
to keep the *base* (`mStyles` / `this`) alive across the call **as well as** whatever it
needed the local for, so the allocator has one more simultaneously-live value and takes
one more callee-saved register. The `__savegprlr_NN` delta is the cheapest possible
confirmation that this is a live-set problem and not a coloring problem.

This is the mirror image of
[Pre-Compute References Before Clobbering Calls](fixable-declarations.md#pre-compute-references-before-clobbering-calls):
that pattern says *create* the local before the call; this one says *use* it after the
call. Creating the local and then not calling through it is the worst of both worlds —
you pay for the local's live range and for the reload.

### Fix

```cpp
RndFontBase *font = mStyles[0].mFont;
MILO_ASSERT(font, 2718);

// BEFORE (92.7%) — dead initializers + member re-load at the call site
unsigned short charCode = 0;
DecodeUTF8(charCode, "8");
float w = 0.0f;
mStyles[0].mFont->CharAdvance(charCode, charCode, w);

// AFTER (96.7%) — call through the local; no initializers
unsigned short charCode;
DecodeUTF8(charCode, "8");
float w;
font->CharAdvance(charCode, charCode, w);

scrollCharWidth = (mStyles[0].mKerning + w) * mStyles[0].mSize;
```

Commit `97455510`.

### Sub-lever: Drop `= 0` / `= 0.0f` on Pure Out-Params

Both `charCode` and `w` are pure out-params — `DecodeUTF8` and `CharAdvance` write them
before any read. The target emits **no** init store for either. An initializer on a pure
out-param adds a store the target doesn't have and gives the value an artificially early
live-range start, which can pull it into a different color.

Check the callee's contract before doing this: it must write unconditionally on every
path the caller can reach. If the callee writes conditionally, the initializer is load-
bearing and removing it is a real bug, not a match win. (See
[harmful-avoid.md: Constructor Zero-Init That Doesn't Exist in Target](harmful-avoid.md#constructor-zero-init-that-doesnt-exist-in-target)
for the constructor-side version of the same trade.)

### Detection

1. `__savegprlr_NN` / `__restgprlr_NN` differ by one or two between target and base →
   the live *set* differs; you are looking for a value you keep alive that the target
   doesn't (or vice versa).
2. Grep the function for a member path that is spelled out at a call site while a local
   already caches it.
3. Grep for locals initialized at declaration whose first real use is as an out-param.

---

## Lever 3 — Fix the Schedule First, Then the Comparison Polarity

**Impact:** +2.6% (96.5% → 99.1%)
**Success Rate:** unknown (1 for 1)
**Time:** 15 minutes

### Symptom

Float register swaps (f30↔f31, f12↔f13) around a compare. It is tempting to read this as
"FPR coloring, unfixable" — the actual cause was that we computed a product *inside* a
later `if` condition while the target computed it earlier, so the multiply landed in a
different scheduling slot and the compare consumed a different register.

`RndText::SizeCheck` (`src/system/rndobj/Text.cpp`), 96.5%, nine f30↔f31 / f12↔f13
swaps.

Target:

```
fmuls  f12, f30, f1        ; product computed BEFORE the compare that consumes it
...
fcmpu  cr6, f13, f0
bge    ...
```

Base:

```
...
fcmpu  cr6, f0, f12        ; operands reversed; product materialized inside the cond
ble    ...
```

### Why It Works

Two independent things, applied in this order:

1. **Scheduling.** Collapsing `font->FontUnit()` and `font->AspectRatio()` into a single
   `float fontSize = font->FontUnit() * font->AspectRatio();` puts the `fmuls` at the
   statement position the target has it in, ahead of the `fcmpu` that consumes it.
   Do this first — it is what moves the FPRs.
2. **Polarity.** Then flip the two float compares to the target's operand order. These
   are exact logical equivalences, **including NaN behavior**: `a <= b` and `b >= a`
   both evaluate the same way for every operand pair including NaN, because both are
   `false` when either operand is NaN. (`a <= b` → `!(a > b)` would *not* be equivalent
   — do not do that.)

With the schedule fixed, all nine register swaps fell out automatically; the polarity
flips only rewrote the `fcmpu` operand order and branch mnemonic.

### Fix

```cpp
// BEFORE (96.5%)
float fontUnit = font->FontUnit();
float aspectRatio = font->AspectRatio();
float cap = 127.5f;
if (screenHeight < 127.5f) cap = screenHeight;
if (cap <= fontUnit * aspectRatio * 1.25f) return;
if (sLastText == this && screenHeight <= sLastHeight) return;
int productInt = (int)(fontUnit * aspectRatio);

// AFTER (99.1%)
float fontSize = font->FontUnit() * font->AspectRatio();
float cap = 127.5f;
if (screenHeight < 127.5f) cap = screenHeight;
if (fontSize * 1.25f >= cap) return;
if (sLastText == this && sLastHeight >= screenHeight) return;
int productInt = (int)fontSize;
```

Commit `0c2b0c38`.

### Detection

- FPR swaps clustered *around* an `fcmpu`, with the arithmetic that feeds the compare
  appearing at a different index in target vs base.
- `run_diff_inspect mode=mismatches` with `full_listing` — look at whether the producing
  `fmuls`/`fadds` is at the same instruction index. If it is not, the swap is a schedule
  artifact and the compare-operand order is a second, separate fix.
- Ordering matters: flipping the compare **before** fixing the schedule just moves the
  swap to the other side of the compare and looks like a regression-neutral wash.

See also [fixable-operators.md: Comparison Operand Order](fixable-operators.md#comparison-operand-order)
and [fixable-control-flow.md: Branch Polarity Steering](fixable-control-flow.md#branch-polarity-steering-beqbne-blebge)
for the polarity half in isolation.

---

## Lever 4 — Scope a Declaration Into the Block That Uses It (stack lever, not a register lever)

**Impact:** +1.5% (96.7% → 98.2%); killed 14 offset diffs at once
**Success Rate:** unknown (1 for 1)
**Time:** 5 minutes

This one is on this page for contrast: it is a **scoping** change and it moved **stack
slots**, exactly as the rule at the top predicts. It did not move any register.

### Symptom

`mode=stack-layout` shows a run of SHIFTED slots — a group of locals all displaced by
the same delta (here +8) relative to target — with no register differences in the same
region. In `RndText::FitTextScroll` this was 14 offset diffs: `w` landed above `bounds`
and pushed `wideChars` / `lines` up by 8, where the target packs `w` into `0x54`
directly next to `charCode`.

### Why It Works

MSVC assigns stack homes per lexical scope and can pack same-scope locals together.
Declaring `charCode` and `w` inside the `if (font) { ... }` block that uses them puts
them in the same inner scope, so they pack adjacently instead of each claiming a slot in
the function's outer frame region. The block itself is also load-bearing here: the
target branches past the whole measurement block when `mStyles[0].mFont` is null (the
assert-fail path ends in a `b` to the join point rather than falling through), so
`if (font)` is a faithful rendering of the target's control flow, not a match hack.

### Fix

```cpp
RndFontBase *font = mStyles[0].mFont;
MILO_ASSERT(font, 2718);

// BEFORE (96.7%) — outer-scope declarations, w lands above bounds
unsigned short charCode;
DecodeUTF8(charCode, "8");
float w;
font->CharAdvance(charCode, charCode, w);
scrollCharWidth = (mStyles[0].mKerning + w) * mStyles[0].mSize;

// AFTER (98.2%) — scoped into the using block; w packs into 0x54 next to charCode
if (font) {
    unsigned short charCode;
    float w;
    DecodeUTF8(charCode, "8");
    font->CharAdvance(charCode, charCode, w);
    scrollCharWidth = (mStyles[0].mKerning + w) * mStyles[0].mSize;
}
```

Commit `4b29b16b`.

### Detection

`mode=stack-layout` SHIFTED rows with a uniform delta, and the shifted set is
contiguous. Confirm the residual is offsets and not registers before reaching for this —
scoping will not move a register swap.

---

## Lever 5 — Name the Temporaries So They Are Built Up Front and Frame-Packed

**Impact:** +19.4% (80.4% → **99.9%**) — 68.1% for the honest starting point
**Success Rate:** unknown (1 for 1)
**Time:** 30 minutes

This is the other half of Lever 4. Lever 4 *narrows* a scope to make locals pack.
This one *widens* the live range of unnamed temporaries — by giving them names — so
the frame packer gets to see them at all.

### Symptom

A run of calls each taking a by-const-ref aggregate built at the call site:

```cpp
m_pTopLeftBone->SetLocalPos(Vector3(minX, 0.0f, maxZ));
m_pTopRightBone->SetLocalPos(Vector3(maxX, 0.0f, maxZ));
// ...
```

The frame comes out **too small** — `stwu r1, -0xb0` against the target's `-0xc0` —
and essentially every FPR and GPR downstream of the first call is permuted. In
`LabelShrinkWrapper::UpdateAndDrawWrapper` (`src/system/ui/LabelShrinkWrapper.cpp`)
that was 68.1%, with 21 inserts and 21 deletes.

Count the target's slots before doing anything else. Read every `stfs`/`stw` with an
`r1` base out of `full_listing=true` and group them by 16-byte slot. Here the target
wrote `0x60,0x64,0x68`, `0x70,0x74,0x78`, `0x60,0x64,0x68` again, and `0x80,0x84,0x88`
— **four values in three slots**, all four materialized in the first basic block,
before the first call.

### Why It Works

An unnamed temporary passed by const-ref dies at the end of the full expression, so
each one dies at its own call and the next reuses the same slot: N temporaries, one
slot. Naming them extends each live range to the end of the enclosing block, so all N
are in the frame at once — and *then* MSVC's frame packer coalesces the pairs whose
live ranges still do not overlap. Here `topLeft` dies as soon as its 16 bytes have
been copied into the bone, which happens before `bottomLeft` is stored, so those two
share `0x60` while `topRight` and `bottomRight` (both live across intervening calls)
keep their own. Four names, three slots — exactly the target.

Getting the slot *count* right fixed everything downstream for free: the FPR
assignment (`f31=minX f30=minZ f29=maxX f28=maxZ`, with `f0` rather than a recycled
`f31` holding the zero constant) and the instruction schedule both fell out with no
further edits.

### Fix

```cpp
SetWorldXfm(label->WorldXfm());

// BEFORE (68.1%) — four temps, one slot, frame 0x10 short
m_pTopLeftBone->SetLocalPos(Vector3(minX, 0.0f, maxZ));
m_pTopRightBone->SetLocalPos(Vector3(maxX, 0.0f, maxZ));
m_pBottomLeftBone->SetLocalPos(Vector3(minX, 0.0f, minZ));
m_pBottomRightBone->SetLocalPos(Vector3(maxX, 0.0f, minZ));

// AFTER (99.9%) — four names, three slots, 0x60 reused by the packer
Vector3 topLeft(minX, 0.0f, maxZ);
Vector3 topRight(maxX, 0.0f, maxZ);
Vector3 bottomLeft(minX, 0.0f, minZ);
Vector3 bottomRight(maxX, 0.0f, minZ);
m_pTopLeftBone->SetLocalPos(topLeft);
m_pTopRightBone->SetLocalPos(topRight);
m_pBottomLeftBone->SetLocalPos(bottomLeft);
m_pBottomRightBone->SetLocalPos(bottomRight);
```

Commits `3bb49c53`, `50d75d6d`; merged as `b79fecaf`.

### Detection

- Frame-size delta on `stwu r1, -N` that is an exact multiple of the aggregate size,
  with the *base* frame smaller than the target's.
- `mode=stack-layout` reporting TGT_ONLY rows for a whole 16-byte group.
- Source-side tell: an aggregate constructed inside a call argument list.

Do not read the resulting register swaps as the problem. Every one of them was a
symptom of the missing slot.

### Do Not Half-Fix It

Two intermediate shapes were measured and both are traps — they buy real points and
then stall, which reads like a floor:

- Naming only the corners that need their own slot (two named + two temps): **90.6%**.
  Right slot *count*, wrong assignment — the named `bottomRight` claims `0x60` and the
  left column is pushed to `0x70`.
- Three names plus a mid-function `left.Set(minX, 0.0f, minZ)` to recycle one:
  **86.2%**. Correct slots *and* frame, but the `Set()` statement sits after the
  second call, so `bottomLeft` is materialized in the third basic block instead of the
  first. Fewer names is not closer.

The rule: match the target's **number of live values**, and let the packer choose the
sharing. Do not hand-recycle a slot.

### Match-Hack Smell

The 80.4% state this replaced used an `auto _tmp0 = Vector3(...)` hoisted above the
other three calls so that `SetLocalPos` on the top-left bone could run *last*, against
the target's call order. That is the shape of a hack that found half of this lever by
accident: naming one temp bought a second slot. If you find a lone named temp whose
only purpose is to reorder calls, the real fix is usually to name **all** of them and
restore the natural order.

---

## Negative Results — Do Not Re-Run These

These cost real hours. They are recorded so the next person does not repeat them.

| Function | Lever tried | Variants | Result |
|----------|-------------|---------:|--------|
| `ObjectDir::Iterate` | Declaration reorder / scope moves of `{var, b, arr, s2, first, key}` | 6 | **Byte-identical `.obj`** — not "no improvement", literally the same bytes |
| `ObjectDir::Iterate` | Declaration reorder, 2 further variants | 2 | Regressed 99.4% → ~95.8% |
| `ObjectDir::Iterate` | Beam-search permuter sweep (decomp-synth) | 65 candidates | **0 improvements** |
| `RndText::FitTextScroll` | Declaration reorder (before Lever 2 was found) | several | No movement |
| `LabelShrinkWrapper::UpdateAndDrawWrapper` | Commutative operand order on a 2-term `fadds` — source operand swap, statement split with `+=`, hoisting either addend into a named local, flat-sum term reorder, explicit grouping | 6 | **Byte-identical `.obj`** every time |
| `LabelShrinkWrapper::UpdateAndDrawWrapper` | Beam-search permuter sweep (decomp-synth), chain-depth 4: `fma_reorder`, `declaration_reorder`, `reference_elimination`, `float_double_literal` | 56 | **0 improvements** over 99.86% |

The `Iterate` result is the sharpest: the compiler's coloring is **deterministic given
the interference graph**, and none of those six edits changed the interference graph, so
the output could not change. That is consistent with the c2.dll mechanism documented in
[unfixable-compiler.md: Register Allocation](unfixable-compiler.md#register-allocation)
(colors are constraint-determined; declaration order only permutes the color→register
mapping *when the constraints leave slack*). When the constraints leave no slack,
declaration order is provably inert — reordering is not a lottery ticket, it is a no-op.

**Read the negative results as a routing rule:** a *byte-identical* result from a
declaration reorder is not "try more reorders." It is positive evidence that you are on
the wrong axis and must change the live set or the schedule.

---

## Triage Split: Statement-Level vs Within-One-Expression

**The single most actionable rule to come out of the 2026-08-04 AT_LIMIT sweep.** It
decides *which functions to open*, and it is what separates the wins from the wasted
build cycles. Register swaps do not classify a function — this does.

| Residual implicates… | Verdict | Tells |
|---|---|---|
| **A statement** — control flow, which field is read, which call is made, what stays live across a call, the shape of an *explicitly nested* expression | **Investigate.** Every win came from here. | insert/delete clusters, Function Call Diff rows, `addi`/`lwz` field-offset diffs, `__savegprlr_NN` deltas, branch polarity, signed-vs-unsigned compares, bool materialization |
| **One arithmetic expression** — commutative operand order, flat-sum term order, which of two independent loads issues first | **Floor. Skip.** | a lone `fadds`/`fmuls`/`add` operand swap with no surrounding structural difference |

MSVC canonicalises a flat `a + b + c` to `A + (C + B)` however you write it, so the flat
case is not a source-visible degree of freedom at all. See the negative-result rows above
for `LabelShrinkWrapper::UpdateAndDrawWrapper`, where six spellings of a 2-term `fadds`
were byte-identical.

### Exception — explicit parenthesization is NOT inert

A nested `a + (b + (c + …))` chain **preserves its shape**, and its term order is
**recoverable from the assembly**: target and base share a schedule, so the difference is
a fixed permutation. Read the mapping off the base — which source term produced which
emission slot — and invert it.

That distinction is the whole difference between the sweep's one expression-level win and
its three expression-level failures: `RndUtl EstimateDraw`, a 12-term *nested* sum, went
**99.6 → 100** by recovering the term order this way; the flat/commutative cases did not
move at all.

Before spending builds on it, confirm the nesting is real. Lane F tested the exception on
`RndMesh::SetVolume`'s dot product and both groupings produced a byte-identical residual —
MSVC had flattened it, so the exception did not apply.

### A third bucket: stack-slot allocation (looks statement-level, is not)

`RndText::Load` cost two cycles by fitting neither half of the binary rule. It *looked*
statement-level — 14 inserts / 15 deletes — but the clusters contained the **same
instructions placed differently**; the real cause was MSVC reusing stack slots across
disjoint nested scopes where the target does not (frame Δ −0x10, 6 target-only slots, 19
DIFFER).

**Added rule: if the insert and delete clusters contain the same instructions and
`mode=stack-layout` shows many DIFFER / PERMUTED slots, it is slot allocation — drop it.**
`HamDirector::OnPopulateMoves` is the same story: its residual `diff_arg` rows are almost
entirely stack offsets shifted by a `0xe0` frame delta, because the target gives each of
three `FileMerger::Merger` locals its own `0x70` slot while MSVC packs ours onto one.
Renaming them apart changed nothing — the packing is lifetime-based.

### How well the rule performs

It is a **filter for what to open, not a predictor of what will close.**

- **It never misfired in the costly direction.** Every expression-level residual that was
  dropped stayed a floor under test.
- **Statement-level does not reliably convert.** At ≥98% match it is necessary but not
  sufficient — `WorldInstance::SyncDir` and `HamNavList::Poll` both presented clean
  statement-level tells and both resisted. One lane went 2-for-5 with three *correct*
  diagnoses that measured worse.
- **Budget** a blind sweep of the AT_LIMIT + `REGISTER_SWAP` bucket at **~1 win per 3
  functions** (the measured blind rate — see
  [INDEX.md: AT_LIMIT Breakdown](INDEX.md#at_limit-breakdown)). Scoped to the
  statement-level half the seven-lane sweep did far better than that, ~1 per 1.3, but
  that number is post-filter and should not be used to budget an unfiltered pass.

### Two signals that are routinely misread as floor evidence

- **`PROLOGUE_MISMATCH` is the fingerprint of a value held across a call.** It is a
  *liveness* tell — Lever 2 territory — **not** floor evidence. (It is also unusable from
  `decomp.db`: `has_prologue_mismatch` is identically 0 for every row, the detector never
  populated it.)
- **A funclet score wobble is the parent's frame size, not noise, and must not veto a
  parent fix.** See
  [objdiff LEARNINGS: Pattern 6](../../tools/objdiff/LEARNINGS.md#pattern-6-eh-funclet-score-wobble).
  Usefully, the converse is a free control: an edit that leaves the paired funclet at
  exactly 100.0 did not move the frame, i.e. it was a pure liveness/scheduling edit.

---

## Diagnostic Order for a Register-Swap Residual

### First: which register class swapped?

objdiff's `REGISTER_SWAP` hint labels the class for you, and the class selects the
lever:

| objdiff hint | Registers | Lever |
|--------------|-----------|-------|
| `[callee-saved — check liveness across calls]` | r14-r31, f14-f31 | **Liveness** — Levers 1-2. A value is live across a call in one build and not the other. |
| `[volatile — scheduling/operand order]` | r0, r3-r12, f0-f13 | **Scheduling** — Lever 3. Volatile registers don't survive calls, so a swap between them is about *when* a value is computed and in what operand order, not what is kept alive. |

This mapping is consistent with all three functions but is n=3 plus first principles
(volatile registers cannot be live across a call, so a volatile swap cannot be a
live-across-call problem). `RndText::FitTextScroll` showed both at once — callee-saved
r27↔r28 / r22↔r23 *and* volatile f12↔f13 — because one extra callee-saved register
displaced the whole allocation; fixing the liveness cause cleared both classes.

### Then

1. **Instruction counts and sizes.** If they are equal and every mismatch is `diff_arg`
   on a register operand, the logic is right and you are purely in allocation territory.
2. **`__savegprlr_NN` / `__restgprlr_NN` delta.** A difference of one or two callee-saved
   registers means the *live set* differs → Lever 2 (something is alive across a call
   that shouldn't be, usually a re-loaded member).
3. **Swap cycle length** (`mode=regswaps`). A 2-cycle is a coloring flip; a 3+ rotation
   means the live set or live ranges differ → Lever 1.
4. **Is a producer at a different index than the target's?** (`mode=mismatches` with
   `full_listing`.) If the arithmetic feeding a compare or call moved, that's a schedule
   problem → Lever 3. Fix the schedule before touching polarity.
5. **Only then** consider declaration order, and stop after the first byte-identical
   result — it means the constraints have no slack.
6. **Offsets, not registers?** Different problem: go to Lever 4 /
   [Offset Swap](fixable-declarations.md#offset-swap).

This order matches objdiff-cli's verdict text, which now suggests **find the cause →
permuter → declaration reorder**, with reorder marked *"usually inert for register-only
swaps; reach for it when stack-slot / OFFSET_SWAP diffs are also present."* Tool output
and these docs should say the same thing; if they drift, fix both.

---

## Floor Evidence: the Three-Part Standard

The floor-evidence standard that held up across these three functions, strengthening the
conditions in [unfixable-compiler.md: When To Truly Accept](unfixable-compiler.md#when-to-truly-accept):

A residual is a floor only if **all** of:

- **(a) Hand variants return byte-identical output.** Not "no improvement" — identical
  bytes. A variant that changes the output but doesn't improve it means the axis is live
  and you haven't found the right point on it.
- **(b) A permuter sweep returns byte-identical / zero-improvement output.** Record the
  date, config, and candidate count (e.g. `Iterate`: 65-candidate beam search, 0 wins).
- **(c) Ghidra-decompile the *target* and show the construct is inexpressible.** ← **new,
  and the one worth promoting.**

### Why (c) matters

(a) and (b) only ever prove "I ran out of ideas." (c) converts that into "I proved this
is unreachable from C++."

Worked example — `RndText::FitTextScroll`'s residual 8 of 232 instructions. Decompiling
the *target* function in Ghidra (not ours) transcribed the residual as a **dead
conditional spill**: the styles-begin *pointer* being written into `float w`'s stack
slot on one path. No C++ statement can express "store an unrelated pointer into a float
local's home on one branch and never read it" — that is the register allocator splitting
a live range and reusing a dead slot as scratch. Once you can *name* the construct as an
allocator artifact rather than a missing source construct, the floor claim is defensible
and the function can be closed.

Practically: run the `ghidra-decompile` skill against the **target** symbol, then read
the residual instructions in the context of the target's own decompilation. You are
looking for one of:

- a spill/reload of a value with no source-level identity (allocator scratch),
- a slot reused for two unrelated values (live-range splitting),
- a store with no corresponding read on any path (dead conditional spill).

Any of those three names an allocator artifact. Anything else — a real computation, an
extra call, a different constant — means there **is** a missing source construct and the
function is not at a floor.

Do **not** claim a floor on (a) alone; see the "regalloc floor" false-certification
anti-pattern in [behavioral-divergence.md](behavioral-divergence.md).

---

## See Also

- [fixable-declarations.md: Variable Declaration Order](fixable-declarations.md#variable-declaration-order) — the corrected lever; still valid for stack/scope effects
- [fixable-declarations.md: Local Pointer Reload to Break Member-Address Reuse](fixable-declarations.md#local-pointer-reload-to-break-member-address-reuse) — sibling live-range-shortening pattern
- [fixable-declarations.md: Pre-Compute References Before Clobbering Calls](fixable-declarations.md#pre-compute-references-before-clobbering-calls) — the "create the local" half of Lever 2
- [fixable-declarations.md: Offset Swap](fixable-declarations.md#offset-swap) — stack-side residuals (Lever 4 territory)
- [fixable-operators.md: Comparison Operand Order](fixable-operators.md#comparison-operand-order) — the polarity half of Lever 3
- [unfixable-compiler.md: Register Allocation](unfixable-compiler.md#register-allocation) — c2.dll coloring mechanism; why byte-identical reorders are expected
- [unfixable-compiler.md: When To Truly Accept](unfixable-compiler.md#when-to-truly-accept) — floor conditions this page strengthens
- [PERMUTER_ROI_ANALYSIS.md](PERMUTER_ROI_ANALYSIS.md) — `declaration_reorder` ROI, corrected for REGISTER_SWAP
- [fixable-inline-boundary.md: Inline-Level Counting](fixable-inline-boundary.md#inline-level-counting-via-the-parameter-home-area) — the parameter-home-area lever; the most common non-liveness cause behind a swap cascade
- [../../tools/objdiff/LEARNINGS.md: Pattern 6](../../tools/objdiff/LEARNINGS.md#pattern-6-eh-funclet-score-wobble) — funclet wobble = parent frame size; the free control experiment for "did my edit move the frame?"
- [../../sessions/2026-08-04-regswap-atlimit-sweep.md](../../sessions/2026-08-04-regswap-atlimit-sweep.md) — the seven-lane sweep that calibrated the triage split (and found 11 live bugs)
