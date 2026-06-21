Generated: /home/free/code/milohax/dc3-decomp/docs/PROGRESS_METRICS.md
============================================================
DC3 Decomp — Progress Metrics
============================================================

  Excluded prefixes (SDK/vendor, not authorable):
    default/xdk/
    default/lib/binkxenon/
  SDK/vendor bytes excluded: 5.03 MB (5,030,268 bytes) (44.2% of XEX)

  [XEX total — XDK-diluted headline]
    Matched code (raw bytes):  44.34%  (5,046,096 / 11,379,344 bytes)
    Matched fns  (fuzzy==100): 61.12%  (29,590 / 48,412)
    Matched fns  (norm==100):  61.55%  (29,798 / 48,412)
    Complete units:            21.17%  (435 / 2055)

  [Authorable — CANONICAL HEADLINE]
    Matched code (raw bytes):  79.46%  (5,045,208 / 6,349,076 bytes)
    Matched fns  (fuzzy==100): 91.70%  (29,576 / 32,252)
 ** Matched fns  (norm==100):  92.35%  (29,784 / 32,252)  <-- CANONICAL
    Complete units:            44.98%  (435 / 967)

  [Remaining authorable work  (normalized < 100 %)]
    Functions:  2,468
    Bytes:      1.19 MB (1,191,604 bytes)

e; counts vendor bytes in denominator |
| **Authorable fuzzy** | 79.46 % | Raw matched-code bytes over authorable-only total; best apples-to-apples byte signal |
| **Authorable normalized %** ✅ | **92.35 %** | **CANONICAL.** Functions where `match_percent_normalized == 100` over authorable total. Forgives register permutation / benign reloc-addend, but NOT wrong constants, offsets, or vtable slots |
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
| Matched code (raw bytes) | 5,045,208 bytes → **79.46 %** |
| Matched fns (fuzzy == 100) | 29,576 / 32,252 → 91.70 % |
| **Matched fns (normalized == 100)** | **29,784 / 32,252 → 92.35 %** |
| Complete units (all fns norm==100) | 435 / 967 → 44.98 % |
| Remaining fns (norm < 100) | 2,468 |
| Remaining bytes (norm < 100) | 1,191,604 bytes (1.19 MB) |

### Full XEX (XDK-diluted, for reference only)

| | Value |
|---|---|
| Total code | 11,379,344 bytes (11.38 MB) |
| Matched code (raw bytes) | 5,046,096 bytes → 44.34 % |
| Matched fns (fuzzy == 100) | 29,590 / 48,412 → 61.12 % |
| Matched fns (normalized == 100) | 29,798 / 48,412 → 61.55 % |
| Complete units | 435 / 2055 → 21.17 % |

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

