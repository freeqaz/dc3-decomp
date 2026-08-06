# Fixable Patterns: Inline Boundary

Patterns where moving a function definition between an inline location (header)
and an out-of-line location (.cpp) changes downstream codegen — sometimes by
eliminating a `bl` call (when MSVC inlines), sometimes by *adding* one (when
the function is no longer visible to inlining), and sometimes by triggering
an ICF merge that influences register allocation in unrelated callers.

These patterns came out of the 2026-05-12 upstream merge gap-recovery pass,
where many functions matched at 100% in the upstream tree but not in ours
despite identical-looking source. The differences turned out to be the
inline boundary — not the function body itself.

---

## Inline Constructor in Header vs Out-of-Line in .cpp

**Impact:** +5-10% (typically a sort-class ctor at ~91% → 100%)
**Success Rate:** 100% when applied to ctors with no body
**Time:** 2 minutes

### Symptom

A bodyless or near-bodyless constructor (often a sort/comparator class deriving
from a base) is at 91-95% match. The mismatch is in the static-init guard
prologue — the guard variable mangling shows `?$S2` where target uses `??_B`
(or vice versa), or the guard bit number differs by 1.

This is the same symptom family as
[Function Definition Order ($S#)](fixable-declarations.md#function-definition-order-tu-wide-static-guard-counters)
but the trigger is different: it's where the ctor lives, not where its
declaration appears in the file.

### Why It Works

When a constructor is defined out-of-line in a `.cpp`, MSVC emits its own
TU-static initialization guard at the call site. When defined inline in the
header, the call site instead inlines the body directly — no guard is
generated for the construction itself, and the surrounding guard counter
slot is freed up for the next runtime-initialized static.

For ctors that are conceptually "always inline" (like sort comparator
classes whose only state is a base-class member init), upstream typically
defines them inline. Out-of-lining them in our tree shifts the guard slot
allocation across the entire translation unit.

### Fix

```cpp
// BEFORE — out-of-line in .cpp
// SongSortByDiff.h
class SongSortByDiff : public SongSort {
public:
    SongSortByDiff();
    ~SongSortByDiff();
};

// SongSortByDiff.cpp
SongSortByDiff::SongSortByDiff() : SongSort(by_diff) {}
SongSortByDiff::~SongSortByDiff() {}
```

```cpp
// AFTER — inline in header
class SongSortByDiff : public SongSort {
public:
    SongSortByDiff() : SongSort(by_diff) {}
    ~SongSortByDiff() {}
};
// (remove from .cpp)
```

### Real Examples

| Function | Before | After | Notes |
|----------|--------|-------|-------|
| `SongSortByDiff::SongSortByDiff` | 91.0% | 100% | Inlined ctor + dtor in `SongSortByDiff.h`, removed `.cpp` defs |
| `SongSortByLocation::SongSortByLocation` | 91.0% | 100% | Same pattern |
| `PlaylistSortByType::PlaylistSortByType` | 91.0% | 100% | Same pattern; also added `utl/Symbol.h` include to the header |

### Detection

Open both versions side-by-side and look for `inline ctor()` in upstream's
header where ours has the body in `.cpp`. The diff scope is small but the
guard-counter ripple can move multiple unrelated static-init sequences.

### When It Hurts

If the ctor body is non-trivial (>3 source lines, calls non-inlined functions),
moving it inline can *worsen* the match because the call sites change shape.
This pattern is specifically for bodyless ctors and trivial init-list-only
ctors that are obvious "always inline" candidates.

---

## Sort Comparator Inline Location (std::sort / std::__median)

**Impact:** +30-50% on the std::sort template instantiation
**Success Rate:** 100% when applied to a sort comparator with one-line `operator()`
**Time:** 5 minutes

### Symptom

A std::__median template instantiation (e.g.
`__median<StoreOffer*, SortCmp>`) matches very poorly (50-60%). The diff
shows a `bl` to `SortCmp::operator()` where target uses an inline call
sequence (`lwz / mtctr / bctrl` or just inlined comparison).

### Why It Works

`std::sort` and its helper templates (`__median`, `__partition`,
`__insertion_sort`) call the comparator via `operator()`. When the
comparator is defined inline in the header, MSVC sees the body during
template instantiation and inlines the comparison directly. When the
comparator's `operator()` is defined out-of-line in the .cpp, the
template can only emit a `bl` to the out-of-line definition.

The target binary was compiled with the comparator inline. To match it,
move the `operator()` body into the header.

### Fix

```cpp
// BEFORE — out-of-line in StoreOffer.cpp
// StoreOffer.h
class SortCmp {
    Symbol mSort;
public:
    SortCmp(Symbol s) : mSort(s) {}
    bool operator()(const StoreOffer *a, const StoreOffer *b) const;
};

// StoreOffer.cpp
bool SortCmp::operator()(const StoreOffer *a, const StoreOffer *b) const {
    return a->Compare(b, mSort) < 0;
}
```

```cpp
// AFTER — inline in header
class SortCmp {
    Symbol mSort;
public:
    SortCmp(Symbol s) : mSort(s) {}
    bool operator()(const StoreOffer *a, const StoreOffer *b) const {
        return a->Compare(b, mSort) < 0;
    }
};
// (remove from .cpp; check no other callers depend on out-of-line def)
```

### Real Examples

| Function | Before | After | Notes |
|----------|--------|-------|-------|
| `__median<StoreOffer*, SortCmp>` | 53.1% | 100% | `SortCmp::operator()` moved from `StoreOffer.cpp` to `StoreOffer.h` |

### Adjacent Fixes

When you make this change, also check the call site that constructs
`SortCmp` and passes it to `std::sort`. Upstream typically uses an
inline temporary (`std::sort(beg, end, SortCmp(s))`) rather than a named
local. Match that style.

### Caveats

- Verify no other translation unit references `SortCmp::operator()` as an
  external symbol. Inlining it removes the out-of-line emission.
- If the comparator captures non-trivial state (e.g., a heavyweight
  member like `std::vector`), inlining is a behavioral change worth a
  performance check, even if match-correct.

---

## Inline Boundary Cascade (ICF Merge of Out-of-Line Accessor)

**Impact:** Variable. Typically fixes a downstream caller's register-allocation
swap (5-50 instructions) by triggering an ICF merge.
**Success Rate:** Pattern is proven; finding it requires diagnose work
**Time:** 30 minutes (because the symptom is in a *different* function)

### Symptom

A function `Caller::Foo` has a residual register-allocation swap (e.g.
r28↔r29 across many instructions) that won't budge despite source-level
fixes. The function being called inside `Caller::Foo` is a trivial
inline accessor like `GetMotdFreq()` defined in a header.

### Why It Works

Out-of-line trivial accessors with identical bodies get folded by the
linker via ICF (Identical COMDAT Folding). When a target binary's
`GetMotdFreq()` is ICF-merged with another function (e.g.
`CVoiceSkin::GetGraphNode`), the call site uses a **real `bl`** to the
merged address. Our inline-in-header version emits the body directly
at the call site (`lbz r10, 0x6c(r11)`).

The presence or absence of the `bl` changes the function's register
pressure profile at the call site — which can cause MSVC to color the
surrounding callee-saved registers differently, fixing what looks like
unrelated register-allocation noise downstream.

### Fix (When Diagnosed)

Move the inline accessor from the header into a `.cpp` file:

```cpp
// BEFORE — RockCentral.h
class RockCentral {
public:
    int GetMotdFreq() const { return mMotdFreq; }
    // ...
};

// AFTER — RockCentral.h
class RockCentral {
public:
    int GetMotdFreq() const;  // declaration only
    // ...
};

// RockCentral.cpp
int RockCentral::GetMotdFreq() const { return mMotdFreq; }
```

After rebuild, the linker may merge `GetMotdFreq` with another
identically-bodied function. The call site in `Caller::Foo` now uses
`bl <merged_address>` instead of an inlined load — and the residual
register swap dissolves.

### Real Examples

| Function | Before | After | Cause |
|----------|--------|-------|-------|
| `MainMenuPanel::MotdInitializeTexts` | 92.6% | 100% | Moved `RockCentral::GetMotdFreq` from inline → out-of-line. Resulting ICF merge with `GetGraphNode` made the call site emit `bl`, dissolving 47 r28↔r29 swap mismatches and 7 instruction reorderings |

### Detection

This is hard to spot from the diff of the affected function alone. Signs:

1. The mismatched function calls a trivial accessor.
2. Upstream's matching version of the *same source* still emits a `bl` to
   that accessor (check by reading upstream's compiled .obj).
3. The residual mismatches are dominated by register swaps, not real
   logic differences.
4. `merged-symbols` lookup near the suspected accessor's address shows
   another function with the same body would merge with it.

### When It Doesn't Help

If the accessor's body is unique (no other function in the binary has
the same instructions), moving it out-of-line just adds a `bl` without
triggering ICF. In that case, the cascade fix won't apply — the source
of the register swap is elsewhere.

---

## Inline-Level Counting via the Parameter Home Area

> **The heading is a misnomer, kept only so existing links do not break.** The
> slot is **not** the parameter home area — see
> [What the slot actually is](#correction-2026-08-06--not-an-abi-property-and-not-the-home-area).
> It is the first local-temp slot, and it looks fixed only because the stack
> packer reuses it. Read that correction before acting on anything in this
> section.

**Impact:** 0 to +7% — two constructors went 92.8% → 100% and 79.3% → 100%;
most candidate sites yield nothing
**Success Rate:** low in general, high in one narrow sub-case (constructors
whose adjacent same-typed scalar fields are really an aggregate member)
**Population:** **rare in its strict form** — 14 functions in the *whole*
binary have a nonzero census delta (see
[Empirical yield](#empirical-yield-2026-08-06--census-tool-and-measured-results))
**Time:** 10 minutes (plus one re-measure per other caller of the accessor)
**Discovered:** 2026-08-04 regswap sweep
([session](../../sessions/2026-08-04-regswap-atlimit-sweep.md));
mechanism corrected by controlled probes 2026-08-06

### Symptom

The diff contains a store to a fixed frame offset — typically
`stw rN, 0x50(r31)` — that is **never reloaded**, appearing on only one side.
Usually it comes in a pair with an address computation:

```
base only:   addi r10, r11, 0x198       ; &someObject->mField
base only:   stw  r10, 0x50, r31        ; dead store, never read back
```

Because the store is dead it looks like scheduling noise. It is not.

### Why It Works

The store materialises an inlined callee's `this` into a stack temp. It is
dead — the value stays in a register for the actual work — but MSVC emits it
anyway, once per qualifying inline level. It is **not** emitted for every
inline level: three conditions must hold simultaneously, and removing any one
of them removes the store (see
[the mechanism](#correction-2026-08-06--not-an-abi-property-and-not-the-home-area)).

Where the conditions do hold, the writes are a **counter for inline depth**:

- An extra **base-only** `addi rN, rBase, <field-offset>` + `stw rN, <home>`
  pair ⇒ our source has *one more* inline level than the target at that point.
  The `<field-offset>` names the sub-object whose method we are calling, which
  tells you exactly which accessor to look at.
- A **target-only** home write ⇒ the target has an inline level we are
  *missing* — we flattened something the original delegated.

The usual cause of the first case is a small accessor that delegates to another
inline instead of doing the work itself.

### Fix

```cpp
// BEFORE — src/system/rndobj/Part.h
// mEmitRate is at 0x198; Vector2::Set is a second inline level, so this
// emits `addi rN, this, 0x198` + a home-slot write for Set's `this`.
void SetEmitRate(float x, float y) { mEmitRate.Set(x, y); }

// AFTER — target assigns the components directly, one inline level
void SetEmitRate(float x, float y) {
    mEmitRate.x = x;
    mEmitRate.y = y;
}
```

`RhythmBattlePlayer::UpdateScore(Hmx::Object*)`: **97.5% → 98.4%**, and the
insert/delete clusters disappeared completely (the residual became a pure
`r29`↔`r30` callee-saved renaming over an identical instruction stream).

### How to Confirm Before Editing

0. **Pre-qualify first, before measuring anything.** If the function has no
   `__CxxFrameHandler` / `__ehfuncinfo$` (no local with a non-trivial
   destructor, no try/catch), or the inlined callee touches `this` only once,
   or the receiver is `this` itself / a plain pointer already in a register,
   the lever **cannot** apply. Skip it — do not spend a build on it.
1. The dead store's offset (`0x50` here) is the same slot the surrounding
   `MILO_ASSERT` sequence uses for its `const int&` line-number temp. That is
   **not** proof it is a home slot — it is proof the stack packer coalesces
   several unrelated temps onto the first local-temp slot. Treat a shared
   offset as weak corroboration only, and check the store is genuinely never
   read back (an `addi rX, r1, 0x50` counts as a read).
2. The `addi` displacement matches a member offset on the receiver class
   (`lookup_struct_offset` / `struct-info`).
3. That member's type has a small inline mutator the accessor is calling.

### CORRECTION (same day) — a single call site does not determine the body

The `SetEmitRate` fix below **was reverted.** It gained +0.9% on
`RhythmBattlePlayer::UpdateScore` (1812 B) and lost 0.9% on `RndScaleObject`
(2928 B) — byte-weighted, a net loss. Measured A/B:

| `SetEmitRate` form | `UpdateScore` | `RndScaleObject` |
|---|---|---|
| `mEmitRate.Set(x, y)` | 97.5% | **90.4%** |
| `mEmitRate.x = x; mEmitRate.y = y;` | **98.4%** | 89.5% |

Two call sites in the same binary disagree about which form the original used.
So **the home-write evidence at one call site constrains an accessor's body; it
does not determine it.** The lever remains valid as a *diagnostic* — it
correctly identified that `UpdateScore` has one inline level too many — but the
*fix* must be validated across every caller, weighted by size, before it lands.
Where callers disagree, the real difference is somewhere other than the
accessor, and flattening it is just moving the error around.

**The regression check itself is the trap.** The check was run and it passed,
because the line-number-to-enclosing-function mapping resolved
`Utl.cpp:2077` to `ResourceFileCacheHelper::CacheFile` (line 1923) instead of
`RndScaleObject` (line 1949). `CacheFile` is three instructions and was 100%
either way. **Resolve the enclosing symbol from the objdiff symbol list, not
from the nearest preceding line matching a function-definition regex.**

### Caveat — Re-measure Every Caller

This is a header edit. Before committing, re-measure **every** other caller of
the accessor; flattening an inline level changes their codegen too. For
`SetEmitRate` the other four callers (`RndParticleSys::OnSetEmitRate`,
`RndParticleSysAnim::SetFrame`, `RhythmBattlePlayer::Enter`,
`ResourceFileCacheHelper::CacheFile`) all stayed at 100%. If any caller
regresses, the accessor is not the right lever — the extra level is somewhere
else on the chain.

### When It Doesn't Help — follow the count, not the direction

If both sides have the same number of home writes, inline depth already
matches and the residual is something else. Do not "fix" a home-write count
that already agrees.

**More important caveat, from `RndMesh::SetVolume` (lane F, same day the lever
was found): the lever is bidirectional and the obvious direction is often
wrong.** There, an extra base-only home write did correctly say "we have too
many inline levels" — but removing the wrapper entirely made it *worse*
(92.0% → 91.3%), because the target wanted **exactly one** level, not zero.
Likewise `HamCharacter::Poll` was fixed by *un-hoisting* a reference so we
would redundantly reload a member — the inverse of the usual liveness advice.

So: read the home-write **count** off the asm and match that number. Do not
reason from "fewer inline levels is closer to the target" — it isn't a
direction, it's a count.

**A zero/zero census is a normal result, not a failed audit.** This build only
emits a home write when the inlined callee's `this` is a **computed sub-object
address** (`addi rN, rBase, <field-offset>`) — and, as the 2026-08-06 probes
below establish, only when two further conditions hold on top of that. A
function that calls accessors on `this` directly produces no home writes at
all, on either side — that is the expected outcome, not evidence you looked
wrong. `BustAMovePanel::Poll` was
audited this way: all 16 of its `stw` instructions are live (Message `DataNode`
temps, member stores, static guards, and two genuine outgoing stack arguments
for a 10-parameter ctor), and the correct conclusion was that the lever does
not apply to that function. Do the census, record the zero, and move to another
lever.

### CORRECTION (2026-08-06) — not an ABI property, and not the home area

**ANSWERED: the signature survives retail `/O1`.** It was originally predicted
that these writes were a low-optimization artifact an optimized build would
elide. That prediction was **wrong**. A whole-binary census on rb3-xenon —
Rock Band 3, retail `/O1 /Oi /GR /EHsc`, a *different compiler build*
(10224 vs DC3's 11886) — found **8,711 of 82,230 functions (10.59%)** carrying
dead home stores, 23,378 in total, **7,715 of them with the exact
`addi`-then-store signature** described above. DC3's debug target reads
**10.34%** — essentially the same rate. Verbatim rb3 instance:
`addi r25, r24, 0x90` / `stw r25, 0x50(r31)`.

The census numbers stand. **The conclusion drawn from them does not.** The
original write-up closed with "this is a property of the MSVC PowerPC ABI, not
of the optimization level, and the lever is portable across both trees." That
is refuted.

So this is **not** an ABI property. Controlled probes with this exact compiler
and flag set (`/O1 /Oi /GR /EHsc`) show the store is emitted only when **all**
of the following hold:

1. The function carries a **C++ EH state** — `__CxxFrameHandler` /
   `__ehfuncinfo$`, i.e. at least one local with a non-trivial destructor, or a
   try/catch.
2. An inlined member call's `this` is a **computed sub-object address**
   (`&obj->member`) — not a pointer already in a register, and not the
   enclosing object at offset 0.
3. The inlined callee references `this` **at least twice**. With a single use
   the offset folds into the load/store displacement and nothing is
   materialised.

Remove any one of the three and the store disappears.

The 10.34% / 10.59% cross-tree agreement therefore means the mechanism is
**stable and reproducible** across compiler builds and optimisation levels —
*not* that it is unavoidable. Roughly that fraction of functions in both
codebases satisfy all three conditions, because EH-carrying locals (`String`,
`Symbol`, `ObjPtr`) are ubiquitous. **The stores are removable from source.**

Condition 3 is the one this page never had, and it is why most candidate sites
yield nothing.

**Two further corrections.**

- The slot is **not the parameter home area.** A `/FAsc` listing of
  `RhythmBattlePlayer::UpdateScore` shows `$T242098`, `$T242102`, `$T242107`
  and the real local `skelIdx$` all coalesced onto offset `0x50`. It is simply
  the **first local-temp slot**, and it looks fixed only because the packer
  reuses it.
- The census figure is an **upper bound.** An address-taken temp
  (`stw rN, 0x50(r1)` followed by `addi r3, r1, 0x50` — the `MakeString` /
  `const int&` shape) has no explicit reload either, so it is counted as dead
  while being fully live.

#### Probe details

- **Field-offset magnitude is irrelevant** — `0x4` behaves identically to
  `0x198`. **Member type is irrelevant** — two `int`s behave like a `Vec2`.
- **A separate second contributor:** one *extra* store of the **raw receiver
  register** (no `addi` at all) appears when the receiver is a **member**
  pointer that is null-checked before the inlined call. It is absent for
  parameter receivers and for unconditional calls. This is why the real
  `RhythmBattlePlayer::UpdateScore` counts **3** stores, not 2 — do not try to
  explain all three with the sub-object rule.

#### Refuted: the `HamNavList::RibbonMode()` evidence

An earlier write-up of this lever cited `HamNavList::RibbonMode()` as proof of
the mechanism. It is not. The accessor is

```cpp
// src/system/hamobj/HamNavList.h
HamListRibbon::RibbonMode RibbonMode() const { return mRibbonMode; }
```

— **one** use of `this`, at **offset 0**, no sub-object. That is exactly the
shape the probes show emits **zero** stores under every configuration tested.
The citation does not hold; do not reason from it.

#### Minimal reproducing construct, and the inverse edit

```cpp
struct Inner {
    int a, b;
    void Set(int x, int y) { a = x; b = y; }   // two uses of `this`
};
struct Outer {
    Inner mInner;                              // at a nonzero offset
    void Touch(int x, int y) {
        String scratch("eh state");            // condition 1: EH-carrying local
        mInner.Set(x, y);                      // condition 2: &this->mInner
    }
};
```

`Touch` emits `addi rN, rThis, <offsetof(mInner)>` + a dead
`stw rN, <temp>(r1)`. The **inverse edit** is either of:

- **Flatten one level at the receiver**, so `this` is no longer a computed
  sub-object address: `mInner.a = x; mInner.b = y;`
- **Split the callee** so each inlined body uses `this` once.

Both remove the store. Which one the original source used is a separate
question — read the *count* off the target and match it (see
[When It Doesn't Help](#when-it-doesnt-help--follow-the-count-not-the-direction)),
and re-measure every other caller before landing a header edit.

---

## Empirical yield (2026-08-06) — census tool and measured results

### The census tool

`scripts/analysis/home_store_census.py` reads both COFF objects directly — **no
objdiff run needed**. It counts sites where `addi rD, rBase, imm` (with `rBase`
not a frame register and `imm > 0`) is followed within 24 instructions by
`stw rD, F(r1|r31)` into a slot that is never read back, and reports

```
delta = target_count − base_count
```

```bash
python3 scripts/analysis/home_store_census.py --min 90 --max 99.99
python3 scripts/analysis/home_store_census.py --all --json /tmp/home.json
```

Two load-bearing corrections were found while building it, both via **false**
candidates — keep them if you ever reimplement this:

- **`r30` is not a frame register on this target.** Only `r1` and `r31` are.
  Treating `r30` as one produced a phantom `delta −1` on
  `PartyModeMgr::DetermineSubModePlayers`.
- **`addi rX, rFrame, D` must count as a use of slot `D`.** Taking a slot's
  address (an sret buffer, an outgoing const-ref argument) makes it live with
  no load naming it.

### Population reality check — read this before planning a sweep

With the strict detector, the **90–99.99% band yields 16 candidates before
those two fixes and 4 after**, and the **whole binary (0–99.99%) has only 14
functions with a nonzero delta.**

So the strict, actionable form of this signature is **rare**. The ~10% census
figure above counts *live* slots. The earlier "floor" verdict on this pattern
was wrong — but so is the population size the original write-up implied. Do
not budget a broad sweep against it.

### Measured results

All measured with `run_objdiff` directly, `project_dir` pointed at a worktree.

| Function | Before → After | Mismatches | Edit |
|---|---|---|---|
| `HamAudio::HamAudio` | 92.8% → **100.0%** | 11 → 0 | 8 loose crossfade fields → two `HamCrossfade` structs (delta +2) |
| `NgPostProc::NgPostProc` | 79.3% → **100.0%** | 16 → 0 | 4 loose floats → two `Vector2` members (delta +2) |
| `RndAnimatable::FireFlowLabel` | 98.0% → 97.6% | 8 → 18 | real bug fix — see [below](#worked-example--the-lever-found-a-live-bug-rndanimatablefireflowlabel) |
| `EventTrigger::CleanupEventCase` | 88.4% → 88.5% | 12 → 12 | `String(it->Str())` instead of `String(*it)` |

**The productive sub-case is narrow and self-confirming:** *constructors whose
adjacent same-typed scalar fields are really an aggregate member.* Both wins
had independent source-side corroboration before the edit was made —
`PollCrossfade` copied all four fields in offset order (a longhand struct
assignment), and `mRefractPanOffset` was accumulated component-wise from an
existing `Vector2`. Run a **targeted pass over that sub-case**, not a broad
sweep.

### Negative results worth not repeating

- **Placement-new probe.** Reproducing the sub-object address with a
  placement-new temp did produce the right `addi`, but crashed the constructor
  to 79.4% — the null check and the init both sink below the body. The field
  has to be a **real member initialised in the initialiser list**.
- **`(*it).Str()` is byte-identical to `it->Str()`.** The spelling is not a
  lever; the `Str()` vs `*it` *conversion* is.

---

## Worked example — the lever found a live bug: `RndAnimatable::FireFlowLabel`

This is the strongest argument for running the lever at all: the diff it opened
up was not a codegen difference, it was a **wrong receiver**.

`RndAnimatable::FireFlowLabel` sent `on_anim_event` to the **wrong object**.
The target loads offset `0x4c` off the `AnimTask` — `mListener.mObject` — and
uses that as the `Handle` receiver. The decomp loaded `0x60`
(`mAnimTarget.mObject`) and therefore sent the message to the `AnimTask`
itself.

`AnimTask` has **no `on_anim_event` handler.** `FlowAnimate` and
`FlowSetProperty` do, and those are exactly what get passed as the `Animate()`
listener. Both sibling sends in `src/system/rndobj/Anim.cpp` (`"looped"`,
`"ended"`) already go to `mListener`. So **animation flow labels were being
delivered to an object that ignores them.** Fixed.

The fix **costs 0.4pp** (98.0% → 97.6%): correcting the receiver triggers a
whole-function `r28`↔`r29` renaming, which the diff scores as 18 mismatches
instead of 8. This is a **correct fix that lowers the headline number** — a
concrete instance of the standing rule *do not judge a fix by the headline %*.
Land it anyway.

`../og-dc3-decomp/src/system/rndobj/Anim.cpp:172` carries the **same bug**.
Recorded here only — that repo is a reference tree and was not edited.

**Secondary finding.** `HamAudio`'s old field names were wrong:
`mActiveCrossfadeEnd` (offset `0x6c`) actually received the crossfade
**start**. The replacement `HamCrossfade` members (`mStart` / `mEnd` /
`mDuration` / `mFlag`) line the roles back up with what the code does.

---

## See Also

- [../../sessions/2026-08-04-regswap-atlimit-sweep.md](../../sessions/2026-08-04-regswap-atlimit-sweep.md) — Session
  that discovered the home-area inline-level counter, plus the calibration of
  the statement-vs-expression triage rule on the AT_LIMIT+REGISTER_SWAP bucket.
  Its mechanism claim is superseded by the
  [2026-08-06 correction](#correction-2026-08-06--not-an-abi-property-and-not-the-home-area)
- [../../investigations/2026-08-04-bustamovepanel-poll.md](../../investigations/2026-08-04-bustamovepanel-poll.md) — the
  zero/zero census that first observed the computed-sub-object-address
  condition (condition 2 of 3)
- `scripts/analysis/home_store_census.py` — the COFF-level census tool; see
  [The census tool](#the-census-tool)
- [verifiable-icf.md](verifiable-icf.md) — Linker-merged ICF as a verifiable
  pattern (when accepting at_limit)
- [fixable-declarations.md: Function Definition Order ($S#)](fixable-declarations.md#function-definition-order-tu-wide-static-guard-counters) — Related
  static-init guard slot pattern, triggered by definition order rather than
  inline location
- [fixable-declarations.md: Static Variable Naming](fixable-declarations.md#static-variable-naming) — Related guard mangling pattern
- [unfixable-compiler.md: Static Guard Naming `??_B` vs `$S`](unfixable-compiler.md#static-guard-naming-convention-_b-vs-s) — When the guard
  naming convention itself diverges (not fixable by inlining)
