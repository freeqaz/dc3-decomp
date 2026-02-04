# Session: analyze-function Tool Improvements

**Date:** 2026-01-24
**Focus:** Comprehensive improvements to the `analyze-function` tool for agent-driven decompilation workflows

---

## Summary

Major overhaul of the `analyze-function` tool (`tools/analyze_function.py`) and its Ghidra MCP integration. Added debugging features, improved symbol resolution, implemented stub detection, and fixed cross-reference extraction. Also made decomp progress on several functions.

## Tool Improvements

### Phase 1: Client-Side Fixes (analyze_function.py)

| Feature | Description |
|---------|-------------|
| **Suggestion formatting** | Fixed raw dict output `{'action': '...'}` → plain text |
| **`--unit` option** | Added `-u, --unit` to disambiguate duplicate symbols |
| **`--quiet` flag** | Added `-q, --quiet` to suppress Ghidra connection warnings |
| **`--verbose` flag** | Added `-v, --verbose` for detailed symbol resolution debugging |
| **Destructor filtering (objdiff)** | Auto-filters `::dynamic atexit destructor` symbols in objdiff results |
| **Destructor filtering (Ghidra)** | Filters destructors during Ghidra symbol resolution |
| **Stub detection** | Detects when Ghidra returns stub/wrong function |
| **Address verification** | Verifies Ghidra's resolved address against symbols.txt |

### Phase 2: Server-Side Fixes (pyghidra-mcp)

| Feature | File | Description |
|---------|------|-------------|
| **Callee extraction** | `tools.py` | Added outbound references (functions this function calls) |
| **Caller extraction fix** | `tools.py` | Fixed inbound references using `find_function()` + `getEntryPoint()` |
| **Direction field** | `models.py` | Added `direction` field to `CrossReferenceInfo` |

## Verbose Mode Output

New `-v` flag shows the full resolution flow:
```
[analyze] Starting analysis for: RndMat::Copy
[analyze] objdiff symbol: RndMat::Copy
[analyze] Selected ghidra_name: RndMat::Copy
[resolve] Input name: RndMat::Copy
[resolve] Searching for mangled pattern: Copy@RndMat
[resolve] Found 3 result(s) for mangled pattern
[resolve]   - ?Copy@RndMat@@UAAXPBVObject@Hmx@@W4CopyType@23@@Z @ 826cc098
[resolve] Class matches for @RndMat@@: 1
[resolve] Single class match: ?Copy@RndMat@@...
[analyze] Expected address lookup: 0x826CC098
```

## Stub Detection

Automatically warns when Ghidra returns a stub:
```markdown
### Resolution Warnings
- Decompilation has only 1 body line(s) but objdiff reports 148 bytes
- Decompilation contains __savegprlr prologue but minimal body

**Recommendation**: Verify the correct function was resolved.
```

Heuristics implemented:
- Body line count vs expected size
- `__savegprlr` prologue detection
- Total character count vs function size
- False positive prevention for constructors/destructors/accessors

## Cross-Reference Fix

### Root Cause
`list_cross_references` used `find_symbol()` which returns a Symbol whose address may not be the function's entry point. `getReferencesTo()` needs the exact entry point.

### Fix
```python
# Before: used find_symbol()
sym = self.find_symbol(name_or_address)
addr = sym.getAddress()

# After: use find_function() first
try:
    func = self.find_function(name_or_address)
    addr = func.getEntryPoint()  # Correct address for callers
except ValueError:
    sym = self.find_symbol(name_or_address)
    addr = sym.getAddress()
```

## Decomp Progress

| Function | Before | After | Status |
|----------|--------|-------|--------|
| **RndMat::Copy** | 99.9% | 99.9% | AT_LIMIT - Single LINKER_MERGED diff (unfixable) |
| **MetagameRank::UpdateScore** | 47.6% | 60.6% | +13% - Added era scoring, birthday check, challenge XP |
| **BustAMovePanel::OnBeat** | 11.4% | 11.4% | Analysis complete - ~85% unimplemented |

### RndMat::Copy (99.9%)
Single instruction difference:
- Target: `bl merged_ObjRefConcCopyRef`
- Base: `bl ?CopyRef@?$ObjRefConcrete@VMetaMaterial@@...`

This is a linker optimization that merged identical template instantiations. Cannot be reproduced - accept current match.

### MetagameRank::UpdateScore (47.6% → 60.6%)
Added:
- Era-based symbols (era01-era05, era_tan_battle)
- Character birthday checking with DateTime
- Challenge mode XP awarding via `TheChallenges->GetBeatenChallengeXPs`
- Pose fatalities combo checking

Still needs:
- Star-based scoring
- Difficulty bonuses
- Intensity scoring
- DLC/fitness bonuses

### BustAMovePanel::OnBeat (11.4%)
Analysis revealed:
- 2594 DELETE instructions (code simply missing)
- Empty switch statement cases for all states
- Stubbed helper functions: `AdvanceFlashcards()`, `RepsToNextPhrase()`, etc.
- Requires substantial reverse engineering work

## Test Results Matrix

| Function | objdiff | Ghidra | Callees | Callers | Stub Warning |
|----------|---------|--------|---------|---------|--------------|
| Game::PollShuttle | 100% | Works | 5 | Pending* | - |
| RndMat::Copy | 99.9% | Stub | 1 | Pending* | Triggered |
| MetagameRank::UpdateScore | 60.6% | Stub | 1 | Pending* | Triggered |
| BustAMovePanel::OnBeat | 11.4% | Works | 375 | Pending* | - |
| operator<<<CharBonesObject> | 98.4% | Not found | - | - | - |
| PropSync<MoggClipMap> | 97.9% | Not found | - | - | - |

*Callers fix applied but requires MCP server restart

## Known Limitations

1. **Template/operator functions** - Ghidra can't resolve complex C++ template names with `<>` characters
2. **Some decompilations return stubs** - Ghidra decompiler limitation, but tool warns correctly
3. **Server restart required** - Changes to pyghidra-mcp require restarting the MCP server

## Files Modified

### Tool Files
- `tools/analyze_function.py` - All client improvements
- `venv/.../pyghidra_mcp/tools.py` - Callee + caller extraction
- `venv/.../pyghidra_mcp/models.py` - Added `direction` field

### Documentation
- `docs/tools/ANALYZE_FUNCTION.md` - Updated with all new features

### Source Files
- `src/lazer/meta_ham/MetagameRank.cpp` - Decomp improvements (+13%)

## New Command Examples

```bash
# Verbose mode for debugging
./bin/analyze-function "RndMat::Copy" -v

# Quiet mode (no Ghidra warnings)
./bin/analyze-function "Game::Poll" -q

# Specify unit to disambiguate
./bin/analyze-function "UpdateScore" -u default/lazer/meta_ham/MetagameRank

# Combined flags
./bin/analyze-function "Game::PollShuttle" -v -q --no-xrefs
```

## Next Steps

1. **Restart Ghidra MCP** to apply caller extraction fix
2. **Test callers** - Verify "Called by" now appears in cross-references
3. **Continue MetagameRank::UpdateScore** - Implement remaining scoring logic
4. **Template function resolution** - Consider using addresses from symbols.txt directly for template functions that Ghidra can't resolve by name
5. **BustAMovePanel::OnBeat** - Major implementation work needed if prioritized

## Patterns Discovered

### Destructor Symbol Naming
Static local variables generate `::dynamic atexit destructor` symbols:
```
void __cdecl `public: void __cdecl BustAMovePanel::OnBeat(void)'::`299'::matchedMessage::`dynamic atexit destructor'(void)
```
These should be filtered when looking for the main function.

### Address Resolution Priority
For reliable Ghidra decompilation:
1. Use mangled name from Ghidra resolution (most reliable)
2. Look up expected address in symbols.txt
3. Verify addresses match before trusting decompilation
