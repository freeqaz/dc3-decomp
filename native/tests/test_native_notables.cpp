// Native "notables" re-adjudicated with runtime evidence (branch native-notables,
// 2026-09-30). docs/decomp/patterns/native-shadow-bodies-are-unmeasured.md,
// section "Notables re-adjudicated", carries the measurements behind each test.

#include "test_helpers.h"

#include "platform/StreamReceiver_Native.h"
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
