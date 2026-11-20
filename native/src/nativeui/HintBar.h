// HintBar: the row of control hints along the bottom of a screen
// ("(A) SELECT   [ESC] BACK").
//
// Each hint is a control's label and what the control does. The integration
// layer chooses the label; how it is drawn follows from its length, the way
// the game draws its own prompts:
//   one character   a round BUTTON: a filled disc in the hint's tone with the
//                   letter cut dark into it (a pad's A, B, X, Y)
//   longer          a KEYCAP: a rounded key, neutral, with the label on it
//                   (a keyboard's ENTER, ESC, TAB)
// A hint with no tone gets a neutral disc. Every hint's text is the same
// colour: a hint that is listed is a hint that works. Hints are laid out left
// to right and never take focus.
#pragma once

#include "nativeui/Theme.h"
#include "nativeui/Widget.h"

#include <string>
#include <vector>

namespace nativeui {

    struct Hint {
        std::string glyph; // the control's label
        std::string text; // what the control does
        // The colour of the control when it is a round button (null: neutral).
        // A keycap ignores it.
        ColorToken tone = nullptr;
    };

    class HintBar final : public Widget {
    public:
        explicit HintBar(std::vector<Hint> hints);

        void SetHints(std::vector<Hint> hints) { hints_ = std::move(hints); }
        const std::vector<Hint> &Hints() const { return hints_; }

    protected:
        Size OnMeasure(const Context &ctx, const Constraints &constraints) override;
        void OnPaint(const Context &ctx, Painter &painter) const override;

    private:
        struct Extents {
            float pill = 0.0f; // width of the button or keycap
            float glyph = 0.0f; // width of the label on it
            bool round = false; // a one-character label: a round button
            float text = 0.0f;
        };
        Extents ExtentsOf(const Context &ctx, const Hint &hint) const;

        std::vector<Hint> hints_;
    };

}
