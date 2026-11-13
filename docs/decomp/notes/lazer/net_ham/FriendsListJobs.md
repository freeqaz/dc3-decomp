# Lane notes: `src/lazer/net_ham/FriendsListJobs.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?OnMsg@UpdateFriendsListJob@@AAA?AVDataNode@@ABVPlatformMgrOpCompleteMsg@@@Z`

`UpdateFriendsListJob::OnMsg` -- moved verbatim from `src/lazer/net_ham/FriendsListJobs.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Inside the body (was lines 77-81):

```text
            // MakeString binds a const& to its argument; handing it a copy
            // keeps `xuid` itself un-addressed, so c2 demotes it to a plain
            // temp and the second allocator gives it r17 (image) instead of a
            // colour candidate taking r27 and shifting r17-r27 (w25-gm, c2rs
            // cands: CDEMOTE 26 -> 27).
```
