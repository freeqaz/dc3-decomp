# Lane notes: `src/lazer/meta_ham/KinectSharePanel.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?ConvertImages@KinectSharePanel@@AAAXXZ`

`KinectSharePanel::ConvertImages` -- moved verbatim from `src/lazer/meta_ham/KinectSharePanel.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Inside the body (was lines 148-156):

```text
            // An explicit pointer WALK, not an indexed store (w7-bn, 99.03 ->
            // 100.0).  With `previewPtr[i * 4] = -1` MSVC strength-reduces the
            // index and materialises the -4 bias right after the mPreviewBuf
            // load, ABOVE the trip-count test; with the pointer as its own
            // induction variable (`previewPtr += 4` in the increment) the bias
            // lands in the loop preheader after `mtctr` (82953744/48), which
            // is the image.  bg's refutations at 99.03: `area` declared first
            // (inert), `((unsigned char *)mPreviewBuf)[i * 4]` (97.00); bn:
            // `void *` local cast inside the loop (inert).
```


## `?ConvertImagesForLinkPost@KinectSharePanel@@AAAXXZ`

`KinectSharePanel::ConvertImagesForLinkPost` -- moved verbatim from `src/lazer/meta_ham/KinectSharePanel.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Inside the body (was lines 202-216):

```text
        // Open (w26-f15): the one charged row is this multiply -- image
        // `mullw r28,r11,r9` (rowBytes first), ours `mullw r28,r9,r11`.  Both
        // `lhz` loads, both registers and every neighbouring instruction
        // already match, so this is c2's commutative sort key (C2RS-BRIDGE
        // 8.7): `rowBytes` is a variable leaf (0x10000|sid<<5) and
        // `bitmap90.Height()` is a memory leaf on the frame base, and the
        // memory leaf sorts first.  The lever is coupled: inlining the
        // RowBytes() load makes both operands memory leaves, which DOES give
        // the image's operand order (disp 0x68 > 0x66) but swaps the two
        // loads, because they are emitted in the reverse of the operand list
        // -- 2 charged rows instead of 1, fuzzy 99.989 and normalized 99.989.
        // 30 spellings inert otherwise: source swap, a named `height` before
        // or after, `(int)`/`unsigned` casts, `const int &rowBytes`,
        // `RndBitmap &bm = bitmap90` used for the loads, a multi-declarator,
        // and a leading `+ 0`.
```
