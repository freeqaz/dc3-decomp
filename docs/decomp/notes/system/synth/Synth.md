# Lane notes: `src/system/synth/Synth.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?DrawMeter@Synth@@QAAXAAMMMPBD@Z`

```text
lane E w-low-synth-os (2026-11-13), behaviour audit against the target listing, value by value: NOT A BUG
(91.26 canonical).  All five colour constants and their stack slots (grey
0xb0, black 0xe0, white 0x90, green 0xd0 = (0.5,1,0,1), red 0xa0 = (1,0.5,0.5,1),
white2 0xc0), labelPos = Width*0.1f, barLeft = Width*sMeterConsts[0] (0.2),
barWidth = Width*sMeterConsts[2] (0.7), level/peak normalised as (v + 40) *
0.025f and clamped to [0,1] (fneg/fsel/fsubs/fsel), bg/level/peak rect fields
and the DrawRect colour per rect, the red-unless-peak!=1 select, dbLabelPos =
barWidth+barLeft, MakeString("%i", (int)peakHold) via fctiwz of f2, and
y += 16 all agree.  Residual is scheduling only (TheRnd vtable loaded into r29
before the MakeString call in the image).
```
