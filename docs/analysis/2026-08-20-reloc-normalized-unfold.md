# Making the canonical metric see wrong callees (2026-08-20)

**Repo: dc3-decomp** (Dance Central 3, Xbox 360, MSVC PPC, title `373307D9`).
The fix lives in the shared objdiff fork at `../objdiff`, branch
`fix/namecheck-reloc-normalized`.

## The question this answers

The standing description of the problem ended with *"no flag turns it off"*,
which read as a law of nature. It was not. It was a scoring decision in a fork
we own, and the reason `name_check` "wasn't good enough" turns out to have
nothing to do with `name_check`'s **detection**, which was already excellent.

## Where the blindness actually lived

```
match_percent_normalized = diff_score − arg_diff_score
```

When two arguments differ, `diff_instruction` charges a penalty to `diff_score`,
and then charges it *again* to `arg_diff_score` for every argument that is not
an immediate. A relocation operand is not an immediate. So for relocations the
two terms cancel **exactly**, and the canonical metric could not move no matter
how wrong the callee was.

`reloc_eq` — the detector — was correct the whole time. It has careful,
well-earned exemptions: placeholder split names (`fn_8xxxxxxx`, `lbl_*`,
`jumptable_*`, `_bss_*`), MSVC `$`-labels with nondeterministic suffixes,
interior self-reference for switch dispatch, counter-suffixed literals compared
by **content** rather than name, and a missing target-side relocation treated as
unverifiable rather than as evidence. It found the wrong callees. The score then
threw the finding away one function later.

**This is why 102 unit tests were green while the metric was blind: every one of
them tested `reloc_eq`.** Not one drove the scoring path end to end.

## The fix

Under `name_check` — and only there — a relocation-name disagreement that
survives all of `reloc_eq`'s exemptions stays in `diff_score` and is not folded
into `arg_diff_score`.

The fold's own comment justified itself with *"reloc diffs are dominated by
benign noise"*, citing an audit run on rb3-decomp. That premise is true for the
other modes and false for `name_check`, whose entire purpose is to remove that
noise before scoring.

Scoped deliberately to the Reloc/Reloc path. A shape mismatch — one side a
relocation, the other a constant — measures how completely dtk attributed
addresses when it split the target, not our source, and stays folded.

## Three carve-outs, measured rather than assumed

The first draft charged **1,537 sites across 611 functions**, and **223 of those
functions were charged on nothing but noise**. Shipping it would have traded one
lying instrument for another. Adjudicating the actual name pairs produced three
exemptions:

| carve-out | why | scale |
|---|---|---|
| Register save/restore helpers (`__savegprlr_25` vs `_26`) | The suffix is the first register the prologue spills, so *which* helper is called is decided entirely by register allocation. Normalization exists to forgive register allocation. `fuzzy` still charges it, correctly. | 467 sites / 226 fns |
| Placeholder-named **enclosing** symbol | MSVC EH funclets split as `fn_<addr>` and objdiff pairs them **by byte signature** — a heuristic. One pair matched `RndRibbon`'s static guard against `RndFont`'s: two different classes, paired because their code shape coincided. Charging names there measures the pairing. | 204 of 226 guard sites; 89 of 213 wrong-symbol sites |
| Function-local static scope ordinal (`?BD@` vs `?BH@`) | A per-TU counter that moves whenever anything earlier in the file moves — the same shape `counter_named_data_eq` already exempts for mwcc. Only when the variable **and** enclosing function are otherwise identical. | 8 fns |

That last one has a matching **positive** control in the test suite: the same
name shape with a *different enclosing function* must still be charged, so the
exemption cannot degrade into "ignore anything that looks like a local static".

## Whole-binary A/B

Both reports generated from the **same prebuilt objects**, so the only variable
is the instrument.

```
denominator                     48,344 functions
normalized DROPPED                 328
normalized RAISED                    0     (the change can only ever charge more)
left the matched set                54     (19,288 bytes)
headline                       -0.1117pp
fuzzy_match_percent           IDENTICAL on all 48,344
```

**The fuzzy column is the built-in control.** `fuzzy` is computed from
`diff_score`, which this change does not touch; if it had moved anywhere, the
change did something unintended and the run would be void.

## Adjudicating all 54 that left the matched set

52 have at least one real charged site. **Zero are charged on noise alone.**
Full per-site data: `reloc-normalized-charged-sites-20260820.json`.

Real bugs this exposes, none of which were visible before:

- **`??_DAppLabel@@QAAXXZ`** — the target calls `??1HamLabel@@UAA@XZ`; we
  called `??1AppLabel@@UAA@XZ`. `AppLabel` declared no destructor at all in the
  original: `??1AppLabel@@UAA@XZ` appears in **no** `ham_xbox_r.map` entry,
  `??1HamLabel@@UAA@XZ` is at `82517e00`, and slot 0 of
  `??_7AppLabel@@6BObject@Hmx@@@` is `??_EHamLabel@@...`. Fixed by deleting the
  spurious `virtual ~AppLabel();` — 27 instructions / 0 mismatches under
  `name_check`.
  *(An earlier revision of this document stated the direction backwards. Acting
  on that would have made the row worse, not better.)*
- ~~`GatherObjectsFromGroup<RndMesh>` RTTI descriptors~~ — **withdrawn, twice.**
  First reported as one wrong type descriptor, then as a 3-cycle implying a
  different `dynamic_cast` order. It is neither: the same three descriptors
  reach the same three **final** registers (`Object@Hmx`→r28,
  `WorldInstance`→r27, `RndMesh`→r25), and only the emission order of six
  `lis`/`addi` pairs differs. A hoist-block artifact. `GotoFirstScreen` is the
  same, and `RunWithoutDebugging`'s block is *shifted* rather than permuted, so
  it is not adjudicable at all.
- **Three wrong float constants.** MSVC names float-literal COMDATs by their hex
  value, so `__real@3ecccccc` vs `__real@3f19999a` is **0.4 vs 0.6**. The fold's
  own comment claimed it preserved wrong-constant bugs "because those are
  immediates" — on PPC a float constant is *not* an immediate, it is a
  relocation to a named literal, so that class was being masked by the very
  mechanism documented as protecting it.

## The limit: a charge is only as good as the instruction pairing under it

objdiff aligns two instruction streams before it compares operands. Where a
function already matches well that alignment is trustworthy; where it does not,
objdiff is comparing rows it merely lined up, and a relocation-name difference
between two rows that are not really the same instruction is **manufactured
signal**. The relocation lane put the practical threshold at a blind score of
about **95**, and found that of 508 population rows only ~20 are adjudicable at
all (7 at 100, 13 at 99.5–100, 44 at 95–99.5, **142 below 95**).

Banding the 328 functions this change charges, by their score BEFORE the change:

| band | functions | bytes |
|---|---:|---:|
| `== 100` — pairing essentially perfect | **54** | 19,288 |
| 99.5–100 | 7 | 11,304 |
| 95–99.5 | 74 | 99,720 |
| **below 95 — pairing unreliable** | **193** | 160,248 |

**The consequential effect is confined to the safe band by construction.** All
54 functions that cross the matched boundary sit at exactly 100 beforehand,
which is precisely the condition under which the alignment is sound. Below 95 a
charge only nudges an already-imperfect number a little lower; it cannot change
matched status, and no headline figure depends on it.

What that does NOT license is reading the per-function delta as a bug report.
For the 193 below-95 rows the delta is not evidence of a wrong callee, and
anyone mining this change for leads should band by prior score first. Gating the
un-fold on a score threshold was considered and rejected as circular — the score
is a function of the charge — so the limit is documented rather than enforced.

## A known FALSE-POSITIVE class: invented decorated names in `symbols.txt`

`WorldCrowd::Mats` references `gImpostorMat` where the target side shows
`?gImpostorMat@@3PAVRndMat@@A`, which looks exactly like the `createFilter`
linkage bug. **It is not.** `gImpostorMat` is `static RndMat *gImpostorMat` at
file scope in `Crowd.cpp` — internal linkage, which MSVC does not decorate.

The decisive check is the linker map: `?gImpostorMat@@3PAVRndMat@@A` appears
**zero** times in `ham_xbox_r.map`. The only `gImpostorMat` there is inside a
string-literal COMDAT name — `??_C@_0BK@CDFKKHOA@?$CBgImpostorMat?9?$DONextPass?$CI?$CJ?$AA@`,
i.e. the text of an assert expression. An externally-linked global would be
present; the independent control is `os/System.cpp`, where our object emits bare
`gSystemMs` beside decorated `?gHostConfig@@3_NA` and `?gHostFile@@3PBDB`, and
`?gSystemMs@@3HA` is likewise absent from the map.

So the decorated spelling exists only because `config/373307D9/symbols.txt`
**invented** it. No source change closes these rows — the target side is being
relabelled. This is the same family as the `??_G`/`??_E` access-specifier defect
(both sides told the same wrong name), except here the config manufactures a
divergence rather than hiding one, so the new ruler charges a config defect as
though it were our bug.

Binary-wide this is exactly **2 pairs**. Not worth an exemption, but worth
knowing before anyone "fixes" a linkage declaration that was already correct.

## Not exempted, deliberately: `__FILE__`

16 functions are charged because the target embeds the original absolute build
path (`e:/lazer_build_gmc1/system/src/utl/trie.cpp`) in its assert strings while
we embed a bare `trie.cpp`. That is a **real difference in the binary**, and it
is fixable — by compiling with the original path prefix — so it belongs in the
work queue, not behind an exemption. Hiding it would be the same mistake this
whole change is undoing.

## Also fixed

The byte-identical fast path set `match_percent` but left
`match_percent_normalized` at its `None` default, so a byte-identical symbol
reported a **null** canonical score. A zero `diff_score` is 100% under both
rulers.

## The methodological lesson

*Unit controls check what you thought of; end-to-end controls check what you
didn't.* The seven new tests drive `diff_objs` rather than `reloc_eq`. Four of
them **fail on the parent commit** — that is what makes them tests. The three
carve-out cases pass on the parent, which is correct and worth stating: they are
guards on the new behaviour, not evidence of the old bug.

## Blast radius

`dc3-decomp`, `rb3` and `rb3-xenon` all set
`options.functionRelocDiffs = name_check` and all three symlink the same
`../objdiff/target/release/objdiff-cli`. One rebuild moves the canonical metric
in all three. Nothing rebuilds that binary automatically — check its mtime
before trusting any row that depends on this fix.
