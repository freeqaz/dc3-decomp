# DC3 Decomp - Claude Context

Dance Central 3 decompilation for Xbox 360 (PowerPC). Goal: produce matching assembly from C++ source.

The target binary in `orig/` is a debug build pulled from an Xbox 360 dev unit (not retail). This means no link-time optimization (LTCG) - matching should be achievable for most functions.

## Key Commands

```bash
ninja                              # Build and regenerate report.json
scripts/measure_progress.sh --functions --detailed HEAD  # Progress vs commit
scripts/measure_progress.sh --current-dir /path/to/worktree HEAD  # Worktree vs commit
scripts/clean_stale_objects.sh     # Fix stale .obj files (older than PCH)
```

Check `./docs/tools/INDEX.md` for agent tool selection and `./docs/INDEX.md` for the full docs sitemap.

## Orchestrator MCP Tools

Use the `mcp__orchestrator__` tools for all decomp analysis. Do not call `objdiff-cli` directly.

- `run_objdiff` — build + diff a function, returns match%, verdict, enrichment. Source of truth for decomp percentages.
- `run_diff_inspect` — deeper analysis: `diagnose` (root cause), `mismatches` (instruction table), `clusters`, `regswaps`, etc.
- `run_analyze_function` — combines objdiff with struct offset resolution for field-level mismatch context.
- `query_functions` — find workable functions by unit pattern and match range.
- `lookup_rb3` — grep RB3 codebase for reference implementations (shared Milo engine).

## Code Style
- Be carefuly when modifying MILO_ASSERT() calls or OBJ_MEM_OVERLOAD macros. Whatever is in there should be tested carefully.
- Keep members protected/private unless confirmed public via DWARF or asserts. For external access, add getters/setters rather than making members public. Use friend classes for closely related types (e.g., Foo and FooHandle).

## Known Patterns
- **Unsigned zero comparisons**: Use `x > 0` instead of `x != 0` for unsigned types (generates `ble` vs `beq`)
- **Merged symbols**: `merged_<addr>` names indicate Identical COMDAT Folding (ICF) where the linker merged functions with identical machine code to a single address
- **Automatic header tracking**: Ninja tracks all header dependencies via `/showIncludes` + wibo path rewriting. Touching any header automatically rebuilds only the affected .obj files. No manual `touch` needed.
- **Stale object diagnosis**: `scripts/clean_stale_objects.sh --dry-run` finds .obj files older than the PCH. Use `--all` to force-touch every .cpp for a full rebuild.
For a complete collection of patterns, find then under ./docs/decomp/patterns/ -- these are incredibly helpful for identifying 'hard' fixes when decompiling.

## Git Commits

- Do not include `Co-Authored-By` lines in commit messages

## Git Worktrees

Use `scripts/setup_worktree.sh <path> <branch>` to create worktrees with a working build system (configures ninja, symlinks tools/compilers/target objects).

## Project Structure

- `src/` - Decompiled C++ source (mirrors original structure)
- `build/` - Build outputs, object files, `373307D9/report.json`
- `include/` - Headers
- `native/` - Native port (x86_64 Linux, WebGPU renderer)
  - Note: You must skip the sandbox for GPU access.
- `objdiff.json` - Project config for objdiff

## Test Assets

Pre-extracted .milo_xbox files for the native port viewer/tests:
- `~/code/milohax/milo-engine-libs/harmonix-repos/milo-rnd-library/dc3/` — DC3 assets (worlds, characters, UI)
  - `world/glitterati/gen/glitterati.milo_xbox` — venue with meshes/lights
  - `world/dclive/gen/dclive.milo_xbox` — outdoor venue
  - `char/main/gen/main.milo_xbox` — main character

## Ghidra MCP Integration

The `analyze-function` tool uses Ghidra MCP for decompilation and cross-reference analysis.

Ghidra MCP runs on `http://127.0.0.1:8000/mcp` (not `/mcp/v1`). Session ID headers are automatically handled by the MCPClient class. May fail due to sandbox restrictions.

## Decomp Docs

- [docs/decomp/TECHNICAL_NOTES.md](docs/decomp/TECHNICAL_NOTES.md) - Compiler quirks, patterns
- [docs/decomp/RB3_REFERENCE.md](docs/decomp/RB3_REFERENCE.md) - Rock Band 3 decomp reference (shared engine)
- [docs/decomp/SUBAGENT_STRATEGY.md](docs/decomp/SUBAGENT_STRATEGY.md) - Parallel agent strategy for batch decomp work
