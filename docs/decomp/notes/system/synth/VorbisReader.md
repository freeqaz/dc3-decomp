# Lane notes: `src/system/synth/VorbisReader.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?CheckHmxHeader@VorbisReader@@AAA_NXZ`

`VorbisReader::CheckHmxHeader` -- moved verbatim from `src/system/synth/VorbisReader.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 501-519):

```text
                // The 2-row residual is a stack-temp assignment: the target
                // reads the FIRST 8-byte value into 0x50 and the second and
                // third into 0x58, while we use 0x58 for all three (and 0x50
                // for the MILO_NOTIFY_ONCE static's temp, which the target puts
                // at 0x58). Both sides use exactly two slots.
                // Measured WORSE (w7-m, 99.98726 -> 99.9, frame +0x10): giving
                // the first read its own named `s64 magic`, and the same again
                // wrapped in a nested block. MSVC allocates a THIRD slot either
                // way -- it does not overlay two named 8-byte locals whose live
                // ranges are disjoint, so a second variable cannot be the shape
                // the original used here.
                // w16-e (99.987): `s64 idx` at function scope is inert;
                // a function-scope `magic` for the first read plus this idx
                // for the other two is worse (99.92, frame changes).
                // w24-slot: the image's layout puts read 1 in the slot pooled with the
                // assert-line temps, and reads 2/3 in the slot pooled with the
                // NOTIFY_ONCE temp.  That is two 8-byte homes whose pools are chosen by
                // liveness (compiler temps share by live range, not scope).  Not tried
                // further.
```
