# IL Fixture Corpus

This directory is the persistent corpus for captured MSVC `_CL_*` IL bundles.

Each fixture should live in its own directory:

```text
msvc-src/analysis/il-fixtures/
  cast_vs_and_return/
    manifest.json
    _CL_12345678ex
    _CL_12345678gl
    _CL_12345678sy
    _CL_12345678in
    _CL_12345678db
```

## Required Coverage

The first corpus should include at least one fixture for each of:

- cast vs `& 0xFF` type control
- bool materialization
- branch polarity
- rlwinm-sensitive byte shift/mask
- switch dispatch
- call / return shape

Current captured fixtures (6/6 coverage areas):

- `il_type_control_cast_vs_and` — IL `CAST` vs `AND` distinction for byte narrowing (4 functions)
- `il_bool_materialization` — comparison/materialization for zero-test, equality, signed/unsigned (6 functions)
- `il_branch_polarity` — condition inversion, nested branches, guard patterns, signed vs unsigned (7 functions)
- `il_rlwinm_shifts` — u8 vs u32 shift fusion, rotation decomposition, mask placement (7 functions)
- `il_switch_dispatch` — small/dense/sparse switch, fall-through, enum-like dispatch (5 functions)
- `il_call_return` — simple/virtual/chained calls, tail calls, early return, conditional return (8 functions)

## Manifest Fields

`manifest.json` is written by `msvc-src/tools/il_parser.py` and should contain:

- `bundle_name`
- `captured_at`
- `source_path`
- `bundle_base`
- `il_base`
- `run_cwd`
- `compiler_path`
- `wibo_path`
- `command`
- `files`

## Capture Workflow

Use a named bundle capture:

```bash
python3 msvc-src/tools/il_parser.py capture path/to/source.cpp \
  --output-dir msvc-src/analysis/il-fixtures \
  --bundle-name cast_vs_and_return
```

Inspect a bundle:

```bash
python3 msvc-src/tools/il_parser.py list-bundle \
  msvc-src/analysis/il-fixtures/cast_vs_and_return --functions
```

Export normalized JSON:

```bash
python3 msvc-src/tools/il_parser.py export-json \
  msvc-src/analysis/il-fixtures/il_bool_materialization
```
