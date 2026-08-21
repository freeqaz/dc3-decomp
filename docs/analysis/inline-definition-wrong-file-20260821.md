# The inline-definition-in-the-wrong-file family — enumerated and adjudicated, dc3-decomp, 2026-08-21

**Repo: dc3-decomp (title 373307D9).** `../rb3` and `../rb3-xenon` share symbol
names and address ranges with this tree; every symbol, address and number below
is dc3's, measured on `fix/inline-defs-20260821` off `2f666acc8`.

Task #111. The brief named two members — `CharBones::MakeShortAng` and "an
assert-attribution cluster" — and said explicitly that they were a sample. They
were: the family is 18 functions, and two independent oracles find it.

## The two oracles

### 1. The `__FILE__` literal says which file the definition was written in

`MILO_ASSERT`, `MEM_OVERLOAD` and `OBJ_MEM_OVERLOAD` all bake `__FILE__` into a
`??_C@` string COMDAT, and **MSVC spells `__FILE__` exactly the way the file was
reached**. A `.cpp` named on the command line (this build does `cd <dir> && cl …
Foo.cpp`) comes out as a bare `Foo.cpp`; a header found through
`/I 'e:\lazer_build_gmc1\system\src'` comes out as the full
`e:\lazer_build_gmc1\system\src\char\CharBones.h`. So the literal is a direct
readout of **which file the definition lived in** — and, because `#pragma once`
means only the first `#include` to open a header counts, of **how that TU
spelled the include**.

`scripts/analysis/file_literal_census.py` reads every function's `??_C@`
*relocations* out of both the target's split object and ours — never positional
pairing; the string-literal lane of 2026-08-20 manufactured 21 false leads that
way — and compares the multisets. Two details matter:

* The mangled `??_C@` name only carries the **first 32 source characters**, so
  `e:\lazer_build_gmc1\system\src\c` is all the name shows and every header
  under one directory decodes identically. The tool resolves each literal to its
  **full bytes** from wherever the COMDAT is defined, which in the target's split
  is usually a *different* unit from the reference.
* `IMAGE_REL_PPC_PAIR` is not a symbol reference (same trap #112 documented); the
  tool only reads relocations whose target name starts with `??_C@`.

### 2. The shipped map's `f i` flag says whether the definition was `inline`

MSVC's map marks every code symbol `f`, and adds a second flag `i` when the
symbol came from a **pick-any COMDAT** — which is what the compiler emits for an
`inline`, template, or in-class definition. A function defined out-of-line in a
`.cpp` and merely made COMDAT by function-level linking is bare `f`. Five
consecutive lines of `char:CharBones.obj` show the distinction cleanly:

```
823c4aa0 ?ToQuat@ByteQuat@@QBAXAAVQuat@Hmx@@@Z        f i char:CharBones.obj
823c4b30 ?Set@ByteQuat@@QAAXABVQuat@Hmx@@@Z           f i char:CharBones.obj
823c4c48 ?ToShort@ShortVector3@@SAFM@Z                f i char:CharBones.obj
823c4cb0 ?Set@ShortVector3@@QAAXABVVector3@@@Z        f i char:CharBones.obj
823c4d08 ?MakeShortAng@@YAFM@Z                        f i char:CharBones.obj
823c4db0 ?TypeOf@CharBones@@SA?AW4Type@1@VSymbol@@@Z  f   char:CharBones.obj
```

Our side carries the same bit in the **COMDAT selection byte** of each section
symbol's aux record: `IMAGE_COMDAT_SELECT_ANY` (2) for an inline/template
definition, `IMAGE_COMDAT_SELECT_NODUPLICATES` (1) for an out-of-line one. Same
distinction, read from the compiler instead of the linker.
`scripts/analysis/inline_linkage_census.py` joins them.

Whole binary: **697 symbols the image defines `inline` and we define out-of-line;
1,474 the other way.** Only 12,680 bytes of the first group are currently
sub-100%, and most of that is sub-100 for unrelated reasons — the flag itself is
free unless the definition contains a `__FILE__`, or unless moving it changes
which TU emits the COMDAT. **The `f i` flag is a lead, not a work item.** The
five `f i` conversion helpers above (`ByteQuat::Set`, `ShortVector3::ToShort`, …)
are all at 100.0% while defined out-of-line in `CharBones.cpp`, which is the
control that says so.

## The population

18 functions, from the literal oracle over 979 units. (The `f i` oracle's 697 is
a superset that includes every free case.)

| class | rows | outcome |
|---|---:|---|
| target says a header, we say the `.cpp` | 9 | 5 fixed, 2 improved, 2 blocked |
| the class itself is declared in a different header | 2 | fixed |
| target and we differ only in the include separator | 6 | 1 improved, 5 refused |
| build-environment artifact | 1 | refused |

## Fixed — 9 functions, 1,444 bytes of newly-matched code

All numbers are `report.json` `match_percent_normalized` (the `name_check`
ruler), `objdiff-cli 4.2.5`, measured with a full `ninja` against a separately
built merge-base tree. "0 mismatches" means `run_objdiff` reported *all equal*.

| function | unit | before → after | B | mismatches |
|---|---|---|---:|---|
| `?get_free_node@Trie@@QAAIXZ` | `utl/trie` | 99.7619 → **100.0** | 168 | 42/42 equal |
| `?AllocInfoInit@@YAXXZ` | `utl/AllocInfo` | 99.73684 → **100.0** | 152 | 38/38 equal |
| `??2FlowSetProperty@@SAPAXI@Z` | `flow/Flow` | 99.47369 → **100.0** | 76 | 19/19 equal |
| `??3FlowSetProperty@@SAXPAX@Z` | `flow/Flow` | 99.47369 → **100.0** | 76 | — |
| `?UpdateEase@FlowSlider@@IAAXXZ` | `flow/FlowSlider` | 99.72973 → **100.0** | 148 | 37/37 equal |
| `?Initialize@Queue@@QAAXH@Z` | `os/UsbMidiGuitar` | 99.8 → **100.0** | 200 | 50/50 equal |
| `??1Queue@@QAA@XZ` | `os/UsbMidiGuitar` | 99.52381 → **100.0** | 84 | 21/21 equal |
| `??$?6VRhythmDetector@@…ObjPtrVec…` | `char/Character` | 95.50725 → **100.0** | 276 | 69/69 equal |
| `??$?6VRndMat@@…ObjPtrVec…` | `char/CharClipGroup` | 59.348484 → **100.0** | 264 | 66/66 equal |

Five distinct mechanisms, worth separating because they recur:

1. **The definition really was in the header.** `Trie::inc_count`, `dec_count`,
   `inc_dup_count`, `dec_dup_count`, `get_free_node` and `delete_node` are all
   `f i` from `utl:trie.obj` while `store` and `remove` on the next two lines are
   bare `f`. Moved into `trie.h`; `check_index` and `get` were already there,
   which is the control. Only `get_free_node` had a `__FILE__` of its own, and
   fixing it needed **both** halves — the definition in the header *and*
   `trie.cpp` spelling its include `"utl\trie.h"` rather than `"trie.h"`, because
   a bare quoted include resolves out of the compiler's cwd to a basename.
2. **The call site was in a header that got fully inlined away.**
   `AllocInfoInit` is bare `f` from `utl:AllocInfo.obj`, so its body *is* in the
   `.cpp` — yet the `__FILE__` its `MemAlloc` carries is `utl\trie.h` line 0x28.
   The only mechanism that produces that is an allocation written in `trie.h` and
   inlined into `AllocInfoInit`, leaving no symbol of its own anywhere in the map.
   `AllocTrieMemory()` reconstructs it; **the original's name for it is not
   recoverable and the source comment says so.** Line 0x28 places it just above
   `class Trie`, whose `check_index` is 0x36.
3. **A `#line` directive faking the answer.** `FlowSetProperty.h` opened with
   `#line 1 "FlowSetProperty.cpp"`, which is right for `PropertyTask` and wrong
   for `FlowSetProperty` itself. The real shape: `PropertyTask` was declared
   *inside* `FlowSetProperty.cpp`. The evidence is the whole class, not one
   literal — `ham_xbox_r.map` credits every `PropertyTask` symbol (`??3`, `??_G`,
   `?StaticClassName@`, `??_7PropertyTask@@6B@`, all four `??_R*` records,
   `ObjPtr<PropertyTask>`, `ObjRefConcrete<PropertyTask>`) to
   `flow:FlowSetProperty.obj` and nothing else, and nothing outside that `.cpp`
   names the type. Class moved, `#line` deleted, and all 22 sibling
   `??2Flow*@@SAPAXI@Z` in the same object still agree.
4. **Right content, wrong header.** `struct MidiMessage` was in `midi/Midi.h`;
   the image spells its `MEM_ARRAY_OVERLOAD`'s `__FILE__` `os\UsbMidiGuitar.h`,
   and the map credits the `"MidiMessage"` class-name literal the same macro
   emits to `os:UsbMidiGuitar.obj`. The type has exactly one user binary-wide.
5. **A hand-written copy where the image compiled the template.** Both
   instantiations of `operator<<(BinStream&, const ObjPtrVec<T,ObjectDir>&)` were
   `.cpp`-local copies; the image attributes both to `obj\ObjPtr_p.h`.
   CharClipGroup already *knew* — it faked the string with `#line 391
   "…obj\ObjPtr_p.h"` over a body that measured 59.3%. Replaced with **explicit
   instantiations**, and the generic template needed one change to *be* the
   image's body: letting the iterator dereference convert to a pointer makes MSVC
   strength-reduce the induction variable to `&node->mObject` and re-derive the
   node with a `subi` to compare against `end()`, and test it signed; reading
   `it->Obj()` into a named `T1*` keeps the Node in the induction register, loads
   `0xc(node)` and tests unsigned — the target's `mr` / `lwz 0xc` / `cmplwi`.

## Improved but not closed — 3 functions

Each had its file literal corrected; each has an unrelated residual, stated so
nobody re-opens it as a literal problem.

| function | before → after | B | why it does not reach 100 |
|---|---|---:|---|
| `?CalculateFaderVolume@ThreeDSound@@AAAXXZ` | 89.347824 → 89.45652 | 368 | control flow: the target tests both `-96.0f` early exits with the opposite polarity (`blt`/`bgt` vs our `bge`/`ble`) and orders the constant loads the other way — 9 instructions in 4 insert/delete clusters |
| `?LoadNewSong@Game@@QAAXVSymbol@@0@Z` | 99.276726 → 99.33962 | 636 | a −0x10 frame delta, two DIFFER stack slots and a missing `stw r28, 0x54(r31)` (`mGameInput` vs `mMaster`) |
| `?PackVector@@YAXAAIABVVector4@@EEEE_N@Z` | 96.111115 → 96.190475 | 504 | register allocation: 13 instructions across three swap pairs plus a bool-mask shape |

`FlowSlider::UpdateEase` and `ThreeDSound::CalculateFaderVolume` are **two more
`GetEaseFunction` call sites the image expands inline** — `math/Easing.h` already
documented that behaviour for `PropertyTask`'s and `AnimTask`'s ctors, and that
list was two short. Both had the assert and the `gEaseFuncs[]` load open-coded in
the `.cpp`.

`PackVector` is worth its own note. The map lists
`?PackVector@@YAXAAIABVVector4@@EEEE_N@Z` at **two** addresses — `826202e0` from
`rnddx9:Mesh.obj` and `8263a168` from `rndobj:Mesh.obj`, both bare `f` — which
only internal linkage produces, and both copies carry
`rndobj/MeshVertCompress.h`. The two shipped bodies are identical word-for-word
except for five branch displacements and each TU's own copy of the file string,
**which is also why `/OPT:ICF` could not fold them**: those string COMDATs sit at
different addresses, so the bytes are not equal. That proves one `static`
definition in the header. We had two hand-written, independently drifted copies.
Both were tried in the header: `rndobj`'s measures 45.3% against the rnddx9 copy,
`rnddx9`'s measures 96.2%. The rndobj body had **never been measured against
anything** — its row is `fn_8263A168` and reads 0% because dtk cannot name a
second symbol with the same mangling in another unit — so it was drift, not
evidence. And the separator is per-TU here too: `826202e0` carries a forward
slash, `8263a168` a backslash.

## Refused, with the arithmetic

### The image is inconsistent about include separators, so three of them cannot be satisfied

The target's string pool contains **both** spellings of four headers. Per-TU,
because `#pragma once` means only the first opener counts:

| header | image wants `/` at | image wants `\` at | ours | verdict |
|---|---|---|---|---|
| `os/CritSec.h` | BinkMovieSys ×2, VorbisReader, Synth, MakeString, MemMgr (6) | HDCache ×2 | `/` | **refuse the flip** |
| `utl/FileStream.h` | SkeletonClip, MidiReader, HDCache, Bitmap ×2, HiResScreen, WavReader (7) | FileStream ×1 | `/` | **refuse the flip** |
| `utl/PoolAlloc.h` | MultiMesh ×2 | PoolAlloc ×4 | `\` | **refuse the flip** |
| `utl/TempoMap.h` | Game ×1 | MidiReader ×3, MultiTempoTempoMap, TempoMap (5) | `\` | **fixed per-TU** |

All three refused headers are **inside the PCH** (`decomp_pch.h` is
`obj/Object.h` + `os/Debug.h`, closure 178 headers), and `HDCache.cpp`,
`FileStream.cpp`, `MultiMesh.cpp` and `PoolAlloc.cpp` all compile with
`/Yu"decomp_pch.h" /FI"decomp_pch.h"`. The PCH opens each header once, with one
spelling, for all 574 PCH TUs — so we can supply exactly one spelling where the
image has two, and the majority is already the one we supply. Flipping costs
more than it gains, every time:

* `utl/PoolAlloc.h`: would gain MultiMesh's `??3FixedSizeAlloc` (24 B) and
  `??_GFixedSizeAlloc` (88 B), and lose `utl/PoolAlloc`'s four rows that are at
  100.0 today — `??3ChunkAllocator` 24, `??0ChunkAllocator` 132, `?PoolAlloc@@`
  336, `??_GReclaimableAlloc` 96. Net **−476 B**.
* `os/CritSec.h`: would gain `??3CriticalSection` (24 B; `HDCache::Init` is at
  97.4 and would not cross), and lose `??_GCriticalSection` (96) +
  `BinkMovieSys::Init` (504) + `VorbisReader::??_G` (96) + `InitMakeString`
  (248), all at 100.0 today. Net **≤ −920 B**.
* `utl/FileStream.h`: would gain `??_GFileStream` (96 B) and lose
  `SkeletonClip::StopRecordingNoClear` (300), `RndBitmap::LoadBmp` (248),
  `RndBitmap::SaveBmp` (148) and `HiResScreen::Finish` (676). Net **≤ −1,276 B**.

`utl/TempoMap.h` is the exception only because `Game.cpp` is one of the 382
**non-PCH** TUs, so it can open the header itself, first, with a slash — measured
after the change, the Game unit's 5 `TempoMap.h` references use the slash form
and the other 9 across three units still use the backslash form.

### `MakeShortAng` — the header move is impossible, and the reason is recorded in the source

Everything says `?MakeShortAng@@YAFM@Z` was written `inline` in
`char/CharBones.h`: the map flags it `f i` from `char:CharBones.obj`, and its
assert's `__FILE__` is the absolute header path. The move was made and
**reverted**. Nothing in our `CharBones.cpp` calls `MakeShortAng`, MSVC does not
emit an inline function the TU never references, so `CharBones.obj` stopped
defining the symbol and the row went **99.7619% → 0%** (42/42 insert, "Stub") —
a 168-byte regression traded for 0.238%.

The missing piece is a **caller in `CharBones.cpp`**, and the image does not
contain one: the assert's condition literal `??_C@_0BI@ILEBKJMM@`
(`f < 32768 && f > -32767`) is referenced from exactly one address in the whole
binary, `823C4D58`, inside `MakeShortAng` itself — so it was never inlined into a
surviving `CharBones.cpp` function either — and the only `bl` reaching
`823C4D08` anywhere is the pair in `CharBonesSamples::Relativize`. Whatever
referenced it in the original `CharBones.cpp` did not survive `/OPT:REF`, which
is consistent with the map still crediting `CharBones.obj` with the kept copy.
Checked and rejected as that caller: `CharBones::RotateBy` (92.29%) and
`RotateTo` (93.40%) are the unit's other sub-100 rows and their frames are 32
bytes *smaller* than the target's, which looks like an inlined assert — but
neither target body references the condition string or branches to `823C4D08`.
Their residual is register allocation.

### `XMemAlloc` — a build-environment artifact, not a source fact

`default/Memory_Xbox` (396 B, 93.6%). The image's `__FILE__` is
`..\..\..\system\src\os\Memory_Xbox.cpp` — a *relative* path with three parent
steps, i.e. the original compiled that one file from a different working
directory. Our ninja rule does `cd <the .cpp's own dir> && cl … Memory_Xbox.cpp`,
which is what produces the bare basename that every other `.cpp` in the image
also has. Reproducing it means changing the build's cwd for one TU, not changing
source; and the row is 93.6% for unrelated reasons, so it would not cross.

## One thing found and deliberately NOT acted on

Extending the census to "code symbols the target's object defines and ours does
not, scored 0%" finds **901 rows / 167,676 bytes**:

```
other (ordinary unimplemented functions, incl. all of xdk/)   467
template instantiation (??$…)                                 153
member of a class template (?…@?$…)                           152
deleting destructor (??_E / ??_G)                             102
fn_ placeholder                                                21
merged_ placeholder                                             6
```

A subset of these are the *same* mechanism as the two `ObjPtrVec` rows fixed
above: our source has a correct template, our TU never instantiates it because
the call site is missing, and MSVC emits nothing. `??$?6VRndAnimatable@@…ObjPtrList…` in `char/CharWeightSetter` (236 B, 0%) is the
clean example — the *same* generic template is already at 100.0 in
`ui/UIFontImporter`, so the body is known good and an explicit instantiation
would almost certainly land the row.

**Not done, on purpose.** In the two cases fixed above a hand-written
placeholder body already existed in the `.cpp` for exactly that purpose, so
replacing it with an explicit instantiation was strictly an improvement. Adding a
*new* instantiation to a TU that references nothing of the sort is a different
act: it moves the metric without the missing call site being reconstructed, and
whether that is acceptable is a project policy call, not a lane decision. The
enumeration and its denominator are recorded here so the decision can be made on
numbers. Note also that this census **overlaps** the concurrent task-#114
ICF-fold-pairing work, which recovers 0% rows by *renaming*; the two populations
must be intersected before either is treated as a total.

## Reproduce

```bash
python3 scripts/analysis/file_literal_census.py --json /tmp/fileloc.json
python3 scripts/analysis/inline_linkage_census.py --json /tmp/inline_linkage.json
```

Both are read-only, take no `--db`, and run against already-built objects.
