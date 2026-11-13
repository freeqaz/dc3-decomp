# Lane notes: `src/system/ui/UIScreen.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?UnloadPanels@UIScreen@@UAAXXZ`

`UIScreen::UnloadPanels` -- moved verbatim from `src/system/ui/UIScreen.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 148-163):

```text
    // w23-g10: the image does NOT clear any task timeline here.  Native used
    // to wipe the whole kTaskSeconds and kTaskUISeconds timelines on every
    // screen unload, on the premise that the native cascade skipped
    // ReplaceRefs so an AnimTask holding a dying panel object outlived it.
    // That premise is gone: since 2026-08-20 ~Object runs NullifyAllRefs in a
    // cascade, which steps every OWNER-CONTROL holder (AnimTask among them,
    // Object.cpp "Owner-control holders during the native cascade") through
    // its Replace(nullptr) exactly as the image's ReplaceRefs does.  The wipe
    // also killed every unrelated seconds/UI-seconds task (DTA-scheduled
    // MessageTasks, other screens' anims) at each transition, which the image
    // keeps running.
    //
    // Suppress FlushDeferredFrees between panel cascades. Without this,
    // panel A's cascade frees memory, then panel B's NullifyAllRefs walks
    // ring nodes in that freed memory → heap corruption. Deferring the
    // flush keeps all memory valid until every panel is destroyed.
```
