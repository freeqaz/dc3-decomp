// Shared helpers for the nativeui tests: a theme + fake text measurer to run
// the passes with, display-list queries, and a plain box widget.
#pragma once

#include "nativeui/DisplayList.h"
#include "nativeui/FixedAdvanceMeasurer.h"
#include "nativeui/Theme.h"
#include "nativeui/Widget.h"

#include <gtest/gtest.h>

#include <functional>
#include <string>
#include <vector>

namespace nativeui_test {

    using namespace nativeui;

    // Every command of one kind, in list order.
    template <class T>
    std::vector<T> CommandsOf(const DisplayList &list) {
        std::vector<T> out;
        for (const Command &command : list.commands) {
            if (const T *typed = std::get_if<T>(&command)) {
                out.push_back(*typed);
            }
        }
        return out;
    }

    inline std::vector<std::string> TextsOf(const DisplayList &list) {
        std::vector<std::string> out;
        for (const TextRun &run : CommandsOf<TextRun>(list)) {
            out.push_back(run.text);
        }
        return out;
    }

    inline int CountText(const DisplayList &list, const std::string &text) {
        int count = 0;
        for (const std::string &candidate : TextsOf(list)) {
            count += candidate == text ? 1 : 0;
        }
        return count;
    }

    inline bool HasText(const DisplayList &list, const std::string &text) {
        return CountText(list, text) > 0;
    }

    // True when every ClipPush has its ClipPop and no pop comes first.
    inline bool ClipsBalanced(const DisplayList &list) {
        int depth = 0;
        for (const Command &command : list.commands) {
            if (std::holds_alternative<ClipPush>(command)) {
                ++depth;
            } else if (std::holds_alternative<ClipPop>(command)) {
                if (--depth < 0) {
                    return false;
                }
            }
        }
        return depth == 0;
    }

    // Runs the frame passes on a bare tree, with no Screen.
    struct Harness {
        Theme theme = Dc3Theme();
        FixedAdvanceMeasurer text;

        Context Ctx() const { return { theme, text }; }

        DisplayList Frame(Widget &root, Size viewport, float deltaSeconds = 0.0f) const {
            const Context ctx = Ctx();
            root.Advance(ctx, deltaSeconds);
            root.Measure(ctx, Constraints::Tight(viewport));
            root.Arrange(ctx, { 0.0f, 0.0f, viewport.width, viewport.height });
            DisplayList list;
            Painter painter(list);
            root.Paint(ctx, painter);
            EXPECT_TRUE(painter.IsBalanced());
            return list;
        }

        // Steps at 60 Hz until the tree rests; fails the test if it never does.
        DisplayList Settle(Widget &root, Size viewport) const {
            DisplayList list = Frame(root, viewport);
            for (int i = 0; i < 600 && !root.AtRest(); ++i) {
                list = Frame(root, viewport, 1.0f / 60.0f);
            }
            EXPECT_TRUE(root.AtRest()) << "still animating after 10 seconds";
            return list;
        }
    };

    // A widget that wants a fixed size, can hold children (each given its whole
    // area), and reports the events it sees.
    class Box : public Widget {
    public:
        explicit Box(Size wanted = {}, bool focusable = false, std::string name = "")
            : name(std::move(name)), wanted_(wanted), focusable_(focusable) {}

        template <class T>
        T &Add(std::unique_ptr<T> child) {
            return Adopt(std::move(child));
        }

        bool HandleEvent(const Event &event) override {
            if (log) {
                log->push_back(name);
            }
            return onEvent && onEvent(event);
        }
        bool IsFocusable() const override { return focusable_; }

        std::string name;
        std::vector<std::string> *log = nullptr; // names, in the order events arrived
        std::function<bool(const Event &)> onEvent;

    protected:
        Size OnMeasure(const Context &ctx, const Constraints &constraints) override {
            for (const auto &child : Children()) {
                child->Measure(ctx, constraints.Loosened());
            }
            return wanted_;
        }
        void OnArrange(const Context &ctx, const Rect &bounds) override {
            for (const auto &child : Children()) {
                child->Arrange(ctx, bounds);
            }
        }
        void OnPaint(const Context &ctx, Painter &painter) const override {
            painter.FillRoundRect(
                Bounds(), 0.0f, CornerColors::Solid(ctx.theme.color.accent)
            );
            PaintChildren(ctx, painter);
        }

    private:
        Size wanted_;
        bool focusable_;
    };

    inline Event Act(Action action) { return ActionEvent { action }; }
    inline Event Type(char32_t codePoint) { return TextEvent { codePoint }; }
    inline Event Key(Edit edit) { return EditEvent { edit }; }

}
