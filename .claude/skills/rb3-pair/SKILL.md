---
name: rb3-pair
description: Get RB3 file pairing info for a DC3 unit. Shows compatibility score, function overlap, and optionally the RB3 source code. Use to leverage RB3 reference implementations for shared Milo engine code.
argument-hint: "[unit-name] [--source]"
allowed-tools: Bash(python3 -c *), Read, Grep, Glob
---

# RB3 Pair Skill

Find the corresponding RB3 source file for a DC3 unit and show compatibility info.

## Arguments

`$ARGUMENTS` - A DC3 unit name (e.g., `CharBones`, `system/char/CharBones`). Add `--source` to include the full RB3 source code.

## Steps

1. **Parse the arguments.** `$0` is a unit name. Check if `--source` flag is present.

2. **Look up the RB3 pairing:**
   ```bash
   python3 -c "
   import sys, json
   sys.path.insert(0, 'scripts')
   from orchestrator.rb3_pairing import find_rb3_file
   from orchestrator.database import get_connection
   from pathlib import Path

   unit = '$0'
   rb3_root = Path.home() / 'code/milohax/rb3/src'

   # Try finding directly
   rb3_file = find_rb3_file(unit, rb3_root)
   if rb3_file:
       print(f'RB3 File: {rb3_file}')
   else:
       print(f'No RB3 file found for: {unit}')
   "
   ```

3. **If `--source` is requested and a file was found**, read the RB3 source file with the Read tool.

4. **For function overlap**, also check the database:
   ```bash
   python3 -c "
   import sys
   sys.path.insert(0, 'scripts')
   from orchestrator.database import get_connection

   unit = '$0'
   conn = get_connection('decomp.db')
   # Try various unit path formats
   for prefix in ['', 'default/', 'default/system/']:
       row = conn.execute(
           'SELECT rb3_file, rb3_score FROM units WHERE unit = ?',
           (prefix + unit,)
       ).fetchone()
       if row:
           print(f'Unit: {prefix + unit}')
           print(f'RB3 File: {row[0]}')
           print(f'Score: {row[1]}')
           break
   else:
       print('Not found in units table')
   "
   ```

## Tips

- The RB3 source is at `~/code/milohax/rb3/src/`
- Use `mcp__orchestrator__lookup_rb3` MCP tool to grep RB3 for specific symbols
- DC3 and RB3 share the Milo engine — most `system/` code is similar or identical
- Compatibility score indicates how much the code overlaps
