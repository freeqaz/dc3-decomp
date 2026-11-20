#include "nativeui/Screen.h"

#include <algorithm>
#include <optional>

namespace nativeui {

    namespace {

        std::optional<Direction> DirectionOf(const Event &event) {
            const ActionEvent *action = std::get_if<ActionEvent>(&event);
            if (!action) {
                return std::nullopt;
            }
            switch (action->action) {
            case Action::Up:
                return Direction::Up;
            case Action::Down:
                return Direction::Down;
            case Action::Left:
                return Direction::Left;
            case Action::Right:
                return Direction::Right;
            default:
                return std::nullopt;
            }
        }

    }

    Screen::Screen(
        Size viewport, Theme theme, const TextMeasurer &text, const Clock &clock
    )
        : viewport_(viewport), theme_(std::move(theme)), text_(text), clock_(clock) {}

    void Screen::SetRoot(std::unique_ptr<Widget> root) {
        focus_.SetFocus(nullptr);
        root_ = std::move(root);
        if (root_) {
            focus_.SetFocus(FirstFocusable(*root_));
        }
    }

    bool Screen::HandleEvent(const Event &event) {
        if (focus_.Dispatch(event)) {
            return true;
        }
        if (const std::optional<Direction> direction = DirectionOf(event)) {
            if (root_ && focus_.MoveFocus(*root_, *direction)) {
                return true;
            }
        }
        return onUnhandledEvent && onUnhandledEvent(event);
    }

    void Screen::PlayEnter() {
        enter_.Snap(0.0f);
        enterPending_ = true;
    }

    const DisplayList &Screen::BuildFrame() {
        const double now = clock_.NowSeconds();
        const float delta =
            hasLastTime_ ? static_cast<float>(std::max(0.0, now - lastTime_)) : 0.0f;
        lastTime_ = now;
        hasLastTime_ = true;

        if (enterPending_) {
            enter_.Start(1.0f, theme_.motion.slow, theme_.motion.easing);
            enterPending_ = false;
        }
        enter_.Advance(delta);

        frame_.commands.clear();
        if (!root_) {
            return frame_;
        }
        const Context ctx { theme_, text_ };
        // Advance before layout, so the rows a list builds in Arrange are the rows
        // at the scroll position that is about to be painted.
        root_->Advance(ctx, delta);
        root_->Measure(ctx, Constraints::Tight(viewport_));
        root_->Arrange(ctx, { 0.0f, 0.0f, viewport_.width, viewport_.height });

        Painter painter(frame_);
        const float arrived = enter_.Value();
        painter.PushLayer({ { 0.0f, (1.0f - arrived) * theme_.space.xl }, 1.0f, arrived });
        root_->Paint(ctx, painter);
        painter.PopLayer();
        return frame_;
    }

    bool Screen::AtRest() const {
        return !enterPending_ && enter_.AtRest() && (!root_ || root_->AtRest());
    }

}
