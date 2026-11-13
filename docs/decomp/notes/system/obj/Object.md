# Lane notes: `src/system/obj/Object.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?SyncProperty@Object@Hmx@@UAA_NAAVDataNode@@PAVDataArray@@HW4PropOp@@@Z`

`Hmx::Object::SyncProperty` -- moved verbatim from `src/system/obj/Object.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Attribution: definition is macro-generated (BEGIN_PROPSYNCS); symbol matched by the name the macro expands to

Above the definition (was lines 429-436):

```text
// w16-d: the image's function-static guard word $S9 tests bits 0x1 (name),
// 0x4 (type) and 0x8 (sinks) -- bit 0x2 is allocated to a static that is never
// initialized (no code tests it), and the scope ordinals of the three _s
// symbols (?6 / ?BC / ?BN) leave no room for an extra block. An unreachable
// static declared after the name arm's `return true` reproduces exactly that
// (99.985 -> 100, name_check clean); a dead `if (0) { static ... }` block gets
// the bit but shifts the scope ordinals by 5. Behaviour is unchanged: the
// static is unreachable and emits no symbol.
```


## `FindPathNameHelper()`

`FindPathNameHelper` -- moved verbatim from `src/system/obj/Object.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Attribution: no function named `FindPathNameHelper` in report.json (unit); filed under the source name

Above the definition (was lines 596-602):

```text
// w17-d: FindPathName 87.606 -> 100. The four "%s (%s)" arms go through an
// inlined helper: each inline instance gets its own by-value `name` parameter,
// which is why the image re-stores r29 into a fresh MakeString temp per arm
// (0x54 in the Loader arm, 0x50 in the others) and keeps the Loader arm's
// FileLocalize call separate from the shared one. Written inline at the call
// sites, all four arms bound the same `name` home and tail-merged into one.
// rb3-xenon has the same helper marked noinline; here it is inlined.
```


## `?MergeSinks@Object@Hmx@@QAAXPAV12@@Z`

`Hmx::Object::MergeSinks` -- moved verbatim from `src/system/obj/Object.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Inside the body (was lines 1228-1238):

```text
// w21-ay (98.88 -> 100): og-dc3's shape -- one GetOrAddSinks()->AddSink call
// per arm, which MSVC tail-merges at the by-value s7 load (0x825AD690 image's
// join) -- is what fixes the whole register permutation; the array arm's s6
// must be ASSIGNED (`Symbol s6; s6 = ...LiteralSym(1);`) so its value is loaded
// right after the first LiteralSym call as the image does (`mr r11,r3` /
// `lwz r30,0(r11)`).  `Symbol s6 = ...` (constructed in its sret slot) reloads
// from the stack (97.9); passing LiteralSym(1) inline holds the address (98.6).
// Behaviour unchanged: LiteralSym(1) is still evaluated before LiteralSym(0).
// Earlier note (w21-s): the arr3->Size()==0 arm passes a literal `true`
// (li r28,1), and the Size()<=3 arm is the inlined Object::AddSink wrapper
// (two by-value Symbol spills to 0x58).
```
