# Wave 8 — climbing from 0 %, smallest functions first

**Started:** 2026-09-15, base `bb2e759eb` (wave-7 close-out).
**Directive:** work the remaining authorable list from the bottom up — 0 % rows
first, then the small rows, then the hard ones. Opus lanes only.
**Coordinator artefacts:** `~/tmp/dc3-wells/w8/` (worklists, snapshots,
`compare.py`, gate logs). Baseline snapshot `report-baseline-bb2e759eb.json`.

## Where the wave starts

| denominator | functions | bytes |
|---|---|---|
| Authorable matched (canonical) | 96.80 % (31,191 / 32,223) | 87.32 % (5,540,052 / 6,344,596) |
| Remaining authorable | 1,032 | 620,028 B |

Remaining work by band, with what the database claims about each row:

| band | functions | bytes | AT_LIMIT | unadjudicated |
|---|---:|---:|---:|---:|
| 0 % | 195 | 21,872 | 27 | 168 |
| under 80 % | 34 | 25,000 | 16 | 18 |
| 80–95 % | 176 | 77,000 | 157 | 19 |
| 95–99.9 % | 519 | 399,000 | 457 | 58 |
| 99.9 – under 100 % | 119 | 79,000 | 90 | 27 |

## Finding 1: 76 of the 195 zero rows are not work

`default/link_glue` is a synthetic unit — `configure.py` registers it with
`"object": None`, so **there is no target object to diff against**. Every one of
its 76 functions therefore scores 0.0 with a diff that is pure `delete`
(`HDCache::Flush` measures 1 instruction, 1 delete, against an empty target
side), while the unit's own metadata reads `complete: true` and
`complete_code_percent: 100.0`.

Consequences, both measured:

- The 0 % class is **119 real rows / 19,984 B**, not 195 / 21,872.
- The canonical authorable headline is **understated by up to 65 functions**.
  `progress_metrics.py` already dedups 11 link-glue rows that shadow a symbol
  matched in its real unit; the other 65 exist only in link_glue and are counted
  as authorable-but-unmatched even though nothing can ever score them.

Do not send a lane at a link_glue row. If the denominator is to be corrected,
that is a `scripts/authorable.py` change, not decomp work.

## Finding 2: the 0 % class is instantiation placement, not missing bodies

Of the 119 real zero rows, the large majority are template instantiations
(`vector<T>::_M_range_insert_realloc`, `StlNodeAlloc<T>::allocate`,
`PropSync<T>`, `ObjPtrList<T>::Unlink`, `ObjDirItr<T>::Advance`,
`CSampleXAPOBase<T>::Process`), plus compiler-generated funclets and ICF fold
survivors. A prior audit found only **10** genuinely unwritten non-excluded
functions in the whole binary, all SDK boilerplate. So this class is about
*which translation unit emits an instantiation and with what linkage*, not about
writing new game logic.

## Lanes

Six Opus lanes, grouped so no two touch the same file.

| lane | rows | bytes | units |
|---|---:|---:|---|
| `w8-a` | 25 | 3,360 | `rndobj/Utl`, `obj/Utl` |
| `w8-b` | 19 | 1,848 | `hamobj/*`, `lazer/meta_ham/*` |
| `w8-c` | 20 | 2,972 | `char/*` |
| `w8-d` | 15 | 1,100 | `synth_xbox/*` except FFT |
| `w8-e` | 38 | 5,528 | `rndobj/*`, `flow/*`, `os/*`, `ui/*`, misc |
| `w8-f` | 2 | 5,176 | `synth_xbox/FFT` (the AltiVec pair, hardest) |

## Phases after the 0 % band

Derived at dispatch time, link_glue excluded throughout.

| phase | population | rows | bytes | lanes |
|---|---|---:|---:|---|
| 1 | 0 %, real rows | 119 | 19,984 | `w8-a` … `w8-f` |
| 2 | partial, under 200 B | 127 | 15,052 | `w8-g` … `w8-j` |
| 3 | partial, 200 B and over | 721 | 583,160 | not yet assigned |

Phase 2 is thin and wide: 127 rows spread over 88 units, at most four in any
one unit, 85 of them already carrying an AT_LIMIT certificate and 36 never
adjudicated at all. Phase 3 holds 94 % of the remaining bytes, and 664 of its
721 rows are already above 90 %.

## Finding 3: a better phase-3 derivation than band times size

Wave 7 ended saying the band-by-size frontier was exhausted and the next wave
needed a different derivation. Here is one, measured at wave-8 dispatch:

| shape | units | bytes to close |
|---|---:|---:|
| units needing exactly **one** more function | 151 | 120,288 |
| units needing one **or two** | 218 | 201,572 |

That is 218 of the 344 incomplete authorable units, and closing them moves the
complete-units metric (currently 623 / 967) rather than fractions of a
percentage point. The single-function list is saved at
`~/tmp/dc3-wells/w8/unit-completions.txt`, sorted by size — it opens with an
8-byte row in `zlib/zutil` and a 24-byte funclet in `jpeg/jcmaster`, and 4 of
the 20 cheapest are already above 99 %.

## Pending measurement correction

`scripts/authorable.py` counts the 65 link-glue-only rows in the authorable
denominator even though nothing can score them. Correcting it would move the
canonical headline by roughly 0.2 pp. **Deliberately deferred** until the wave's
lanes have landed, so that every lane's before/after comparison in this wave is
taken against one definition. It is a denominator change and must be committed
on its own, stating the before and after explicitly.

## Results

*(appended as lanes land)*
