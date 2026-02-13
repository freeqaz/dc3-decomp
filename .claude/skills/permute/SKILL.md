---
name: permute
description: Run the source permuter on a function to find signed/unsigned and variable extraction improvements. Use when working on a function that isn't matching 100% and you want to automatically try source variations.
argument-hint: "[symbol-or-function] [--apply] [--batch]"
allowed-tools: Bash(venv/bin/python *), Bash(ninja *), Read, Grep, Glob
---

# Permuter Skill

Run the source permuter to automatically try signed/unsigned casts and variable extraction
patterns on a decomp function, scoring each variant with objdiff.

## Arguments

`$ARGUMENTS`

## Modes

### Single Function Mode (default)

When given a function name or symbol, run the permuter on that specific function.

**Steps:**

1. **Resolve the function info.** The argument `$0` can be:
   - A mangled symbol (e.g., `?Seek@AsyncFile@@UAAHHH@Z`)
   - A qualified C++ name (e.g., `AsyncFile::Seek`)
   - A partial name (e.g., `Seek`) — search decomp.db to find the best match

2. **Look up source path and unit** from `decomp.db` and `objdiff.json`:
   ```bash
   venv/bin/python -c "
   import sqlite3, json
   conn = sqlite3.connect('decomp.db')
   conn.row_factory = sqlite3.Row
   # Try exact symbol match first, then demangled LIKE match
   row = conn.execute('''
       SELECT symbol, demangled, unit, current_percent, verdict
       FROM functions WHERE symbol = ? OR demangled LIKE ?
       LIMIT 1
   ''', ('$0', '%$0%')).fetchone()
   if row:
       print(json.dumps(dict(row)))
   "
   ```

   Then get the source_path from objdiff.json:
   ```bash
   venv/bin/python -c "
   import json
   data = json.load(open('objdiff.json'))
   for u in data['units']:
       if u['name'] == 'UNIT_NAME':
           print(u['metadata'].get('source_path', ''))
           break
   "
   ```

3. **Extract the qualified C++ name** from the demangled signature using regex:
   Match pattern: `([\w~][\w:~]*(?:::[\w~]+)+)\s*\(`

4. **Run the permuter:**
   ```bash
   venv/bin/python -m scripts.permuter \
       --symbol SYMBOL \
       --source SOURCE_PATH \
       --function QUALIFIED_NAME \
       --stop-on-perfect
   ```

   If `--apply` is in the arguments, add the `--apply` flag to auto-apply the best improvement.

5. **Report results** to the user:
   - Baseline match percentage
   - Number of variants tested
   - Any improvements found (with diffs)
   - Whether the improvement was applied

### Batch Mode (`--batch`)

When `--batch` is in the arguments, run the batch validator across multiple functions.

```bash
venv/bin/python -m scripts.permuter.batch_validate $ARGUMENTS
```

Pass through any additional flags like `--limit`, `--min-pct`, `--max-pct`, `--apply-all`.

## Tips

- The permuter works best on functions at 50-99% match — it finds signed/unsigned mismatches
  and variable extraction opportunities that are hard to spot manually.
- Build failure rate should be under 10%. If it's higher, something may be wrong.
- For functions with no variants generated, the function likely has no comparisons or
  variable expressions that match the permuter's patterns.
- If a variant improves the match, review the diff carefully before applying — the permuter
  finds mechanical fixes, not semantic ones.
