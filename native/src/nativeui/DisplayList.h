// DisplayList: what a frame of UI looks like, as a flat list of plain commands.
//
// Rules:
//  - Widgets never draw. They call a Painter, which appends commands here; a
//    Renderer (Renderer.h) replays the list on a real canvas.
//  - Commands are final: coordinates are in the root logical space and colours
//    carry the opacity of every enclosing layer. A renderer keeps no transform
//    or opacity state, only a clip stack.
//  - The command set is closed and small on purpose. Each one maps to a single
//    batched quad (or a glyph run) in the engine's 2D canvas.
//
// What each command means, exactly:
//  - RoundRectFill: the rounded rectangle `rect` with corner `radius` (a
//    renderer clamps it to half the shorter side). Colour is the bilinear blend
//    of the four corner colours over `rect`. `softness` is the width of the
//    edge's alpha ramp, centred on the edge: alpha is 1 at softness/2 inside
//    and 0 at softness/2 outside. Zero means the renderer's ordinary
//    one-pixel antialiasing. Shadows and glows are fills with a large softness.
//  - RoundRectStroke: the band `thickness` wide just inside the same edge;
//    `softness` applies to both sides of the band.
//  - ImageQuad: `image` sampled over `uv` (0..1, origin top-left), multiplied
//    by `tint`, drawn in `rect` with rounded corners.
//  - TextRun: `text` (UTF-8, already in display case) with the left end of its
//    baseline at `origin`; `letterSpacing` is added between adjacent glyphs.
//  - ClipPush / ClipPop: an axis-aligned scissor. A pushed rectangle is already
//    intersected with every enclosing clip, so a renderer only ever scissors to
//    the top of its stack.
#pragma once

#include "nativeui/Color.h"
#include "nativeui/Geometry.h"
#include "nativeui/Text.h"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace nativeui {

    // An opaque reference to a texture owned by the integration layer. The zero
    // handle means "no image (yet)".
    struct ImageHandle {
        std::uint32_t id = 0;
        bool IsNull() const { return id == 0; }
    };
    inline bool operator==(ImageHandle a, ImageHandle b) { return a.id == b.id; }
    inline bool operator!=(ImageHandle a, ImageHandle b) { return a.id != b.id; }

    // One colour per corner of a rectangle; the renderer blends between them.
    struct CornerColors {
        Color topLeft;
        Color topRight;
        Color bottomRight;
        Color bottomLeft;

        static CornerColors Solid(const Color &c) { return { c, c, c, c }; }
        static CornerColors Vertical(const Color &top, const Color &bottom) {
            return { top, top, bottom, bottom };
        }
        static CornerColors Horizontal(const Color &left, const Color &right) {
            return { left, right, right, left };
        }
    };
    bool operator==(const CornerColors &a, const CornerColors &b);

    struct RoundRectFill {
        Rect rect;
        float radius = 0.0f;
        CornerColors colors;
        float softness = 0.0f;
    };

    struct RoundRectStroke {
        Rect rect;
        float radius = 0.0f;
        CornerColors colors;
        float thickness = 0.0f;
        float softness = 0.0f;
    };

    struct ImageQuad {
        Rect rect;
        ImageHandle image;
        Rect uv;
        Color tint;
        float radius = 0.0f;
    };

    struct TextRun {
        Vec2 origin;
        FontId font;
        float size = 0.0f;
        Color color;
        float letterSpacing = 0.0f;
        std::string text;
    };

    struct ClipPush {
        Rect rect;
    };

    struct ClipPop {};

    bool operator==(const RoundRectFill &a, const RoundRectFill &b);
    bool operator==(const RoundRectStroke &a, const RoundRectStroke &b);
    bool operator==(const ImageQuad &a, const ImageQuad &b);
    bool operator==(const TextRun &a, const TextRun &b);
    bool operator==(const ClipPush &a, const ClipPush &b);
    bool operator==(const ClipPop &a, const ClipPop &b);

    using Command =
        std::variant<RoundRectFill, RoundRectStroke, ImageQuad, TextRun, ClipPush, ClipPop>;

    struct DisplayList {
        std::vector<Command> commands;
    };

    // One line of text per command, for test goldens and debugging, e.g.
    //   fill 16,8 200x40 r=6 soft=0 #33A7E3FF
    //   text 24,36 font=1 size=26 ls=1.04 #FFFFFFFF "WARMUP"
    std::string Describe(const Command &command);
    std::string Describe(const DisplayList &list);

    // How a subtree is composed into its parent: drawn at `offset`, scaled by
    // `scale` about its own origin, and faded by `opacity`.
    struct Layer {
        Vec2 offset;
        float scale = 1.0f;
        float opacity = 1.0f;
    };

    // Painter: the only way commands get into a DisplayList.
    //
    // It applies the current layer (offset, uniform scale, opacity) to everything
    // drawn, and keeps clips nested. Pushes and pops must balance; IsBalanced()
    // says whether they did. Nothing is emitted for drawing done while the
    // composed opacity is zero.
    class Painter {
    public:
        explicit Painter(DisplayList &out);

        void FillRoundRect(
            const Rect &rect,
            float radius,
            const CornerColors &colors,
            float softness = 0.0f
        );
        void StrokeRoundRect(
            const Rect &rect,
            float radius,
            const CornerColors &colors,
            float thickness,
            float softness = 0.0f
        );
        void DrawImage(
            const Rect &rect,
            ImageHandle image,
            const Rect &uv,
            const Color &tint,
            float radius
        );
        // `origin` is the left end of the baseline.
        void DrawText(
            Vec2 origin,
            const TextStyle &style,
            const Color &color,
            const std::string &displayText
        );

        void PushClip(const Rect &rect);
        void PopClip();

        void PushLayer(const Layer &layer);
        void PopLayer();

        bool IsBalanced() const { return layers_.size() == 1 && clips_.empty(); }

    private:
        const Layer &Top() const { return layers_.back(); }
        Rect Map(const Rect &rect) const;
        CornerColors Fade(const CornerColors &colors) const;
        bool Invisible() const { return Top().opacity <= 0.0f; }

        DisplayList &out_;
        std::vector<Layer> layers_; // each entry already composed with its parents
        std::vector<Rect> clips_; // root space, each already intersected with its parents
    };

}
