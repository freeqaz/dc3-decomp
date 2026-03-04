---
name: rb2-class
description: Get class layout from RB2 DWARF debug info. Shows member offsets, sizes, types, and inheritance. Ground truth for struct layouts when DC3 headers lack offset information.
argument-hint: "[class-name] [offset]"
allowed-tools: Bash(python3 -c *), Read, Grep, Glob
---

# RB2 Class Info Skill

Query the RB2 DWARF dump for class layout ground truth.

## Arguments

`$ARGUMENTS` - Class name (e.g., `Character`, `RndDir`). Optional offset to look up a specific member.

## Steps

1. **Parse the arguments.** `$0` is a class name. Optional `$1` is an offset.

2. **If an offset is given**, look up the specific member:
   ```bash
   python3 -c "
   import sys
   sys.path.insert(0, 'scripts')
   from orchestrator.rb2_dwarf import RB2DwarfParser, DEFAULT_RB2_DUMP

   parser = RB2DwarfParser(DEFAULT_RB2_DUMP)
   result = parser.get_member_at_offset('$0', $1)
   if result:
       print(f\"{result['class']}::{result['member']}\")
       print(f\"  Type: {result['type']}\")
       print(f\"  Offset: 0x{result['offset']:x}\")
       print(f\"  Size: {result['size']} bytes\")
   else:
       print(f'No member at offset $1 in $0')
   "
   ```

3. **Otherwise**, show full class layout:
   ```bash
   python3 -c "
   import sys
   sys.path.insert(0, 'scripts')
   from orchestrator.rb2_dwarf import RB2DwarfParser, DEFAULT_RB2_DUMP

   parser = RB2DwarfParser(DEFAULT_RB2_DUMP)
   info = parser.get_class('$0')
   if not info:
       similar = parser.search_classes('$0')[:10]
       if similar:
           print(f\"Class '$0' not found. Similar:\")
           for c in similar:
               print(f'  - {c}')
       else:
           print(f\"Class '$0' not found in RB2 DWARF\")
   else:
       print(f\"{info['kind']} {info['name']} (RB2 DWARF)\")
       print(f\"Size: 0x{info['total_size']:x} ({info['total_size']} bytes)\")
       if info['parents']:
           print(f\"Parents: {', '.join(info['parents'])}\")
       chain = parser.get_inheritance_chain('$0')
       if len(chain) > 1:
           print(f\"Chain: {' -> '.join(chain)}\")
       if info['members']:
           print()
           print(f\"{'Offset':<10} {'Size':<8} {'Type':<30} Name\")
           print('-' * 80)
           for m in info['members']:
               print(f\"0x{m['offset']:<8x} 0x{m['size']:<6x} {m['type']:<30} {m['name']}\")
   "
   ```

4. **Present the results** clearly, noting any differences from DC3 headers.

## Tips

- RB2 DWARF is ground truth for shared Milo engine classes (offset-accurate)
- DC3 may have additional members not in RB2 — compare carefully
- DWARF dump is at `~/code/milohax/rb3/doc/rb2_dump.cpp`
- Use alongside `/struct-info` (DC3 headers) and `/ghidra-struct` (binary analysis) for three-way comparison
