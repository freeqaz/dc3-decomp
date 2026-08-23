# The ICF alias map was a snapshot of one tree, and it aged

dc3 (Dance Central 3, Xbox 360 / MSVC debug XEX `373307D9`). Task #138. Every
number here is dc3's, taken with `objdiff-cli 4.2.8 (358c715835cc, xxh3
9b2bb6f1f3a21062)` under `functionRelocDiffs=name_check`.

## The gap, stated as a measurement

`scripts/symbol_aliases.json` carried **2,062 groups / 8,719 memberships**. Its
`retailmap:` class came from an archived run script whose admission rule was
*"the address must carry two or more names OUR OWN OBJECTS REFERENCE"*. That is
a snapshot of one tree. A spelling our source did not emit on the day it ran is
absent from the group forever, so every later source fix that starts emitting
that spelling finds the group too narrow.

| retail-map address | names in `ham_xbox_r.map` | names in the alias file |
|---|---:|---:|
| `0x82E2AB00` (`merged_Returns1`) | 163 | 74 |
| `0x82704B70` (`__uninitialized_copy`) | 38 | 37 |
| `0x8246E2F0` (`__uninitialized_fill_n`) | 26 | 25 |
| `0x82AF68F0` (`GetMaxProcess`) | 10 | 6 |
| `0x8255A0A0` (`MakeString`) | 5 | 3 |

The last row is the whole of `HttpGet::Poll`'s residual: task #140 changed
`HttpGet::mState` from `int` to `HttpGet::State`, our `bl` started naming
`MakeString<HttpGet::State>` at the *identical* map address as the target's
(`0x8255A0A0`), and the alias file did not know the two spellings were the same
code.

So the input was **stale, not wrong**, and the repair is to re-derive membership
from the map on every run.

## Why "emit every name at the address" is the wrong fix

The map states what RETAIL folded. Our build folds what OUR bodies say, and the
two differ exactly where the decomp is wrong. A blind widening would launder a
genuine wrong-callee row into "benign fold" — the failure mode that cost an
earlier lane nine real bugs.

The producer is `decomp-synth tools/il_witness/icf_retail_widen.py`. Each
admitted membership carries a body witness on top of the map, and **BODY plus
RESOLVE together reconstruct `/OPT:ICF`'s own predicate** (post-relocation
content identity) rather than approximating it:

| gate | what it requires | member drops |
|---|---|---:|
| MAP-1 | every name resolves to exactly ONE map address (1,393 dc3 names do not) | — |
| MAP-2 | ≥2 non-annotation names (`__unwind$`/`__catch$`/`$L`/`??_C@` excluded) | — |
| TGT | exactly one member is defined by the target objects = the survivor | — |
| WRITE | the survivor's COMDAT is not writable (`/OPT:ICF` never folds those) | — |
| BODY | our body masked-equal to the target's at that address, per-type reloc field masks, pad trimmed | 20 |
| COPY | the base tree agrees with itself (1,863 names have divergent copies across objects) | 61 |
| RESOLVE | every relocation site resolves, through the same map, to the same address on both sides | 458 |
| UNIQ | reloc-free bodies only: the masked body is the only one of its shape in the whole target set | 160 |

Whole binary: **3,051 shared addresses adjudicated → 950 admitted / 4,127
memberships.** Refused: 1,534 no member compiled here, 193 no member witnessed,
185 fewer than two real names, 181 no target resident, 6 two target residents,
2 multiply-addressed.

### UNIQ is the answer to "refuse zero-relocation bodies"

The standing rule is right about the body-test-only channel: `li r3,1; blr` is
byte-identical to a great many unrelated functions **in source**. It is not
right about the retail image, because `/OPT:ICF` already folded every one of
those into a single body. Measured: **all eight** reloc-free candidates in the
census have exactly ONE match across 69,543 target functions, and it is the
survivor every time. When a reloc-free body does collide, UNIQ refuses it —
2,880 distinct masked bodies in that set are claimed by more than one function,
and 160 memberships were dropped on it.

Applying UNIQ to a *relocated* body would be wrong in the other direction: 68
target functions share `_Copy_Construct<T>`'s masked shape and differ only in
the constructor they call. That is the question RESOLVE answers and masking
cannot.

### The gates fire on real things

* Three of the BODY drops at `0x82E2AB00` are functions whose bodies genuinely
  disagree with retail's: `NavListSortMgr::HeadersSelectable`,
  `XboxPurchaser::NeedsEnum` and `XboxMultipleItemsPurchaser::NeedsEnum` all
  return **false** where the image returns **true**. The widening refuses them
  rather than declaring them folded. **These are three open source bugs**, found
  as a by-product.
* Two RESOLVE drops are `__FILE__` string-pool literals resolving to different
  addresses (`e:\lazer_build_gmc1…` vs `e:\lazer_b…`).
* 458 RESOLVE drops are fail-closed on `??3@YAXPAX@Z` (`operator delete`), which
  sits at **three** addresses in this image and therefore cannot be resolved.
* Gate COPY was added after auditing the tool's own index: 23 of what first read
  as BODY divergences were the indexer witnessing the wrong copy of a per-TU
  `MakeString`.

### Negative control

`--negative-control` re-adjudicates every admitted group with the survivor's
body replaced by a different target function of the same length. **950 groups
checked, 0 still admitted.** Plus 22 unit tests, one per gate, each a sabotage
of a group the adjudicator otherwise admits.

## Result

`scripts/symbol_aliases.json`: **2,062 → 2,072 groups, 8,719 → 8,843
memberships (+124)**. `build/373307D9/icf_aliases.map`: 10,794 → 10,928 lines
(13 header + one comment per group + one line per membership).

The +124 is **99 memberships added to 27 existing groups** + **10 new groups
carrying 25 memberships** (10 survivors + 15 folded). Nothing else moved; no
group lost a member.

### A/B, cold vs cold, same commit, whole binary

Base leg is a separate clean worktree at the same SHA (`6f5fa9ccf`), not main's
working copy. `report.cache` / `report_raw.cache` / `baseline.cache` purged and
a full `ninja` run on both sides; all 48,344 rows compared by
`match_percent_normalized`. `verify_objs_patched.py --verify-manifest` and
`verify_split_current.py --check` pass on both legs.

**Measured twice, across a re-split.** The first A/B ran at `a8fead7b1`. Main
then landed `6f5fa9ccf`, which rewrites 1,469 `config/373307D9/symbols.txt`
extents and therefore re-splits every target object — the exact substrate the
adjudicator reads. Both worktrees were moved to the new base, the target objects
re-split, and the adjudication re-derived from scratch against them. **Every
number below is identical**, and so is the admitted set: 3,051 addresses, 950
groups, 4,127 memberships, 114 added, 950 checked / 0 leaked in the control.
That is an unplanned but real control on whether these verdicts are a property
of the code or of one split.

```
18 improved, 0 regressed, 0 rows appear or vanish
matched_functions  29,885 -> 29,892        (+7)
matched_code       5,048,168 -> 5,050,136  (+1,968 B)
headline           44.385647% -> 44.402950%  (+0.017303pp)
```

Seven rows crossed into the matched set:

| Δ | before → after | size | function |
|---:|---|---:|---|
| +0.172 | 99.828 → **100.0** | 116 | `vector<Label>::push_back` |
| +0.111 | 99.889 → **100.0** | 180 | `NgPostProc::CheckHueConverge` |
| +0.106 | 99.894 → **100.0** | 188 | `__introsort_loop<CuePoint>` |
| +0.072 | 99.928 → **100.0** | 276 | `SampleInst360::SampleInst360` |
| +0.017 | 99.983 → **100.0** | 1208 | **`HttpGet::Poll`** |
| +0.016 | 99.985 → **100.0** | 5152 | `SaveLoadManager::SetState` |
| +0.014 | 99.986 → **100.0** | 1416 | `OptionsPanel::OnMsg` |

Eleven more improved without crossing, including two the census never named —
`Debug::Fail` (+0.048) and `MoveDir::UpdateOverlay` (+0.004) — which is the
widening reaching folds outside the worklist.

## What this closes, and what it does not

All **16 PURE FOLD census rows** now have every divergent pair covered by one
alias group, and **`HttpGet::Poll` is at 100.0**. The five rows task #129 filed
as "the linker itself says they are the same function, the alias map does not
admit those groups" — both `MoveDir` rows, `UIListDir::BuildDrawState`,
`OptionsPanel::OnMsg` — are all in the widened set.

**The callee charge is closed; the ROW is not always closed.** Nine of the 16
are still below 100 and remain real source work. `MoveDir::ResetDetectFrames`
reads 99.0 with 16 `diff_arg` register swaps and two offset swaps;
`UIListDir::BuildDrawState` reads 89.4 with a `-0x50` frame delta and a branch
polarity. Neither shows a `WRONG_CALLEE` pattern any more. This is the standing
rule holding: *a fold group proves the printed NAME is unreliable; it does not
prove your call is right.*

**One of the 16 cannot be closed at all.**
`__uninitialized_fill_n<(anonymous)::Label*>` (unit `system/synth/SynthSample`,
96 B) reads **0.0 with 24 inserts and no base side** — it is a target-only
spelling, the ICF non-survivor class that `scripts/analysis/icf_nonsurvivor_rows.py`
excludes. Its fold IS admitted into the map (group `_Copy_Construct@0x8273b9e8`,
7 members); the row is a stub and the alias cannot move it. It should never have
been counted as closable by an alias change.

### Deliberately not done

* **`verify_pattern_scan_current.py --check` was not re-run to green.** It
  passes on `main` and fails inside any worktree (`no pattern scan recorded`),
  because a worktree's `decomp.db` is the deliberate tripwire. Re-deriving the
  scan (`pattern_census.py --ruler name_check --apply`) against the widened map
  is the correct next step and belongs on `main` after this lands — the census
  will find fewer `WRONG_CALLEE` rows, which is the point.
* The 458 `operator delete` RESOLVE refusals are left refused. Resolving them
  needs per-call-site disambiguation of a name at three addresses, which the map
  cannot do and this tool will not guess.
* The three `return false` / `return true` body divergences found by gate BODY
  are reported, not fixed.
