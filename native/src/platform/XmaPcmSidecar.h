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
#include <sys/stat.h> // mkdir (native sidecar-write path)
#include <vector>     // ogg read-into-memory buffer

// Compact vorbis SFX sidecars (port of rb3's W5-T2): scripts/web/gen_sfx_ogg.py
// emits a <hexkey>.ogg next to each <hexkey>.pcm (~12% of the raw PCM bytes),
// same key. TryLoad tries the .ogg first, decoding with vendored stb_vorbis
// into the identical SidecarPCM contract, and falls back to .pcm on miss.
//
// stb_vorbis.h is header-only by default; the SINGLE TU that defines
// DC3_STB_VORBIS_IMPL (StbVorbisImpl.cpp) compiles the implementation so the
// decoder symbols link exactly once. Only the pulldata open-memory API is used
// (we read the file/MEMFS bytes ourselves), so push-data + the stdio file API
// are compiled out to keep the impl lean.
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_STDIO
#ifndef DC3_STB_VORBIS_IMPL
#define STB_VORBIS_HEADER_ONLY
#endif
#include "stb_vorbis.h"
#ifdef STB_VORBIS_HEADER_ONLY
#undef STB_VORBIS_HEADER_ONLY
#endif

#ifdef __EMSCRIPTEN__
// Web: a sidecar MEMFS miss is filled by one synchronous JSPI fetch into MEMFS
// (WebAssetsFetchSync), the same miss-then-retry ordering AsyncFile_Native and
// native_file.cpp use. Compiled out on non-web builds.
#include "platform/WebAssets.h"
#endif

namespace dc3_xma {

// Recursively create the parent directories of `path` (mkdir -p over the
// directory components; the trailing filename component is not created). POSIX;
// used by the native asset-prep write path (HX_FFMPEG). EEXIST is ignored.
inline void MakeParentDirs(const std::string &path) {
    for (size_t i = 1; i < path.size(); ++i) {
        if (path[i] == '/') {
            std::string sub = path.substr(0, i);
            if (!sub.empty()) mkdir(sub.c_str(), 0755); // ignore errors (EEXIST)
        }
    }
}

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

inline std::string KeyHex(const void *xmaData, int sizeBytes, int sampleRate) {
    uint64_t key = PayloadKey(xmaData, sizeBytes, sampleRate);
    char keyHex[24];
    std::snprintf(keyHex, sizeof(keyHex), "%016llx",
                  static_cast<unsigned long long>(key));
    return std::string(keyHex);
}

inline std::string SidecarPath(const void *xmaData, int sizeBytes, int sampleRate) {
    return SidecarDir() + "/" + KeyHex(xmaData, sizeBytes, sampleRate) + ".pcm";
}

// Prefer the compact <hex>.ogg sidecar (default ON). DC3_SFX_OGG_OFF=1 forces
// the legacy raw .pcm path (A/B arm — reachable in-browser via ?env=).
inline bool SfxOggEnabled() {
    static int sEnabled = -1;
    if (sEnabled < 0) {
        sEnabled = 1; // default ON
        if (const char *e = getenv("DC3_SFX_OGG_OFF"))
            if (e[0] && e[0] != '0')
                sEnabled = 0;
    }
    return sEnabled != 0;
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
    // Ensure the sidecar directory exists before opening for write — a fresh
    // native run has no sfx/gen/xma_pcm/ tree, so fopen(wb) would otherwise fail
    // and no sidecars would ever be produced.
    MakeParentDirs(path);
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            std::fprintf(stderr,
                         "[dc3_xma] WriteSidecar: cannot open '%s' for write "
                         "(set DC3_SFX_PCM_DIR to a writable dir)\n",
                         path.c_str());
        }
        return false;
    }
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

// Open a sidecar file, returning a FILE* (caller fclose's) or null.
// On web, a MEMFS miss triggers one synchronous XHR into MEMFS and a retry —
// the same miss-then-retry ordering AsyncFile_Native / native_file.cpp use.
inline FILE *OpenSidecarFile(const std::string &path) {
#ifdef __EMSCRIPTEN__
    // WebAssetsFetchSync writes bytes to its argument path VERBATIM
    // (FS.writeFile(memfsPath, ...)) and the default SidecarDir() is relative
    // ("sfx/gen/xma_pcm"). DC3's web boot does NOT chdir("/data") (unlike RB3),
    // so Emscripten's cwd stays "/". A relative open would therefore resolve to
    // "/sfx/..." while the fetch writes the ABSOLUTE "/data/sfx/...". Anchor
    // BOTH the fetch and every fopen under /data so they agree on one location.
    std::string openPath = path;
    if (!openPath.empty() && openPath[0] != '/')
        openPath = "/data/" + openPath;
    FILE *f = std::fopen(openPath.c_str(), "rb");
    if (!f) {
        // MEMFS miss: the sidecar has not been fetched yet (or the async bundle
        // prefetch hasn't landed it). Pull it from the dev server into MEMFS
        // with one synchronous JSPI fetch, then retry the open at the SAME
        // absolute path. A 404 (no sidecar of this flavor for this key) returns
        // false → f stays null → the caller falls through to the next flavor.
        if (WebAssetsFetchSync(openPath.c_str()))
            f = std::fopen(openPath.c_str(), "rb");
    }
    return f;
#else
    return std::fopen(path.c_str(), "rb");
#endif
}

// Read an entire open file into a byte buffer (whole-stream ogg decode).
inline bool ReadAllBytes(FILE *f, std::vector<uint8_t> &buf) {
    if (std::fseek(f, 0, SEEK_END) != 0) return false;
    long sz = std::ftell(f);
    if (std::fseek(f, 0, SEEK_SET) != 0) return false;
    if (sz <= 0) return false;
    buf.resize(static_cast<size_t>(sz));
    return std::fread(buf.data(), 1, buf.size(), f) == buf.size();
}

// Decode a whole in-memory Ogg/Vorbis stream into the SidecarPCM contract
// (16-bit interleaved malloc'd buffer; caller free()s). Empty SidecarPCM on any
// decode failure so the caller falls back to .pcm. The vorbis sidecar carries
// no RB3PCM01 header — channels/rate come from the stream itself (the encoder
// wrote the SAME per-channel sample count / rate / channels as the .pcm).
inline SidecarPCM DecodeOggBuffer(const uint8_t *bytes, int len) {
    SidecarPCM out;
    if (!bytes || len <= 0) return out;
    int err = 0;
    stb_vorbis *v = stb_vorbis_open_memory(bytes, len, &err, nullptr);
    if (!v) return out;
    stb_vorbis_info info = stb_vorbis_get_info(v);
    int ch = info.channels;
    int sr = static_cast<int>(info.sample_rate);
    if (ch <= 0 || ch > 8 || sr <= 0) {
        stb_vorbis_close(v);
        return out;
    }
    unsigned int perChan = stb_vorbis_stream_length_in_samples(v);
    if (perChan == 0 || perChan > 0x7fffffffu / static_cast<unsigned int>(ch)) {
        stb_vorbis_close(v);
        return out;
    }
    int total = static_cast<int>(perChan) * ch; // total interleaved shorts
    int16_t *pcm = static_cast<int16_t *>(std::malloc(total * sizeof(int16_t)));
    if (!pcm) {
        stb_vorbis_close(v);
        return out;
    }
    // get_samples_short_interleaved fills up to num_shorts and returns the
    // per-channel frame count written; it may stop a frame or two short of the
    // reported stream length, so zero-pad any tail and trust the written count.
    int framesWritten = stb_vorbis_get_samples_short_interleaved(v, ch, pcm, total);
    stb_vorbis_close(v);
    if (framesWritten <= 0) {
        std::free(pcm);
        return out;
    }
    if (framesWritten < static_cast<int>(perChan)) {
        int writtenShorts = framesWritten * ch;
        std::memset(pcm + writtenShorts, 0, (total - writtenShorts) * sizeof(int16_t));
    }
    out.data = pcm;
    out.numSamples = static_cast<int>(perChan); // per-channel count
    out.numChannels = ch;
    out.sampleRate = sr;
    out.byteSize = total * static_cast<int>(sizeof(int16_t));
    return out;
}

// Try the compact <hex>.ogg vorbis sidecar. Empty SidecarPCM on miss or decode
// failure (caller falls back to .pcm).
inline SidecarPCM TryLoadOgg(const std::string &keyHex) {
    SidecarPCM out;
    FILE *f = OpenSidecarFile(SidecarDir() + "/" + keyHex + ".ogg");
    if (!f) return out;
    std::vector<uint8_t> bytes;
    bool ok = ReadAllBytes(f, bytes);
    std::fclose(f);
    if (!ok) return out;
    return DecodeOggBuffer(bytes.data(), static_cast<int>(bytes.size()));
}

// Read a web sidecar for the given raw XMA payload (web path). data==nullptr if
// none found; caller free()s data. Prefers the compact <hex>.ogg (default ON,
// ~12% of the bytes on the wire); miss/decode-fail falls through to the legacy
// raw <hex>.pcm. DC3_SFX_OGG_OFF=1 skips the .ogg attempt entirely (A/B arm —
// byte-identical to the pre-ogg path).
inline SidecarPCM TryLoad(const void *xmaData, int sizeBytes, int sampleRate) {
    SidecarPCM out;
    if (!xmaData || sizeBytes <= 0) return out;

    std::string keyHex = KeyHex(xmaData, sizeBytes, sampleRate);
    if (SfxOggEnabled()) {
        SidecarPCM ogg = TryLoadOgg(keyHex);
        if (ogg.data) return ogg;
        // miss/decode-fail → fall through to the raw .pcm sidecar below.
    }

    std::string path = SidecarDir() + "/" + keyHex + ".pcm";
    FILE *f = OpenSidecarFile(path);
    if (!f) {
        static bool warnedMiss = false;
        if (!warnedMiss) {
            warnedMiss = true;
            std::fprintf(stderr,
                         "[dc3_xma] TryLoad: no sidecar on server for '%s' "
                         "(kXMA SFX will be silent)\n",
                         path.c_str());
        }
        return out;
    }
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
