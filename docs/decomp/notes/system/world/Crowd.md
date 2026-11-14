# Lane notes: `src/system/world/Crowd.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?SetFullness@WorldCrowd@@QAAXMM@Z`

`WorldCrowd::SetFullness` -- moved verbatim from `src/system/world/Crowd.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 770-776):

```text
            // The image walks mInstances TWICE: three count loops back to
            // back before the fctiwz (mInstances into r7, mBackup into r8, the
            // `add r8, r8, r7` sum, then mInstances again into r10), and it is
            // that second walk, r10, which BOTH comparisons and both
            // subtractions consume (`cmpw cr6, r9, r10` / `subf r10, r10, r9`).
            // w10-b: summing the first two walks in one expression and testing
            // curInstances (not the first walk) on both arms -> 100.
```


## `?Mats@WorldCrowd@@UAAXAAV?$list@PAVRndMat@@V?$StlNodeAlloc@PAVRndMat@@@stlpmtx_std@@@stlpmtx_std@@_N@Z`

`WorldCrowd::Mats` -- moved verbatim from `src/system/world/Crowd.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 982-986):

```text
                // BUG FIX (w19-x): the per-material shader-variant loop is INSIDE
                // the random-colour test -- 0x82838E8C `beq` on mUseRandomColor (lbz
                // 0x28) jumps to the character-list latch, past SetMatColorFlags
                // AND the material loop.  We built colour variants for every
                // crowd character, random-coloured or not.
```


## `?DrawShowing@WorldCrowd@@UAAXXZ`

`WorldCrowd::DrawShowing` -- moved verbatim from `src/system/world/Crowd.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 1094-1121):

```text
    // Remaining at 94.9697 canonical (152 rows). The FPR permutation cascade is
    // downstream of a STACK-SHAPE difference, so chase that first, not the
    // registers:
    //   - our frame is 0x270, the image's 0x290 -- we are 0x20 SHORT, and the
    //     image has one stack object we do not emit at all (slot 0x170, the
    //     RndEnvironTracker `tracker`; ours lives at 0x130). So this is a
    //     missing/extra local, not spill shaping.
    //   - `_at` (START_AUTO_TIMER) is at 0xd0 in the image and 0xe0 here, and
    //     an 8-byte address object occupies the other slot on each side -- one
    //     declaration-order swap at the top of the body.
    //   - the `rects` vector is at 0x58 in the image (see __unwind$225663,
    //     `addi r3, r31, 0x58` before ~vector<Hmx::Rect>) and 0x50 here.
    // Two Function Call Diff rows here are ICF artifacts, NOT source bugs: the
    // image's `erase` resolves to the vector<LightPreset::SpotlightDrawerEntry>
    // instantiation and its MakeString to <char const[19],int,char const[5]>.
    // Both are 16-byte-POD / any-4-byte-enum folds of our own instantiations --
    // the same representative MakeString name appears in CamShotFrame::Interp.
    // w33-d (2026-11-04): canonical 100, fuzzy 99.90923 over 10 diff_arg rows.
    // Rows 97/106 are two symbol relocations (lis then addi) resolved
    // link-time; canonical 100 and no source lever. Seven rows (103, 151,
    // 156, 388, 394, 577, 579) move the same value between f23 and f24, the
    // callee-saved pair spanning a call, and row 456 is the rect loop's
    // `y + h` fadds operand order. Nine spellings (named refs for the
    // half-extents and the hoisted
    // 0.5f, reordered extent expressions, a named rect sum) measured inert.
    // This unit's per-object path is INEXACT for this function, reading
    // 99.86385 canonical 99.98487 against these report values, so any candidate
    // must be measured on a full ninja.
```

## `?Apply3DCharXfm@WorldCrowd@@QAAXABU?$_List_iterator@UCharData@WorldCrowd@@U?$_Nonconst_traits@UCharData@WorldCrowd@@@stlpmtx_std@@@stlpmtx_std@@HPAVRndCam@@@Z`

p2-reloc (2026-11-14), canonical 100.0 / fuzzy 99.848, 3 non-equal row(s): NOT A BUG.
Every row is register-only, and `scripts/analysis/regperm_equivalence.py` finds one
consistent target<->base value pairing at every use (parameters, call arguments, stores,
compares): pure register permutation, no operand swap.
