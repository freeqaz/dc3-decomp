# Build System — the split pipeline and its toolchain

How `ninja` turns `orig/373307D9/default.xex` plus the hand-maintained config into
per-unit target objects, and which parts of that pipeline are *not* wired the way
people assume. For scripts and day-to-day commands see [REFERENCE.md](REFERENCE.md);
for agent tool selection see [INDEX.md](INDEX.md).

## The graph, in one picture

```
orig/373307D9/default.xex ─┐
config/373307D9/splits.txt ─┼─► SPLIT (dtk xex split) ─► build/373307D9/config.json
config/373307D9/symbols.txt ┘        ▲                          │
config/373307D9/config.yml ──────────┘ (declared input)         ▼
../jeff/target/release/dtk ──────────┘ (implicit input)   configure.py ─► build.ninja
                                                                │
src/**.cpp ─► MSVC ─► build/373307D9/src/*.obj ─────────────────┴─► REPORT ─► report.json
../objdiff/target/release/objdiff-cli ──────────────────────────────┘ (implicit input)
```

The declared input of the SPLIT edge is `config/373307D9/config.yml`. The XEX,
`splits.txt` and `symbols.txt` reach ninja through a **depfile**, not through the
edge's input list — which is why the wiring looks absent if you only read
`build.ninja`'s `build` line.

## `symbols.txt` is already a tracked dependency — this is settled

This was carried as an open task for a while on the belief that editing
`config/373307D9/symbols.txt` did not re-trigger the split. **It does.** Nothing
needs to be added to `tools/project.py`. Re-verified end to end on 2026-08-04:

| Claim | Where |
|-------|-------|
| The split rule declares `depfile = $out_dir/dep` and `deps = gcc` | `tools/project.py` (`n.rule(name="split", …)`) |
| dtk writes that depfile at the end of `xex split` | `../jeff/src/cmd/xex.rs` (`let dep_path = args.out_dir.join("dep")`) |
| The depfile names all three real inputs | `ninja -t deps build/373307D9/config.json` → `#deps 3 … (VALID)`, listing `orig/373307D9/default.xex`, `config/373307D9/splits.txt`, `config/373307D9/symbols.txt` |
| Editing `symbols.txt` schedules a re-split | `touch config/373307D9/symbols.txt && ninja -n build/373307D9/config.json` → `[1/2] SPLIT config/373307D9/config.yml` |
| No cold-start hole | `rm .ninja_deps` with nothing else changed → SPLIT is scheduled. With `deps = gcc`, a *missing* deps entry alone marks the edge dirty, so the first build in a fresh clone or worktree cannot skip the split |

Both halves of the wiring are original: `depfile=` dates to this repo's initial
commit (`f8ce7c2a`, 2025-07-25) and dtk's dep emission predates it
(`../jeff` `f3c9133`, 2025-07-29). The dependency was never missing.

What *was* broken is covered next: the edge existed but self-refired, because dtk
rewrote `symbols.txt` every time it read it. Fixing that is what made the
pre-existing dependency usable, and it is the reason this stopped looking like a
missing-dependency problem.

**Do not re-open this.** If a re-split seems not to happen, check `ninja -t deps`
first — a `(stale)` or missing entry is a ninja-state problem, not a wiring one.

## The fixed-point invariant

> **`dtk xex split` must not modify its own inputs.** Its output has to be a fixed
> point of its input.

This is the property to test whenever the splitter changes. A splitter that
rewrites `symbols.txt` turns the depfile edge into a self-refiring loop: every
build dirties the input it just consumed, so SPLIT (and the `configure.py`
regeneration hanging off it) runs on every invocation forever.

Verify it like this — trigger a split via a *different* input so `symbols.txt`'s
own mtime is not the trigger, then confirm nothing about it moved:

```bash
touch -d '2020-01-01 00:00:00 UTC' config/373307D9/symbols.txt
sha256sum config/373307D9/symbols.txt          # record
touch config/373307D9/splits.txt               # trigger via the other depfile input
ninja build/373307D9/config.json
ls -la --time-style=full-iso config/373307D9/symbols.txt   # mtime unchanged
sha256sum config/373307D9/symbols.txt                      # sha unchanged
```

Current state (2026-08-04): mtime stays at the checkout value, size stays
`19080213`, sha256 stays `88f66ded…888a740e`, and `git status` is clean — dtk
never opens the file for writing. Repeated full `ninja` runs settle to exactly
one always-run edge (`[1/1] PROGRESS`).

## The jump-table split bug (fixed 2026-08-04)

**Fix:** `../jeff`, branch `fix/jumptable-internal-branch-targets`, commit
`dde965c` (135/135 tests pass). Regenerated config landed here as `cb5e1bb4`
(−52/+23 lines, purely restorative).

**Symptom.** `dtk xex split` could not complete in this repo:

```
Failed: Overlapping functions 4:0x82B728F8-4:0x82B72974 -> 4:0x82B7291C
```

**Precise defect statement — get this right, it was gotten wrong once.** This was
*not* "dtk rewrites the file and then fails in the same invocation". The first
split **exits 0 and rewrites `symbols.txt`**; only the *second* split fails, on
the file the first one wrote. The correct characterisation is:

> **dtk's output was not a fixed point of its own input.**

**Root cause.** `synthesize_reloc_targeted_leaf_functions_once()` in
`src/cmd/xex.rs` treated every relocation target in a code section that sat
immediately after a hard flow terminator as a leaf-function entry. That heuristic
cannot distinguish a call from a function's own internal control flow. A switch
dispatch materializes its case-table base with `lis rX, T@ha / addi rX, rX, T@l`
and jumps through `bctr`; both the reloc sources and `T` live in the *same*
function, and `T` lands right after the `bctr`. So the parent got clamped and its
jump table was retyped Object → Function and resized to swallow the parent's own
case bodies. The jump-table pass then re-derived the true parent extent and the
two collided.

**The fix.** Require an **external** reference before carving a leaf function out
of a parent, and exclude `jumptable_*` / `except_data_*` / `except_record_*` as
reloc *targets*. Plus self-healing of data-named `type:function` entries in
`src/util/config.rs` and `src/analysis/cfa.rs`.

Correct entries in `config/373307D9/symbols.txt` after the fix:

```
?LowerForearm@ST@@YAHKPAK@Z = .text:0x82B728F8; // type:function size:0xB4 scope:global
jumptable_82B7291C = .text:0x82B7291C;          // type:object   size:0x58 scope:global
```

### Two judgments worth keeping

**dtk's overlap check is correct and must not be relaxed.** It was reporting a
real problem in its input. Every time this class of failure appears, the check is
the messenger. Fix the thing that produced the overlapping symbols.

**Hand-reverting generated config cannot fix a generator bug.** An earlier
diagnosis blamed config commit `05f3e705` and reverted it. That was wrong:
`05f3e705` was dtk's own output committed by hand, and dtk **regenerates the bad
entries on every split**, so no revert of the config could ever hold. The fix had
to be, and was, upstream in `../jeff`. If `config/373307D9/*` diverges from what
dtk produces, the config is not the bug — treat it as a symptom and go to the
generator.

## Toolchain propagation: nothing rebuilds `dtk` or `objdiff-cli` for you

This repo points at **prebuilt binaries** in sibling checkouts:

| Tool | Path used by `build.ninja` | Wired as |
|------|---------------------------|----------|
| dtk | `../jeff/target/release/dtk` | implicit input of the `split` edge |
| objdiff-cli | `../objdiff/target/release/objdiff-cli` | implicit input of the `report` / `report_raw` / `baseline` edges |

Ninja tracks the **binary's mtime**, not the Rust sources. There is no `cargo`
rule in the generated `build.ninja` (`grep -c "rule cargo" build.ninja` → `0`),
so a source change in `../jeff` or `../objdiff` has **zero effect** on this repo
until somebody runs cargo by hand:

```bash
cd ../jeff    && cargo build --release            # then ninja re-splits
cd ../objdiff && cargo build --release -p objdiff-cli   # then ninja re-reports
```

This gap has real cost: landed objdiff work sat unused for a day because no one
rebuilt the binary. If a fix you know is upstream is not showing up, check the
binary's mtime against the upstream commit date before debugging anything else.

The `cargo` rule only appears if `--dtk` / `--objdiff` are pointed at a *source*
directory. If you ever do that, note that the rule intentionally has **no
depfile** — cargo's depfile uses an absolute target path that ninja rejects,
which makes the tool perpetually dirty and re-fires CARGO (and potentially a
re-SPLIT cascade) on every build. In that configuration, `.rs` edits need
`touch ../jeff/Cargo.toml && ninja` or `touch ../objdiff/Cargo.toml && ninja`.
Same fix as rb3-xenon (2026-06-30).

See also [objdiff.md § Which binary am I running?](objdiff.md#which-binary-am-i-running)
— the same `objdiff-cli` binary is shared by three repos, so one rebuild
propagates to all of them at once.

## Expected steady state

A fully built, unmodified tree should behave like this:

```bash
$ ninja -n
[1/1] PROGRESS          # the one always-run edge; anything else means work is pending
$ git status --short    # empty — the build writes nothing into config/ or src/
```

If `ninja -n` shows a SPLIT with no config edit behind it, the fixed-point
property has regressed — see above before assuming a ninja bug.

Other build-hygiene tools:

- `scripts/clean_stale_objects.sh --dry-run` — finds `.obj` files older than the
  PCH. `--all` force-touches every `.cpp` for a full rebuild.
- Header dependencies are tracked automatically via `/showIncludes` plus wibo
  path rewriting; touching a header rebuilds only the affected objects. No manual
  `touch` needed.
- `scripts/setup_worktree.sh <path> <branch>` — creates a worktree with a working
  build system (ninja configured, tools/compilers/target objects symlinked, ninja
  state primed).

## Related

- [REFERENCE.md § Progress Measurement](REFERENCE.md#progress-measurement) — the staleness gate that catches a report built against a different config or dtk
- [objdiff.md](objdiff.md) — the diff tool, its binary distribution, and its doc-link contract
- [../plans/BUILD_ROADMAP.md](../plans/BUILD_ROADMAP.md) — linking the decompiled XEX (a separate goal from splitting the original)
