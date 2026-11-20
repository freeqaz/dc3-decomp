// A small software rasteriser for nativeui display lists, and a PNG writer.
//
// TEST TOOL ONLY. It exists so a person can look at a layout without a GPU
// (see SongBrowserUi.Pictures). It is not the game's
// renderer and nothing outside native/tests may include it.
//
// What it does and does not do:
//  - Rounded rectangles are a signed distance test per pixel, with the list's
//    softness as a linear alpha ramp centred on the edge.
//  - Images are not loaded: a handle is drawn as a two-tone gradient made from
//    its id, so different "album art" looks different.
//  - Text is a 5x7 dot font, capitals, digits and a little punctuation, spaced
//    the way FixedAdvanceMeasurer measures (half the size per glyph). Accented
//    capitals are drawn as their base letter with a mark above.
//  - Colours are blended as stored (no gamma handling).
#pragma once

#include "nativeui/DisplayList.h"
#include "nativeui/FixedAdvanceMeasurer.h"
#include "nativeui/Renderer.h"
#include "nativeui/Utf8.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace nativeui_test {

    using namespace nativeui;

    class Raster final : public Renderer {
    public:
        Raster(int width, int height)
            : width_(width), height_(height),
              pixels_(static_cast<std::size_t>(width) * height, Color { 0, 0, 0, 1 }) {
            clips_.push_back(
                { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height) }
            );
        }

        int Width() const { return width_; }
        int Height() const { return height_; }
        Color At(int x, int y) const { return pixels_[Index(x, y)]; }

        void Draw(const RoundRectFill &fill) override {
            const float ramp = std::max(fill.softness, 1.0f);
            ForEachPixel(fill.rect.Inflated(ramp), [&](int x, int y, Vec2 p) {
                const float distance = Distance(p, fill.rect, fill.radius);
                Blend(x, y, ColorAt(fill.colors, fill.rect, p), Ramp(-distance, ramp));
            });
        }

        void Draw(const RoundRectStroke &stroke) override {
            const float ramp = std::max(stroke.softness, 1.0f);
            ForEachPixel(stroke.rect.Inflated(ramp), [&](int x, int y, Vec2 p) {
                const float distance = Distance(p, stroke.rect, stroke.radius);
                const float coverage =
                    Ramp(-distance, ramp) * Ramp(distance + stroke.thickness, ramp);
                Blend(x, y, ColorAt(stroke.colors, stroke.rect, p), coverage);
            });
        }

        void Draw(const ImageQuad &image) override {
            // Two colours from the id; the picture is a diagonal blend of them.
            const float hue =
                std::fmod(static_cast<float>(image.image.id) * 0.61803f, 1.0f);
            const Color light = FromHue(hue, 0.95f);
            const Color dark = FromHue(std::fmod(hue + 0.12f, 1.0f), 0.35f);
            ForEachPixel(image.rect.Inflated(1.0f), [&](int x, int y, Vec2 p) {
                const float u = (p.x - image.rect.x) / std::max(image.rect.width, 1.0f);
                const float v = (p.y - image.rect.y) / std::max(image.rect.height, 1.0f);
                Color color =
                    Mix(light, dark, std::min(1.0f, std::max(0.0f, (u + v) * 0.5f)));
                color = { color.r * image.tint.r,
                          color.g * image.tint.g,
                          color.b * image.tint.b,
                          image.tint.a };
                Blend(x, y, color, Ramp(-Distance(p, image.rect, image.radius), 1.0f));
            });
        }

        void Draw(const TextRun &text) override {
            const float advance =
                FixedAdvanceMeasurer::kAdvance * text.size + text.letterSpacing;
            const float dotWidth =
                FixedAdvanceMeasurer::kAdvance * text.size * 0.8f / 5.0f;
            const float dotHeight = text.size * 0.7f / 7.0f;
            float x = text.origin.x;
            for (std::size_t offset = 0; offset < text.text.size();) {
                const utf8::Decoded decoded = utf8::Decode(text.text, offset);
                offset += decoded.length;
                bool accented = false;
                const std::uint8_t *rows = GlyphRows(decoded.codePoint, accented);
                for (int row = 0; rows && row < 7; ++row) {
                    for (int column = 0; column < 5; ++column) {
                        if (rows[row] & (0x10 >> column)) {
                            FillBox(
                                { x + column * dotWidth,
                                  text.origin.y - (7 - row) * dotHeight,
                                  dotWidth,
                                  dotHeight },
                                text.color
                            );
                        }
                    }
                }
                if (accented) {
                    FillBox(
                        { x + 2 * dotWidth,
                          text.origin.y - 8.5f * dotHeight,
                          dotWidth,
                          dotHeight },
                        text.color
                    );
                }
                x += advance;
            }
        }

        void PushClip(const Rect &rect) override {
            clips_.push_back(Intersect(rect, clips_.back()));
        }
        void PopClip() override {
            if (clips_.size() > 1) {
                clips_.pop_back();
            }
        }

        // The picture as 8-bit RGB rows, top to bottom.
        std::vector<unsigned char> Rgb8() const {
            std::vector<unsigned char> out;
            out.reserve(pixels_.size() * 3);
            for (const Color &pixel : pixels_) {
                for (const float channel : { pixel.r, pixel.g, pixel.b }) {
                    out.push_back(
                        static_cast<unsigned char>(
                            std::lround(std::min(1.0f, std::max(0.0f, channel)) * 255.0f)
                        )
                    );
                }
            }
            return out;
        }

    private:
        std::size_t Index(int x, int y) const {
            return static_cast<std::size_t>(y) * width_ + x;
        }

        // 0 at or below `-ramp/2`, 1 at or above `+ramp/2`.
        static float Ramp(float value, float ramp) {
            return std::min(1.0f, std::max(0.0f, 0.5f + value / ramp));
        }

        // Signed distance from `p` to the rounded rectangle's edge; negative inside.
        static float Distance(Vec2 p, const Rect &rect, float radius) {
            const float r = std::min(radius, std::min(rect.width, rect.height) * 0.5f);
            const float qx = std::fabs(p.x - rect.Center().x) - (rect.width * 0.5f - r);
            const float qy = std::fabs(p.y - rect.Center().y) - (rect.height * 0.5f - r);
            const float outside = std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f));
            return outside + std::min(std::max(qx, qy), 0.0f) - r;
        }

        static Color ColorAt(const CornerColors &colors, const Rect &rect, Vec2 p) {
            const float u = std::min(
                1.0f, std::max(0.0f, (p.x - rect.x) / std::max(rect.width, 1e-6f))
            );
            const float v = std::min(
                1.0f, std::max(0.0f, (p.y - rect.y) / std::max(rect.height, 1e-6f))
            );
            return Mix(
                Mix(colors.topLeft, colors.topRight, u),
                Mix(colors.bottomLeft, colors.bottomRight, u),
                v
            );
        }

        static Color FromHue(float hue, float value) {
            const auto channel = [&](float shift) {
                const float k = std::fmod(shift + hue * 6.0f, 6.0f);
                return value
                    * (1.0f - 0.65f * std::max(0.0f, std::min({ k, 4.0f - k, 1.0f })));
            };
            return { channel(5.0f), channel(3.0f), channel(1.0f), 1.0f };
        }

        void Blend(int x, int y, const Color &color, float coverage) {
            const float alpha = color.a * coverage;
            if (alpha <= 0.0f) {
                return;
            }
            Color &pixel = pixels_[Index(x, y)];
            pixel = { pixel.r + (color.r - pixel.r) * alpha,
                      pixel.g + (color.g - pixel.g) * alpha,
                      pixel.b + (color.b - pixel.b) * alpha,
                      1.0f };
        }

        // Calls `visit(x, y, centre)` for every pixel of `area` inside the clip.
        template <class Visit>
        void ForEachPixel(const Rect &area, Visit visit) {
            const Rect visible = Intersect(area, clips_.back());
            const int x0 = static_cast<int>(std::floor(visible.Left()));
            const int x1 = static_cast<int>(std::ceil(visible.Right()));
            const int y0 = static_cast<int>(std::floor(visible.Top()));
            const int y1 = static_cast<int>(std::ceil(visible.Bottom()));
            for (int y = std::max(0, y0); y < std::min(height_, y1); ++y) {
                for (int x = std::max(0, x0); x < std::min(width_, x1); ++x) {
                    const Vec2 centre { x + 0.5f, y + 0.5f };
                    if (clips_.back().Contains(centre)) {
                        visit(x, y, centre);
                    }
                }
            }
        }

        // A plain box, each pixel covered by the share of it the box overlaps.
        void FillBox(const Rect &box, const Color &color) {
            ForEachPixel(box, [&](int x, int y, Vec2) {
                const float overlapX =
                    std::min(box.Right(), x + 1.0f) - std::max(box.Left(), float(x));
                const float overlapY =
                    std::min(box.Bottom(), y + 1.0f) - std::max(box.Top(), float(y));
                Blend(x, y, color, std::max(0.0f, overlapX) * std::max(0.0f, overlapY));
            });
        }

        // Seven rows of five dots (bit 4 is the left column), or null for a blank.
        static const std::uint8_t *GlyphRows(char32_t codePoint, bool &accented) {
            static const std::uint8_t kLetters[26][7] = {
                { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 },
                { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E },
                { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E },
                { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E },
                { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F },
                { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 },
                { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F },
                { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 },
                { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E },
                { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C },
                { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 },
                { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F },
                { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 },
                { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 },
                { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E },
                { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 },
                { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D },
                { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 },
                { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E },
                { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 },
                { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E },
                { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 },
                { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 },
                { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 },
                { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 },
                { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F },
            };
            static const std::uint8_t kDigits[10][7] = {
                { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E },
                { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },
                { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F },
                { 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E },
                { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 },
                { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E },
                { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E },
                { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
                { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E },
                { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C },
            };
            struct Mark {
                char symbol;
                std::uint8_t rows[7];
            };
            static const Mark kMarks[] = {
                { '.', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C } },
                { ',', { 0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08 } },
                { '&', { 0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D } },
                { '(', { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 } },
                { ')', { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 } },
                { '-', { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 } },
                { '\'', { 0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00 } },
                { '!', { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 } },
                { '?', { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 } },
                { '/', { 0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10 } },
                { ':', { 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00 } },
            };
            static const std::uint8_t kUnknown[7] = { 0x1F, 0x11, 0x11, 0x11,
                                                      0x11, 0x11, 0x1F };

            // Latin-1 capitals with a mark: the base letter of each, from U+00C0.
            static const char kLatin1Base[] = "AAAAAA CEEEEIIIIDNOOOOO OUUUUY";
            if (codePoint >= 0xC0 && codePoint <= 0xDD
                && kLatin1Base[codePoint - 0xC0] != ' ') {
                accented = true;
                codePoint = static_cast<char32_t>(kLatin1Base[codePoint - 0xC0]);
            }
            if (codePoint >= U'a' && codePoint <= U'z') {
                codePoint -= U'a' - U'A';
            }
            if (codePoint >= U'A' && codePoint <= U'Z') {
                return kLetters[codePoint - U'A'];
            }
            if (codePoint >= U'0' && codePoint <= U'9') {
                return kDigits[codePoint - U'0'];
            }
            if (codePoint == U' ') {
                return nullptr;
            }
            for (const Mark &mark : kMarks) {
                if (codePoint == static_cast<char32_t>(mark.symbol)) {
                    return mark.rows;
                }
            }
            return kUnknown;
        }

        int width_;
        int height_;
        std::vector<Color> pixels_;
        std::vector<Rect> clips_;
    };

    // An uncompressed (stored deflate) 8-bit RGB PNG.
    inline std::vector<unsigned char>
    EncodePng(int width, int height, const std::vector<unsigned char> &rgb) {
        const auto append32 = [](std::vector<unsigned char> &out, std::uint32_t value) {
            for (int shift = 24; shift >= 0; shift -= 8) {
                out.push_back(static_cast<unsigned char>(value >> shift));
            }
        };
        const auto crc32 = [](const unsigned char *data, std::size_t size) {
            std::uint32_t crc = 0xFFFFFFFFu;
            for (std::size_t i = 0; i < size; ++i) {
                crc ^= data[i];
                for (int bit = 0; bit < 8; ++bit) {
                    crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
                }
            }
            return crc ^ 0xFFFFFFFFu;
        };
        std::vector<unsigned char> png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
        const auto chunk = [&](const char *type, const std::vector<unsigned char> &data) {
            append32(png, static_cast<std::uint32_t>(data.size()));
            const std::size_t start = png.size();
            png.insert(png.end(), type, type + 4);
            png.insert(png.end(), data.begin(), data.end());
            append32(png, crc32(png.data() + start, png.size() - start));
        };

        std::vector<unsigned char> header;
        append32(header, static_cast<std::uint32_t>(width));
        append32(header, static_cast<std::uint32_t>(height));
        header.insert(header.end(), { 8, 2, 0, 0, 0 }); // 8 bits, RGB, no interlace
        chunk("IHDR", header);

        // Scanlines, each led by filter type 0, in stored deflate blocks.
        std::vector<unsigned char> raw;
        const std::size_t stride = static_cast<std::size_t>(width) * 3;
        for (int y = 0; y < height; ++y) {
            raw.push_back(0);
            raw.insert(
                raw.end(), rgb.begin() + y * stride, rgb.begin() + (y + 1) * stride
            );
        }
        std::vector<unsigned char> zlib = { 0x78, 0x01 };
        std::uint32_t a = 1, b = 0; // Adler-32
        for (std::size_t at = 0; at < raw.size();) {
            const std::size_t count = std::min<std::size_t>(65535, raw.size() - at);
            zlib.push_back(at + count == raw.size() ? 1 : 0);
            zlib.push_back(static_cast<unsigned char>(count & 0xFF));
            zlib.push_back(static_cast<unsigned char>(count >> 8));
            zlib.push_back(static_cast<unsigned char>(~count & 0xFF));
            zlib.push_back(static_cast<unsigned char>((~count >> 8) & 0xFF));
            for (std::size_t i = at; i < at + count; ++i) {
                zlib.push_back(raw[i]);
                a = (a + raw[i]) % 65521u;
                b = (b + a) % 65521u;
            }
            at += count;
        }
        append32(zlib, (b << 16) | a);
        chunk("IDAT", zlib);
        chunk("IEND", {});
        return png;
    }

    inline bool
    WriteFile(const std::string &path, const std::vector<unsigned char> &bytes) {
        std::ofstream out(path, std::ios::binary);
        out.write(
            reinterpret_cast<const char *>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())
        );
        return static_cast<bool>(out);
    }

}
