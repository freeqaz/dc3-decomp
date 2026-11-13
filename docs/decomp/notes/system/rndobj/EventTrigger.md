# Lane notes: `src/system/rndobj/EventTrigger.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?CleanupEventCase@EventTrigger@@IAAXAAV?$list@VSymbol@@V?$StlNodeAlloc@VSymbol@@@stlpmtx_std@@@stlpmtx_std@@@Z`

`EventTrigger::CleanupEventCase` -- moved verbatim from `src/system/rndobj/EventTrigger.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 614-649):

```text
// RESIDUAL (w7-az, 89.66, 14 rows).  The image materialises &*it into its own
// callee-saved register and a dead 4-byte local: 0x82727888 is
// `addi r30, r29, 0x8`, and 0x82727894 / 0x827278AC are two `stw r30, 0x54(r31)`
// that nothing ever reloads, one immediately before each of `bl strstr`
// (0x82727898) and `bl ??0String@@QAA@PBD@Z` (0x827278B0).  That costs the
// target a fifth callee-saved GPR -- `bl __savegprlr_27` at 0x82727864 and
// `b __restgprlr_27` at 0x827278E8, against our _28 -- and lets it address the
// Symbol as `lwz r4, 0x0(r30)` / `stw r11, 0x0(r30)` where we use `0x8(r30)`
// off the node.  Note the FIRST use still goes through the node
// (`lwz r3, 0x8(r29)` at 0x8272788C), so &*it is computed before it is needed.
// NEGATIVES, all byte-inert, none reproduce the 0x54 local: naming the element
// (`Symbol &sym = *it;` and using sym throughout); an explicit pointer local
// (`Symbol *sym = it.operator->();`); and spelling the second access as a
// second `it.operator->()->Str()` so the call expression appears twice.
// w19-a (still 89.66): four more spellings, none emits the 0x54 home stores:
// RB3's `const char *lightStr = strstr(...); if (lightStr)` (inert);
// `(*it).Str()` at both uses (inert); `String str(*it)` (89.71 canonical but
// calls the String(Symbol) ctor where the image calls String(const char *) --
// wrong callee); `*it = Symbol(str.c_str())` (89.63).  The image has only the
// ~String unwind funclet, so 0x54 is not an RAII local.
// w34-a (2026-11-04, behaviour verdict): AGREES, including both apparent offset
// differences.  The image keeps &*it in r30 (node + 8) and addresses the Symbol
// as 0x0(r30); we keep the node in r30 and address it as 0x8(r30).  Those are
// the same byte on both sides, and so are the two assignment stores
// (`stw r11, 0x0(r30)` against `stw r11, 0x8(r30)`).  The image's two extra
// `stw r30, 0x54(r31)` stores are dead: nothing in either listing loads
// 0x54(r31), and neither call receives that slot as an argument, so they are
// home-slot spills of the address temp that the allocator never reloads.  The
// loop bound, the "lighting_" comparison, and the strstr / String / Symbol
// callees all agree.  MEASURED NEGATIVE this lane, all byte-inert at
// 89.65714 / 88.22857: an explicit iterator for-loop; `Symbol &sym = *it;`
// used for both reads and the write; the same reference used for the reads
// only; and `Symbol *sym = it.operator->();`.  Mechanism hypothesis: the dead
// home stores are emitted by the original's front end for a reference-valued
// dereference temp, and nothing in this source spelling asks for that temp, so
// the multiplicity difference has no source operand.
```


## `?Cleanup@EventTrigger@@KA?AVDataNode@@PAVDataArray@@@Z`

`EventTrigger::Cleanup` -- moved verbatim from `src/system/rndobj/EventTrigger.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 704-712):

```text
                // Raw ObjRef* ring walk (w24-pch, 99.738 -> 100).  The ring's
                // end is the ObjRef header itself.  An ObjRef::iterator here --
                // even with the raw-pointer loop test (w13-a, 10 rows -> 1) --
                // homes a dead `stw r10, 0x60(r31)` of the vbase-adjusted
                // &mRefs; with no iterator object there is nothing to home.
                // Kept from w13-a: a `const ObjRef &refs` local is WORSE
                // (97.9), the image recomputes the vbase adjust for &Refs()
                // inside the loop after each RefOwner() call; and an end()
                // loop test reads 98.38 (w19-a).
```
