// dc3_xma_sidecars.cpp -- derive the web build's kXMA SFX sidecars from the
// asset tree, not from whatever one native session happened to load.
//
// The web build has no FFmpeg, so SampleData::Load (src/system/synth/
// SampleData.cpp, the HX_NATIVE && !HX_FFMPEG branch) swaps every kXMA sample
// for a pre-decoded sidecar fetched from <assets>/sfx/gen/xma_pcm/<key>.{ogg,pcm}
// (native/src/platform/XmaPcmSidecar.h). Until now those sidecars were a side
// effect of a desktop run (WriteSidecar after DecodeXMAToPCM), so the set only
// covered the banks that run loaded; a full song on the web 404'd on the rest.
//
// This tool walks every .milo_xbox under the given roots, inflates the
// ChunkStream container exactly as src/system/utl/ChunkStream.cpp does
// (0xCABEDEAF stored, 0xCBBEDEAF raw deflate, 0xCCBEDEAF gzip member,
// 0xCDBEDEAF size-prefixed raw deflate; a chunk flagged 0x01000000 is stored),
// finds each SampleData blob in the object stream, and for every kXMA payload
// computes the runtime key (dc3_xma::PayloadKey: FNV-1a over the payload,
// mixed with size and sample rate). With --out it decodes each key that has no
// sidecar yet through the same DecodeXMAToPCM the desktop build uses and
// writes <key>.pcm in the RB3PCM01 format WriteSidecar writes;
// scripts/web/gen_sfx_ogg.py then encodes the .ogg twins.
//
// SampleData layout (DC3, rev 0x10, see SampleData::Load):
//   int rev | int mCRC (rev > 0xE) | int fmt | int numSamples | int sampleRate
//   | int sizeBytes | bool hasData (rev >= 0xB) | payload | markers (rev >= 0xE:
//   int count, count x {String, int}) | int numChannels (rev >= 0x10)
// The scan accepts a candidate only if every field parses, so a false positive
// needs a self-consistent header, an in-bounds payload, parseable markers and a
// 1..8 channel count. Lower SampleData revs are counted (--list prints them)
// but never seen in DC3's data.
//
// usage: dc3-xma-sidecars [--locale L] [--out DIR] [--manifest FILE] [--force]
//                         ROOT|FILE...
//   --locale : skip containers under loc/<other>/ (the game loads one language;
//              the other 14 hold ~28k keys / ~0.9 GB of XMA it never asks for)
//   no --out : scan only; print the summary (and the manifest if asked)
//   --out    : decode every key with no <key>.pcm/.ogg in DIR
//   --force  : re-decode keys that already have a sidecar
// Exit 0 on success, 1 on usage/IO error, 2 if any decode or container failed.

#include "platform/XmaSampleDecoder.h"

#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

uint32_t rd_le32(const uint8_t *p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
           (uint32_t(p[3]) << 24);
}
uint32_t rd_be32(const uint8_t *p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) |
           uint32_t(p[3]);
}

bool read_file(const fs::path &path, std::vector<uint8_t> &out) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(sz > 0 ? size_t(sz) : 0);
    bool ok = sz >= 0 && std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

// zlib inflate of a raw deflate stream (windowBits -MAX_WBITS), like
// DecompressMem in src/system/utl/Compress.cpp.
bool inflate_raw(const uint8_t *in, size_t inLen, std::vector<uint8_t> &out) {
    z_stream s{};
    if (inflateInit2(&s, -MAX_WBITS) != Z_OK) return false;
    s.next_in = const_cast<uint8_t *>(in);
    s.avail_in = uInt(inLen);
    uint8_t buf[1 << 16];
    int ret;
    do {
        s.next_out = buf;
        s.avail_out = sizeof(buf);
        ret = inflate(&s, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateEnd(&s);
            return false;
        }
        out.insert(out.end(), buf, buf + (sizeof(buf) - s.avail_out));
    } while (ret != Z_STREAM_END && s.avail_in > 0);
    inflateEnd(&s);
    return ret == Z_STREAM_END;
}

// The milo object stream: the concatenation of every chunk, inflated.
// Returns false (with a reason) if the container does not parse.
bool object_stream(const std::vector<uint8_t> &file, std::vector<uint8_t> &out,
                   const char **why) {
    if (file.size() < 16) { *why = "short file"; return false; }
    uint32_t id = rd_le32(file.data());
    if ((id & 0xf0ffffffu) != 0xC0BEDEAFu) {
        // Not a chunked container: ChunkStream reads the whole file raw.
        out = file;
        return true;
    }
    uint32_t infoSize = rd_le32(file.data() + 4);
    uint32_t numChunks = rd_le32(file.data() + 8);
    if (16 + size_t(numChunks) * 4 > file.size() || infoSize > file.size()) {
        *why = "bad chunk header";
        return false;
    }
    size_t pos = infoSize;
    for (uint32_t c = 0; c < numChunks; c++) {
        uint32_t v = rd_le32(file.data() + 16 + c * 4);
        size_t sz = v & 0x00ffffffu;
        if (pos + sz > file.size()) { *why = "chunk past EOF"; return false; }
        const uint8_t *p = file.data() + pos;
        if (id == 0xCABEDEAFu || (v & 0x01000000u)) {
            out.insert(out.end(), p, p + sz);
        } else if (id == 0xCDBEDEAFu) {
            if (sz < 4 || !inflate_raw(p + 4, sz - 4, out)) { *why = "inflate (CD)"; return false; }
        } else if (id == 0xCCBEDEAFu) {
            if (sz < 18 || !inflate_raw(p + 10, sz - 18, out)) { *why = "inflate (CC)"; return false; }
        } else if (id == 0xCBBEDEAFu) {
            if (!inflate_raw(p, sz, out)) { *why = "inflate (CB)"; return false; }
        } else {
            *why = "unknown chunk id";
            return false;
        }
        pos += sz;
    }
    return true;
}

struct Blob {
    int rev, fmt, numSamples, sampleRate, sizeBytes, numChannels;
    size_t payload; // offset into the object stream
};

// Strict SampleData rev-0x10 scan (layout in the header comment).
void scan(const std::vector<uint8_t> &s, std::vector<Blob> &out) {
    const uint8_t *p = s.data();
    size_t n = s.size();
    for (size_t i = 0; i + 29 < n; i++) {
        if (rd_be32(p + i) != 0x10) continue;
        size_t q = i + 8; // rev, mCRC
        int fmt = int(rd_be32(p + q));
        int ns = int(rd_be32(p + q + 4));
        int sr = int(rd_be32(p + q + 8));
        int sz = int(rd_be32(p + q + 12));
        if (fmt < 0 || fmt > 6) continue;
        if (ns <= 0 || ns > 200000000) continue;
        if (sr < 4000 || sr > 48000) continue;
        if (sz <= 0 || size_t(sz) > n) continue;
        size_t o = q + 16;
        if (p[o] != 1) continue; // hasData
        o += 1;
        if (o + size_t(sz) + 8 > n) continue;
        size_t payload = o;
        size_t a = o + size_t(sz);
        uint32_t markers = rd_be32(p + a);
        a += 4;
        if (markers > 4096) continue;
        bool ok = true;
        for (uint32_t m = 0; m < markers && ok; m++) {
            if (a + 4 > n) { ok = false; break; }
            uint32_t len = rd_be32(p + a);
            a += 4;
            if (len > 1024 || a + len + 4 > n) { ok = false; break; }
            a += len + 4;
        }
        if (!ok || a + 4 > n) continue;
        int ch = int(rd_be32(p + a));
        a += 4;
        if (ch < 1 || ch > 8) continue;
        out.push_back({0x10, fmt, ns, sr, sz, ch, payload});
        i = a - 1; // never rescan payload bytes
    }
}

// dc3_xma::PayloadKey (native/src/platform/XmaPcmSidecar.h), byte for byte.
// Evidence they agree: every key a desktop run wrote through PayloadKey is in
// this tool's set (scripts/web/gen_sfx_sidecars.sh --check).
uint64_t payload_key(const uint8_t *d, int size, int rate) {
    uint64_t h = 1469598103934665603ULL;
    for (int i = 0; i < size; i++) {
        h ^= d[i];
        h *= 1099511628211ULL;
    }
    h ^= uint64_t(uint32_t(size));
    h *= 1099511628211ULL;
    h ^= uint64_t(uint32_t(rate));
    h *= 1099511628211ULL;
    return h;
}

struct KeyInfo {
    std::string milo; // first container seen (relative to its root)
    int uses = 0;
    Blob blob{};
    std::vector<uint8_t> xma; // payload copy, only kept when it may be decoded
};

void put_le32(std::vector<uint8_t> &v, uint32_t x) {
    for (int i = 0; i < 4; i++) v.push_back(uint8_t(x >> (8 * i)));
}

} // namespace

int main(int argc, char **argv) {
    std::string outDir, manifestPath, locale;
    bool force = false;
    std::vector<std::string> roots;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--out" && i + 1 < argc) outDir = argv[++i];
        else if (a == "--manifest" && i + 1 < argc) manifestPath = argv[++i];
        else if (a == "--locale" && i + 1 < argc) locale = argv[++i];
        else if (a == "--force") force = true;
        else if (a == "-h" || a == "--help") roots.clear(), argc = 0;
        else roots.push_back(a);
    }
    if (roots.empty()) {
        std::fprintf(stderr,
                     "usage: %s [--locale L] [--out DIR] [--manifest FILE] [--force] "
                     "ROOT|FILE...\n",
                     argc ? argv[0] : "dc3-xma-sidecars");
        return 1;
    }

    std::vector<std::pair<fs::path, fs::path>> files; // (file, root)
    int skippedLocale = 0;
    for (const auto &r : roots) {
        fs::path root(r);
        std::error_code ec;
        if (fs::is_regular_file(root, ec)) {
            files.push_back({root, root.parent_path()});
            continue;
        }
        for (auto it = fs::recursive_directory_iterator(
                 root, fs::directory_options::follow_directory_symlink, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec) || it->path().extension() != ".milo_xbox")
                continue;
            if (!locale.empty()) {
                bool other = false, prevLoc = false;
                for (const auto &part : fs::relative(it->path(), root)) {
                    if (prevLoc && part != locale) other = true;
                    prevLoc = part == "loc";
                }
                if (other) {
                    skippedLocale++;
                    continue;
                }
            }
            files.push_back({it->path(), root});
        }
    }
    std::sort(files.begin(), files.end());

    std::map<uint64_t, KeyInfo> keys;
    int containers = 0, badContainers = 0, blobs = 0, xmaBlobs = 0;
    std::map<int, int> byFormat;
    for (const auto &[path, root] : files) {
        std::vector<uint8_t> raw, stream;
        const char *why = "read";
        if (!read_file(path, raw) || !object_stream(raw, stream, &why)) {
            std::fprintf(stderr, "BAD container (%s): %s\n", why, path.c_str());
            badContainers++;
            continue;
        }
        containers++;
        std::vector<Blob> found;
        scan(stream, found);
        for (const Blob &b : found) {
            blobs++;
            byFormat[b.fmt]++;
            if (b.fmt != 3) continue; // kXMA
            xmaBlobs++;
            uint64_t k = payload_key(stream.data() + b.payload, b.sizeBytes, b.sampleRate);
            KeyInfo &ki = keys[k];
            if (ki.uses++ == 0) {
                ki.milo = fs::relative(path, root).string();
                ki.blob = b;
                if (!outDir.empty())
                    ki.xma.assign(stream.data() + b.payload,
                                  stream.data() + b.payload + b.sizeBytes);
            }
        }
    }

    int have = 0, decoded = 0, failed = 0;
    long long pcmBytes = 0;
    for (auto &[k, ki] : keys) {
        char hex[24];
        std::snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)k);
        if (outDir.empty()) continue;
        fs::path pcm = fs::path(outDir) / (std::string(hex) + ".pcm");
        fs::path ogg = fs::path(outDir) / (std::string(hex) + ".ogg");
        if (!force && (fs::exists(pcm) || fs::exists(ogg))) {
            have++;
            continue;
        }
        const Blob &b = ki.blob;
        void *out = nullptr;
        int outSize = 0;
        if (!DecodeXMAToPCM(ki.xma.data(), b.sizeBytes, b.numSamples, b.sampleRate,
                            b.numChannels, &out, &outSize) ||
            !out || outSize <= 0) {
            std::fprintf(stderr, "FAIL decode %s (%s, %d B, %d ch @ %d Hz)\n", hex,
                         ki.milo.c_str(), b.sizeBytes, b.numChannels, b.sampleRate);
            std::free(out);
            failed++;
            continue;
        }
        // Same header SampleData::Load + WriteSidecar produce: per-channel count
        // is the decoded size, not the stored mNumSamples.
        int perChan = outSize / (2 * b.numChannels);
        std::vector<uint8_t> file = {'R', 'B', '3', 'P', 'C', 'M', '0', '1'};
        put_le32(file, uint32_t(b.sampleRate));
        put_le32(file, uint32_t(perChan));
        put_le32(file, uint32_t(b.numChannels));
        put_le32(file, 0);
        const uint8_t *pp = static_cast<const uint8_t *>(out);
        file.insert(file.end(), pp, pp + size_t(perChan) * b.numChannels * 2);
        std::free(out);
        fs::create_directories(outDir);
        fs::path tmp = pcm;
        tmp += ".tmp";
        FILE *f = std::fopen(tmp.c_str(), "wb");
        if (!f || std::fwrite(file.data(), 1, file.size(), f) != file.size()) {
            if (f) std::fclose(f);
            std::fprintf(stderr, "FAIL write %s\n", tmp.c_str());
            failed++;
            continue;
        }
        std::fclose(f);
        fs::rename(tmp, pcm);
        pcmBytes += (long long)file.size();
        decoded++;
    }

    if (!manifestPath.empty()) {
        FILE *m = std::fopen(manifestPath.c_str(), "w");
        if (!m) {
            std::fprintf(stderr, "cannot write %s\n", manifestPath.c_str());
            return 1;
        }
        std::fprintf(m,
                     "# kXMA SampleData sidecar keys derived from every .milo_xbox "
                     "(native/tools/xma_sidecars).\n"
                     "# key\tchannels\trate\tsamples\txma_bytes\tuses\tfirst_container\n");
        for (const auto &[k, ki] : keys)
            std::fprintf(m, "%016llx\t%d\t%d\t%d\t%d\t%d\t%s\n", (unsigned long long)k,
                         ki.blob.numChannels, ki.blob.sampleRate, ki.blob.numSamples,
                         ki.blob.sizeBytes, ki.uses, ki.milo.c_str());
        std::fclose(m);
    }

    std::printf("dc3-xma-sidecars: %d containers (%d bad), %d SampleData blobs", containers,
                badContainers, blobs);
    for (auto &[f, c] : byFormat) std::printf(" fmt%d=%d", f, c);
    std::printf(", %d kXMA -> %zu unique keys", xmaBlobs, keys.size());
    if (!locale.empty()) std::printf(" (locale %s: %d containers skipped)", locale.c_str(), skippedLocale);
    std::printf("\n");
    if (!outDir.empty())
        std::printf("  %s: %d already present, %d decoded (%.1f MB pcm), %d failed\n",
                    outDir.c_str(), have, decoded, pcmBytes / 1e6, failed);
    return (failed || badContainers) ? 2 : 0;
}
