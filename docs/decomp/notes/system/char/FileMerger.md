# Lane notes: `src/system/char/FileMerger.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Clear@Merger@FileMerger@@QAAX_N@Z`

`FileMerger::Merger::Clear` -- moved verbatim from `src/system/char/FileMerger.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Inside the body (was lines 120-184):

```text
            // w13-c: the loop-unswitched spelling (one `while (!empty())`
            // with `if (mergerDir) ... else pop_back();` inside) is NOT what
            // MSVC unswitches into the image's shape: 93.3.
            // Residual (97.85%, 4 rows, 12 B): the image computes BOTH guards
            // before either branch --
            //   cmplwi r28, 0    (cr0, mergerDir)
            //   cmpwi  cr6, r11, 0 (mLoadedSubdirs.mSize, loaded at 0x54(r29))
            //   beq    -> else     (cr0)
            //   beq    cr6 -> exit
            // so the else arm's zero-trip guard is a bare `beq cr6` reusing the
            // hoisted compare, and the pop_back loop is guard + fall-through
            // do-while.  We emit `cmplwi cr6, r28, 0 / beq cr6 -> else` first
            // and only then compute the size compare, so the else arm has to
            // jump to its own bottom test (`b 0x6f0c`).  REFUTED, both worse
            // (97.85 -> 97.25): spelling this arm as an explicit
            // `while (!empty()) pop_back();`, and as an explicit
            // `if (!empty()) do { pop_back(); } while (!empty());`.  Both gave
            // the else arm its own top-of-block `cmpwi cr6` which MSVC still
            // did not CSE with the if arm's, and additionally sank the
            // `addi r30, r29, 0x50`.  clear() is the best spelling found.
            // w16-c: `while (mLoadedSubdirs.size() != 0)` for the if-arm guard
            // (same expression shape as clear()'s) is byte-inert; inverting
            // to `if (!mergerDir) clear(); else while ...` is 88.4.
            // w21-u (97.85, same 4 rows): `if (!empty()) clear();` here is
            // 95.8 (the else arm grows its own reload + `b`); an inline
            // template helper spelled `while (!l.empty()) l.pop_back();` or as
            // a guarded do/while is byte-inert (MSVC canonicalises the loop the
            // same way as clear()); the single unswitched while with the
            // if/else inside re-measured at 93.3 (not unswitched, r28/r29 swap).
            // w21-aq (97.85, same 4 rows): the image's else arm is clear()
            // inlined in its usual GUARD form (`addi r30,r29,0x50; beq cr6`
            // then pop_back / `lwz 0x4(r30)` / bne) -- every ObjPtrList dtor in
            // the image inlines clear() the same way -- with the guard's
            // compare CSE'd against the if arm's `0x54(r29)` load and hoisted
            // above the mergerDir branch (hence cr0 for mergerDir).  Ours
            // rotates the inlined loop into jump-to-bottom-test instead.
            // Measured: `ObjPtrList<ObjectDir> &subdirs = mLoadedSubdirs;` bound
            // once for both arms is 95.0 (r28/r29 swap); `if (ObjectDir
            // *mergerDir = MergerDir())` is byte-inert; an explicit
            // `while (mLoadedSubdirs.size() != 0) pop_back();` else arm gets
            // the guard back but recomputes its own cmpwi and loads through
            // r29 at the bottom, 97.2.
            // w24-c3 (97.85, same 4 rows): `ObjectDir *mergerDir =
            // MergerDir();` hoisted to function scope above the HX_NATIVE
            // block is byte-inert.  Hypothesis: the image hoists the common
            // first compare of BOTH arms into the MergerDir() join block
            // (head merge), which needs the else arm's inlined clear() in
            // guard form; ours lowers it jump-to-bottom-test, so the two arms
            // share no first instruction.  Open question: what decides
            // MSVC's rotation form for that inlined loop.
            // w26-v (97.85, same 4 rows): re-measured with the cr0/cr6 split
            // in view.  The image's `cmplwi r28, 0` is cr0 while the size test
            // is cr6; cr6 then survives the mergerDir branch and BOTH arm
            // guards (`beq cr6, exit` at 0x1a4 and 0x1f4) consume the one
            // hoisted compare, which is why the else arm falls through into
            // its body instead of rotating.  Ours puts the mergerDir test in
            // cr6 too, so the size compare cannot be hoisted past the branch.
            // All inert (same bytes, 97.7852): `bool haveSubdirs =
            // !mLoadedSubdirs.empty();` bound before the branch and used by
            // both arms; the same with the else arm spelled as a guarded
            // do/while; `ObjPtrList<ObjectDir> &subdirs = mLoadedSubdirs;` used
            // by both arms; `if (mergerDir != 0)`.  Worse: guard-form else
            // arm `if (!empty()) do {...} while (!empty())` 97.07; `while
            // (mLoadedSubdirs.size() != 0)` 97.07; guard wrapping clear()
            // 95.77.  The cr0 choice is not reachable from this TU's source.
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: while vs
do-while loop shape; function-local static Message named lbl_ on target side.
