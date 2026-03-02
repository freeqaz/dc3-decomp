// BitmapExport — CPU-only texture decode from RndBitmap to RGBA8 buffer.
// Refactored from TextureConvert::CreateFromBitmap() without any GPU dependency.

#pragma once

#include <cstdint>
#include <vector>

class RndBitmap;

namespace BitmapExport {

// Decode an Xbox 360 RndBitmap to a linear RGBA8 buffer (4 bytes/pixel).
// Handles: untiling (mOrder & 4), DXT decompression, BGRA→RGBA, palette expansion.
// Returns empty vector on failure (null pixels, zero dimensions).
std::vector<uint8_t> ToRGBA(const RndBitmap& bmp);

} // namespace BitmapExport
