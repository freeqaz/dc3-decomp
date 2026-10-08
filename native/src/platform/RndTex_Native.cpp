// DC3 Native Port - RndBitmap loading
// Replaces the engine_stubs_generated.cpp stub for RndBitmap::Load. It must
// consume the correct bytes from the stream to keep it aligned for subsequent
// object loading in .milo files. (RndTex::Load/PreLoad/PostLoad used to be
// shadowed here too; native now runs the image's bodies in rndobj/Tex.cpp.)

#include "rndobj/Tex.h"
#include "rndobj/Bitmap.h"
#include "utl/BinStream.h"
#include "utl/ChunkStream.h"

// Forward declaration - defined in ChunkStream.cpp, no header declaration
BinStream &ReadChunks(BinStream &bs, void *data, int total_len, int max_chunk_size);

// --- RndBitmap::Load ---
// Reads bitmap header, palette, pixel data (via ReadChunks), and mip chain.
// Based on RB3 reference: rb3/src/system/rndobj/Bitmap.cpp:1018
void RndBitmap::Load(BinStream &bs) {
    u8 mipCt;
    LoadHeader(bs, mipCt);
    if (mBuffer) {
        MemFree(mBuffer);
        mBuffer = nullptr;
    }
    mPalette = nullptr;
    AllocateBuffer();
    if (mPalette)
        bs.Read(mPalette, PaletteBytes());
    ReadChunks(bs, mPixels, PixelBytes(), 0x8000);
    RELEASE(mMip);
    RndBitmap *workingMip = this;
    int working_w = mWidth;
    int working_h = mHeight;
    while (mipCt--) {
        RndBitmap *newMip = new RndBitmap();
        workingMip->mMip = newMip;
        workingMip = newMip;
        working_w = working_w >> 1;
        working_h = working_h >> 1;
        newMip->Create(working_w, working_h, 0, mBpp, mOrder, mPalette, 0, 0);
        ReadChunks(bs, newMip->Pixels(), newMip->PixelBytes(), 0x8000);
    }
}
