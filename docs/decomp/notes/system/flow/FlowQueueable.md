# Lane notes: `src/system/flow/FlowQueueable.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?ChildFinished@FlowQueueable@@UAAXPAVFlowNode@@@Z`

`FlowQueueable::ChildFinished` -- moved verbatim from `src/system/flow/FlowQueueable.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 125-144):

```text
            // w16-d (99.53, 9 rows at 0x82xxxD50..D90): the image keeps the
            // begin NODE in r10 and re-reads its value for ReleaseListener
            // (`lwz r4, 0x8(r10)`), i.e. front is reached through the node, not
            // copied. `Hmx::Object *&front = mListeners.front();` (or an
            // iterator `first` with `*first`) reproduces every register, but
            // MSVC then rotates the loop as a guarded do-while (cmplw/beq at
            // entry) where the image enters with `b <cond>` -- 98.6, 3 rows.
            // `for (++it; ...)` and `while (++it != end)` give the same
            // rotation; comparing against `mListeners.front()` inside the loop
            // is 99.1. Kept the higher-scoring value copy.
            // w21-ad (still 99.53; behaviour re-read vs 823F82C4..823F8368:
            // agrees, incl. the size()<=1 fallthrough to the !empty arm):
            // the rotation is LICM's -- whenever the value load sits INSIDE the
            // loop (reference `front`, `*first`-in-loop) MSVC hoists it into a
            // guarded preheader (98.6, 3 rows), and whenever it is a copy
            // before the loop the release reuses the copy. Measured: iterator
            // `first` + `front = *first` + ReleaseListener(*first) = CSE'd, same
            // 9 rows; ReleaseListener(mListeners.front()) re-reads begin AND
            // value (99.5, 8 rows); ref + value copy 98.1; `for (it++; ..; it++)`
            // with ref 98.6; `!found &&` in the condition 96.6; std::find 96.6.
```
