// Icon: the small shapes a screen draws that are not text and not pictures
// (a star, a die, a list).
//
// The display list has no polygon: everything is a rounded rectangle, a glyph
// or an image. So an icon IS an image: a white shape whose alpha is its
// coverage, drawn with an ImageQuad whose tint gives it its colour. The handle
// of each icon is fixed (IconImage), the shape is defined here as outlines in
// a unit square, and IconCoverage rasterises it at any size. Whoever resolves
// image handles (the host's renderer, a test's rasteriser) recognises an icon
// handle with IconOf and makes its texture from IconCoverage; nothing is
// loaded from a file.
//
// Rules:
//  - Icons are single-colour. The tint is the colour; the texture is white.
//  - A shape is drawn to fill its square, so an icon's rectangle is its size.
//  - Adding an icon is adding an enumerator and its outline in Icon.cpp.
#pragma once

#include "nativeui/DisplayList.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace nativeui {

    enum class Icon : std::uint8_t {
        Star, // a filled five-pointed star
        StarOutline, // the same star as a line
        Shuffle, // a die showing five: "pick one for me"
        Playlist, // a bulleted list
        Search // a magnifying glass
    };
    inline constexpr int kIconCount = 5;

    // The image handle that means `icon`. Icon handles sit at the top of the
    // handle space, where no cache hands out ids.
    ImageHandle IconImage(Icon icon);
    // The icon `image` stands for, if it is an icon handle.
    std::optional<Icon> IconOf(ImageHandle image);

    // `icon` rasterised into `size` x `size` coverage bytes (0 = outside,
    // 255 = inside), rows top to bottom, antialiased.
    std::vector<std::uint8_t> IconCoverage(Icon icon, int size);

    // How many texels a side an icon's texture needs to stay sharp: small marks
    // (stars) are drawn about a dozen units high, the others fill a tile.
    int IconTexelSize(Icon icon);

}
