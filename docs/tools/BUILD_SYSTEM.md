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

## The post-compile patchers, and why they must re-run behind a recompile

Five scripts rewrite `build/373307D9/src/**/*.obj` **in place** after every
object compiles and before anything reads them — anonymous-namespace hashes,
`??__E` STATIC→EXTERNAL promotion, `$S`→`??_B` guard naming, bool-parameter
back-reference mangling, `??__F` atexit scope counters. They are not cosmetic:
the symbol names, storage classes and relocations they produce are what this
project matches the retail image against. **An object that skipped them is raw
compiler output, and anything measured from it is wrong.**

They are wired as a serialized chain of stamps in `configure.py`, and each
stamp takes **`all_source` as a real implicit input, never `order_only`.**

> Order-only constrains ORDER but never marks an edge dirty.

That distinction was the whole bug (fixed 2026-08-09; rb3-xenon had already hit
and fixed the identical one in `bd6cefa1`, 2026-08-02). With `order_only`, two
routes produced a tree of unpatched objects and announced nothing:

1. **Any incremental build.** One edited `.cpp` recompiled one `.obj`, which
   arrived unpatched while every stamp was still "current".
2. **Any *second* `ninja`, on any tree.** An in-place rewrite makes the object
   newer than the mtime ninja stored beside its `deps = msvc` record, so the
   next build says `stored deps info out of date` and recompiles it — producing
   a fresh *unpatched* object that no stamp re-ran behind.

Route 2 made "patches wiped" the **steady state**. Measured at dc3 `21f7f331`:
a clean full build → `ninja` reverted **277 of 980** objects, no patcher
re-fired, and the third `ninja` reported `[1/1] PROGRESS` — the tree looked
settled and clean while carrying 224 unpromoted `??__E` symbols across 125
objects. The ADDR_IDENTITY witness generated from it produced **53** pairings
instead of **60** (`cdea2f9e…` vs `ccde57e8…`).

### The three coupled parts (any one alone is not a fix)

1. `configure.py` — `all_source` is an **implicit** input of all five stamps, so
   ninja propagates the objects' mtimes through the phony and any object newer
   than a stamp re-triggers that patcher.
2. `scripts/obj_patch_io.py` — every patcher writes through
   `write_patched_obj()`, which **restores the object's mtime**. This is what
   makes part 1 converge: a patcher that bumped the mtime would make ninja
   recompile the object it just patched, which now re-triggers the patcher, for
   ever. (Measured: with part 1 only, every `ninja` recompiled the same 277
   objects and re-ran all five patchers.)
3. `tools/project.py` — `report.json` / `report_raw.json` **depend on**
   `post-compile` rather than being ordered after it, so a build in which only
   the patchers ran cannot leave a stale report.

### How a degraded tree announces itself

`scripts/verify_objs_patched.py` runs as the last post-compile edge:

- `--check` re-runs all five patchers in dry-run and **fails the build** unless
  the object tree is a fixed point of them. If it ever fires during a full
  build, the dependency graph above has regressed.
- `--emit` then writes `build/373307D9/patch_state.json` — the sha256 of every
  object at the moment the tree was verified patched.

The manifest exists because no build-time check can see the remaining hole: a
**targeted** `ninja build/373307D9/src/Foo.obj`, or a tool compiling one TU,
does not pull in the post-compile edges at all. Consumers of the tree — the
decomp-synth ADDR_IDENTITY witness among them — check it with

```bash
python3 scripts/verify_objs_patched.py --verify-manifest
```

which needs no toolchain and no COFF parsing. It is content-keyed on purpose:
because part 2 preserves mtimes, **the patch state of this tree is not visible
in any timestamp**, and no age-based check can substitute.

## Expected steady state

A fully built, unmodified tree should behave like this:

```bash
$ ninja -n
[1/1] PROGRESS          # the one always-run edge; anything else means work is pending
$ git status --short    # empty — the build writes nothing into config/ or src/
```

If `ninja -n` shows a SPLIT with no config edit behind it, the fixed-point
property has regressed — see above before assuming a ninja bug. If it shows a
batch of `MSVC` edges on a tree nobody edited, read the `-d explain` output: a
wall of `stored deps info out of date` means something rewrote objects in place
without restoring their mtimes (see the post-compile patchers above).

**`[1/1] PROGRESS` is necessary and not sufficient.** Between 2026-02 and
2026-08-09 the degraded, patch-wiped tree reported exactly that. The additional
question a settled tree must answer is `scripts/verify_objs_patched.py
--verify-manifest`.

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
