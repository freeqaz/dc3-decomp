---
name: struct-info
description: Get class/struct layout info including members, offsets, parents, and inheritance chain. Also look up which field is at a specific offset. Use when debugging struct alignment or verifying class layouts.
argument-hint: "[class-name] [offset]"
allowed-tools: Bash(python3 tools/struct_db.py *), Read, Grep, Glob
---

# Struct Info Skill

Query the struct database for class/struct layout information.

## Arguments

`$ARGUMENTS`

## Steps

1. **Parse the arguments.** `$0` is a class name. Optional `$1` is an offset to look up a specific field.

2. **If an offset is given**, look up the field at that offset:
   ```bash
   python3 tools/struct_db.py lookup $0 $1
   ```

3. **Otherwise**, show full class info:
   ```bash
   python3 tools/struct_db.py info $0
   ```

4. **Present the results** showing:
   - File path where the class is defined
   - Parent classes and inheritance chain
   - Member table with offsets, types, and names

## Tips

- Offsets can be hex (`0x48`) or decimal (`72`)
- If the struct DB is missing, rebuild it: `python3 tools/struct_db.py build src/`
- Use `python3 tools/struct_db.py list --pattern '*Rnd*'` to search for classes by name pattern
- Compare with `/rb2-class` for RB2 DWARF ground truth when DC3 headers may be wrong
- Compare with `/ghidra-struct` to cross-check against Ghidra's analysis
