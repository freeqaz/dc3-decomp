// Renderer: the drawing seam. The integration layer implements it on the
// engine's 2D canvas; the framework only ever calls it through Replay().
//
// Rules:
//  - One call per command, in list order. Later commands draw over earlier ones.
//  - Everything arrives in root logical coordinates with final colours; see
//    DisplayList.h for what each command means, to the pixel.
//  - PushClip rectangles are already intersected with enclosing clips. A
//    renderer scissors to the most recent push and restores the previous one
//    on PopClip; with an empty stack nothing is clipped.
#pragma once

#include "nativeui/DisplayList.h"

namespace nativeui {

    class Renderer {
    public:
        virtual ~Renderer() = default;

        virtual void Draw(const RoundRectFill &fill) = 0;
        virtual void Draw(const RoundRectStroke &stroke) = 0;
        virtual void Draw(const ImageQuad &image) = 0;
        virtual void Draw(const TextRun &text) = 0;
        virtual void PushClip(const Rect &rect) = 0;
        virtual void PopClip() = 0;
    };

    // Sends every command of `list` to `renderer`, in order.
    void Replay(const DisplayList &list, Renderer &renderer);

    // A Renderer that draws nothing and remembers everything. Replaying a list
    // into it reproduces the list, which is how tests check the seam.
    class RecordingRenderer final : public Renderer {
    public:
        void Draw(const RoundRectFill &fill) override {
            recorded.commands.push_back(fill);
        }
        void Draw(const RoundRectStroke &stroke) override {
            recorded.commands.push_back(stroke);
        }
        void Draw(const ImageQuad &image) override { recorded.commands.push_back(image); }
        void Draw(const TextRun &text) override { recorded.commands.push_back(text); }
        void PushClip(const Rect &rect) override {
            recorded.commands.push_back(ClipPush { rect });
        }
        void PopClip() override { recorded.commands.push_back(ClipPop {}); }

        DisplayList recorded;
    };

}
