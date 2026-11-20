// Color: an RGBA colour value.
//
// Rules:
//  - Components are 0..1 floats, sRGB-encoded (the numbers you would read off
//    a screenshot), with straight (not premultiplied) alpha.
//  - Colours are written down in Theme.cpp only; widgets ask the theme.
#pragma once

#include <cstdint>

namespace nativeui {

    struct Color {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 1.0f;

        // 0xRRGGBB, as sampled from an image.
        static constexpr Color FromRgb8(std::uint32_t rgb, float alpha = 1.0f) {
            return { static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
                     static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
                     static_cast<float>(rgb & 0xFF) / 255.0f,
                     alpha };
        }
        constexpr Color WithAlpha(float alpha) const { return { r, g, b, alpha }; }
        // The same colour with its alpha multiplied by `opacity`.
        constexpr Color Faded(float opacity) const { return { r, g, b, a * opacity }; }
    };

    constexpr bool operator==(const Color &x, const Color &y) {
        return x.r == y.r && x.g == y.g && x.b == y.b && x.a == y.a;
    }
    constexpr bool operator!=(const Color &x, const Color &y) { return !(x == y); }

    // Component-wise blend: `from` at t = 0, `to` at t = 1.
    constexpr Color Mix(const Color &from, const Color &to, float t) {
        // Written so that t = 0 gives `from` and t = 1 gives `to` exactly.
        const float s = 1.0f - t;
        return { from.r * s + to.r * t,
                 from.g * s + to.g * t,
                 from.b * s + to.b * t,
                 from.a * s + to.a * t };
    }

}
