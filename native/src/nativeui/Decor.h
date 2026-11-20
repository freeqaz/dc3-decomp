// Decor: the pieces of look that several widgets share, so that a selection,
// a focus ring and a fading edge are drawn the same way everywhere.
//
// THE SELECTION RULE. Every region that can hold a selection (a list, a shelf,
// a tab strip) remembers it when focus moves away. Only the region that has
// focus shows a FILLED highlight; a region that merely remembers shows a
// quiet marker, an outline in Palette::selectionMarker. A screen therefore
// never shows two things that both look selected. `focus` below is the
// widget's own 0..1 focus amount, so the two looks crossfade.
#pragma once

#include "nativeui/DisplayList.h"
#include "nativeui/Theme.h"

namespace nativeui {

    // The light a focused control throws: half strength on the edge of `rect`,
    // gone Metrics::glowSoftness outside it, and never a flat plateau (a
    // plateau next to a bright fill reads as a dark line along its edge).
    void PaintFocusGlow(
        Painter &painter, const Theme &theme, const Rect &rect, float radius, float amount
    );

    // The bar under a selected row: with focus a filled bar (highlightStart to
    // highlightEnd) with a bright rim and a glow that falls away from its very
    // edge; without, an outline.
    void PaintSelectionBar(
        Painter &painter, const Theme &theme, const Rect &bar, float radius, float focus
    );

    // The frame around a selected card: with focus an accent ring; without,
    // the same quiet outline. `amount` scales the whole thing (a card one step
    // from the selection gets less of it). Paint it over the card, and
    // PaintFocusGlow(focus * amount) under it.
    void PaintSelectionRing(
        Painter &painter,
        const Theme &theme,
        const Rect &card,
        float radius,
        float focus,
        float amount
    );

    enum class Edge {
        Top,
        Bottom,
        Left,
        Right
    };

    // Fades what was just painted into Palette::well over the last
    // Metrics::edgeFade units before `edge` of `area`: the way a scrolling
    // region says its content goes on. `amount` 0..1 scales it (0: nothing).
    // It paints the well's colour over the content, so it is only right where
    // the well is what lies behind.
    void PaintEdgeFade(
        Painter &painter, const Theme &theme, const Rect &area, Edge edge, float amount
    );

}
