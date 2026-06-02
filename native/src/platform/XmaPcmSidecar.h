// XmaPcmSidecar.h — offline XMA->PCM sidecar bridge for the DC3 native/web port.
//
// DC3 decodes kXMA at load time on NATIVE (SampleData::Load, #ifdef HX_FFMPEG,
// via DecodeXMAToPCM). WEB has no runtime ffmpeg, so kXMA SFX are silent there.
// This bridge closes the web gap WITHOUT an emscripten ffmpeg port:
//
//   * On native (HX_FFMPEG): after a successful runtime decode, WriteSidecar()
//     persists the decoded PCM (keyed by a content hash of the raw XMA payload)
//     into RB3_SFX_PCM_DIR / DC3_SFX_PCM_DIR. A single native asset-prep run
//     thus GENERATES every web sidecar as a byproduct of DC3's own validated
//     decode — no separate milo parser or codec reimplementation.
//   * On web (HX_NATIVE && !HX_FFMPEG): TryLoad() reads the matching sidecar so
//     the existing PCM playback path Just Works.
//
// The key + file format are identical to RB3's native/src/rb3_xma_sidecar.h, so
// one shared sidecar directory serves both engines.
#pragma once

#if defined(HX_NATIVE)

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace dc3_xma {

// FNV-1a over the raw XMA payload + size + sample rate. MUST match RB3's
// rb3_xma::PayloadKey and the offline converter's milo_scan::payload_key.
inline uint64_t PayloadKey(const void *data, int sizeBytes, int sampleRate) {
    const uint8_t *p = static_cast<const uint8_t *>(data);
    uint64_t h = 1469598103934665603ULL;
    for (int i = 0; i < sizeBytes; i++) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    h ^= static_cast<uint64_t>(static_cast<uint32_t>(sizeBytes));
    h *= 1099511628211ULL;
    h ^= static_cast<uint64_t>(static_cast<uint32_t>(sampleRate));
    h *= 1099511628211ULL;
    return h;
}

inline std::string SidecarDir() {
    if (const char *d = getenv("DC3_SFX_PCM_DIR")) return std::string(d);
    if (const char *d = getenv("RB3_SFX_PCM_DIR")) return std::string(d);
    return std::string("sfx/gen/xma_pcm");
}

inline std::string SidecarPath(const void *xmaData, int sizeBytes, int sampleRate) {
    uint64_t key = PayloadKey(xmaData, sizeBytes, sampleRate);
    char keyHex[24];
    std::snprintf(keyHex, sizeof(keyHex), "%016llx",
                  static_cast<unsigned long long>(key));
    return SidecarDir() + "/" + keyHex + ".pcm";
}

inline void put_le32(FILE *f, uint32_t x) {
    uint8_t b[4] = {(uint8_t)(x & 0xff), (uint8_t)((x >> 8) & 0xff),
                    (uint8_t)((x >> 16) & 0xff), (uint8_t)((x >> 24) & 0xff)};
    std::fwrite(b, 1, 4, f);
}

// Persist decoded PCM as a web sidecar (native + HX_FFMPEG path). pcm is
// interleaved int16; numSamples is per-channel count.
inline bool WriteSidecar(const void *rawXma, int xmaSize, int sampleRate,
                         const void *pcm, int numSamples, int numChannels) {
    if (getenv("DC3_NO_SIDECAR_WRITE")) return false; // opt-out
    std::string path = SidecarPath(rawXma, xmaSize, sampleRate);
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return false; // dir may not exist (set DC3_SFX_PCM_DIR); silent no-op
    std::fwrite("RB3PCM01", 1, 8, f);
    put_le32(f, (uint32_t)sampleRate);
    put_le32(f, (uint32_t)numSamples);
    put_le32(f, (uint32_t)numChannels);
    put_le32(f, 0);
    std::fwrite(pcm, 1, (size_t)numSamples * numChannels * sizeof(int16_t), f);
    std::fclose(f);
    return true;
}

struct SidecarPCM {
    int16_t *data = nullptr;
    int numSamples = 0;
    int numChannels = 0;
    int sampleRate = 0;
    int byteSize = 0;
};

// Read a web sidecar for the given raw XMA payload (web path). data==nullptr if
// none found; caller free()s data.
inline SidecarPCM TryLoad(const void *xmaData, int sizeBytes, int sampleRate) {
    SidecarPCM out;
    if (!xmaData || sizeBytes <= 0) return out;
    std::string path = SidecarPath(xmaData, sizeBytes, sampleRate);
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return out;
    uint8_t hdr[24];
    if (std::fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr) ||
        std::memcmp(hdr, "RB3PCM01", 8) != 0) {
        std::fclose(f);
        return out;
    }
    auto le32 = [](const uint8_t *p) -> int {
        return (int)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
    };
    int sr = le32(hdr + 8), ns = le32(hdr + 12), ch = le32(hdr + 16);
    if (ns <= 0 || ch <= 0 || ch > 8) { std::fclose(f); return out; }
    int bytes = ns * ch * (int)sizeof(int16_t);
    void *pcm = std::malloc(bytes);
    if (!pcm) { std::fclose(f); return out; }
    if ((int)std::fread(pcm, 1, bytes, f) != bytes) {
        std::free(pcm);
        std::fclose(f);
        return out;
    }
    std::fclose(f);
    out.data = (int16_t *)pcm;
    out.numSamples = ns;
    out.numChannels = ch;
    out.sampleRate = sr;
    out.byteSize = bytes;
    return out;
}

} // namespace dc3_xma

#endif // HX_NATIVE
