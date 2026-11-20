// FontSet: which GlyphSource serves which nativeui::FontId, and the
// nativeui::TextMeasurer over them. The widgets measure through it and the
// CanvasRenderer draws through it, so the two cannot use different fonts.
//
// Rules:
//  - Sources are borrowed and must outlive the set.
//  - An id with no source falls back to the default source (the first one
//    registered, or the one named by SetDefault). With no source at all, text
//    measures zero and draws nothing.
#pragma once

#include "nativeui/Text.h"
#include "nativeui_host/GlyphSource.h"

#include <unordered_map>

namespace nativeui_host {

    class FontSet final : public nativeui::TextMeasurer {
    public:
        void Set(nativeui::FontId font, const GlyphSource *source);
        void SetDefault(const GlyphSource *source) { default_ = source; }
        // The source for `font`, the default for an unknown id, or null.
        const GlyphSource *Find(nativeui::FontId font) const;
        bool Empty() const { return sources_.empty() && !default_; }

        // nativeui::TextMeasurer
        float AdvanceWidth(
            nativeui::FontId font, float size, std::string_view utf8
        ) const override;
        nativeui::FontMetrics Metrics(nativeui::FontId font, float size) const override;

    private:
        std::unordered_map<std::uint32_t, const GlyphSource *> sources_;
        const GlyphSource *default_ = nullptr;
    };

}
