# objdiff - Assembly Diffing Tool

objdiff is an assembly comparison tool for decompilation projects. It compares compiled output from decompiled source code against the original binary, showing instruction-level differences to help achieve matching assembly.

**Binary:** `~/code/milohax/objdiff/target/release/objdiff-cli`

**Note:** This is a custom/extended version of objdiff with additional CLI features (report querying, JSON output, function search). The standard objdiff is available at https://github.com/encounter/objdiff

## Which binary am I running?

There is only ever **one** binary, and three repos share it.

```
/home/free/code/milohax/objdiff/target/release/objdiff-cli   ← the only real file
    ▲            ▲            ▲                    ▲
    │            │            │                    └── build.ninja calls
    │            │            │                        ../objdiff/target/release/objdiff-cli
    │            │            └── rb3-xenon/bin/objdiff-cli   (symlink)
    │            └── rb3/bin/objdiff-cli                      (symlink)
    └── dc3-decomp/bin/objdiff-cli                            (symlink)
```

`bin/objdiff-cli` in this repo is a symlink; `build.ninja` spells the sibling path
out directly. Same file either way — there is no separate "project copy" to get
out of sync, and `objdiff-cli --version` cannot tell you which repo's work you are
running because there is no such thing.

Two consequences worth internalising:

- **One `cargo build --release -p objdiff-cli` propagates to all three repos at
  once.** Convenient, and also the reason an objdiff change is never scoped to the
  repo you were working in — a behaviour change lands in RB3 and rb3-xenon the
  moment you rebuild here.
- **Nothing propagates until someone runs it.** This repo has no `cargo` ninja
  edge by default, so a source change in `../objdiff` has *zero* effect until the
  binary is rebuilt by hand. This gap silently held back landed work for a day.
  Ninja does track the binary's **mtime** (it is an implicit input of the `report`
  edges), so once you rebuild, `report.json` regenerates on its own.

```bash
cd ../objdiff && cargo build --release -p objdiff-cli
```

If a fix you know is upstream is not showing up in a diff, compare the binary's
mtime against the upstream commit date *before* debugging anything else. See
[BUILD_SYSTEM.md § Toolchain propagation](BUILD_SYSTEM.md#toolchain-propagation-nothing-rebuilds-dtk-or-objdiff-cli-for-you).

## Pattern documentation links

objdiff annotates detected patterns with links into the consuming repo's pattern
docs. **The URLs are project-relative**, so every one of them is a claim about
*this* repo's filesystem — and DC3 and RB3 use different filenames for the same
material (DC3 `PERMUTER_ROI_ANALYSIS.md` / `at-limit-systemic.md`; RB3
`permuter-roi.md` / `at-limit-mwcc.md`).

For a long time one shared table served both, and the rot was near total: checked
mechanically, **29 of the 30 URLs objdiff emitted for DC3 failed against the RB3
tree**, MetroWerks-targeted `at-limit-mwcc.md` content was the *rendered* link for
four patterns inside this MSVC repo (`AnonymousNamespaceHash`,
`DeadStoreElimination`, `AddressRelocationNoise`, `BooleanNegation`), and two URLs
(`fixable-comparison.md#signed-vs-unsigned-comparison`,
`fixable-casting.md#signedness-and-width-mismatch`) existed in **neither** repo and
had presumably never resolved for anyone.

Fixed upstream in `../objdiff` (`1030000`, 2026-08-04). objdiff now detects the
consuming project by looking for **marker filenames** under its
`docs/decomp/patterns/` directory, walking up from `--project` or the cwd. Nothing
that calls objdiff-cli had to change — ninja, the orchestrator, the skills and the
MCP wrappers all still invoke it the same way. An unrecognised project degrades to
the 12-link intersection that is valid everywhere. `OBJDIFF_DOC_PROJECT=dc3|rb3|unknown`
overrides detection when it cannot work (e.g. diffing from a scratch directory).

### Re-check the links after renaming any pattern doc

```bash
../objdiff/target/release/objdiff-cli doc-links -P dc3 -f json \
  | python3 ../objdiff/scripts/check_doc_links.py
```

The checker resolves every URL objdiff would emit against every project's working
tree — confirming the file exists *and* that the anchor matches a real heading
under GitHub's slug rules. Current state (2026-08-04): **dc3 30/30, rb3 25/25,
unknown 12/12**.

### Anchor contract

objdiff renders only the **first** doc URL for a pattern, so these DC3 anchors are
load-bearing — an innocent heading rename silently breaks the tool's links:

| File | Anchor |
|------|--------|
| `docs/decomp/patterns/fixable-declarations.md` | `#pre-compute-references-before-clobbering-calls` |
| `docs/decomp/patterns/fixable-declarations.md` | `#offset-swap` |
| `docs/decomp/patterns/PERMUTER_ROI_ANALYSIS.md` | `#instruction-scheduling` |

The full contract — including the `fixable-liveness.md` anchors reachable from
REGISTER_SWAP and OFFSET_SWAP — lives with the docs it constrains, in
[../decomp/patterns/INDEX.md](../decomp/patterns/INDEX.md). Run the checker above
after any rename or restructure in that directory.

## Known defect: the REGISTER_SWAP "dominated by" line is non-deterministic

When two register-swap pairs have the **same** count, which one is reported as
dominant varies between runs of the same binary on the same input. Observed
`f0↔f10` on one run and `f13↔f9` on the next — both count 4 — plus a varying third
entry in the details list.

Mechanism (`objdiff-cli/src/cmd/analysis.rs`, `detect_register_swap`): pairs are
accumulated in a `HashMap`, then `swaps.sort_by(|a, b| b.count.cmp(&a.count))`.
Rust's `sort_by` is *stable*, so ties preserve input order — and the input order
is HashMap iteration order, which is randomised per process. The summary then
reads `swaps[0]`.

**Practical rule: never A/B a source edit on that line.** Diff reports are not
reproducible in it. Use the match percentage, or the full swap list with its
counts, as the signal instead.

## Quick Reference

```bash
# Find near-match functions (good work targets)
objdiff-cli report query build/373307D9/report.json --functions \
  --min-percent 90 --max-percent 99 --limit 10

# Check a specific function's match status
objdiff-cli report function build/373307D9/report.json "Game::Poll"

# Markdown diff with verdict (default format)
objdiff-cli diff -p . "Game::Poll" --verdict

# With context around mismatches (like grep -C)
objdiff-cli diff -p . "Game::Poll" --verdict -C 3

# Full instruction listing
objdiff-cli diff -p . "Game::Poll" --verdict --full-listing

# Interactive diff viewer (TUI)
objdiff-cli diff -p . "Game::Poll" -f tui

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
- **[objdiff/JSON_EXTENSIONS.md](objdiff/JSON_EXTENSIONS.md)** - Fork-only JSON: data-symbol diffs (`--include-data`, vtables/init data) + instruction branch graph (see the `/data-diff` skill)

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
