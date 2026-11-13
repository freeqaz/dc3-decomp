# Lane notes: `src/system/obj/Task.h`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?ResetTaskTime@TaskTimeline@@QAAXM@Z`

`TaskTimeline::ResetTaskTime` -- moved verbatim from `src/system/obj/Task.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

Above the definition (was lines 156-161):

```text
    // w8-g: defined IN-CLASS on purpose.  MSVC/Xenon propagates a same-TU
    // callee's real register-clobber set into its callers, so an out-of-line
    // definition in Task.cpp let TaskMgr::ResetTaskTime keep `this` in r9 and
    // the second float in fr2 across both calls.  The target saves r31/fr31
    // instead, i.e. it had NO clobber information -- which is what a deferred
    // in-class body gives you.  Measured: out-of-line 73.6%, in-class 100%.
```
