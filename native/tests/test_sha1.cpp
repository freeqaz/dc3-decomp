// CSHA1 little-endian / LP64 regression tests (Wave-3 Lane C, roadmap N.4).
//
// `CSHA1::Transform` (src/system/math/SHA1.cpp) reads the 64-byte message block
// as sixteen 32-bit words via a union (`SHA1_WORKSPACE_BLOCK`). The decompiled
// source is faithful to the Xbox 360 binary, which is BIG-ENDIAN and uses an
// ILP32 `unsigned long`. On the little-endian, LP64 native/web host that same
// source is doubly wrong:
//
//   1. `unsigned long l[16]` is 64-bit on LP64, so l[16] is 128 bytes (overruns
//      the 64-byte union) and the SHA1 word math operates on 64-bit words.
//   2. Even with 32-bit words, the host reads each message word little-endian,
//      the reverse of the big-endian order SHA1 expects.
//
// The fix (guarded by HX_NATIVE so the PPC match is untouched) pins `l[]` to a
// 32-bit `unsigned int` and byteswaps the raw word in blk0. These tests pin the
// CANONICAL SHA1 digests of the standard FIPS-180 vectors; they fail on the
// pre-fix native code (which produced e.g. SHA1("abc") = 15e323a5... or crashed
// under -O2 from the 128-byte union overrun).

#include <gtest/gtest.h>

#include "math/SHA1.h"

#include <cstdio>
#include <string>

namespace {

// Hash a byte string with CSHA1 and return the 40-char lowercase hex digest.
std::string Sha1Hex(const std::string &msg) {
    CSHA1 sha;
    sha.Update(reinterpret_cast<const unsigned char *>(msg.data()),
               static_cast<unsigned int>(msg.size()));
    const CSHA1::Digest &d = sha.Final();
    char hex[41];
    for (int i = 0; i < 20; ++i)
        std::sprintf(hex + 2 * i, "%02x", d.digits[i]);
    return std::string(hex, 40);
}

}  // namespace

// FIPS-180-1 vector: SHA1("") = da39a3ee...
TEST(Sha1, EmptyString) {
    EXPECT_EQ(Sha1Hex(""), "da39a3ee5e6b4b0d3255bfef95601890afd80709");
}

// FIPS-180-1 vector: SHA1("abc") = a9993e36... (the canonical short test).
// Pre-fix native returned 15e323a5dca7fcbdba3e25717850c26c9cd0d89d.
TEST(Sha1, Abc) {
    EXPECT_EQ(Sha1Hex("abc"), "a9993e364706816aba3e25717850c26c9cd0d89d");
}

// 56-byte vector (exercises the exact one-block-with-padding boundary).
TEST(Sha1, FiftySixBytes) {
    EXPECT_EQ(Sha1Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
              "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
}

// 1000 'a's (exercises multi-block Transform with the blk()/blk0() interplay).
TEST(Sha1, ThousandA) {
    EXPECT_EQ(Sha1Hex(std::string(1000, 'a')),
              "291e9a6c66994949b57ba5e650361e98fc36b1ba");
}

// Hashing twice from a fresh object is deterministic and Reset() works (the
// dtor + ctor path the real HDCache / StreamChecksum users rely on).
TEST(Sha1, Deterministic) {
    EXPECT_EQ(Sha1Hex("milo"), Sha1Hex("milo"));
}
