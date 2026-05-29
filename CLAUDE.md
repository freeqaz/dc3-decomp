# DC3 Decomp - Claude Context

Dance Central 3 decompilation for Xbox 360 (PowerPC). Goal: produce matching assembly from C++ source.

The target binary in `orig/` is a debug build pulled from an Xbox 360 dev unit (not retail). This means no link-time optimization (LTCG) - matching should be achievable for most functions.

## Key Commands

```bash
ninja                              # Build and regenerate report.json
scripts/measure_progress.sh --functions --detailed HEAD  # Progress vs commit
scripts/measure_progress.sh --current-dir /path/to/worktree HEAD  # Worktree vs commit
scripts/clean_stale_objects.sh     # Fix stale .obj files (older than PCH)
scripts/dc3-agent-test.sh          # Launch native port with HTTP debug server + fast boot + telemetry
# Re-sync decomp.db after permuter wins land on main (run from repo root, ~2-3 min):
python3 scripts/sync_match_percent.py --build --promote   # rebuilds report.json + updates current_percent/verdict
```

To interact with a running native engine instance, use `scripts/dc3-agent-test.sh` (sets `DC3_HTTP=1 DC3_FAST_BOOT=1 DC3_TEL=1`). Then `curl localhost:9090/api/health`, `/api/dta/eval`, `/api/screenshot`, etc. See `docs/tools/HTTP_DEBUG_SERVER.md`.

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

## Git Actions **important**

- Do not run `git stash` commands in the main repo. If you want to compare a change against HEAD or another commit, use a git worktree. Odds are high that concurrent agents are working in the main repo, so a `git stash` will _deeply break things_!
- Do not include `Co-Authored-By` lines in commit messages

## Git Worktrees

Use `scripts/setup_worktree.sh <path> <branch>` to create worktrees with a working build system (configures ninja, symlinks tools/compilers/target objects).

## Project Structure

- `src/` - Decompiled C++ source (mirrors original structure)
- `build/` - Build outputs, object files, `373307D9/report.json`
- `include/` - Headers
- `native/` - Native port (x86_64 Linux, WebGPU renderer)
  - Note: You must skip the sandbox for GPU access.
  - **Web build**: `scripts/build/web.sh` — builds the Emscripten/WASM port and deploys to `native/web/build/`. Dev server: `python3 native/web/server.py --port 8421`
- `objdiff.json` - Project config for objdiff

## Shared Engine

DC3's native port consumes the shared **`../milo-native-engine`** repo (sibling at `/home/free/code/milohax/milo-native-engine`) — a game-agnostic LP64 modern-C++ runtime that owns gfx (WebGPU), audio (miniaudio/FFmpeg), input, file I/O, the host-STL shim, and POSIX impls of the `os/` interfaces. As of Phase 0, **all four native consumers — `dc3-native`, `milo-viewer`, `render-test`, `milo-tests` — link `libmilo-engine.a`** (built via `add_subdirectory(${MILO_ENGINE_PATH})`). `milo-tests` passes **371/371** against the engine.

The engine is pulled in with a soft SHA pin: `MILO_ENGINE_PIN` in **`native/CMakeLists.txt`** (currently `9a58e86`); a mismatch with the engine's `git HEAD` warns but never fails. Bump it via `scripts/bump-engine.sh` when an engine change lands canonical. Engine roadmap/status: `../milo-native-engine/README.md` and `rb3/docs/native/NATIVE_PORT_ROADMAP.md`.

## Assets
./orig-assets/ and ./orig-assets/extracted/ contain DC3 game assets.

## Ghidra MCP Integration

The `run_analyze_function` tool uses Ghidra + m2c for decompilation and cross-reference analysis.

Ghidra MCP runs on `http://127.0.0.1:8000/mcp` (not `/mcp/v1`). Session ID headers are automatically handled by the MCPClient class. May fail due to sandbox restrictions, so skip the sandbox for scripts that call to the MCP server.

## Decomp Docs

- [docs/INDEX.md](docs/INDEX.md) - Table of Contents for docs. START HERE! This helps you find the right docs.
- [docs/decomp/TECHNICAL_NOTES.md](docs/decomp/TECHNICAL_NOTES.md) - Compiler quirks, patterns
- [docs/decomp/RB3_REFERENCE.md](docs/decomp/RB3_REFERENCE.md) - Rock Band 3 decomp reference (shared engine)
