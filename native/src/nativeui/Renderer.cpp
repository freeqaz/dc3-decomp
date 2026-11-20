#include "nativeui/Renderer.h"

namespace nativeui {

    namespace {

        struct Replayer {
            Renderer &renderer;

            void operator()(const RoundRectFill &c) const { renderer.Draw(c); }
            void operator()(const RoundRectStroke &c) const { renderer.Draw(c); }
            void operator()(const ImageQuad &c) const { renderer.Draw(c); }
            void operator()(const TextRun &c) const { renderer.Draw(c); }
            void operator()(const ClipPush &c) const { renderer.PushClip(c.rect); }
            void operator()(const ClipPop &) const { renderer.PopClip(); }
        };

    }

    void Replay(const DisplayList &list, Renderer &renderer) {
        const Replayer replayer { renderer };
        for (const Command &command : list.commands) {
            std::visit(replayer, command);
        }
    }

}
