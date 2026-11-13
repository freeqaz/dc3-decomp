# Stream Desync: Milo Object Loading on Native

Tracking document for stream desync issues encountered during native port boot.

## The Problem

Milo (`.milo_xbox`) files are compressed binary containers loaded by `DirLoader`.
Each container has a header listing object names/types, then a linear stream of
object data. Objects are loaded sequentially — each reads exactly the right number
of bytes from the stream for its type. A sentinel (`0xADDEADDE`) separates objects
for resynchronization.

**Stream desync** occurs when an object consumes the wrong number of bytes:
- Too few → next object reads leftover data as its revision, gets garbage
- Too many → next object skips past its real data, reads into the object after it

The `ReadDead` sentinel scan partially recovers, but cumulative drift eventually
produces garbage revisions → crashes in `DataNode::Load`, `TypeProps::Load`, or
allocation of impossibly large arrays/strings.

## Root Causes Identified

### 1. Stubbed `::Load` functions (primary cause)

`engine_stubs_generated.cpp` contains ~2000 weak symbol stubs. Any `::Load` stub
that gets called during a .milo parse consumes **zero bytes** from the stream,
causing the full byte count of that object's data to drift into subsequent reads.

**Fixed so far:**
- `DrivenPropertyEntry::Load` — implemented from symmetric `Save` method
- `FlowMathOp::Load` — implemented from symmetric `Save` method
- `RndFontBase::Load` — implemented (Session 4)
- `RndTex::PreLoad/PostLoad` — implemented (Session 4)

**Remaining risk**: Any stubbed Load function that gets called will cause desync.
The stubs are in `engine_stubs_generated.cpp` — search for `::Load(BinStream` to
find them. Priority stubs listed in the plan file's "Known Stubbed ::Load Functions"
section.

### 2. Nested ObjectDir detection (DirLoader-format data)

At `mRev >= 32`, ObjectDir subclass objects (RndDir, PanelDir, etc.) embedded inside
a .milo can have their data in raw **DirLoader format** (full container with its own
header, CreateObjects entries, and nested object data) rather than the simpler
**PreLoad/PostLoad format** (packed class revision + object-specific data).

The parent DirLoader tries to call `PreLoad(stream)` on these objects, which
interprets the DirLoader header as a class revision → garbage.

**Example**: `timey_wimey_elements.milo_xbox` contains object `boxyman` (type
`RndDir`). Its data starts with `0x20` (32) — a DirLoader `mRev`, not a class
revision. ObjectDir's max class revision is 28. RndDir's max class revision is 10.

## The Hack: Peek-and-Unreread Detection

### ChunkStream::Unreread

ChunkStreams are forward-only compressed streams with no `Seek()` support. We added
an `Unreread(int bytes)` method that rewinds within the current decompressed chunk
buffer by decrementing `mCurBufOffset` and `mTell`:

```cpp
// ChunkStream.h — HX_NATIVE only
void Unreread(int bytes) {
    mCurBufOffset -= bytes;
    mTell -= bytes;
}
```

**Safety constraint**: Only valid for bytes just read, within the same chunk. No
chunk boundary crossing.

### DirLoader::LoadObjs Peek Mechanism

Before loading any ObjectDir subclass object, we peek at the first 4 bytes:

```cpp
ObjectDir *dirObj = dynamic_cast<ObjectDir *>(obj);
if (dirObj) {
    int peekVal;
    *mStream >> peekVal;
    ChunkStream *cs = dynamic_cast<ChunkStream *>(mStream);
    if (cs) cs->Unreread(4);

    // DirLoader mRev: plain int, upper 16 bits zero, value > 28
    // Packed class rev: (altRev << 16) | rev, upper bits often non-zero
    bool isDirLoaderFormat = (peekVal & 0xFFFF0000) == 0
                          && (peekVal & 0xFFFF) > 28;

    if (isDirLoaderFormat) {
        ObjectDir *subDir = DirLoader::LoadObjects(mFile, nullptr, mStream);
        delete subDir;
        ReadDead(*mStream);  // consume sentinel
        mObjects.pop_front();
        continue;
    }
}
```

### Detection Heuristic

Distinguishing DirLoader format from PreLoad/PostLoad format using the first 4 bytes:

| Format | First 4 bytes | Pattern |
|--------|---------------|---------|
| DirLoader | `mRev` (e.g., 32) | Small int, upper 16 bits = 0, value > 28 |
| PreLoad | `(altRev << 16) \| rev` | Often has non-zero upper 16 bits |

**Edge case**: Classes with `altRev = 0` and `rev ≤ 28` (e.g., simple objects) are
correctly detected as PreLoad format (value ≤ 28). Classes with `altRev > 0` have
non-zero upper 16 bits → correctly not flagged as DirLoader.

**Known limitation**: A class with `altRev = 0` and `rev > 28` would be falsely
detected as DirLoader format. No such class exists in practice (max known class
revision is ~28 for ObjectDir itself).

## Defensive Guards (Crash Prevention)

These `#ifdef HX_NATIVE` guards prevent crashes from residual stream desync but
don't fix the underlying data consumption mismatch:

| Location | Guard | Purpose |
|----------|-------|---------|
| `Object::LoadType` | Skip Symbol read if `rev > 2` | Garbage rev from desync |
| `Object::LoadRest` | Return early if `rev > 100` or `altRev > 100` | Garbage rev in TypeProps/mNote |
| `FlowMathOp::Load` | Return early if `rev > 20` | Garbage FlowMathOp rev |
| `DrivenPropertyEntry::Load` | Cap `numOps` to 256 | Garbage operation count |
| `FlowNode::Load` | Cap `numEntries` to 256 | Garbage entry count |
| `BinStream::operator>>(String&)` | Cap size to 0 if `> 10000` or `< 0` | Prevent multi-GB allocation |
| `BinStream::ReadString` | Cap to `i - 1` if `a >= i` | Prevent buffer overflow |

## What Remains

### Immediate: More Stubbed Load Functions

As boot progresses, more .milo files will be loaded that exercise different object
types. Each new object type whose `::Load` is stubbed will cause desync. The fix is
always the same:

1. Find the symmetric `::Save` method for the format
2. Implement `::Load` to consume the same fields in the same order
3. Add `#ifdef HX_NATIVE` sanity caps on counts/revisions

### Medium-term: Proper Sub-DirLoader Integration

The current hack creates a sub-DirLoader, loads its objects, then **deletes
everything**. The nested dir's objects aren't wired into the parent object graph.
This means nested ObjectDirs (like `boxyman` inside `timey_wimey_elements`) are
effectively empty shells.

For actual rendering, nested dirs need their objects preserved and connected to the
parent dir's object hierarchy. This requires:

1. Not deleting the sub-DirLoader result
2. Assigning the sub-dir to the parent ObjectDir pointer
3. Handling object name resolution across nested scopes

### Long-term: Remove Defensive Guards

Once all Load functions are properly implemented and stream desync no longer occurs,
the defensive guards should be removed. They mask real bugs — a revision of 200 is
a symptom of desync, not a valid state to silently ignore.

## Debugging Stream Desync

### Symptoms
- `SUSPICIOUS length=NNNN` in BinStream string reads
- `Rev N is too great, must be ≤ M` warnings from `ASSERT_REVS`
- ASan global-buffer-overflow in `DataNode::Load` (garbage type → OOB on gDataFuncs)
- Process hangs (garbage string size → multi-GB allocation)
- `ObjectDir::PreLoad` reads revision > 28

### Diagnostic technique
1. Add `printf("tell=%d", mStream->Tell())` before/after each object's PreLoad/PostLoad
2. Compare consumed bytes with expected object size
3. The object where `tell_after - tell_before` doesn't match expected size is the culprit
4. Check if its `::Load` is stubbed in `engine_stubs_generated.cpp`
5. If stubbed: implement from symmetric `::Save` method
6. If implemented: compare field-by-field with Save to find format mismatch

### Key files
- `src/system/obj/DirLoader.cpp` — main loader, peek mechanism
- `src/system/utl/ChunkStream.h` — Unreread method
- `src/system/obj/Object.cpp` — LoadType/LoadRest defensive guards
- `src/system/utl/BinStream.cpp` — String/ReadString size caps
- `native/src/engine_stubs_generated.cpp` — stubbed Load functions
