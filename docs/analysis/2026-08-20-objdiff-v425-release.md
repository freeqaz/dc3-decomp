# objdiff v4.2.4 / v4.2.5: the canonical score stops forgiving wrong callees

Released 2026-08-20 from `github.com:freeqaz/objdiff`, tags `v4.2.4` and `v4.2.5`.
`bin/objdiff-cli` is a **symlink shared by `dc3-decomp`, `rb3` and `rb3-xenon`**, so
one rebuild moved all three at once. Verify with `bin/objdiff-cli --version`; the
string carries the git hash, which is a better check than an mtime.

## What changed

**v4.2.4 — two instrument defects.**

1. *Scoring.* `match_percent_normalized = diff_score − arg_diff_score`. A relocation
   operand is not an immediate, so its penalty was charged to **both** terms and
   cancelled to exactly zero. Calling an entirely different symbol cost nothing
   under every reloc mode, `name_check` included. Under `name_check` only, a
   relocation-name disagreement that survives `reloc_eq`'s full exemption machinery
   now stays in `diff_score`.

   The fold's own comment justified it with "reloc diffs are dominated by benign
   noise" — an audit run under the *other* modes. `name_check` exists precisely to
   strip that noise first, so the premise did not transfer. `name_check`'s
   **detection was always correct**; only the scoring discarded it.

2. *Reporting.* `objdiff-cli diff` computed all three percent fields from
   `symbol_diff.match_percent` and never read `match_percent_normalized` at all —
   a field named `normalized_match_percent` carried **fuzzy**, understated by up to
   7.6pp. Added `canonical_match_percent`; markdown now renders `canonical`.
   Self-concealing, because the documented remedy for a suspect number was to
   re-measure with the same broken ruler.

**v4.2.5 — the carve-outs only fired on MSVC.** Found by auditing the siblings:
CodeWarrior spells its register-save helpers `_savegpr_14` (one underscore, no
`lr`) and the matcher required two, so **188 sites of pure register-allocation
noise** were charged on RB3. RB3-Xenon's splitter spells an unrecovered vtable
`vftable_<hex>`, missing from the placeholder list — 34 sites / 16 functions.

## Three carve-outs, each measured rather than assumed

Register save/restore helpers (which helper is called *is* register allocation, and
normalization exists to forgive that); `fn_<addr>` EH funclets, which objdiff pairs
**by byte signature**, so charging names there measures the pairing heuristic rather
than our source; and function-local-static scope ordinals (`?BD@` vs `?BH@`, a
per-TU counter).

Deliberately **not** exempted: `__FILE__` literals (a bare-filename vs build-path
difference is real spelling divergence) and symbol **kind** mismatches, which stay
folded because one side being a reloc and the other a constant measures the
splitter's attribution coverage.

## Impact, per repo

| repo | toolchain | effect |
|---|---|---|
| dc3-decomp | MSVC/Xenon | 328 functions drop, **0 rise**; 54 leave the matched set (19,288 B); headline −0.1117pp. Of the 54, **52 have a real charged site and 0 are charged on noise alone**. v4.2.5 changes nothing here — neither new spelling occurs in an MSVC tree. |
| rb3 | CodeWarrior | `matched_functions` 32,019 → 31,932 of 41,254 (−0.211pp); 526 drop, 0 rise, 87 leave the matched set. >95% of charged rows are CodeWarrior literal/ordinal naming artifacts. |
| rb3-xenon | MSVC/Xenon | `matched_functions` 44,510 → 42,172 of 69,219 (−3.378pp); 2,751 drop, 0 rise, 2,338 leave the matched set. |

DC3's canonical headline after the swap: **29,496 / 32,202 authorable = 91.60%**.

### Read rb3-xenon's −3.38pp carefully

All 1,782 adjudicable target names there come from `scripts/target_symbol_map.json`,
which its own `_comment` describes as BinDiff output at confidence ≥ 0.95, and **239
sit on addresses in that file's own `_bijection_arbitrary` list**. The number is
`name_check` scoring a *reconstructed naming layer*, not 2,338 wrong callees. The
`ObjPtrList<CharCollide>::Link` lead dies on reading `band.exe`, where the address
in question begins `lfs/lfs/fmuls`.

## Two misnomers documented, not renamed

`measures.fuzzy_match_percent` in `report.json` accumulates
`match_percent_normalized` — the **canonical** score under a field named fuzzy. No
aggregate holds true size-weighted fuzzy. Both sibling audits independently read a
"violated fuzzy control" off it when every per-function value was byte-identical.

`matched_code` is credited on fuzzy while `matched_functions` is credited on
canonical, so a canonical-only change moves the function count and leaves
`matched_code` **exactly flat**. A flat `matched_code` is not evidence that nothing
happened. Both are proto fields with stable tags and three consumers, so v4.2.5
documents them in `report.proto` rather than renaming them.

## The rule this work kept re-proving

**A name-based sweep is evidence a CLASS exists and no evidence about any individual
row.** objdiff pairs relocation rows positionally and instruction rows by alignment;
below a blind score of ~95 it compares rows it merely lined up. 193 of DC3's 328
charged rows sit there. Three per-function leads extracted this way were all
refuted, one with its **direction reversed** — acting on it would have made the row
worse. Establish the pairing independently before a row becomes a lead.

Guard: `scripts/analysis/ruler_agreement.py` checks the two measurement paths
against *each other* rather than against a constant, biases its sample toward
divergent functions (a uniform sample is dominated by matched functions where both
rulers read 100.0 and agree regardless of correctness), and exits **2, not 0**, when
nothing in the sample could have discriminated.

## Known-open

`objdiff-cli diff` and `report generate` resolve **different things** on ~17% of
divergent functions — they disagree on *fuzzy* too, which no scoring change can
cause. Reproduces on rb3 under both 4.2.3 and 4.2.5, so it is pre-existing and
orthogonal. Tracked as task #44.
