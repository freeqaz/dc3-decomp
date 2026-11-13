# Lane notes: `src/system/synth/ThreeDSound.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?SyncProperty@ThreeDSound@@UAA_NAAVDataNode@@PAVDataArray@@HW4PropOp@@@Z`

`ThreeDSound::SyncProperty` -- moved verbatim from `src/system/synth/ThreeDSound.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Attribution: definition is macro-generated (BEGIN_PROPSYNCS); symbol matched by the name the macro expands to

Above the definition (was lines 39-44):

```text
// w20-g: normalized 100.0, fuzzy 99.95 (name_check). Every operand and branch
// target checked against the image: no divergence. The 4 charged rows (0x160c/
// 0x1614, 0x16a0/0x16a8) are only the NAMES of the radius / doppler_power
// `static Symbol _s`: the image's ?_s@?DM@ / ?_s@?EP@ are config names in
// symbols.txt (since 4abc61f0f, not in the map), ours mangle ?FE@ / ?FP@ --
// MSVC's per-function scope ordinal. Same guard bits (0x80/0x100), same strings.
```


## `?CalculateFaderVolume@ThreeDSound@@AAAXXZ`

`ThreeDSound::CalculateFaderVolume` -- moved verbatim from `src/system/synth/ThreeDSound.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 273-315):

```text
        // NEGATIVE RESULT (w7-af, 2026-09-14): putting the radius test INSIDE
        // `case 1:` and reaching the shared block with a `goto` back into the
        // mSilenceDistance arm does fix the dispatch -- 0x827661C8 `blt` and
        // 0x827661CC `beq` then land on two different labels and the post-FAIL
        // re-read of mShape disappears -- but MSVC sinks BOTH the radius block
        // and the -96.0f block to the tail of the function, inverting
        // 0x8276618C `blt cr6` to `bge` and costing more than the dispatch
        // gains. 89.46 -> 83.60, measured twice (goto to a `done:` label after
        // the chain, and goto back to a `silent:` label inside the first arm --
        // byte-identical results). The residual here is block PLACEMENT, and
        // this spelling is the better of the two measured.
        // NEGATIVE RESULT (w7-ao, 2026-09-14): the third spelling, which w7-af
        // did not measure -- a `bool silent` set inside `case 1:` and tested
        // after the switch -- is worse than both: 89.46 -> 86.87. It does
        // collapse the dispatch to one `lwz`/`cmplwi`, but MSVC then
        // MATERIALISES the flag (`li r30, 0x0` + a second `li`), which the
        // image never does, and still sinks the -96.0f block.
        //
        // w7-bu (89.5 -> 94.5): the -96.0f placement WAS reachable.  Both
        // `vol = -96.0f` copies are tail-merged; MSVC deletes the copy that is
        // only reached by a jump and keeps the fall-through one.  With the
        // radius test written `if (mShape == 1 && unk210 > mRadius) vol = -96`
        // the radius copy is the fall-through, so the first arm's copy is the
        // one deleted and 0x8276618C `blt` becomes `bge` forward.  Inverted --
        // `if (mShape != 1 || unk210 <= mRadius) { falloff } else { -96 }` --
        // the radius copy is the jump-only block, it is the one deleted, and
        // the first arm keeps its inline load with 0x827661FC `bgt cr6`
        // jumping back to it, exactly as in the image.
        // RESIDUAL 8 rows: the post-FAIL re-test.  The image's default arm
        // goes `b` straight to the falloff (after 0x827661E8 Fail), so its
        // radius test lives inside `case 1:`; every goto/flag spelling of
        // that (w7-af, w7-ao, and the else-form + goto measured here: 83.6)
        // sinks the blocks.  Caching `int shape = mShape` so the re-test can
        // fold is worse (91.2: MSVC neither folds it nor keeps &mShape as
        // the MakeString argument).
        // w15-i2: WORSE (47.0): the falloff block duplicated into each case
        // arm (case 1 testing the radius first), hoping MSVC cross-jumps it.
        // w19-e: 94.5 -> 100.  The radius test lives inside `case 1:` and
        // leaves through an early `SetVolume(-96.0f); return;` -- MSVC
        // cross-jumps that call into the first arm's `vol = -96.0f` ->
        // SetVolume tail (image: 0x827661FC `bgt cr6` back to 0x82766190),
        // and the default arm's FAIL falls straight into the falloff.  Same
        // behaviour as the flag/goto spellings above, with no re-test.
```
