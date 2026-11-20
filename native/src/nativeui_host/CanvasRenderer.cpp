#include "nativeui_host/CanvasRenderer.h"

#include <algorithm>
#include <cmath>

namespace nativeui_host {

    namespace {

        Canvas2DColor ToCanvas(const nativeui::Color &c) { return { c.r, c.g, c.b, c.a }; }

        Canvas2DRect ToCanvas(const nativeui::Rect &r) {
            return { r.x, r.y, r.width, r.height };
        }

        void SetCorners(Canvas2DQuad &quad, const nativeui::CornerColors &colors) {
            quad.tl = ToCanvas(colors.topLeft);
            quad.tr = ToCanvas(colors.topRight);
            quad.br = ToCanvas(colors.bottomRight);
            quad.bl = ToCanvas(colors.bottomLeft);
        }

    }

    CanvasRenderer::CanvasRenderer(QuadSink &sink, const FontSet &fonts, ImageSource *images)
        : sink_(sink), fonts_(fonts), images_(images) {}

    void CanvasRenderer::Draw(const nativeui::RoundRectFill &fill) {
        Canvas2DQuad quad;
        quad.rect = ToCanvas(fill.rect);
        quad.radius = fill.radius;
        quad.softness = CanvasSoftness(fill.softness);
        SetCorners(quad, fill.colors);
        sink_.Quad(quad);
        counts_.quads++;
    }

    void CanvasRenderer::Draw(const nativeui::RoundRectStroke &stroke) {
        if (stroke.thickness <= 0.0f) {
            return; // a canvas border of 0 is a fill, not an invisible ring
        }
        Canvas2DQuad quad;
        quad.rect = ToCanvas(stroke.rect);
        quad.radius = stroke.radius;
        quad.border = stroke.thickness;
        quad.softness = CanvasSoftness(stroke.softness);
        SetCorners(quad, stroke.colors);
        sink_.Quad(quad);
        counts_.quads++;
    }

    void CanvasRenderer::Draw(const nativeui::ImageQuad &image) {
        RndTex *tex = images_ ? images_->TextureFor(image.image) : nullptr;
        if (!tex) {
            counts_.imagesMissing++;
            return;
        }
        Canvas2DQuad quad;
        quad.rect = ToCanvas(image.rect);
        quad.u0 = image.uv.x;
        quad.v0 = image.uv.y;
        quad.u1 = image.uv.x + image.uv.width;
        quad.v1 = image.uv.y + image.uv.height;
        quad.tex = tex;
        quad.texMode = Canvas2DTexMode::kColor;
        quad.radius = image.radius;
        quad.softness = -1.0f;
        quad.SetColor(ToCanvas(image.tint));
        sink_.Quad(quad);
        counts_.quads++;
    }

    void CanvasRenderer::Draw(const nativeui::TextRun &text) {
        const GlyphSource *source = fonts_.Find(text.font);
        if (!source || text.text.empty() || text.size <= 0.0f) {
            return;
        }
        scratch_.clear();
        source->Layout(text.size, text.letterSpacing, text.text, scratch_);

        float baseline = text.origin.y;
        if (deviceScaleY_ > 0.0f) {
            baseline = std::round(baseline * deviceScaleY_) / deviceScaleY_;
        }
        const Canvas2DColor colour = ToCanvas(text.color);
        const Canvas2DTexMode mode = source->PagesCarryColour()
            ? Canvas2DTexMode::kColor
            : Canvas2DTexMode::kCoverageA;

        for (const GlyphQuad &glyph : scratch_) {
            Canvas2DQuad quad;
            quad.rect = { text.origin.x + glyph.x, baseline + glyph.y, glyph.w, glyph.h };
            quad.SetColor(colour);
            if (glyph.page < 0) {
                // The missing-glyph box: a thin ring, antialiased.
                quad.border = std::max(1.0f, text.size * 0.06f);
                quad.softness = -1.0f;
                counts_.placeholders++;
            } else {
                RndTex *tex = source->PageTexture(glyph.page);
                if (!tex) {
                    continue; // a page with no texture: no garbage, no quad
                }
                quad.tex = tex;
                quad.texMode = mode;
                quad.u0 = glyph.u0;
                quad.v0 = glyph.v0;
                quad.u1 = glyph.u1;
                quad.v1 = glyph.v1;
                quad.softness = 0.0f;
            }
            sink_.Quad(quad);
            counts_.quads++;
            counts_.glyphs++;
        }
    }

    void CanvasRenderer::PushClip(const nativeui::Rect &rect) {
        sink_.PushClip(ToCanvas(rect));
    }

    void CanvasRenderer::PopClip() { sink_.PopClip(); }

}
