# 14 — Strict-Reloc Re-Certification Results (Lane C, Wave 1)

**Date:** 2026-06-10. **Lane:** C — strict-reloc recertification (roadmap 0.5 / 0.6).
**Branch:** `wave1/c-strict-reloc` (dc3) + `wave1/strict-reloc` (objdiff fork v4.2.3).
**Source doc:** `02-measurement-objdiff-fork.md` (F2/F3 — the only *uncounted* measurement risk).

## TL;DR — the number

`report.json` feeds decomp.db under the lenient reloc mode `functionRelocDiffs=None`, which
forgives **all** relocation targets (a `bl wrong_function` scores 100% as long as the reloc
*type* matches). This lane builds the missing strict view and counts the genuine false-100%s.

- **Candidate set** (100% under `None` but <100% under the new **`NameOnly`** mode, which
  matches a reloc iff the target symbol **name + section** match and **ignores the addend**):
  **10,470 functions**, 2,748,496 bytes.
- After per-reloc classification of every candidate (re-diffed under `NameOnly`):

  | class | functions | authorable | bytes |
  |---|---:|---:|---:|
  | **genuine_wrong_target** (different symbol NAME) | **2,408** | **2,405** | 742,728 |
  | template_instantiation_variant (same method, different type params — ICF-equivalent) | 2,992 | 2,992 | 862,700 |
  | target_split_label (jeff `fn_`/`merged_`/`lbl_`/`$T`/EH-record artifacts) | 3,096 | 3,096 | 1,004,664 |
  | benign_string_path (`??_C@` __FILE__ build-path text) | 1,321 | 1,321 | 152,008 |
  | benign_build_artifact (anon-ns hash, local-scope counters, cv-tail, jumptable, save/rest helpers) | 645 | 645 | 47,912 |
  | non_reloc_codegen (the strict drop is not reloc-related) | 8 | 8 | 828 |

- **The conservative upper bound on genuine wrong-call-target false-100%s is 2,405
  authorable functions.** But that bucket is itself *dominated by ICF folds* (see below): a
  deeper pass attributes ~1,227 to ICF-merged identical-body functions (e.g.
  `MemOrPoolFree`↔`MemOrPoolFreeSTL`, destructor folds, the folded `On*` handler), ~250 to
  genuinely-different log/assert string literals, ~174 to STL-helper folds, leaving a
  **residual ~754-function "suspect tail."** Inspecting the 20 largest of that tail
  (below) shows it is *still* overwhelmingly ICF-merged symbols and target-split
  mis-attribution, not wrong-behavior source bugs.

**Recommendation: NO "matched" function needs to be reopened on strict-reloc grounds today.**
The genuine *behavioral* wrong-callee population is at most low dozens (and likely fewer),
not thousands. Doc 02's framing — "bounded but uncounted, the genuine subset much smaller
than the 11,052 raw drops" — is **confirmed**: the raw 10,470/11,052 collapses to ≈754
suspect after ICF/template/artifact removal, and the residue is mostly cosmetic. Keep the
lenient `None` mode as the steering metric; run this recert on a cadence (below).

## What was built

### 1. objdiff fork — `FunctionRelocDiffs::NameOnly` (branch `wave1/strict-reloc`, commit `72b553f`)

The fork had no mode that checks the reloc target *name* while *ignoring the addend*.
`name_address` couples name **and** addend, so it over-penalizes every callee that merely
lives at a different build address (≈9,263 of the 11,052 raw drops are 99-100% pure addend
noise per doc 02 F2). `None`/`data_value` forgive the target entirely. `NameOnly` is the
exact recert mode that was missing.

- `objdiff-core/config-schema.json`: new `name_only` choice item under `functionRelocDiffs`
  (the enum + serde + `FromStr` are generated from the schema by `config_gen.rs`).
- `objdiff-core/src/diff/code.rs` `reloc_eq`: when `NameOnly`, short-circuit to
  `section_name_eq(...) && names_match` — addend ignored, `relax_reloc_diffs` stays `false`
  (a missing reloc on one side is still penalized, unlike `None`). External-symbol (no
  section) case matches on name alone.
- `objdiff-cli/src/views/function_diff.rs`: added `NameOnly` to the interactive `x`-key
  reloc-mode cycle (the only non-exhaustive match; all CLI `diff.rs` arms have `_` catch-alls).
- 3 unit tests pin the truth table directly on `reloc_eq` (fixture-independent):
  `test_name_only_forgives_addend` (same name, different addend → MATCH; NameAddress → no
  match), `test_name_only_catches_wrong_callee` (different name → no match; None → match),
  `test_name_only_exact_match` (identical → match under every mode). All pass.

`-c functionRelocDiffs=name_only` is accepted by `objdiff-cli report generate` and
`objdiff-cli diff`.

### 2. `report_strict.json` (side channel; report.json untouched)

Generated from the **same** primed worktree build that produced `report.json`/`report_raw.json`
(same 2,224 units / 48,417 functions), using the NameOnly objdiff-cli:

```
objdiff-cli report generate --project . -c functionRelocDiffs=name_only \
    -o build/373307D9/report_strict.json
```

Measures: `matched_code` 5,000,868 (None) → **2,252,372** (NameOnly); `matched_functions`
29,278 → **29,272** (the *function-count* metric barely moves; the byte drop is concentrated
in large call-heavy functions, exactly as doc 02 predicted). The `report` rule that feeds
the DB was **not** changed — strict is a separate artifact.

### 3. Classifier — `scripts/analysis/reloc_strict_classify.py`

Read-only (never writes decomp.db). Loads `report.json` + `report_strict.json`, finds the
candidate set, re-diffs each candidate under `NameOnly` with `--include-instructions`, walks
every per-instruction `symbol` argument diff, and classifies each target-name mismatch.
Emits `build/373307D9/reloc_strict_classify.json` (full authorable genuine list +
top-units + a template-variant sample) and a stdout summary table. ~90 s on 30 workers.

Benign-pattern detectors (each collapses a known non-behavioral name difference):
anon-namespace hash `?A0x<hex>@`, local-scope/static-local disambiguators, data cv-qualifier
tail, `__FILE__` build-path string normalization (`?1`↔`?2`), template array-size decay
(`$$BY0..@`), save/restore runtime helpers, switch jump-tables (`jumptable_<hex>`↔`$T<n>`),
EH unwind records, ICF-fold stubs (`merged_`/`OnlyReturns`/`Returns*`), and target-split
address labels (`fn_`/`lbl_`/…). Same-method-token-with-different-type-params is split out
as `template_instantiation_variant`.

## Top authorable units by genuine_wrong_target

```
33  default/system/world/LightPreset      21  default/system/world/CameraShot
26  default/system/obj/Dir                20  default/lazer/meta_ham/CampaignPerformer
25  default/lazer/meta_ham/AccomplishmentManager  20  default/system/char/Character
25  default/lazer/meta_ham/CampaignProgress 20  default/system/rndobj/Console
21  default/system/rndobj/Mesh            20  default/system/synth_xbox/Synth
```

## The residual "suspect tail" is still ICF, not bugs

The 20 largest residual-tail functions (after removing the known ICF/string/STL buckets) —
each row is the **first** mismatched reloc, `t=`target / `b=`base symbol:

- `MoveMgr::Handle`, `PartyModeMgr::ctor`, curl `multi_runsingle` → target reloc
  `?SetEngine@CTrigramStore@NUISPEECH@@` (one ICF-merged function the target-split
  mis-named across many callers).
- `SaveLoadManager::GetDialogMsg`, `HamProfile::Handle`, `UIList::SyncProperty` → target
  `?Parent@Node@?$ObjPtrVec@VRndTransformable@@…` (another single ICF-merged accessor).
- aes `rijndael_ecb_*` (`rijndael_desc` vs `it_tab`/`ft_tab`), curl `Curl_setopt`
  (`curlx_ultous` vs `curlx_sltous`), `ByteGrinder::Init` (`?op53@` vs `?op27@`) → vendor
  data tables / ops that ICF-folded to one symbol.
- ftp/rtsp `*_statemach`/`*_do`: target reloc is a `??_C@` error string, base is a switch
  table `$T<n>` — a target-split labeling artifact.
- `HamDirector::ctor`: `??_8RndCam@@7BRndTransformable@@` vs
  `??_8HamDirector@@7BRndDrawable@@` — a **vbase-table for a different base class**; this is
  the one genuinely layout-relevant row in the top-20 and is worth a manual look, but it is
  a single function and is already 99.91% strict.

In other words: there is **no** large population of functions calling the wrong runtime
target. The lenient metric is not hiding a behavioral problem at scale.

## Recommendation / cadence

1. **Do not reopen any "matched" function** on strict-reloc grounds in this wave. The
   genuine behavioral wrong-callee set is at most low-dozens and every example found is
   ≥98.6% strict (ICF-folded identical bodies or target-split mis-attribution).
2. **Keep `None` as the DB/steering metric.** Adopt the project "done" definition from doc
   02: 100% = right opcode + right registers (up to regalloc) + right immediates/offsets/
   vtable slots + right reloc *type* — but not a byte-verified reloc *target*.
3. **Recert cadence:** regenerate `report_strict.json` + run this classifier whenever a
   batch of new functions reaches 100% (e.g. weekly, or in the nightly that runs
   reconcile.py from Lane A). Watch the `genuine_wrong_target` count for any **new**
   non-ICF entry whose two symbols are *different real methods at different addresses* — that
   is the only signal that would mean a real false-100% slipped in. The current run is the
   baseline: 2,405 authorable, dominated by ICF.
4. **Future tightening (optional, not blocking):** the classifier could promote
   ICF-fold detection from name-pattern heuristics to a definitive check by comparing the
   two symbols' resolved addresses (same address ⇒ ICF ⇒ benign). That requires per-reloc
   address output objdiff does not currently expose in the diff JSON; it would shrink the
   "genuine" bucket from 2,405 toward the true behavioral residue (≈ dozens).

## Reproduce

```bash
# from the primed dc3 worktree (objects + report.json built):
WTOBJ=/home/free/code/milohax/wt-objdiff-strict/target/release/objdiff-cli
$WTOBJ report generate --project . -c functionRelocDiffs=name_only \
    -o build/373307D9/report_strict.json
python3 scripts/analysis/reloc_strict_classify.py --jobs 30 \
    --objdiff $WTOBJ --out build/373307D9/reloc_strict_classify.json
# objdiff fork tests:
cargo test -p objdiff-core --lib --features ppc,std name_only   # 3/3 pass
```

## Artifacts

- objdiff fork: `wave1/strict-reloc` @ `72b553f` (NameOnly mode + 3 unit tests).
- `build/373307D9/report_strict.json` (NameOnly report, side channel).
- `build/373307D9/reloc_strict_classify.json` (full classification + authorable genuine list).
- `scripts/analysis/reloc_strict_classify.py` (the classifier).
