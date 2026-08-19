# DC3 Decomp — Progress Metrics

> **Auto-generated** by `scripts/progress_metrics.py`.  Do not edit manually.
> Re-generate with `python3 scripts/progress_metrics.py --markdown`.

> Report built: **2026-08-19 11:07** · objdiff-cli `4.2.3` (commit `88b425bc3bad-dirty`) · relocation mode `functionRelocDiffs=name_check`.
> A number without those three facts is not comparable to another number.

## Why there are three headline numbers

The DC3 binary contains two disjoint code populations:

1. **Authorable game code** — C++ source lives in `src/`; this is the code
   the decomp project is actually writing.  (~6.35 MB)
2. **Vendor / SDK code** — Microsoft Xbox Dev Kit (XDK), RAD Bink, etc.
   No source exists in the repo; these units will never be authored.
   (~5.03 MB, 44.2 % of the binary)

Excluded prefixes:

- `default/xdk/`
- `default/lib/binkxenon/`

Any metric that counts vendor bytes in the denominator is permanently capped
near 56 % — the project looks half-done when game code is
actually three-quarters matched.  This document names the three numbers so
they cannot be confused.

## The coexisting headlines

| Metric | Value | Notes |
|--------|-------|-------|
| **XDK-diluted fuzzy** | 43.86 % | `matched_code_percent` in report.json measures root node; counts vendor bytes in denominator |
| **Authorable fuzzy** | 78.63 % | Raw matched-code bytes over authorable-only total; best apples-to-apples byte signal |
| **Authorable normalized %** ✅ | **91.59 %** | **CANONICAL.** Functions where `match_percent_normalized == 100` over authorable total. Forgives register permutation / benign reloc-addend, but NOT wrong constants, offsets, or vtable slots |

## Relocation-mode caveat

This report was built with `functionRelocDiffs=name_check`, read from
`report.json`'s `provenance` block — not assumed.

Under `name_check` a relocation whose *target symbol name* differs is
charged even when the instruction bytes are identical, so a
`bl wrong_function` can no longer score 100 %.  This is a stricter ruler
than the `functionRelocDiffs=None` mode used before 2026-08: switching
to it moved the authorable headline down by roughly **1.2 pp** with no
code change.  Numbers from before the switch are not comparable to
numbers after it.  See
[STATE_OF_THE_DECOMP.md](STATE_OF_THE_DECOMP.md#the-2026-08-ruler-change).

## Current numbers

*(report: `report.json`, 32,202 authorable functions)*

### Authorable code (canonical)

| | Value |
|---|---|
| Total authorable code | 6,343,156 bytes (6.34 MB) |
| Matched code (raw bytes) | 4,987,744 bytes → **78.63 %** |
| Matched fns (fuzzy == 100) | 29,142 / 32,202 → 90.50 % |
| **Matched fns (normalized == 100)** | **29,495 / 32,202 → 91.59 %** |
| Complete units (all fns norm==100) | 429 / 967 → 44.36 % |
| Remaining fns (norm < 100) | 2,707 |
| Remaining bytes (norm < 100) | 1,220,580 bytes (1.22 MB) |

### Full XEX (XDK-diluted, for reference only)

| | Value |
|---|---|
| Total code | 11,373,424 bytes (11.37 MB) |
| Matched code (raw bytes) | 4,988,784 bytes → 43.86 % |
| Matched fns (fuzzy == 100) | 29,157 / 48,333 → 60.33 % |
| Matched fns (normalized == 100) | 29,510 / 48,333 → 61.06 % |
| Complete units | 429 / 2055 → 20.88 % |

## How to re-compute

```bash
ninja build/373307D9/report.json     # refresh objdiff report
python3 scripts/progress_metrics.py  # print to stdout
python3 scripts/progress_metrics.py --markdown  # regenerate this file
```

Or via `measure_progress.sh`:

```bash
scripts/measure_progress.sh --authorable
```

