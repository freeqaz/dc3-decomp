# Ghidra MCP Skills — Session Notes (2026-02-17)

Features in pyghidra-mcp that we've implemented as CLI tools for decomp work.

## Tools Implemented

### 1. Struct Validation — `tools/ghidra/struct_check.py`

Compares our C++ header struct layouts (from `struct_db.sqlite`) against Ghidra's DTM.

```bash
python3 tools/ghidra/struct_check.py HamDirector
python3 tools/ghidra/struct_check.py --unit system/char/CharBones
python3 tools/ghidra/struct_check.py --all --pattern 'Rnd*'
```

**Status:** Working. Tested with HamDirector (80/80 fields OK). Handles Ghidra's `.conflict` naming when DTM has duplicate entries.

**Caveat:** Currently compares our seeded data against itself (since we seed from struct_db). Real value comes from comparing against Ghidra's *decompiler-inferred* types, which requires running `extract_structures` first.

### 2. Pcode Inspection — `tools/ghidra/pcode_inspect.py`

Analyzes decompiled output + raw PPC bytes for switch tables and cast operations.

```bash
python3 tools/ghidra/pcode_inspect.py "DataNode::Handle"
python3 tools/ghidra/pcode_inspect.py "0x82878b58" --switches
python3 tools/ghidra/pcode_inspect.py "CharBones::PoseMeshes" --casts
```

**Status:** Working. Decompiles via MCP, scans for PPC switch patterns (cmplwi/lwzx/mtctr/bctr) and sign/zero extension instructions, plus Ghidra C cast patterns.

**Note:** pyghidra-mcp doesn't expose raw pcode yet. This tool works around that by analyzing the decompiled C output + raw bytes. Adding a pcode endpoint to pyghidra-mcp would make this significantly more powerful.

### 3. Semantic Code Search — `tools/ghidra/code_search.py`

Vector search over all decompiled functions via ChromaDB.

```bash
python3 tools/ghidra/code_search.py "iterate list and delete each element"
python3 tools/ghidra/code_search.py --code "for (i = 0; i < count; i++) { arr[i]->Save(bs); }"
python3 tools/ghidra/code_search.py --strings "CharBones"
```

**Status:** Working. 42,212 functions indexed. Returns ranked results with full decompiled code snippets.

## Bugs Fixed During Testing

### pyghidra-mcp: ChromaDB batch size overflow
- **Bug:** `collection.add()` with 42K documents exceeds ChromaDB's 5461 max batch size
- **Fix:** Batch inserts at 5000 documents per call (`context.py`, both code and strings collections)

### pyghidra-mcp: `create_structures` creates empty structs
- **Bug:** `total_size: 0` passed from `batch_export_types.py` creates zero-length Ghidra structs, so `replaceAtOffset` silently fails for all members
- **Fix:** Auto-calculate total_size from max member offset + size (both in `batch_export_types.py` caller and `tools.py` server fallback)

### struct_check: Ghidra `.conflict` naming
- **Bug:** When DTM already has an empty shell struct, the seeded version lands as `Name.conflict` — exact regex `^Name$` misses it
- **Fix:** Match regex includes `.conflict` suffix, prefer candidate with most named members

### struct_check: Unnamed padding flood
- **Bug:** Ghidra fills struct gaps with 1-byte `undefined` padding, producing hundreds of OURS_MISSING rows
- **Fix:** Filter out Ghidra members with `name == None` before diffing

### pyghidra-mcp: DTM not persisted to disk
- **Bug:** Seeded structs, created functions, and applied signatures are lost on service restart. `close()` didn't call `project.save()`, and mutation tool handlers didn't save after transactions.
- **Fix:** Added `project.save(program)` in `close()` (context.py) and after `create_structures`, `bulk_create_functions`, and `apply_demangled_signatures` (server.py)

### pyghidra-mcp: `search_strings` crashes on n_results=0
- **Bug:** When `get()` returns all results, remaining `limit` becomes 0, and `query(n_results=0)` raises ValueError
- **Fix:** Early return if `limit <= 0` before the semantic query call (tools.py)

### batch_export_types: Member sizes all hardcoded to 4
- **Bug:** All struct members sent with `size: 4`, causing overlaps when fields are smaller (e.g., 3-byte `MatPerfSettings` at 0xdc before `bool` at 0xdf). `replaceAtOffset` silently skips overlapping inserts.
- **Fix:** Infer size from gap to next member offset, capped at 4 (batch_export_types.py)

## Log Audit Summary

Issues found in service logs after full startup + seeding:

| Issue | Severity | Status |
|-------|----------|--------|
| ChromaDB batch overflow | Error | **Fixed** — batched at 5000 |
| DTM not persisted | Error | **Fixed** — save after mutations |
| `search_strings` n_results=0 | Error | **Fixed** — early return |
| Member size=4 overlaps | Bug | **Fixed** — infer from gaps |
| `install_plugin` directory error | Warning | Harmless — fallback works (jar added to classpath) |
| ClientDisconnect on reconnect | Warning | Harmless — session race during batch seeding |
| Java Unsafe deprecation | Warning | Upstream Felix framework, no action needed |
| 3 CreateFunctionCmd failures | Info | Expected — ICF-merged addresses (nuispeech, jpeg) |
| 10 no-function-at-address | Info | Expected — same 3 ICF addresses with multiple symbols |

## Prerequisites / Setup

All tools require a running pyghidra-mcp with seeded DTM and ChromaDB index:

```bash
# Start service (uses ghidra_projects/DC3/DC3 project)
./tools/ghidra/pyghidra-service.sh start

# Seed DTM with struct definitions + function signatures
python3 tools/ghidra/batch_export_types.py --seed

# ChromaDB indexes automatically on first startup (takes ~10 min for 42K functions)
# If empty, delete chromadb dir and restart:
#   rm -rf ghidra_projects/DC3/DC3/chromadb
#   ./tools/ghidra/pyghidra-service.sh restart
```

**Note:** After the persistence fix, seeded data (structs, functions, signatures) is saved to disk and survives restarts. First seed takes ~2 minutes; subsequent restarts skip it. ChromaDB index also persists.

## Comparison: GhidrAssistMCP

Evaluated https://github.com/jtang613/GhidrAssistMCP — a Ghidra GUI plugin with 34 MCP tools. Key things it has that we don't:
- Raw pcode output (would improve pcode_inspect significantly)
- Struct create/modify/merge via MCP (interactive editing)
- Basic block / CFG output
- Bookmark management

Our pyghidra-mcp is better suited for automated/headless decomp work with its caching, MSVC demangling, map file support, and XEX handling.

## Future Ideas

- **Pcode endpoint in pyghidra-mcp**: Add `get_pcode(function)` tool for raw pcode access
- **Persistent DTM**: Save Ghidra project after seeding so structs survive restarts
- **Struct diff against inferred types**: Compare headers vs decompiler-inferred layouts (not just seeded data)
- **Pre-commit struct check**: Run `struct_check.py` on header changes to catch layout regressions
- **Pcode-guided permuter**: Feed switch/cast info to the C++ permuter for targeted permutations
- **"Find similar matched functions"**: Combine code_search with decomp.db to find matched functions similar to your current target
