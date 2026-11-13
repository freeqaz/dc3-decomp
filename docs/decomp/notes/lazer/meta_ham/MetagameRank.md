# Lane notes: `src/lazer/meta_ham/MetagameRank.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/lazer/meta_ham/MetagameRank.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

File-level (was lines 58-84; nearest following definition: `BuildUnlockablesList`):

```text
    // AT LIMIT, MEASUREMENT ARTIFACT, NOT A SOURCE DEFECT (w9-c 2026-09-30).
    // `gTiers`'s inner type drags in the copy constructor
    // `vector<Unlockable *, StlNodeAlloc<Unlockable *> >::vector(const vector &)`,
    // the ONLY row in this unit below 100%: report.json reads 99.8214 canonical
    // (99.821 fuzzy) while objdiff under the normalized ruler reads 100.0 with
    // ZERO mismatch rows -- all 28 instructions of all 112 bytes are equal.  The
    // entire 0.179 is one RELOCATION NAME at index 8: the image's callee is dtk's
    // synthetic ICF representative `OnlyReturns`, ours is
    // `?get_allocator@?$vector@PAUUnlockable@?A0xf8e4b4b5@@V?$StlNodeAlloc@...@XZ`.
    //
    // The shipped MSVC linker map settles it.  orig/373307D9/ham_xbox_r.map lists
    // OUR EXACT SYMBOL --
    //   0005:000b3b70  ?get_allocator@?$vector@PAUUnlockable@?A0xf8e4b4b5@@...@XZ
    //                  823e3b70  f i meta_ham:MetagameRank.obj
    // -- at 823e3b70, and 769 other names share that address: the /OPT:ICF fold
    // class whose survivor dtk names `OnlyReturns`.  So the callee is right, the
    // name is the linker's own, and no source edit can change it.
    //
    // Why the alias map does not absolve it: build/373307D9/icf_aliases.map has an
    // `OnlyReturns@0x823e3b70` group, but our get_allocator is not a member, and
    // `scripts/retail_map_fold_reconcile.py` (dry run, this tree) does not offer
    // it -- the 823e3b70 group is dropped in its census, never reaching the
    // byte-identity gate, so the name is never admitted.  scripts/symbol_aliases.json
    // says "Do not hand-edit", and widening a 769-member fold class by hand to buy
    // one row would be laundering a measurement, so this row is left charged.
    // Treat `lazer/meta_ham/MetagameRank` as complete in substance: 98 functions,
    // 97 at 100% and the 98th byte-identical modulo an ICF survivor's name.
```

File-level (was lines 1195-1202; no definition follows it):

```text
// w8-j 2026-09-15 -- UNSCOREABLE at 99.824% for the vector copy constructor
// ??0?$vector@PAUUnlockable@?A0xf8e4b4b5@@V?$StlNodeAlloc@...@@QAA@ABV01@@Z
// (112 B).  All 28 of 28 instructions are EQUAL; the entire 0.176 is a
// RELOCATION-NAME charge under the graded name_check ruler, not a code
// difference.  The image spells one callee `OnlyReturns` where we emit
// ?get_allocator@...; both bodies are the same two instructions, so this is an
// ICF fold that build/373307D9/icf_aliases.map has not grouped.  There is no
// source change that can move it -- the code is already byte-exact.
```
