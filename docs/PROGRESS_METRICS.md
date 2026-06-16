Generated: /home/free/code/milohax/dc3-decomp/docs/PROGRESS_METRICS.md
============================================================
DC3 Decomp — Progress Metrics
============================================================

  Excluded prefixes (SDK/vendor, not authorable):
    default/xdk/
    default/lib/binkxenon/
  SDK/vendor bytes excluded: 5.03 MB (5,030,268 bytes) (44.2% of XEX)

  [XEX total — XDK-diluted headline]
    Matched code (raw bytes):  44.21%  (5,030,412 / 11,379,344 bytes)
    Matched fns  (fuzzy==100): 60.98%  (29,524 / 48,412)
    Matched fns  (norm==100):  61.41%  (29,731 / 48,412)
    Complete units:            21.07%  (433 / 2055)

  [Authorable — CANONICAL HEADLINE]
    Matched code (raw bytes):  79.22%  (5,029,524 / 6,349,076 bytes)
    Matched fns  (fuzzy==100): 91.50%  (29,510 / 32,252)
 ** Matched fns  (norm==100):  92.14%  (29,717 / 32,252)  <-- CANONICAL
    Complete units:            44.78%  (433 / 967)

  [Remaining authorable work  (normalized < 100 %)]
    Functions:  2,535
    Bytes:      1.21 MB (1,208,268 bytes)

e; counts vendor bytes in denominator |
| **Authorable fuzzy** | 79.22 % | Raw matched-code bytes over authorable-only total; best apples-to-apples byte signal |
| **Authorable normalized %** ✅ | **92.14 %** | **CANONICAL.** Functions where `match_percent_normalized == 100` over authorable total. Forgives register permutation / benign reloc-addend, but NOT wrong constants, offsets, or vtable slots |
| Strict reloc (pending Lane C) | TBD | Would use name-only reloc mode; expected to differ only for benign addend diffs |

## Relocation-mode caveat

The `match_percent_normalized` field (and therefore this metric) is computed
with `functionRelocDiffs = None`, which forgives relocation-target differences.
In practice this means a `bl wrong_function` would still score 100 % if the
wrong callee has the same relocation flags.  Lane C's strict-reloc recertification
will quantify the genuine false-100 % exposure; preliminary analysis (doc 02)
found only ~2 non-boilerplate functions dropping >10 % under strict mode, so the
risk is bounded but currently unquantified.

## Current numbers

*(report: `report.json`, 32,252 authorable functions)*

### Authorable code (canonical)

| | Value |
|---|---|
| Total authorable code | 6,349,076 bytes (6.35 MB) |
| Matched code (raw bytes) | 5,029,524 bytes → **79.22 %** |
| Matched fns (fuzzy == 100) | 29,510 / 32,252 → 91.50 % |
| **Matched fns (normalized == 100)** | **29,717 / 32,252 → 92.14 %** |
| Complete units (all fns norm==100) | 433 / 967 → 44.78 % |
| Remaining fns (norm < 100) | 2,535 |
| Remaining bytes (norm < 100) | 1,208,268 bytes (1.21 MB) |

### Full XEX (XDK-diluted, for reference only)

| | Value |
|---|---|
| Total code | 11,379,344 bytes (11.38 MB) |
| Matched code (raw bytes) | 5,030,412 bytes → 44.21 % |
| Matched fns (fuzzy == 100) | 29,524 / 48,412 → 60.98 % |
| Matched fns (normalized == 100) | 29,731 / 48,412 → 61.41 % |
| Complete units | 433 / 2055 → 21.07 % |

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

