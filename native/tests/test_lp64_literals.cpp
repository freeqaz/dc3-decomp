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

} // namespace
