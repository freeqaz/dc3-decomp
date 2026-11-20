// IconTextures: the textures behind nativeui's icons.
//
// A nativeui icon is an image handle with a fixed id and a shape defined in
// code (nativeui/Icon.h). This is the ImageSource that turns such a handle
// into a texture: a white square whose alpha is the icon's coverage, made
// from nativeui::IconCoverage the first time the icon is drawn and kept for
// as long as this object lives. Every other handle goes to the source it
// wraps (the album art cache, say).
//
// Rules:
//  - A texture is made on first use, on the thread that draws, so a host
//    that never draws an icon never touches the renderer through this.
//  - The wrapped source is borrowed and may be null or change at any time.
#pragma once

#include "nativeui/Icon.h"
#include "nativeui_host/CanvasRenderer.h"

#include <array>

class RndTex;

namespace nativeui_host {

    class IconTextures final : public ImageSource {
    public:
        IconTextures() = default;
        ~IconTextures() override;
        IconTextures(const IconTextures &) = delete;
        IconTextures &operator=(const IconTextures &) = delete;

        // Where handles that are not icons are looked up.
        void SetNext(ImageSource *next) { next_ = next; }

        RndTex *TextureFor(nativeui::ImageHandle image) override;

        // How many icon textures exist right now.
        int MadeCount() const;

    private:
        ImageSource *next_ = nullptr;
        std::array<RndTex *, nativeui::kIconCount> textures_ {};
    };

}
