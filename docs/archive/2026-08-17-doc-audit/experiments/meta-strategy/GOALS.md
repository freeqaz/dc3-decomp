# Goals: Realistic Targets for DC3 Decomp

Based on pattern analysis and current project state.

## Current State (2026-02-20)

| Metric | Value |
|--------|-------|
| Total functions | 47,849 |
| Excluded (SDK/lib) | 16,021 |
| Non-excluded | 31,828 |
| COMPLETE (100%) | 25,442 (79.9% of non-excluded) |
| AT_LIMIT (classified stuck) | 6,406 (20.1% of non-excluded) |
| Remaining workable | 0 |
| Fuzzy match | 44.06% |
| Matched code | 35.99% |
| Complete units | 165 / 2,224 |

### What Changed Since 2026-02-14

- **Exhaustive triage complete**: All 31,828 non-excluded functions classified (COMPLETE or AT_LIMIT)
- **Regswap patching**: +0.32% code match (709 functions patched, 114 promoted to 100%)
- **Stub burndown**: 43 stubs implemented, +0.21% fuzzy match
- **SmartGlass send path**: 8 functions implemented (DtaToJson, XbcSendMsg, etc.)
- **Unicorn behavioral testing**: 93.1% equivalence rate across 25,682 functions
- **X360 linking**: VA shift fixed, XEX builds working, debug.xex boots in Xenia

## Active Work Streams

### 1. SmartGlass Receive Path (In Progress)

**JsonToDta** — 1,132-byte recursive JSON-to-DataArray parser. Forward-declared but unimplemented.
- Symbol: `?JsonToDta@?A0x8a9ffbf2@@YA?AVDataArrayPtr@@PAUHJSONREADER__@@_N@Z`
- DB shows 100% from a previous batch-check (may be stale — needs validation)
- Completes the SmartGlass bidirectional communication path

### 2. XDK Header Improvements (In Progress)

Missing XDK declarations block stub implementation and future decomp work.

| Subsystem | Missing | Priority | Status |
|-----------|---------|----------|--------|
| NUI/Kinect (xnui.h) | 18 XNui* title-wrapper functions | High | New header needed |
| NUI skeleton | 3 functions | High | Add to nuiskeleton.h |
| NUI camera/image | 4 functions | High | Add to nuidetroit.h |
| NUI identity | 1 function (NuiIdentityEnroll) | Medium | Add to nuiidentity.h |
| NUI audio | 3 private functions | Medium | Add to nuiaudio.h |
| MMIO | 3 functions (mmioAscend/CreateChunk/Descend) | Medium | Add to mmio.h |
| XMic (Microphone) | ~8 public functions | Low | New header needed |
| XHV (Voice) | ~20 engine functions | Low | New header needed |

### 3. Stub Burndown (Ongoing)

Implementing function bodies for forward-declared stubs. Each stub implemented improves fuzzy match.
- Previous session: 43 stubs → +0.21% fuzzy match
- Remaining stubs: TBD (need audit)

## Realistic Achievable Targets

### Why 100% Is Rare

**~80% of near-match functions have unfixable patterns** creating permanent gaps.

| Pattern | Prevalence | Impact | Fixable |
|---------|-----------|--------|---------|
| LINKER_MERGED | 80% | 0.5-3% gap | No |
| BOOL_MASK | 5% | ~3% gap | No |
| REGISTER_SWAP | 80% | 1-3% gap | Patched post-build |
| FMA direction | Mixed | ~1% gap | No (mixed directions per function) |
| CONTROL_FLOW | 40% | 1-5% gap | 70% success |

### Match Quality Audit Results (2026-02-18)

- **95-100% range**: 0 fixable encoding patterns (all relocation/merged/regswap noise)
- **50-80% range**: 193 pattern hits in 79 functions (bool_mask: 108, FMA: 63) — all unfixable
- **Regswap patching**: 709 functions, +0.32% code match (post-build step, re-run after ninja)

## Goal Categories

### Tier 1: Implement Missing Function Bodies

**Target**: Implement stubs and missing functions to increase fuzzy match percentage.

This is the **highest-impact remaining work**. Each function body implementation directly increases the fuzzy match metric.

Priority targets:
- SmartGlass receive path (JsonToDta)
- Functions with RB3 reference implementations (shared Milo engine)
- Large functions (more bytes matched per function)

### Tier 2: XDK Header Coverage

**Target**: Declare all XDK functions used by the game to unblock future decomp.

Benefits:
- Enables proper type checking in decomp source
- Unblocks stub implementation for callers
- Improves code readability

### Tier 3: Runtime Validation (Xenia Headless)

**Target**: Validate decomp output runs correctly in Xenia.

Current state:
- Debug.xex boots with 60 guest memory patches (57 NUI + 3 XBC/SmartGlass stubs)
- Deferred rendering has gamma/readback issues
- PE Override blocked (decomp linker produces different function addresses)

### Tier 4: Accept AT_LIMIT Functions

**Target**: 0 further effort on the 6,406 AT_LIMIT functions.

All have been triaged. Remaining gaps are unfixable compiler artifacts (merged symbols, register allocation, FMA direction, bool masks).

## Known Gaps to Investigate

### Inlined Handler Functions (Not Individually Tracked)

Small handler functions called via the `HANDLE()` macro in `Handle()` dispatch methods are **inlined by the compiler** into the parent `Handle()` function. They don't exist as separate symbols in the binary, so they aren't tracked individually in `symbols.txt` or `decomp.db`.

**Impact**: These inlined handlers contribute to the parent `Handle()` function's match percentage but can't be measured or reported independently. For example, `UIManager::Handle` is 3,300 bytes (940 instructions) at 55.8% match — many of its mismatches come from stub handlers like `OnPushScreen`, `OnPopScreen`, `OnFocusPanel`, `OnSetSink`, `OnToggleDevMenu`, and `OnResetScreen` that currently return 0 but should have real implementations.

**Known affected Handle functions**:
- `UIManager::Handle` (0xCE4 bytes, 55.8%) — ~10 stub handlers inlined
- `Automator::Handle` (0x82C bytes, 41.1%) — stub handlers inlined

**Investigation needed**:
1. Audit all large `Handle()` functions to identify which have stub handlers folded in
2. Use Ghidra to recover the correct handler implementations
3. Track improvement via the parent Handle function's match percentage
4. Quantify the total byte impact of fixing these inlined stubs

This is potentially a significant source of recoverable match percentage, since Handle functions are large and their stubs are often simple to implement from Ghidra decompilation.

## Anti-Goals (What NOT to Do)

1. **Don't chase 100% on AT_LIMIT functions** — All 6,406 have been triaged as unfixable
2. **Don't count XDK progress** — 16,021 functions excluded from metrics
3. **Don't retry exhaustive batch sweeps** — Full triage already complete
4. **Don't spend >3-4 attempts on register allocation** — Diminishing returns

## Decision Framework

### "Should I work on this function?"

```
IF excluded = 1:
    SKIP (XDK/SDK)
ELIF status = 'AT_LIMIT':
    SKIP (already triaged as unfixable)
ELIF function has no implementation body (stub/forward-decl):
    YES, implement it (highest impact)
ELIF RB3 reference exists:
    YES, use as guide
ELIF function is large (>200 bytes):
    YES, high byte-count impact
ELSE:
    Use scoring model or skip
```

### "Is this function done?"

```
IF current_percent = 100:
    YES, perfect match
ELIF status = 'AT_LIMIT':
    YES, at practical limit (accept)
ELIF current_percent >= 99 AND has_linker_merged:
    YES, at practical limit
ELIF current_percent >= 97 AND has_bool_mask:
    YES, at practical limit
ELSE:
    NO, more work possible
```

## Key Metrics to Track

| Metric | Current | Notes |
|--------|---------|-------|
| Fuzzy match % | 44.06% | Source of truth for decomp progress |
| Matched code % | 35.99% | Byte-level accuracy |
| Complete units | 165 / 2,224 | Units where all functions match |
| COMPLETE functions | 25,442 | Out of 31,828 non-excluded |
| Stubs remaining | TBD | Forward-declared but unimplemented |
