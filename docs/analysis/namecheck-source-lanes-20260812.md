# dc3's remaining `name_check` SOURCE defects: what each of the three lanes is

2026-08-12. Follow-on to `namecheck-residency-split-20260812/`. The alias,
patcher and ruler lanes took dc3's `name_check`-exposed population from 1,101
functions to 320; the question here is how much of what is left is our source
actually differing from retail's, in the three lanes assigned that label:
`different_function`, `local_static_moved_fn`, `template_sibling`.

Answer: **two of the three are source lanes and one is not**, and the profitable
sub-shape inside the source lanes is much narrower — and much more mechanical —
than the lane totals suggest.

## The instrument: the shipped linker map, not map residency

The previous lane bucketed pairs by looking both charged names up in
`scripts/target_symbol_map.json`. That was refuted on the sibling project: the
map is a **VA→name function** over an ICF-folded link, so a spelling that lost
the fold vote is simply absent, and "both mapped at different addresses" is not
evidence of anything.

`orig/373307D9/ham_xbox_r.map` — the MSVC linker map that made the image — does
not have that defect. It prints **one line per symbol**, and every member of a
fold set prints the same address:

    0005:000014d8  ??$MakeString@PAD@@YAPBDPBDABQAD@Z  823314d8  obj:DataFile.obj
    0005:000014d8  ??$MakeString@PBD@@YAPBDPBDABQBD@Z  823314d8  App.obj

118,000 names against `target_symbol_map.json`'s 69,132, and it states folds
outright. Every verdict below is read off it.

| pair verdict | `different_function` | `template_sibling` |
|---|--:|--:|
| retail shipped BOTH bodies (distinct addresses) | 57 | 6 |
| ours absent from the map | 18 | 3 |
| FOLDED at one address (⇒ alias lane, not ours) | 2 | 3 |
| target absent / neither present | 9 | 0 |

So `different_function` really is a source lane: only 2 of 86 pairs are folds.
Its two largest pairs by site count are not, though — `SUBSAMPLED_HALF_WIDTH`
vs `__real@42200000` (8 sites) is a *data* COMDAT fold at 8201ffcc, and the
`??_B?BD@??Text@CampaignSongProvider…` pair (8 sites) is a local-static guard
the lane's `isfn` heuristic ("contains `@Z`") mis-filed as a function.

Re-splitting all 140 `different_function` sites on the mangling:

| | sites | pairs |
|---|--:|--:|
| genuine function vs function | 92 | 75 |
| both sides function-local statics, SAME enclosing function | 33 | 8 |
| one side a local static or an fp constant | 11 | 2 |
| both local statics, different enclosing function | 4 | 1 |

## `local_static_moved_fn` is NOT a source lane

All **50** of its solo-lane functions are placeholder-named (`fn_XXXXXXXX`),
1,600 bytes in total, average 32 bytes. Every one is the same shape — the
guard-clear thunk that MSVC emits beside a function-local static:

    stwu r1, -0x60, r1
    lis  r11, ??_B?1??StaticClassName@RndLightAnim@@SA?AVSymbol@@XZ@51   <- target
    lwz  r11, …                                                          | ours names
    clrrwi r11, r11, 1                                                   | RndFlare
    …
    blr

`fn_8265C730` sits immediately after `?StaticClassName@RndLightAnim@@…` in
`config/373307D9/symbols.txt`, so it is that class's thunk. Ours is at a
different offset and gets paired with it positionally.

**49 of the 54 charged retail-side guards are defined by our own object, in the
same translation unit.** Our source is not missing them and is not putting them
in the wrong function; objdiff is pairing interchangeable anonymous thunks by
position. There is no name-level repair either: dumping `Rnd.obj`'s COFF symbol
table shows our thunks carry no symbol of their own (only `__unwind$…` and
`$M…` neighbours), so naming the target side in `symbols.txt` cannot help. The
repair, if anyone wants these 1,600 bytes, is in objdiff's matcher — pair a
thunk by the guard it references, not by its position.

The five exceptions are real and are two different things:

* three are the guard **spelling** difference in the SAME function — retail
  `??_B?<ord>??fn@5<ord>` (a dedicated word per static) against our
  `?$S<n>@?<ord>??fn@4IA` (one word whose bits are one-per-static; visible in
  `compare_deferred_points` as `ori r11,r11,0x1` then `ori r11,r11,0x2`). Their
  scope ordinals differ too, so this sub-shape belongs to the deliberately-open
  `local_static_scope_ordinal` question, not here.
* two are `??_B?1??Type@InviteAcceptedMsg@@…` and `…PartyMembersChangedMsg…`,
  absent from our `PlatformMgr_Xbox.obj` — genuinely missing message classes.

A note for whoever re-runs the triage: `namecheck_triage.py`'s `LOCAL_GUARD`
rule ends the enclosing function at `@(\d+)$` with a non-greedy body, so
`…@Z@5` and `…@Z@4IA` split at different places and two symbols in the *same*
function can compare as different functions. It only costs 3 pairs here (9
sites), but it is a real mis-split; terminate the function at its own `@Z`/`XZ`.

## `template_sibling` is a MakeString ARGUMENT-TYPE lane, and it is free to fix

9 of dc3's 12 pairs are `MakeString<T>`. `MakeString<T>(const char *, const T &)`
mangles T into the callee, so the relocation names the argument type outright,
and the linker map says which T's are interchangeable:

    823b2008   void*  ==  unsigned int  ==  unsigned long  ==  long  == …
    82610090   int    ==  enum
    823314d8   char*  ==  const char*

The repair costs no bytes, and the disassembly says why: every one of these
sites already materialises a **one-word stack temporary** and passes its
address —

    lwz  r11, 0x30, r11      ; the Symbol's mStr
    stw  r11, 0x50, r1       ; -> temp slot
    addi r4, r1, 0x50        ; &temp
    bl   MakeString<…>

— and a `Symbol` temp, a `const char *` temp and an `unsigned` temp are the
same word in the same slot. Spelling the argument `.Str()`, or declaring a
named local `const char *` instead of `Symbol`, or dropping a decompiler's
`(int)` cast off a `size()`, changes the callee and nothing else.

**This does not transfer to rb3-xenon as hoped.** Its 483 `template_sibling`
sites are only 17 `MakeString`; the bulk is STL algorithm templates
(`__uninitialized_copy` 81, `_Destroy_Range` 48, `_M_find` 43, `DeleteAll` 36),
i.e. container **element**-type mismatches, which are a structural repair and
not an argument respelling.

## What the residual `different_function` needs

Of the 75 genuine function-vs-function pairs, roughly 30 are destructors of
unrelated classes (`??1String` vs `??1DataNode`, `??1Object@Hmx` vs
`??1UIListWidget`) and roughly 12 are class-scoped `operator delete`
(`??3FxSendDistortion` vs `??3FxSendReverb`). The tempting reading is that these
are ICF folds. **They are not** — the linker map has them at distinct addresses
(`??3FlowNode` 823eb968, `??3PropertyEventProvider` 823ebcc8, same object), so
the pooled deletes and the destructors really do have different bodies and we
really do call the wrong one.

Each is a single site inside an inlined destructor or delete chain, and the
repair is to work out which temporary's lifetime ends there and give it the
right static type. Identification is mechanical and complete; the repair is
per-site archaeology with no shared key. That is the honest description of the
~60 pairs this lane did not touch: not a batch, sixty separate small readings.

The tractable sub-shape, and the one every landed edit here came from, is the
pair whose two manglings differ in something the mangling itself names —
argument type, cv-qualification, access region, template argument. `Vector3`'s
const `operator[]` and `UILabel::Style`'s const overload are the whole of that
sub-shape left in dc3 after this lane.

## Landed, and what each cost

Pinned `objdiff-cli-B` (objdiff main 745b7e3) throughout, one `-o` path per arm
with the `.cache` sidecar purged, rendered alias map regenerated before every
measurement.

| wave | name_check | fns | `none` |
|---|---|---|---|
| baseline | 42.635746% (4,849,144) | — | 43.730507% / 4,973,656 |
| MILO_WARN → MILO_NOTIFY ×5 | 42.660540% | +5 −0 | unmoved |
| MakeString Symbol → char* ×10 | 42.677420% | +6 −0 | unmoved |
| six wrong-callee sites | 42.705450% | +6 −0 | unmoved |
| three int/unsigned arguments | 42.714455% | +3 −0 | unmoved |
| **total** | **+0.078709 pp, +8,952 bytes** | **+20 −0** | **byte-identical** |

`none` is 43.730507% / 4,973,656 bytes at every checkpoint, and none_guard's
28,677-name `??_C@` fingerprint never moved.

Re-running the triage against the landed tree: **320 → 300 exposed functions,
124,512 → 115,560 bytes.** Per lane, before → after:

| lane | sites | pairs | only-lane fns |
|---|---|---|---|
| `template_sibling` | 25 → **8** | 12 → 8 | 17 → **5** |
| `different_function` | 140 → 132 | 86 → 82 | 86 → 79 |
| `local_static_moved_fn` | 211 → 211 | 54 → 54 | 50 → 50 |

`template_sibling` is effectively finished: what remains is the 3 fold pairs
(alias lane) and a handful of STL siblings. `local_static_moved_fn` is
unchanged by construction — nothing in source was wrong with it.

### Two things that were tried and cost bytes

**Hoisting a const view out of a loop is not the same edit as taking it at the
use.** `HamListRibbon::GetLabelTotalAlpha` needs the const `UILabel::Style`
overload. Writing

    const UILabel *placeholder = mLabelPlaceholder;
    for (unsigned i = 0; i < placeholder->NumStyles(); i++)

cost `none` 128 bytes and dropped the function from 100 to 73.28 — the original
reloads the `ObjPtr` through the loop condition every iteration. Declaring the
same local *inside* the loop body keeps the reload and is byte-free.

**Two macros in the same header are not interchangeable.**
`HamDirector::LoadRoutineBuilderData` is charged `?Notify@Debug@@QAAXPBD@Z`
against `??6TextStream@@QAAAAV0@PBD@Z`, which reads exactly like the five
`MILO_WARN`→`MILO_NOTIFY` sites that landed clean. Those were `Debug::Warn` and
`Debug::Notify` — two methods of one class reached through the same `TheDebug`
global, so the caller's instruction stream is identical and only the relocation
moves. This one crosses `TextStream` to `Debug`: `MILO_LOG` is
`TheDebug << …` and the streams differ. `none` fell to 99.957085 (−932 bytes).
Reverted.
