# Lane notes: `src/system/synth_xbox/SynapseAPO.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `??0SynapseAPO@DSP@@QAA@XZ`

`DSP::SynapseAPO::SynapseAPO` -- moved verbatim from `src/system/synth_xbox/SynapseAPO.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Above the definition (was lines 8-23):

```text
// The two-row residual in this constructor is a scheduling floor: the target
// interleaves the implicit `this` computation for the mCurrentParams
// constructor between the two implicit vtable-pointer stores, and we emit both
// stores first. Neither row has a source-level expression -- both the second
// vtable store (IXAPOParameters at +0x20) and the `addi r3, this, 0x16c` are
// compiler-generated. Controls: adding an explicit `mCurrentParams()` to the
// initialiser list is byte-inert, and rb3-xenon's independently-written
// SynapseAPO.cpp -- a different class spelling with padding members instead of
// real ones -- lands on the identical 93.3333% with the same two rows.
// NOTE (w7-av): 93.33 is a compiler-emission floor.  The only residual is the
// position of the SECOND vptr store, `stw r10, 0x20(r30)` (the IXAPOParameters
// subobject): the image emits it AFTER `addi r3, r30, 0x16c`, the `this` for
// the mCurrentParams ctor, and we emit it before.  Both stores are synthesised
// by MSVC after the base ctor returns, so no initialiser-list spelling reaches
// them -- naming mCurrentParams() explicitly in the list scores 93.33 with a
// byte-identical row set.
```
