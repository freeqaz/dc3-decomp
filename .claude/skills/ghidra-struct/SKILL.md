---
name: ghidra-struct
description: Check struct/class layout against Ghidra's Data Type Manager. Compares DC3 header definitions with Ghidra's analysis to find offset mismatches. Use when debugging struct alignment issues or verifying class layouts.
argument-hint: "[class-name ...]"
allowed-tools: Bash(python3 tools/ghidra/struct_check.py *)
---

# Ghidra Struct Check

Compare DC3 header struct definitions against Ghidra's Data Type Manager.

## Arguments

`$ARGUMENTS` - One or more class names to check

## Usage

```bash
python3 tools/ghidra/struct_check.py ClassName [ClassName2 ...]
```

## Steps

1. **Run the check:**
   ```bash
   python3 tools/ghidra/struct_check.py HamDirector RndWind
   ```

2. **Review output** showing:
   - Offset comparison table
   - Field names from both sources
   - Status: OK or MISMATCH

3. **Investigate mismatches** by:
   - Checking header file for the class
   - Looking for missing padding or inheritance issues
   - Comparing with RB3 DWARF dump if available

## Output Format

```
======================================================================
  HamDirector
======================================================================
  Offset     Our Field                 Ghidra Field              Status
  ---------- ------------------------- ------------------------- ----------
  0x0048     unk48                     unk48                     OK
  0x005c     mSongAnims                mSongAnims                OK
  0x0150     mWorldPostProc            mWorldPostProc            OK
```

## Tips

- Check multiple classes at once for efficiency
- Use with `/rb2-class` for DWARF comparison
- Mismatches often indicate:
  - Missing base class members
  - Wrong member order
  - Incorrect padding
  - Wrong type sizes

## When to Use

- Before fixing offset mismatches in objdiff
- When adding new class members
- After making struct layout changes
- To verify inheritance chain is correct
