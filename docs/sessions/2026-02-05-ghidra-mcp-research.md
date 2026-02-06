# Ghidra MCP Enhancement Research Results

**Date**: 2026-02-05
**Purpose**: Evaluate current Ghidra MCP setup and identify enhancement opportunities for stuck decomp functions

## Executive Summary

**Current setup is sufficient for primary use cases.** The analyze-function tool already integrates Ghidra decompilation seamlessly. No immediate enhancements needed for the RhythmBattle functions.

## Findings

### 1. Current Capabilities (Already Working)

| Feature | Status | Tool |
|---------|--------|------|
| Decompile function | ✅ Working | `mcp__orchestrator__run_analyze_function` |
| Pseudo-C output | ✅ Working | Integrated in analyze-function |
| m2c decompilation | ✅ Working | Integrated in analyze-function |
| Symbol lookup | ✅ Working | Multi-strategy (map file + Ghidra) |
| Cross-references | ✅ Available | `list_cross_references` MCP tool |
| Call graph | ✅ Available | `gen_callgraph` MCP tool |
| String search | ✅ Available | `search_strings` MCP tool |
| Semantic code search | ✅ Available | `search_code` (ChromaDB) |

### 2. Test Results

Successfully ran `analyze-function` for both stuck functions:

**RhythmBattle::OnReset (55.7%)**
- Ghidra decompilation shows full control flow
- Reveals CAMP_MINDCONTROL string comparison logic
- Shows ObjPtr access patterns and Message construction
- m2c output provides typed function signatures

**UpdateMindControl (35%)**
- Ghidra decompilation shows missing logic:
  - `CAMP_MINDCONTROL` string comparison (strcmp)
  - `CAMP_MINDCONTROL_DANCE` property setting
  - `CAMP_6.3_DCI_mind_control_03.shot` comparison
  - Threshold checks: 0.5/0.95 for grooving, 0.2/12.0 for not_grooving
  - ForceShot call to gNullStr

### 3. Fork vs Upstream Comparison

| Feature | Our Fork (0.1.6+) | Upstream (0.1.13) |
|---------|-------------------|-------------------|
| XEX support | ✅ Enhanced | ❌ None |
| Xenon language | ✅ Auto-detected | ❌ Not supported |
| FastMCP transport | ✅ Yes | ✅ Yes |
| Semantic search | ✅ ChromaDB | ✅ ChromaDB |
| Struct/type analysis | ❌ None | ❌ None |
| Data type export | ❌ None | ❌ None |

**Verdict**: No benefit from upgrading to upstream v0.1.13. Our fork has critical XEX support they don't have.

### 4. What's NOT Available (Anywhere)

Neither our fork nor upstream has:
- `get_data_type(class_name)` - Get struct layout
- `list_structures()` - List all defined types
- `get_function_variables(func)` - Stack/local variables
- `get_field_at_offset(class, offset)` - Field at offset lookup

However, our orchestrator already provides complementary tools:
- `mcp__orchestrator__lookup_struct_offset` - Field at offset lookup
- `mcp__orchestrator__struct_info` - Class layout info
- `mcp__orchestrator__get_rb2_class_info` - RB2 DWARF class info

### 5. Service Status

The pyghidra-mcp service is configured but **not currently running**. This doesn't matter because:
1. The orchestrator's analyze-function tool works independently
2. Service is only needed for direct MCP tool calls
3. Claude Code MCP integration uses stdio transport, not HTTP

## Architecture Understanding

```
┌─────────────────────────────────────────────────────────────┐
│                     Claude Code                              │
└─────────────────────────────────────────────────────────────┘
                              │
              ┌───────────────┼───────────────┐
              │               │               │
              ▼               ▼               ▼
┌─────────────────┐ ┌─────────────────┐ ┌─────────────────┐
│  orchestrator   │ │ pyghidra-stdio  │ │   decomp.me     │
│   MCP server    │ │   MCP server    │ │   MCP server    │
│                 │ │   (optional)    │ │                 │
│ • run_objdiff   │ │ • decompile_fn  │ │ • scratches     │
│ • run_analyze   │ │ • search_code   │ │                 │
│ • struct_info   │ │ • gen_callgraph │ │                 │
│ • lookup_rb3    │ │ • list_xrefs    │ │                 │
└─────────────────┘ └─────────────────┘ └─────────────────┘
        │                   │
        │                   │
        ▼                   ▼
┌─────────────────┐ ┌─────────────────┐
│   objdiff-cli   │ │  Ghidra/pyghidra│
│   (extended)    │ │  + XEXLoaderWV  │
└─────────────────┘ └─────────────────┘
```

**Key Insight**: The orchestrator's `run_analyze_function` command launches Ghidra internally via pyghidra. It doesn't require the HTTP service to be running.

## Recommendations

### Immediate (No Changes Needed)

1. **Use analyze-function for stuck functions** - Already provides Ghidra decompilation
2. **Reference Ghidra pseudo-C for missing logic** - UpdateMindControl now has clear targets
3. **Use m2c output for type hints** - Function signatures help with implementation

### Future Enhancements (Optional)

If we need deeper Ghidra integration later:

**Option A: Add struct analysis to analyze-function**
```python
# Add to orchestrator's analyze_function.py
def get_struct_at_offset(class_name: str, offset: int) -> str:
    """Query Ghidra DataTypeManager for field at offset"""
    # Uses existing Ghidra context from decompilation
```

**Option B: Extend pyghidra-mcp fork**
- Add `get_data_type` MCP tool
- Add `list_structures` MCP tool
- Low priority - orchestrator already has struct tools

**Option C: Cross-reference Ghidra + RB2 DWARF**
- Ghidra infers types from analysis
- RB2 DWARF has ground truth offsets
- Combine for higher confidence

## Actionable Next Steps for RhythmBattle

Based on Ghidra decompilation analysis:

### UpdateMindControl (35% → target: 80%+)

Missing implementations identified from Ghidra:

1. **Add CAMP_MINDCONTROL string comparison**
   ```cpp
   if (strcmp(TheHamDirector->unk268->unkFC, "CAMP_MINDCONTROL") == 0) {
       unk10c = 0.0f;
       unk110 = 0.0f;
       // CAMP_MINDCONTROL_DANCE shot check
   }
   ```

2. **Add ForceShot call**
   ```cpp
   TheHamDirector->ForceShot(gNullStr);
   ```

3. **Add SetProperty for CAMP_MINDCONTROL_DANCE**
   ```cpp
   DataNode node(Symbol("CAMP_MINDCONTROL_DANCE"));
   TheHamDirector->SetProperty(Symbol("shot"), node);
   ```

4. **Fix threshold values**
   - Grooving: `unk10c > 0.5f && unk10c < 0.95f && unk110 > 5.0f`
   - Not grooving: `unk10c < 0.2f && unk110 > 12.0f`

### OnReset (55.7% → likely stuck at ~60%)

Blockers identified:
- `merged_SetVirtualObjConcrete` - ICF merged call, unfixable
- ~64 missing instructions in Message/ObjPtr patterns

Can still improve:
- Fix field access order (OFFSET_SWAP pattern)
- Restructure control flow (CONTROL_FLOW pattern)

## Files Referenced

- `.mcp.json` - MCP server configuration
- `tools/pyghidra-mcp-fork/pyghidra_mcp/tools.py` - Fork tool implementations
- `tools/pyghidra-mcp-fork/pyghidra_mcp/server.py` - Fork MCP server
- `tools/ghidra/mcp_client.py` - HTTP client for service mode
- `docs/tools/GHIDRA_MCP_INTEGRATION.md` - Integration docs

## Conclusion

The Ghidra MCP integration is **fully operational** through the orchestrator's analyze-function tool. No infrastructure changes needed. The RhythmBattle functions can be improved using the Ghidra decompilation output already available.
