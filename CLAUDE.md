# DC3 Decomp - Claude Context

Dance Central 3 decompilation for Xbox 360 (PowerPC). Goal: produce matching assembly from C++ source.

The target binary in `orig/` is a debug build pulled from an Xbox 360 dev unit (not retail). This means no link-time optimization (LTCG) - matching should be achievable for most functions.

## Key Commands

```bash
ninja                              # Build
ninja build/373307D9/report.json   # Generate progress report
```

Check `./docs/tools/INDEX.md` for a full list of decomp tools. Especially `./src/master_agent_prompt.md` for what tool to use + when.

## objdiff CLI

Compares compiled C++ against the original binary. Extended for this project to:
- Find near-match functions worth investigating
- Diagnose why code doesn't match (merged calls, register alloc, bool masks)
- Verdict whether a function is fixable or at its limit
- Track progress over time

**Use `./bin/objdiff-cli`** (not the system `objdiff-cli` which lacks extended commands).

Usage: [docs/tools/objdiff/USAGE.md](docs/tools/objdiff/USAGE.md)

Note: objdiff is the source of truth for decomp percentages. Our database and report.json can be out of sync with the code.

## Code Style
- Be carefuly when modifying MILO_ASSERT() calls or OBJ_MEM_OVERLOAD macros. Whatever is in there should be tested carefully.
- Keep members protected/private unless confirmed public via DWARF or asserts. For external access, add getters/setters rather than making members public. Use friend classes for closely related types (e.g., Foo and FooHandle).

## Known Patterns
- **Unsigned zero comparisons**: Use `x > 0` instead of `x != 0` for unsigned types (generates `ble` vs `beq`)
- **Merged symbols**: `merged_<addr>` names indicate Identical COMDAT Folding (ICF) where the linker merged functions with identical machine code to a single address

## Git Commits

- Do not include `Co-Authored-By` lines in commit messages

## Git Worktrees

When creating worktrees for PR branches, symlink clangd config to avoid false diagnostics:
```bash
git worktree add /tmp/claude/my-branch my-branch
ln -s /home/free/code/milohax/dc3-decomp/compile_commands.json /tmp/claude/my-branch/
ln -s /home/free/code/milohax/dc3-decomp/.clangd /tmp/claude/my-branch/
```

## Project Structure

- `src/` - Decompiled C++ source (mirrors original structure)
- `build/` - Build outputs, object files, `373307D9/report.json`
- `include/` - Headers
- `objdiff.json` - Project config for objdiff

## Ghidra MCP Integration

The `analyze-function` tool uses Ghidra MCP for decompilation and cross-reference analysis.

Ghidra MCP runs on `http://127.0.0.1:8000/mcp` (not `/mcp/v1`). Session ID headers are automatically handled by the MCPClient class. May fail due to sandbox restrictions.

## Decomp Docs

- [docs/decomp/TECHNICAL_NOTES.md](docs/decomp/TECHNICAL_NOTES.md) - Compiler quirks, patterns
- [docs/decomp/RB3_REFERENCE.md](docs/decomp/RB3_REFERENCE.md) - Rock Band 3 decomp reference (shared engine)
- [docs/decomp/SUBAGENT_STRATEGY.md](docs/decomp/SUBAGENT_STRATEGY.md) - Parallel agent strategy for batch decomp work
