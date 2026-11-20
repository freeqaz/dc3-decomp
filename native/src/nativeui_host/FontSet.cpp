#include "nativeui_host/FontSet.h"

namespace nativeui_host {

    void FontSet::Set(nativeui::FontId font, const GlyphSource *source) {
        if (!source) {
            sources_.erase(font.value);
            return;
        }
        sources_[font.value] = source;
        if (!default_) {
            default_ = source;
        }
    }

    const GlyphSource *FontSet::Find(nativeui::FontId font) const {
        const auto found = sources_.find(font.value);
        return found != sources_.end() ? found->second : default_;
    }

    float FontSet::AdvanceWidth(
        nativeui::FontId font, float size, std::string_view utf8
    ) const {
        const GlyphSource *source = Find(font);
        return source ? source->Advance(size, utf8) : 0.0f;
    }

    nativeui::FontMetrics FontSet::Metrics(nativeui::FontId font, float size) const {
        const GlyphSource *source = Find(font);
        if (!source) {
            // No font: the proportions of a plain line, so layout stays sane.
            return { size * 0.8f, size * 0.2f, size * 1.2f };
        }
        return source->Metrics(size);
    }

}
