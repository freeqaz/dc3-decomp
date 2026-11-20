// Geometry: positions, sizes and rectangles in the framework's logical space.
//
// Rules:
//  - Logical units, origin top-left, x right, y down, floats.
//  - Nothing here knows the size of the logical screen.
//  - Plain values: copy them, compare them; there are no invariants to keep.
#pragma once

#include <algorithm>

namespace nativeui {

    struct Vec2 {
        float x = 0.0f;
        float y = 0.0f;
    };

    inline bool operator==(Vec2 a, Vec2 b) { return a.x == b.x && a.y == b.y; }
    inline bool operator!=(Vec2 a, Vec2 b) { return !(a == b); }
    inline Vec2 operator+(Vec2 a, Vec2 b) { return { a.x + b.x, a.y + b.y }; }
    inline Vec2 operator-(Vec2 a, Vec2 b) { return { a.x - b.x, a.y - b.y }; }
    inline Vec2 operator*(Vec2 a, float s) { return { a.x * s, a.y * s }; }

    struct Size {
        float width = 0.0f;
        float height = 0.0f;
    };

    inline bool operator==(Size a, Size b) {
        return a.width == b.width && a.height == b.height;
    }
    inline bool operator!=(Size a, Size b) { return !(a == b); }

    // Space kept clear on each side of a rectangle.
    struct Insets {
        float left = 0.0f;
        float top = 0.0f;
        float right = 0.0f;
        float bottom = 0.0f;

        static Insets All(float amount) { return { amount, amount, amount, amount }; }
        static Insets Symmetric(float horizontal, float vertical) {
            return { horizontal, vertical, horizontal, vertical };
        }
        float Horizontal() const { return left + right; }
        float Vertical() const { return top + bottom; }
    };

    struct Rect {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;

        float Left() const { return x; }
        float Top() const { return y; }
        float Right() const { return x + width; }
        float Bottom() const { return y + height; }
        Vec2 Center() const { return { x + width * 0.5f, y + height * 0.5f }; }
        bool IsEmpty() const { return width <= 0.0f || height <= 0.0f; }

        bool Contains(Vec2 p) const {
            return p.x >= Left() && p.x < Right() && p.y >= Top() && p.y < Bottom();
        }
        Rect Translated(Vec2 delta) const {
            return { x + delta.x, y + delta.y, width, height };
        }
        // Grows every side by `amount`; a negative amount shrinks, never below zero size.
        Rect Inflated(float amount) const {
            return { x - amount,
                     y - amount,
                     std::max(0.0f, width + 2.0f * amount),
                     std::max(0.0f, height + 2.0f * amount) };
        }
        Rect Deflated(const Insets &insets) const {
            return { x + insets.left,
                     y + insets.top,
                     std::max(0.0f, width - insets.Horizontal()),
                     std::max(0.0f, height - insets.Vertical()) };
        }
    };

    inline bool operator==(const Rect &a, const Rect &b) {
        return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
    }
    inline bool operator!=(const Rect &a, const Rect &b) { return !(a == b); }

    // The overlap of two rectangles; a zero-sized rectangle when they do not overlap.
    inline Rect Intersect(const Rect &a, const Rect &b) {
        const float left = std::max(a.Left(), b.Left());
        const float top = std::max(a.Top(), b.Top());
        const float right = std::min(a.Right(), b.Right());
        const float bottom = std::min(a.Bottom(), b.Bottom());
        return { left, top, std::max(0.0f, right - left), std::max(0.0f, bottom - top) };
    }

}
