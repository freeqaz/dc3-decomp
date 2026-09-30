// Native "notables" re-adjudicated with runtime evidence (branch native-notables,
// 2026-09-30). docs/decomp/patterns/native-shadow-bodies-are-unmeasured.md,
// section "Notables re-adjudicated", carries the measurements behind each test.

#include "test_helpers.h"

#include "math/Geo.h"
#include "platform/StreamReceiver_Native.h"
#include "rndobj/Mesh.h"
#include "synth/MoggClip.h"
#include "synth/StreamReceiver.h"
#include "synth/Synth.h"

#include <cstdint>
#include <vector>

namespace {

class NativeNotablesMoggTest : public EngineTestFixture {
protected:
    void SetUp() override {
        mSavedFactory = StreamReceiver::sFactory;
        StreamReceiver::sFactory = StreamReceiverNative::Create;
    }
    void TearDown() override { StreamReceiver::sFactory = mSavedFactory; }
    StreamReceiverFactoryFunc *mSavedFactory = nullptr;
};

} // namespace

// MoggClip::LoadNumChannels (PostLoad, SetFile) finds the mogg's channel count
// by starting it: the image calls Play(0) (vtable slot 0xc,
// build/373307D9/asm/system/synth/MoggClip.s) and polls the synth until the
// stream reports its info channels. Native called SynthPoll() instead, which
// does nothing for a clip that is not playing, so every MoggClip read -1 --
// Sound::SetPan / DisablePan then never recognised a stereo mogg. Every mogg a
// perform, practice and party route loaded (34 distinct files) is stereo.
TEST_F(NativeNotablesMoggTest, LoadNumChannelsReadsTheMoggsChannelCount) {
    MoggClip *clip = Hmx::Object::New<MoggClip>();
    clip->SetFile("sfx/samples/shell/shell_dciambience.mogg");
    EXPECT_EQ(clip->NumChannels(), 2)
        << "a stereo mogg must report 2 channels after SetFile; -1 means "
           "LoadNumChannels never started the stream (the image calls Play(0))";
    EXPECT_FALSE(clip->IsStreaming()) << "LoadNumChannels must Stop() the probe stream";
    delete clip;
}

// ---------------------------------------------------------------------------
// StreamReceiver: when a finished stream reports kFinished.
//
// The image's StreamReceiver::Poll (build/373307D9/asm/system/synth/
// StreamReceiver.s) keeps its voice's ring of mNumBuffers 0x4000-byte buffers
// full: each time the play cursor leaves a buffer it sends the next 0x4000
// bytes of its 0x8000-byte local ring into it. After EndData() the local ring is
// zero-padded, and every completed send counts one "done buffer";
// StandardStream::PollStream reports kFinished once that count exceeds
// mNumBuffers + 2. Walked through (a model of that Poll over 4000 random
// lengths, buffer counts 4..12 and frame sizes 735..2940 bytes), that is the
// moment the play cursor reaches the buffer boundary one whole buffer past the
// one holding the last byte written: 0x4000 * (ceil(written / 0x4000) + 1),
// i.e. between 1 and 2 buffers (186..372 ms of 44.1 kHz mono PCM per channel)
// of silence after the audio ends -- to within one frame, whatever mNumBuffers.
//
// Native counted one "done buffer" per Poll() once its ring had drained, so a
// stream finished mNumBuffers + 3 frames after its audio ended: measured on a
// perform route 36..90 ms of silence (1153..3974 samples), below the image's
// minimum of 8192.
namespace {

class ProbeReceiver : public StreamReceiverNative {
public:
    ProbeReceiver(int numBuffers) : StreamReceiverNative(numBuffers, false) {}
    bool Finished() const { return mDoneBufferCounter > mNumBuffers + 2; }
};

// Write `written` bytes, EndData, then render `frameBytes` per poll until the
// receiver counts as finished; returns the bytes played by then.
long long PlayedAtFinish(int numBuffers, int written, int frameBytes) {
    ProbeReceiver r(numBuffers);
    r.Play();
    std::vector<int16_t> pcm(written / 2, 1000);
    for (int off = 0; off < written; off += 0x800) {
        int n = std::min(0x800, written - off);
        r.WriteData(reinterpret_cast<const char *>(pcm.data()) + off, n);
    }
    r.EndData();
    std::vector<float> out(frameBytes); // stereo float frames: frameBytes/2 * 2
    for (int i = 0; i < 1000; i++) {
        r.Poll();
        if (r.Finished())
            return (long long)r.GetBytesPlayed();
        r.RenderAudio(out.data(), frameBytes / 2);
    }
    return -1;
}

class NativeNotablesStreamTest : public EngineTestFixture {};

} // namespace

TEST_F(NativeNotablesStreamTest, ReceiverFinishesOneBufferPastTheLastBuffer) {
    const int B = 0x4000;
    struct Case {
        int numBuffers, written, frameBytes;
    } cases[] = {
        { 6, 2 * B + 5000, 1470 }, // 44.1 kHz at 60 fps
        { 4, 3 * B + 2, 1066 },    // 32 kHz
        { 6, 2 * B, 1470 },        // ends exactly on a buffer boundary
        { 12, 17000, 2940 },       // many buffers: the image rule ignores mNumBuffers
    };
    for (const Case &c : cases) {
        long long expect = (long long)B * ((c.written + B - 1) / B + 1);
        long long got = PlayedAtFinish(c.numBuffers, c.written, c.frameBytes);
        EXPECT_GE(got, expect) << "numBuffers " << c.numBuffers << " written " << c.written
                               << ": finished after only " << got - c.written
                               << " bytes of silence; the image plays to " << expect;
        EXPECT_LT(got, expect + c.frameBytes)
            << "numBuffers " << c.numBuffers << " written " << c.written
            << ": finished late (" << got << " vs " << expect << ")";
    }
}

// ---------------------------------------------------------------------------
// RndMesh::SetVolume(kVolumeBSP) builds the mesh's collision BSP tree with
// MakeBSPTree (math/Geo.cpp, 0x82538xxx). Native compiled MakeBSPTree as
// `return false`, so SetVolume released the tree and a BSP-volume mesh built
// or copied at runtime (SetVolume from the `volume` property, CopyGeometry,
// a rev-0x12 mesh's Load) had no collision at all. Meshes loaded from the
// shipped files carry their tree on disk (rev > 0x12) and are unaffected;
// measured on perform + practice routes, SetVolume(kVolumeBSP) was never
// reached, so this is a latent divergence.
TEST_F(NativeNotablesStreamTest, BspVolumeMeshGetsATreeThatCollides) {
    RndMesh *mesh = Hmx::Object::New<RndMesh>();
    static const float kCorners[8][3] = {
        { -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 },
        { -1, -1, 1 },  { 1, -1, 1 },  { 1, 1, 1 },  { -1, 1, 1 },
    };
    mesh->Verts().resize(8);
    for (int i = 0; i < 8; i++)
        mesh->Verts()[i].pos.Set(kCorners[i][0], kCorners[i][1], kCorners[i][2]);
    static const int kFaces[12][3] = {
        { 0, 2, 1 }, { 0, 3, 2 }, { 4, 5, 6 }, { 4, 6, 7 }, { 0, 1, 5 }, { 0, 5, 4 },
        { 3, 6, 2 }, { 3, 7, 6 }, { 0, 4, 7 }, { 0, 7, 3 }, { 1, 2, 6 }, { 1, 6, 5 },
    };
    mesh->Faces().resize(12);
    for (int i = 0; i < 12; i++)
        mesh->Faces()[i].Set(kFaces[i][0], kFaces[i][1], kFaces[i][2]);
    mesh->SetVolume(RndMesh::kVolumeBSP);
    const BSPNode *tree = mesh->GetBSPTree();
    ASSERT_NE(tree, nullptr) << "SetVolume(kVolumeBSP) left no BSP tree: MakeBSPTree failed";
    Segment through;
    through.start.Set(0, 0, -5);
    through.end.Set(0, 0, 5);
    float frac = -1;
    Plane pl;
    EXPECT_TRUE(Intersect(through, tree, frac, pl)) << "a segment through the cube must hit it";
    Segment beside;
    beside.start.Set(3, 0, -5);
    beside.end.Set(3, 0, 5);
    EXPECT_FALSE(Intersect(beside, tree, frac, pl)) << "a segment beside the cube must miss it";
    delete mesh;
}
