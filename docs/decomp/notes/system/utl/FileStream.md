# Lane notes: `src/system/utl/FileStream.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/utl/FileStream.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

File-level (was lines 12-31; nearest following definition: `FileStream::FileStream`):

```text
// REFUTED (w9-a): flipping TextFileStream.h line 2 to a backslash does take this
// row to 100.0% and costs SEVEN others.  Whole-binary A/B, full ninja both ways:
//   UP 1    utl/FileStream  ??_GFileStream                  99.583 -> 100.0   96 B
//   DOWN 7  synth/WavReader ??0WavReader ctor               100.0 -> 99.9505 808 B
//           rndobj/HiResScreen ?Finish@HiResScreen          100.0 -> 99.9408 676 B
//           gesture/SkeletonClip ?StopRecordingNoClear      100.0 -> 99.8667 300 B
//           rndobj/Bitmap ?LoadBmp@RndBitmap                100.0 -> 99.8387 248 B
//           os/HDCache ?OpenHeader@HDCache                  100.0 -> 99.7872 188 B
//           rndobj/Bitmap ?SaveBmp@RndBitmap                100.0 -> 99.7297 148 B
//           midi/MidiReader ??3FileStream@@SAXPAX@Z         100.0 -> 98.3333  24 B
//   matched_functions 31374 -> 31368, matched_code 5,569,568 -> 5,567,272.
// obj/Data.h's comment on the same trade says "6 want forward"; the measured
// number is 7.  The retail build carried both spellings because it had per-TU
// headers; one shared PCH cannot, so this unit cannot reach 100% without
// splitting the PCH.  Do not re-try the flip.
//
// w21-bc: CLOSED (99.583 -> 100) by building this one TU without the shared
// PCH (configure.py `pch_excluded_sources`), so line 1 below is the first
// include of utl\FileStream.h and fixes its __FILE__ spelling, as in retail.
// Whole-binary A/B: UP 1, DOWN 0.
```
