# Lane notes: `src/system/utl/Locale.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/utl/Locale.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

File-level (was lines 15-25; nearest following definition: `DataSetLocaleVerboseNotify`):

```text
// w8-g 2026-09-15: fn_827EA01C (40 B, 93.90% normalized) is an EH funclet and is
// NOT adjudicable from this unit.  objdiff flags it UNVERIFIABLE_PAIRING -- the
// two sides were paired by masked byte signature, not by name -- and the two
// mismatched rows say the parent frames disagree: the image's is 0x160
// (`subi r31, r12, 0x160`) where ours is 0x80, and the image's funclet destroys
// a String (`addi r3, r31, 0x80` / `bl ??1String@@UAA@XZ`) where ours frees a
// DataArray (`lwz r3, 0x50(r31)` / `bl ??3DataArray@@SAXPAX@Z`).  So it is a
// funclet of a DIFFERENT parent function in the same object; nothing can be
// fixed here until the parent it belongs to is matched.
// w21-ag: it WAS Locale::Init's funclet (frame 0x160, devkitPath String at
// r31+0x80); with Init's slot layout fixed it reads 100.
```


## `??$FastSort@$02@LocaleChunkSort@@YAHPBX0@Z`

`LocaleChunkSort::FastSort` -- moved verbatim from `src/system/utl/Locale.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Above the definition (was lines 110-115):

```text
// Compares N keys, each the leading 4-byte word of an 8-byte (Xbox DataNode)
// element: node1/node2/node3's values for an OrderedLocaleChunk.
// w21-bc: a plain indexed loop with the two keys named closes the last rows
// (94.44 -> 100); the earlier hand-stepped `b += 8` / `a - b` offset loop made
// MSVC step the parameter register itself instead of a copy (`mr r11, r4`).
// Same 8-byte stride, same signed compare -- behaviour unchanged.
```


## `?FindDataIndex@Locale@@IBA_NVSymbol@@AAH_N@Z`

`Locale::FindDataIndex` -- moved verbatim from `src/system/utl/Locale.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Inside the body (was lines 214-253):

```text
// Residual after the FastSort fix: 85 rows, all regalloc/slot permutation, no
// structural difference.  Frames are the same size (0x160) and the same 18
// callee-saved GPRs are used; what differs is WHICH.  Two independent strands:
//   * r19 <-> r20 (26 rows).  The image keeps `this` in r20 from `mr r20, r3`
//     at idx 8; we keep it in r19.
//   * the `devkitPath` String sits at 0x80/0x84 in the image and at 0x88/0x8c
//     in ours, which is where every `off:+8` row comes from.  The image's
//     DataNode temps for `DataArrayPtr altCfg((DataNode(locale)),
//     DataNode(devkitPath)))` are at 0x88 (the locale node, built inline with
//     `li r19, 0x5` / `stw r19, 0x8c` / `stw r11, 0x88`) and 0xa0 (the
//     devkitPath node, built by ??0DataNode@@QAA@ABVString@@@Z) -- see
//     build/373307D9/asm/system/utl/Locale.s @827E9958..827E9980.  Ours uses
//     the same two temps, eight bytes lower, so the String lands eight bytes
//     higher.  Declaration order of the named locals above already matches the
//     image's addresses (altCfg 0x68 < devkitPath 0x80), so the difference is
//     in the temp pool, not in the source's declaration list.
// w17-d (98.928, canonical unchanged): two structural corrections read off the
// image, neither of which moves the forgiving ruler. (1) The temp-heap push/pop
// is a MemDoTempAllocations scope -- __unwind$103222 destroys one at r31+0xbc.
// (2) The sort and the unique-count loop sit INSIDE `if (mInitialized)`: the
// image's not-initialized branch at 827E9918 lands on .L_827E9DC0 (the arrVec
// teardown), skipping both. Still open: the temp-slot pool. The image has
// devkitPath at 0x80 and its five DataNode temps at 0x88 (locale), 0x90/0x98
// (node1/node2), 0xa0 (DataNode(devkitPath)), 0xa8 (node3), arrVec 0xb0, the
// MemDoTempAllocations 0xbc; ours runs devkitPath 0x88, locale 0x90, node1-3
// 0x98-0xa8, DataNode(devkitPath) 0xb0, arrVec 0xb8, tmp 0xc4 -- one extra
// 4-byte temp below devkitPath (our SystemLanguage() Symbol temp is not pooled
// with the `chunks` new-temp at 0x74 the way the image's is).
// w21-ag: 98.93 -> 100 canonical (fuzzy 99.6; 39 register-only rows left:
// this r20 vs our r19 against the kDataSymbol constant 5, the chunk counter /
// k loop index r27<->r28, one stbx operand order, and the "redefined" notify
// storing prevSym's register where we store curSym's -- the two are equal on
// that path).  The stack layout above is now slot-for-slot the image's.  What
// closed it: (1) LiteralArray's source argument is the previous chunkArr
// (behaviour, see the note in the loop); (2) the locale_keep MakeString is its
// own statement; (3) altCfg's devkitPath argument is the implicit String ->
// DataNode conversion; (4) the chunk loops count with their own `n`.
// Inert for the register rows: `int n` at function scope; assigning numChunks
// after the Sort instead of before it costs 100 -> 99.3 (the image's
// `mr r26, r27` sits before the cfg->Size() > 1 test).
```

Inside the body (was lines 317-352):

```text
                    // BEHAVIOURAL (w21-ag): LiteralArray's error-context argument is
                    // the PREVIOUS chunk array, not curArr, from the second entry on.
                    // Image 827E9BDC `mr r30, r26` (curArr) before the k loop, then in
                    // the loop 827E9C04 `mr r4, r30` / bl LiteralArray / 827E9C10
                    // `mr r30, r3` -- one register carries both.  Only the file/line
                    // of a "Data %s is not Array" failure depends on it.  The size
                    // test re-reads Size() (lha 0x8 is CSE'd; the image spills it to
                    // 0x6c only inside the MILO_FAIL arm, which a named `size` local
                    // would not do).
                    // w25-ha: n (r28) and k (r27) are exchanged against the image
                    // (n r27, k r28).  c2 COLOR keys: n pri 6 tie 472, k pri 4
                    // tie 473, so an equal priority would already pop k first.
                    // Hypothesis: the image's n carries 2 less priority, i.e.
                    // one fewer candidate referenced in a block where n is
                    // referenced and k only live (node2 = n), or one more in a
                    // k-only block (Node(k)).  Inert on the keys: for-increment
                    // order, n++ in the body, Sort/loop bound spelled n, k
                    // declared outside the for, a chunk reference, DataNode(n).
                    // w26-m re-dumped the row for the §8.7 argument-order lever
                    // and it does not apply: the 11 charged rows are all
                    // mr/subic./mulli/stw/addi/cmpwi (no commutative op), and
                    // the tap shows two plain k04 colour candidates -- `k`
                    // id 38 pri 4 tie 473 -> r27, with `n` popping earlier and
                    // taking r28.  It is a priority difference, not a key one.
                    // Six more spellings measured inert: a `chunks[n]` reference
                    // binding, `node2` stored first, `n + 0`, a named `int idx`,
                    // a named `int kk = k`, and `DataNode`-free store reorder.
                    // w26-f12: keys re-dumped today and unchanged -- n id 19 pri 6
                    // tie 472 ben 9 ref 9 -> r28; k id 38 pri 4 tie 473 ben 6 ref 6
                    // -> r27.  Six more spellings inert: an explicit output-pointer
                    // walk (`out++` alongside `k--, n++`), `(int)n` on the stored
                    // counter, the increment pair written `n++, k--`, a hoisted
                    // `chunkBase` alias, and node1/node3/node2 store order and a
                    // named `size` local (those two change the code: 96.666 / 99.695).
                    // No spelling that keeps the instruction stream has moved either
                    // key, which is what "priority, not key" predicts.
```

## `?Init@Locale@@QAAXXZ`

p2-reloc (2026-11-14), canonical 100.0 / fuzzy 99.874, 11 non-equal row(s): NOT A BUG.
Every row is register-only, and `scripts/analysis/regperm_equivalence.py` finds one
consistent target<->base value pairing at every use (parameters, call arguments, stores,
compares): pure register permutation, no operand swap.
