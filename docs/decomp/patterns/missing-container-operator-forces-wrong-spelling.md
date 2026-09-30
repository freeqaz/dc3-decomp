# A missing container operator forces a wrong spelling

**dc3-decomp (title 373307D9).** When the operator the original code used does
not exist on our reconstruction of a container, the correct source *cannot be
written*. Whoever decompiled the function had to write something that compiles,
and the nearest thing that compiles is usually the opposite operation.

## The instance

`HamCamShot::SetPreFrame` rewinds a multi-shot camera sequence:

```cpp
while (nextOffset < mNextShotOffset && mNextShotIt != mNextShots.begin()) {
    --mNextShotIt;                                   // was: ++mNextShotIt
    mNextShotDuration = (*mNextShotIt)->GetTotalDuration();
    mNextShotOffset -= mNextShotDuration;
    mCurrentShot = *mNextShotIt;
}
```

The body subtracts durations and the loop stops at `begin()`, so the step must
go backwards. `ObjPtrList<T>::iterator` had `operator++` (follows
`Node::next`, `+0x14`) and **no `operator--`**, so the loop was spelled `++`.
The image reads `+0x18`, which is `Node::prev`.

Consequence, on PPC and native alike: seeking a camera backwards advanced to
*later* shots while the offset bookkeeping moved *earlier*; and because the
stop test is `!= begin()`, a forward walk can pass the last node onto the null
end iterator and dereference it.

## Why it survived

* It costs **one** `diff_arg` row — an offset (`0x18` vs `0x14`) on an
  otherwise identical `lwz`. The function sat at 97.34% under a pile of
  unrelated lowering rows.
* The one tool that flagged it read the offset as a **field** name
  (`*mNextShotIt` vs `mCurrentShot`), which was the behaviour-neutral half of
  the diff.
* The source *looked* deliberate: `++it` inside a loop is the most ordinary line
  in C++.

## Recognizer

1. An offset diff on an iterator step whose two offsets are **adjacent links of
   the same node** (`next`/`prev`, `left`/`right`, `head`/`tail`) is a
   direction bug until proven otherwise. Look up the node layout; do not read
   it as a field substitution.
2. Read the loop body. Durations or indices being **subtracted**, or a stop test
   against **`begin()`**, means the walk is backwards.
3. Check whether the container we wrote actually supports the operation. If it
   does not, the decompiled spelling was forced.

## Sweep, 2026-09-30 — the instance is unique, and why

* **Source side:** every `!= X.begin()` test in `src/` outside stlport/xdk —
  9 sites. Six are backward walks and **all six use `--`**; two are
  first-element separator tests; one is `SetPreFrame`. Every other walk is over
  an STL container, which has `operator--`. `SetPreFrame` was the only backward
  walk over an `ObjPtrList`.
* **Image side:** the target's `ObjPtrList` iterator step has a fingerprint —
  `clrrwi rA, rA, 0` then `lwz rB, 0x18(rA)`. Over all 2,223 target listings
  (69,307 functions, 1,124 `clrrwi rA,rA,0` sites) it matched 6 iterator steps,
  3 of them via `prev`: `SetPreFrame` (fixed), `SampleInst360::GetProgress`
  (ours already reads `prev`; the only diff is the no-op `clrrwi`), and
  `ObjPtrList<EventTrigger>::Unlink` (not emitted in our object — the 0%
  placement class, not a wrong walk). ⚠ This fingerprint is **narrow**: a
  backward walk lowered without the `clrrwi` idiom is invisible to it. It is a
  net, not a census.

Other iterators worth knowing about: `ObjRef`'s iterator has `operator--` only
under `HX_NATIVE`; `ObjPtrVec`'s has both directions.

## Adding the operator is free

`obj/Object.h` is PCH-reached, so the edit was measured rather than assumed:
full `ninja`, whole-binary `report.json` comparison — **1 of 48,365** functions
changed, and it was `SetPreFrame`. An unused member of a class template emits
no code.
