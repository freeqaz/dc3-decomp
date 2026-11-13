# Lane notes: `src/system/char/CharClip.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?AddNode@Transitions@CharClip@@QAAXPAV2@ABUCharGraphNode@@@Z`

`CharClip::Transitions::AddNode` -- moved verbatim from `src/system/char/CharClip.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Above the definition (was lines 128-158):

```text
// w21-j: 96.15 -> 99.2 normalized. Levers: the else-arm is a placement new
// of the WHOLE NodeVector (its inline ctor builds clip(owner, NULL)) -- the
// image keeps that guarded result in r30 (`mr. r30,r3 ... li r30,0`) and uses
// it as the vector afterwards; and the tail start is one local. Remaining
// 3 rows: the image computes Next() twice (`add r10,r11,r3` / `add r4,r11,r3`
// then `addi r3,r10,0x8`); we CSE to one add. REFUTED (each folds the +8 into
// (size+4)*8, 97.5): two Next() calls, `next` + a second Next() in either
// argument, an intptr_t round-trip, `nodes + size (+1)`. Tried
// `(CharGraphNode *)next + 1`: same 99.2.
// w21-ap (99.17, same 3 rows; the SetObjConcrete<AnimTask> vs <CharClip>
// row is an ICF-folded name). Standalone cl.exe probe of the whole function,
// ~40 spellings: every form either CSEs to ONE add (`next` local, `char *`
// or `void *` locals, dst declared first, `&next->clip`, `&next->clip.p`,
// `next->nodes - 2`, `end + 1` over a CharGraphNode *, byte-offset `off`
// locals, Resize result copied through `nodes`/a second variable) or
// re-derives both with the +8 folded into (size+4)*8 (two Next() calls,
// &nodes[size(+1)], member-of-Next(), four alternative Next() bodies). None
// keeps the shared (size+3)*8 with two adds and a separate addi as the image
// does -- it looks like a register-allocator rematerialisation, not a source
// shape. Behaviour re-read against the image: same moved count (computed
// before Resize), same dst/src, same insertion and ring fix-up.
// w26-q (99.0909 fuzzy / 99.17355 canonical, same 3 rows): 5 more spellings of
// the tail start, all inert or worse -- `char *nextBytes` (byte pointer, one
// base + two displacements) is byte-identical, a named `dst` local is
// byte-identical, and the two-inline-Next() forms regress to 97.46 because the
// +8 folds into (size+4)*8. The mechanism is the second allocator, not a
// commutative key: the image rematerialises `this + 0x18 + size*0x60` for the
// second use of the same value instead of reusing the CSE'd temp (two `add`
// into different registers, then `addi` off the first), and no source spelling
// reaches that decision. Open handle: a shape where c1 emits two distinct
// symbols for the same address expression without changing the (size+3)*8 base.
```

Inside the body (was lines 174-180):

```text
        // w22-a23: the image's 0x20 (`addi r4, r11, 0x20` before Resize) is the
        // Xbox size of a ONE-node vector -- the 0x18 header (ObjOwnerPtr + size)
        // plus one CharGraphNode, i.e. the Next() of the vector built here. On
        // LP64 the header is 0x34 (ObjOwnerPtr<CharClip> is 48 bytes), so the
        // literal under-allocated by 28 bytes and the placement new + nodes[0]
        // store below wrote past the end of the block. Same quantity, native
        // layout.
```


## `?Load@Transitions@CharClip@@QAAXAAVBinStreamRev@@H@Z`

`CharClip::Transitions::Load` -- moved verbatim from `src/system/char/CharClip.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Inside the body (was lines 261-292):

```text
    // RESIDUAL (w13-c, 99.500): the four int count slots are assigned in
    // exactly the reverse order (image 0x50/0x54 = old-branch counts,
    // 0x58/0x5c = new-branch counts).  Inert, byte-identical: an early
    // `return` out of the oldRev < 8 arm (the lever that fixed WorldDir::Poll),
    // swapping the old arm's `num_nodes, num_node_vectors` declaration order,
    // and swapping buf/buf2.
    // w16-c: stack-layout reading of the residual -- image 0x50 = the shared
    // PathName / placement-new temp, 0x54 = old-arm count, 0x58/0x5c = new-arm
    // node_vectors/nodes; ours is exactly reversed.  Inert: hoisting the
    // new-arm pair to function scope (old pair renamed); swapping the arms
    // (`oldRev >= 8` first) is 32.5 -- the image lays the old arm out first.
    // w21-ad (99.9 after the NodeVector placement-new fix below): only the
    // slot permutation remains -- image [0x50 it/PathName temp, 0x54 old
    // num_nodes, 0x58 new num_node_vectors, 0x5c new num_nodes], ours [0x50
    // new num_nodes, 0x54 new num_node_vectors, 0x58 it, 0x5c old num_nodes].
    // Also inert: swapping the new arm's declaration order, declaring `it` at
    // function scope, scoping the old arm's num_node_vectors into its loop.
    // w24-slot (99.9, unchanged): the slot rule (size, then reference count
    // desc, then last reference asc, greedy reuse across disjoint scopes) does
    // NOT reproduce either side here.  Listing counts: old num_nodes 4, new
    // num_node_vectors 4, new num_nodes 3, PathName temp 2, it 2.  The rule puts
    // a 4-count local first on both sides, but the image starts with the
    // PathName-temp/it slot and ours with new num_nodes.  The old and new arms'
    // counts also never share a slot, though they are disjoint if/else arms.
    // Treat this layout as unexplained, not as a declaration-order question.
    // w26-d: the 18 charged rows on the current tool are the same permutation
    // (0x50/0x54 <-> 0x58/0x5c, the two pairs swapping as units) plus one ICF call
    // name (SetObjConcrete<AnimTask>/<CharClip>, one address in
    // icf_aliases.map), so no value is read from a slot it was not written to.
    // Re-measured inert: hoisting the OLD arm's count pair to function scope
    // (the direction opposite to w16-c), and sharing one pair across both arms
    // (98.4, one extra slot).
```

Inside the body (was lines 353-363):

```text
                // Owner is the Transitions container, not mOwner: the target
                // passes `this` (823D5830 `mr r4, r25`, r25 being the same
                // register 0xc(r25) is loaded from for mOwner->Dir()).
                // w21-ad: the image placement-news the whole NodeVector through
                // its inline ctor (NodeVector(ObjRefOwner *) builds `clip`), and
                // every later access goes through the new-expression's value:
                // the guard's null arm is `li r27, 0` (823D5840) into the SAME
                // register `it` lives in, and the assignment, size reads and
                // Next() all use r27 (823D5848..823D58B4). The earlier
                // `new (&it->clip) ObjOwnerPtr<CharClip>(...)` spelling used the
                // out-of-line ctor's return in r3 instead (99.50 -> 99.9).
```
