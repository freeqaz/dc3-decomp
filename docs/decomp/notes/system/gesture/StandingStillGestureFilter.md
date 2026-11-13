# Lane notes: `src/system/gesture/StandingStillGestureFilter.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Update@StandingStillGestureFilter@@QAAXABVSkeleton@@H@Z`

`StandingStillGestureFilter::Update` -- moved verbatim from `src/system/gesture/StandingStillGestureFilter.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Above the definition (was lines 36-50):

```text
// 100% canonical (w7-bm, 2026-09-15; was 96.6). The image tail-merges every
// state store: the not-tracked site (state 2) at 0x8c is the cross-jump body
// holder and states 3..9 are each `li r9,k; b 0x8c`, so every site below is
// written as the full store/reset/return tail rather than as one `int state`
// chain with a single store after it (that chain puts the join after the
// last site and leaves the state-2 site as a separate store: 96.6; the
// r30/r31 this/skeleton rotation and the 0x50..0x90 slot permutation were
// downstream of that join placement). The dotR test must `goto` a label
// INSIDE the dotL block: any spelling that makes the state-9 tail the
// function's fall-through (`dotR > t || dotL > t`, or `if (dotR <= t) {..}`
// followed by the tail) makes it the body holder and moves the whole block
// to the end (94.2). Residual: 4 register-only rows (r9/r11, value vs address
// registers in the 16-byte `unk38 = skeleton.GetUnkab0()` copy); hoisting
// `pos` above the `if` and copying from it drops the dead source-address
// addi the image has (99.6).
```

Inside the body (was lines 163-176):

```text
    // w25-rl stop note (c2rs regasg tap): the r9/r11 swap in this 16-byte
    // copy is not a colour decision. All three values (the first word's lwz,
    // the dead src/dst `addi` addresses) are regasg temps, and regasg sees
    // lwz, addi src, addi dst in sched3 order, so first fit gives the word
    // r11 (r10 holds mRaisedMs). The image's r9 word means r11 was blocked
    // at the lwz: the src addi came first in sched3, or was a colour
    // candidate (memcpy makes the DST addi one, but reloads mRaisedMs).
    // Inert: GetUnkab0Padded() casts, a named `cur` ref, `!mRaisedMs`,
    // Vector3 temp, savedPos split. w26-f13 also measured inert: a named
    // `const Vector3 &` for the call result, and a named `const Vector3 *`
    // src for the same; a named `Vector3 &`/`Vector3 *` DESTINATION moves the
    // dst addi into a k04 but costs 0.63 fuzzy / 0.36 canonical, so the copy
    // body itself is what has to move. Open question: which IL shape orders
    // the copy's src address ahead of its first load in sched3.
```
