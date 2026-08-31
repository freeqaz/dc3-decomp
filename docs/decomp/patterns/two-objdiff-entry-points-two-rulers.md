# `objdiff-cli diff` and `objdiff-cli report generate` were two different rulers

**Status:** cause found, fixed in project config 2026-08-31, guarded by
`scripts/verify_ruler_agreement.py`. Read this before comparing any
per-function number against `report.json`.

## The symptom

`?SetVHBlurWeights@@YAX_NHH@Z` (`default/system/rndobj/DOFProc_NG`, dc3-decomp)
read, on the *same tree*, with the *same* `bin/objdiff-cli` (4.2.8,
`358c715835cc`, xxh3 `9b2bb6f1f3a21062`), with the report cache **cold**:

| path | score |
|---|---|
| `report.json` → `match_percent_normalized` | **100.0** |
| `objdiff-cli diff` / `run_objdiff` → `canonical_match_percent` | **99.675674**, 1 charged row |

The charged row was:

```
[128] replace: `subi r28, r11, 0x8`  vs  `subi r28, r11, 0x8`
```

Two textually identical instructions. The JSON shows why: the **target** side
carries an extra `{"type": "Symbol", "value": "lbl_830A8A58"}` operand that the
base side does not.

## The mechanism

The two CLI entry points carry **different hardcoded base configs**, and neither
is the schema default:

| | `report generate`<br>`objdiff-cli/src/cmd/report.rs:581` | `diff`<br>`objdiff-cli/src/cmd/diff.rs:1070`<br>(and `--batch` at `diff.rs:1807`) |
|---|---|---|
| `functionRelocDiffs` | `none` | `data_value` |
| `combineDataSections` | **true** | false (schema default) |
| `combineTextSections` | **true** | false (schema default) |
| `ppc.calculatePoolRelocations` | **false** | **true** (schema default) |

Both then layer `objdiff.json`'s `options` block on top. This repo's block set
only `functionRelocDiffs`, so it fixed the ruler both paths *argue* about and
left them disagreeing on the other three.

`ppc.calculatePoolRelocations` is the one that bites. It **synthesizes**
`R_PPC_NONE` relocations for pooled data loads —
`objdiff-core/src/arch/ppc/mod.rs:819 make_fake_pool_reloc`, reached from
`objdiff-core/src/obj/read.rs:708` — and the config schema calls them *"fake
relocations"* in as many words ("Display pooled data references in functions as
fake relocations"). They are reconstructed per object by walking that object's
control flow and looking the computed address up **in that object's own symbol
table**. A dtk-carved *target* obj (a whole linked data section, anonymous
`lbl_*` labels) and our MSVC per-TU COMDAT *base* obj do not reconstruct the
same set.

`reloc_eq` then charges the asymmetry:

```rust
// objdiff-core/src/diff/code.rs:1330-1338
(None, Some(_)) => return relax_reloc_diffs || name_check,   // base-only: forgiven
(None, None)    => return true,
_               => return false,                              // TARGET-only: CHARGED
```

A relocation present on one side and absent on the other is charged under
**every** `functionRelocDiffs` mode except `none` — `name_check` included. So a
synthesized display annotation that only one side reconstructs costs a real
point. objdiff's own source comment at `arch/ppc/mod.rs:845-856` documents this
exact failure class and a previous fix for one instance of it; this is the
residual.

This is **upstream objdiff behaviour, not a milohax fork bug**: the three extra
report-side values arrive in `0c9e552 "Combine sections when generating report"`
(Luke Street, 2025-05-07), which touched `report.rs` only.

## Scope: 155 functions, 120,728 bytes, all in one direction

Whole-binary sweep, dc3-decomp, worktree at `edcb3279f`, full `ninja` completed
before reading `report.json`, `diff --batch` over all 48,339 distinct function
names in the report, one objdiff-cli:

* comparable rows (a real percent on both sides): **31,955**
  (16,318 more are unpaired — batch returns `null`, report returns `0.0`; both
  agree, no disagreement there)
* **disagreements attributable to the config split: 155 (120,728 bytes)**
* direction: `report` higher on **155**, `diff` higher on **0**
* magnitude: +0.0076 pp to **+15.00 pp**, mean +0.97 pp
* of the 155, **49 (28,240 B) read exactly 100.0 in `report.json` and <100
  through `diff`** — the class where a lane refuses a promotion for a reason
  that does not exist
* 247 mismatch **rows** are phantom; on 47 functions the *entire* mismatch set
  was phantom

Attribution over the 155: `ppc.calculatePoolRelocations` alone explains
**155/155**; `combineDataSections` additionally suffices for 17;
`combineTextSections` explains none.

A separate, non-defect class: 143 rows where `diff --batch` scores 100.0 and the
report scores 0.0. Every one carries `base_unit` — the batch path's disclosed
cross-unit COMDAT fallback, which finds the body in another unit's base obj.
The report scores per-unit only. Those two numbers answer different questions.

## Which path is right

The **report** path. Three independent reasons:

1. **The annotation is derived, not observed.** It is not a relocation in either
   object file; it is reconstructed from (pool base relocation, displacement),
   both of which the diff already charges directly. It adds no information that
   is not already metered — but it can *disagree* spuriously, because the lookup
   that names it depends on a symbol table the two sides do not share.
2. **The charged row is two identical instructions.** Nothing about the base
   object is wrong at instruction 128 of `SetVHBlurWeights`.
3. **Turning it off hides nothing** (negative control, below).

## Controls

Both were run before the conclusion was written.

**Control 1 — the report path is not blanket-forgiving.** Sabotage
`weights[7] = 0.125f` → `0.25f` in `src/system/rndobj/DOFProc_NG.cpp`, full
`ninja`:

| | clean | sabotaged |
|---|---|---|
| `report.json` | 100.0 | **98.54054** |
| `diff -c ppc.calculatePoolRelocations=false` | 100.0 | **98.54054** |
| `diff` (its own defaults) | 99.675674 | 98.21622 |

The report path drops on a one-constant change, and `diff` under the report's
config reproduces the canonical number exactly. The residual 0.324 pp gap is
constant — one phantom row.

**Control 2 — the opposite result, checked.** A genuinely wrong *pooled datum*
is still charged with the annotation off. The same sabotage, diffed with
`ppc.calculatePoolRelocations=false`, charges seven rows loudly:

```
| 5 | lis  r10, __real@3e000000@h        | lis  r9,  __real@3e800000@h        | diff_arg |
| 8 | lfs  f0,  __real@3e000000@l(r10)   | lfs  f13, __real@3e800000@l(r9)    | diff_arg |
| 9 | lwz  r10, lbl_830A8AD8@l(r9)       | lis  r9,  ?$S3@?4??SetVHBlur…@h    | replace  |
```

A pooled reference that is really wrong moves the **real** relocations both
objects carry. The fake ones were never load-bearing.

## The fix

One project-config change, in `tools/project.py`'s `options` block — **both**
CLI entry points layer it, so pinning it there fixes the per-function path
without touching a tool, without an MCP server restart, and without a rebuild of
the shared `bin/objdiff-cli` symlink:

```python
"options": {
    "functionRelocDiffs": "name_check",
    "combineDataSections": True,
    "combineTextSections": True,
    "ppc.calculatePoolRelocations": False,
},
```

**It changes no recorded number.** Same worktree, full `ninja` before and after:

| | matched_functions | matched_code | matched_code_percent | fuzzy_match_percent |
|---|---|---|---|---|
| before | 29,902 | 5,056,848 | 44.45998 | 54.37279 |
| after | 29,902 | 5,056,848 | 44.45998 | 54.37279 |

And the whole-binary re-sweep after the change: **0** config-caused
disagreements (31,812 agree; the only 143 remaining all carry `base_unit`).

## Consequences

* **The headline is not overstated by this.** The report path was never the
  lower of the two, on any of 31,955 comparable functions. Nothing that
  `report.json` counts as matched was being forgiven here.
* **Every lane that refused a promotion on this basis refused wrongly.** The
  49 functions above are 100.0 canonical. `run_objdiff` — the tool lanes
  actually use — reproduced the defect verbatim, printing
  `[128] replace: subi r28, r11, 0x8 vs subi r28, r11, 0x8` under a
  `LikelyFixable (Medium) — inspect the few mismatched instructions directly`
  verdict. That is an instruction to go chase a bug that is not there.
* **AT_LIMIT reasoning on the other 106 was taken over an inflated row set.**
  74 of the 155 carry an `AT_LIMIT` verdict in `decomp.db`, several with
  `verdict_reason = "auto: all mismatches unfixable"` — a verdict computed from
  a row set that included phantom rows.

## Prior art, and why it did not close this

`scripts/analysis/ruler.py` already documented the base-config split and already
set `ppc.calculatePoolRelocations=false`, with a measurement from a lane "EB-4"
("up to 14.75 pp on 118 of 1,639 named sub-100 rows"). But `ruler.py` is
imported only by `scripts/analysis/*`. The orchestrator MCP tools — `run_objdiff`,
`run_diff_inspect`, `run_analyze_function`, `run_symbol_sweep`, which is what
every lane calls — never used it. A correct doc plus a correct helper that the
actual measurement path does not import is zero enforcement.

## The guard

```
python3 scripts/verify_ruler_agreement.py --check      # ~0.2 s config-pin assertion
python3 scripts/verify_ruler_agreement.py --selftest   # ~10 s, with negative control
```

`--check` reads the effective config out of `report.json`'s own
`provenance.diff_config` (authoritative by construction: it is not a description
of the config, it *is* the config the score was taken under) and asserts each
divergent key is pinned in `objdiff.json`.

`--selftest` re-runs the end-to-end comparison with
`-c ppc.calculatePoolRelocations=true`, restoring `diff`'s own default, and
**requires** that to produce disagreements. If it does not, it exits **5**
("vacuous"), names the rotted witness set, and tells you to re-derive with
`--all` — it does not report success from a probe that examined nothing.
Measured on the fixed tree: 411 witness functions, 411 agree as configured,
**19 disagree** under the control flip.

## What the defect actually cost, re-derived after the fix (2026-08-31)

The scope section above was measured on `edcb3279f`. Re-derived independently on
`ea3f5a226` after a full `ninja`, same objdiff-cli 4.2.8
(`358c715835cc`, xxh3 `9b2bb6f1f3a21062`), with
`scripts/analysis/pool_reloc_population.py`:

* universe **48,323** uniquely-named report functions (16 dropped: defined in >1 unit)
* examined **31,811** · **agree_now 31,811 · disagree_now 0** — the pin holds
* unpaired 16,318 · unresolved 50 · cross-unit `base_unit` fallback 144
* **population 158 functions / 126,320 bytes**, report higher on 158, `diff` on 0
* **52 (34,868 B)** read exactly 100.0 in `report.json`

158/126,320 **supersedes** 155/120,728. The delta is source movement between the
two trees, not drift in the defect — do not read it as the defect growing.

### Row counts, which is what a verdict was actually decided on

`scripts/analysis/pool_reloc_rows.py` counts charged rows under both configs.
A percentage does not adjudicate a verdict; a row count does, because
`sync_objdiff.py`'s `"auto: all mismatches unfixable"` walked rows.

* **774 phantom rows removed**, 7,229 real rows remain (was 8,003)
* entire mismatch set was phantom on **50** functions (34,044 B)

### Two claims about this population that are FALSE

Both were plausible and both were checked before being written down:

1. **"AT_LIMIT rows are sitting on functions the canonical ruler scores 100.0."**
   No. All 52 rows reading 100.0 are already `verdict='COMPLETE'`. Not one
   AT_LIMIT row in the population reads 100.0. There is no such class to repair.
2. **"The functions whose entire mismatch set was phantom are promotion
   candidates."** They are the *same already-COMPLETE rows*. Zero AT_LIMIT rows
   sit at zero real mismatches.

### How the 74 AT_LIMIT rows actually divide

| bucket | n | meaning |
|---|--:|---|
| phantom rows = 0 | 55 | the row **set** was identical under both configs; only the score moved, because the synthesized annotation lands in `arg_diff_score` without creating a row. The verdict never rested on this defect. |
| evidence set shrank | 19 | the verdict cited rows that partly did not exist. Worst two: `?ParseNode@@YA_NXZ` 24 of 38 phantom (63%), `yylex` 33 of 54 (61%). |
| zero real mismatches | 0 | — |

That 55/19 split is the useful correction: the defect was real and worth fixing,
but it invalidated a *minority* of the verdicts it touched, and the majority
needed no action. Re-adjudicated with
`scripts/analysis/pool_reloc_readjudicate.py --apply`.

### Two rows read 100.0 in report.json *with* real charged rows

`?ReadEmbeddedFile@@YAPAVDataArray@@PBD_N@Z` (8 rows) and
`?NewFile@@YAPAVFile@@PBDH@Z` (17). That is the canonical ruler forgiving
register permutation, not this defect. They are **complete modulo register
permutation** — never write "byte-identical" for a row you have not checked at
`raw`.
