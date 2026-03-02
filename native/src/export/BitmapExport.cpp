// BitmapExport — CPU-only texture decode from RndBitmap to RGBA8 buffer.
// Reuses the Xbox 360 decode functions from TextureConvert (byte swap, untile, DXT decompress).

#include "export/BitmapExport.h"
#include "gfx/TextureConvert.h"
#include "rndobj/Bitmap.h"

#include <cstring>

namespace BitmapExport {

std::vector<uint8_t> ToRGBA(const RndBitmap& bmp) {
    int w = bmp.Width();
    int h = bmp.Height();
    if (w == 0 || h == 0 || !bmp.Pixels())
        return {};

    unsigned int order = bmp.Order();
    unsigned int dxt = order & 0x38;
    int bpp = bmp.Bpp();
    int pixelBytes = bmp.PixelBytes();
    const uint8_t* srcPixels = bmp.Pixels();

    // Working copy for in-place transforms
    std::vector<uint8_t> workBuf(srcPixels, srcPixels + pixelBytes);
    uint8_t* workData = workBuf.data();

    // Step 1: Untile if needed (mOrder & 4)
    uint8_t* untiled = nullptr;
    if (order & 4) {
        untiled = TextureConvert::UntileMilo(bmp);
        workData = untiled;
    }

    // Step 2: Byte-swap DXT data from Xbox BE
    if (dxt) {
        TextureConvert::ByteSwapDXT(workData, pixelBytes);
    }

    // Step 3: Decode to RGBA8
    std::vector<uint8_t> rgba(w * h * 4);

    if (dxt) {
        // DXT compressed
        switch (dxt) {
        case kDXT1: TextureConvert::DecompressDXT1(workData, rgba.data(), w, h); break;
        case kDXT3: TextureConvert::DecompressDXT3(workData, rgba.data(), w, h); break;
        case kDXT5: TextureConvert::DecompressDXT5(workData, rgba.data(), w, h); break;
        default:
            // Unknown DXT format — fill magenta for visibility
            for (int i = 0; i < w * h; i++) {
                rgba[i * 4 + 0] = 255;
                rgba[i * 4 + 1] = 0;
                rgba[i * 4 + 2] = 255;
                rgba[i * 4 + 3] = 255;
            }
            break;
        }
    } else if (bpp == 32) {
        // 32-bit uncompressed
        memcpy(rgba.data(), workData, w * h * 4);
        if (!(order & 1)) {
            // BGRA → RGBA
            TextureConvert::SwapBGRAtoRGBA(rgba.data(), w, h);
        }
    } else if (bpp == 24) {
        // 24-bit RGB → RGBA
        for (int i = 0; i < w * h; i++) {
            if (order & 1) {
                rgba[i * 4 + 0] = workData[i * 3 + 0];
                rgba[i * 4 + 1] = workData[i * 3 + 1];
                rgba[i * 4 + 2] = workData[i * 3 + 2];
            } else {
                rgba[i * 4 + 0] = workData[i * 3 + 2]; // B→R
                rgba[i * 4 + 1] = workData[i * 3 + 1]; // G
                rgba[i * 4 + 2] = workData[i * 3 + 0]; // R→B
            }
            rgba[i * 4 + 3] = 0xFF;
        }
    } else if (bpp == 8 || bpp == 4) {
        // Palette-indexed: expand via PixelColor
        for (int py = 0; py < h; py++) {
            for (int px = 0; px < w; px++) {
                uint8_t r, g, b, a;
                bmp.PixelColor(px, py, r, g, b, a);
                int idx = (py * w + px) * 4;
                rgba[idx + 0] = r;
                rgba[idx + 1] = g;
                rgba[idx + 2] = b;
                rgba[idx + 3] = a;
            }
        }
    } else {
        // Unsupported bpp — fill magenta
        for (int i = 0; i < w * h; i++) {
            rgba[i * 4 + 0] = 255;
            rgba[i * 4 + 1] = 0;
            rgba[i * 4 + 2] = 255;
            rgba[i * 4 + 3] = 255;
        }
    }

    delete[] untiled;
    return rgba;
}

} // namespace BitmapExport
