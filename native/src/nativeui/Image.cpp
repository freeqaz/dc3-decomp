#include "nativeui/Image.h"

namespace nativeui {

    Image::Image(ImageHandle image, ColorToken placeholder, RadiusToken radius)
        : image_(image), placeholder_(placeholder), radius_(radius),
          fade_(image.IsNull() ? 0.0f : 1.0f) {}

    void Image::SetImage(ImageHandle image) {
        if (image.IsNull()) {
            fade_.Snap(0.0f);
            fadeInPending_ = false;
        } else if (image_.IsNull()) {
            fadeInPending_ = true;
        }
        image_ = image;
    }

    Size Image::OnMeasure(const Context &, const Constraints &constraints) {
        return constraints.Biggest();
    }

    void Image::OnAdvance(const Context &ctx, float deltaSeconds) {
        if (fadeInPending_) {
            fade_.Start(1.0f, ctx.theme.motion.normal, ctx.theme.motion.easing);
            fadeInPending_ = false;
        }
        fade_.Advance(deltaSeconds);
    }

    void Image::OnPaint(const Context &ctx, Painter &painter) const {
        const Rect &bounds = Bounds();
        const float radius = ctx.theme.radius.*radius_;
        const float shown = fade_.Value();
        if (shown < 1.0f) {
            painter.FillRoundRect(
                bounds, radius, CornerColors::Solid(ctx.theme.color.*placeholder_)
            );
        }
        if (shown > 0.0f) {
            painter.DrawImage(
                bounds, image_, uv_, ctx.theme.color.imageTint.Faded(shown), radius
            );
        }
    }

}
