// png_read.h -- a minimal PNG reader for tests: 8-bit, non-interlaced RGB or
// RGBA (what the engine's screenshot path, stb_image_write, writes).
#pragma once

#include <zlib.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// An 8-bit, non-interlaced RGB or RGBA PNG (what stb_image_write writes) as
// RGB. False on anything else.
inline bool ReadPngRgb(const std::string& path, int& w, int& h, std::vector<uint8_t>& rgb) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    std::vector<uint8_t> b;
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) b.insert(b.end(), buf, buf + n);
    fclose(f);
    static const uint8_t kSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (b.size() < 8 || memcmp(b.data(), kSig, 8) != 0) return false;
    auto be32 = [&](size_t o) {
        return (uint32_t)b[o] << 24 | (uint32_t)b[o + 1] << 16 | (uint32_t)b[o + 2] << 8 | b[o + 3];
    };
    int channels = 0;
    std::vector<uint8_t> z;
    for (size_t o = 8; o + 12 <= b.size();) {
        const uint32_t len = be32(o);
        if (o + 12 + len > b.size()) return false;
        const char* type = (const char*)&b[o + 4];
        const uint8_t* d = &b[o + 8];
        if (!memcmp(type, "IHDR", 4)) {
            w = (int)be32(o + 8);
            h = (int)be32(o + 12);
            if (d[8] != 8 || d[12] != 0) return false;  // 8-bit, not interlaced
            channels = d[9] == 6 ? 4 : d[9] == 2 ? 3 : 0;
            if (!channels) return false;
        } else if (!memcmp(type, "IDAT", 4)) {
            z.insert(z.end(), d, d + len);
        } else if (!memcmp(type, "IEND", 4)) {
            break;
        }
        o += 12 + len;
    }
    if (!channels || w <= 0 || h <= 0) return false;
    const size_t stride = (size_t)w * channels;
    std::vector<uint8_t> raw((stride + 1) * h);
    uLongf rawLen = raw.size();
    if (uncompress(raw.data(), &rawLen, z.data(), z.size()) != Z_OK || rawLen != raw.size())
        return false;
    std::vector<uint8_t> px(stride * h);
    for (int y = 0; y < h; y++) {
        const uint8_t filter = raw[y * (stride + 1)];
        const uint8_t* in = &raw[y * (stride + 1) + 1];
        uint8_t* out = &px[y * stride];
        const uint8_t* up = y ? &px[(y - 1) * stride] : nullptr;
        for (size_t i = 0; i < stride; i++) {
            const int a = i >= (size_t)channels ? out[i - channels] : 0;
            const int u = up ? up[i] : 0;
            const int c = (up && i >= (size_t)channels) ? up[i - channels] : 0;
            int v = in[i];
            switch (filter) {
            case 0: break;
            case 1: v += a; break;
            case 2: v += u; break;
            case 3: v += (a + u) / 2; break;
            case 4: {
                const int p = a + u - c, pa = abs(p - a), pb = abs(p - u), pc = abs(p - c);
                v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? u : c);
                break;
            }
            default: return false;
            }
            out[i] = (uint8_t)v;
        }
    }
    rgb.resize((size_t)w * h * 3);
    for (size_t i = 0; i < (size_t)w * h; i++)
        for (int c = 0; c < 3; c++) rgb[i * 3 + c] = px[i * channels + c];
    return true;
}
