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

- `run_objdiff` — build + diff a function, returns match%, verdict, enrichment. Source of truth for decomp percentages. **Always pass `project_dir`** when working in a worktree; omitting it silently measures the main repo and your edits look like they did nothing.
  - ⚠ **`project_dir` selects a *worktree of this project*, never a different project.** These tools are pinned to DC3's `decomp.db`, `report.json`, struct DB and title ID (`373307D9`). Pointing `project_dir` at a foreign repo — `../rb3-xenon`, `../rb3` — used to answer out of DC3's database and return a plausible wrong number (2026-08-04: 93.3%, DC3's baseline, where rb3-xenon's was 92.2%; and DC3's `ObjectDir::Iterate` 100.0% where rb3-xenon's is ~60%). **Since `2d39e6cc` (2026-08-04) this raises `CrossProjectError`** naming both paths and both title IDs — it can no longer answer silently. To work in another decomp tree, use that tree's own orchestrator (`<repo>/scripts/orchestrator/mcp_server.py`). Note the DB-only tools (`query_functions`, `get_attempts`, `lookup_struct_offset`, `lookup_merged_symbol`) take no `project_dir` and are *always* about DC3.
- `run_diff_inspect` — deeper analysis: `diagnose` (root cause), `mismatches` (instruction table), `clusters`, `regswaps`, etc.
- `run_analyze_function` — combines objdiff with struct offset resolution for field-level mismatch context.
- `query_functions` — find workable functions by unit pattern and match range. A work-selection index, not a measurement: `current_percent` drifts (the ninja DB sync deliberately does not write it) and `has_prologue_mismatch` is identically 0 for every row. Re-measure with `run_objdiff` before acting — see `docs/tools/REFERENCE.md`.
- `lookup_rb3` — grep RB3 codebase for reference implementations (shared Milo engine).

## Code Style
- Be carefuly when modifying MILO_ASSERT() calls or OBJ_MEM_OVERLOAD macros. Whatever is in there should be tested carefully.
- Keep members protected/private unless confirmed public via DWARF or asserts. For external access, add getters/setters rather than making members public. Use friend classes for closely related types (e.g., Foo and FooHandle).

## Known Patterns
- **A displayed "100.0%" is NOT byte-identity** — but know *which* surface you are reading. `decomp.db.current_percent` rounds (inconsistently: it holds `99.85558` for one row and `95.38` for another), and `run_objdiff`'s headline rounds (it printed `Match: 100.0% normalized` above a table listing 3 mismatches). **`report.json.match_percent_normalized` does NOT round** — it is an exact score-weighted f32, and an immediate/displacement diff costs a point that survives normalization, so a wrong field is always visible there (`CamShot::Load` = `99.85558`; a re-injected single-field bug = `99.992905`). What it *does* forgive is register permutation, so `100.0` there means "no non-register mismatch", not "byte-identical". The standing rule "a behavioral divergence on a 100%-matched function must be a harness artifact" **may only be applied to a zero-mismatch instruction count, never to a displayed number** — two real bugs were hiding under a displayed 100.0 (`CharUpperTwist::Load` Save/Load member-order permutation, `RndFlare::Load` reading the wrong field). See [docs/decomp/patterns/rounded-100-hides-real-bugs.md](docs/decomp/patterns/rounded-100-hides-real-bugs.md).
- **`run_objdiff`'s "Offset Mismatches (resolved)" block ignores the base register** — it resolves any offset against the class struct, so a pure `(r1)` stack-slot diff is rendered as *"Source accesses 'X' but target accesses 'Y' — wrong field?"*, indistinguishable from a true positive. It manufactured exactly that story for `FxSendChorus::Load` and sent a lane hunting a nonexistent missing member in `FxSend`. Treat that block as a lead, never a finding, and read the prologue first.
- **A unicorn verdict is only as good as the harness that produced it**: `unicorn_signal_version` describes the *comparator*, not the *emulator*. Eight harness defects fixed 2026-08-18/19 changed verdicts wholesale without touching one comparator rule, so signal_version sat at 3 across the break and the old numbers overstated real bugs ~8x. Read `unicorn_harness_version` (h1..hN, changelog in `scripts/unicorn_runner/signal_version.py`); **h1/NULL means pre-fix, do not trust**. Filter with `query_functions(min_unicorn_harness_version=4)` or `scripts/unicorn/query.py --min-harness 4`. After the 2026-08-19 whole-DB re-ingest, **99.4% of DIVERGENT rows are in a known-artifact class** (`data_layout` alone is 9,815 of 12,419) and there are **zero `error`-class rows** — the oracle is now a regression detector, not a bug finder. See [docs/analysis/2026-08-19-unicorn-reingest.md](docs/analysis/2026-08-19-unicorn-reingest.md).
- **Unsigned zero comparisons**: Use `x > 0` instead of `x != 0` for unsigned types (generates `ble` vs `beq`)
- **Merged symbols**: `merged_<addr>` names indicate Identical COMDAT Folding (ICF) where the linker merged functions with identical machine code to a single address
- **Automatic header tracking**: Ninja tracks all header dependencies via `/showIncludes` + wibo path rewriting. Touching any header automatically rebuilds only the affected .obj files. No manual `touch` needed.
- **Stale object diagnosis**: `scripts/clean_stale_objects.sh --dry-run` finds .obj files older than the PCH. Use `--all` to force-touch every .cpp for a full rebuild.
- **No cargo depfile**: the `cargo` rule (only emitted if `--dtk`/`--objdiff` point at a *source dir*; this repo defaults to the prebuilt `../jeff` / `../objdiff` binaries, so no cargo edges exist) intentionally has no depfile — cargo's depfile uses an absolute target path that ninja rejects, making the tool perpetually dirty and re-firing CARGO (and potentially a re-SPLIT cascade) on every build. If you ever build the forks through ninja, `.rs` edits need `touch ../jeff/Cargo.toml && ninja` (dtk) or `touch ../objdiff/Cargo.toml && ninja` (objdiff-cli). Rebuilding a fork manually with cargo still works as before — ninja tracks the prebuilt binary's mtime. Same fix as rb3-xenon (2026-06-30).
- **Nothing rebuilds `dtk`/`objdiff-cli` for you**: no cargo edge means a source change in `../jeff` or `../objdiff` has zero effect here until you run `cargo build --release` in that repo by hand. `bin/objdiff-cli` is a symlink shared with rb3 and rb3-xenon, so one rebuild propagates to all three. If an upstream fix "isn't showing up", check the binary's mtime first.
- **`config/373307D9/symbols.txt` is already a tracked ninja dependency** — editing it re-triggers SPLIT. This was tracked as an open task on the false belief that the wiring was missing; it has been there since the initial commit. Do not re-open it. See [docs/tools/BUILD_SYSTEM.md](docs/tools/BUILD_SYSTEM.md).
- **`dtk xex split` must not modify its own inputs** — its output has to be a fixed point of its input, or the depfile edge self-refires on every build. Test this after any splitter change (`docs/tools/BUILD_SYSTEM.md`). dtk's overlap check is correct: when it fires, fix whatever produced the overlapping symbols, and never hand-revert generated config to work around a generator bug.

For build-system details (the split graph, the fixed-point rule, toolchain propagation) see `./docs/tools/BUILD_SYSTEM.md`.
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
  - **Web build**: `scripts/web/build.sh` — the one canonical web build script (mirrors rb3's). Builds the Emscripten/WASM port and dual-deploys `release/` (`-g0` stripped, cached immutable) + `debug/` (`-g2`, no-store) to `native/web/build/`. Flags: `--release`/`--debug`/`--both`/`--reconfigure`. (`scripts/build/web.sh` and `native/web/build.sh` are back-compat delegators.) Dev server: `python3 native/web/server.py --port 8420`; `http://localhost:8420/` loads release, `?debug=true` loads debug.
- `objdiff.json` - Project config for objdiff

## Shared Engine

DC3's native port consumes the shared **`../milo-native-engine`** repo (sibling at `/home/free/code/milohax/milo-native-engine`) — a game-agnostic LP64 modern-C++ runtime that owns gfx (WebGPU), audio (miniaudio/FFmpeg), input, file I/O, the host-STL shim, and POSIX impls of the `os/` interfaces. As of Phase 0, **all four native consumers — `dc3-native`, `milo-viewer`, `render-test`, `milo-tests` — link `libmilo-engine.a`** (built via `add_subdirectory(${MILO_ENGINE_PATH})`). `milo-tests` registers 441 tests: **362 execute and pass, 79 skip** (measured 2026-08-19). `ctest` reports that as "100% tests passed out of 441" and exits 0 — the skips are counted as passes, and the skipped set is the whole end-to-end tier (`DC3_GAMEPLAY_TESTS`, `DC3_DTA_FLOW_TESTS`, `DC3_AUDIO_TESTS`, `MILO_LIB`), which is where the live bugs are. The older **371/371** figure is stale; so is treating a green default run as coverage.

The engine is pulled in with a soft SHA pin: `MILO_ENGINE_PIN` in **`native/CMakeLists.txt`**; a mismatch with the engine's `git HEAD` warns but never fails. ⚠ **`scripts/bump-engine.sh` does not work against an existing build directory** — the pin is `set(... CACHE STRING ...)` *without* `FORCE`, so `CMakeCache.txt` permanently shadows the source value and the warning quotes the cached pin, not the one in the file. Verified 2026-08-19 with four different values live at once (engine HEAD `6d5dc0f`, source `77eb428b`, cache `12455b0a`, this doc's former `8282103`). Delete the cache entry or add `FORCE` before trusting a bump. Engine roadmap/status: `../milo-native-engine/README.md` and `rb3/docs/native/NATIVE_PORT_ROADMAP.md`.

## Assets
./orig-assets/ and ./orig-assets/extracted/ contain DC3 game assets.

## Ghidra MCP Integration

The `run_analyze_function` tool uses Ghidra + m2c for decompilation and cross-reference analysis.

Ghidra MCP runs on `http://127.0.0.1:8000/mcp` (not `/mcp/v1`). Session ID headers are automatically handled by the MCPClient class. May fail due to sandbox restrictions, so skip the sandbox for scripts that call to the MCP server.

## Decomp Docs

- [docs/INDEX.md](docs/INDEX.md) - Table of Contents for docs. START HERE! This helps you find the right docs.
- [docs/STATE_OF_THE_DECOMP.md](docs/STATE_OF_THE_DECOMP.md) - Where the project is. **Both headline metrics with their denominators** (MATCHED from report.json vs DONE-WITH-CERTS from decomp.db — they are different populations and conflating them is the top source of wrong numbers), the 2026-08 `name_check` ruler change, and how much to trust an AT_LIMIT certificate.
- [docs/decomp/REMAINING_WORK.md](docs/decomp/REMAINING_WORK.md) - How to find work. Queries only, never worklists — every hardcoded worklist this project wrote rotted within weeks.
- [docs/decomp/TECHNICAL_NOTES.md](docs/decomp/TECHNICAL_NOTES.md) - Compiler quirks, patterns
- [docs/decomp/RB3_REFERENCE.md](docs/decomp/RB3_REFERENCE.md) - Rock Band 3 decomp reference (shared engine)
