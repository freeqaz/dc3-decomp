# Inliner — Reverse Engineering Results

## Overview

The inliner runs in c2.dll's per-function optimization loop. It decides which
callee functions get inlined into the caller based on a cost model. Source path
from diagnostic strings: same as COLOR — `regasg.c` is in `p2`, the inliner
likely in the front-end bridge or a separate module.

## Architecture

### Call Graph

```
FUN_10ba347b (109b) — inliner setup / top-level call
  └→ FUN_10ba32fc (383b) — inline_dispatch: sets flags, calls cost calculator
       └→ FUN_10ba1eca (1368b) — inline_cost: walks IL, computes cost
            ├→ FUN_10b32533 (3b) — per-node weight (STUB: returns 0)
            ├→ FUN_10b82860 (60b) — callee cost measurement
            ├→ FUN_10b9c95d (86b) — inlineable callee check
            └→ FUN_10ba1e82 (27b) — linear flow: mark always-inline
       └→ FUN_10ba1c2d (597b) — inline_execute: performs inlining
            └→ prints "Inlining %s (%d instrs) into %s"
            └→ prints "[normal inline]" / "[force inline]" / "[vcall inline]"
```

## Cost Model — THE KEY FINDING

### Threshold: **150 counted IL nodes** (0x96)

```c
// In inline_cost (FUN_10ba1eca):
if (0x96 < local_30) {
    printf("INF:\t%s won't be inlined (too big)\n");
    func->flags |= 0x100;  // mark "too big"
}
```

### Cost Counting

The cost calculator (`FUN_10ba1eca`) iterates all IL nodes in the function:

```c
for each IL_node in function->body:
    if (node.type is significant):
        cost += 1
    cost += per_node_weight()  // FUN_10b32533 — always returns 0
```

**Per-node weight is a STUB** (`FUN_10b32533` = `return 0`). The cost is
exactly **1 per counted IL node**. There is no weighted cost model — it's a
simple node count.

### Which Nodes Are Counted

Not every IL node adds to the cost. The counting logic:

1. **Skipped entirely** (cost += 0):
   - Type 0x15 nodes (NOPs/comments)
   - Opcode 0x2B3 (some internal marker)
   - Opcode 0x2C0 nodes where the callee type is 0x12 (delegating calls)
   - Type 0x12 nodes with no extra info (`piVar6[0xd] == 0`) and certain opcodes

2. **Counted** (cost += 1):
   - Most arithmetic/assignment/compare IL nodes
   - Branch targets, control flow nodes
   - Memory operations, load/store

3. **Call nodes** (type 0x0f) — special handling:
   - Cost += 1 for non-inlineable calls
   - For inlineable callees: **subtract the callee's cost** from the caller:
     ```c
     callee_cost = callee->inline_data->cost - 1;
     callee_size = get_func_size(callee);
     cost += callee_cost + (-1 - callee_size);
     ```
   - This means **inlined callees DON'T count toward the caller's budget**

### Reconciling with Empirical Results

Our differential testing found ~30 IR tuples as the inline threshold. With a
threshold of 150 IL nodes:
- Each source statement generates ~5 IL nodes on average
- Simple arithmetic: `int a = get(0);` → CALL_START + CALL_EXEC + ASSIGN + type info ≈ 4-5 nodes
- 30 statements × 5 nodes/statement ≈ 150 nodes ✓

## Overrides

### Linear Flow: Always Inline

If a function has "linear flow" (no branches, no complex control flow), it's
**always inlined** regardless of size:

```c
// In inline_cost:
if (has_linear_flow && cost < 0xFFFF) {
    inline_mark_always(func);  // FUN_10ba1e82: sets flags |= 0x900
}
```

`FUN_10ba1e82` (27b):
```c
void inline_mark_always(int func) {
    if (opt_level == 2) opt_level = 0;  // disable further opt
    func->flags94 |= 0x900;  // 0x100 (size flag) + 0x800 (linear flow inline)
}
```

### Force Inline: `__forceinline`

The `__forceinline` attribute (flag `0x2000` on the function descriptor) causes
the inline executor to skip the size check entirely:

```c
// In inline_execute (FUN_10ba1c2d):
if (func->flags4c & 0x2000) {
    // [force inline] path — no size check
}
```

This matches our empirical finding that `__forceinline` has no size limit.

### Redirector Functions

Small wrapper functions that just forward to another function:
```c
if (is_redirector && call_count_diff < 6 && ...) {
    func->flags94 |= 0x40;  // mark as redirector
    // "INF:\t%s is a redirector function\n"
}
```

## Inline Types

From `inline_execute` (`FUN_10ba1c2d`), three inline types:

| Type | Condition | Description |
|------|-----------|-------------|
| `[normal inline]` | `callsite->flags4a & 4` or `func->flags38 & 1` | Standard inlining |
| `[vcall inline]` | `callsite->flags4a & 2` | Virtual call devirtualization + inline |
| `[force inline]` | `func->flags4c & 0x2000` | `__forceinline` keyword |

## Inline Budget for DC3

DC3 compiles with `/Ox` which includes `/Ob2` (automatic inlining). The
threshold of 150 counted IL nodes means:

- **Small functions** (< ~30 statements): Always inlined
- **Medium functions** (~30-50 statements): May or may not inline depending on
  IL node distribution (calls, branches count differently)
- **Large functions** (> ~50 statements): Never inlined unless `__forceinline`
- **Linear flow functions**: Always inlined regardless of size
- **Inlined callees**: Their cost is subtracted — a function that calls 5
  small inlineable functions still has room for its own logic

## Implications for the Permuter

1. **Accessor inlining is predictable**: Small accessors (< ~10 IL nodes) will
   always be inlined when defined in headers. To prevent inlining, move the
   body to the .cpp file (proven on UIListSlot::Draw).

2. **Header body size affects TU inlining budget**: Adding function bodies to
   headers increases the number of inlineable callees, which changes which
   functions the compiler decides to inline in the TU. This is why removing
   og function bodies sometimes causes regressions.

3. **The threshold is per-function, not per-TU**: Each function's inlineability
   is decided independently based on its own IL node count.

4. **Call node special handling**: A function with many calls to small inline
   functions has a lower effective cost than one with the same number of
   non-inlineable calls.

## Key Addresses

| Address | Size | Name | Role |
|---------|------|------|------|
| `0x10ba347b` | 109b | `inline_top` | Top-level inliner entry |
| `0x10ba32fc` | 383b | `inline_dispatch` | Dispatcher, sets flags |
| `0x10ba1eca` | 1368b | `inline_cost` | Cost calculator (walks IL) |
| `0x10ba1c2d` | 597b | `inline_execute` | Performs inlining |
| `0x10b32533` | 3b | `inline_node_weight` | Per-node weight (STUB = 0) |
| `0x10ba1e82` | 27b | `inline_mark_always` | Linear flow: always inline |
| `0x10ba1e9d` | 16b | `inline_mark_too_big` | Mark function "won't profile" |
| `0x10b82860` | 60b | `inline_func_size` | Get function size for adjustment |
| `0x10b9c95d` | 86b | `inline_is_candidate` | Check if callee is inlineable |

## Diagnostic Strings

| Address | String | Where Used |
|---------|--------|------------|
| `0x10b16b60` | `INF:\t%s won't be inlined (too big)\n` | inline_cost |
| `0x10b16bc8` | `WRN:\t%s (%s) won't be inlined\n` | inline_cost |
| `0x10b16b04` | `[normal inline]\n` | inline_execute |
| `0x10b16b24` | `[vcall inline]\n` | inline_execute |
| `0x10b16b38` | `[force inline]\n` | inline_execute |
| `0x10b16ef0` | `INF:\t%s (redirector) is always inline by transitivity\n` | inline_cost |
| `0x10b16ca0` | `WRN:\t%s has 'dangerous' inline asm, won't be profiled\n` | inline_dispatch |
| `0x10b025a0` | `INL:\t!!! InlBadCandidate said not to inline %s into %s\n` | FUN_10b60930 |

## Source File Reference

The inliner diagnostic format strings use `INL:` prefix (vs `INF:` for general
info). The inliner is part of the backend optimizer in c2.dll, not the
front-end (c1xx.dll handles `__forceinline` marking, but c2.dll makes the
actual inline decision during optimization).
