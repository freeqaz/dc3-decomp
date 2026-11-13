# Lane notes: `src/system/rndobj/Anim.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?FireFlowLabel@RndAnimatable@@IAAXVSymbol@@@Z`

`RndAnimatable::FireFlowLabel` -- moved verbatim from `src/system/rndobj/Anim.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 170-176):

```text
    // w24-pch (99.17 -> 100): a raw ObjRef* ring walk, the way the ObjRef
    // header says refs are iterated (HasDirPtrs).  Every ObjRef::iterator
    // spelling (w13-a, w16-a, w21-x: begin()/end(), raw-pointer test, const-ref
    // operator!=, mRefs direct, split declaration, ...) left one dead
    // `stw r10, 0x50(r31)` homing the vbase-adjusted &mRefs; with no iterator
    // object there is nothing to home.  Same fix in EventTrigger::Cleanup and
    // rndobj ConvertBonesToTranses.
```


## `?Poll@AnimTask@@UAAXM@Z`

`AnimTask::Poll` -- moved verbatim from `src/system/rndobj/Anim.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 443-464):

```text
            // w25-gt (fuzzy 99.899, 6 rows): the image's mScale / mFrameSpan
            // loads in the two snap tests below take f13, ours f0.  regasg
            // first-fits those temps, so f0 must be occupied there in the image.
            // The one spelling that reproduces f13 -- fmod(.., animMax - animMin)
            // in each arm, which keeps animMax a COLOR candidate in f0 live into
            // the arms -- also sinks the subtraction into both arms, where the
            // image computes it once here.  Open: what holds f0 across the snap
            // tests while the range stays a single fsubs (16 spellings tried).
            // w26-f12: the regasg dump confirms f0 is FREE at each of the three
            // loads (t 195 b 26, t 202 b 29, t 205 b 30 all select f0 with an
            // unoccupied f0 at the head of the list), so the image must carry a
            // live temp in f0 across blocks 26..30 that our body does not --
            // consistent with a sched3 order that places the `animRange` fsubs
            // after them, which sched0 then hoists back.  Ten spellings measured
            // with `const float &` bindings on animMin, animMax, mScale and
            // mFrameSpan: none reached the row's own f13 (best 99.636 canonical
            // 99.990, i.e. it changes shape), the rest 98.418-99.158.
            // NOTE the ruler: this TU's per-object build emits `?DI` where the
            // full build emits `?DM` for the `on_anim_event` static's scope
            // ordinal (the target's own spelling is `?DM`), so the per-object
            // path reads 99.832 here against report.json's 99.899.  Full-build
            // numbers are the only ones comparable on this function.
```


## `?OnAnimate@RndAnimatable@@IAA?AVDataNode@@PAVDataArray@@@Z`

`RndAnimatable::OnAnimate` -- moved verbatim from `src/system/rndobj/Anim.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 524-535):

```text
// The offsets in the comments below are the SHIPPED IMAGE's stack slots.
// w24-slot (99.952 -> 100): the slot order of the six 4-byte FindData
// out-locals is MSVC's frame-slot sort -- reference count descending, then
// last reference ascending (see SLOT-RULE in the w24-slot lane notes).
// Declaration order is inert, as w7-j and w16-a measured.  Read off the
// listing, our order was exactly that sort over our references; the image's
// order needed local_units and local_delay to carry ONE MORE reference each.
// That reference is the second TheTaskMgr.Start call: the image's
// trigger_anim_task test is an if / else-if with a Start call in each arm,
// which MSVC tail-merges into one call (identical code), but the second
// call's IL references still count in the slot sort.
// w22-a27: the AnimTask ctor takes local_wrap, not local_wait (see the call).
```

Inside the body (was lines 618-626):

```text
        // w22-a27: the image passes WRAP here, not wait. At 0x8264EED4 the
        // ctor's last argument is `lbz r11, 0x71(r31)` (stored to the stack
        // arg slot 0x67(r1)), and 0x71 is the slot FindData(wrap) filled
        // (`addi r5, r31, 0x71` at 0x8264EC84); wait lives at 0x72
        // (`addi r5, r31, 0x72` at 0x8264EC70) and is read only by the
        // BlendTask/TimeUntilEnd test below (`lbz r10, 0x72(r31)`, 0x8264EFF0).
        // The ctor stores this flag as AnimTask's frame-wrap switch (Poll's
        // fmod-into-the-animatable's-range branch). Passing local_wait made
        // `(wait TRUE)` wrap and left `(wrap ...)` read but ignored.
```
