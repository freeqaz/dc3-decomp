// DC3 Native Port - NativeSynth
// Platform-specific Synth subclass for Linux/macOS/Windows.
// Replaces system/synth_xbox/Synth.cpp (Synth360).
//
// Responsibilities:
// - Register StreamReceiverNative factory for audio output
// - Initialize/terminate AudioDevice (miniaudio)
// - Create stream decoders for audio formats (Vorbis, FFmpeg/Bink)

#include "synth/Synth.h"
#include "synth/StandardStream.h"
#include "synth/StreamReceiver.h"
#include "synth/StreamNull.h"
#include "synth/VorbisReader.h"
#include "os/File.h"
#include "utl/Symbol.h"
#include "audio/AudioDevice.h"
#include "platform/StreamReceiver_Native.h"

#ifdef HX_FFMPEG
#include "platform/FFmpegAudioReader.h"
#endif

extern File *NewFile(const char *, int);

class NativeSynth : public Synth {
public:
    virtual void Init() override {
        Synth::Init();

        // Register native StreamReceiver factory (like StreamReceiver360::Init())
        StreamReceiver::sFactory = StreamReceiverNative::Create;

        // Initialize audio output device
        AudioDevice::GetInstance().Init(44100);
    }

    virtual void Terminate() override {
        AudioDevice::GetInstance().Terminate();
        Synth::Terminate();
    }

    virtual StreamReader *NewStreamDecoder(File *file, StandardStream *stream, Symbol type) override {
#ifdef HX_FFMPEG
        // "bink" symbol = Bink audio container (.bik files used for song previews)
        if (type == "bink") {
            return new FFmpegAudioReader(file, stream);
        }
#endif
        // Vorbis/OGG/MOGG — the primary song audio format
        if (type == "ogg" || type == "mogg") {
            return new VorbisReader(file, false, stream, false);
        }
        return nullptr;
    }

    virtual void NewStreamFile(const char *path, File *&file, Symbol &sym) override {
        // Determine codec type from file extension
        const char *ext = strrchr(path, '.');
        if (ext) {
            if (strcmp(ext, ".bik") == 0) {
                sym = "bink";
            } else if (strcmp(ext, ".mogg") == 0) {
                sym = "mogg";
            } else if (strcmp(ext, ".ogg") == 0) {
                sym = "ogg";
            } else {
                sym = "ogg"; // default to ogg
            }
        } else {
            sym = "ogg";
        }
        file = NewFile(path, 2); // read mode
    }

    virtual Stream *NewStream(const char *path, float vol, float pan, bool loop) override {
        File *file = nullptr;
        Symbol sym;
        NewStreamFile(path, file, sym);
        if (!file) {
            return new StreamNull(vol);
        }
        return new StandardStream(file, vol, pan, sym, loop, true, false);
    }
};

// Called from SynthPreInit() via Synth::New()
// This replaces the Xbox360's "new Synth360()" path
Synth *CreateNativeSynth() {
    return new NativeSynth();
}
