# Lane notes: `src/system/world/Instance.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?DeleteTransientObjects@WorldInstance@@AAAXXZ`

`WorldInstance::DeleteTransientObjects` -- moved verbatim from `src/system/world/Instance.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 245-275):

```text
// w19-b (99.753 canonical): 13 rows, all register numbering of the seven
// loop-invariant addresses MSVC hoists above the ObjDirItr loop (target
// kAssertStr=r25 ObjRef-vtable=r24 TheDebug=r28 file=r27 msg=r26; ours
// r24/r28/r27/r26/r25) plus their uses.  The two call-name rows
// (ObjDirPtr<ObjectDir>::operator-> and MakeString<char[19],int,char[5]>) are
// ICF fold aliases at 0x82440E08 / 0x824D1870, not source differences.  Worse:
// `ObjRef refs;` declared above the Find (97.5).
// w21-av (stopped at 99.753, same 13 rows): the image also hoists the two
// RTTI addresses ABOVE the assert's file/cond strings (lis order vtable,
// TheDebug, RTTI Object, RTTI RndMesh, file, cond, kAssertStr; ours puts
// file/cond before the RTTI pair), so the ObjRef vtable lands in r24 not
// r28. Inert: `dynamic_cast<...>(...) == nullptr`, MILO_ASSERT_IF (lever o).
// Worse: binding `Hmx::Object *o = obj;` for the loop body (98.8).
// w26-h: 99.75 canonical, 17 charged rows, none behavioural.
//   * two CALL-NAME rows are link-time folds, not wrong callees: the target
//     names `ObjDirPtr<ObjectDir>::operator ObjectDir*` where we name the
//     WorldInstance instantiation, and both are at 0x82440E08 in the map
//     (icf_aliases.map); likewise the assert's `MakeString<...>` row is one
//     folded instantiation named from one TU's __FILE__ length.  The argument
//     strings referenced by the instructions are identical on both sides.
//   * the other 15 rows are the ROTATION of the seven hoisted address
//     constants around the ObjRef/vtable/assert block.  The c2rs tap shows
//     they are per-symbol candidates popping in descending-id order
//     (arena ids 22 vtable, 23 TheDebug, 26 __FILE__, 27 the cond string,
//     28/29 the two RTTI blobs, plus code-band id 10 for kAssertStr); our pop
//     order is the id order, the image's moves ONLY the ObjRef vtable from
//     first to after kAssertStr.  That is a smaller id or a lower priority for
//     that one symbol -- the ObjRef vtable is stored twice (both `ObjRef refs;`
//     constructions), and moving either the arena ordinal or that ref count is
//     an unexplored lever; declaration/statement reorder inside the loop was
//     tried in earlier waves and is inert.
```


## `?SyncDir@WorldInstance@@AAAXXZ`

`WorldInstance::SyncDir` -- moved verbatim from `src/system/world/Instance.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 352-379):

```text
                // Residual (99.4275 canonical, 5 rows, all here). The image spends
                // ONE extra instruction we do not: `addi r11, r3, 0x0` right after
                // `bl __RTDynamicCast`, i.e. it materialises the cast result into a
                // scratch pointer and runs the bool conversion off that copy
                // (`subic r10, r11, 1 / subfe r27, r10, r11`), where we run it
                // straight off r3. That one extra instruction is also what moves
                // `cmplwi cr6, r16, 0x0` (the `!grp` test) ahead of the subic/subfe
                // pair, which is the delete/insert row pair -- one cause, five rows.
                // Refuted spellings (measured, all bit-identical to this one):
                //   RndMesh *p = dynamic_cast<...>(&*it); bool curMesh = p != nullptr;
                //   dynamic_cast<RndMesh *>(it.Ptr()) != nullptr
                //   dynamic_cast<RndMesh *>(it)  -- does not compile (C2682,
                //     ObjDirItr is a class; the cast needs &*it or .Ptr()).
                //   RndMesh *p declared OUTSIDE the loop, assigned here, then
                //     `bool curMesh = p != nullptr;` -- lengthening the pointer's
                //     live range past the loop body does NOT buy the `mr`
                //     (lane w7-aa, 2026-09-14; also bit-identical).
                // Also checked: the 7 `ObjDirPtr<ObjectDir>::operator->` vs
                // `ObjDirPtr<WorldInstance>::operator->` rows in the Function Call
                // Diff are ICF folds -- the name_check ruler charges 0 of them.
                // w13-c: `(RndDrawable *)dynamic_cast<RndMesh *>(&*it)` (an
                // upcast to the offset-0 base) is also bit-identical here.
                // w21-ae: CLOSED (99.43 -> 100, 524/524 rows): the comparison is
                // written null-FIRST, `nullptr != dynamic_cast<...>`.  With the
                // constant on the left MSVC copies the call result
                // (`addi r11, r3, 0x0`) before the subic/subfe, exactly as the
                // image does; found with a standalone cl.exe probe (`0 != p`
                // emits the addi, `p != 0` / plain bool conversion do not).
```
