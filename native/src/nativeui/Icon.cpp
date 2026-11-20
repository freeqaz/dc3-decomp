#include "nativeui/Icon.h"

#include <algorithm>
#include <cmath>

namespace nativeui {

    namespace {

        // Icon handles: the top 65536 ids. An image cache counts up from 1.
        constexpr std::uint32_t kIconHandleBase = 0xFFFF0000u;

        constexpr float kPi = 3.14159265358979f;

        // A closed outline in the unit square (y down). A shape is one or more;
        // a point is inside the shape when it is inside an odd number of them,
        // so an outline inside another cuts a hole and one inside that fills
        // it again.
        using Outline = std::vector<Vec2>;

        // A five-pointed star, one point up. `inner` is the radius of the notches.
        Outline StarOutline(Vec2 centre, float outer, float inner) {
            Outline points;
            for (int i = 0; i < 10; ++i) {
                const float radius = i % 2 == 0 ? outer : inner;
                const float angle = -kPi * 0.5f + static_cast<float>(i) * kPi / 5.0f;
                points.push_back(
                    { centre.x + radius * std::cos(angle), centre.y + radius * std::sin(angle) }
                );
            }
            return points;
        }

        Outline CircleOutline(Vec2 centre, float radius) {
            constexpr int kSegments = 28;
            Outline points;
            for (int i = 0; i < kSegments; ++i) {
                const float angle = static_cast<float>(i) * 2.0f * kPi / kSegments;
                points.push_back(
                    { centre.x + radius * std::cos(angle), centre.y + radius * std::sin(angle) }
                );
            }
            return points;
        }

        Outline RoundRectOutline(const Rect &rect, float radius) {
            constexpr int kSegments = 6; // per corner
            // Corner centres, clockwise from the top right, and where each arc starts.
            const Vec2 centres[4] = { { rect.Right() - radius, rect.y + radius },
                                      { rect.Right() - radius, rect.Bottom() - radius },
                                      { rect.x + radius, rect.Bottom() - radius },
                                      { rect.x + radius, rect.y + radius } };
            Outline points;
            for (int corner = 0; corner < 4; ++corner) {
                const float start = -kPi * 0.5f + static_cast<float>(corner) * kPi * 0.5f;
                for (int i = 0; i <= kSegments; ++i) {
                    const float angle = start + static_cast<float>(i) * kPi * 0.5f / kSegments;
                    points.push_back({ centres[corner].x + radius * std::cos(angle),
                                       centres[corner].y + radius * std::sin(angle) });
                }
            }
            return points;
        }

        // The star that fills the unit square: its points touch the left, right
        // and top edges. A regular star's notches are at 0.38 of its radius;
        // these are at 0.47, which keeps the star a star at a dozen pixels.
        constexpr float kStarRadius = 0.5257f;
        constexpr Vec2 kStarCentre { 0.5f, 0.55f };
        constexpr float kStarNotch = 0.47f;
        // The outlined star's line: the inner outline is the star at this scale.
        constexpr float kStarLine = 0.66f;

        std::vector<Outline> Shape(Icon icon) {
            switch (icon) {
            case Icon::Star:
                return { StarOutline(kStarCentre, kStarRadius, kStarRadius * kStarNotch) };
            case Icon::StarOutline:
                return { StarOutline(kStarCentre, kStarRadius, kStarRadius * kStarNotch),
                         StarOutline(
                             kStarCentre,
                             kStarRadius * kStarLine,
                             kStarRadius * kStarNotch * kStarLine
                         ) };
            case Icon::Shuffle: {
                // A die: a rounded frame and five pips.
                std::vector<Outline> shape { RoundRectOutline({ 0.06f, 0.06f, 0.88f, 0.88f }, 0.2f),
                                             RoundRectOutline(
                                                 { 0.15f, 0.15f, 0.70f, 0.70f }, 0.12f
                                             ) };
                const Vec2 pips[] = { { 0.33f, 0.33f },
                                      { 0.67f, 0.33f },
                                      { 0.5f, 0.5f },
                                      { 0.33f, 0.67f },
                                      { 0.67f, 0.67f } };
                for (const Vec2 &pip : pips) {
                    shape.push_back(CircleOutline(pip, 0.075f));
                }
                return shape;
            }
            case Icon::Playlist: {
                // Three bullets, three lines.
                std::vector<Outline> shape;
                for (const float y : { 0.24f, 0.5f, 0.76f }) {
                    shape.push_back(CircleOutline({ 0.15f, y }, 0.075f));
                    shape.push_back(
                        RoundRectOutline({ 0.32f, y - 0.06f, 0.62f, 0.12f }, 0.06f)
                    );
                }
                return shape;
            }
            case Icon::Search: {
                // A ring and a handle. The handle starts just outside the ring: two
                // outlines that overlapped would cut a hole in each other.
                const Vec2 lens { 0.42f, 0.42f };
                const float half = 0.045f; // half the handle's width, along each axis
                return { CircleOutline(lens, 0.34f),
                         CircleOutline(lens, 0.24f),
                         Outline { { 0.67f + half, 0.67f - half },
                                   { 0.93f + half, 0.93f - half },
                                   { 0.93f - half, 0.93f + half },
                                   { 0.67f - half, 0.67f + half } } };
            }
            }
            return {};
        }

    }

    ImageHandle IconImage(Icon icon) {
        return { kIconHandleBase + static_cast<std::uint32_t>(icon) };
    }

    std::optional<Icon> IconOf(ImageHandle image) {
        if (image.id < kIconHandleBase
            || image.id - kIconHandleBase >= static_cast<std::uint32_t>(kIconCount)) {
            return std::nullopt;
        }
        return static_cast<Icon>(image.id - kIconHandleBase);
    }

    int IconTexelSize(Icon icon) {
        // About twice the pixels the icon covers at 1080 rows, so it is never
        // magnified and never minified by more than the sampler's two-by-two.
        return icon == Icon::Star || icon == Icon::StarOutline ? 40 : 96;
    }

    std::vector<std::uint8_t> IconCoverage(Icon icon, int size) {
        std::vector<std::uint8_t> out;
        if (size <= 0) {
            return out;
        }
        const std::vector<Outline> shape = Shape(icon);
        // Scanline fill, odd-even: several sub-rows per pixel row, and along each
        // the exact length of every span that falls in a pixel.
        constexpr int kSubRows = 5;
        std::vector<float> coverage(static_cast<std::size_t>(size) * size, 0.0f);
        std::vector<float> crossings;
        const float scale = static_cast<float>(size);
        for (int row = 0; row < size; ++row) {
            for (int sub = 0; sub < kSubRows; ++sub) {
                const float y = (static_cast<float>(row) + (static_cast<float>(sub) + 0.5f) / kSubRows)
                    / scale;
                crossings.clear();
                for (const Outline &outline : shape) {
                    for (std::size_t i = 0; i < outline.size(); ++i) {
                        const Vec2 &a = outline[i];
                        const Vec2 &b = outline[(i + 1) % outline.size()];
                        if ((a.y <= y) != (b.y <= y)) {
                            crossings.push_back(a.x + (y - a.y) / (b.y - a.y) * (b.x - a.x));
                        }
                    }
                }
                std::sort(crossings.begin(), crossings.end());
                for (std::size_t i = 0; i + 1 < crossings.size(); i += 2) {
                    const float left = std::max(0.0f, crossings[i] * scale);
                    const float right = std::min(scale, crossings[i + 1] * scale);
                    for (int x = static_cast<int>(left); x < size && static_cast<float>(x) < right;
                         ++x) {
                        const float covered = std::min(right, static_cast<float>(x + 1))
                            - std::max(left, static_cast<float>(x));
                        coverage[static_cast<std::size_t>(row) * size + x] +=
                            std::max(0.0f, covered) / kSubRows;
                    }
                }
            }
        }
        out.reserve(coverage.size());
        for (const float value : coverage) {
            out.push_back(
                static_cast<std::uint8_t>(std::lround(std::min(1.0f, value) * 255.0f))
            );
        }
        return out;
    }

}
