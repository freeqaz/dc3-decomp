// CanvasRenderer: nativeui::Renderer on the engine's 2D canvas (gfx/Canvas2D.h).
//
// The canvas is reached through QuadSink, three calls wide, so the translation
// below is tested with a recording sink and no GPU; Canvas2DSink forwards to a
// real Canvas2D. Logical space is whatever the canvas was opened with (the
// host opens it at 1280x720 on the current viewport); nothing is scaled here.
//
// THE TRANSLATION, command by command
//   RoundRectFill   one quad. rect and radius as given (the canvas clamps the
//                   radius to half the short side, as nativeui asks). Corner
//                   colours topLeft/topRight/bottomRight/bottomLeft -> the
//                   quad's tl/tr/br/bl. Softness: nativeui 0 means "an
//                   ordinary one-pixel antialiased edge", which the canvas
//                   spells softness < 0 (its 0 is a HARD edge), so 0 -> -1;
//                   a positive softness is the same width in both (a ramp
//                   centred on the edge) and is passed through.
//   RoundRectStroke the same quad with border = thickness: the canvas's ring
//                   is a band that wide just inside the edge, as nativeui's
//                   stroke is. A stroke with thickness <= 0 draws NOTHING (a
//                   canvas border of 0 would be a fill).
//   ImageQuad       one textured quad, texMode kColor (texel RGBA x tint), uv
//                   rect {x,y,w,h} -> u0,v0 = x,y and u1,v1 = x+w,y+h, rounded
//                   by `radius`. The handle is resolved through ImageSource; a
//                   handle with no texture (not loaded, evicted, unknown)
//                   draws nothing -- nativeui's Image paints its own
//                   placeholder for the null handle.
//   TextRun         the font's GlyphSource lays the text out at `size` with
//                   `letterSpacing` after every code point but the last -- the
//                   same walk FontSet measures with, so a run is exactly as
//                   wide as MeasureWidth said. `origin` is the left end of the
//                   baseline. One quad per inked glyph, hard-edged (canvas
//                   softness 0: the cell's texels map straight onto the quad),
//                   kCoverageA with the run's colour for a coverage atlas,
//                   kColor (atlas RGB x colour) for a coloured one. A missing
//                   glyph's placeholder is a hollow box in the run's colour.
//                   With a device scale set (SetDeviceScale), the baseline is
//                   moved to the nearest device pixel row; x is never snapped,
//                   so widths stay exactly the measurer's.
//   ClipPush        Canvas PushClip with the rectangle as given. nativeui has
//                   already intersected it with the enclosing clips; the
//                   canvas intersects again, which changes nothing.
//   ClipPop         Canvas PopClip.
//
// Colours are passed through unchanged: both sides are straight-alpha sRGB
// frame colours.
#pragma once

#include "gfx/Canvas2D.h"
#include "nativeui/Renderer.h"
#include "nativeui_host/FontSet.h"

#include <vector>

namespace nativeui_host {

    // What the renderer needs of a canvas.
    class QuadSink {
    public:
        virtual ~QuadSink() = default;
        virtual void Quad(const Canvas2DQuad &quad) = 0;
        virtual void PushClip(const Canvas2DRect &rect) = 0;
        virtual void PopClip() = 0;
    };

    // The engine canvas as a sink. The canvas must be open (Begin succeeded).
    class Canvas2DSink final : public QuadSink {
    public:
        explicit Canvas2DSink(Canvas2D &canvas) : canvas_(canvas) {}
        void Quad(const Canvas2DQuad &quad) override { canvas_.Quad(quad); }
        void PushClip(const Canvas2DRect &rect) override { canvas_.PushClip(rect); }
        void PopClip() override { canvas_.PopClip(); }

    private:
        Canvas2D &canvas_;
    };

    // Resolves image handles to textures. The texture must stay alive until
    // the canvas's End() (ImageCache defers its evictions for that reason).
    class ImageSource {
    public:
        virtual ~ImageSource() = default;
        virtual RndTex *TextureFor(nativeui::ImageHandle image) = 0;
    };

    class CanvasRenderer final : public nativeui::Renderer {
    public:
        // `fonts` and `images` are borrowed; `images` may be null (no images).
        CanvasRenderer(QuadSink &sink, const FontSet &fonts, ImageSource *images);

        // Device pixels per logical unit, for baseline snapping; 0 = no snapping.
        void SetDeviceScale(float scaleY) { deviceScaleY_ = scaleY; }

        void Draw(const nativeui::RoundRectFill &fill) override;
        void Draw(const nativeui::RoundRectStroke &stroke) override;
        void Draw(const nativeui::ImageQuad &image) override;
        void Draw(const nativeui::TextRun &text) override;
        void PushClip(const nativeui::Rect &rect) override;
        void PopClip() override;

        // Counters since construction.
        struct Counts {
            unsigned quads = 0; // quads sent to the sink
            unsigned glyphs = 0; // of which glyphs
            unsigned placeholders = 0; // glyphs drawn as the missing-glyph box
            unsigned imagesMissing = 0; // image quads skipped for want of a texture
        };
        const Counts &GetCounts() const { return counts_; }

        // nativeui softness -> canvas softness (see the header comment).
        static float CanvasSoftness(float nativeuiSoftness) {
            return nativeuiSoftness > 0.0f ? nativeuiSoftness : -1.0f;
        }

    private:
        QuadSink &sink_;
        const FontSet &fonts_;
        ImageSource *images_;
        float deviceScaleY_ = 0.0f;
        Counts counts_;
        std::vector<GlyphQuad> scratch_;
    };

}
