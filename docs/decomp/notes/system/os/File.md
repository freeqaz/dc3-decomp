# Lane notes: `src/system/os/File.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?FileLocalize@@YAPBDPBDPAD@Z`

`FileLocalize` -- moved verbatim from `src/system/os/File.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Inside the body (was lines 588-619):

```text
                    // NEGATIVE RESULT (w7-ap, 2026-09-14, 91.62 canonical):
                    // writing it the other way round -- `!HongKongExceptionMet
                    // || (strstr == 0 && strstr == 0)` with the language arm
                    // first -- is BYTE-IDENTICAL, same 24 rows.  MSVC
                    // normalises the two spellings and still emits the
                    // language arm at the fallthrough (idx 79 is `stb r8,
                    // 0x1(r11)` vs `bl SystemLanguage` either way), so the
                    // block order in this region is not reachable from the
                    // condition's polarity.  The form below is kept because it
                    // is the one the listing's branches describe.
                    //
                    // 100% (w7-bp, 2026-09-15, was 91.62): the residual was
                    // never the condition -- it was the two NAMED LOCALS the
                    // language arm used to carry (`char *dst = &buffer[...]`
                    // and `const char *langStr = SystemLanguage().Str()`).
                    // With them, both arms end in a textually identical
                    // `memcpy(dst, src, 3)` and MSVC CROSS-JUMPS them: the eng
                    // arm degenerates to one `lbz` plus a `b` into a shared
                    // tail that computes `subf/add/addi` once and does all
                    // three `stb`.  The image does not merge them -- it emits
                    // the address chain and the three stores TWICE, once per
                    // arm (0x825D0BC4-D8 `subf r11,r31,r30` / `add r11,r11,r29`
                    // / three `stb 0x1..0x3(r11)` / `b`, and again at
                    // 0x825D0BF0-0C1C into r31/r30 across the SystemLanguage
                    // call).  Spelling the destination expression inline in
                    // BOTH arms, so neither block has a common named value to
                    // merge on, reproduces that exactly: 141/141 equal.  The
                    // dead `addi rN, rM, 0x1` each arm emits is the `+ 1` of
                    // `&buffer[p + 1 - iFilename]`, which the inlined 3-byte
                    // memcpy then addresses as 0x1/0x2/0x3 off the pre-`+1`
                    // base -- keep the subscript spelling, not a `buffer + n`
                    // one.
```


## `?NewFile@@YAPAVFile@@PBDH@Z`

`NewFile` -- moved verbatim from `src/system/os/File.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Above the definition (was lines 667-671):

```text
// w25-rc: the capture write is guarded by the positive condition and both
// paths share ONE `return theFile`.  The early `return theFile` gave theFile
// an extra reference in its own block (colour priority 7 vs iFilename's 2), so
// theFile took r28; with the single return theFile drops to -6 and iFilename
// (image r28) is coloured first.
```
