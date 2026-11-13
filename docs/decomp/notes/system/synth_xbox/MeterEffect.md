# Lane notes: `src/system/synth_xbox/MeterEffect.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?DoProcess@MeterEffect@@UAAXABUMeterEffectParams@@PIAMII@Z`

`MeterEffect::DoProcess` -- moved verbatim from `src/system/synth_xbox/MeterEffect.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 60-74):

```text
    // w26-f10 stop note, fuzzy 99.818 / canonical 100.0, one row left: the
    // final `add r11,r6,r11` (image) vs `add r11,r11,r6` (ours), i.e. the image
    // puts frameCount first and the mFrameCount load second.  Source order does
    // not reach it -- c2 orders each commutative use list by its own key
    // (C2RS-BRIDGE 8.7), whose last run is after lowering:
    //   mFrameCount += frameCount;                      99.818 (inert)
    //   mFrameCount = mFrameCount + frameCount;         99.818 (inert)
    //   unsigned int frames = mFrameCount; ...          99.818 (inert)
    //   unsigned int frames = mFrameCount + frameCount; 99.818 (inert)
    //   const unsigned int &frames = mFrameCount;       99.818 (inert)
    // Both operands are already leaves, so the "name a single-use subexpression"
    // lever has no target here; `frameCount` is a parameter and cannot be
    // respelled into a subexpression.  The tap's pre-sort list for the tuple
    // carrying the 0x90 member is `V1{0x10020} k07[0x90..]`, and every spelling
    // above reproduces it unchanged.
```
