#include "nativeui_host/IconTextures.h"

#include "rndobj/Bitmap.h"
#include "rndobj/Tex.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace nativeui_host {

    namespace {

        constexpr int kBitsPerPixel = 32;
        constexpr int kBytesPerPixel = kBitsPerPixel / 8;

        // A white texture whose alpha is `coverage` (size x size bytes).
        RndTex *MakeTexture(const std::vector<std::uint8_t> &coverage, int size) {
            // Four bytes a texel, white with the coverage as alpha. White in
            // every channel, so the channel order of the bitmap does not matter.
            std::vector<std::uint8_t> texels(coverage.size() * kBytesPerPixel, 0xFF);
            for (std::size_t i = 0; i < coverage.size(); ++i) {
                texels[i * kBytesPerPixel + 3] = coverage[i];
            }
            RndBitmap bitmap;
            bitmap.Create(
                size, size, size * kBytesPerPixel, kBitsPerPixel, 0, nullptr, texels.data(), nullptr
            );
            RndTex *texture = Hmx::Object::New<RndTex>();
            // The texture copies the texels: `texels` may go once this returns.
            texture->SetBitmap(bitmap, nullptr, true, RndTex::kRegular);
            return texture;
        }

    }

    IconTextures::~IconTextures() {
        for (RndTex *texture : textures_) {
            delete texture;
        }
    }

    RndTex *IconTextures::TextureFor(nativeui::ImageHandle image) {
        const std::optional<nativeui::Icon> icon = nativeui::IconOf(image);
        if (!icon) {
            return next_ ? next_->TextureFor(image) : nullptr;
        }
        RndTex *&texture = textures_[static_cast<std::size_t>(*icon)];
        if (!texture) {
            const int size = nativeui::IconTexelSize(*icon);
            texture = MakeTexture(nativeui::IconCoverage(*icon, size), size);
        }
        return texture;
    }

    int IconTextures::MadeCount() const {
        return static_cast<int>(
            std::count_if(textures_.begin(), textures_.end(), [](const RndTex *texture) {
                return texture != nullptr;
            })
        );
    }

}
