# Unicorn `Reteleport` Call-Count Anomaly (Follow-up)

Date: 2026-02-19
Unit: `default/system/hamobj/HamDirector`
Symbol: `?Reteleport@HamDirector@@QAAXXZ`

## Summary
During HamDirector decomp work, Unicorn repeatedly classified `Reteleport` as `DIVERGENT` with `call_count_mismatch`.
However, direct runner outputs suggest symbol/function resolution issues in Unicorn for this case, making the divergence potentially unreliable.

## Observed Signals
- Objdiff/decomp context showed normal function sizing and stable matching behavior around ~93-95%.
- `run_recon` repeatedly reported:
  - `DIVERGENT`
  - `class=call_count`
  - `reason=call_count_mismatch`
- Direct Unicorn runner invocation produced suspicious metrics:
  - `matched_prefix` only `1` or `6`
  - `coloaded_callees: 0`
  - call traces quickly diverged into non-semantic symbols (`$M...`, `.pdata`, etc.)

## Repro Commands
```bash
python3 -m scripts.unicorn_runner.run \
  --unit default/system/hamobj/HamDirector \
  --symbol '?Reteleport@HamDirector@@QAAXXZ' \
  --json --verbose
```

Also tried explicit object paths from `objdiff.json`:
```bash
python3 -m scripts.unicorn_runner.run \
  --symbol '?Reteleport@HamDirector@@QAAXXZ' \
  --decomp-obj build/373307D9/src/system/hamobj/HamDirector.obj \
  --orig-obj build/373307D9/obj/system/hamobj/HamDirector.obj \
  --json --verbose
```

## Suspicious Output (Representative)
- `reason: call_count_mismatch`
- `decomp_calls: 1792, orig_calls: 1730, matched_prefix: 6`
- and in explicit-object mode:
  - `decomp_size: 20`
  - `orig_size: 20904`
  - `decomp_calls: 1`
  - `matched_prefix: 1`

The explicit-object result is especially suspect (20-byte decomp function) and likely indicates symbol/address resolution mismatch for this symbol in Unicorn runner pathing.

## Why This Matters
`call_count` divergence is currently gating confidence for `Reteleport`, but if Unicorn is not resolving/stepping the intended function body, the verdict may be a tooling artifact rather than a true logic mismatch.

## Follow-up Tasks
1. Validate symbol-to-address selection for this mangled symbol in unicorn runner (`scripts/unicorn_runner/*`), especially when `--symbol` is used.
2. Verify object-role mapping (decomp vs orig) in explicit-object mode for this unit.
3. Add a sanity check: reject/flag runs where resolved function size is implausibly tiny for known symbol size.
4. Add trace diagnostics that print first resolved function address/size from both sides before execution.
5. Re-run `Reteleport` after runner fix and update DB verdict/class if needed.

## Current Working State
- Kept source variant that improved asm match to ~95.1% for `Reteleport`.
- Unicorn still reports `call_count_mismatch`, but reliability is currently in question pending runner investigation.
