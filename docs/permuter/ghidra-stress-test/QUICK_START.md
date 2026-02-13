# Ghidra Stress Test - Quick Start (Session 2)

**For the agent/user running Session 2: Everything is ready. Use this guide.**

---

## Prerequisites Check

```bash
# 1. Ghidra service running?
./tools/ghidra/pyghidra-service.sh status
# Should show: "Service running... Status: Ready"

# 2. Type seeding applied?
python3 tools/ghidra/batch_export_types.py --seed | tail -5
# Should show: "Applied: 11733" (or similar)

# 3. Working directory
cd /home/free/code/milohax/dc3-decomp
```

---

## Test Functions (Copy-Paste Ready)

```python
test_functions = {
    "SaveLoadManager::Handle": {
        "symbol": "?Handle@SaveLoadManager@@QAEXPAVUI@HAVUIListLabel@@HH@Z",
        "file": "src/lazer/meta_ham/SaveLoadManager.cpp",
        "match": "95.1%",
        "size": 655,
        "issues": "112 diff_arg (struct access)"
    },
    "UIEventMgr::TriggerEvent": {
        "symbol": "?TriggerEvent@UIEventMgr@@QAAXABVsymbol@@PAVDataArray@@@Z",
        "file": "src/lazer/meta_ham/UIEventMgr.cpp",
        "match": "95.6%",
        "size": 175,
        "issues": "25 diff_arg + control flow"
    },
    "CharBonesSamples::Load": {
        "symbol": "?Load@CharBonesSamples@@QAAXAAVBinStream@@@Z",
        "file": "src/system/char/CharBonesSamples.cpp",
        "match": "96.7%",
        "size": 73,
        "issues": "Register swaps + BinStream"
    },
    "BaseMaterial::PropValDifferent": {
        "symbol": "?PropValDifferent@BaseMaterial@@QAA_NABVsymbol@@0@Z",
        "file": "src/system/rndobj/BaseMaterial.cpp",
        "match": "97.7%",
        "size": 102,
        "issues": "Symbol comparison"
    },
    "RhythmBattlePlayer::Load": {
        "symbol": "?Load@RhythmBattlePlayer@@UAA_NPAVBinStream@@_N@Z",
        "file": "src/system/hamobj/RhythmBattlePlayer.cpp",
        "match": "97.7%",
        "size": 166,
        "issues": "BinStream + offsets"
    }
}
```

---

## Workflow Per Function (30 min max each)

### Step 1: Baseline (3-5 min)

```bash
# Get objdiff analysis
mcp__orchestrator__run_objdiff \
  "[SYMBOL_HERE]" \
  /home/free/code/milohax/dc3-decomp
```

**Record**: Match %, verdict, patterns, mismatch counts

---

### Step 2: Ghidra Decompilation (5-10 min)

**Option A: Using mcp_client.py (RECOMMENDED)**

```python
import sys
sys.path.insert(0, '/home/free/code/milohax/dc3-decomp/tools/ghidra')
from mcp_client import MCPClient

client = MCPClient()
client.initialize()

# Decompile with types
result = client.decompile_function("[FUNCTION_NAME_HERE]")

# result is a dict with:
# - 'code': Decompiled C code (with types!)
# - 'function_name': Ghidra function name
# - 'address': Function address
print(result['code'])
```

**Option B: Use m2c fallback (no types)**

```bash
./tools/decompile.sh "[DEMANGLED_NAME_HERE]"
```

---

### Step 3: Read Our Source (2-3 min)

```bash
# Read our current implementation
# (Use Read tool on the .cpp file from test_functions dict)
```

**Compare**:
- Do types match between Ghidra and our headers?
- Are struct offsets correct?
- Are member names aligned?

---

### Step 4: Supplemental Analysis (as needed)

```bash
# Resolve specific offset
mcp__orchestrator__lookup_struct_offset "ClassName" "0x48"

# Get class layout
mcp__orchestrator__struct_info "ClassName"

# Deep dive on mismatches
mcp__orchestrator__run_diff_inspect \
  "[SYMBOL]" \
  "diagnose" \
  /home/free/code/milohax/dc3-decomp
```

---

### Step 5: Document Findings (5 min)

Copy `FINDINGS_TEMPLATE.md` and fill out:

```bash
cp docs/permuter/ghidra-stress-test/FINDINGS_TEMPLATE.md \
   docs/permuter/ghidra-stress-test/findings_CharBonesSamples.md

# Fill in the template with your observations
```

**Key questions**:
1. Did type info reveal the root cause? (Yes/No/Partial)
2. Type quality rating (1-5 for each aspect)
3. Time to identify root cause (minutes)
4. Is this fixable or systemic (register allocation, etc)?

---

## Python Helper for Bulk Analysis

```python
#!/usr/bin/env python3
"""Quick Ghidra decompilation for all test functions."""
import sys
sys.path.insert(0, '/home/free/code/milohax/dc3-decomp/tools/ghidra')
from mcp_client import MCPClient

test_functions = {
    "SaveLoadManager::Handle": "?Handle@SaveLoadManager@@QAEXPAVUI@HAVUIListLabel@@HH@Z",
    "UIEventMgr::TriggerEvent": "?TriggerEvent@UIEventMgr@@QAAXABVsymbol@@PAVDataArray@@@Z",
    "CharBonesSamples::Load": "?Load@CharBonesSamples@@QAAXAAVBinStream@@@Z",
    "BaseMaterial::PropValDifferent": "?PropValDifferent@BaseMaterial@@QAA_NABVsymbol@@0@Z",
    "RhythmBattlePlayer::Load": "?Load@RhythmBattlePlayer@@UAA_NPAVBinStream@@_N@Z"
}

client = MCPClient()
client.initialize()

for name, symbol in test_functions.items():
    print(f"\n{'='*80}")
    print(f"Function: {name}")
    print(f"Symbol: {symbol}")
    print('='*80)

    try:
        # Try demangled name first
        result = client.decompile_function(name)
        print(result['code'])
    except Exception as e:
        print(f"Demangled name failed, trying symbol: {e}")
        try:
            result = client.decompile_function(symbol)
            print(result['code'])
        except Exception as e2:
            print(f"ERROR: {e2}")
```

Save as `/tmp/claude/decompile_all.py` and run:
```bash
python3 /tmp/claude/decompile_all.py > /tmp/claude/all_decompilations.txt
```

---

## Success Criteria (Remember!)

This is **investigative work**, not completion:

✅ **Minimum**:
- Ran workflow on all 5 functions
- Documented findings for each
- Identified 2-3 tooling improvements
- Answered: "Does type seeding help?"

🎯 **Stretch**:
- Fixed 1-2 functions to 100%
- Discovered reusable patterns

⚠️ **Avoid**:
- Spending >30 min per function
- Getting stuck on unfixable issues
- Trying to complete everything

---

## Common Pitfalls

### "Ghidra decompilation failed"
- Use `mcp_client.py` directly (see Option A above)
- Service might need restart: `./tools/ghidra/pyghidra-service.sh restart`

### "Types look wrong"
- Check if struct_db has the class: `grep "ClassName" config/structs/*.yml`
- Verify type seeding: `python3 tools/ghidra/batch_export_types.py --seed`

### "Too many register swaps"
- Document as "likely unfixable" if >10 swaps across >3 pairs
- Don't spend more than 2-3 variable reorder attempts

### "Objdiff shows LINKER_MERGED"
- Use `mcp__orchestrator__lookup_merged_symbol` to verify
- If verified, mark as at_limit (unfixable)

---

## After Session 2

Create `SUMMARY.md`:
- What patterns did type seeding solve?
- What remained unclear?
- Tooling gaps identified?
- Overall value assessment (worth the investment?)

**Template**:
```markdown
# Cross-Function Findings

## Type Seeding Successes
- [Pattern 1]: Types revealed X in N functions
- [Pattern 2]: ...

## Type Seeding Limitations
- [Gap 1]: Couldn't identify Y despite types
- [Gap 2]: ...

## Tooling Improvements Needed
1. [Specific request with rationale]
2. ...

## Overall Assessment
Type seeding was [very helpful / somewhat helpful / not helpful] because...
```

---

## Questions?

- **Testing Protocol**: `docs/permuter/ghidra-stress-test/TESTING_PROTOCOL.md`
- **Findings Template**: `docs/permuter/ghidra-stress-test/FINDINGS_TEMPLATE.md`
- **Session 1 Summary**: `docs/permuter/ghidra-stress-test/SESSION1_SUMMARY.md`
