# Lane notes: `src/system/os/ContentMgr_Xbox.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Poll@XboxContent@@UAAXXZ`

`XboxContent::Poll` -- moved verbatim from `src/system/os/ContentMgr_Xbox.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Inside the body (was lines 88-92):

```text
        // `= {0}` (w11-b): zeroes HighPart through the frame and LowPart
        // through a precomputed address, then `ld`s it for the by-value
        // argument -- the image's `addi r11,r1,0x64` / `stw r29,0x60(r1)` /
        // `stw r29,0x0(r11)` / `ld r10,0x60(r1)` at 825EA4FC-825EA528.
        // `contentSize.QuadPart = 0` let MSVC SROA the union away.
```


## `?StartRefresh@XboxContentMgr@@UAAXXZ`

`XboxContentMgr::StartRefresh` -- moved verbatim from `src/system/os/ContentMgr_Xbox.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Inside the body (was lines 247-255):

```text
                // ONE enumerator call PER ARM (w7-bn, 99.12 -> 100.0).  bg's two
                // "unreachable" residuals -- the shared `li r3, 0xff` hosted at
                // the end of the i==5 arm with i==6 branching BACK to it
                // (825EC040/825EC058), and the result tested in cr6 (825EC084)
                // -- are one cause: the image has four call sites that MSVC
                // cross-jumps into the single `bl` at 825EC080, so the tested
                // value is a phi (cr6), not a direct call result (cr0).  With
                // param/flags/handle locals and one call: 99.12; a named DWORD
                // result on that one call: INERT; a switch: 92.7.
```


## `?PollRefresh@XboxContentMgr@@UAAXXZ`

`XboxContentMgr::PollRefresh` -- moved verbatim from `src/system/os/ContentMgr_Xbox.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Inside the body (was lines 497-504):

```text
                        // BEHAVIOURAL (w20-p): BREAK, not `continue`.  The image's
                        // hit arm leaves the per-item loop entirely:
                        //     825EB95C  cmplw cr6, r3, r11       ; found vs end()
                        //     825EB960  bne   cr6, .L_825EBABC   ; -> memset(...)
                        // and .L_825EBABC is the post-loop memset/XEnumerateCrossTitle
                        // block, not the `j++` at 825EBAA8.  The first ignored
                        // item stops discovery of every item after it in the
                        // batch.  We had `continue`, which skipped only that item.
```
