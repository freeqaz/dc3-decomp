# Docs Index

Top-level sitemap for the DC3 decomp documentation.

> ### ⚠ Do not move, rename, or delete anything under `docs/decomp/patterns/`
>
> `objdiff-cli` identifies this project by probing for the marker file
> **`docs/decomp/patterns/PERMUTER_ROI_ANALYSIS.md`**. If that file moves, the
> probe fails and objdiff **silently stops emitting every DC3 documentation
> link** in its analysis output — no error, no warning, just missing links in
> every diff report. Two anchors in `fixable-declarations.md` are contractual
> for the same reason.
>
> Verify after any change that touches `docs/`:
>
> ```bash
> python3 ../objdiff/scripts/check_doc_links.py --dc3 . --rb3 ../rb3
> ```
>
> It must report **30/30 ok** for `[dc3]`.

## Where we are

| Doc | Description |
|-----|-------------|
| [STATE_OF_THE_DECOMP.md](STATE_OF_THE_DECOMP.md) | **Start here.** The two headline metrics with their denominators, how to regenerate every number, the 2026-08 `name_check` ruler change, how much to trust AT_LIMIT, and the open frontier by shape |
| [PROGRESS_METRICS.md](PROGRESS_METRICS.md) | Generated headline numbers with build provenance (report time, objdiff version, relocation mode). Regenerate with `scripts/progress_metrics.py --markdown`; never hand-edit |
| [decomp/FRONTIER.md](decomp/FRONTIER.md) | The 2026-08-20 frontier re-derivation — the 2,687 remaining functions banded with denominators, the three disagreeing denominators, which scanners are still lying and how, and a ranked lane list |
| [decomp/REMAINING_WORK.md](decomp/REMAINING_WORK.md) | How to *find* work — canonical queries, which DB columns lie, triage routing, and the metric-invisible work class. Ships queries, never worklists |

## Tools & Workflows

| Doc | Description |
|-----|-------------|
| [tools/INDEX.md](tools/INDEX.md) | Agent tool selection guide — which tool to use when |
| [tools/BUILD_SYSTEM.md](tools/BUILD_SYSTEM.md) | The split pipeline — dtk, the `symbols.txt` depfile dependency (settled: already wired), the fixed-point invariant and the 2026-08-04 jump-table bug, toolchain propagation |
| [tools/REFERENCE.md](tools/REFERENCE.md) | Scripts, commands, symbol lookup, linking tools, progress measurement + its staleness gate, decomp.db trust caveats |
| [tools/WORKFLOW.md](tools/WORKFLOW.md) | Workflow narratives, common patterns, diff_inspect reference |

Tool-specific deep docs live in subdirectories:

| Subdir | Description |
|--------|-------------|
| [tools/objdiff/](tools/objdiff/) | objdiff CLI reference docs (usage, options, learnings, agent workflow) |
| [tools/orchestrator/](tools/orchestrator/) | Orchestrator incremental builds |
| [tools/GHIDRA.md](tools/GHIDRA.md) | Ghidra setup, service management, type seeding pipeline, CLI tools |

## Decomp Knowledge

| Doc | Description |
|-----|-------------|
| [decomp/TECHNICAL_NOTES.md](decomp/TECHNICAL_NOTES.md) | Compiler quirks, codegen patterns, Xbox 360 specifics — including why a header edit is a TU-wide inlining event |
| [decomp/OBJECT_MATCHING.md](decomp/OBJECT_MATCHING.md) | What "matching" means at the object level — COMDATs, sections, symbols; the mechanics behind the percentage |
| [decomp/MSVC_X360_REGALLOC.md](decomp/MSVC_X360_REGALLOC.md) | MSVC's PPC register-allocation model — the machinery behind most "regswap" verdicts |
| [decomp/patterns/INDEX.md](decomp/patterns/INDEX.md) | Fixable/unfixable codegen patterns catalog — start at its **Corrections** section, which supersedes older rows. **Do not move this directory** (see banner) |
| [decomp/RB3_REFERENCE.md](decomp/RB3_REFERENCE.md) | Rock Band 3 decomp reference (shared Milo engine) — directory compatibility matrix; header progress numbers carry a dated correction |
| [decomp/UPSTREAM_PORT_WORKFLOW.md](decomp/UPSTREAM_PORT_WORKFLOW.md) | Porting from a related decomp tree (RB3, og-dc3-decomp) when their function is at 100 % and ours isn't |
| [decomp/XBOX360_FLOATING_POINT_CODEGEN.md](decomp/XBOX360_FLOATING_POINT_CODEGEN.md) | Floating-point code generation details |

### Compiler pragmas

| Doc | Description |
|-----|-------------|
| [decomp/PRAGMA_INDEX.md](decomp/PRAGMA_INDEX.md) | Entry point for the Xbox 360 compiler pragma documentation |
| [decomp/PRAGMA_CODEGEN_SUMMARY.md](decomp/PRAGMA_CODEGEN_SUMMARY.md) | Which pragmas actually change codegen, and how |
| [decomp/PRAGMA_MATCHING_CHECKLIST.md](decomp/PRAGMA_MATCHING_CHECKLIST.md) | Checklist for pragma-driven matching attempts |
| [decomp/XBOX360_PRAGMA_REFERENCE.md](decomp/XBOX360_PRAGMA_REFERENCE.md) | Full reference of the Xbox 360 compiler's pragmas |

## Reference

| Doc | Description |
|-----|-------------|
| [reference/STYLEGUIDE.md](reference/STYLEGUIDE.md) | Code style conventions |
| [reference/MACROS.md](reference/MACROS.md) | Project macros (MILO_ASSERT, OBJ_MEM_OVERLOAD, etc.) |
| [reference/DATABASE_SCHEMA.md](reference/DATABASE_SCHEMA.md) | decomp.db SQLite schema — **incl. the columns you cannot trust for triage** (`verdict`, `current_percent`) and the reloc-blind pattern pass repaired 2026-08-19 |
| [reference/FREE60_XEX_FORMAT.md](reference/FREE60_XEX_FORMAT.md) | Xbox 360 XEX executable format |

## Code Transformation

| Doc | Description |
|-----|-------------|
| [permuter/INDEX.md](permuter/INDEX.md) | C++ source permuter — **extracted to the standalone `decomp-synth` tool** ([`../../decomp-synth`](../../decomp-synth)); this page is now a pointer + how to run it in DC3 |
| [permuter/ghidra-stress-test/](permuter/ghidra-stress-test/) | DC3-specific Ghidra-guided permuter stress-test findings (project-side) |

## Dynamic Analysis

| Doc | Description |
|-----|-------------|
| [tools/UNICORN_FUNCTION_RUNNER.md](tools/UNICORN_FUNCTION_RUNNER.md) | Unicorn runner usage and design overview — the project's real-bug oracle |
| [analysis/](analysis/) | Machine-generated lane artifacts (JSON/JSONL + writeups) from specific investigations — the 2026-08-12 `name_check` residency split, anon-namespace hash lane, and data-COMDAT-fold measurements |

## Plans

| Doc | Description |
|-----|-------------|
| [plans/permuter/PERFORMANCE_ROADMAP.md](plans/permuter/PERFORMANCE_ROADMAP.md) | Permuter speed & power roadmap — throughput, search quality, synthesis revival (living tracker) |
| [plans/custom-graphics-engine/PLAN.md](plans/custom-graphics-engine/PLAN.md) | Native port master plan — rendering/audio/input/motion |
| [plans/dc3-native/STATUS.md](plans/dc3-native/STATUS.md) | Native port status — boot flow, error handling, env vars |
| [plans/dc3-native/PLATFORM_HACKS_ANALYSIS.md](plans/dc3-native/PLATFORM_HACKS_ANALYSIS.md) | HX_NATIVE hacks audit — 298 guards categorized, DTA handler root cause, screen flow reference |
| [plans/dc3-native/TEST_GAP_ANALYSIS.md](plans/dc3-native/TEST_GAP_ANALYSIS.md) | Test gaps — high-value missing tests for native port correctness |

## Projects

| Doc | Description |
|-----|-------------|
| [vmx128/README.md](vmx128/README.md) | VMX128 SIMD Ghidra support (Xbox 360 AltiVec extensions) |

## Debugging

| Doc | Description |
|-----|-------------|
| [debugging/native.md](debugging/native.md) | **Start here** — native port debugging, ASan, headless testing, scripted input, ObjRef rings |
| [debugging/web.md](debugging/web.md) | Web build debugging — WASM/Emscripten testing, CDP debugger |
| [tools/HTTP_DEBUG_SERVER.md](tools/HTTP_DEBUG_SERVER.md) | HTTP debug server — live DTA eval, screenshots, telemetry, settings, object introspection |
| [native/CONSOLE_DTA_EVAL.md](native/CONSOLE_DTA_EVAL.md) | Evaluating DTA on a **real Xbox 360** and getting the answer back on the PC — surface survey, wire protocols, `tools/console/dc3_eval.py` |
| [native/CONSOLE_HW_FINDINGS.md](native/CONSOLE_HW_FINDINGS.md) | **Hardware ground truth** for the console channel: why the FTP transport can never work (a title launch unloads the dashboard's FTP server), the XBDM file transport that replaces it, drive-name aliasing behind the `game:\` trap, and `tools/console/hw_smoke.py` |

## Runtime (original binary under emulation)

| Doc | Description |
|-----|-------------|
| [runtime/XENIA_HEADLESS_STATUS.md](runtime/XENIA_HEADLESS_STATUS.md) | Running the original DC3 XEX headless in Xenia — Vulkan pipeline, multi-frame capture, guest memory patches, fake Kinect |
| [runtime/XENIA_ASYNC_COMPLETION_STALL.md](runtime/XENIA_ASYNC_COMPLETION_STALL.md) | The async-completion stall that blocked guest progress, and its resolution |
| [runtime/BOOT_ANALYSIS.md](runtime/BOOT_ANALYSIS.md) | Boot progression of the original binary under emulation |
| [runtime/SCRIPTED_INPUT_TESTING.md](runtime/SCRIPTED_INPUT_TESTING.md) | Driving the emulated title with scripted input |

## Native Port

| Doc | Description |
|-----|-------------|
| [native/TESTING.md](native/TESTING.md) | Native build testing guide — GTest fixtures, ASan, debugging |
| [native/dta/OVERLAY_ENGINE.md](native/dta/OVERLAY_ENGINE.md) | DTA overlay engine — file overlay system design |
| [native/dta/USAGE_GUIDE.md](native/dta/USAGE_GUIDE.md) | DTA overlay usage — settings toggles, locale strings |

## History

Dated records. **None of these are maintained**, and none of them is a source of
current numbers — they describe what was true on their own date, frequently on a
measurement ruler that has since changed.

| Where | Count | What it is |
|-------|-------|------------|
| [sessions/](sessions/) | 365 files, 2025-01 → 2026-08 | Work-session logs, named by date and topic (e.g. `2026-02-11-x360-linking-pipeline.md`). History by construction |
| [investigations/](investigations/) | 5 dated lanes | Self-contained investigation lanes with their own findings — [`2026-06-10-roadmap-to-100/`](investigations/2026-06-10-roadmap-to-100/), [`2026-07-01-native-camera-fixes/`](investigations/2026-07-01-native-camera-fixes/), [`2026-08-02-viewer-rb3-asset-render/`](investigations/2026-08-02-viewer-rb3-asset-render/README.md) (milo-viewer dropped geometry the asset asks for — LOD resolution from `Character::mLods`, material-less geometry libraries, outlier-guarded auto-framing), [`2026-08-04-bustamovepanel-poll.md`](investigations/2026-08-04-bustamovepanel-poll.md), [`2026-08-12-local-static-scope-ordinal.md`](investigations/2026-08-12-local-static-scope-ordinal.md) |
| [archive/](archive/README.md) | 42 files (1 archive) | Superseded status snapshots and planning worklists, preserved byte-for-byte with a [manifest](archive/2026-08-17-doc-audit/MANIFEST.md) recording what went stale in each and what replaced it |

## General

| Doc | Description |
|-----|-------------|
| [CONTRIBUTING.md](CONTRIBUTING.md) | Contributing guidelines |
| [FAQ.md](FAQ.md) | Frequently asked questions |
