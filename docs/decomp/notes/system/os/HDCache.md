# Lane notes: `src/system/os/HDCache.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/os/HDCache.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

File-level (was lines 16-41; nearest following definition: `HDCache::HDCache`):

```text
// w8-g 2026-09-15: ??3CriticalSection@@SAXPAX@Z (24 B) reads 100.0% under the
// normalized ruler with ZERO mismatched instructions, and 98.33% in report.json,
// which charges relocation NAMES.  The whole charge is one string COMDAT: the
// OBJ_MEM_OVERLOAD __FILE__ for CritSec.h.  The image's is
//   ??_C@_0CM@BHOIGAFK@... = "e:\lazer_build_gmc1\system\src\os\CritSec.h"
// and ours is
//   ??_C@_0CM@HACIFEKI@... = "e:\lazer_build_gmc1\system\src\os/CritSec.h"
// -- a FORWARD slash, because MSVC spells __FILE__ as the include search dir
// plus the path exactly as written in the #include that first opened the file.
// This file's own `#include "os\CritSec.h"` is not that include: os/CritSec.h is
// inside the PCH closure (os/Debug.h -> os/ThreadCall.h, which is the one place
// in the tree that spells it with a forward slash), so the PCH decides it for
// all 574 PCH TUs at once.
//
// NOT actionable from here, and the direction matters: across the whole target
// binary the FORWARD-slash literal appears in 5 objects (moviebink/BinkMovieSys,
// synth_xbox/Synth, synth/VorbisReader, utl/MakeString, utl/MemMgr) and the
// BACKSLASH one in exactly ONE -- this object.  So the shipped build opened
// CritSec.h per TU and disagreed with itself; a single PCH cannot reproduce
// that, and changing ThreadCall.h's spelling would fix this 24-byte row and
// break the five that are currently right.
// w21-bj: CLOSED (100) by building this TU without the shared PCH
// (config.pch_excluded_sources in configure.py), so HDCache.h's own
// backslash `#include "os\CritSec.h"` opens it first.  HDCache.h's FileStream.h
// include is spelled with a forward slash to keep that literal on the image's
// spelling.  Whole-binary A/B: UP 2, DOWN 0.
```


## `?Init@HDCache@@QAAXXZ`

`HDCache::Init` -- moved verbatim from `src/system/os/HDCache.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Inside the body (was lines 382-417):

```text
            // w18-d (99.975, 9 lwzx/stwx base<->index operand rows): `i[readFiles]`
            // is byte-inert; indexing mReadArkFiles/mWriteArkFiles directly
            // instead of the two pointer locals costs 94.2.
            // w25-gx (c2rs tap, C2RS-BRIDGE 8.7): the lwzx order is the address
            // add's commutative operand order. Ours sorts the strength-reduced
            // i*4 temp (sid 1140, key 0x11d00) above readFiles/writeFiles (named
            // locals sid 19/18, keys 0x10260/0x10240); the else-loop's temp
            // (sid 1180, 0x12700) also sorts above this->mBlockState (memory
            // leaf 0x12008). Hypothesis: the image has ~108-116 fewer IL symbols
            // before these temps (sid 1024..1032 gives every row the image's
            // order, and keeps mBlockState[i] = ... base-first), or readFiles in
            // the c1 overflow range (past the first 31 locals: 14 extra locals
            // moved it to sid 225) plus >= 28 fewer symbols. Open: which source
            // shape has that symbol count with the same code.
            // w26-n: no behaviour difference here -- lwzx/stwx compute rA+rB, so
            // the base/index swap is the same address, and name_check charges the
            // same 9 rows (the call-name diffs `HDCache::Flush` /
            // `vector<UILabel*>::erase` are ICF aliases at one address).  The
            // base-reference lever was tried: binding `int **blockState =
            // mBlockState;` for the else-loop does re-key that memory leaf, but it
            // also re-populates COLOR and moves `this` from r21 to r20, i.e. 27
            // new rows against 0 repaired.  The same-shape `File **` re-binding is
            // already the source form.
            // w26-f4 (fuzzy lane, 2026-11-02, 99.77444 fuzzy / 100.0
            // normalized): re-read the charged set -- the same 9 rows, r28 the
            // strength-reduced i*4 temp in every one. Re-measured two spellings
            // of the w25-gx lever family, normalized held at 100.0 throughout:
            //   `i[readFiles]` / `i[writeFiles]`: byte-identical, fuzzy
            //     unchanged -- confirms the w18-d claim that source operand
            //     order is not the lever (the key, not the spelling, decides).
            //   `int idx = i;` above the test: 99.17 / 99.50 canon, rejected.
            // Both lanes agree the residual is a symbol-count question: the temp
            // at sid 1140 / 1180 against named locals at 19/18 (w26-n), or by the
            // commutative key 0x10000|(sid<<6 & 0xffff) with sid mod 1024 = 116
            // that has to fall to <= 9 (w26-f4).  Open mechanism, not a floor.
            // Check if read/write files are valid
```

Inside the body (was lines 472-477):

```text
        // The image spells `!skipHdcache` as a full MASK (`subic r11, r11, 1` /
        // `subfe r11, r11, r11` = -(x == 0)), ANDs it with the flag byte and
        // tests that with a separate `clrlwi.`.  w13-d: that is MSVC's lowering
        // of a CONDITIONAL CLEAR -- `if (skip) next = false; if (next)` -- not
        // of `!skip & next` (cntlzw/extrwi, w7-r/w7-y) nor of `&&` (a branch).
        // Same predicate.
```
