# objdiff - Assembly Diffing Tool

objdiff is an assembly comparison tool for decompilation projects. It compares compiled output from decompiled source code against the original binary, showing instruction-level differences to help achieve matching assembly.

**Binary:** `~/code/milohax/objdiff/target/release/objdiff-cli`

**Note:** This is a custom/extended version of objdiff with additional CLI features (report querying, JSON output, function search). The standard objdiff is available at https://github.com/encounter/objdiff

## Quick Reference

```bash
# Find near-match functions (good work targets)
objdiff-cli report query build/373307D9/report.json --functions \
  --min-percent 90 --max-percent 99 --limit 10

# Check a specific function's match status
objdiff-cli report function build/373307D9/report.json "Game::Poll"

# Interactive diff viewer (TUI)
objdiff-cli diff -p . "Game::Poll"

# Get JSON diff with instructions
objdiff-cli diff -p . "Game::Poll" -f json --include-instructions
```

## Key Commands

| Command | Purpose |
|---------|---------|
| `report summary` | Aggregate statistics from a report |
| `report query` | Filter/search functions and units |
| `report function` | Direct function lookup |
| `diff` | Compare target vs base assembly |

## Detailed Documentation

For comprehensive usage information, see:

- **[OBJDIFF_CLI_USAGE.md](../OBJDIFF_CLI_USAGE.md)** - Main usage guide with examples
- **[OBJDIFF_CLI_COMMANDS.md](../OBJDIFF_CLI_COMMANDS.md)** - Full command reference
- **[OBJDIFF_LEARNINGS.md](../OBJDIFF_LEARNINGS.md)** - Patterns and lessons learned from decomp work

## Typical Workflow

1. **Find work targets:**
   ```bash
   objdiff-cli report query build/373307D9/report.json --functions \
     --min-percent 90 --max-percent 99 --sort-by size --sort-order asc
   ```

2. **Investigate a function:**
   ```bash
   objdiff-cli diff -p . "TargetFunc"
   ```

3. **Edit source, rebuild, compare:**
   ```bash
   ninja && objdiff-cli diff -p . "TargetFunc"
   ```

4. **Verify match:**
   ```bash
   objdiff-cli report function build/373307D9/report.json "TargetFunc"
   ```
