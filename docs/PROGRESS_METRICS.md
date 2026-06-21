Generated: /home/free/code/milohax/dc3-decomp/docs/PROGRESS_METRICS.md
============================================================
DC3 Decomp — Progress Metrics
============================================================

  Excluded prefixes (SDK/vendor, not authorable):
    default/xdk/
    default/lib/binkxenon/
  SDK/vendor bytes excluded: 5.03 MB (5,030,268 bytes) (44.2% of XEX)

  [XEX total — XDK-diluted headline]
    Matched code (raw bytes):  44.30%  (5,040,520 / 11,379,344 bytes)
    Matched fns  (fuzzy==100): 61.05%  (29,557 / 48,412)
    Matched fns  (norm==100):  61.48%  (29,765 / 48,412)
    Complete units:            21.12%  (434 / 2055)

  [Authorable — CANONICAL HEADLINE]
    Matched code (raw bytes):  79.38%  (5,039,632 / 6,349,076 bytes)
    Matched fns  (fuzzy==100): 91.60%  (29,543 / 32,252)
 ** Matched fns  (norm==100):  92.25%  (29,751 / 32,252)  <-- CANONICAL
    Complete units:            44.88%  (434 / 967)

  [Remaining authorable work  (normalized < 100 %)]
    Functions:  2,501
    Bytes:      1.20 MB (1,197,180 bytes)

e; counts vendor bytes in denominator |
| **Authorable fuzzy** | 79.38 % | Raw matched-code bytes over authorable-only total; best apples-to-apples byte signal |
| **Authorable normalized %** ✅ | **92.25 %** | **CANONICAL.** Functions where `match_percent_normalized == 100` over authorable total. Forgives register permutation / benign reloc-addend, but NOT wrong constants, offsets, or vtable slots |
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
| Matched code (raw bytes) | 5,039,632 bytes → **79.38 %** |
| Matched fns (fuzzy == 100) | 29,543 / 32,252 → 91.60 % |
| **Matched fns (normalized == 100)** | **29,751 / 32,252 → 92.25 %** |
| Complete units (all fns norm==100) | 434 / 967 → 44.88 % |
| Remaining fns (norm < 100) | 2,501 |
| Remaining bytes (norm < 100) | 1,197,180 bytes (1.20 MB) |

### Full XEX (XDK-diluted, for reference only)

| | Value |
|---|---|
| Total code | 11,379,344 bytes (11.38 MB) |
| Matched code (raw bytes) | 5,040,520 bytes → 44.30 % |
| Matched fns (fuzzy == 100) | 29,557 / 48,412 → 61.05 % |
| Matched fns (normalized == 100) | 29,765 / 48,412 → 61.48 % |
| Complete units | 434 / 2055 → 21.12 % |

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

