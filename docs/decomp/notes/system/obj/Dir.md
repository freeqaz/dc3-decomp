# Lane notes: `src/system/obj/Dir.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Save@ObjectDir@@UAAXAAVBinStream@@@Z`

`ObjectDir::Save` -- moved verbatim from `src/system/obj/Dir.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Above the definition (was lines 411-415):

```text
// w14-d (99.997, 2 rows): the only residual is two stores in the inlined
// vector swap at the end (image stores the 0x58 swap temp before the 0xa4
// finish pointer, we after).  `unused.swap(mInlinedDirs)` is byte-inert.
// w17-d (99.997, same 2 rows): moving `gLoadingProxyFromDisk = oldProxy` above
// the vector/swap is WORSE (99.3: the gLoading store moves into the swap).
```

Inside the body (was lines 539-551):

```text
    // w26-n2: the last charged row in this function is a STORE ORDER inside
    // this inlined vector::swap.  c1 materialises the address of each swapped
    // member into a dead temp slot ($T71879 / $T71902, both at 0x58); the image
    // emits the two dead address stores adjacently (`... &_M_finish`, `...
    // &_M_end_of_storage`) and only then the live `unused._M_finish = <old>` at
    // 0xa4, while we interleave that live store between them.  Every store's
    // effective address is identical on both sides, so nothing here is
    // observable.  Measured inert at source level (all byte-identical):
    // `unused.swap(mInlinedDirs)`, `std::swap(mInlinedDirs, unused)`, and a
    // trailing `unused.clear()`; the drop-in-replacement forms
    // (`mInlinedDirs = std::vector<InlinedDir>();` 95.34,
    // `std::vector<InlinedDir>().swap(mInlinedDirs)` 97.90) are worse because
    // they change which member expressions are materialised at all.
```


## `?ResetViewports@ObjectDir@@QAAXXZ`

`ObjectDir::ResetViewports` -- moved verbatim from `src/system/obj/Dir.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Above the definition (was lines 768-772):

```text
// w17-d (98.647, 11 rows): the image keeps -768 itself in f30 across
// MakeRotMatrix and uses fmadds (m.y * -768); we fold the negative literal into
// +768 / fmsubs. Tried: og-dc3's `Multiply(Vector3(0,-768,0), m, v)` (90.6, the
// inline Multiply drops the *0 terms), `Vector3 v; v.Set(...)` (inert), copying
// v from vp[5].mXfm.v before the call (82.6, spills to the stack).
```

Inside the body (was lines 791-805):

```text
    // w21-l: the image keeps -768 (__real@c4400000) in f30 and does
    // fmadds m.y*(-768); we folded the sign into fmsubs with +768, so the wrong
    // one of the two constants was the one live across MakeRotMatrix (11 rows:
    // 3 fmsubs-vs-fmadds + 8 f12/f30 swaps).
    // RESOLVED (w27-b, 98.35 -> 100.0): a statement boundary blocks the c1
    // rewrite.  Written as one expression, c1 rewrites `m.z*v.z + m.y*v.y` into
    // `m.z*v.z - m.y*768` BEFORE contraction runs, so no arrangement of the
    // expression itself keeps the negative literal -- six spellings were measured
    // byte-identical (int and float literals, v.x*m vs m*v, (y+z)+x vs x+(y+z), a
    // named Vector3, `-(m.y.x*768)`, `m.y.x*-768`, `x*0 - (y*768 - z*0)`).
    // Binding each column's products to named locals keeps v.y as a multiply
    // operand, so -768 is what survives the call and takes f30, and c2 then
    // contracts to fmadds.  Naming only the m.y column fixes the constant but
    // leaves the wrong accumulator base (m.z*0.0 first) and stops at 99.73;
    // naming both columns finishes the row.
```


## `?RemoveSubDir@ObjectDir@@QAAXABV?$ObjDirPtr@VObjectDir@@@@@Z`

`ObjectDir::RemoveSubDir` -- moved verbatim from `src/system/obj/Dir.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Inside the body (was lines 1347-1363):

```text
            // Residual row [159]: the target has a dead `stw r11, 0x58(r31)`
            // here -- the alloc result spilled into the local-temp slot that
            // `FilePath fp` and the ASSERT_REVS `ClassName()` return temp also
            // use. It is never reloaded (the terminator below re-reads the
            // member), and it is the ONLY charged row left in this function;
            // base is 3884 B against the target's 3888 B, exactly that one
            // instruction. Refuted levers: a named local for the alloc result
            // (inert -- MSVC coalesces it with the member); rb3-xenon's
            // two-local spelling `char *ptr = (char *)mAlwaysInlineHash;`
            // (inert on [159] and it flips the stbx operand order at [163]).
            // w-homeslot (2026-11-13): the assignment as Read's argument,
            // `bs.Read((void *)(mAlwaysInlineHash = (char *)MemOrPoolAlloc(..)),
            // hashLen)`, is byte-inert (still only [159]); a local `hash` used
            // for the terminator after Read (`hash[hashLen] = 0`) keeps it in a
            // callee-saved register -- 85 rows, r19/r20 rotate.  The image
            // reloads the MEMBER after Read, so nothing after the call reads
            // the stored copy.
```

Inside the body (was lines 1487-1494):

```text
                // The target materialises the element count into a register and
                // tests it (`srawi. r9, r9, 2`); writing the test directly
                // against `intVec.size()` lets MSVC apply its
                // `x/4 != 0 <=> x & ~3 != 0` peephole instead (`clrrwi.`),
                // because the count is dead after the branch. Naming the count
                // keeps it materialised.
                // w16-d (99.897): the r9/r10 swap on the end/begin loads at
                // [532-538] survives `intVec.end() - intVec.begin()` (inert).
```

## `operator<<()`

`operator<<` -- moved verbatim from `src/system/obj/Dir.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

Attribution: ambiguous: 183 overloads of `operator<<` fit equally (e.g. `??$?6V?$ObjDirPtr@VObjectDir@@@@V?$StlNodeAlloc@V?$ObjDirPtr@VObjectDir@@@@@stlpmtx_std@@@@YAAAVBinStream@@AAV0@ABV?$vector@V?$ObjDirPtr@VObjectDir@@@@V?$StlNodeAlloc@V?$ObjDirPtr@VObjectDir@@@@@stlpmtx_std@@@stlpmtx_std@@@Z`, `??$?6VUILabelDir@@@@YAAAVBinStream@@AAV0@ABV?$ObjDirPtr@VUILabelDir@@@@@Z`, `??$?6VString@@V?$StlNodeAlloc@VString@@@stlpmtx_std@@@@YAAAVBinStream@@AAV0@ABV?$vector@VString@@V?$StlNodeAlloc@VString@@@stlpmtx_std@@@stlpmtx_std@@@Z`, `??$?6EV?$StlNodeAlloc@E@stlpmtx_std@@@@YAAAVBinStream@@AAV0@ABV?$vector@EV?$StlNodeAlloc@E@stlpmtx_std@@@stlpmtx_std@@@Z`); filed under the source name

Above the definition (was lines 266-284):

```text
// w8-j 2026-09-15: this used to be a DECLARATION with no definition anywhere in a
// header, so no TU ever emitted the COMDAT and every ObjDirPtr<T> stream-out was
// an undefined external resolved from link_glue.cpp.  The body link_glue carries
// is already byte-exact -- all 36 instructions equal the target at 0x82793CA8 --
// but it is in the WRONG TRANSLATION UNIT: ham_xbox_r.map contributes that COMDAT
// from ui:UILabel.obj, and objdiff scores link_glue's copy as 36 `delete`s
// against a 0-byte target while UILabel's row reads 0.0% with a 144-byte target
// and a 0-byte base.  Defining the primary template here lets each using TU emit
// its own COMDAT, which is what the image does.
//
// A saved ObjDirPtr is a PATH the loader can reopen, not a name; GetFile() above
// is the mObject->Loader()->LoaderFile() / mLoader->LoaderFile() /
// mObject->StoredFile() / FilePath::sNull chain the target inlines here, and
// FilePath's own BinStream operator<< (utl/FilePath.h:32) supplies the
// FileRelativePath(FilePath::Root().c_str(), ...) the listing ends with.
//
// This body is now compiled natively too. The native build used to write
// dir->Name() instead, so a native save/load round trip handed the reader
// (operator>> below: FilePath, then LoadFile) a bare object name.
```
