// X360 literal sizes and byte order in natively compiled engine source.
//
// Ported from rb3-xenon's W16-UB LP64 literal audit (63407036c, gates in
// cada38a30): each test drives one engine body whose X360 spelling assumed
// 4-byte pointers or a big-endian host, and checks the result against a
// reference built independently of the code under test.
//
// The bodies that could fault on the broken spelling run in a threadsafe
// death-test child, so a fault fails that one test instead of the run.
#include "test_helpers.h"

#include "obj/Data.h"
#include "obj/DataFile.h"
#include "rndobj/Bitmap.h"
#include "utl/BufStream.h"
#include "utl/Compress.h"

#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <unistd.h>
#include <vector>

// RndVelocityBuffer's ctor and members are private.
#define private public
#define protected public
#include "rndobj/VelocityBuffer.h"
#undef private
#undef protected

namespace {

class Lp64LiteralTest : public SymbolTestFixture {};

// ---------------------------------------------------------------------------
// RndVelocityBuffer::RndVelocityBuffer clears mViewProjXfm through mCam.
// Retail clears a literal 0xa4 bytes (0x8..0xac), ending at the last byte of the
// 4-byte mCam. Natively mCam is 8 bytes, so 0xa4 left its upper half holding
// whatever the allocator returned. Construct over a 0xA5 fill so it shows.
TEST(Lp64Literals, VelocityBufferCtorClearsThroughCam) {
    void *mem = std::malloc(sizeof(RndVelocityBuffer));
    ASSERT_NE(mem, nullptr);
    std::memset(mem, 0xA5, sizeof(RndVelocityBuffer));
    RndVelocityBuffer *vb = ::new (mem) RndVelocityBuffer();

    const unsigned char *begin = (const unsigned char *)&vb->mViewProjXfm;
    const unsigned char *end = (const unsigned char *)(&vb->mCam + 1);
    int nonZero = 0;
    for (const unsigned char *p = begin; p < end; p++) {
        if (*p != 0)
            nonZero++;
    }
    EXPECT_EQ(nonZero, 0) << "of " << (end - begin)
                          << " bytes mViewProjXfm..mCam (0xa4 on X360)";
    EXPECT_EQ(vb->mCam, nullptr);

    vb->~RndVelocityBuffer();
    std::free(mem);
}

// ---------------------------------------------------------------------------
// RndBitmap::LoadDIB reads a bottom-up BMP row by row into
// (void *)((int)pixels + i * rowBytes): the int truncates a 64-bit heap pointer.
// The fixture's pixels encode their own coordinates, so a misplaced or short
// row shows up as a mismatch.
const int kBmpW = 3;
const int kBmpH = 4;

void PutU16(std::vector<unsigned char> &v, unsigned int x) {
    v.push_back(x & 0xFF);
    v.push_back((x >> 8) & 0xFF);
}
void PutU32(std::vector<unsigned char> &v, unsigned int x) {
    PutU16(v, x & 0xFFFF);
    PutU16(v, x >> 16);
}

std::vector<unsigned char> BuildBottomUpBmp() {
    const int rowBytes = kBmpW * 4;
    std::vector<unsigned char> v;
    PutU16(v, 0x4D42); // "BM"
    PutU32(v, 14 + 40 + rowBytes * kBmpH); // bfSize
    PutU16(v, 0);
    PutU16(v, 0);
    PutU32(v, 14 + 40); // bfOffBits
    PutU32(v, 40); // biSize
    PutU32(v, kBmpW);
    PutU32(v, kBmpH); // positive = bottom-up
    PutU16(v, 1); // biPlanes
    PutU16(v, 32); // biBitCount
    PutU32(v, 0); // biCompression
    PutU32(v, rowBytes * kBmpH);
    PutU32(v, 0xB11); // biXPelsPerMeter: skip the alpha fill
    PutU32(v, 0);
    PutU32(v, 0);
    PutU32(v, 0);
    for (int y = kBmpH - 1; y >= 0; y--) { // file rows run bottom to top
        for (int x = 0; x < kBmpW; x++) {
            v.push_back(x);
            v.push_back(y);
            v.push_back(0x5A);
            v.push_back(0xA5);
        }
    }
    return v;
}

void LoadBottomUpBmpThenExit() {
    std::vector<unsigned char> file = BuildBottomUpBmp();
    BufStream bs(file.data(), (int)file.size(), true);
    RndBitmap bmp;
    if (!bmp.LoadBmp(&bs))
        _exit(2);
    if (bmp.Width() != kBmpW || bmp.Height() != kBmpH || bmp.Bpp() != 32)
        _exit(3);
    int misplaced = 0;
    for (int y = 0; y < kBmpH; y++) {
        const unsigned char *row = bmp.Pixels() + y * bmp.RowBytes();
        for (int x = 0; x < kBmpW; x++) {
            const unsigned char *px = row + x * 4;
            if (px[0] != x || px[1] != y || px[2] != 0x5A || px[3] != 0xA5)
                misplaced++;
        }
    }
    _exit(misplaced == 0 ? 0 : 4);
}

TEST_F(Lp64LiteralTest, LoadDibReadsBottomUpRowsIntoTheHeapBlock) {
    GTEST_FLAG_SET(death_test_style, "threadsafe"); // see test_object_lifetime.cpp
    ASSERT_EXIT(LoadBottomUpBmpThenExit(), ::testing::ExitedWithCode(0), "")
        << "exit 2 = LoadBmp failed, 3 = wrong dimensions, 4 = misplaced pixels; "
           "a signal means the row reads went through a truncated pointer";
}

// ---------------------------------------------------------------------------
// LoadDtz reads the decompressed size from a little-endian 4-byte trailer.
// The X360 spelling stores its bytes most-significant first, which is a
// little-endian read only on a big-endian host. The payload is padded until
// the size's low byte is >= 0x80, so the byte-reversed read is negative rather
// than an oversized buffer that happens to decompress correctly.
std::vector<char> SerializeArray(const DataArray *da) {
    std::vector<char> bytes(0x10000);
    BufStream out(bytes.data(), (int)bytes.size(), true);
    out << da;
    bytes.resize(out.Tell());
    return bytes;
}

void LoadDtzRoundTripThenExit() {
    std::vector<char> payload;
    DataArray *src = nullptr;
    for (int pad = 0; pad < 0x200; pad++) {
        std::string text = "(w16ub (size 1 2 3) (label \"" + std::string(pad, 'x') + "\"))";
        if (src)
            src->Release();
        src = DataReadString(text.c_str());
        payload = SerializeArray(src);
        if ((payload.size() & 0xFF) >= 0x80)
            break;
    }
    if ((payload.size() & 0xFF) < 0x80)
        _exit(2);

    std::vector<char> dtz(payload.size() * 2 + 0x100);
    int compLen = (int)dtz.size() - 4;
    CompressMem(payload.data(), (int)payload.size(), dtz.data(), compLen, "w16ub");
    unsigned int size = (unsigned int)payload.size();
    dtz[compLen + 0] = (char)(size & 0xFF);
    dtz[compLen + 1] = (char)((size >> 8) & 0xFF);
    dtz[compLen + 2] = (char)((size >> 16) & 0xFF);
    dtz[compLen + 3] = (char)((size >> 24) & 0xFF);

    DataArray *loaded = LoadDtz(dtz.data(), compLen + 4);
    if (!loaded)
        _exit(3);
    std::vector<char> again = SerializeArray(loaded);
    bool same = again == payload;
    loaded->Release();
    src->Release();
    _exit(same ? 0 : 4);
}

TEST_F(Lp64LiteralTest, LoadDtzReadsTheSizeTrailerLittleEndian) {
    GTEST_FLAG_SET(death_test_style, "threadsafe"); // see test_object_lifetime.cpp
    ASSERT_EXIT(LoadDtzRoundTripThenExit(), ::testing::ExitedWithCode(0), "")
        << "exit 2 = could not pad the payload, 3 = LoadDtz returned null, "
           "4 = round trip differs; a signal means the size was misread";
}

// ---------------------------------------------------------------------------
// DataNode::Equal on pointer-valued nodes (w25-pch2).
// The image compares two same-type nodes with UncheckedInt(), the whole 4-byte
// value. Natively the union is 8 bytes and UncheckedInt() reads its low half,
// so two objects / vars / funcs 4 GiB apart compared EQUAL. The pointers are
// never dereferenced on the same-type path, so fabricated values are safe.
TEST_F(Lp64LiteralTest, DataNodeEqualComparesWholePointer) {
    const uintptr_t low = 0x00001000u;
    const uintptr_t hiA = uintptr_t(1) << 32, hiB = uintptr_t(2) << 32;

    DataNode objA(reinterpret_cast<Hmx::Object *>(hiA | low));
    DataNode objB(reinterpret_cast<Hmx::Object *>(hiB | low));
    DataNode objA2(reinterpret_cast<Hmx::Object *>(hiA | low));
    EXPECT_FALSE(objA.Equal(objB, nullptr, false)) << "Object: same low half, different pointer";
    EXPECT_TRUE(objA.Equal(objA2, nullptr, false)) << "Object: same pointer";

    DataNode varA(reinterpret_cast<DataNode *>(hiA | low));
    DataNode varB(reinterpret_cast<DataNode *>(hiB | low));
    EXPECT_FALSE(varA.Equal(varB, nullptr, false)) << "Var: same low half, different pointer";

    DataNode funcA(reinterpret_cast<DataFunc *>(hiA | low));
    DataNode funcB(reinterpret_cast<DataFunc *>(hiB | low));
    EXPECT_FALSE(funcA.Equal(funcB, nullptr, false)) << "Func: same low half, different pointer";

    // Int-valued types keep the image's 4-byte compare.
    EXPECT_TRUE(DataNode(7).Equal(DataNode(7), nullptr, false));
    EXPECT_FALSE(DataNode(7).Equal(DataNode(8), nullptr, false));
}

// DataNode::Load of a 4-byte value (Int, Float, ...) into a node that held a
// pointer: DataArray::Load overwrites an ifdef'd-out entry in place, so the
// node can carry the previous pointer's high half. The ctors zero all 8 bytes
// natively; Load must too, or the int node's union is not its value.
TEST_F(Lp64LiteralTest, DataNodeLoadOfFourByteValueZeroesTheHighHalf) {
    const uintptr_t stale = (uintptr_t(0xAAAAAAAAu) << 32) | 0x1234u;
    char bytes[16];
    {
        BufStream out(bytes, sizeof(bytes), true);
        out << (int)kDataInt << (int)5;
    }
    DataNode node(reinterpret_cast<Hmx::Object *>(stale));
    BufStream in(bytes, sizeof(bytes), true);
    node.Load(in);
    ASSERT_EQ(node.Type(), kDataInt);
    EXPECT_EQ(node.UncheckedInt(), 5);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(node.UncheckedObj()), uintptr_t(5))
        << "high half of an Int node loaded over a pointer was left stale";

    float one = 1.0f;
    unsigned int oneBits;
    memcpy(&oneBits, &one, sizeof(oneBits));
    {
        BufStream out(bytes, sizeof(bytes), true);
        out << (int)kDataFloat << one;
    }
    DataNode fnode(reinterpret_cast<Hmx::Object *>(stale));
    BufStream fin(bytes, sizeof(bytes), true);
    fnode.Load(fin);
    ASSERT_EQ(fnode.Type(), kDataFloat);
    EXPECT_EQ(fnode.UncheckedFloat(), 1.0f);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(fnode.UncheckedObj()), uintptr_t(oneBits))
        << "high half of a Float node loaded over a pointer was left stale";
}

} // namespace
