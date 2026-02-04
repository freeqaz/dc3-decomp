# objdiff CLI Command Reference

> Part of the [objdiff CLI Design](./OBJDIFF_CLI_DESIGN.md) documentation.

This document specifies the new and extended CLI commands for objdiff.

---

## New Command: `report query`

Query and filter existing report files.

```
objdiff report query [OPTIONS] <REPORT_FILE>

Arguments:
  <REPORT_FILE>    Path to report file (JSON or proto), or "-" for stdin

Options:
  -o, --output <FILE>           Output file (default: stdout)
  -f, --format <FORMAT>         Output format: json, json-pretty [default: json]
                                (Phase 3 adds: markdown, csv)

Filtering:
  --unit <PATTERN>              Filter units by glob pattern (e.g., "src/lazer/*")
  --function <PATTERN>          Filter functions by regex pattern
  --category <ID>               Filter to specific progress category
  --min-percent <N>             Minimum match percentage (0-100)
  --max-percent <N>             Maximum match percentage (0-100)
  --unimplemented               Only functions with null match % (no implementation)
  --incomplete                  Only show incomplete units (not marked complete)
  --min-size <BYTES>            Minimum function size in bytes
  --max-size <BYTES>            Maximum function size in bytes

Sorting:
  --sort-by <FIELD>             Sort by: match_percent, size, name [default: name]
  --sort-order <ORDER>          Sort order: asc, desc [default: asc]
  --limit <N>                   Limit output to N results

Output Selection:
  --summary                     Output only aggregate measures
  --units                       Output unit-level data (default)
  --functions                   Output function-level data
  --sections                    Output section-level data
```

### Example Use Cases

```bash
# Get functions between 50-99% match, sorted by size (largest first)
objdiff report query report.json \
  --functions --min-percent 50 --max-percent 99 \
  --sort-by size --sort-order desc --limit 20

# Get all functions in PresenceMgr
objdiff report query report.json \
  --functions --function "PresenceMgr::.*"

# Get summary stats only
objdiff report query report.json --summary

# Get incomplete units in lazer directory
objdiff report query report.json \
  --units --unit "src/lazer/*" --incomplete
```

### Output Schema (JSON)

```json
{
  "query": {
    "filters": {"min_percent": 50, "max_percent": 99},
    "sort_by": "size",
    "limit": 20
  },
  "summary": {
    "total_matched": 15,
    "total_filtered": 234
  },
  "results": [
    {
      "unit": "src/lazer/game/Game.cpp",
      "name": "Game::Poll",
      "demangled_name": "Game::Poll()",
      "size": 1248,
      "fuzzy_match_percent": 97.5,
      "address": 2147680256
    }
  ]
}
```

---

## New Command: `report summary`

Quick aggregate stats for scripting.

```
objdiff report summary [OPTIONS] <REPORT_FILE>

Arguments:
  <REPORT_FILE>    Path to report file

Options:
  -f, --format <FORMAT>    Output format: json, text [default: json]
  --category <ID>          Show stats for specific category only
```

### Example

```bash
$ objdiff report summary report.json
{
  "fuzzy_match_percent": 30.7,
  "matched_code_percent": 28.4,
  "matched_functions": 21211,
  "total_functions": 46958,
  "matched_functions_percent": 45.2,
  "total_code": 12847592,
  "matched_code": 3648716
}
```

---

## Extended Command: `diff --output-format`

Add non-interactive output mode to existing diff command.

```
objdiff diff [EXISTING_OPTIONS] [NEW_OPTIONS]

New Options:
  --output-format <FORMAT>      Output format: tui, json, json-pretty [default: tui]
  -o, --output <FILE>           Output file (only with --output-format json*)
  --include-instructions        Include instruction-level diff in output
  --include-data                Include data section diff in output
```

### Example

```bash
# Get JSON diff for a specific function
objdiff diff -p . -u Game.cpp Game::Poll --output-format json

# Get detailed diff with instructions
objdiff diff -p . -u Game.cpp Game::Poll \
  --output-format json --include-instructions
```

### Output Schema (JSON)

```json
{
  "symbol": "Game::Poll",
  "demangled": "Game::Poll()",
  "unit": "src/lazer/game/Game.cpp",
  "target_size": 1248,
  "base_size": 1252,
  "fuzzy_match_percent": 97.5,
  "diff_score": {
    "matched": 1220,
    "total": 1248
  },
  "build_status": {
    "success": true,
    "stdout": "",
    "stderr": ""
  },
  "instructions": [
    {
      "index": 0,
      "target": {"address": "0x80001000", "opcode": "mflr", "args": "r0"},
      "base": {"address": "0x80001000", "opcode": "mflr", "args": "r0"},
      "match": "equal"
    },
    {
      "index": 42,
      "target": {"address": "0x800010A8", "opcode": "li", "args": "r3, 0"},
      "base": {"address": "0x800010A8", "opcode": "li", "args": "r3, 1"},
      "match": "diff_arg"
    }
  ]
}
```

---

## New Command: `report function`

Direct function lookup (convenience wrapper).

```
objdiff report function [OPTIONS] <REPORT_FILE> <FUNCTION_NAME>

Arguments:
  <REPORT_FILE>      Path to report file
  <FUNCTION_NAME>    Function name (supports regex)

Options:
  -f, --format <FORMAT>    Output format: json, text [default: json]
  --exact                  Exact match only (no regex)
```

### Example

```bash
$ objdiff report function report.json "Shuttle::SetActive"
{
  "found": true,
  "matches": [
    {
      "unit": "src/lazer/game/Shuttle.cpp",
      "name": "_ZN7Shuttle9SetActiveEb",
      "demangled_name": "Shuttle::SetActive(bool)",
      "size": 32,
      "fuzzy_match_percent": 100.0,
      "address": 2147812352
    }
  ]
}
```

---

## New Command: `report trending` (Phase 4)

Compare multiple reports over time.

```
objdiff report trending [OPTIONS] <REPORT_FILES>...

Arguments:
  <REPORT_FILES>...    Two or more report files in chronological order

Options:
  -o, --output <FILE>       Output file
  -f, --format <FORMAT>     Output format: json, csv [default: json]
  --function <PATTERN>      Track specific functions by regex
  --unit <PATTERN>          Track specific units by glob
```

### Example

```bash
$ objdiff report trending day1.json day2.json day3.json --function "Shuttle::.*"
{
  "reports": ["day1.json", "day2.json", "day3.json"],
  "overall": [
    {"report": "day1.json", "fuzzy_match_percent": 28.5},
    {"report": "day2.json", "fuzzy_match_percent": 29.8},
    {"report": "day3.json", "fuzzy_match_percent": 30.7}
  ],
  "tracked_functions": [
    {
      "name": "Shuttle::SetActive(bool)",
      "history": [
        {"report": "day1.json", "percent": 85.0},
        {"report": "day2.json", "percent": 95.0},
        {"report": "day3.json", "percent": 100.0}
      ]
    }
  ]
}
```
