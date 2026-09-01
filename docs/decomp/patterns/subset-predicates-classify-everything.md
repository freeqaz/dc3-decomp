# A subset test against a whitelist classifies everything

**Repo: dc3-decomp** (Dance Central 3, Xbox 360 PowerPC, title `373307D9`).
Cross-repo comparison target: `../rb3-xenon`.

`if diffset <= {'<', '>', '==', ...}:` reads like "every differing token is a
comparison operator". It also fires when **nothing differs at all**, because the
empty set is a subset of every set. Any bucket whose predicate is a subset test
against a whitelist absorbs every hunk the whitelist cannot describe.

## The measured damage

The cross-repo drift classifier (originally untracked, at
`/home/free/scratch/drift-audit/classify.py`) produced a `COMPARISON_FLIP`
bucket that a lane used to size the "comparison operator drifted between the two
decomps" class. Over `src/system`, dc3-decomp `10dc2a23b` vs rb3-xenon
`26bfd7246`, 2,340 files compared:

| bucket | legacy | +D1 only | +D1+D2/D3 (fixed) | net |
|---|---:|---:|---:|---:|
| COMPARISON_FLIP | **91** | 9 | **29** | **-62** |
| SIGN_FLIP | 6 | 6 | 4 | -2 |
| NEAR_IDENTICAL_EDIT | 4350 | 4419 | 4382 | +32 |
| CONST_RETUNE | 735 | 735 | 735 | 0 |
| OPERAND_REORDER | 88 | 88 | 88 | 0 |
| ONE_SIDED_GUARD | 16 | 16 | 16 | 0 |
| TOTAL classified | 5286 | 5273 | 5254 | -32 |

**Only one bucket carried the defect.** The classifier has exactly two subset
predicates; `SIGN_FLIP`'s was already written `diffset <= SIGN_OPS and diffset`
and was therefore correct (0 of 7 hunks had an empty diffset).
`OPERAND_REORDER` (multiset equality), `CONST_RETUNE` (token-list equality),
`NEAR_IDENTICAL_EDIT` (similarity ratio) and `ONE_SIDED_GUARD` (two regex
searches) are not subset tests and cannot exhibit this failure — confirmed by
their `+0` deltas.

## Three defects, two pulling in opposite directions

**D1 — the empty-set subset test.** `diffset = set(ta) ^ set(tb)` is a *set*
symmetric difference, so it is blind to multiplicity. A hunk that only changed
how many times a token appears — helper arity (`UtilDrawSphere(p, c, 0)` vs
`UtilDrawSphere(p, c)`), statement splitting, log-string wording — yields the
**empty set** and satisfied `diffset <= CMP_OPS`. 85 of 97 hunks in the original
census; the lane's own re-run measured 84 of 96.

**D2 — six of the eight whitelist entries were unreachable.** The tokenizer's
operator alternative is `[^\sA-Za-z0-9_]`, which matches *exactly one*
non-alphanumeric character. It can never emit `<=`, `>=`, `==`, `!=`, `&&` or
`||`. Only `<` and `>` were live.

**D3 — `<` and `>` were ambiguous.** Under that tokenizer `p->x`, `a >> 1` and
`a << 1` each inject a bare `<`/`>` into the diffset, so pointer and shift drift
scored as comparison drift. 3 of the 12 genuine-diffset survivors were this.

D1 **inflated** the bucket; D2 **deflated** it by hiding every multi-character
operator flip. Fixing only the reported bug gives **9**, which is wrong by 20 in
the other direction. The correct count is **29**. This is why the repair is
"check each bucket's intent", not "add `and diffset` everywhere".

## What the corrected bucket actually contains

Of the 29, adjudicated by hand:

- **8** null-check spelling — `if (x)` vs `if (x != nullptr)`. Semantically identical.
- **5** commuted comparison — `a < b` vs `b > a`. Source spelling only.
- **4** `&&`-vs-nested-`if` restructuring. Semantically identical.
- **2** ternary/clamp re-spelling. `RndFlare::SetSteps` is `if (i1 < 1) i1 = 1;`
  vs `if (i1 <= 1) i1 = 1;` — **both clamp to 1**, zero signal.
- **2** `&` vs `&&` (`ContentMgr_Xbox`, `LiveCameraInput`) — deliberate
  non-short-circuit spellings that match MSVC codegen, not bugs.
- **1** `> 0` vs `!= 0` on an unsigned — the project's own documented
  `ble`-vs-`beq` idiom, deliberate.
- **4** one-sided extra condition (DC3-only guards) — legitimate divergence.
- **1** `MinEq` helper substitution (`midi/MidiParser.cpp`).
- **1** genuine polarity difference: `os/HolmesClient.cpp`, DC3
  `str == gLastCachedResource` vs rb3-xenon `str != …` in a cache-hit early-out.
  DC3's reading is the coherent one. **This is the single highest-signal hunk in
  the bucket and the legacy ruler filed it under `SIGN_FLIP`**, where nobody
  looking at comparison drift would have found it.

So the bucket's real yield is ~1 lead, not 91 candidates — consistent with the
prior lane's calibration of 3 confirmed bugs per 5,261 substantive hunks. **A
near-empty bucket is the valuable result here**: it retires work nobody needs to
do.

`RndSpline::SyncPristineCtrlPoints` (`rndobj/Spline.cpp:159`) remains in the
bucket and is **already adjudicated byte-identical** — the gap there is
liveness, not a comparison flip. Do not re-open it.

## The fix, and the controls that make it believable

`scripts/analysis/classify_cross_repo_drift.py` is the tracked successor.
`--legacy` reproduces the original predicates verbatim — verified by running the
original scratch script against the current trees and getting **5286 / 91 both
ways**, identical bucket for bucket. (The 2026-08-31 artifact reads 5277 / 97;
that gap is *source movement* in the two repos since, not a reproduction
failure, and it is established by re-running the original tool rather than
assumed.)

`--selftest` is sabotage-checked. Exit codes watched failing before the fix was
trusted:

| sabotage | exit | outcome |
|---|---:|---|
| none (fixed predicate) | 0 | PASS, 14 cases, 9 negative controls |
| **A** revert D1 (drop `and diffset`) | **1** | 4 cases FAIL |
| **B** revert D2/D3 (legacy tokenizer) | **4** | 5 FAIL, 5 mis-specified |
| **C** gut the precondition checker *and* mis-specify a case | **0** | PASS — proves the checker is load-bearing |
| **D** mis-specify a case, checker intact | **4** | caught |

The suite asserts its own **preconditions**, and that is not decoration: the
first draft was vacuous. Three of the four "empty diffset" negative controls did
not actually produce an empty diffset — `f(a, a)` vs `f(a)` differs by a bare
`,` — so they exercised nothing and passed under the buggy predicate too. A case
tagged `empty` must yield an empty diffset, `cmp` must yield one intersecting
`CMP_OPS`, `noncmp` must yield a non-empty diffset that does not; a
mis-specified case is a hard **exit 4**, not a silent pass. Sabotage C is the
proof that removing this check lets a broken case report PASS.

## Reproducing the census

The original census input was untracked scratch JSON, which is why no one could
re-derive it. The replacement derives its file list from `git ls-files` in both
repositories and records **both HEAD revisions** in the artifact's `provenance`
block:

```sh
python3 scripts/analysis/classify_cross_repo_drift.py --selftest
python3 scripts/analysis/classify_cross_repo_drift.py \
    --repo-b ../rb3-xenon --ab \
    --summary-out docs/analysis/data/2026-09-01-cross-repo-drift-census.json
```

The committed summary carries the per-bucket counts under **both** rulers plus
every hunk outside the two bulk buckets (137 hunks, 42 KB). The full 1.4 MB
census is regenerable with `--out`.

## The general rule

A subset test states a *universal* over the differing tokens, and a universal
over an empty domain is vacuously true. Whenever a classifier bucket is written
`if diffs <= WHITELIST`, ask what an empty `diffs` means — and if the answer is
"this hunk has nothing to do with the bucket", the predicate needs an
existential guard too. The neighbouring `SIGN_FLIP` line in this very function
had the guard, which is the ordinary way this bug survives review: it looks
correct by analogy to code three lines away that *is* correct.
