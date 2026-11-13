# Lane notes: `src/system/synth_xbox/FftIpp.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?FftRealCcs@FftIpp@@QAAXPIBMPIAM@Z`

`FftIpp::FftRealCcs` -- moved verbatim from `src/system/synth_xbox/FftIpp.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 22-44):

```text
    // NOTE (w7-av): 97.55 is a CR-field floor.  Every one of the 49
    // instructions matches except the condition-register field of this one
    // compare and its branch: the image writes
    //     82E4DC24  cmplwi r29, 0x0     (cr0)
    //     82E4DC28  beq    .L_82E4DC3C
    // and we write `cmplwi cr6, r29, 0x0` / `beq cr6`.  Everything before
    // (indices 0-32, including the assert's own cr0 `cmpwi r3, 0x0`) and
    // everything after (35-48) is byte-identical, so there is no liveness or
    // scheduling difference left to steer the allocator with.  Refuted, all
    // measured: hoisting `n` above the MILO_ASSERT (93.53 -- it splits the
    // single `lwz r29, 0x0(r31)` into an extra load before the assert),
    // `if (n)` instead of `if (n != 0)` (97.55, identical rows), and dropping
    // the local entirely so the guard and the tail both index `mSize`
    // directly (97.55, identical rows -- MSVC CSEs it back into one r29).
    // NEGATIVE RESULT (w9-a): four more spellings, each a full ninja, all exactly
    // 97.55102 with the same two rows -- an inverted `if (n == 0) {} else {...}`,
    // the `if` wrapped in a redundant inner block, the brace-less one-liner, and a
    // `while (n != 0) { ...; break; }`.  Note what the shape of the residue rules
    // out: the FIRST guard at 0x82E4DBB8 is `cmplwi cr6` in BOTH builds and the
    // assert's own `cmpwi r3, 0x0` is cr0 in both, so MSVC is not simply
    // preferring one field -- it reuses cr0 for the third compare where we take
    // cr6, after an identical compare/branch/call prefix.  Nothing in the source
    // names a condition register.  CR-allocation floor.
```

## `XboxAllocator::XboxAllocator()`

`XboxAllocator::XboxAllocator` -- moved verbatim from `src/system/synth_xbox/FftIpp.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

Attribution: no function named `XboxAllocator::XboxAllocator` in report.json (whole report (header)); filed under the source name

Above the definition (was lines 22-29):

```text
    // No user-provided copy ctor or dtor (w16-e).  With both gone the class is
    // trivially copyable, so vector<float,XboxAllocator<float> >::get_allocator()
    // inlines to nothing in _M_fill_assign, as in the image: it passes an
    // uninitialised r1+0x50 straight to _Vector_base(n, const alloc&)
    // (82E4D760 `addi r5, r1, 0x50` / `bl _Vector_base`).  w7-as found removing
    // the copy ctor ALONE inert -- the empty user dtor also has to go.
    // _M_fill_assign 94.4 -> 100, no other row moves.  Behaviour is unchanged:
    // all three were empty.
```


## `XboxAllocator::allocate()`

`XboxAllocator::allocate` -- moved verbatim from `src/system/synth_xbox/FftIpp.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

Attribution: no function named `XboxAllocator::allocate` in report.json (whole report (header)); filed under the source name

Above the definition (was lines 46-54):

```text
    // BUG FIX (w7-as, 2026-09-14): three things wrong here, all visible at the
    // inlined call site in vector<float,XboxAllocator<float> >::
    // _M_insert_overflow (0x828C6C5C..0x828C6C7C):
    //   * a zero-count guard -- `add. r26, r10, r11` / `beq` / `li r28, 0x0`;
    //     we called MemAlloc(0, ...) instead of returning NULL,
    //   * the ALIGNMENT argument is 0x10, not 0 (`li r7, 0x10`).  A float vector
    //     handed to the VMX/IPP spectral code was being 4-byte aligned,
    //   * the line number is 0x2f = 47 (`li r5, 0x2f`), which is where this call
    //     sits in the real common_vector.h; __LINE__ here is this file's line.
```
