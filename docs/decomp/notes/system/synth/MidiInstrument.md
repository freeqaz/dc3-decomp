# Lane notes: `src/system/synth/MidiInstrument.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `??0NoteVoiceInst@@QAA@PAVMidiInstrument@@PAVSampleZone@@EEHHM@Z`

`NoteVoiceInst::NoteVoiceInst` -- moved verbatim from `src/system/synth/MidiInstrument.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 12-22):

```text
// w14-e (97.95, 6 rows: the 0x39/0x3a/0x3b byte-store order, one fadds
// operand order and one lwz schedule).  RB3's inline
// `zone->Volume() + RatioToDb(...)` fixes the fadds order but hoists the
// Volume() load above the call into f31 (95.8); `db + zone->Volume()` and
// `db += zone->Volume()` are byte-identical to the current spelling.
// w19-e: 97.95 -> 100.  The image reads SampleZone's mCenterNote, mVolume
// and mPan as MEMBERS, not through the CenterNote()/Volume()/Pan() inline
// accessors (one inline level fewer; friend access added in SampleZone.h).
// mCenterNote direct put the 0x39 byte store back in order; mVolume direct
// with RatioToDb inline in the sum gave `fadds f1, f1, f0` (db first) with the
// Volume load after the call; mPan direct moved the mSample reload after it.
```
