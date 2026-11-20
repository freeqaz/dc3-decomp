#include "nativeui/DisplayList.h"

#include <cmath>
#include <cstdio>

namespace nativeui {

    bool operator==(const CornerColors &a, const CornerColors &b) {
        return a.topLeft == b.topLeft && a.topRight == b.topRight
            && a.bottomRight == b.bottomRight && a.bottomLeft == b.bottomLeft;
    }

    bool operator==(const RoundRectFill &a, const RoundRectFill &b) {
        return a.rect == b.rect && a.radius == b.radius && a.colors == b.colors
            && a.softness == b.softness;
    }

    bool operator==(const RoundRectStroke &a, const RoundRectStroke &b) {
        return a.rect == b.rect && a.radius == b.radius && a.colors == b.colors
            && a.thickness == b.thickness && a.softness == b.softness;
    }

    bool operator==(const ImageQuad &a, const ImageQuad &b) {
        return a.rect == b.rect && a.image == b.image && a.uv == b.uv && a.tint == b.tint
            && a.radius == b.radius;
    }

    bool operator==(const TextRun &a, const TextRun &b) {
        return a.origin == b.origin && a.font == b.font && a.size == b.size
            && a.color == b.color && a.letterSpacing == b.letterSpacing
            && a.text == b.text;
    }

    bool operator==(const ClipPush &a, const ClipPush &b) { return a.rect == b.rect; }
    bool operator==(const ClipPop &, const ClipPop &) { return true; }

    // ---------------------------------------------------------------------------
    // Describe
    // ---------------------------------------------------------------------------

    namespace {

        std::string Number(float value) {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%.6g", static_cast<double>(value));
            return buffer;
        }

        std::string Hex(const Color &c) {
            const auto byte = [](float v) {
                return static_cast<int>(
                    std::lround(std::min(1.0f, std::max(0.0f, v)) * 255.0f)
                );
            };
            char buffer[16];
            std::snprintf(
                buffer,
                sizeof(buffer),
                "#%02X%02X%02X%02X",
                byte(c.r),
                byte(c.g),
                byte(c.b),
                byte(c.a)
            );
            return buffer;
        }

        std::string Box(const Rect &r) {
            return Number(r.x) + "," + Number(r.y) + " " + Number(r.width) + "x"
                + Number(r.height);
        }

        std::string Colors(const CornerColors &c) {
            if (c == CornerColors::Solid(c.topLeft)) {
                return Hex(c.topLeft);
            }
            return Hex(c.topLeft) + " " + Hex(c.topRight) + " " + Hex(c.bottomRight) + " "
                + Hex(c.bottomLeft);
        }

        struct Describer {
            std::string operator()(const RoundRectFill &c) const {
                return "fill " + Box(c.rect) + " r=" + Number(c.radius)
                    + " soft=" + Number(c.softness) + " " + Colors(c.colors);
            }
            std::string operator()(const RoundRectStroke &c) const {
                return "stroke " + Box(c.rect) + " r=" + Number(c.radius)
                    + " t=" + Number(c.thickness) + " soft=" + Number(c.softness) + " "
                    + Colors(c.colors);
            }
            std::string operator()(const ImageQuad &c) const {
                return "image id=" + std::to_string(c.image.id) + " " + Box(c.rect)
                    + " uv=" + Box(c.uv) + " r=" + Number(c.radius)
                    + " tint=" + Hex(c.tint);
            }
            std::string operator()(const TextRun &c) const {
                return "text " + Number(c.origin.x) + "," + Number(c.origin.y)
                    + " font=" + std::to_string(c.font.value) + " size=" + Number(c.size)
                    + " ls=" + Number(c.letterSpacing) + " " + Hex(c.color) + " \""
                    + c.text + "\"";
            }
            std::string operator()(const ClipPush &c) const {
                return "clip " + Box(c.rect);
            }
            std::string operator()(const ClipPop &) const { return "unclip"; }
        };

    }

    std::string Describe(const Command &command) {
        return std::visit(Describer {}, command);
    }

    std::string Describe(const DisplayList &list) {
        std::string out;
        for (const Command &command : list.commands) {
            out += Describe(command);
            out += '\n';
        }
        return out;
    }

    // ---------------------------------------------------------------------------
    // Painter
    // ---------------------------------------------------------------------------

    Painter::Painter(DisplayList &out) : out_(out) { layers_.push_back(Layer {}); }

    Rect Painter::Map(const Rect &rect) const {
        const Layer &layer = Top();
        return { layer.offset.x + rect.x * layer.scale,
                 layer.offset.y + rect.y * layer.scale,
                 rect.width * layer.scale,
                 rect.height * layer.scale };
    }

    CornerColors Painter::Fade(const CornerColors &colors) const {
        const float opacity = Top().opacity;
        return { colors.topLeft.Faded(opacity),
                 colors.topRight.Faded(opacity),
                 colors.bottomRight.Faded(opacity),
                 colors.bottomLeft.Faded(opacity) };
    }

    void Painter::FillRoundRect(
        const Rect &rect, float radius, const CornerColors &colors, float softness
    ) {
        if (Invisible()) {
            return;
        }
        const float scale = Top().scale;
        out_.commands.push_back(
            RoundRectFill { Map(rect), radius * scale, Fade(colors), softness * scale }
        );
    }

    void Painter::StrokeRoundRect(
        const Rect &rect,
        float radius,
        const CornerColors &colors,
        float thickness,
        float softness
    ) {
        if (Invisible()) {
            return;
        }
        const float scale = Top().scale;
        out_.commands.push_back(
            RoundRectStroke { Map(rect),
                              radius * scale,
                              Fade(colors),
                              thickness * scale,
                              softness * scale }
        );
    }

    void Painter::DrawImage(
        const Rect &rect,
        ImageHandle image,
        const Rect &uv,
        const Color &tint,
        float radius
    ) {
        if (Invisible()) {
            return;
        }
        const Layer &layer = Top();
        out_.commands.push_back(
            ImageQuad {
                Map(rect), image, uv, tint.Faded(layer.opacity), radius * layer.scale }
        );
    }

    void Painter::DrawText(
        Vec2 origin,
        const TextStyle &style,
        const Color &color,
        const std::string &displayText
    ) {
        if (Invisible() || displayText.empty()) {
            return;
        }
        const Layer &layer = Top();
        out_.commands.push_back(
            TextRun { layer.offset + origin * layer.scale,
                      style.font,
                      style.size * layer.scale,
                      color.Faded(layer.opacity),
                      LetterSpacing(style) * layer.scale,
                      displayText }
        );
    }

    void Painter::PushClip(const Rect &rect) {
        Rect clip = Map(rect);
        if (!clips_.empty()) {
            clip = Intersect(clip, clips_.back());
        }
        clips_.push_back(clip);
        out_.commands.push_back(ClipPush { clip });
    }

    void Painter::PopClip() {
        if (clips_.empty()) {
            return;
        }
        clips_.pop_back();
        out_.commands.push_back(ClipPop {});
    }

    void Painter::PushLayer(const Layer &layer) {
        const Layer &parent = Top();
        layers_.push_back(
            Layer { parent.offset + layer.offset * parent.scale,
                    parent.scale * layer.scale,
                    parent.opacity * layer.opacity }
        );
    }

    void Painter::PopLayer() {
        if (layers_.size() > 1) {
            layers_.pop_back();
        }
    }

}
