#include "nativeui/Backdrop.h"

#include <algorithm>
#include <cmath>

namespace nativeui {

    namespace {

        constexpr float kTwoPi = 6.28318530717959f;

        // One ribbon of light. Positions are fractions of the view; the two
        // frame bands are placed from the theme's frame metrics instead.
        struct Band {
            float y; // centre, fraction of the height; negative: see `frame`
            float left; // where it starts and ends, fractions of the width
            float right;
            ColorToken leftColor;
            float leftOpacity; // of kBackdropPeakOpacity
            ColorToken rightColor;
            float rightOpacity;
            int rate; // whole drift cycles per ambient period (keeps the loop seamless)
            float phase; // 0..1, so the bands do not move in step
            int frame; // 0: free; -1: on the frame's top hairline; +1: on its bottom one
        };

        // The game's ribbons enter from the right edge behind its list, one per
        // row; a few start at the left under the sort hint and the help bar.
        // These keep that grammar, sparser, since text now sits over them.
        constexpr Band kBands[] = {
            { 0.085f, -0.05f, 0.46f, &Palette::bandBlue, 0.9f, &Palette::bandMagenta, 0.0f, 1, 0.00f, 0 },
            { 0.0f, 0.30f, 1.05f, &Palette::bandViolet, 0.0f, &Palette::bandBlue, 1.0f, 1, 0.35f, -1 },
            { 0.29f, 0.52f, 1.05f, &Palette::bandMagenta, 0.0f, &Palette::bandViolet, 0.7f, 2, 0.60f, 0 },
            { 0.47f, 0.38f, 1.05f, &Palette::bandViolet, 0.0f, &Palette::bandBlue, 0.9f, 1, 0.15f, 0 },
            { 0.66f, 0.58f, 1.05f, &Palette::bandMagenta, 0.0f, &Palette::bandViolet, 0.6f, 2, 0.80f, 0 },
            { 0.0f, -0.05f, 0.62f, &Palette::bandBlue, 1.0f, &Palette::bandMagenta, 0.0f, 1, 0.50f, 1 },
            { 0.955f, 0.44f, 1.05f, &Palette::bandMagenta, 0.0f, &Palette::bandViolet, 0.7f, 1, 0.25f, 0 },
        };

        // How far a band slides each way, as a fraction of the view's width, and
        // how much of its light comes and goes.
        constexpr float kDriftShare = 0.035f;
        constexpr float kBreathShare = 0.22f;
        // The two pools, as shares of kBackdropPeakOpacity.
        constexpr float kVioletPool = 0.80f;
        constexpr float kBluePool = 0.60f;
        // A band's fine core, in units of the theme's border width.
        constexpr float kCoreThickness = 1.0f;

    }

    Size Backdrop::OnMeasure(const Context &, const Constraints &constraints) {
        return constraints.Biggest();
    }

    void Backdrop::OnAdvance(const Context &ctx, float deltaSeconds) {
        const float period = ctx.theme.motion.ambientPeriod;
        if (period > 0.0f) {
            phase_ = std::fmod(phase_ + deltaSeconds / period, 1.0f);
        }
    }

    void Backdrop::OnPaint(const Context &ctx, Painter &painter) const {
        const Theme &theme = ctx.theme;
        const Palette &color = theme.color;
        const Rect &bounds = Bounds();

        painter.FillRoundRect(
            bounds,
            theme.radius.none,
            CornerColors::Vertical(color.backdropTop, color.backdropBottom)
        );

        // The pools: each a rectangle softened by more than its own size, so no
        // edge of it can be seen.
        const float pool = bounds.height * 0.9f;
        const float sway = std::sin(kTwoPi * phase_) * bounds.width * kDriftShare;
        painter.FillRoundRect(
            { bounds.x + bounds.width * 0.74f + sway,
              bounds.y - bounds.height * 0.05f,
              bounds.width * 0.34f,
              bounds.height * 0.34f },
            pool,
            CornerColors::Solid(color.bandViolet.Faded(kBackdropPeakOpacity * kVioletPool)),
            pool
        );
        painter.FillRoundRect(
            { bounds.x - bounds.width * 0.08f - sway,
              bounds.y + bounds.height * 0.80f,
              bounds.width * 0.34f,
              bounds.height * 0.3f },
            pool,
            CornerColors::Solid(color.bandBlue.Faded(kBackdropPeakOpacity * kBluePool)),
            pool
        );

        const float soft = theme.metric.backdropBandSoftness;
        const float core = theme.metric.borderWidth * kCoreThickness;
        for (const Band &band : kBands) {
            const float turn = kTwoPi * (phase_ * static_cast<float>(band.rate) + band.phase);
            const float slide = std::sin(turn) * bounds.width * kDriftShare;
            const float breath = 1.0f - kBreathShare * (0.5f + 0.5f * std::cos(turn));
            const float y = band.frame < 0
                ? theme.metric.frameTop
                : (band.frame > 0 ? theme.metric.frameBottom : bounds.height * band.y);
            const float left = bounds.x + bounds.width * band.left + slide;
            const float width = bounds.width * (band.right - band.left);
            const Color from =
                (color.*band.leftColor).Faded(kBackdropPeakOpacity * band.leftOpacity * breath);
            const Color to =
                (color.*band.rightColor).Faded(kBackdropPeakOpacity * band.rightOpacity * breath);
            // The halo: a thin rectangle with a falloff much wider than itself.
            painter.FillRoundRect(
                { left, bounds.y + y - core * 0.5f, width, core },
                core * 0.5f,
                CornerColors::Horizontal(from, to),
                soft
            );
            // The core: the same line, crisp.
            painter.FillRoundRect(
                { left, bounds.y + y - core * 0.5f, width, core },
                core * 0.5f,
                CornerColors::Horizontal(from, to)
            );
        }
    }

}
