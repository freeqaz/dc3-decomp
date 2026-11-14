// MOGG v0xE decryption diagnostic test
//
// Tests the full key derivation pipeline for v0xE encrypted mogg files
// (song audio). v0xB (SFX) uses a hardcoded key and works; v0xE uses
// GrindArray DTA scripting and fails. This test isolates the failure.
//
// Usage:
//   ./milo-tests --gtest_filter='MoggV0xE*'

#include "test_helpers.h"
#include <gtest/gtest.h>

#include "synth/Synth.h"
#include "synth/StandardStream.h"
#include "synth/VorbisReader.h"
#include "synth/StreamReceiver.h"
#include "platform/StreamReceiver_Native.h"
#include "audio/AudioDevice.h"
#include "os/File.h"
#include "os/BufFile.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "platform/ExecutablePath.h"

extern File *NewFile(const char *, int);

// ============================================================================
// Test fixture
// ============================================================================

class MoggV0xETest : public EngineTestFixture {
protected:
    void SetUp() override {
        if (!getenv("DC3_AUDIO_TESTS"))
            GTEST_SKIP() << "Set DC3_AUDIO_TESTS=1 to enable (audio device contention under ctest -j)";
        if (!StreamReceiver::sFactory)
            StreamReceiver::sFactory = StreamReceiverNative::Create;
        if (!AudioDevice::GetInstance().IsInitialized())
            AudioDevice::GetInstance().Init(44100);
        // ByteGrinder::Init() registers DTA functions (ma, za, ha, ya, O0-O70)
        // needed by setupCypher/GrindArray. In the full engine, SynthInit() calls
        // Synth::InitSecurity() which calls this. The test harness skips SynthInit.
        TheSynth->Grinder().Init();
    }
};

// ============================================================================
// Test 1: Can we open boyfriend.mogg from the archive?
// ============================================================================

TEST_F(MoggV0xETest, OpenSongMogg) {
    const char *moggPath = "songs/boyfriend/boyfriend.mogg";
    File *file = NewFile(moggPath, 2);
    ASSERT_NE(file, nullptr) << "Could not open " << moggPath << " from archive";

    int size = file->Size();
    printf("  Opened: %s (%d bytes)\n", moggPath, size);
    EXPECT_GT(size, 3252) << "File too small to contain mogg header";

    // Read first 8 bytes and verify header
    unsigned char hdr[8];
    file->Read(hdr, 8);

    int version = hdr[0] | (hdr[1] << 8) | (hdr[2] << 16) | (hdr[3] << 24);
    int hdrSize = hdr[4] | (hdr[5] << 8) | (hdr[6] << 16) | (hdr[7] << 24);
    printf("  version=0x%x (%d), headerSize=%d\n", version, version, hdrSize);
    EXPECT_EQ(version, 0xE) << "Expected v0xE mogg";
    EXPECT_EQ(hdrSize, 3252);

    delete file;
}

// ============================================================================
// Test 2: Load boyfriend.mogg into buffer and try full VorbisReader decode
// ============================================================================

TEST_F(MoggV0xETest, DecodeBoyfriendMogg) {
    const char *moggPath = "songs/boyfriend/boyfriend.mogg";
    File *file = NewFile(moggPath, 2);
    ASSERT_NE(file, nullptr) << "Could not open " << moggPath;

    int fileSize = file->Size();
    printf("  Loading %s (%d bytes)...\n", moggPath, fileSize);

    // Read entire file into buffer (mimics FileLoader)
    std::vector<unsigned char> buffer(fileSize);
    file->Read(buffer.data(), fileSize);
    delete file;

    // Use StandardStream which owns the VorbisReader internally.
    // Previously this test created a VorbisReader with stream=nullptr, but
    // once decryption succeeds, VorbisReader::Init() dereferences mStream
    // to set up audio output — causing a null-pointer crash.
    BufFile *bufFile = new BufFile(buffer.data(), fileSize);
    StandardStream stream(bufFile, 0.0f, 0.0f, "mogg", false, true, false);

    printf("  Polling StandardStream (header parse + decrypt)...\n");
    int polls = 0;
    const int maxPolls = 500;
    bool ready = false;
    while (polls < maxPolls) {
        stream.PollStream();
        polls++;
        if (stream.IsReady()) {
            ready = true;
            printf("  *** DECRYPTION SUCCEEDED after %d polls! ***\n", polls);
            printf("  channels=%d, sampleRate=%d\n",
                   stream.GetNumChannels(), (int)stream.GetSampleRate());
            break;
        }
        if (stream.Fail()) {
            break;
        }
    }

    if (stream.Fail()) {
        printf("  *** DECRYPTION FAILED after %d polls ***\n", polls);
        printf("  This confirms GrindArray DTA key derivation produces wrong key on x86_64.\n");
        printf("\n  Diagnostic: first 16 bytes of encrypted audio data:\n    ");
        int hdrSize = buffer[4] | (buffer[5] << 8) | (buffer[6] << 16) | (buffer[7] << 24);
        for (int i = 0; i < 16 && hdrSize + i < fileSize; i++)
            printf("%02x ", buffer[hdrSize + i]);
        printf("\n");
    }

    EXPECT_FALSE(stream.Fail()) << "v0xE mogg decryption failed";
    // A bad key no longer sets the reader's mFail (TryReadHeader keeps
    // retrying a page-sync failure, as the image does), so a wrong decrypt
    // shows up as a stream that never becomes ready -- check that directly.
    EXPECT_TRUE(ready) << "v0xE mogg never became ready after " << polls << " polls";
}

// ============================================================================
// DLC: a song mogg from the Dance Central 2 DLC megapack (also v0xE) decodes
// with the same key path.  The package is game content and never committed
// (scripts/dlc/import.sh puts it in orig-assets/dlc), so it skips when absent
// -- and ONLY then.  Its own fixture opens no audio device: reaching IsReady()
// is header parse + key derivation + decrypt, none of which plays a sample, so
// the DC3_AUDIO_TESTS gate (device contention under ctest -j) has nothing to
// guard here, and a box that has the package runs it in the default gate.
// ============================================================================

class MoggDlcTest : public EngineTestFixture {
protected:
    void SetUp() override {
        if (!StreamReceiver::sFactory)
            StreamReceiver::sFactory = StreamReceiverNative::Create;
        TheSynth->Grinder().Init(); // as MoggV0xETest: the v0xE key path's DTA funcs
    }
};

TEST_F(MoggDlcTest, DecodeDlcMilkshakeMogg) {
    char exe[4096];
    ssize_t len = ReadExecutablePath(exe, sizeof(exe) - 1);
    ASSERT_GT(len, 0);
    exe[len] = '\0';
    std::string dir(exe);
    dir = dir.substr(0, dir.rfind('/'));
    std::string path =
        dir + "/../../orig-assets/dlc/dancecentral2dlcmegapack/songs/milkshake/milkshake.mogg";
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) GTEST_SKIP() << "DLC package absent: " << path;
    std::vector<unsigned char> buffer;
    unsigned char chunk[65536];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0)
        buffer.insert(buffer.end(), chunk, chunk + n);
    fclose(f);
    ASSERT_GT(buffer.size(), 8u);
    EXPECT_EQ(buffer[0], 0xE) << "expected a v0xE mogg";

    BufFile *bufFile = new BufFile(buffer.data(), (int)buffer.size());
    StandardStream stream(bufFile, 0.0f, 0.0f, "mogg", false, true, false);
    bool ready = false;
    for (int polls = 0; polls < 500 && !stream.Fail(); polls++) {
        stream.PollStream();
        if (stream.IsReady()) {
            ready = true;
            break;
        }
    }
    EXPECT_FALSE(stream.Fail());
    ASSERT_TRUE(ready) << "the DLC mogg never became ready (wrong key?)";
    printf("  milkshake.mogg: channels=%d sampleRate=%d\n", stream.GetNumChannels(),
           (int)stream.GetSampleRate());
    EXPECT_GT(stream.GetNumChannels(), 0);
    EXPECT_EQ((int)stream.GetSampleRate(), 44100);
}

// ============================================================================
// Test 3: Compare v0xB (working) and v0xE (failing) side by side
// ============================================================================

TEST_F(MoggV0xETest, CompareV0xBvsV0xE) {
    struct TestCase {
        const char *path;
        const char *label;
        int expectedVersion;
    };
    TestCase cases[] = {
        {"sfx/samples/shell/shellmusic_loop_01.mogg", "shellmusic (v0xB)", 0xB},
        {"songs/boyfriend/boyfriend.mogg", "boyfriend (v0xE)", 0xE},
    };

    for (auto &tc : cases) {
        printf("\n  === %s ===\n", tc.label);
        File *file = NewFile(tc.path, 2);
        if (!file) {
            printf("  SKIP: could not open %s\n", tc.path);
            continue;
        }

        int fileSize = file->Size();
        std::vector<unsigned char> buffer(fileSize);
        file->Read(buffer.data(), fileSize);
        delete file;

        int version = buffer[0] | (buffer[1] << 8) | (buffer[2] << 16) | (buffer[3] << 24);
        printf("  version=0x%x, size=%d\n", version, fileSize);
        EXPECT_EQ(version, tc.expectedVersion);

        BufFile *bufFile = new BufFile(buffer.data(), fileSize);
        StandardStream stream(bufFile, 0.0f, 0.0f, "mogg", false, true, false);

        int polls = 0;
        bool ready = false, failed = false;
        while (polls < 500) {
            stream.PollStream();
            polls++;
            if (stream.IsReady()) { ready = true; break; }
            if (stream.Fail()) { failed = true; break; }
        }

        printf("  result: %s (polls=%d)\n",
               ready ? "READY" : (failed ? "FAILED" : "TIMEOUT"), polls);

        if (ready) {
            printf("  channels=%d, sampleRate=%d\n",
                   stream.GetNumChannels(), (int)stream.GetSampleRate());
        }
    }
}
