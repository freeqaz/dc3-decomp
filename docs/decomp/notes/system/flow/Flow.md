# Lane notes: `src/system/flow/Flow.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/flow/Flow.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

File-level (was lines 16-36; nearest following definition: `Flow::Flow`):

```text
// w8-e 2026-09-15: the five 0% rows in this unit are a SINGLE real gap, not an
// ICF or placeholder artifact -- they are the std::vector<Hmx::Object*> growth
// machinery, 636 B total, and orig/373307D9/ham_xbox_r.map contributes every one
// of them from `flow:Flow.obj` itself (`f i`, i.e. a COMDAT of THIS TU):
//   ?_M_insert_overflow@?$vector@PAVObject@Hmx@@...  823f2630  248 B
//   ?push_back@?$vector@PAVObject@Hmx@@...           823f29c0  112 B
//   ?allocate@?$StlNodeAlloc@PAVObject@Hmx@@...      823ed698  112 B
//   ?deallocate@?$StlNodeAlloc@PAVObject@Hmx@@...    823ed708  128 B
//   ??1?$vector@PAVObject@Hmx@@...                   823f25a8   36 B
// So unlike most of the w8-e worklist these ARE emittable from this file; all
// five read 0.0% as of 2026-09-15 because our Flow.cpp never instantiates that
// vector at all -- it holds its Hmx::Object* set in other containers.
//
// w8-k 2026-09-15: the "Refuted" that used to close this note -- that an explicit
// `template class std::vector<Hmx::Object*>;` cannot reach these -- was REASONED,
// not measured, and it is WRONG.  The counter-example was already in the tree:
// src/system/char/Waypoint.cpp:251 carries `template class std::vector<Waypoint *>;`
// and its identically-shaped quartet (allocate/deallocate/_M_insert_overflow 248 B/
// push_back 112 B) all read 100.0%.  An explicit instantiation emitting EXTRA
// COMDATs costs nothing, because report.json enumerates TARGET symbols -- an
// unpaired base-side symbol is not a row.  See the instantiation at end of file.
```


## `??1Flow@@UAA@XZ`

`Flow::~Flow` -- moved verbatim from `src/system/flow/Flow.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 70-75):

```text
        // w14-f: 90.08 -> 100.  The image calls the virtual ProxyFile() through
        // the full object (`lwz r11, 0x68(r29)` / slot 1 / bctrl) and homes r29
        // in a frame slot; a call on `this` in a dtor is devirtualised and
        // inlined to the mProxyFile read.  Same value: Flow does not override it.
        // Declared above the native-only arm so the `else` below binds to the
        // `if`, not to this declaration.
```


## `?Copy@Flow@@UAAXPBVObject@Hmx@@W4CopyType@23@@Z`

`Flow::Copy` -- moved verbatim from `src/system/flow/Flow.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 155-174):

```text
        // w14-f: 98.26 -> 98.79.  Full object named once for the tail (the image's
        // `subi r31, r30, 0x180` at 823F7278 feeding RefreshPortLabelLists and the
        // ProxyFile adjust).  Remaining 4 rows: both SetParent(this, true) calls
        // schedule `subi r4, r30, 0x180` BEFORE `li r5, 1` in the image (823F71C8,
        // 823F722C); ours puts the li first.  Tried: (FlowNode *)this, a block-local
        // `FlowNode *parent = this`, a default `bool = true` param (no change), and
        // a loop-hoisted `Flow *self` (pinned to r27, 95.8 -- worse).
        // w21-p (98.79, same 4 rows): behaviour re-read against the image -- all
        // 18 branches land on the same blocks (function-relative offsets equal ours), the
        // delete loop, both SetParent(this, true) calls, Copy(kCopyDeep=0) and
        // MoveIntoDir(this, c) carry the image's values.  Also byte-identical:
        // SetParent(this, 1); a static inline reparent(child, parent) helper with
        // `true` inside it; the same helper taking the bool as a parameter.
        // w21-ap (98.79, same 4 rows): a `FlowNode *parent = this` local just
        // above the copy loop pins it in a callee-saved GPR (96.3, worse).  A
        // standalone cl.exe probe of the loop (virtual-base Copy override,
        // SetParent through the child's vtable) emits `li r5, 1` before the
        // parent operand for plain `this`, static_cast<FlowQueueable-level *>,
        // a block-local parent/bool pair and a function-scope `self` alike, so
        // no source spelling of the two arguments reorders them.
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: stores
equal under symbolic sim.


## `?PreSave@Flow@@UAAXAAVBinStream@@@Z`

`Flow::PreSave` -- moved verbatim from `src/system/flow/Flow.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 322-326):

```text
// w17-d: 89.613 -> 100. Two levers: the virtual ProxyFile() goes through the
// full object (the image forms this-0x180 then +0x68, `Flow *self = this;`,
// as in w14-f's dtor), and the mode is ONE call with a ternary argument (the
// image loads r4 = kInlineCached before the IsProxy compare and overwrites it
// with kInlineAlways on the fall-through).
```


## `?Enter@Flow@@UAAXXZ`

`Flow::Enter` -- moved verbatim from `src/system/flow/Flow.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 567-571):

```text
    // w14-f: 94.69 -> 100.  The image materialises `q` FIRST (`subi r31, r3,
    // 0x104`) and reaches the ObjectDir subobject from it (`addi r3, r31, 0x68`).
    // Calling ProxyFile() through q -- FlowQueueable sits at offset 0, so the
    // static_cast is a no-op with no null check -- reproduces that; the old
    // (w7-av) `ObjectDir *dir = this;` attempt was null-checked (90.72).
```


## `?ActivateTrigger@Flow@@UAA_NXZ`

`Flow::ActivateTrigger` -- moved verbatim from `src/system/flow/Flow.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 852-857):

```text
// w8-k: the image's flow:Flow.obj carries the whole std::vector<Hmx::Object*>
// growth quartet as COMDATs of THIS TU (allocate 0x823ED698, deallocate
// 0x823ED708, ~vector 0x823F25A8, _M_insert_overflow 0x823F2630, push_back
// 0x823F29C0 -- ham_xbox_r.map, all `flow:Flow.obj`), but nothing in the
// surviving source odr-uses that vector; retail's Flow.cpp did and the call site
// did not survive /OPT:REF.  Same shape and same lever as Waypoint.cpp:251.
```
