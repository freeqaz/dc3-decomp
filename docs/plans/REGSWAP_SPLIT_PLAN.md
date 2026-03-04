# Plan: Split Register Swap Detection into Fixable vs Unfixable

## Problem

Register swaps are currently classified as `MaybeFixable` in `analysis.rs`. This prevents auto-AT_LIMIT for functions where reg_swap co-occurs with unfixable patterns (ADDR_RELOC, LINKER_MERGED), keeping ~636 functions artificially workable.

Meanwhile, most volatile register swaps (r3-r12, f0-f13) are genuinely unfixable — the compiler's register allocator assigns these based on internal heuristics we can't control.

## Current State

### Data (2026-03-04)

| Category | Count | Notes |
|----------|------:|-------|
| Workable with regswap | 717 | All classified MaybeFixable |
| Regswap only (no other patterns) | 81 | Best candidates for actual fixes |
| Regswap + ADDR_RELOC | 627 | Would be AT_LIMIT if regswap were Unfixable |
| Regswap + LINKER_MERGED | 389 | Same — blocked regardless |
| Regswap at 99-100% | 56 | Lowest-hanging fruit |

### Current Detection (`analysis.rs`)

- `detect_register_swap()` at line ~1571 extracts registers with `\b([rf]\d+)\b`
- No distinction between register types
- All swaps get `Fixability::MaybeFixable`
- Auto-AT_LIMIT requires ALL patterns to be `Unfixable` → regswap blocks it

## Proposed Change

Split `RegisterSwap` into two sub-patterns based on register class:

### Register Classes (PowerPC / Xbox 360 ABI)

| Class | Registers | Fixable? | Why |
|-------|-----------|----------|-----|
| GPR callee-saved | r13-r31 | Sometimes | Declaration order → BSF graph coloring → register assignment |
| GPR volatile | r0, r3-r12 | No | Compiler-internal, no source-level control |
| FPR callee-saved | f14-f31 | Sometimes | Float declaration order maps to f31, f30, ... |
| FPR volatile | f0-f13 | No | Compiler-internal scratch registers |

### Implementation Options

#### Option A: Single pattern, split fixability (simpler)

Keep one `RegisterSwap` pattern but set fixability based on which registers are swapped:

```rust
// In detect_register_swap():
let mut has_callee_saved_swap = false;
let mut has_volatile_swap = false;

for (r1, r2) in swap_pairs.keys() {
    if is_callee_saved(r1) && is_callee_saved(r2) {
        has_callee_saved_swap = true;
    } else {
        has_volatile_swap = true;
    }
}

let fixability = if has_callee_saved_swap && !has_volatile_swap {
    Fixability::MaybeFixable  // Pure callee-saved: might fix via decl reorder
} else if has_volatile_swap && !has_callee_saved_swap {
    Fixability::Unfixable     // Pure volatile: compiler quirk
} else {
    Fixability::MaybeFixable  // Mixed: keep workable, callee-saved part might help
};
```

Helper:
```rust
fn is_callee_saved(reg: &str) -> bool {
    if let Some(num_str) = reg.strip_prefix('r') {
        if let Ok(n) = num_str.parse::<u32>() {
            return n >= 13 && n <= 31;  // r13-r31
        }
    }
    if let Some(num_str) = reg.strip_prefix('f') {
        if let Ok(n) = num_str.parse::<u32>() {
            return n >= 14 && n <= 31;  // f14-f31
        }
    }
    false
}
```

**Pros**: Minimal code change, no new pattern types.
**Cons**: Mixed-register functions stay MaybeFixable even though the volatile part is unfixable.

#### Option B: Two separate patterns (more precise)

Emit up to two patterns per function:

- `RegisterSwapCalleeSaved` → `MaybeFixable`
- `RegisterSwapVolatile` → `Unfixable`

Auto-AT_LIMIT then works naturally:
- ADDR_RELOC (Unfixable) + RegisterSwapVolatile (Unfixable) → all unfixable → AT_LIMIT
- ADDR_RELOC (Unfixable) + RegisterSwapCalleeSaved (MaybeFixable) → stays workable

**Pros**: Most precise. Functions with only volatile swaps + addr_reloc correctly auto-AT_LIMIT.
**Cons**: More code, new pattern variant, DB schema may need updating.

### Recommended: Option A with a twist

Use Option A but add a special case in the auto-AT_LIMIT logic:

```rust
// In verdict logic:
let all_effectively_unfixable = analysis.patterns.iter().all(|p| {
    p.fixability == Fixability::Unfixable ||
    (p.pattern == PatternType::RegisterSwap && is_pure_volatile_swap(p))
});
```

This keeps the pattern model simple while still allowing volatile-only swaps to count as unfixable for auto-AT_LIMIT purposes.

## Files to Change

| File | Change |
|------|--------|
| `objdiff-cli/src/cmd/analysis.rs` | Add `is_callee_saved()`, update `detect_register_swap()` to classify swap type, update auto-AT_LIMIT logic |
| `objdiff-cli/src/cmd/analysis.rs` | Store swap details in pattern metadata (which registers are swapped) |

No DB schema changes needed — the `has_register_swap` boolean stays, and the fixability is computed at analysis time.

## Validation

After implementing:

1. Rebuild objdiff: `cd ../objdiff && cargo build --release`
2. Re-sync: `venv/bin/python scripts/sync_objdiff.py --all -j16`
3. Check: volatile-only regswap + ADDR_RELOC functions should now be AT_LIMIT
4. Verify: callee-saved regswap-only functions should remain workable

### Expected Impact

- ~636 functions with regswap + unfixable patterns → most should auto-AT_LIMIT (those with pure volatile swaps)
- ~81 regswap-only functions → stay workable (callee-saved) or AT_LIMIT (volatile)
- Net reduction in workable count: estimated 400-500 functions

## References

- Register allocation findings: `MEMORY.md` → "Register Allocation" and "BSF color-to-GPR mapping"
- BSF validation: `tools/compiler_trace/bsf_trace.py`, `tools/compiler_trace/regmap_solver.py`
- FPR declaration order: confirmed via synthetic experiment (first float → f31, second → f30)
- Session context: `docs/sessions/2026-03-04-verdict-pipeline-fix.md`
