# BustAMovePanel::Poll — 98.6% → 99.6%

**Symbol:** `?Poll@BustAMovePanel@@UAAXXZ`
(`public: virtual void __cdecl BustAMovePanel::Poll(void)`)
**Unit:** `src/lazer/game/BustAMovePanel.cpp` (body at ~line 1506)
**Size:** 3008 bytes / 753–761 instructions depending on variant
**Date:** 2026-08-04
**Branch:** `sweep/regswap-bam` (worktree `wt/rsw-bam`)

| | |
|---|---|
| Baseline in | **98.6%** normalized (98.1% raw), 26 mismatched instructions |
| Final | **99.6%** normalized (99.1% raw), **5** mismatched instructions |
| Commits | `bb18b48c`, `db6866a9`, `0a7dfc2b`, `0e4cff98` |

Four independent source-level fixes landed. All four are behaviour-preserving;
two of them (`mBeatCount` signed compare, `scores` pointer removal) also move
the source *towards* the semantically-plain form rather than away from it.

---

## 1. What landed

### 1.1 `bb18b48c` — invert `isPlayer0Pink` (98.6 → 98.8, +0.2)

```cpp
// BEFORE
bool isPlayer0Pink = false;
if (TheGameData->Player(0)->Side() == kSkeletonLeft
    && GetPlayerColor(0) == "pink") {
    isPlayer0Pink = true;
}

// AFTER
bool isPlayer0Pink = true;
if (TheGameData->Player(0)->Side() != kSkeletonLeft
    || GetPlayerColor(0) != "pink") {
    isPlayer0Pink = false;
}
```

The `= false` initialiser is materialised by **reusing the function's zero
register** (`mr r28, r22` at idx 319, hoisted above the `Side()` /
`GetPlayerColor()` calls). That extra use lengthened the zero constant's live
range enough to flip the callee-saved colouring for the whole function: the
zero took `r22` and `&TheTaskMgr` took `r21`, where the target has zero in
`r21` and `&TheTaskMgr` in `r22`. **13 instructions of pure `r21`↔`r22`
renaming.**

Initialising to `true` hoists `li r28, 1` — an immediate, *not* the zero
register — so the zero constant keeps the target's shorter live range and is
coloured `r21`. All 13 renaming mismatches vanish at once.

This is the answer to the brief's question "did the r21↔r22 renaming ever
flip?" — **yes**, and this is what flipped it. See §3.1 for the two other
edits that also flipped it (and cost more elsewhere).

### 1.2 `db6866a9` — declare `remainingBeat` before `currentPhrase` (98.8 → 99.3, +0.5)

```cpp
// BEFORE                                  // AFTER
unsigned int currentPhrase = 0;            int remainingBeat = (int)(TheTaskMgr.Beat() + 0.5f);
int remainingBeat = ...TheTaskMgr.Beat()   unsigned int currentPhrase = 0;
```

Pure declaration order; neither initialiser reads the other. The target emits
`bl Beat` **before** `mr r24, r21` (`currentPhrase = 0`), and reloads the
`fctiwz`'d beat (`lwz r10, 0x8c(r31)`) **after** the `srawi` that computes
`mSongStructure.size()`. Both orderings fall out of the swap. Killed 4
insert/delete rows (idx 677/681 and 687/689).

### 1.3 `0a7dfc2b` — remove the `scores` preheader local (99.3 → 99.5, +0.2)

```cpp
// BEFORE
float *scores = (float *)&mPlayerScoreLeft;
for (int p = 0; p < 2; p++) {
    ...
    scores[p] = mRecorder->GetScore(pSkelIdx, p, -1.0f, false);
    float pBase = scores[p];

// AFTER
for (int p = 0; p < 2; p++) {
    ...
    ((float *)&mPlayerScoreLeft)[p] = mRecorder->GetScore(pSkelIdx, p, -1.0f, false);
    float pBase = ((float *)&mPlayerScoreLeft)[p];
```

An explicit preheader *statement* makes MSVC emit `addi r28, r30, 0x90` before
the induction variable is initialised. The target has:

```
206  mr    r29, r21              ; p = 0
207  lfs   f26, __real@3fb33333  ; 1.4f
208  addi  r28, r30, 0x90        ; &mPlayerScoreLeft
```

With no explicit statement the address computation comes out of
loop-invariant hoisting, which runs *after* induction-variable setup, and the
three preheader instructions land in the target's order.

The double-cast spelling is uglier than the local; it is nonetheless the form
that matches. See §5 for the type-punning landmine this exposes.

### 1.4 `0e4cff98` — `mBeatCount >= 3` is a signed compare (99.5 → 99.6, +0.1)

Removed the `(unsigned int)` cast. Target: `cmpwi cr6, r11, 0x3`. **This is
the parked-and-retried edit** — see §4.

---

## 2. The home-area / inline-level audit (the brief's headline lever)

**Result: negative. Inline depth already matches on both sides; there is
nothing to fix with this lever in this function.**

Method: full instruction listing (`run_objdiff full_listing:true`), then
enumerate every `stw` and classify it. `r31` is set by
`subi r31, r1, 0x1f0` *before* `stwu r1, -0x1f0, r1`, so **`r31 == r1` after
the prologue** and `r31`- and `r1`-relative offsets are directly comparable.

There are only **16 `stw` instructions in the entire 753-instruction
function**, and every one of them is a live store. Full census:

| idx | store | side | what it is | dead? |
|----:|-------|------|-----------|-------|
| 5 | `stwu r1, -0x1f0, r1` | both | prologue | — |
| 28, 29 | `stw rZERO, 0x98/0x9c(r31)` | both | `DataNode(0)` for `Message("hide_hud", 0)` #1 | live (reloaded at 55/58) |
| 53 | `stw r29, 0xe8(r31)` | both | `Message` vtable store (dtor path) | live |
| 60, 61 | `stw rZERO, 0x90/0x94(r31)` | both | `DataNode(0)` for `Message("hide_hud", 0)` #2 | live (reloaded at 83/86) |
| 80 | `stw r29, 0xd8(r31)` | both | `Message` vtable store #2 | live |
| 105 | `stw r3, 0xa0(r30)` | both | `mCreatorSide = ...Side()` | live (member) |
| 117 | `stw r29, 0x44(r11)` | both | `mRecorder->mSkeletonIndex = skelIdx` | live (member) |
| 123 | `stw r29, 0x58(r30)` | both | `mRecordSkelIdx = skelIdx` | live (member) |
| 383, 440 | `stw r2x, 0x9bc(r30)` | both | `mDepthBufPlayer = ...` | live (member) |
| 501 | `stw r23, 0x968(r30)` | both | `mFailureEndBeat = -1` | live (member) |
| 506, 564 | `stw r11, <static guard>` | both | `??_B`/`$S` static-init guard | live |
| 580 | `stw r7, 0x64(r1)` | both | `100` → DebugGraph arg 7 (outgoing stack arg) | live (ABI) |
| 588 | `stw r3, 0x7c(r1)` | both | outgoing stack arg | live (ABI) |

**Home-slot write count: base = target = 0 dead stores.** No `stw rN, 0x50(r31)`-style
parameter-home writes exist on either side, and the two `r1`-relative stores
that do exist (`0x58`, `0x64`) are genuine outgoing stack arguments for the
10-parameter `DebugGraph` constructor, present identically on both sides.

Per the pattern doc's own "When It Doesn't Help" clause — *"If both sides have
the same number of home writes, inline depth already matches and the residual
is something else. Do not 'fix' a home-write count that already agrees."* —
this lever is correctly inapplicable here. Every accessor the brief flagged
(`SetShowing`, `SetRatingFrac`, `SetPlayerPalette`, `ForceDrawSkeletonIndex`,
`HamPlayerData::Side()`, `Provider()->Export`, `GetScore`, `MsToBeat`) inlines
to the same depth as the target already, or is a real `bl`/`bctrl` on both
sides.

Note for whoever generalises this lever: **this build only emits home-area
writes when the inlined callee's `this` is a computed sub-object address.**
Functions like `Poll` that call accessors on `this` directly, or through a
pointer already in a register, produce no home writes at all — so a zero/zero
census is the *expected* outcome for a large fraction of functions, not a sign
the audit was done wrong.

---

## 3. Everything tried, with measured deltas

Each row is a single change measured against the state named in "from".
`run_objdiff` normalized % throughout, `project_dir=/home/free/code/milohax/wt/rsw-bam`.

| # | Change | From | Result | Δ | Verdict |
|--:|--------|-----:|-------:|--:|---------|
| 1 | `bool isPlayer0Pink = A && B;` | 98.6 | 97.2 | **−1.4** | reverted (§3.1) |
| 2 | `bool isPlayer0Pink = A ? B : false;` | 98.6 | 98.4 | −0.2 | reverted |
| 3 | `Hmx::Color(1,1,1)` 3-arg ctor for the white DebugGraph arg (with #1) | 97.2 | 97.2 | **0.0** | exactly neutral |
| 4 | drop `(unsigned int)` cast (with #1) | 97.2 | 96.9 | −0.3 | parked → later landed |
| 5 | `scores` decl moved inside the loop (with #1) | 97.2 | 96.7 | −0.5 | reverted |
| 6 | extra `{ }` block around `mRecorder->Poll();` (with #1) | 97.2 | 97.2 | **0.0** | exactly neutral (§3.2) |
| 7 | decl/assign split: `bool x; x = A && B;` | 97.2 | 97.2 | **0.0** | exactly neutral |
| 8 | **invert to init-true / clear-on-negation** | 98.6 | **98.8** | **+0.2** | **LANDED `bb18b48c`** |
| 9 | drop `(unsigned int)` cast — retry #1 | 98.8 | 98.5 | −0.3 | parked again |
| 10 | `scores` decl inside loop — retry | 98.8 | 97.1 | −1.7 | reverted (adds `r20`) |
| 11 | `int p = 0;` hoisted above `scores`, bare `for (; p < 2; p++)` | 98.8 | 98.8 | **0.0** | exactly neutral |
| 12 | **`remainingBeat` declared before `currentPhrase`** | 98.8 | **99.3** | **+0.5** | **LANDED `db6866a9`** |
| 13 | `&&` form — retry #2 | 99.3 | 97.5 | −1.8 | reverted |
| 14 | **`((float *)&mPlayerScoreLeft)[p]` in place of `scores`** | 99.3 | **99.5** | **+0.2** | **LANDED `0a7dfc2b`** |
| 15 | **drop `(unsigned int)` cast — retry #2** | 99.5 | **99.6** | **+0.1** | **LANDED `0e4cff98`** |
| 16 | `&&` form — retry #3 | 99.6 | 97.7 | −1.9 | reverted (§3.1) |
| 17 | int literals `Hmx::Color(1,1,1,1)` / `(0,0,0,0.3f)` (with #16) | 97.7 | 97.7 | **0.0** | exactly neutral |
| 18 | `bool x; if (A) x = B; else x = false;` | 99.6 | 99.1 | −0.5 | reverted |
| 19 | `bool x = true; if (!A) x = false; else x = B;` | 99.6 | 99.1 | −0.5 | reverted |

### 3.1 The `&&` form — a real, reproducible cross-block coupling

This is the most interesting negative result in the run and it deserves its
own record, because **the `&&` form is almost certainly what the original
source said**, and it still cannot be used.

The target's `isPlayer0Pink` materialisation is:

```
322  cmpwi   r3, 0                ; Side() == kSkeletonLeft ?
323  bne     0x534                ;   no  -> shared zero path
324-330                           ; GetPlayerColor(0) == "pink"
331  clrlwi. r11, r3, 24
332  li      r10, 0x1             ; assume true
333  bne     0x538                ;   B true -> keep 1
334  mr      r10, r21             ; L_ZERO: r10 = 0   (shared by both false paths)
335  lwz     r11, 0x938, r30      ; scheduled into the delay-ish slot
336  clrlwi  r28, r10, 24         ; normalise to bool
```

Both false paths merging on a single `r10 = 0`, plus the trailing
`clrlwi ..., 24` normalisation of an already-0/1 value, is the signature of a
**short-circuit `&&` materialised into a `bool` variable** — not of an
if-statement. `bool isPlayer0Pink = A && B;` reproduces idx 319–336
**byte-for-byte**, *and* fixes the `r21`↔`r22` renaming.

But it simultaneously flips the frame-layout assignment of the two
`Hmx::Color` temporaries in the `bam_debug` `DebugGraph` constructor call, ~230
instructions later and in a different branch of the function:

| temp | target slot | `&&` build slot |
|---|---|---|
| `Hmx::Color(1.0f, 1.0f, 1.0f, 1.0f)` (arg 5) | `0xb0`–`0xbc` | `0xc0`–`0xcc` |
| `Hmx::Color(0.0f, 0.0f, 0.0f, 0.3f)` (arg 6) | `0xc0`–`0xcc` | `0xb0`–`0xbc` |

The ABI mapping is unaffected (arg 5 → `r8`,`r9`; arg 6 → `r10` + stack
`0x58`; the `100` → stack `0x64`) — only the scratch slots swap, and the whole
block reschedules around them. That costs **26 mismatches** (9 insert, 9
delete, 8 diff_arg) against the 4 it saves.

`run_diff_inspect mode=stack-layout` confirms the diagnosis precisely:

```
Frame size:  TGT 0x1f0   BASE 0x1f0   Δ +0x0
Callee-saved GPRs: 11 = 11    FPRs: 7 = 7
PERMUTED 10 · MATCH 40
→ "both sides use the same slot at different program points, i.e. the SAME SET
   of slots with variables assigned differently. This is MSVC temporary/slot
   allocation shaping, not a declaration count difference."
```

Attempts to break the coupling, all measured, all neutral or worse:
`Hmx::Color` 3-arg ctor (#3), int literals (#17), decl/assign split (#7),
extra lexical scope (#6). The coupling survived **three** retries at 98.6,
99.3 and 99.6 — i.e. it is not a masking artifact of any of the four defects
fixed in this session. At 99.6 the `&&` build's residual is *exclusively* the
DebugGraph block plus the commutative `add`; every other instruction in the
function matches.

**Conclusion:** our `bam_debug` DebugGraph statement almost certainly differs
from the original in some way that is invisible while `isPlayer0Pink` is
written as a statement, and only becomes visible under the `&&` form. Cracking
it would be worth roughly **+0.3% here (99.6 → ~99.9)** and, more importantly,
would explain an MSVC frame-layout rule we do not currently model. This is the
single highest-value open lead on this function.

### 3.2 Lexical scope count is *not* the mechanism

Worth recording because it looks like a plausible lever and is not one. The
static-local scope index in the mangled names differs markedly between target
and base:

| symbol | target | base (98.6) | base (`&&`) |
|---|---|---|---|
| `??__FscoreGraph@?…@` (atexit) | `?DG` (= 0x36 = 54) | `?EO` (78) | `?EO` (78) |
| `?$S16@?…@` (static guard) | *(stripped)* | `?EL` (75) | `?EI` (72) |

Our scope counter runs ~20 higher than the target's, and it *moves* when the
source structure changes (75 → 72 under the `&&` edit), so it looked like a
lever. Adding a redundant `{ }` block around `mRecorder->Poll();` (#6) changed
the match by **exactly zero instructions**. MSVC's stack-temp layout here is
not driven by the lexical scope counter. The scope-index gap remains an
unexplained (and score-invisible, since it only appears inside relocation
names) structural difference.

---

## 4. Parked-and-retried edits

Following the sibling lane's mid-run correction (a correct edit can measure as
a regression while an unrelated defect still masks it):

**`(unsigned int)mBeatCount >= 3` → `mBeatCount >= 3`** — retried three times:

| retry point | from | to | Δ | why |
|---|---:|---:|--:|---|
| on the 98.6 baseline (pre-session, re-confirmed) | 98.6 | 98.3 | −0.3 | fixes idx 129, but idx 206–210 preheader degrades 2 `replace` → 4 insert/delete |
| after `bb18b48c` (98.8) | 98.8 | 98.5 | −0.3 | same cause, unchanged |
| **after `0a7dfc2b` (99.5)** | 99.5 | **99.6** | **+0.1** | preheader now matches, so the cast removal is free — **LANDED** |

The masking defect was exactly the one the sibling predicted: the idx 206–210
preheader ordering. Once `0a7dfc2b` removed the explicit `scores` statement,
the preheader aligned and the cast removal paid. **The "revert anything that
lowers %" rule would have permanently discarded a correct edit here.**

The `&&` form was parked and retried on the same discipline (#1 → #13 → #16)
and did *not* pay; three retries across three different states, always the
same 26-mismatch DebugGraph cost. That is what a genuine trade-off looks like,
as distinct from a masked win.

---

## 5. Live bugs / semantic hazards

**No live bugs found.** Nothing in this function diverges semantically from
the target. Two hazards worth recording anyway:

1. **`mPlayerScoreLeft` / `mPlayerScoreRight` are declared `int` but hold
   float bits** (`BustAMovePanel.h:95-96`, and the comment at
   `BustAMovePanel.cpp:1393`). `0a7dfc2b` makes the type-punning more visible
   by spelling `((float *)&mPlayerScoreLeft)[p]` at the use sites. This is
   strict-aliasing UB and it is load-bearing for the match. It is *correct* on
   the Xbox 360 target and on the native port (both have 4-byte `int` and
   `float`, and the two members are adjacent), but any future code that reads
   these members as integers — or any struct-layout change that inserts
   padding between them — silently corrupts the ShowMoveSequence scores. If
   these are ever retyped, they must be retyped as `float mPlayerScores[2]`
   together, and the same pattern at `BustAMovePanel.cpp:1398` (a different
   function) must change with them.

2. **The two hand-unrolled integer-power loops** (`unsigned int e = 2; float
   scoreSq = 1.0f; do { if (e & 1) scoreSq *= base; ... }`) are a decompiled
   constant-exponent `pow` expansion. Traced by hand with `e = 2`: iteration 1
   sets `base = base²`, iteration 2 multiplies it into `scoreSq` and breaks —
   so the result is exactly `mMoveScore²`. Semantically correct. Both loops
   match the target at 100% (idx 174–181 and the mirrored copy in the
   ShowMoveSequence arm) as *inlined* code with no `bl`, so there is **no
   helper call to reconstruct** — searching RB3 / og-dc3 for a `Pow` helper
   would not change the score. Leaving them as-is; a readability pass could
   replace both with `score * score` only if it re-measures at 99.6.

---

## 6. Residual at 99.6% — 5 instructions

```
[319] insert    li      r28, 0x1
[332] delete    li      r10, 0x1
[334] diff_arg  mr      r10, r21   vs  mr r28, r21
[336] delete    clrlwi  r28, r10, 24
[729] diff_arg  add     r4, r11, r28  vs  add r4, r28, r11
```

**idx 319/332/334/336 — the `isPlayer0Pink` bool shape.** Not a floor. The
exact source form that fixes it is known (`bool x = A && B;`, §3.1) and
reproduces the target byte-for-byte; it is blocked only by the DebugGraph
frame-layout coupling. Anyone picking this up should attack the DebugGraph
temp slots, not the bool.

**idx 729 — commutative `add`, probable floor, with evidence.** This computes
`&mSongStructure[i]` for `MakeString<int>`'s `const int&` parameter. `r28` is
**not** a source-visible value: it is a compiler-created strength-reduced
induction variable, initialised to 0 at idx 713 (`mr r28, r21`) and bumped by
`addi r28, r28, 0x4` at idx 750. `r11` is the vector's `mBegin`, reloaded each
iteration at idx 732. Because the offset operand is synthesised by MSVC's own
loop strength reduction rather than written by the programmer, **no source
ordering of `mSongStructure[i]` can steer the operand order** — the only lever
would be editing `std::vector::operator[]` in a shared header, which would
perturb every other vector user in the tree. This matches the
`stream3_fmuls_operand_order_floor` finding (plain 2-term same-register swap =
backend floor); it is not the exempt int-ABS sub-case. Calling it a floor on
that basis, not on "regalloc".

---

## 7. Reproduce

```bash
cd /home/free/code/milohax/wt/rsw-bam && ninja      # ~1-3 min warm-up
# then, via MCP:
#   run_objdiff symbol="?Poll@BustAMovePanel@@UAAXXZ" \
#               project_dir="/home/free/code/milohax/wt/rsw-bam"
```

Caveat carried over from the brief and re-confirmed: `run_diff_inspect`
`mode=attributed` and `mode=asm_listing` **segfault** on this TU (the `/FAs`
recompile of `BustAMovePanel.cpp` crashes). `mode=stack-layout`,
`mode=mismatches` and `mode=diagnose` all work fine and `stack-layout` was the
decisive tool for §3.1.
