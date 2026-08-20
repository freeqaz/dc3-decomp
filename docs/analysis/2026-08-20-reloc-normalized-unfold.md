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

- **`??_DAppLabel@@QAAXXZ`** calls `??1HamLabel@@UAA@XZ` where the target calls
  `??1AppLabel@@UAA@XZ` — a vbase destructor running the wrong destructor.
- **`GatherObjectsFromGroup<RndMesh>`** references `??_R0?AVObject@Hmx@@@8`
  where the target references `??_R0?AVRndMesh@@@8` — the RTTI type descriptor
  for the wrong class.
- **`WorldCrowd::Mats`** references `gImpostorMat` unmangled where the target
  has `?gImpostorMat@@3PAVRndMat@@A`. Exactly the `createFilter` shape: a
  linkage declaration that disagrees with the original.
- **Three wrong float constants.** MSVC names float-literal COMDATs by their hex
  value, so `__real@3ecccccc` vs `__real@3f19999a` is **0.4 vs 0.6**. The fold's
  own comment claimed it preserved wrong-constant bugs "because those are
  immediates" — on PPC a float constant is *not* an immediate, it is a
  relocation to a named literal, so that class was being masked by the very
  mechanism documented as protecting it.

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
