# Lane notes: `src/system/os/PlatformMgr_Xbox.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/os/PlatformMgr_Xbox.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

File-level (was lines 16-24; nearest following definition: `DefaultXShowCallback`):

```text
// w8-e 2026-09-15: the two 0% rows here (360 B) are a real, same-TU gap, not an
// artifact.  ham_xbox_r.map contributes both from `os:PlatformMgr_Xbox.obj`:
//   ?_M_insert_overflow@?$vector@_KV?$StlNodeAlloc@_K@...  825d8900  248 B
//   ?push_back@?$vector@_KV?$StlNodeAlloc@_K@...           825d8e80  112 B
// `_K` is `unsigned __int64`, so the image grows a std::vector<u64> in this
// translation unit -- almost certainly a list of XUIDs / offer IDs.  Our
// PlatformMgr_Xbox.cpp has no vector<u64> at all, so neither COMDAT is emitted
// and both read 0.0%.  Closing them needs the real member found and used on its
// growth path; an explicit instantiation alone emits the wrong member set.
```

File-level (was lines 1622-1628; no definition follows it):

```text
// w8-k: same lever as Waypoint.cpp:251 / Flow.cpp.  ham_xbox_r.map contributes
// ?_M_insert_overflow@?$vector@_K... (0x825D8900, 248 B) and
// ?push_back@?$vector@_K... (0x825D8E80, 112 B) from os:PlatformMgr_Xbox.obj.
// The rest of the vector<u64> member set (allocate/deallocate/_Vector_base ctor/
// copy ctor/dtor) already reads 100% here off the by-value copy in
// MultipleItemsEnumJob; only the growth path was missing, because every
// vector<u64> this TU still names is taken by reference and never pushed to.
```


## `??0PlatformMgr@@QAA@XZ`

`PlatformMgr::PlatformMgr` -- moved verbatim from `src/system/os/PlatformMgr_Xbox.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Above the definition (was lines 147-153):

```text
// w14-d (95.93, 13 rows): store scheduling of the anonymous-namespace globals
// only -- the image stores mServiceIDOverlapped first, we last.  INERT: moving
// that assignment above the mXuidCache loop.  WORSE (88.24): moving it after
// mServiceIdState.
// w15-i2: INERT as well -- declaring mServiceIDOverlapped2 above
// mServiceIDOverlapped (the map's .bss order: 0x82F6763C / 0x82F67640), and
// marking Friend::Friend `inline` (map `f i`; Poll's caller unchanged).
```

Inside the body (was lines 174-178):

```text
    // Store scheduling (w27-b): when a run of independent stores to anon-namespace
    // globals is emitted, MSVC defers the FIRST one of the run to the end of it.
    // The image's order is Overlapped, Overlapped2, StorageList, PathLen,
    // ServiceIdState, so ServiceIdState is the one written first here.  No
    // aliasing and no call between them, so the order is unobservable.
```


## `?RegionInit@PlatformMgr@@QAAXXZ`

`PlatformMgr::RegionInit` -- moved verbatim from `src/system/os/PlatformMgr_Xbox.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Above the definition (was lines 540-549):

```text
// w8-g: 88.24% (normalized, full ninja). Residue is where `mr r3, r31` is placed
// relative to the branch join. REFUTED: collapsing the if/else into a single call
// `SetRegion(XGetGameRegion() != 0xFF ? kRegionEurope : kRegionNA);` -- which should
// force `this` to be set up after the join -- measured 55.29%, a 32.9pp regression.
// The two-call if/else is the better spelling. Reverted.
// Also refuted (w12-d): `region = kRegionEurope; if (== 0xFF) region = kRegionNA;
// SetRegion(region)` 70.4; an if/else assigning `region` 55.3 (branchless);
// the inverted two-call if/else (`== 0xFF` first) 87.8.
// w18-d (88.24): a `switch (XGetGameRegion()) { case 0xFF: ... default: ... }`
// is byte-identical to the if/else.
```

```text
lane E w-low-synth-os (2026-11-13), behaviour audit against the target listing, value by value: NOT A BUG
(88.24).  Full 32-bit XGetGameRegion() == 0xFF selects kRegionNA (1), anything
else kRegionEurope (2), then one SetRegion call.  Only the `mr r3, r31`
placement differs (before the li/bne in ours, after the join in the image).
```

## `?JsonToDta@?A0x8a9ffbf2@@YA?AVDataArrayPtr@@PAUHJSONREADER__@@_N@Z`

`JsonToDta` -- moved verbatim from `src/system/os/PlatformMgr_Xbox.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Inside the body (was lines 687-697):

```text
            // w14-d: the array/map results reach `node` by IMPLICIT conversion
            // (DataNode(const DataArrayPtr &)).  That is what makes the image
            // re-address the temp for operator= (`addi r4, r31, 0xa0` @825D70F0,
            // `addi r4, r31, 0xb8` @825D7134) where an explicit `DataNode(...)`
            // temp reuses the ctor's returned `this` (`mr r4, r3`), and it also
            // closes the 0x10 frame delta.  98.32 -> 99.07 together with the
            // fieldName conversion below and `node(kDataInt, 0)`.  RESIDUAL
            // (8 rows): before the jump table the image re-loads only node's
            // value (`lwz r3, 0x50`) and its shared `continue` dtor at 0x369c
            // re-reads mType; we also cache mType in r10 and enter one
            // instruction later.
```


## `?XbcCallback@?A0x8a9ffbf2@@YAXJPAU_XBC_EVENT_PARAMS@@PAX@Z`

`XbcCallback` -- moved verbatim from `src/system/os/PlatformMgr_Xbox.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Above the definition (was lines 766-778):

```text
    // The two error arms must be written as early returns, not as an
    // if / else-if chain: MSVC then lays the merged TheDebug::Notify tail
    // immediately after the FIRST arm (825??? -- `bl MakeString` / `lis
    // TheDebug@ha` / `bl Notify` / `b end`, with the second arm branching
    // back up to it), which is what the image does.  The chained form puts
    // the tail after the SECOND arm instead and costs 8 rows.
    // RESIDUAL (w7-am, 100.0 canonical, all 94 equal): the image instantiates
    // MakeString<unsigned int> for the first message and
    // MakeString<int *, int> for the second, where we get MakeString<long>
    // and MakeString<unsigned long, XBC_EVENT_TYPE>.  Both pairs are ICF-
    // folded, so they cost nothing here; reproducing them would need casts at
    // the call sites (the `int *` one implies the image's XBC_EVENT_PARAMS
    // declared the field at +0x4 as a pointer, which our XDK header does not).
```


## `?Poll@PlatformMgr@@QAAXXZ`

`PlatformMgr::Poll` -- moved verbatim from `src/system/os/PlatformMgr_Xbox.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Inside the body (was lines 1404-1415):

```text
                        // w17-d (99.172 -> 99.249): the plain indexed loop, not
                        // a hand-stepped `xf++` pointer -- MSVC now strength-
                        // reduces it itself.
                        // w26-n2 (99.249 -> 99.422): the image biases the
                        // induction pointer +8 (&szGamertag: disps 0x10/-0x8/0),
                        // ours was +0x18 (&dwFriendState, the first field
                        // read). Naming that first read -- `DWORD state =
                        // friends[i].dwFriendState;` -- is what moves the bias:
                        // the only remaining array-field address in the body is
                        // szGamertag, so c1 makes IT the induction expression.
                        // The `XONLINE_FRIEND &xf` alias and a named gamertag
                        // pointer are both byte-inert in this indexed form.
```

Inside the body (was lines 1435-1440):

```text
        // w26-n2: the image has ONLY the `size()` walk here -- the
        // `cmplw begin,sentinel / beq` at its loop head is the size loop's own
        // entry check, not a second `empty()`.  The `!empty() &&` this used to
        // carry was redundant with `size() != 0` (stlpmtX list: size() walks
        // the sentinel), so dropping it is behaviour-identical and recovers the
        // induction-setup placement: 99.42197 -> 99.75227.
```

Inside the body (was lines 1542-1557):

```text
                // The image reuses the loop latch's `lwz r11, 0x4(r3)` for the
                // mListSize store (`stw r11, mListSize@l(r9)`); we re-load it,
                // because MSVC puts `li 0x3` in r11 and clobbers the live value.
                // REFUTED (wave 7, lane w7-y): swapping these two statements so
                // mListSize is written first does NOT keep the latch value --
                // it repermutes the whole 0x50..0x88 stack-slot block and costs
                // 2.5pp binary-visible here (99.17 -> 96.7, 21 rows -> 135).
                // w26-n2: the other source handles are measured inert too --
                // naming the re-read (`unsigned long n = ...;` between the two
                // stores is BYTE-IDENTICAL: one-block/one-def, so c1 folds it),
                // naming it BEFORE the state store costs 2.2pp, a
                // `const unsigned long &n = mStorageList->dwNumItemsReturned`
                // bound in the branch costs 2.2pp, and a cast on the re-read is
                // byte-identical.  What is left is which register c2 gives
                // `li 0x3`: with the latch value live it takes r10 (image), and
                // our reload is the fixed point of it taking r11.
```
