# Lane notes: `src/system/obj/DataFunc.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?DataReplaceObject@@YA?AVDataNode@@PAVDataArray@@@Z`

`DataReplaceObject` -- moved verbatim from `src/system/obj/DataFunc.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Above the definition (was lines 1359-1383):

```text
// HISTORY (w7-az, 93.75, 5 rows -- superseded by w21-s below).  Only the third int->bool conversion is
// placed differently.  The image defers it past the `bl Node` for GetObj(1) and
// lands it in r31 -- `subic r11, r30, 0x1` at 0x825B8A70 and
// `subfe r31, r11, r30` at 0x825B8A78 straddle `mr r4, r31` at 0x825B8A74,
// reusing `array`'s register after its last use -- so `mr r7, r31` at
// 0x825B8A8C feeds the call.  We emit the same two instructions two slots
// earlier and overwrite r30 in place.  The first two conversions
// (`subfe r27` at 0x825B89F0, `subfe r29` at 0x825B8A20) match exactly.
// NEGATIVE: declaring setProxyFile as `int` and letting the conversion happen
// implicitly at the call is byte-inert -- MSVC normalises it to the same
// placement.  The declaration order is already confirmed correct: the image
// evaluates the three ternaries 3,4,5 before either GetObj, which only happens
// if they are locals; written inline as call arguments MSVC's right-to-left
// argument evaluation would run them 5,4,3.
// w14-d: also INERT -- `int setProxyFile` passed as `setProxyFile != 0`.
// w18-d (93.75): `int setProxyFile` plus a named `Hmx::Object *to = GetObj(2)`
// is byte-inert.
// w21-s: CLOSED by one more inline level on the object reads.  Reading each
// object as `array->Node(i).Obj<Hmx::Object>(array)` (the DataNode template,
// whose dynamic_cast to the same type folds away, through the NON-const Node --
// the image calls ?Node@DataArray@@QAA... here, never the const QBA one) moves
// the third int->bool conversion past `bl Node` for GetObj(1) into r31, exactly
// as at 0x825B8A70..0x825B8A8C.  `array->Obj<Hmx::Object>(i)` gives the same
// placement but calls the const Node, which this file's image never does.
// Behaviour unchanged: same calls, same arguments.
```
