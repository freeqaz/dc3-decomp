# Experiment 6: Callee Signatures

## Status: STUB IMPLEMENTATION

**Token cost**: ~1000 (estimated)
**Expected impact**: Low - marginal lookup savings
**Effort**: High

## Hypothesis

Pre-resolved callee signatures reduce lookup turns by:
1. Providing function signatures upfront
2. Eliminating need to grep headers
3. Showing parameter types and return values

## Implementation Status

This experiment has a **stub implementation** only. Full implementation requires:
1. Parsing cross-references from Ghidra or objdiff output
2. Extracting signatures from headers or DWARF
3. Significant token cost management

### Why Deprioritized

1. **High complexity**: Requires xref parsing and signature extraction
2. **Low expected impact**: Agents can use grep/read for signatures
3. **Token cost uncertainty**: Varies widely by function complexity
4. **Better alternatives**: Experiments 1-5 provide higher ROI

### Current Code

```python
def get_callee_signatures(symbol: str, xrefs_path: Optional[str] = None) -> Dict[str, Any]:
    """Stub implementation."""
    return {
        "count": 0,
        "signatures": [],
        "summary": "(callee signature lookup not fully implemented)",
    }
```

## Future Implementation

If experiments 1-5 prove successful and more enrichments are desired:

### Required Components

1. **Xref Parser**: Extract callees from objdiff JSON or Ghidra output
2. **Signature Extractor**: Look up signatures from:
   - Header files (grep + parse)
   - RB2 DWARF data
   - Database of known signatures
3. **Token Budget**: Limit signatures to most important callees

### Proposed Output Format

```markdown
## Called Function Signatures

**Found 5 callees with signatures.**

| Function | Signature |
|----------|-----------|
| BinStream::operator>> | `BinStream& operator>>(int&)` |
| Object::Find | `Object* Find(Symbol name, bool fail)` |
| MILO_ASSERT | `void MILO_ASSERT(bool, const char*)` |
```

## A/B Assignment

```python
# Defined but returns empty data
enrichment_flags.get("callee_signatures")  # True = treatment (no-op)
```

## Success Metrics

### Target: -2 avg iterations to final match%

Would require tracking:
- Number of header lookups per attempt
- Time spent searching for signatures

## Recommendation

**SKIP FOR NOW** - Focus on experiments 1-5 which have:
- Lower implementation complexity
- Higher expected impact
- Lower token cost uncertainty

Revisit if:
1. Experiments 1-5 show positive results
2. Token budget allows additional enrichments
3. Xref parsing infrastructure is built for other purposes

## Notes

- Could leverage Ghidra MCP `list_cross_references` tool
- Header parsing complexity varies by include structure
- May duplicate information agents can easily find themselves
