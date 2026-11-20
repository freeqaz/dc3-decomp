// Image: a picture, or a flat placeholder until the picture exists.
//
// The framework never loads images. The screen gives the widget an ImageHandle
// when it has one; the null handle means "not here yet" and paints the
// placeholder colour instead.
//
// Behaviour:
//  - It has no size of its own: it fills the space it is given (wrap it in a
//    SizedBox).
//  - An image present when the widget is created shows at once. One that
//    arrives later through SetImage fades in over the placeholder.
//
// The corner radius and placeholder colour are theme tokens.
#pragma once

#include "nativeui/Animation.h"
#include "nativeui/Widget.h"

namespace nativeui {

    class Image final : public Widget {
    public:
        explicit Image(
            ImageHandle image = {},
            ColorToken placeholder = &Palette::imagePlaceholder,
            RadiusToken radius = &Radii::small
        );

        void SetImage(ImageHandle image);
        ImageHandle GetImage() const { return image_; }
        // The part of the image to show; the whole of it by default.
        void SetUv(const Rect &uv) { uv_ = uv; }

    protected:
        Size OnMeasure(const Context &ctx, const Constraints &constraints) override;
        void OnAdvance(const Context &ctx, float deltaSeconds) override;
        void OnPaint(const Context &ctx, Painter &painter) const override;
        bool IsAtRest() const override { return !fadeInPending_ && fade_.AtRest(); }

    private:
        ImageHandle image_;
        ColorToken placeholder_;
        RadiusToken radius_;
        Rect uv_ { 0.0f, 0.0f, 1.0f, 1.0f };
        Tween fade_; // 0 = placeholder only, 1 = image only
        bool fadeInPending_ = false; // the fade starts on the next Advance
    };

}
