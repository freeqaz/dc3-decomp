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

**Impact:** +0.5-1% per occurrence, and it collapses whole insert/delete
clusters rather than shaving single instructions
**Success Rate:** high — the signal is unambiguous once you know to look
**Time:** 10 minutes (plus one re-measure per other caller of the accessor)
**Discovered:** 2026-08-04 regswap sweep
([session](../../sessions/2026-08-04-regswap-atlimit-sweep.md))

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

On this build every **inlined** callee writes its `this` into the outgoing
parameter home area before its body is emitted. The store is dead — the value
stays in a register for the actual work — but MSVC emits it anyway, once per
inline level.

So the home-slot writes are a **counter for inline depth**:

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

1. The dead store's offset (`0x50` here) is the same slot the surrounding
   `MILO_ASSERT` sequence uses to home its `const int&` line-number temp —
   that confirms it is the parameter home area and not a real local.
2. The `addi` displacement matches a member offset on the receiver class
   (`lookup_struct_offset` / `struct-info`).
3. That member's type has a small inline mutator the accessor is calling.

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
address** (`addi rN, rBase, <field-offset>`). A function that calls accessors
on `this` directly produces no home writes at all, on either side — that is the
expected outcome, not evidence you looked wrong. `BustAMovePanel::Poll` was
audited this way: all 16 of its `stw` instructions are live (Message `DataNode`
temps, member stores, static guards, and two genuine outgoing stack arguments
for a 10-parameter ctor), and the correct conclusion was that the lever does
not apply to that function. Do the census, record the zero, and move to another
lever.

**Unverified for optimized builds.** Every observation here comes from DC3's
*debug* target. Whether a retail `/O1` MSVC PPC build emits these home writes
at all is an open question — see the rb3-xenon port for the answer.

---

## See Also

- [../../sessions/2026-08-04-regswap-atlimit-sweep.md](../../sessions/2026-08-04-regswap-atlimit-sweep.md) — Session
  that discovered the home-area inline-level counter, plus the calibration of
  the statement-vs-expression triage rule on the AT_LIMIT+REGISTER_SWAP bucket
- [verifiable-icf.md](verifiable-icf.md) — Linker-merged ICF as a verifiable
  pattern (when accepting at_limit)
- [fixable-declarations.md: Function Definition Order ($S#)](fixable-declarations.md#function-definition-order-tu-wide-static-guard-counters) — Related
  static-init guard slot pattern, triggered by definition order rather than
  inline location
- [fixable-declarations.md: Static Variable Naming](fixable-declarations.md#static-variable-naming) — Related guard mangling pattern
- [unfixable-compiler.md: Static Guard Naming `??_B` vs `$S`](unfixable-compiler.md#static-guard-naming-convention-_b-vs-s) — When the guard
  naming convention itself diverges (not fixable by inlining)
