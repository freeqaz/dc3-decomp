# Audit: jeff XEX-splitter correctness of the TARGET side of every diff

## Question

The target objects every match% is measured against are produced by our fork of
`jeff` (`/home/free/code/milohax/jeff`, a Rust XEX splitter). If jeff mis-splits
the XEX — wrong boundaries, phantom/overlapping symbols, oversized symbols, bogus
synthetic vftables — every downstream match% is wrong. Four recent jeff commits
fix exactly those areas. Are the DC3 target objects in this repo (a) generated
with those fixes, (b) free of the mis-split signatures the fixes target, and (c)
would a re-split at jeff HEAD change anything?

## Method (commands run)

- `git log --stat` + `git show -s --format=%ci` on the four fix commits in
  `/home/free/code/milohax/jeff`, and on `HEAD`.
- Mapped the build wiring: `objdiff.json` target_paths -> `build/373307D9/obj/**`;
  ninja `rule split` (build.ninja:33486) runs `dtk xex split config/373307D9/config.yml build/373307D9`.
- mtime histograms of `build/373307D9/obj/**/*.obj` vs each fix-commit date
  (`find -newermt`).
- Read the split source: `src/analysis/slices.rs` (prologue/terminator),
  `src/cmd/xex.rs` (`prune_overlapping_phantom_functions`,
  `clamp_oversized_function_symbols`, `write_coff_if_changed`),
  `src/analysis/pass.rs` (`FindXboxVtables`).
- **Re-split at HEAD into `/tmp/jeff-resplit` and `/tmp/jeff-resplit2`** (outside
  the repo) and byte-compared all 2,223 objs vs the repo; captured `RUST_LOG=info`
  prune/clamp/vtable telemetry; read `proposed_splits.txt`.
- Static mis-split scan: parsed `config/373307D9/symbols.txt` (19 MB) for
  overlapping / zero-size .text function symbols; counted `fn_<addr>` synthetic
  symbols in report.json + decomp.db; verified a sample `fn_<addr>` is an EH funclet.
- Cross-checked report.json vs decomp.db match% for two functions.

## Findings

### F1 (load-bearing): The target objects are NOT stale — a re-split at jeff HEAD is byte-for-byte identical.

`dtk` is built at jeff HEAD (`/home/free/code/milohax/jeff/target/release/dtk`,
mtime 2026-06-09 18:07, contains all four fixes incl. the June-9 terminator one).
I re-ran `dtk xex split config/373307D9/config.yml /tmp/jeff-resplit` (exit 0,
"Done!") and byte-compared every obj:

```
SAME=2223  DIFF=0  ONLY_IN_REPO=2
```

All 2,223 shared objs are **byte-identical** (`cmp -s`). The two "repo-only" files
are stale leftovers from an earlier split layout, not live targets:
`build/373307D9/obj/system/utl/StreamRecorder.obj` (0 bytes — moved to
`system/gesture/StreamRecorder.obj`, identical 86,020 B in both) and
`build/373307D9/obj/system/synth_xbox/SampleInst.obj` (13,082 B — moved to
`system/synth/SampleInst.obj`, identical 23,792 B in both). config.json unit lists
match exactly: repo 2223, resplit 2223, symmetric difference empty.

This is corroborated independently by the build itself: ninja's `rule split`
(build.ninja:33486-33491) re-ran the split at HEAD on **2026-06-09 18:09**
(`build/373307D9/config.json` mtime) — `config.json` is NEWER than `dtk` — yet
**zero `.obj` files were touched on Jun 9** (`find -newermt "2026-06-09 00:00"` =
0). The mechanism is `write_coff_if_changed` (src/cmd/xex.rs:857-868): it xxh3-hashes
the new bytes and skips the write when unchanged. So the Jun-9 HEAD split produced
the byte-identical objs and left their May-27 mtimes intact.

### F2 (load-bearing): Three of the four "fixes" are NO-OPS on DC3; the fourth (vtable) was already baked in.

Fix commit dates vs newest target obj (2026-05-27 23:39):

| commit  | date       | what it fixes                         | effect on DC3 |
|---------|------------|---------------------------------------|---------------|
| f4a3eff | 2026-05-27 02:37 | suppress synthetic vftable overlapping user symbol | **already applied** (objs post-date it) |
| 1900431 | 2026-05-28 22:55 | prune phantom overlapping function symbols | **no-op** (0 overlaps in DC3) |
| a8ffeb8 | 2026-05-29 02:27 | clamp oversized function symbols       | **no-op** (0 oversized in DC3) |
| a422812 | 2026-06-09 18:08 | stricter prologue terminator check     | **no-op** (symbol-table-driven split, 0 byte change) |

Evidence for no-op status, from the HEAD re-split `RUST_LOG=info` log:
`grep -ciE "Pruning phantom|Clamping oversiz" = 0`. The prune (1900431) and clamp
(a8ffeb8) fixes are guarded on the overlap test, and DC3 has no overlaps (F3), so
they find nothing. The terminator fix (a422812) lives in `slices.rs` prologue
detection, which is the **CFA/heuristic boundary path used for initial analysis**;
DC3's production split is driven by the curated `config/373307D9/symbols.txt` (19 MB,
2026-05-26) + `splits.txt` + `.pdata`, so the heuristic does not set DC3 boundaries
— hence the June-9 split produced 0 byte changes. The prune/clamp commits' own
messages confirm their A/B numbers were measured on **rb3-xenon**, not DC3.

The f4a3eff vtable fix IS active and load-bearing for DC3: the HEAD re-split log
shows `FindXboxVtables: emitted 0 vtable candidate(s) ... dropped: short=86603
align=0 skip=0 hull=706 user=946`. All 946 heuristic vtable candidates that would
overlap user-declared symbols are suppressed; **0 synthetic `vftable_<addr>`
symbols** reach the target objs (report.json: `vftable_<addr> = 0`; decomp.db:
`symbol LIKE 'vftable_%' = 0`). Before this fix the DC3 split aborted with hundreds
of "ends within symbol" errors (per the commit message), so it gates the split
running at all.

### F3 (load-bearing): The DC3 symbol table — the authoritative split input — has ZERO mis-split signatures.

Parsing the 69,160 `.text` `type:function` entries in
`config/373307D9/symbols.txt` (running-max-end interval sweep):

```
parsed .text function symbols: 69160
OVERLAPPING function pairs: 0
zero-size function symbols:   0
largest fn symbol: ?ImportExpression@Compiler@D3DXShader@@... size 0x96B0
```

Zero overlaps and zero zero-size functions. The largest symbols (0x96B0 / 38,576 B
`ImportExpression`, 0x77F0 `Simplify`, 0x6CF4 `OptimizeLoops`) are all genuine
D3DXShader compiler functions, not split artifacts — confirmed by name; decomp.db
shows the same set sorted by size. Because well-formed symbols never overlap, the
prune+clamp fixes (which trigger only on overlap) provably have no candidates here.

### F4: The 1,536 `fn_<addr>` synthetic names are EH funclets, not split artifacts.

report.json: 1,536 `fn_<8hex>` names, 1,324 at 100%. decomp.db: 1,586 `fn_%`
rows (1,328 at 100%, 25 partial summing 1,000 B, 233 at 0% summing 17,184 B).
Sampled `fn_82336098` in the HEAD re-split disasm: it is referenced from `.pdata`
(`pdata@822AFCC8` -> `.4byte fn_82336098`) and an exception scope table
(`except_record_82336BB8`), and begins with a real prologue (`mflr r12`). These are
MSVC-generated exception-handler / `__C_specific_handler` continuation funclets —
real code in the binary, anchored by unwind metadata, but not source-authorable
from C++. This matches the documented objdiff v4.2.0 funclet-pairing (memory:
reference_objdiff_funclet_pairing). They are correctly part of the target side; the
~233 unpaired-at-0% ones (17 KB total) are a measurement-denominator artifact, not a
split defect, and are not source-fixable.

### F5: report.json and decomp.db agree with the live measurement chain.

Spot check: `?OnBeat@RhythmBattle...` = 96.63% in report.json vs 96.626% in
decomp.db; `?SyncProperty@MetaMaterial...` = 100% in both. The MetaMaterial.obj
mtime is a `2030-01-01` outlier, but `cmp -s` proves it is byte-identical to the
HEAD re-split and SyncProperty scores 100% — the bad mtime is cosmetic, not
corruption. `proposed_splits.txt` from the HEAD re-split lists `candidates: 0` —
jeff at HEAD suggests no new boundaries for DC3.

### How jeff finds boundaries (for completeness)

Two paths. (1) **Heuristic/CFA** (`src/analysis/slices.rs`): `is_end_of_seq`
(branch or r0/r1 def/use, line 69) bounds prologue/epilogue search; the new
`is_function_terminator` (line 78: blr / `b` non-link / rfi / `addi r1,r1` /
`lwz r1,d(rN!=r1)`) is the stricter gate for splitting two prologue-like sequences.
This path is bounded against runaway VM exploration (caps at slices.rs:54-61). (2)
**Authoritative**: for DC3 the boundaries come from the curated `symbols.txt` +
`splits.txt` + `.pdata` (`obj.known_functions`), and `write_coff` carves one COFF
section per function symbol. DC3 runs the authoritative path; the heuristic path is
relevant mainly to *initial* symbol-table generation and to games without a clean
symbol table (RB3-xenon). **Residual failure modes after the fixes** therefore
apply to the heuristic path, not to DC3's current production split: the prune fix's
own doc (xex.rs) admits a residual false-positive class (a real function that is
simultaneously mis-sized into a neighbor AND unreferenced-in-module AND absent from
.pdata — tail-call thunks / vtable-only entries / XEX exports could be wrongly
pruned); every prune is info-logged for traceability. For DC3 this never fires (0
prunes in the HEAD log).

## Implications for the roadmap

1. **The TARGET side of the diff is correct and current. This audit clears jeff as
   a source of measurement error for DC3.** No re-baseline is needed for
   correctness. The 43.8% matched_code / 29,236 matched_functions figures are not
   inflated or deflated by a stale or buggy split.
2. The four recent jeff fixes were RB3-xenon-motivated; they are confirmed no-ops
   (or already-baked-in) for DC3. Do not spend effort re-splitting DC3 expecting
   match% movement — there is none to gain.
3. The `fn_<addr>` funclets inflate the *denominator* (48,413 report functions
   includes 1,536 funclets, ~233 stuck at 0%). "Done = 100%" must be defined
   against source-authorable functions and exclude unpaired EH funclets, or the
   ceiling is artificially < 100%. This is a denominator-definition task, not a
   split fix.
4. Minor hygiene: two stale objs (`obj/system/utl/StreamRecorder.obj` 0 B,
   `obj/system/synth_xbox/SampleInst.obj`) and one `2030` mtime
   (`obj/system/rndobj/MetaMaterial.obj`) are harmless but indicate
   `build/373307D9/obj` is never pruned across split-layout changes.

## Tooling gaps found

- **No re-baseline script.** Re-splitting DC3 today means manually
  `cargo build --release` in jeff + `ninja` (the split rule); there is no single
  documented "re-split -> re-diff -> re-sync" command. `scripts/build/rebuild_jeff_link.sh`
  rebuilds for *linking* (it does `rm -rf build/373307D9/obj` then `ninja link`),
  not for measurement re-baseline, and it deletes objs without pruning stale ones.
- **`build/373307D9/obj` is not pruned on re-split** (write_coff_if_changed never
  deletes), so old-layout objs (StreamRecorder/SampleInst) and bad mtimes
  (MetaMaterial 2030) persist silently. A re-split that *renames* a unit leaves a
  ghost obj that nothing now consumes but which could mislead an mtime audit.
- **No automated mis-split assertion in CI.** The overlap=0 / zero-size=0 /
  vftable_=0 / proposed_splits candidates=0 checks I ran by hand are exactly the
  invariants that would catch a future jeff regression. None of them run in the
  build. A `verify_split_integrity` check (parse symbols.txt + read split log) would
  make jeff bumps safe and turn the prune/clamp info-logs into a tripwire.
- **mtime is an unreliable staleness signal** because of write_coff_if_changed:
  an obj's mtime can lag its true generation by weeks while still being current
  (the May-27 objs are HEAD-current). Any "is the target stale?" check must compare
  *content hash* against a fresh re-split, not mtimes. This audit had to re-split to
  prove currency; the project has no standing way to assert it.
