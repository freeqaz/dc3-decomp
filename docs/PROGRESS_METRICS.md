Generated: /home/free/code/milohax/dc3-decomp/docs/PROGRESS_METRICS.md
============================================================
DC3 Decomp — Progress Metrics
============================================================

  Excluded prefixes (SDK/vendor, not authorable):
    default/xdk/
    default/lib/binkxenon/
  SDK/vendor bytes excluded: 5.03 MB (5,030,268 bytes) (44.2% of XEX)

  [XEX total — XDK-diluted headline]
    Matched code (raw bytes):  44.19%  (5,028,356 / 11,379,344 bytes)
    Matched fns  (fuzzy==100): 60.96%  (29,511 / 48,412)
    Matched fns  (norm==100):  61.39%  (29,718 / 48,412)
    Complete units:            21.07%  (433 / 2055)

  [Authorable — CANONICAL HEADLINE]
    Matched code (raw bytes):  79.18%  (5,027,468 / 6,349,076 bytes)
    Matched fns  (fuzzy==100): 91.46%  (29,497 / 32,252)
 ** Matched fns  (norm==100):  92.10%  (29,704 / 32,252)  <-- CANONICAL
    Complete units:            44.78%  (433 / 967)

  [Remaining authorable work  (normalized < 100 %)]
    Functions:  2,548
    Bytes:      1.21 MB (1,210,324 bytes)

e; counts vendor bytes in denominator |
| **Authorable fuzzy** | 79.18 % | Raw matched-code bytes over authorable-only total; best apples-to-apples byte signal |
| **Authorable normalized %** ✅ | **92.10 %** | **CANONICAL.** Functions where `match_percent_normalized == 100` over authorable total. Forgives register permutation / benign reloc-addend, but NOT wrong constants, offsets, or vtable slots |
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
| Matched code (raw bytes) | 5,027,468 bytes → **79.18 %** |
| Matched fns (fuzzy == 100) | 29,497 / 32,252 → 91.46 % |
| **Matched fns (normalized == 100)** | **29,704 / 32,252 → 92.10 %** |
| Complete units (all fns norm==100) | 433 / 967 → 44.78 % |
| Remaining fns (norm < 100) | 2,548 |
| Remaining bytes (norm < 100) | 1,210,324 bytes (1.21 MB) |

### Full XEX (XDK-diluted, for reference only)

| | Value |
|---|---|
| Total code | 11,379,344 bytes (11.38 MB) |
| Matched code (raw bytes) | 5,028,356 bytes → 44.19 % |
| Matched fns (fuzzy == 100) | 29,511 / 48,412 → 60.96 % |
| Matched fns (normalized == 100) | 29,718 / 48,412 → 61.39 % |
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

