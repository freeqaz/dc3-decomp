# Lane notes: `src/system/rndobj/Group.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?MoveObject@RndGroup@@QAAHPAVObject@Hmx@@H@Z`

`RndGroup::MoveObject` -- moved verbatim from `src/system/rndobj/Group.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 400-415):

```text
// w18-a: 100 normalized, 1 fuzzy row (idx 31).  The delta == 0 edge (`beq cr6`
// off the delta compare) lands on the shared `cmplw r29, r31` in the image; we
// jump-thread it straight to Update() because target == node is known on that
// path.  Inert: the negative walk as a pre-tested `while (remaining != 0)`, and
// `else if (delta < 0)` (becomes bge, still threaded).
// w25-gc: not a behaviour difference (on the delta == 0 edge target == node,
// so both sides reach Update() with no relink), and not a register decision:
// every register and instruction matches, only that one branch target moves.
// Mechanism hypothesis: MSVC's jump threading proves `node != target` false
// on that edge from the `target = node` copy; the image either threads before
// that copy is propagated or never sees a copy. Open question: which source
// shape hides the equality. Inert (or worse) as well: `target` assigned in
// each arm, `target != node`, `it.mNode != target`, the negative walk as a for
// loop, `remaining != 0` as its guard, an empty `delta == 0` arm, the positive
// walk as a while loop, an extra `remaining == delta` test, and Update() +
// return duplicated into a `node == target` arm.
```

p2-reloc (2026-11-14), canonical 100.0 / fuzzy 99.904, 1 non-equal row(s): One
BRANCHDEST row (idx 31): the delta == 0 edge, where target == node, so both reach
Update() with no relink (w25-gc). NOT A BUG.
