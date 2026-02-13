# Ghidra Type Seeding Stress Test - Session 1 Summary

**Date**: 2026-02-13
**Session Goal**: Set up infrastructure for stress testing Ghidra type seeding
**Status**: ✅ Complete (with known issues documented)

---

## Completed Tasks

### ✅ 1. Ghidra Service Setup
- **Service Status**: Running (PID: 4010222)
- **Endpoint**: `http://127.0.0.1:8000/mcp`
- **Session ID**: `81a090464bf74fffb3a2cca961287450`
- **Binary Loaded**: `default.xex-997567`

### ✅ 2. Type Seeding Applied
- **Structures Created**: 2,105 DC3 classes in Ghidra DTM
- **This Pointer Types Applied**: 11,733 / 17,706 functions (66% success)
- **Missing Functions**: 5,973 (likely SDK/library code not in binary)
- **Command**:
  ```bash
  python3 tools/ghidra/batch_export_types.py --seed
  ```

### ✅ 3. Documentation Created
- **Testing Protocol**: `docs/permuter/ghidra-stress-test/TESTING_PROTOCOL.md`
  - 5 test functions selected (95-98% match range)
  - Step-by-step workflow defined
  - Metrics tracking defined
- **Findings Template**: `docs/permuter/ghidra-stress-test/FINDINGS_TEMPLATE.md`
  - Structured format for documenting each function
  - Type quality rating scale (1-5)
  - Cross-function pattern tracking

### ✅ 4. Workflow Verification
- **Tested Function**: `CharBonesSamples::Load` (96.7% match)
- **Working Tools**:
  - ✅ `mcp__orchestrator__run_objdiff` - Perfect baseline analysis
  - ✅ `mcp__orchestrator__run_analyze_function` - Objdiff portion works
  - ✅ Standalone `mcp_client.py` - Direct Ghidra calls work
- **Issues Found**: See below

---

## Issues Discovered

### ⚠️ Issue #1: Ghidra Decompilation Fails in analyze_function.py

**Symptom**:
```
Error: Decompile failed: {'code': -32602, 'message': 'Invalid request parameters', 'data': ''}
```

**Root Cause**:
1. `tools/analyze_function.py` has a **duplicate MCP client implementation** separate from `tools/ghidra/mcp_client.py`
2. The duplicate client has session initialization issues:
   - Server logs show: `"Failed to validate request: Received request before initialization was complete"`
   - Requests arrive before MCP session is fully ready
3. Parameter mismatch was fixed (line 490: `"name"` → `"name_or_address"`) but didn't resolve the core issue

**Workaround Verified**:
The standalone `mcp_client.py` works correctly:
```python
from mcp_client import MCPClient
client = MCPClient()
client.initialize()
result = client.decompile_function("CharBonesSamples::Load")
# SUCCESS - returns decompiled code with types
```

**Impact on Stress Test**:
- **Low impact**: Can use workaround for Session 2
- **Options**:
  1. Use standalone `mcp_client.py` for Ghidra decompilation
  2. Fix `analyze_function.py` MCP client (requires refactor)
  3. Use m2c fallback (works but lacks type info - defeats purpose)

**Recommended Fix** (for later):
- Remove duplicate MCP client from `analyze_function.py`
- Import and use `mcp_client.MCPClient` directly
- Ensures consistent behavior across all tools

---

## Files Created

```
docs/permuter/ghidra-stress-test/
├── TESTING_PROTOCOL.md       # Workflow steps for Session 2
├── FINDINGS_TEMPLATE.md      # Per-function documentation template
└── SESSION1_SUMMARY.md       # This file
```

---

## Test Candidates Ready for Session 2

| Function | Symbol | Match | Size | Key Issues |
|----------|--------|-------|------|------------|
| **SaveLoadManager::Handle** | `?Handle@SaveLoadManager@@...` | 95.1% | 655 insn | 112 diff_arg (struct access) |
| **UIEventMgr::TriggerEvent** | `?TriggerEvent@UIEventMgr@@...` | 95.6% | 175 insn | 25 diff_arg + control flow |
| **CharBonesSamples::Load** | `?Load@CharBonesSamples@@...` | 96.7% | 73 insn | Register swaps + BinStream |
| **BaseMaterial::PropValDifferent** | `?PropValDifferent@BaseMaterial@@...` | 97.7% | 102 insn | Symbol comparison |
| **RhythmBattlePlayer::Load** | `?Load@RhythmBattlePlayer@@...` | 97.7% | 166 insn | BinStream + offsets |

**Full symbols in**: `TESTING_PROTOCOL.md`

---

## Validation Results

### ✅ Service Health
```bash
$ ./tools/ghidra/pyghidra-service.sh status
Service running (PID: 4010222)
URL: http://127.0.0.1:8000/mcp/v1
Status: Ready
```

### ✅ Type Seeding Active
```bash
$ python3 tools/ghidra/batch_export_types.py --seed
Applied: 11733
```

### ✅ Objdiff Analysis Works
```bash
$ mcp__orchestrator__run_objdiff "?Load@CharBonesSamples@@..." /path/to/project
Match: 96.7%
Verdict: MAYBE_FIXABLE
Patterns: LINKER_MERGED (1), REGISTER_SWAP (20)
```

### ⚠️ Ghidra Decompilation (Partial)
- **Via `mcp_client.py`**: ✅ Works
- **Via `analyze_function.py`**: ❌ Fails (session init issue)
- **m2c Fallback**: ✅ Works (but lacks types)

---

## Ready for Session 2?

**Status**: ✅ **YES** (with documented workaround)

**Prerequisites Met**:
- [x] Ghidra service running
- [x] Type seeding applied (11,733 functions)
- [x] Test candidates selected (5 functions)
- [x] Testing protocol documented
- [x] Findings template ready
- [x] Workflow tested (objdiff works, decompile has workaround)

**Blockers**: None (decompile issue has workaround)

**Recommended Approach for Session 2**:
1. Use `mcp__orchestrator__run_objdiff` for baseline analysis (works perfectly)
2. For Ghidra decompilation, use standalone `mcp_client.py` directly:
   ```python
   from tools.ghidra.mcp_client import MCPClient
   client = MCPClient()
   client.initialize()
   result = client.decompile_function("FunctionName")
   ```
3. Document type quality and insights as planned
4. Fix `analyze_function.py` MCP client in follow-up (not blocking)

---

## Next Steps

### Session 2 (80% of work)
1. Run through all 5 test functions using the workflow
2. Document findings for each (use template)
3. Identify patterns across functions
4. Propose tooling improvements
5. Optionally: Fix 1-2 functions if patterns clear

### Follow-up Items (Not Blocking)
- [ ] Fix `analyze_function.py` MCP client (refactor to use `mcp_client.py`)
- [ ] Consider merging duplicate MCP client implementations
- [ ] Add retry/wait logic for MCP session initialization

---

## Lessons Learned

1. **Duplicate code causes bugs**: Two MCP clients (`mcp_client.py` vs `analyze_function.py`) led to inconsistent behavior
2. **Test end-to-end early**: Found decompile issue during Session 1 setup, not mid-Session 2
3. **Workarounds are OK for research**: The goal is to evaluate type seeding, not perfect tooling (yet)
4. **Ghidra type seeding is working**: 11,733 functions typed, structures loaded - core infrastructure is solid

---

## Time Investment

- **Service setup**: ~15 min (port conflicts, sandbox issues)
- **Type seeding verification**: ~5 min
- **Documentation creation**: ~30 min
- **Workflow testing**: ~20 min
- **Issue diagnosis**: ~20 min

**Total Session 1**: ~90 min (on track for 20% of planned work)
