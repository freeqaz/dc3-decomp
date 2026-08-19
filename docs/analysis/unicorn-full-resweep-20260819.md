# Full unicorn re-sweep, 2026-08-19 — NOT APPLIED, and why; plus a 49-row harvest

Ran `batch_to_db.py --force` over the whole binary at `eda64e956` **against a copy of
decomp.db**, never the live one. 30,058 functions in 154 s. The result is not applicable
as a wholesale replacement; the reason is worth writing down, and one genuine worklist
falls out of it.

## What prompted it

`sync_match_percent.py --build --promote` does **not** run unicorn — it only syncs objdiff
percentages. So the "DB is up to date" claim after a merge wave covers match%, not
behaviour. Checking the unicorn columns showed the split:

| `unicorn_tested_at` | rows |
|---|---:|
| 2026-08-19 (post-harness-fix frontier refresh) | 1,836 |
| 2026-08-18 | 15 |
| 2026-06-11 | 97 |
| 2026-03-04 | **25,594** |

The frontier was fresh; the ~25.6 K fully-matched population was five months old, from a
harness that has since had eight defects fixed.

## Step 1 — the frontier refresh confirms the merge wave (this one WAS applied)

`refresh_frontier.py --run` over the authorable partial frontier at `eda64e956`:
1,827 fns / 453 units, 20.6 s, signal_version 3.

```
verdict_dist: {'DIVERGENT': 926, 'EQUIVALENT': 899, 'SKIPPED': 2}
flip_EQUIVALENT_to_DIVERGENT: 0
flip_DIVERGENT_to_EQUIVALENT: 6
prior_EQUIVALENT_stayed_EQUIVALENT: 893   (893 of 893)
```

**Zero EQUIVALENT→DIVERGENT.** None of the 19 merges landed on 2026-08-18/19 introduced a
behavioural regression on the frontier, and six functions were behaviourally repaired.
Applied to the live DB via `apply_refresh.py --apply` (1,825 rows).

## Step 2 — the full sweep, and why it must not be applied

| | live DB | full re-sweep |
|---|---:|---:|
| EQUIVALENT | 25,628 | 17,177 |
| DIVERGENT | 2,154 | **12,862** |

A 6× explosion in DIVERGENT is not 10 K new bugs. Class breakdown of the new set:

| class | rows | of which render normalized 100 % |
|---|---:|---:|
| `data_layout` | 9,960 | 9,512 |
| `cap_exhausted` | 2,116 | 1,781 |
| `build_env` | 468 | 465 |
| everything else | 318 | — |

`data_layout` is a **self-declared artifact class**. From the harness's own docstring,
`scripts/unicorn_runner/comparator.py:422`:

> `'data_layout'` — the only differing values are addresses the harness assigns
> independently per side (GLOBAL / RDATA / TRAMPOLINE). Same operand, different placement.
> **ARTIFACT — do not chase these as decomp bugs.**

`data_layout` + `cap_exhausted` + `build_env` = 12,544 of 12,862, i.e. **97.5 % of the new
divergences are classes already known to be artifact** (`cap_exhausted` ran ~97 % artifact
in the 2026-08-18 mining; `build_env` is in the harness's own unfixable list).

Why it concentrates on 100 %-matched functions is mechanical rather than mysterious: when
both sides are byte-identical there is no code difference left to observe, so the only
thing the comparator *can* see is the independently-assigned address placement. The
sub-100 % frontier has real code differences, which is why it classifies elsewhere.

**Cross-check that the two harnesses agree where it matters.** Restricting to the 1,982
rows the live DB carries at signal_version 3 (both paths, same schedule):

```
agree     = 1,934  (97.6 %)
disagree  =    48  — 47 DIVERGENT->EQUIVALENT, 1 EQUIVALENT->DIVERGENT
```

So the sweep is not *broken*. It is measuring a population where the dominant signal is a
placement artifact, and writing it over 25,628 curated EQUIVALENT verdicts would destroy
the oracle's signal-to-noise for no gain. **Not applied. The copy is disposable.**

### The tooling task this implies

Make the harness assign GLOBAL/RDATA/TRAMPOLINE addresses identically across sides (the
2026-08-18 shared-global-image work is the obvious precedent), or suppress `data_layout`
from the verdict entirely rather than recording it as DIVERGENT. Until then a whole-binary
`--force` sweep cannot be applied, and `batch_to_db.py --force` should probably refuse to
write a live DB without `--allow-artifact-classes` or similar.

## Step 3 — the harvest: 49 real-class divergences on functions that render 100 %

Filtering the sweep to the classes the harness calls *real* (`call_count`, `call_arg`,
`return_value`, `object_memory`, `error`, `wild_jump_match`, `logic`) **and** to functions
at normalized ≥ 100 gives **49 rows** — full data in
[`unicorn-resweep-20260819-rendered100.json`](unicorn-resweep-20260819-rendered100.json).

| class | rows |
|---|---:|
| `call_count` | 32 |
| `object_memory` | 7 |
| `return_value` | 6 |
| `call_arg` | 3 |
| `wild_jump_match` | 1 |

**27 of the 49 were EQUIVALENT in the live DB** — the fixed harness newly flags them; 19
were already DIVERGENT; 3 had no prior verdict.

This is exactly the population
[`docs/decomp/patterns/rounded-100-hides-real-bugs.md`](../decomp/patterns/rounded-100-hides-real-bugs.md)
was written about: every percentage surface rounds, so these render `100.0` and the
standing "100 % ⇒ artifact" rule would dismiss them on sight. That rule may only be applied
to a **zero-mismatch instruction count**, never to a rendered number.

Leads that stand out (serializers and container mutators — the same shape that produced
`CharUpperTwist::Load` and `RndFlare::Load`):

- `ObjectDir::SaveInlined` / `ObjectDir::PreLoadInlined` (`system/obj/Dir`)
- `HamNavList::PostLoad` and its `$4PPPPPPPM@A@` adjustor thunk, `HamNavList::Copy`
- `StreamRenderer::Save`, `UIFontImporter::Save` (adjustor thunks)
- `LightPreset::AddSpotlight` / `SetSpotlight` — both `call_arg`
- `Synth::Synth()` ctor, `RndLight::Copy`, `UIList::CollidePlane`
- `DataPushVar`, `FileRecursePattern`, `DataCallStackFrame` ctor
- several STL `push_back` / `resize` instantiations (`PracticeSection`, `HamRibbon`,
  `UIListWidget`)

**Do not treat any row as a finding.** Roughly 87 % of this DB's DIVERGENT rows have
historically been harness artifact, and four separate harness defects were fixed this week
alone. Each row is a lead to be confirmed with objdiff's instruction table and by reading
the target asm. What makes this list worth keeping is not its verdicts but its *shape*: it
is small, it is concentrated in serializers and container mutators, and it points at
functions no percentage-based query will ever surface.
