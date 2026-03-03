# Cached Vector3 Padding Fix — CharBonesSamples::LoadData

**Date**: 2026-03-02
**Impact**: Fixed character animation in native milo-viewer (dance poses went from grotesque/broken to correct)

## Problem

Character dance animation in the native WebGPU milo-viewer produced wildly incorrect poses: hands detached from arms, legs bent backwards, extreme unnatural squats. The T-pose was also broken. Manual bone tests (individual transforms) worked fine, pointing to a data reading issue rather than a rendering issue.

## Root Cause

`CharBonesSamples::LoadData` had a bulk-read fast path for cached `.milo_xbox` streams:

```cpp
if (d.stream.Cached()) {
    int totalBytes = AllocateSize(); // = mTotalSize * mNumSamples
    d.stream.Read(mRawData, totalBytes);
    // ... byte-swap ...
}
```

The bug: **cached .milo_xbox files pad each uncompressed Vector3 to 16 bytes** (12 bytes of float data + 4 bytes zero padding), but `mTotalSize` is computed from `TypeSize(TYPE_POS)` which returns `sizeof(Vector3) = 12`.

This is visible in `CharBonesSamples::Save`:

```cpp
for (Vector3 *p = (Vector3 *)mStart; p < quatOffset; p++) {
    bs << *p;                    // 12 bytes
    if (cached) {
        float zero = 0.0f;
        bs << zero;              // +4 bytes padding
    }
}
```

So per sample, the on-disk size for positions is `numPositions * 16`, but the in-memory buffer expects `numPositions * 12`. The bulk read consumed the wrong amount of data from the stream, and the byte-swap loop operated on misaligned data. Every bone position was corrupted by accumulated 4-byte offsets.

### Why it wasn't caught earlier

The previous comment in the code incorrectly stated `sizeof(Vector3) = 16`. It's actually 12 (three floats, no padding member). The comment was wrong, masking the real discrepancy.

### Compression types affected

DC3 dance clips use `compression=1` (kCompressRots): compressed rotations but **uncompressed positions**. This is the exact case where the padding mismatch occurs.

| Compression | Positions | Quats | Rots | Padding bug? |
|---|---|---|---|---|
| kCompressNone (0) | float Vector3 | float Quat | float | **YES** |
| kCompressRots (1) | float Vector3 | ShortQuat | short | **YES** |
| kCompressVects (2) | short[3] | ShortQuat | short | No |
| kCompressQuats (3) | short[3] | ByteQuat | short | No |
| kCompressAll (4) | short[3] | ByteQuat | short | No |

Only compression < kCompressVects has the padding issue (uncompressed positions use `sizeof(Vector3)` in TypeSize but 16 bytes on disk).

## Fix

In `src/system/char/CharBonesSamples.cpp`, detect the mismatch and use a per-element read path that skips padding:

```cpp
bool cachedPaddingMismatch = d.stream.Cached() && mCompression < kCompressVects;

if (d.stream.Cached() && !cachedPaddingMismatch) {
    // Bulk read — safe when positions are compressed (no padding)
    d.stream.Read(mRawData, totalBytes);
    // byte-swap ...
} else if (cachedPaddingMismatch) {
    // Read element-by-element, skipping 4-byte padding after each Vector3
    for (each sample) {
        for (each position) {
            d >> *pos;           // reads 12 bytes with endian swap
            float pad; d >> pad; // skip 4-byte zero padding
        }
        // quaternions and rotations: no padding, read normally
        // skip per-sample end padding (alignment to 16)
    }
} else {
    // Non-cached: standard element-by-element path
}
```

The per-sample end padding (`delta` bytes to align total to 16) is also handled — it exists in the on-disk format regardless of compression type.

## Files Changed

- `src/system/char/CharBonesSamples.cpp` — Added `cachedPaddingMismatch` detection and element-by-element read path with padding skip
- `src/system/char/CharClip.h` — Added `GetFull()`/`GetOne()` accessors under `#ifdef HX_NATIVE` for diagnostics

## Verification

Screenshots at beats 16, 20, 22, 30, 40, 50, 60 all show natural dance poses. Before the fix, every frame had broken geometry (detached limbs, inverted joints). After the fix, the character animates correctly through the full clip range.

Screenshots: `archive/screenshots/fixed_cached_load/`

## Lesson

When a file format has platform-specific padding (e.g., aligning Vector3 to 16 bytes for SIMD on Xbox 360), bulk-read optimizations must account for the size difference between on-disk and in-memory representations. The padding only appears in the serialized cached format, not in the runtime struct layout.
