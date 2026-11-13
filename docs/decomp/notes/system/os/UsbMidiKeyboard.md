# Lane notes: `src/system/os/UsbMidiKeyboard.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Poll@UsbMidiKeyboard@@SAXXZ`

`UsbMidiKeyboard::Poll` -- moved verbatim from `src/system/os/UsbMidiKeyboard.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Inside the body (was lines 53-60):

```text
            // RESOLVED (w27-b, 98.900 -> 100.0): the image emits `li r26, 1`
            // (slotCounter) BETWEEN `lwz TheKeyboard` and `addi r29, r3, 0x34`
            // (proData), i.e. one slot before the addi.  Declaring slotCounter
            // first really does hoist the `li` to before the call (w7-r, idx 38
            // vs the image's 40), which is why order alone never worked.  The
            // addi's source position moves when the JoypadData pointer is named:
            // with `padData` in a local the addi is materialised one slot later
            // and the li lands in 40 from its natural place at the end.
```

Inside the body (was lines 135-142):

```text
            // The image emits four separate fused rlwinm extractions and a flat
            // add chain (d<<2) + (c<<1) + (e<<3) + b.  The declaration order
            // below is the one that interleaves the loads and shifts the way the
            // image does: lbz 0xd, lbz 0xc, rlwinm 0xd, lbz 0xe, rlwinm 0xc,
            // lbz 0xb, rlwinm 0xe.  Measured declaration orders (w27-b, canonical):
            // B,E,C,D 100.0 | B,D,C,E 99.581 | D,B,C,E 99.165 | D,C,B,E 99.162 |
            // B,D,E,C 98.897 | B,C,D,E 98.687 | B,C,E,D 98.684 | E,B,D,C 98.485.
            // The sum keeps the image's term order (d<<2) + (c<<1) + (e<<3) + b.
```
