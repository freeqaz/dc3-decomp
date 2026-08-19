# Local-static scope indices: reading a function's block structure off its symbol names

**dc3-decomp (title 373307D9).** Applies to any MSVC PPC target; the numbers
below were measured against this build's own compiler,
`build/compilers/X360/16.00.11886.00/cl.exe` with `/O1 /Oi /EHsc /TP`.

## What the number is

A function-local static mangles as

```
?<name>@?<scope>??<enclosing function>@4<type>A
```

`<scope>` is an MSVC number: a single digit `d` encodes `d+1`, anything else is
a base-16 string over `A`..`P` terminated by `@` (`?BA@` = 0x10 = 16, `?9` = 10).
It is **a per-function counter of scopes opened so far**, sampled at the point of
declaration. It is not an identity — a static declared after an inner block keeps
that block's number, and statics in the same scope share one.

That makes it a fingerprint of the enclosing function's block structure that
survives into the shipped binary. When our index differs from the target's, our
source has a different number of lexical scopes before that declaration. Under
the graded ruler (`functionRelocDiffs=name_check`, the one `report.json` uses)
**every relocation naming that static is charged**, so a single missing brace can
cost a 2,500 B row.

## The cost table

| construct | cost |
|---|---:|
| `if (c) stmt;` | 2 |
| `if (c) { stmt; }` | 3 |
| `else stmt;` | +1 |
| `else { stmt; }` | +2 |
| `else if (c) { stmt; }` | +4 |
| bare block `{ ... }` | 1 |
| `while` / `for` / `do` (+ its braced body) | 2 |
| `switch (x) { ... }` (+1 per braced case) | 2 |
| `MILO_ASSERT(c, line)` | 5 |
| ternary, `&&`, `||` | 0 |

`MILO_NOTIFY_ONCE` / `MILO_WARN_ONCE` cost 1 — the macro body is a bare block,
and `_dw` is declared inside it, so the macro's own block is the scope the
static lands in. `START_AUTO_TIMER` costs 0: under `MILO_DEBUG` it expands to
two *declarations* (`static Timer *_t`, `AutoTimer _at`), and a declaration
opens nothing.

**The counter starts at 2 because the function body itself is scope 2.** The
doc used to say "a function's first construct starts at 2", which is the same
statement read backwards and invites an off-by-two when you walk a function by
hand. The direct evidence is `?_t@?1??DrawShowing@WorldCrowd@@UAAXXZ@...`:
`?1` decodes to 2, and `_t` is `START_AUTO_TIMER`'s static, declared at the top
of the body before any construct. So: **start at 2, add the table in source
order, and the value you are standing on when you open the static's scope is
the index.** Verified to the digit on all five `_dw` in
`RndTexBlender::DrawShowing` (9, 15, 26, 41, 56) and both in
`WorldCrowd::DrawShowing` (42, 54).

`MILO_ASSERT`'s 5 is just the table applied to its expansion: `do`(1) +
block(1) + `if`(2) + block(1). A static declared *inside* a construct's braced
body reads the same number as one declared just after the whole construct — the
counter only ever goes up.

Further rows, all measured against the shipping cl.exe on 2026-08-19:

| construct | cost |
|---|---:|
| `else if (c) stmt;` | +3 |
| unbraced `while` / `for` body | +1 |
| `if (a) if (b) stmt;` | +4 (only `&&` collapses it to 2) |
| `MILO_NOTIFY` (not `_ONCE`) | 0 |

Two things that cost **nothing**, and both are traps because the intuition says
otherwise:

* **Inlining.** An inline callee whose body contains a bare block, an `if (){}`,
  or a `for(){}` adds 0 to the *caller's* counter, at `/O1 /Oi /EHsc` and with
  the call in a condition. The counter is a front-end lexical thing.
* **A temporary with a destructor in the static's initialiser.** `static D
  a("a", T(0))` with `~T()` numbers exactly like `static D a("a", P(0))` without.

Worked example — `_SYNC_PROP_BITFIELD` in `system/obj/Object.h` predicts 39 for
the gap between two `_s` statics in `CamShot::SyncProperty`, and 39 is what the
compiler emits.

## The target side is `ham_xbox_r.map`, NOT `symbols.txt` (2026-08-19)

**`config/373307D9/symbols.txt` is not evidence about the original's local
statics.** It names 2,192 local-static data symbols; only **998** of them exist
in `orig/373307D9/ham_xbox_r.map`, the shipped image's own linker map. The
other 1,194 were synthesised from *our* build — **97.9% are byte-identical to a
name our objects already emit**, against 85.9% for the map-backed ones. Diffing
our indices against those is a tautology wearing a lab coat.

Where it is not a tautology it is worse. A synthesised data name sitting beside
a *real* atexit helper for the same single static looks like **two** statics:

```
symbols.txt:  ??__F_dw@?1??DataIndex@NavListSortMgr@@UBAHVSymbol@@@Z@YAXXZ   <- map, real, scope 2
              ?_dw@?2??DataIndex@NavListSortMgr@@UBAHVSymbol@@@Z@4V...A      <- synthesised from US, scope 3
```

and the census duly printed `COUNT tgt/ours=(2,1)` — "the original declares a
`MILO_NOTIFY_ONCE` we do not." It does not. `NavListSortMgr::DataIndex` is
100% with **52/52 instructions equal** and contains exactly one
`MILO_NOTIFY_ONCE`, and **our own compiler emits the same index for a static's
data symbol and its `??__F` helper**, so those two names cannot describe two
declarations. The map lists only the `?1`.

That last fact is the linchpin — it is what licenses reading our *data*
indices against the map's *atexit* indices at all — so it is worth the four
lines of evidence. Read both lists out of the same freshly built `.obj`:

```
NavListSortMgr.obj   ?_dw@?2                ??__F_dw@?2                  (3, 3)
MainMenuPanel.obj    ?msg@?CF@ ?msg@?CL@    ??__Fmsg@?CF@ ??__Fmsg@?CL@  (37/43)
ShellInput.obj       ?...@?9 ?...@?N@ ?BE@  ??__F...@?9 ?N@ ?BE@         (10/13/20)
RhythmBattle.obj     ?finished_intro@?DN@   ??__Ffinished_intro@?DN@     (61, 61)
```

Equal on every one, and equal *before* `obj_atexit_scope_patcher.py` gets a
say (it pairs by canonical key = name with the counter stripped, so it does
nothing at all when our variable name differs from the target's — which is
also why "helpers vs data symbols are two different rulers" is the wrong
diagnosis for these rows).

Three more instrument rules, all learned by having them bite:

* **Key both sides by NAME ONLY.** The data symbol carries the static's type
  (`...@Z@4VMessage@@A`) and the atexit helper does not. Prefix-folding the
  helpers into type-keyed buckets doubles them: `OptionsPanel::OnMsg` declares
  `?msg@?BA@...VLinkingCodeRetrievedMsg` at 16 and `?msg@?M@...VTokenRedeemedMsg`
  at 12, both **correct and matching**, and came out as two rows reading
  `tgt=[12,16] ours=[16]` and `tgt=[12,16] ours=[12]`.
* **Strip the type at the FIRST `@4` that ends a mangling, not the last.** A
  templated static's type can contain a `@4` back-reference
  (`...@4V?$vector@MV?$StlNodeAlloc@M@stlpmtx_std@@@4@A`), and an `rsplit` cuts
  inside it. That desynchronised `AnalyzeData`'s `normalized`/`raw` from their
  own helpers and reported both as missing.
* **`build/373307D9/src/**/*.obj` includes `*.manual.obj`, which ninja neither
  builds nor links.** `ContentLoadingPanel.manual.obj`'s stale `?types@?4` was
  reported as an extra static next to the real `?types@?5` in
  `ContentLoadingPanel.obj` — which matches the map exactly.

**A count row is evidence only when it is atexit-backed.** One `??__F` helper
for an fn/name proves that type has a destructor, and the map names every
helper in the image, so the enumeration is complete in both directions. A
data-only key is not: a trivially destructible static (`Symbol`, `DataArray *`,
`const char *`) has no helper, and if the map also lacks its data name it is
simply **invisible**. The whole `_s`/`SYNC_PROP` class hides exactly here — the
map carries **511 `SyncProperty` symbols and zero `_s` statics**, so
`RndRibbon::SyncProperty _s tgt=[7] ours=[7,18,30,...,78]` says nothing at all
about the original and must never be used to delete nine `_SYNC_PROP` entries.

After all four fixes the census reads **970 agree / 203 disagree** (the old one
said 1,259/60 — it was comparing us to ourselves for most rows) and the COUNT
class went from ~33 rows to **2**.

### What the COUNT class actually was

Nine of the ten evidence-backed count rows were **local-static variable names
we had guessed**. The map's helper carries the declaring identifier, so these
are the original's own names, recovered for free and instruction-neutral:
`change_proxies`→`msg` (`ObjectDir::PostLoad`), `finish_intro`→`finished_intro`
(`RhythmBattle::OnBeat`), `on_set_frame_msg`→`start`
(`LightPreset::SetFrameEx`), `sPlayMsg`→`playMsg` (`GamePanel::UpdateLatency`),
`utility_image_loaded`→`msg` (`MainMenuPanel::UpdateArtLoaders`, whose map has
*two* `??__Fmsg` at 34/40 beside our `msg`/`utility_image_loaded` pair),
`refresh_complete`→`self_msg` (`HamStorePanel::FinishSpecialOfferEnum`), and
three in `ShellInput::SyncVoiceControl`. Watch the trap: a regex over the whole
declaration line rewrites the message **string** too —
`Message refresh_complete("refresh_complete")` became
`Message self_msg("self_msg")` before it was caught.

The one row that was a real missing declaration is worked below
(`DingoJob::SendCallback`); the two that survive are
`RndTexBlender::DrawShowing` (5 ours / 3 target, hand-inlined `DrawBlendList`)
and `BustAMovePanel::OnBeat` (2 ours / 3 target `matchedMessage`, in a
3,044-instruction function at 97.5% whose dominant problem is member offsets).

### A count row that WAS real: `DingoJob::SendCallback` (82.6% -> 99.1%)

The map has two helpers, `??__Fmsg@?L@` (11) and `??__Fmsg@?O@` (14); we
declared only the first. The second is a different type — a static
`ServerStatusChangedMsg` — at the tail of a failure block our source had
truncated. **11 -> 14 is +3, i.e. exactly one braced `if`**, which is what
identified the three nested `if`s in our source as a single
`!success && TheServer.IsAuthenticated() && !cancelled`; three would have been
+9. The rest fell out of the instruction stream and was corroborated
line-for-line by `DingoServer::OnMsg(const DingoJobCompleteMsg &)` in
`src/system/net/DingoSvr.cpp`, which ends with the identical
severity/project + `RecordDebugDataPoint` + `CancelOutstandingCalls` +
`Logout()` + `Export(ServerStatusChangedMsg(kServerStatusDisconnected))`
sequence. Two `AddPair`s were missing outright, `AddPair("sync", "sync")` was
really `AddPair("project", "sync")`, and the trailing call was `Logout()`
(vtable 0x68) not `Poll()` (0x70). **This is what a real count row looks like:
it carries instructions, so fixing it moves the match.**

## Reading the two sides correctly

This bit the census twice, and both traps are easy to walk into by hand as well.

* **Our indices come from `?<name>@?<scope>??<fn>@4<type>A` data symbols in
  `build/373307D9/src/**.obj`, never from `??__F<name>@...` atexit helpers.**
  `scripts/obj_atexit_scope_patcher.py` rewrites the atexit names in our objects
  to whatever the target says, so objdiff can pair the bodies. Reading them back
  tells you the target's structure with our filename on it. (It also leaves the
  pre-patch string behind in the COFF string table, so `strings` shows both.)
* **The target's indices come from `orig/373307D9/ham_xbox_r.map`.** The map is
  the shipped image's own linker map and it names every
  `??__F<name>@?<scope>??<fn>@YAXXZ` (486 of them) plus 1,038 local-static
  `.data` symbols. The atexit list is the only *complete* enumeration of a
  target function's statics — and only for types that have a destructor.
  `RndTexBlender::DrawShowing` has three atexit helpers; the three are the
  truth. **Do not read the target off `symbols.txt`** — see the section above.
* **A function can declare many statics under one name** — one `_dw` per
  `MILO_NOTIFY_ONCE`, one `msg` per static `Message`, one `_s` per `SYNC_PROP`.
  Compare the sorted *lists*. If the lengths differ **and the key is
  atexit-backed**, the count is the finding: we invented or dropped a
  declaration, and no amount of brace-shuffling will reconcile it. If it is
  not atexit-backed, the map is blind and the row is noise.
* **A count row is usually a NAME, not a declaration.** The helper carries the
  variable's identifier, so `tgt=[X] ours=None` most often means our variable
  is spelled differently. Check for an ours-only name at a nearby index before
  concluding anything is missing — nine of ten did in the 2026-08-19 sweep.
* **objdiff's relocation pairing tells you which static is which.** Run
  `run_diff_inspect mode=clusters` and read the `lis`/`addi` pairs: it lines our
  `?_dw@?DG@` up against the target's `?_dw@?CK@`, which is how you learn that
  two indices that happen to be equal belong to different declarations.

## Calibrating it yourself

Do not reason about a new construct — compile it. A standalone TU with no
includes is enough, and it takes seconds:

```sh
cat > /tmp/cal.cpp <<'EOF'
struct S { S(const char*); int i; };
extern void sink(int); extern bool c();
void f_top()  { static S a("a"); sink(a.i); }
void f_ifb()  { if (c()) { sink(1); } static S a("a"); sink(a.i); }
void f_bare() { if (!c()) { sink(9); } static S a("a"); sink(a.i); }
EOF
WIBO_PATH_MAP='' ~/code/milohax/wibo/build/release/wibo \
  build/compilers/X360/16.00.11886.00/cl.exe /nologo /c /GR /O1 /Oi /EHsc /TP /GS- \
  /tmp/cal.cpp /Fo/tmp/cal.obj
strings -a /tmp/cal.obj | grep -o '?a@?[^ ]*@4US@@A' | sort -u
```

## Compute the brace-only floor FIRST

Before hunting for a construct, work out the **minimum index any legal source
shape can reach**: unbrace every body that legally permits it, collapse nested
ifs onto dangling elses, swap inverted arms. If the target's index is at or
above that floor, the answer is a brace and you should go find it. **If the
target is strictly BELOW the floor, stop — no brace can get you there**, and the
answer is a declaration the original has and we lack, a branch polarity swap
that moves the static into the other arm, or (rarely, see below) a differently
spelled macro.

## Using it

1. `python3 scripts/analysis/scope_index_census.py` — whole-build census of
   target vs our indices, grouped by enclosing function. After the 2026-08-19
   multiset fix: 1,259 functions agree and 60 do not. (The older "1,289 / 30"
   figures came from a census that kept only the last static parsed under each
   name; 33 of the 60 were hiding behind that collapse, and most of them are
   `COUNT tgt/ours=(2,1)` — a warn-once the target has and we do not.)
## Using it

1. `python3 scripts/analysis/scope_index_census.py` — whole-build census of
   target vs our indices, grouped by enclosing function. Since it started
   reading `ham_xbox_r.map`: **970 functions agree and 203 do not**, with two
   COUNT rows. Earlier figures are not comparable and should not be quoted:
   "1,259 / 60" (2026-08-19 multiset fix) and "1,289 / 30" before it were both
   measured against `symbols.txt`, i.e. against ourselves for the majority of
   rows, and their `COUNT tgt/ours=(2,1)` class was an artifact end to end.
2. For one function, decode both sides and walk the source with the table. The
   delta localises to the exact stretch between two statics.
3. Fix the source, rebuild the one `.obj`, and read the new names back with
   `strings -a <obj> | grep '??<function>'`. The obj patchers do not touch scope
   indices, so a single-object build is enough for this check (it is NOT enough
   for a match percentage — see BUILD_SYSTEM.md).
4. Confirm with `run_objdiff` that the instruction stream is untouched. **If an
   edit moves a single instruction it is the wrong edit**: these are pure
   structure, and the compiler emits the same code for braced and unbraced
   bodies, for `if (a) if (b)` and `if (a && b)`, and for a branch and its
   inversion with the arms swapped.

## What the deltas mean in practice

Every fix landed on 2026-08-17 was one of four shapes:

* **A missing brace pair.** `ClipCollide::SyncWaypoint` (+1: the early-out
  `if (!mChar || !mWaypoint) return;` is braced in the target),
  `DirLoader::FixClassName` (each rung of the rev ladder is a block, +1 each).
* **A brace pair we invented.** `IsUselessLoad` (-2: two single-statement bodies
  in the dance_battle branch are bare), `DataMergeFilter::Filter` (-1).
* **Two nested ifs that are one short-circuit condition.**
  `Automator::OnCheatInvoked` (-3).
* **An inverted branch with the arms swapped.** `Leaderboards::Text` (-2): the
  static lives in the arm that our source made the `else`. `a && b` with the
  arms one way and `!a || !b` with them the other compile to the same branch
  layout, so only the index can tell them apart.

  `KeylessHash<T1,T2>::Insert` (2026-08-19) is the largest of these so far, -10
  across all three instantiations that carry a `MILO_NOTIFY_ONCE`. Everything
  before the static is identical on both sides, but our source had

  ```c++
  if (mOwnEntries) { MILO_ASSERT(mSize, 0xB3); Resize(...); ... return Insert(val); }
  else             { MILO_NOTIFY_ONCE("Hash table half full (%d)", mSize / 2); }
  ```

  and the original had the test inverted with the arms swapped, which puts the
  notify-once block at 30 instead of 40 *and* moves `MILO_ASSERT(mSize, 0xB3)`
  after the static so its 5 stops counting. -10 is exactly two asserts' worth,
  which is a trap: the obvious reading ("we have two asserts the target lacks")
  is refuted by the instruction stream, which was already 100% with every
  instruction equal and carries all four asserts' explicit line numbers as
  immediates. The second reading ("we invented brace pairs") is refuted by
  arithmetic — unbracing every body that legally permits it, including
  collapsing the two nested `if`s onto a dangling `else`, bottoms out at 39.
  **Compute the floor of the brace-only explanation before you go looking for a
  brace.** All three bodies stayed at 212/209/201 instructions "all equal"
  afterwards; MSVC lays the arms out identically because one of them ends in a
  `return`.

## When the skew is not a scope fix at all

If the counts differ, the function is missing or inventing a *declaration*, and
the index is a symptom of ordinary decomp divergence. Do not touch braces; the
neutrality gate ("if an edit moves an instruction it is the wrong edit") does not
even apply, because the function is not matching in the first place. Two worked
examples, left open on purpose:

* **`RndTexBlender::DrawShowing`** (92.5%). Target has 3 `_dw`, we have 5. The
  relocation pairing shows the target's near-list and far-list loops both
  reference **`?_dw@?P@??DrawBlendList@...`** — i.e. the original called
  `DrawBlendList(nearList, ...)` / `DrawBlendList(farList, ...)` and the compiler
  inlined them, keeping the static's name attached to `DrawBlendList`. Our source
  hand-wrote both loops inside `DrawShowing`, which manufactures two statics that
  cannot exist in the target. The remaining three need +3 before the first
  notify, -3 between the first and second, +1 between the second and third —
  consistent with the outer `if (a && b && mOutputTextures)` being two nested
  `if`s and the two validity warnings being siblings rather than `if`/`else`.
  Note `DrawBlendList` is itself only 92.5%, and its `state != 2 ? mNearMap :
  mFarMap` disagrees with the near loop's `mRenderedStates |= 2`, so that body
  wants checking before anything is routed through it.
* **`WorldCrowd::DrawShowing`** (85.9%). Both sides have 2 `_dw`, but they are
  offset by different amounts (+8 on the collider warning, +12 on the environ
  warning) — the equal-looking 42 on each side is our *first* static against the
  target's *second*. So we are 8 scopes long before the collider loop and 4 more
  between it and the environ check; the `if (n != 0) { do { } while }` around the
  bounding-rect reduction (5) where a plain `for` would be 2 is the most likely
  three of those four.

## The one that is not brace-shaped: assert spelling

**The shipping binary contains more than one assert spelling.** That was an open
question until 2026-08-19; it is now measured.

### The population is tiny — only ~24 functions constrain the assert cost

Most of the 1,800-odd tracked statics have no assert between the function's
opening brace and their declaration, so they are insensitive to the spelling and
tell you nothing. A whole-build A/B (swap `system/os/Debug.h` to the expression
form `((cond) || (TheDebugFailer << MakeString(...), 0))`, rebuild, re-census)
isolates exactly the sensitive set:

| spelling in `Debug.h` | census pairs | match | differ |
|---|---:|---:|---:|
| `do { if (!(c)) { fail; } } while (0)` — cost 5 | 1817 | 1765 | 52 |
| `((c) \|\| (fail, 0))` — cost 0 | 1813 | 1661 | 152 |

The swap moves **109 statics in 16 functions OFF** a correct numbering and
**9 statics in 4 functions ON**. Whole-build cost of the global swap:
**−10,556 B** of matched code (an older note in this file said 7,716 B; that was
an underestimate). The bare-`if` form (cost 3) cost 8,600 B when it was tried.
So the do/while form stays as the default and always will.

Functions that require cost **5** (do/while): `Accomplishment::Configure` (24
statics), `RhythmBattlePlayer::UpdateAnimations` (22),
`Campaign::UpdateEraSongUnlockInstructions` (17), `MetagameRank::UpdateScore`
(11), `SkeletonChooser::IsSinglePlayerMode` (8),
`SkeletonIdentifier::OnMsg(SkeletonIdentifiedMsg)` (6),
`MainMenuPanel::MotdPickNextText` (4), `MetagameStats::Text` (4), plus
`MyFindClip`, `RhythmBattle::OnBeat`, `Synth360::ReleaseMic`,
`SaveLoadManager::SetState`, `HelpBarPanel::SyncToPanel`,
`UpdateFriendsListJob::OnMsg`, `MoveDir::FinalPoseStateMachine`,
`BeginMemTrackObjectName`.

### Cost 0: solved, and the fix is landed

Four functions require an assert that opens **no** lexical scope:

| function | static | ours (cost 5) | target |
|---|---|---:|---:|
| `Hmx::Object::ExportPropertyChange` | `msg` | 10 | 5 |
| `` `anonymous namespace'::MonthToken `` | `month_symbols` | 7 | 2 |
| `ShellInput::Init` | `reset_controller_mode_timeout`, `$S2` | 17 | 2 |
| `KinectSharePanel::OnPostLink` | 5 × `fb_link_*` | 12 | 7 |

In each of these the asserts are the *only* scopes before the declaration, so
the target sits below the brace-only floor and no brace can explain it.

The obvious alternative — **"the static is simply declared earlier in the
original"** — predicts every one of those target numbers exactly, and it is
**refuted by the instruction stream**:

* `ExportPropertyChange`: moving `static Message msg` above the assert gives
  index 5 *and* drops the function from 83/83 equal to **32.5%** (28 insert /
  28 delete) — the guarded init moves above the assert branch, which the target
  does not do.
* `MonthToken`: same move gives index 2 and **100% → 55.4%** — the target
  range-checks first.

MSVC does not sink a local static's guarded initialiser, so declaration position
*is* code position; you cannot trade one against the other. Whereas rewriting
the assert in the 0-scope form gives the target's index **and** a byte-identical
instruction stream (83/83, 87/87, 187/187 equal; `OnPostLink` unchanged at 263
instructions / 28 diff_arg). For `ShellInput::Init` even the guard's *name*
comes out right — `?$S2@?1??Init@ShellInput@@QAAXXZ@4IA`, digit for digit.

Positive identification of the spelling: the sibling RB3 decomp of the same Milo
engine defines it verbatim, `../rb3/src/system/os/Debug.h:89`:

```c
#define MILO_ASSERT(cond, line) \
    ((cond) || (TheDebugFailer << (MakeString(kAssertStr, __FILE__, line, #cond)), 0))
```

(`og-dc3-decomp` and `rb3-xenon` both use the do/while form, but og-dc3 shares
this tree's lineage, so it is not an independent witness.)

`MILO_ASSERT_EXPR` therefore lives beside `MILO_ASSERT` in `system/os/Debug.h`
and is used at exactly those six call sites. Whole build: matched_code
4,961,036 → 4,962,492 (**+1,456 B**), census 1765/52 → 1776/43, nine statics
fixed and zero regressed. **Do not use it anywhere else** — every new call site
needs a target scope index that demands it.

The expression form remains refuted as a *global* macro, and separately refuted
wherever the assert condition builds a temporary: in `TexLoadPanel::LoadMoggClip`
the target destroys the `String` temporary BEFORE the failure branch, which only
the statement form does.

### Cost 3: still open, and do NOT invent a third macro

Four functions reconcile at an assert costing **3** (`if (!(c)) { fail; }`):

| function | static | ours | target | asserts before | delta |
|---|---|---:|---:|---:|---:|
| `Automator::FillButtonMsg` | `button_down` | 7 | 5 | 1 | −2 |
| `CampaignMqCrewProvider::UpdateList` | `mq_difficulty` | 12 | 8 | 2 | −4 |
| `CampaignMqCrewProvider::Text` | `mq_difficulty`, `stars_fraction` | 23 | 19 | 2 | −4 |
| `CampaignSongProvider::Text` | `tan_battle_song` | 23 | 19 | 2 | −4 |
| | `campaign_song_locked` | 25 | 21 | 2 | −4 |
| | `song_select_song_prefix` | 41 | 39 | 2 | **−2** |

A fifth joined them on 2026-08-19 once the census read the linker map:
`PreloadPanel::OnMsg(const UITransitionCompleteMsg &)`, whose three `msg`
statics sit at 10/12/20 against the map's 8/10/16 — flat −2 on the first two and
−4 on the third, exactly its two `MILO_ASSERT`s at 3 instead of 5. Its 183
instructions are all equal, so as with the rest there is nothing to fix in the
code, only in the numbering.

All four are zero-mismatch on instructions (146, 135, 142, 187, all equal), and
all four are below their brace-only floor at cost 5 (`FillButtonMsg` floor 7 vs
target 5; `UpdateList` 12 vs 8; `CampaignMqCrewProvider::Text` 21 vs 19 even
with both single-statement arms unbraced), so this is not a brace either. And
the first five rows land *exactly* at cost 3.

But it is not landed, for two reasons:

1. **No witness.** The bare-`if` spelling exists in no sibling decomp. The
   cost-0 form was landed only because rb3 has it verbatim; there is nothing
   equivalent here.
2. **It is not even self-consistent.** `CampaignSongProvider::Text`'s last
   static needs a further **+2** on top of cost 3. Rewriting its two `else if`
   rungs as `else { if ... }` supplies exactly that — measured 2026-08-19:
   `song_select_song_prefix` 41 → 43, the two earlier statics unmoved, and the
   function still 187/187 instructions equal, so the rewrite is genuinely
   code-neutral. But it only helps *in combination with* cost-3 asserts (alone
   it moves 41 → 43 away from the target's 39), and the sibling
   `CampaignMqCrewProvider::Text` must *not* have the rewrite or its statics
   overshoot. So the combined story needs a per-file brace style as well as a
   per-file macro, which is two unwitnessed assumptions to buy four rows.

Enough to keep the row on the worklist; nowhere near enough to add a macro.

### The atexit `??__F` names are NOT a second probe

`??__F<name>@?<scope>??<fn>@YAXXZ` looks like an independent reading of the same
block structure. It is not usable as one:

* Our own objects' `??__F` names are **rewritten to the target's values** by
  `scripts/obj_atexit_scope_patcher.py`, which runs as a build step. Reading
  them out of `build/373307D9/src/**/*.obj` after a full build and comparing
  against the target is circular. (A single-object `ninja build/.../X.obj`
  skips the patcher, so the same file gives different answers depending on how
  you built it — which is how this trap was found.)
* On raw compiler output the thunk index always equals the variable's, in every
  synthetic shape tested. In the *target*, 20 of 161 pairs disagree, including
  `ExportPropertyChange` where the variable reads 5 and the thunk reads 8.
* **Never fold target `??__F` scopes into the target's variable list** to
  "enumerate statics". Because the two indices can differ, that manufactures a
  phantom extra target static and makes a single static look like two. It did
  exactly that for `ExportPropertyChange`, which has one `static Message msg`
  on both sides.

A census must compare `?<name>@?<scope>??<fn>` on both sides only, and must
compare **lists** per name — several `_dw`, `_s`, `msg` or `$S<n>` statics can
share a name inside one function, and keying `name -> int` silently compares two
unrelated statics.
