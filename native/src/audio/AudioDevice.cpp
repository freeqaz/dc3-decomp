// DC3 Native Port - Audio Device implementation
// Uses miniaudio for cross-platform audio output.

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING   // we don't encode audio
#define MA_NO_GENERATION // we don't need built-in waveform generation
#include "audio/miniaudio.h"

#include "audio/AudioDevice.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>

static void MaDataCallback(ma_device *device, void *output, const void * /*input*/, ma_uint32 frameCount) {
    AudioDevice *ad = (AudioDevice *)device->pUserData;
    ad->MixSources((float *)output, (int)frameCount);
}

AudioDevice &AudioDevice::GetInstance() {
    static AudioDevice instance;
    return instance;
}

AudioDevice::AudioDevice() : mDevice(nullptr), mInitialized(false), mSampleRate(0) {}

AudioDevice::~AudioDevice() {
    Terminate();
}

bool AudioDevice::Init(int sampleRate) {
    if (mInitialized)
        return true;

    mDevice = new ma_device;

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2; // stereo
    config.sampleRate = (sampleRate > 0) ? (ma_uint32)sampleRate : 0; // 0 = device default
    config.dataCallback = MaDataCallback;
    config.pUserData = this;
    config.periodSizeInFrames = 512; // ~10ms at 48kHz — low latency for rhythm game

    // Suppress ALSA error spam during device enumeration (no sound card = ~30 lines of noise)
    int savedStderr = dup(STDERR_FILENO);
    int devNull = open("/dev/null", O_WRONLY);
    if (devNull >= 0) {
        dup2(devNull, STDERR_FILENO);
        close(devNull);
    }

    ma_result result = ma_device_init(nullptr, &config, mDevice);

    // Restore stderr
    if (savedStderr >= 0) {
        dup2(savedStderr, STDERR_FILENO);
        close(savedStderr);
    }

    if (result != MA_SUCCESS) {
        fprintf(stderr, "AudioDevice: ma_device_init failed: %d\n", result);
        delete mDevice;
        mDevice = nullptr;
        return false;
    }

    mSampleRate = (int)mDevice->sampleRate;

    result = ma_device_start(mDevice);
    if (result != MA_SUCCESS) {
        fprintf(stderr, "AudioDevice: ma_device_start failed: %d\n", result);
        ma_device_uninit(mDevice);
        delete mDevice;
        mDevice = nullptr;
        return false;
    }

    mInitialized = true;
    printf("AudioDevice: initialized — %d Hz, %d channels, period %d frames\n",
           mSampleRate, 2, 512);
    return true;
}

void AudioDevice::Terminate() {
    if (!mInitialized)
        return;

    ma_device_uninit(mDevice);
    delete mDevice;
    mDevice = nullptr;
    mInitialized = false;
    mSampleRate = 0;

    std::lock_guard<std::mutex> lock(mSourceMutex);
    mSources.clear();
}

void AudioDevice::AddSource(AudioSource *source) {
    std::lock_guard<std::mutex> lock(mSourceMutex);
    mSources.push_back(source);
}

void AudioDevice::RemoveSource(AudioSource *source) {
    std::lock_guard<std::mutex> lock(mSourceMutex);
    mSources.erase(
        std::remove(mSources.begin(), mSources.end(), source),
        mSources.end()
    );
}

void AudioDevice::MixSources(float *output, int frameCount) {
    int totalSamples = frameCount * 2; // stereo
    memset(output, 0, totalSamples * sizeof(float));

    std::lock_guard<std::mutex> lock(mSourceMutex);

    if (mSources.empty())
        return;

    // Ensure temp buffer is large enough
    if ((int)mMixBuffer.size() < totalSamples) {
        mMixBuffer.resize(totalSamples);
    }

    for (auto it = mSources.begin(); it != mSources.end(); ) {
        AudioSource *src = *it;
        memset(mMixBuffer.data(), 0, totalSamples * sizeof(float));

        int framesWritten = src->RenderAudio(mMixBuffer.data(), frameCount);

        // Additive mix into output
        int samplesToMix = framesWritten * 2;
        for (int i = 0; i < samplesToMix; i++) {
            output[i] += mMixBuffer[i];
        }

        // Remove finished sources
        if (src->IsFinished()) {
            it = mSources.erase(it);
        } else {
            ++it;
        }
    }

    // Clamp output to [-1, 1]
    for (int i = 0; i < totalSamples; i++) {
        if (output[i] > 1.0f) output[i] = 1.0f;
        else if (output[i] < -1.0f) output[i] = -1.0f;
    }
}
