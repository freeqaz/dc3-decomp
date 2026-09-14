# Wave 6 — three instrument defects, and the bugs a perfect score cannot see

**Date:** 2026-09-14
**Method:** nine lanes (`w6-a`..`w6-i`) in CoW worktrees, coordinator rebases, merges `--no-ff`, gates natively.
**Headline:** 30,962 → **30,967** functions, 5,419,160 → **5,427,300** B, 47.641537% → **47.713097%** — +5 functions, **+8,140 bytes**, +0.07156 pp.

> **Read the headline last.** The wave's actual output is a set of behavioural
> defects that no ruler in this project can see, and three defects in the
> measuring instruments themselves — one of which was reporting this wave's own
> work as *"0 improvements, 48373 unchanged, +0.00%"*.

## Crossings

| function | unit | bytes | before → after | lane |
|---|---|---:|---|---|
| `RndFont::Load` | `rndobj/Font` | 2,748 | 99.99418 → **100.0** (687/687 equal) | w6-c |
| `UILabel::PreLoad` | `ui/UILabel` | 2,820 | 99.77305 → **100.0** (705/705 equal) | w6-c |
| `Campaign::ConfigureCampaignData` | `meta_ham/Campaign` | 2,572 | 99.751 → **100.0** (643/643 equal, `name_check` too) | w6-h |
| `Spotlight::UpdateTransforms` | `world/Spotlight` | 1,456 | 91.956 → **100.0** (3 register rows) | w6-i |
| `RndShadowMap::PrepShadow` | `rndobj/ShadowMap` | 1,072 | 93.388 → **100.0** | w6-i |

⚠ **The five crossings total 10,668 bytes of function size, but `matched_code`
moved only +8,140.** The difference is exactly the last two rows (1,456 + 1,072 =
2,528). This is the **ruler split**: `matched_functions` is credited on
`match_percent_normalized` (canonical), while `matched_code` is credited on
`fuzzy_match_percent`. `Spotlight::UpdateTransforms` reads canonical 100.0 with
fuzzy 99.94505, and `RndShadowMap::PrepShadow` canonical 100.0 with fuzzy
99.88806 — their residual register-permutation and commutative-operand rows are
forgiven by the canonical ruler and charged by the fuzzy one. **A function can
cross to 100 and add zero bytes.** Quote the measured report delta, never the sum
of function sizes.

⚠ The first three are **zero-mismatch** hundreds. The last two are **canonical**
hundreds carrying register-permutation and commutative-operand rows, which the
normalized ruler forgives by design — say "100 modulo register permutation", not
"byte-identical". Both are nonetheless exactly `100.0` in `match_percent_normalized`,
which does not round.

### Big non-crossing moves

| function | bytes | before → after | lane |
|---|---:|---|---|
| `RndTexBlender::DrawShowing` | 2,636 | 95.343 → **99.514** | w6-g |
| `CharClip::LockAndDelete` | — | 88.446 → **96.486** | w6-f |
| `CharEyes::LidTrackAndClampingUpdate` | 2,652 | 96.235 → **98.350** | w6-d |
| `ParseStatusCode` | — | 85.9 → **87.9** | w6-e |

---

## 1. The instruments were wrong three times

This is now the dominant failure mode of the project and it deserves the top of
the write-up rather than a footnote.

### 1.1 `measure_progress.sh --functions` hid every crossing in the 99.5–100 band

Found by lane **w6-c**, which crossed two functions to exactly 100.0 and then
watched the tool report `0 improvements, 48373 unchanged, +0.00%` over its own
work.

`compare_functions` took `min_diff: float = 0.5`; its only caller never passed
it; there was no CLI override. **In the 99.5–100 band every crossing moves the
percent by less than 0.5 by construction**, so the entire band was silently
bucketed as `unchanged`. Both `report.json` files were correct on disk — the
*comparison* discarded the result. The only trace was the coverage block's
`unchanged_within_min_diff` count, which does not say a crossing happened.

**Fix:** arriving at or leaving 100 is now always reported at any threshold — a
threshold is a claim about *magnitude*, while crossing 100 is *categorical* — and
`--min-diff` is a real flag. Guarded by
`scripts/analysis/tests/test_compare_progress_min_diff.py`, 5 cases, non-vacuous
in three directions (an ordinary sub-threshold move must still be filtered, so
"report everything" cannot pass; a supra-threshold move must be reported, so an
empty list cannot pass; `--min-diff` must move the bar both ways while crossings
stay exempt). Sabotage-verified: restoring the old line drops the crossing and
the test catches it.

**Consequence worth chasing:** any lane in any previous wave that confirmed its
own work with this tool may have wrongly concluded it achieved nothing.

### 1.2 `query_functions(objdiff_pattern=…)` is still answered unfiltered

Reported by lane **w6-e**, and confirmed independently: the `query_functions`
tool *schema being served* carries no `objdiff_pattern` field at all, while
`scripts/orchestrator/mcp_server.py` has had it since `ad5916597` (2026-09-11).
The running MCP server predates that commit and holds the old module. A lane's
first call returned 200 unrelated functions including `Timer::SplitMs`.

This is the documented "a running MCP server holds the old module" condition.
**It needs a server restart; nothing in the repo can fix it.** Until then, read
`decomp.db` directly, which is what w6-e did.

### 1.3 A guard that failed open — in the fix written this same session

`setup_worktree.sh` gained a warning for a worktree created *inside* the main
checkout. The first version resolved the path with `cd "$(dirname …)" && pwd`,
which **fails when the parent does not exist yet** and then fell back to
comparing an unresolved relative path that can never match `"$MAIN_REPO"/*`. It
printed nothing for exactly the case it exists to catch. Its own test found it;
`realpath -m` fixed it.

Three defects, one session, all the same shape: **a broken measurement and a
clean result look identical.**

---

## 2. Behavioural defects — the real output

None of these moved the headline by more than a rounding error. All are things a
player could have experienced.

| defect | function | how it hid |
|---|---|---|
| **The clips were never destroyed** | `CharClip::LockAndDelete` | `if ((unsigned int)clip) clip->Release((ObjRef *)1);` inlined an ObjRef ring unlink where the image does a plain `delete`. The `(unsigned int)` cast was doing the opposite of what it looks like. |
| **The assert tested the wrong parameter** | `CharClip::LockAndDelete` | `MILO_ASSERT` stringifies its condition, and the image's string `"remaining >= 0"` matched ours *exactly* — while the image compiles it against the **third** parameter (`cmpwi cr6, r5, 0x0`) and we tested the second. The code was right and the **names** were swapped. |
| **Font cell size transposed** | `RndFont::Load` | At `rev < 4` the target divides `Width()` by the **second** stream float and `Height()` by the **first**; we did the opposite. On any non-square font page the cell dimensions were swapped. Our two divisor *slots* were already right, so no ruler could see it. |
| **A null-check we invented** | `CharEyes::LidTrackAndClampingUpdate` | `if (graph && !lidsOK) {…} else { graph->AddSphere(…) }` — the `else` arm dereferences `graph` regardless, so the guard never guarded, and the image tests `GetOneFrame()`'s result nowhere. Reads as an improvement in review; a latent null-deref natively. |
| **An eye-velocity accumulator never reset** | `CharEyes::Enter` | `mAvDelta` was never zeroed, leaking the previous take's eye velocity into a freshly entered character. Found by counting the store **set**, not the score: both sides emit 21 stores, so the count hid it. |
| **Two wrong template instantiations** | `DataMinerJobs` | `MakeString<unsigned long>` where the target has `<unsigned int>` (`DWORD` *is* `unsigned long`), and `<unsigned long, int>` where the target has `<int*, int>`. The second is structural: `ABQAH` is `int * const &`, which only comes from `T1 = int*`. |

### The verification that matters

For `CharClip::LockAndDelete` both claims were checked against the target's own
assembly rather than taken on argument:

```
cmpwi  cr6, r5, 0x0      ; the assert tests r5 — the THIRD parameter
...
cmplwi cr6, r3, 0x0      ; UNSIGNED null test
lwz    r11, 0x0(r3)      ; vtable pointer
li     r4, 0x1           ; "also free the memory"
lwz    r11, 0x0(r11)     ; vtable slot 0 — scalar deleting destructor
bctrl
```

That sequence *is* MSVC's `delete` on a polymorphic type. Independently
corroborated by the sole caller, `CharClipGroup.cpp:178`, which passes
`mClips.size()` as the second argument — so the second is the clip count and the
third the delete budget, exactly as the assert implies.

---

## 3. Floors proven, not asserted

**`RhythmBattle::OnBeat`** (16,508 B — the largest non-XDK function under 100,
at 99.44997) is **one stack word**. Both sides reference exactly **203 distinct
`r31` slots**, so there is no extra *variable*, only an extra *word* (target 202,
base 203). The target's only multi-byte word is `0x8c` holding `{0x8c, 0x8d}`,
which `/FAs` names `inMindControl` and `goofy`; our build has zero multi-byte
words. The 470-row offset residual cascades arithmetically from that one word:
base pads `0xe4` to 8-align the `DataNode` pair, then again to 16-align the
`Vector3` block at `0x350` — the `0x760`-vs-`0x750` frame delta. Histogram
`+8 ×327, +4 ×71, +16 ×41, 0 ×117`. **Making the two bools lexically adjacent
does not pack them.**

The lever is now refused by **~37 distinct spellings across two lanes**.

**`Multiply(Vector3, Matrix3, Vector3)` is left-associated in the original**, and
that is settled: right-associating it costs five functions whose entire body is
one inlined copy their perfect scores (`Multiply(Transform,Matrix3,Transform)`
100 → 91.56, `MultiplyInverse` 100 → 92.69, `CharServoBone::MoveToDeltaFacing`
100 → 92.43, `GetLightPosition` 100 → 93.61, `DrawBounds` 100 → 96.74), for a
whole-binary net of **5 up, 14 down, −800 B**.

### …and the hand-off that was right about the site and wrong about the mechanism

Lane `w6-b` found two functions that *gained* under right-association, correctly
refused the header edit, and handed over the lead framed as an **association**
problem. Lane `w6-i` crossed both and found that framing was wrong — which is
what unlocked the last four rows.

`PrepShadow`'s y and z components are **left-associated in the target** (they seed
from `m.x.c`) and are *still not factored*; only x seeds from `m.z.c`. The actual
lever is MSVC's `/fp:fast` **distributive** step —
`m.x.c*0 + m.y.c*v.y + m.z.c*0` → `(m.x.c + m.z.c)*0 + …` — whose fingerprint is
**a leading `fadds` whose two operands are both matrix elements**. Right-association
merely happened to break that pattern.

The fix is **per-component accumulator statements** (`float a = …; a += …;`):
MSVC will not reassociate across a `+=`, so statement order pins the association
and each row seeds from the element the target seeds from. Three spellings
measured on `PrepShadow`: uniform right-association **99.98508**, left-associated
y/z as one expression **94.66418** (the factoring returns), target-seeded
accumulators **100.0**.

**The control that locates the lever:** spelling the header's own left-associated
tree out at the `Spotlight` call site scores **91.95605** — the baseline to five
decimals. Call-site context is irrelevant; only the **expression tree shape**
matters. So both sites are fixable locally and the four callers already at 100%
are untouched.

> Generalisable: a hand-off should carry the **measurements**, not the
> **explanation**. `w6-b`'s numbers were all correct and reproducible; its causal
> story was not, and a lane that had accepted the story would have stopped at
> 99.985.

---

## 4. Stale "at limit" tables are worse than no tables

The 2026-03-06 TexBlender session doc listed `DrawShowing`'s residual as a floor:
*register swaps, stack frame +8, address relocations, static guard counters,
vtable caching*. Lane **w6-g** took the function **95.343 → 99.514** and
corrected that table per row rather than deleting it:

- *"Stack frame +8 — compiler stack layout"* → **refuted**; it was a missing
  early return. The shipped build early-returns where we wrapped the body in an
  `if`, and the target's false path carries its own copy of the three vectors'
  destructor sequence instead of branching to the common exit (17 of 24 delete
  rows). De Morgan'ing the condition collapsed the whole stack cascade in one
  edit.
- *"Static guard counters — TU definition order"* → **moot**; all three ordinals
  match.
- *"TheShaderMgr vtable caching"* → **misdiagnosed**. Both sides hoist the vtable
  load above the `Matrix4` ctor; only the register holding the **object pointer**
  differs.

> An AT_LIMIT table written at 88.6% was still telling lanes to give up at 95.3%.
> **A floor claim must carry the percentage it was taken at**, or it rots into a
> false prohibition.

---

## 5. Coordination defects (mine)

Recorded because they cost lane budget, and the lanes reported them rather than
absorbing them.

1. **I briefed a lane onto a documented floor without naming the document.**
   `w6-a` spent seven variants re-deriving `RhythmBattle::OnBeat`'s one-word
   floor, which `w3-s2` had already settled across 30+ variants in
   `docs/decomp/patterns/stack-slot-sharing.md`. The brief now mandates a prior-art
   grep before the first measurement, and requires a lane that refutes a lever to
   write it into the **pattern doc**, not only its commit message.

   ⚠ **And the rule as first written was itself incomplete.** It searched only
   `docs/`. Lane `w6-i` reported the same day that `grep -rl` over
   `docs/decomp/patterns/` returned **nothing** for either of its functions, and
   the prior art that mattered was a comment about `Hmx::Dot4` **inside
   `src/system/math/Mtx.h`**, found only by reading the header. A large share of
   this project's compiler knowledge lives in source comments beside the code
   they explain — exactly where they help a reader and exactly where a docs
   search cannot see them. The rule now includes `src/` and says to read the
   header of any inlined helper.
2. **I gave a mangled name that exists in no object** (`GameEndedDataPointJob`'s
   ctor). `run_objdiff`'s not-found message recovered the real one — good tooling
   covering for a bad brief.
3. **I entangled two lanes' branches.** I reused lane `w6-e`'s worktree for lane
   `w6-h` while `w6-e` was still resumable, so when resumed it committed onto
   `w6-h`'s branch. Since that is a live working surface it was **not** rewritten;
   the lane was warned instead and attribution handled in the merge message.
4. **I let the native gate race the merges, twice.** A gate started before three
   more lanes landed reports on a tree that no longer exists. The rule is one
   authoritative gate *after* all merges.

---

## 6. A probe that was invalid, not a refutation

Lane `w6-g` noticed the target's `DataMinerJobs.obj` carries
`MakeString<char[14], int, char[5]>` where ours has `char[18]` — a 13-character
`__FILE__` against our 17-character `DataMinerJobs.cpp`, and `DataMiner.cpp` is
exactly 13. If the original file had a different name it would charge a
relocation name on every `MILO_ASSERT` in the unit.

The obvious cheap test — compare the **first** `MakeString<char[N],…>` in each
target object against that object's filename length — was run over all 562
qualifying units and **is not a valid probe**. Only 45 agree, the implied name
lengths are absurd (0 characters, 53 characters), and a large cluster implies
exactly 18 across `App.cpp`, `Geo.cpp`, `mtx.cpp`, `Key.cpp`, `Dir.cpp` and
`Utl.cpp` alike — different filenames cannot all imply 18. These are COMDATs and
dtk assigns them from any TU, so the first one in an object is simply not that
object's assert `__FILE__`.

**The lead is still open.** A valid probe must anchor on a `MILO_ASSERT` call
site inside a known function in the unit.
