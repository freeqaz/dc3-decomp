# Lane notes: `src/system/os/VirtualKeyboard.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `VirtualKeyboard::Terminate()`

`VirtualKeyboard::Terminate` -- moved verbatim from `src/system/os/VirtualKeyboard.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Attribution: no function named `VirtualKeyboard::Terminate` in report.json (unit); filed under the source name

Above the definition (was lines 45-53):

```text
// w8-g 2026-09-15: ?Terminate@VirtualKeyboard@@QAAXXZ reads 0.00% (4 B) and is
// AT_LIMIT by construction, not by codegen.  In the image it is a linker ICF
// thunk -- a single `b OnlyReturns` -- and the two sides of the accounting
// disagree about where it lives: ham_xbox_r.map credits the symbol to
// os:VirtualKeyboard.obj, while config/373307D9/splits.txt puts its ADDRESS
// inside the Memcard_Xbox range (see the note beside MemcardXbox::Terminate in
// os/Memcard_Xbox.cpp, which folded to the same address).  No source spelling of
// an empty member function can produce a 4-byte branch-to-another-function; only
// the linker can.  3 prior attempts, all refuted.
```
