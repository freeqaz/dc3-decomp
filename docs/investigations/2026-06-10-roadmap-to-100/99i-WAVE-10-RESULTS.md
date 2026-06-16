# Wave 10 Results — vtable-dispatch bug hunt + finish og audio + measurement hygiene

**Date:** 2026-06-16 · **Landed through main `51dca955`** (+ DB runbook) ·
**Orchestration:** 1 Workflow (lanes A/B/C, 8 agents, ~1.09M tokens) + orchestrator-inline harvest.
**Authorship:** Fable (research+orchestrator) over Opus implementer/verifier subagents.

Wave 10 ran three narrow lanes (per the wave-8/9 "no broad sweeps" stance): a **new
bug class** (raw-plane wrong-target virtual dispatch), the **structural-port finish**
(EQ/Wah audio), and **measurement-trust hardening** (the wave-9 SQL-LIKE bug class).

## Headline

| Metric | Pre-wave (wave 9) | Post-wave 10 |
|---|---|---|
| done-with-certs fns | 99.04% | **99.06%** |
| done-with-certs bytes | 96.73% | **96.74%** |
| open residual | 264 fns / 193.9 KB | **260 fns / 192.9 KB** |

Suite milo-tests **418/418 0 FAIL**, check_native_compiles PASS (from main, via lane-C
path fix), web release green, PPC ninja green, reconcile_db **0 drift**.

## Lane A — vtable-dispatch correctness sweep (VERIFIED PASS, `02ba5067`)

**A new, reusable bug-finding tool + 3 real behavioral fixes.** The wave-9 Splash
Suspend/Resume bug (wrong virtual dispatched, invisible at the normalized metric) was
not a one-off — it's a *class*. Lane A built `scripts/analysis/vtable_dispatch_scan.py`:
it bounds candidates by the raw-vs-normalized gap (483 fns at norm≥98 ∧ raw<norm; a
1457-fn broadening run confirmed nothing real hid lower), re-diffs each in RAW mode, and
pattern-matches vtable-slot loads (`lwz rN, 0xNN(r12)` consumed by `mtctr/bctrl`) whose
pure-immediate offset differs — then resolves each offset to a virtual via
`dump_vtable.py` to separate real wrong-dispatch from benign reloc/ICF noise. It
complements `reloc_strict_classify.py` (catches wrong NAME, misses same-vtable-wrong-slot)
and `audit_normalized_masking.py` (drops the immediate when a reloc is present).

Fixes (each asm + dump_vtable verified in the raw plane):
- **Splash::Draw 97.6 → 100% raw** — the IDENTICAL crossed-dispatch wave-9 fixed in
  Suspend/Resume (`eae4ad39`) but **missed in Draw**: source called `TheNgRnd.Resume()`
  where the target dispatches vtable+0x138 (`DxRnd::Suspend`). The splash thread owns the
  device during loading, so the dispatch is intentionally crossed.
- **NgSpotlightDrawer::RenderScene 95.6 → 99.0% raw** — `r30` vcall slot 0x5c → 0x60.
- **NgFur::Shell** — two `TheShaderMgr` vcall sites, slot 0x40 → 0x24 (the dispatch
  diffs cleared; residual is separate FMA-fusion noise, unrelated).
- **CharEyes::Poll** — left as needs-review: a genuine wrong-slot, but the receiver
  vtable resolves ambiguously through an `ObjRefConcrete<CharLookAt>` sub-object; not
  safe to fix blind.

Zero native risk (Splash threaded path dead under HX_NATIVE). Touched zero headers.

## Lane B — finish og audio EQ/Wah (VERIFIED PASS, `e65719b4`)

Closed the wave-9 EQEffect::Params blocker by resolving the layout **from target asm**:
the DC3 truth is `sizeof 0x38` — field-0 is a `bool` (`unk0`), not `u32 mActiveBands`,
and there is no 14th float (`mBand5Q` dropped). Both guard-rails held at 100% raw
(`SetParameters@EQEffect`, `@WahEffect`). Landed: FxSendEQ360 Sync 2→100 + CreateFx
11→100, FxSendWah360 Sync 1→100, `StandardEffect<EQEffect>`/`<WahEffect>` +
`CSampleXAPOBase<WahEffect>` families to 100. Reverb deferred (3336B).

**Cross-repo coordination note (important):** a concurrent agent in the sibling
`milo-native-engine` repo had already advanced its `FxSendNative.cpp` to `p.unk0`
(uncommitted WIP). This meant the native build was **broken from main** — the engine
referenced `unk0` while DC3's header still had `mActiveBands` (this is why lanes A and C
reported their native gate blocked). Lane B's EQEffect change **re-aligns** the build;
DC3's own `native/src/platform/FxSendNative.cpp` was updated in lockstep. After landing,
`check_native_compiles` passes from main and the suite is 418/418. The decomposition test
(transient-revert on the integrated tree) was run on both shared headers — PASS.

## Lane C — measurement-trust hygiene (`51dca955`)

The wave-9 lesson, turned into durable guardrails:
- **SQL-LIKE-over-symbols audit found 2 MORE live instances** of the `??_%` wildcard
  bug: `unicorn/refresh_frontier.py` hid 108 authorable `??` partials from the unicorn
  frontier; `batch_check.py`'s `skip_boilerplate` over-excluded 607 symbols (146 real
  authorable work). Both fixed with `ESCAPE`; 11 latent sites hardened; ledger in
  `data/sql_like_audit.md`; 7-test regression suite (`test_like_prefix_escape.py`). (The
  harvest also escaped one more harmless-but-inconsistent `merged_%` site the verifier
  flagged.)
- **Two-path denominator self-check** in `certify_floor.py`: computes the authorable
  total two independent ways and exits nonzero on disagreement — so a future filter bug
  fails LOUDLY (verified: trips on the injected wave-9 bug, passes clean).
- **`check_native_compiles.sh` worktree-path fix**: resolves the engine path absolutely;
  works from main and worktrees without a manual `-DMILO_ENGINE_PATH` override.
- **MemTracker STL cluster** (the newly-visible ≥70% `??` frontier) diagnosed as floors
  — 4 certs applied from `data/floor_cert_backlog_w10.json` (`__adjust_heap`/
  `__linear_insert`/`__partial_sort<MemDiffEntry>` STL-header templates +
  `CSampleXAPOBase<SynapseAPO>` ATG SDK template; all regalloc/coalescing floors).

Lane C's verifier returned `pass=false` for two reasons, both non-blocking: (1) the
external engine-WIP native blocker (byte-identical from main — resolved once lane B
landed); (2) the one cosmetic `merged_%` nit (fixed at harvest). The substantive work is
sound and verified.

## DB runbook (single-writer, applied)

sync (15 improvements / 0 regressions / 13 promotions) → reconcile --fix (12 stale
is_stub, 2 stale certs cleared) → auto cert census → w10 MemTracker backlog (4 certs) →
final reconcile **0 drift**. New headline above. The 2 aggregate-sweep "regressions" are
the same untouched-math report-pairing artifacts as wave 9 (the 12-byte Normalize alias
entry — real Normalize is 99.9% — and EH funclet `fn_82EDB280`).

## Open follow-ups (wave 11 candidates, all narrow)

1. **Run `vtable_dispatch_scan.py` periodically / on the full binary** — lane A bounded
   to norm≥98; a deeper sweep (and resolving CharEyes::Poll's ambiguous receiver) may
   find more wrong-dispatch bugs. This is now a standing tool, not a one-shot.
2. **FxSendReverb360** Sync/CreateFx (3336B) — the last og-audio body, deferred.
3. **ShaderProgram::Cache 85.7** residual (carried from wave 9).
4. **Bump MILO_ENGINE_PIN** once the concurrent engine `FxSendNative.cpp` unk0 WIP is
   committed (currently the pin `8fb669d` lags engine HEAD `15ce606` + uncommitted WIP;
   DC3 main now matches the unk0 direction).
5. ~26 stale `wt/arch*`/`dc3-sweep*` worktrees to remove.
