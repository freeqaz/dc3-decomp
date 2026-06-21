# Wave 15 Results — const-split family + broad fake-impl harvest + structural cracks

**Date:** 2026-06-21 · **Landed through main `e815c977`** (+ DB runbook) · **Orchestration:**
1 Workflow (lanes A/B/C, all-Opus, agentRetry-resilient). Goal-loop wave.

Wave 15 landed **5 fixes + 2 certs** and **exhausted two bug classes**. Headline: done-with-certs
**99.07% fns / 96.76% bytes** (up a hair; most wins are in excluded/near-100 territory).
Gates: PPC ninja green, check_native_compiles PASS, **milo-tests 418/418**, reconcile 0 drift.

## Lane A — SampleInst360 const-split + const-mismatch class EXHAUSTED (`ea381191`)

- **SampleInst360::IsPlaying 0 → 100%** — the wave-14 mis-cert, fixed exactly as that verifier
  predicted: HX_NATIVE const-split on the base `SampleInst::IsPlaying` pure-virtual + the
  override decl + definition (non-const under `#ifndef HX_NATIVE` to match the target UAA,
  const under `#ifdef HX_NATIVE` so the engine's `SampleInstNative::IsPlaying() const override`
  binds). Native 418/418 — the const coupling is satisfied both ways, confirming it was a
  fix, not a native floor.
- **The const-mismatch CLASS is exhausted.** A custom MSVC-PowerPC-BE COFF symbol parser
  (`/tmp/coffsyms.py` — llvm-nm/objdump can't read machine 0x01F2 objs) scanned 968 non-vendor
  objs for cv-flipped emitted-vs-target symbols. Exactly 1 other hit: `DataArray::Node` — a
  not-a-bug (const overload ICF-folded into the non-const, required to compile const accessors).
- **GetProgress/ElapsedTime are NOT const-mismatches** — they're missing implementations (268B
  / 124B, not declared in SampleInst360). Target-asm decode in
  `docs/backlog/w15-sampleinst360-getprogress-elapsedtime.json` for a future decomp lane (HIGH
  risk: ICF virtual-target resolution + XAUDIO2 GetState).

## Lane B — DxShaderMgr::SetVConstant + fake-impl pool EXHAUSTED (`3f6ecd1b`)

- **DxShaderMgr::SetVConstant(float\*, uint) 2.9 → 100%** — recovered the missing body from
  target asm.
- **The fake-impl pool is exhausted.** Broad `fake_impl_scan.py` run (505 authorable
  candidates): only 7 fakes, stable across all thresholds. 6 are non-harvestable: intentional
  stubs (Synth360::PreInit, MemAlloc), STL templates (MemTracker::__median), and reference-less
  large reconstructions (DxMesh::OnSync 804B, NgEnviron::Select 1956B — need dedicated m2c).

## Lane C — structural unicorn-divergence cracks + certs (`e815c977`)

Cracked 3 wave-13-deferred emulation-proven divergences (one-edit, native-gated):
- **BeatClock::OnSyncStateChange 95.4 → 99.6%** (call_arg).
- **RndText::ReplaceMissingCharacters 91.7 → 92.5%** (return_value).
- **XfmOnCircleEdge 77.3 → 78.9%** (landed-trade, call_arg; sign-ternary→fsel + a safe reorder;
  rejected a permuter reorder that was a real use-before-def bug).
- Certs applied: Voice::IsPlaying 98% (CR-field allocation floor), XfmOnCircleEdge 81.3%
  residual (FPR coloring floor). compute_apres deferred (anon-namespace symbol + stale is_stub).

## Loop status — approaching exhaustion

This session's productive classes and their state:
| Class | Tool | Status |
|---|---|---|
| vtable dispatch (raw) | vtable_dispatch_scan.py | high-conf done; 8 ambiguous-receiver left (risky) |
| vtable construction (data) | data_symbol_scan.py | all 5,132 vtables swept |
| const-signature (UAA/UBA) | coffsyms.py | **EXHAUSTED** (968 objs, 1 not-a-bug) |
| fake/stub impl | fake_impl_scan.py | **EXHAUSTED** of cheap wins (7 fakes, 6 non-harvestable) |
| unicorn divergence | refresh_frontier.py | real bugs harvested w13–15; rest are deep floors |

**Remaining genuine levers are all HIGH-risk reconstruction:** GetProgress/ElapsedTime
missing-impl decomp, DxMesh::OnSync (804B) + NgEnviron::Select (1956B) reference-less m2c, the
8 ambiguous vtable-dispatch candidates. Wave 16 attempts these as the last lever; if it returns
mostly blocked, the levers are exhausted.
