# Lane notes: `src/system/synth_xbox/SynthSample.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/synth_xbox/SynthSample.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

File-level (was lines 87-118; no definition follows it):

```text
// NOTE ON ?SampleAlloc@@YAPAXHPBDH0H@Z ABOVE -- kept at end-of-file on purpose:
// SampleFree() a few lines below it passes __LINE__, so ANY comment inserted
// above these functions shifts that immediate and costs real bytes.  Measured
// 2026-09-13: a 9-line note above SampleAlloc moved
// ?SampleFree@@YAXPAXPBDH1@Z from 100.0 to 99.888885 (-36 B).
//
// UNMEASURED BY CONSTRUCTION.  ham_xbox_r.map lists ?SampleAlloc@@YAPAXHPBDH0H@Z
// at two addresses -- synth:SynthSample.obj 0x8273B818 (the portable one, which
// report.json scores) and synth_xbox:SynthSample.obj 0x82E42D48 (this one).
// symbols.txt can only bind the name once, so dtk carves this address as
// fn_82E42D48 and nothing ever pairs it with the definition above.  Adjudicate
// it against build/373307D9/asm/system/synth_xbox/SynthSample.s.
// Audited 2026-09-13: 33/33 instructions equal, the only textual difference an
// ICF fold survivor name (both MakeString instantiations -> 0x824D1870).
// Tool: scripts/analysis/map_multiplicity_census.py
//
// w8-d 2026-09-15: re-measured at 0.0 (132 B) after a full ninja and the
// adjudication above HOLDS -- this row is not closable by this lane's levers.
// Checked explicitly: the ICF/weak-alias rename that closed the other eight 0%
// rows in this lane does NOT apply here.  That lever works when ham_xbox_r.map
// prints SEVERAL names at ONE address and symbols.txt is bound to the one whose
// object dtk does not carve the address into; the fix is to bind the name the
// map gives the owning object.  Here the shape is the mirror image -- ONE name
// at TWO addresses (0x8273B818 synth:SynthSample.obj, 0x82E42D48
// synth_xbox:SynthSample.obj), both of which our tree really does define, since
// synth/SynthSample.cpp and synth_xbox/SynthSample.cpp both compile the body.
// symbols.txt maps name -> address and cannot bind one name twice, so exactly
// one of the pair must carve as fn_ no matter which is chosen.  Moving the
// binding to 0x82E42D48 would only push the 0% onto 0x8273B818, which scores
// 100.0 today; it is zero-sum at best and would be a goalpost-move, since the
// map gives both addresses the same name and neither is the "wrong" one.
// Refuted as a rename target; left as-is deliberately.
```
