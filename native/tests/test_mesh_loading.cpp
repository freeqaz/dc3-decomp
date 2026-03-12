#include "test_helpers.h"

#include "gfx/VertexFormats.h"
#include "rndobj/Mesh.h"
#include "rndobj/MeshVertCompress.h"

BinStreamRev &operator>>(BinStreamRev &, RndMesh::Vert &);

namespace {

class TestMesh : public RndMesh {
public:
    TestMesh() : RndMesh() {}
    using RndMesh::LoadVertices;
};

static std::vector<uint8_t> MakeCompressedVertexRecord(
    uint32_t packedWeights,
    uint32_t packedIndices
) {
    std::vector<uint8_t> buf;
    buf.reserve(sizeof(CompressedVertex_Xbox));
    PutBEFloat(buf, 1.0f);
    PutBEFloat(buf, 2.0f);
    PutBEFloat(buf, 3.0f);
    PutBE32(buf, 0xFF7F3F1F);
    PutBE32(buf, 0);
    PutBE32(buf, 0);
    PutBE32(buf, 0);
    PutBE32(buf, packedWeights);
    PutBE32(buf, packedIndices);
    return buf;
}

} // namespace

TEST(MeshVertexLoading, NativeCompressedLoadPreservesRawBlob) {
    TestMesh mesh;

    const uint32_t packedWeights = 1023u;
    const uint32_t packedIndices = 2u | (3u << 8) | (4u << 16) | (5u << 24);
    std::vector<uint8_t> record = MakeCompressedVertexRecord(packedWeights, packedIndices);

    std::vector<uint8_t> streamBytes;
    PutBE32(streamBytes, 1);
    streamBytes.push_back(1);
    PutBE32(streamBytes, sizeof(CompressedVertex_Xbox));
    PutBE32(streamBytes, 1);
    streamBytes.insert(streamBytes.end(), record.begin(), record.end());

    MemBinStream ms(streamBytes.data(), (int)streamBytes.size(), false);
    BinStreamRev rev(ms, 0x26);
    mesh.LoadVertices(rev);

    EXPECT_EQ(mesh.NumVerts(), 0);
    EXPECT_EQ(mesh.NumCompressedVerts(), 1u);
    ASSERT_NE(mesh.CompressedVerts(), nullptr);
    EXPECT_EQ(memcmp(mesh.CompressedVerts(), record.data(), sizeof(CompressedVertex_Xbox)), 0);
}

TEST(MeshVertexLoading, CompressedSkinnedDecodePreservesBoneWeightsAndIndices) {
    const uint32_t packedWeights = 1023u;
    const uint32_t packedIndices = 2u | (3u << 8) | (4u << 16) | (5u << 24);
    std::vector<uint8_t> record = MakeCompressedVertexRecord(packedWeights, packedIndices);

    GpuVertexSkinned out{};
    ASSERT_EQ(
        VertexFormats::UnpackCompressedSkinnedVertices(
            record.data(), 1, &out, 1
        ),
        1
    );

    EXPECT_FLOAT_EQ(out.pos[0], 1.0f);
    EXPECT_FLOAT_EQ(out.pos[1], 2.0f);
    EXPECT_FLOAT_EQ(out.pos[2], 3.0f);
    EXPECT_FLOAT_EQ(out.boneWeights[0], 1.0f);
    EXPECT_FLOAT_EQ(out.boneWeights[1], 0.0f);
    EXPECT_FLOAT_EQ(out.boneWeights[2], 0.0f);
    EXPECT_FLOAT_EQ(out.boneWeights[3], 0.0f);
    EXPECT_EQ(out.boneIndices[0], 2);
    EXPECT_EQ(out.boneIndices[1], 3);
    EXPECT_EQ(out.boneIndices[2], 4);
    EXPECT_EQ(out.boneIndices[3], 5);
}

TEST(MeshVertexLoading, UncompressedVertRev26ReadsWeightsAndIndices) {
    std::vector<uint8_t> buf;
    PutBEFloat(buf, 1.0f);
    PutBEFloat(buf, 2.0f);
    PutBEFloat(buf, 3.0f);
    PutBEFloat(buf, 4.0f);
    PutBEFloat(buf, 5.0f);
    PutBEFloat(buf, 6.0f);
    PutBEFloat(buf, 0.1f);
    PutBEFloat(buf, 0.2f);
    PutBEFloat(buf, 0.3f);
    PutBEFloat(buf, 0.4f);
    PutBEFloat(buf, 0.5f);
    PutBEFloat(buf, 0.6f);
    PutBEFloat(buf, 0.7f);
    PutBEFloat(buf, 0.2f);
    PutBEFloat(buf, 0.1f);
    PutBEFloat(buf, 0.0f);
    PutBE16(buf, 1);
    PutBE16(buf, 2);
    PutBE16(buf, 3);
    PutBE16(buf, 4);
    PutBEFloat(buf, 1.0f);
    PutBEFloat(buf, 0.0f);
    PutBEFloat(buf, 0.0f);
    PutBEFloat(buf, 1.0f);

    MemBinStream ms(buf.data(), (int)buf.size(), false);
    BinStreamRev rev(ms, 0x26);
    RndMesh::Vert vert;
    rev >> vert;

    EXPECT_FLOAT_EQ(vert.boneWeights.x, 0.7f);
    EXPECT_FLOAT_EQ(vert.boneWeights.y, 0.2f);
    EXPECT_FLOAT_EQ(vert.boneWeights.z, 0.1f);
    EXPECT_FLOAT_EQ(vert.boneWeights.w, 0.0f);
    EXPECT_EQ(vert.boneIndices[0], 1);
    EXPECT_EQ(vert.boneIndices[1], 2);
    EXPECT_EQ(vert.boneIndices[2], 3);
    EXPECT_EQ(vert.boneIndices[3], 4);
}
