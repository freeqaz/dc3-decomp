# Fix `orchestrate sync` — Dropping 21K Unimplemented Functions

**Date:** 2026-02-06

## Problem

`./bin/orchestrate sync` was silently dropping ~21,248 out of 46,897 functions from the database sync. The inline SQL in `cmd_sync()` had `if fuzzy is None: continue`, which skipped every unimplemented function (those without `fuzzy_match_percent` in report.json).

Additionally, there was a proper implementation in `database.py:ingest_report()` that handled None percentages correctly, but `cmd_sync` wasn't using it. `ingest_report` itself had two smaller bugs.

## Bugs Fixed

### Bug 1: Unimplemented functions skipped (`cmd_sync`)
- `decomp_orchestrate.py:808-809`: `if not symbol or fuzzy is None: continue`
- This dropped all functions without `fuzzy_match_percent` (i.e., all unimplemented functions)
- **Fix:** Replaced the entire inline SQL block with a call to `ingest_report()`, which handles None correctly

### Bug 2: Demangled name not extracted (`ingest_report`)
- `database.py:335`: `func.get("demangled", func.get("name", ""))` — wrong field path
- Report stores demangled in `metadata.demangled_name`, not a top-level `demangled` field
- **Fix:** `func.get("metadata", {}).get("demangled_name", "") or func.get("demangled", func.get("name", ""))`

### Bug 3: Size stored as string (`ingest_report`)
- `database.py:336`: `func.get("size", 0)` — report has `"size": "144"` (string)
- **Fix:** `int(func.get("size", 0) or 0)`

## Files Changed

| File | Change |
|------|--------|
| `scripts/orchestrator/database.py:335-336` | Fixed demangled extraction path + size int cast |
| `scripts/decomp_orchestrate.py:69` | Added `ingest_report` to imports |
| `scripts/decomp_orchestrate.py:787-838` | Replaced 40-line inline SQL with `ingest_report()` call |
| `tests/test_ingest_report.py` | New test suite (10 tests) |

## Results

Before fix:
```
Updated: ~25,649 functions
Inserted: 0 functions
(21,248 silently dropped)
```

After fix:
```
Updated: 46,897 functions
Inserted: 0 functions
Skipped: 0 functions
```

## Tests Added

`tests/test_ingest_report.py` — 10 tests across 4 classes:

- **TestIngestUnimplementedFunctions** (3 tests) — core regression: functions without `fuzzy_match_percent` are inserted, mixed implemented/unimplemented both ingested, updating unimplemented to implemented works
- **TestIngestDemangledNames** (3 tests) — reads from `metadata.demangled_name`, falls back to mangled name, never empty
- **TestIngestSizeAsInt** (1 test) — string `"144"` stored as int `144`
- **TestIngestWithRealReport** (3 tests) — integration against actual `report.json`: all entries processed, all have units, demangled names populated

## Key Insight

The report.json structure for functions is:
```json
{
  "name": "?asciiDigitToHex@@YAED@Z",
  "size": "144",
  "fuzzy_match_percent": 95.55556,
  "metadata": {
    "demangled_name": "unsigned char __cdecl asciiDigitToHex(char)"
  }
}
```

Unimplemented functions omit `fuzzy_match_percent` entirely (no key, not even null). The `metadata` dict may also be empty. The `size` field is always a string. There are 21 duplicate symbols across units (46,897 entries but 46,867 unique symbols).
