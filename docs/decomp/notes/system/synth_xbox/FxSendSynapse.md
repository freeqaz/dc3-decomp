# Lane notes: `src/system/synth_xbox/FxSendSynapse.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?SyncEffectParams@FxSendSynapse360@@UBAXPAUIXAudio2SubmixVoice@@@Z`

`FxSendSynapse360::SyncEffectParams` -- moved verbatim from `src/system/synth_xbox/FxSendSynapse.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Above the definition (was lines 21-57):

```text
// w14-e (96.4 -> 99.5 canonical, 32 -> 7 rows): the map lists
// ??0SynapseAPOParams@DSP@@QAA@XZ as `f i` (synth_xbox:FxSendSynapse.obj),
// i.e. an inline/SELECT_ANY definition, and marking the definition below
// `inline` (docs/decomp/patterns/map-comdat-flag-gates-clobber-propagation.md)
// is enough: `this`/`voice` now live in r31/r30 across the ctor call with the
// image's 0xd0 frame, without moving the ctor to another object.  Two more
// spellings closed the rest of the body: band 0 stores coeff1/gain before
// freq (the image loads mProximityEffect ahead of mNote1Hz), and band 1's
// unison test is a full if/else (`beq` to the zeroing `fmr` over a `b`) where
// band 2's is a one-armed if (`bne` over it).  Left: 7 rows, the stfs order
// of 0x58/0x54/0x60/0x68/0xa4/0x7c/0xa8 -- moving the low/high cutoff stores
// above band 1, or to the end of the function, was inert or worse.
// The history below predates the `inline` lever.
// NOT YET 100% (96.4%, 32 rows of 89), and the residual is a *build-model*
// finding rather than a source one.  Every remaining row follows from register
// allocation: the target spends two callee-saved registers (r30 = voice,
// r31 = this) and a 0xd0 frame, while we keep `this` in the VOLATILE r8 across
// the `bl ??0SynapseAPOParams` and need only r31 and a 0xc0 frame.  That is
// only legal if the compiler knows the callee's clobber set -- MSVC's whole-TU
// register-usage propagation -- and it does, because the ctor is defined in
// this file.  Measured, one variable at a time:
//
//   ctor defined in this TU, at the bottom (as shipped here)   96.4%
//   ctor defined in this TU, between CreateFx and here          96.4%  (inert:
//       the analysis is whole-TU, not emission-order dependent)
//   ctor moved to SynapseAPO.cpp (a different TU)               98.8%
//
// So the original compiled SyncEffectParams without the ctor body in scope,
// i.e. in a separate object -- exactly how rb3-xenon splits the same engine
// code (synth_xbox/FxSendSynapse.cpp holds only the Params ctor;
// synth_xbox/FxSendSynapse360.cpp holds these methods).  Acting on that here
// needs config/373307D9/splits.txt to model two objects whose .text
// interleaves (CreateFx, ctor, SyncEffectParams by address), including a
// correct .rdata/.pdata division, so it is left for a lane that can validate
// the split.  Moving the ctor alone is a net loss: it costs the 80 bytes
// ??0SynapseAPOParams currently matches in this unit and SyncEffectParams
// still does not reach 100%.
```
