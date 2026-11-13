# Lane notes: `src/system/os/Debug.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/os/Debug.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

File-level (was lines 16-27; nearest following definition: `NativeModalTap`):

```text
// w8-e 2026-09-15: both 0% `fn_` rows in this unit are symbols.txt name
// collisions, not missing code.
//   * fn_825CCF20 (160 B) is ?DebugModal@@YAXAAW4ModalType@Debug@@AAVFixedString
//     @@_N@Z.  ham_xbox_r.map lists that mangled name TWICE -- 82331FA8 from
//     App.obj (size 0x1D0) and 825CCF20 from os:Debug.obj (this one) -- both
//     bare `f`, which only internal linkage produces.  dtk's apply_symbols_file
//     (jeff src/util/config.rs) can bind a real name exactly once and PARKS the
//     loser as fn_<addr>; config/373307D9/symbols.txt:117577 binds the App copy,
//     so the Debug copy is permanently fn_825CCF20 and can never pair.
//   * fn_825CE40C: see the w17-d note below -- it was a real funclet.
//
// w17-d: fn_825CE40C is Debug::Fail's MemHeapTracker cleanup (now 100).
```


## `?Fail@Debug@@QAAXPBDPAX@Z`

`Debug::Fail` -- moved verbatim from `src/system/os/Debug.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Above the definition (was lines 268-276):

```text
// w17-d: 98.019 -> 100 (and funclets fn_825CE39C/fn_825CE3C4 99.9 -> 100,
// fn_825CE40C 0 -> 100). BEHAVIOUR FIX. The image's unwind funclets name two
// RAII locals we were missing: __unwind$110261 runs
// ??1?$ScopedState@_N$00$0A@@@ on r31+0x54 (mFailing set true on entry, false
// on exit), and fn_825CE40C runs ??1MemHeapTracker on r31+0x2194. With the bare
// `mFailing = true ... MemPopHeap(); mFailing = false;` a `throw msg` out of a
// MILO_TRY left mFailing stuck at true (every later Fail silently ignored) and
// the heap pushed; the image clears both on unwind. `t` is block-scoped so its
// slot pools with the throw temp at 0x50.
```


## `?GetExpCode@@YAPBDH@Z`

`GetExpCode` -- moved verbatim from `src/system/os/Debug.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Above the definition (was lines 408-414):

```text
// BEHAVIOUR FIX (w13-d, 96.75 -> 100): this used to be a hand-unrolled if/switch
// tree whose first half (codes <= 0xC000008D) ended in `default: break;` and then
// FELL OFF THE END of the function -- an unknown code such as 0x80000005
// returned whatever was in r3.  The image sends every unmatched code to the
// "Unhandled Exception %d" MakeString (0x825CC524: reached from the
// `bne cr6` at 0x825CC38C, 0x825CC404 and the `bgt cr6` at 0x825CC470).  It is
// one plain switch; MSVC builds the whole compare tree from it.
```


## `?DoCrucible@Debug@@QAAXW4ModalType@1@PBDPAX@Z`

`Debug::DoCrucible` -- moved verbatim from `src/system/os/Debug.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Inside the body (was lines 548-555):

```text
    // MEASURED, 2026-09-14 (lane w7-aa).  The residual 22 rows here are a single
    // r27<->r28 rotation (16 of 21 swap rows) between these two DataPoints, plus
    // one insert/delete pair at idx 278/282 (`addi r3, r31, 0x50` scheduled four
    // rows apart) and 3 PERMUTED stack slots.  Swapping THESE TWO DECLARATIONS
    // does NOT move the registers -- it keeps the whole r27<->r28 rotation and
    // ADDS 9 offset swaps of (0x70,0x90), i.e. it moves the frame slots instead:
    // 22 rows -> 40 rows, 99.53052 -> 99.5 (worse).  Do not re-try the swap; the
    // lever is liveness across the Directory()/DataPointGetter calls, not order.
```

Inside the body (was lines 584-588):

```text
    // w16-d: the ternary (with THIS polarity) gives the image's register
    // assignment -- &TheSystemArgs in r27, the TextStream vtable in r28; the
    // `exeName = ""; if (!empty()) exeName = front();` statement form swapped
    // them (20 rows). `!empty() ? front() : ""` fixes the registers but emits
    // an extra branch.
```

Inside the body (was lines 604-613):

```text
        // INERT (lane w7-bb, 2026-09-14): folding this if/else into the ternary
        // `DataNode(mCrucibleApp ? mCrucibleApp : exeBase.c_str())` -- the shape
        // suggested by the image hoisting the DataNode temp's address
        // (`addi r3, r31, 0x50`) ABOVE the `cmplwi`/`bne` at idx 278 where we
        // emit it after -- is byte-identical.  DoCrucible stays at 99.53052 with
        // the identical 22 rows, so the two-slot hoist is scheduler-owned, not
        // a conditional-expression-vs-statement difference.
        // w16-d: also inert -- `DataNode(!mCrucibleApp ? exeBase.c_str() :
        // mCrucibleApp)`; and `mCrucibleApp ? DataNode(..) : DataNode(..)`
        // is far worse (92.6, frame +0x20).
```
