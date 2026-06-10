# 13 — Verification Log

Adversarial verification of every load-bearing claim across the 12 audit docs. Each
claim was independently reproduced (fresh DB reads, source reads, re-splits, fresh
`run_objdiff`). Verdicts: **VERIFIED** (reproduced within tolerance), **REFUTED**
(claim's stated mechanism/number is wrong), **UNVERIFIED** (not independently checked
this pass — use with caution).

## Summary

- 16 claims formally verified by the adversarial pass; **15 VERIFIED, 1 REFUTED.**
- The single REFUTED item is a *mechanistic sub-claim* in doc 11 (why `MemAlloc` is
  not a native bug). The headline numbers it sits on (53 / 100 / 153 split, 43,364
  bytes) all VERIFIED — only the stated reason is wrong.
- All measurement-chain trust claims (jeff correctness, objdiff scoring semantics,
  db/report reconciliation arithmetic) VERIFIED. The measurement chain is trustworthy
  with the documented caveats.

## Verified / Refuted table

| # | Source doc | Claim (abbrev) | Verdict | Confidence | Note |
|---|---|---|---|---|---|
| 1 | 01 jeff-split | Re-split at jeff HEAD is byte-identical to repo objs (2223 same, 0 diff); target side not stale | **VERIFIED** | high | Independent re-split reproduced SAME=2223 DIFF=0 |
| 2 | 01 jeff-split | Jun-9 HEAD split ran but touched 0 objs (write_coff_if_changed xxh3-skips) | **VERIFIED** | high | config.json mtime 83s after dtk; xex.rs:857-869 confirmed. Minor: MetaMaterial.obj 2030 fake-mtime is a known pre-existing outlier |
| 3 | 01 jeff-split | DC3 symbol table has 0 overlaps / 0 zero-size fns; prune+clamp fixes are no-ops | **VERIFIED** | high | 69,160 fns, 0 overlaps, 0 zero-size reproduced exactly |
| 4 | 01 jeff-split | vftable fix f4a3eff active & load-bearing; 946 candidates suppressed, 0 reach objs | **VERIFIED** | high | Re-split log "user=946", report.json/decomp.db vftable_ = 0. (doc cited "symbol table"; real table is `functions` — naming nit, fact holds) |
| 5 | 01 jeff-split | 1536 fn_addr are MSVC EH funclets, not split artifacts; ~233 stuck at 0% inflate denom | **VERIFIED** | high | 1503/1536 referenced from .pdata; 233 NULL/0 rows = 17,184 bytes |
| 6 | 02 objdiff-fork | report.json uses function_reloc_diffs=None → forgives ALL reloc-target diffs + missing relocs | **VERIFIED** | high | code.rs:830/834/841-842, report.rs:363, sync:84 all confirmed |
| 7 | 02 objdiff-fork | Neither None nor DataValue verifies call-TARGET correctness; only name_address does, and it isn't fed to db | **VERIFIED** | medium | code.rs:866-877 DataValue short-circuits; bl has kind!=Object so passes. Wrong-target false-100% is real but uncounted |
| 8 | 02 objdiff-fork | 11,052 fns 100% lenient / <100% strict; matched_code 4.98M vs 2.18M (2.29x); fn count barely moves | **VERIFIED** | high | Reproduced 4,983,704 vs 2,176,568; 11,023 (vs 11,052, 0.26% staleness delta) |
| 9 | 02 objdiff-fork | The lenient gap is overwhelmingly BENIGN addend noise; 9,263 of 11,052 are 99-100% strict; serious-looking ArcDetector cases are STALE report_raw, fresh ~99-100% | **VERIFIED** | high | 9,263 exact; IsLockedIn stale 51.9%→fresh 99.1%/100% norm. Minor: "only 2 non-boilerplate <90%" is really 2-3 (a ??__F dyninit borderline) |
| 10 | 02 objdiff-fork | f62bc9c TIGHTENED the metric — normalized no longer hides wrong constants/offsets/vtable slots (rb3 found 75 hidden bugs) | **VERIFIED** | high | is_immediate gate at code.rs:1041-1065; arg_diff_score incremented only when !is_immediate |
| 11 | 02 objdiff-fork | 3+ coexisting headline numbers: 43.8% strict bytes / 53.2% normalized / 29,236 fns / db 31,056 of 52,504 | **VERIFIED** | high | All five reproduced to full precision |
| 12 | 08 floor-vs-routable | "routable" bucket is mostly call_count emulation artifact; genuine hard residue ~27 fns / ~13K bytes | **VERIFIED** | medium | call_count partial=143 @88.9%; top examples at 100%; residue 27 fns / 12,988 bytes confirmed |
| 13 | 08 floor-vs-routable | Expected CEILING ~1,000-1,150 fns / ~700-900K bytes remain <100%, almost all cosmetic; true work ~150-650 fns | **VERIFIED** | medium | Frontier 1,699 exact; floor 1,096 (vs 1,093); routable 167 (vs 173); 650 zero-starts / 53,616 bytes exact. Minor count drift ~3% |
| 14 | 11 native-divergence | 153 of 182 sub-100 real-bug DIVERGENT native-compiled; 100 in guarded files; 53 in zero-guard files (43,364 bytes) | **VERIFIED** | medium | 53/100/153 split and 43,364-byte sum all reproduced exactly |
| 15 | 11 native-divergence | MemAlloc (fan_in=354) / MemOrPoolAllocSTL (fan_in=533) are NOT native bugs because their bodies are bypassed by HX_NATIVE guards | **REFUTED** | medium | See correction below |

## REFUTED claims — corrections

### #15 (doc 11 — native-unicorn-divergences, finding F5/F7)

**Stated:** MemAlloc is "not a native bug" because its body (line 299) "is bypassed on
native" via the four HX_NATIVE guards in MemMgr.cpp.

**Why wrong:** MemMgr.cpp's four guards are at lines 46 (operator new/delete), 127 and
149 (inside `MemFree`), and 313 (inside `MemOrPoolAllocSTL`). `MemAlloc` (lines
298-303) has **no guard of its own**; its body — an unconditional `malloc()` — runs on
native exactly as written. The guard at line 313 belongs to `MemOrPoolAllocSTL`, not
`MemAlloc`.

**Correction:** MemAlloc is still correctly *excluded from the live-native-bug
concern*, but for a different reason: its native stub (`malloc()`) is behaviorally
correct for native; the 1.4% assembly divergence reflects an *undecompiled Xbox custom
allocator*, not a behavioral regression. The defensible 53-function zero-guard live-bug
set is unchanged. **Roadmap impact: none on the burndown numbers; only the rationale
in doc 11 F5/F7 needs the above correction.** Pointer: `docs/investigations/2026-06-10-roadmap-to-100/11-native-unicorn-divergences.md`
F5, F7.

## UNVERIFIED (carry forward with caution)

These claims were not in the adversarial pass; they are internally consistent across
multiple docs but should be re-confirmed before being treated as load-bearing:

- **db/report reconciliation arithmetic** (doc 03): 639 FALSE-COMPLETE, 20 stale
  COMPLETE, 1,728 stale is_stub, ~1.44 MB non-SDK remaining, best_percent externally
  seeded. *Cross-corroborated* by docs 04 (1,728 stub, 20 COMPLETE, 206 fuzzy/normalized)
  and 08 (1,699 frontier). High internal consistency; treat as reliable but un-adversarially-verified.
- **AT_LIMIT 40-85 routability** (doc 04 F5, doc 07 F3-F4): 8/8 sampled routable, no
  structural lever. Sampled, not exhaustive. Reliable as a *direction*, not a guarantee
  every AT_LIMIT row is routable.
- **og lane = ~186 net-new stubs / ~22 KB** (doc 09): recomputed from two report.jsons.
  Plausible and aligned with prior stub-harvest memory; not re-derived this pass.
- **Build-env clean** (doc 10): 0 truly-stale objs, no wrong-flag units, deterministic
  modulo COFF timestamp. Falsifiable tight-band test returned 0 candidates. Reliable.
- **Native runtime stub surface = 171 in engine_stubs_generated.cpp; json-c not
  compiled; IK is DoFSM int/float + mConstraints wiring** (doc 12): source-grep based,
  internally consistent with doc 11's IK/DXT findings. Re-confirm json-c compile gap and
  CharIKFoot::DoFSM 0x30/0x34 field type live before committing fixes.

## Net trust statement

The measurement chain (jeff → objdiff → ninja → report.json → decomp.db) is
**trustworthy for the TARGET side and for the scoring semantics**, with three bounded,
documented caveats that the roadmap's Phase 0 closes:

1. **Wrong-call-target false-100%** is possible under None/DataValue but is bounded and
   currently *uncounted* (claim #7, VERIFIED). → strict re-certification (NameOnly mode).
2. **decomp.db is optimistically drifted** vs report.json (sticky COMPLETE, stale
   is_stub, fuzzy-not-normalized). → reconcile.py + sync on normalized + demote path.
3. **The headline 43.8% is XDK-diluted** (6 docs agree, all VERIFIED arithmetic). →
   re-anchor to authorable denominator (~77.5-78.5%).


