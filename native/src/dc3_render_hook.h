// dc3_render_hook.h -- what other DC3 glue may ask of HamRenderHook
// (dc3_render_hook.cpp).
#pragma once

// A function HamRenderHook::DrawGameOverlay calls every frame, inside the
// overlay pass (the finished frame, 1x, no depth): the place to draw with the
// engine's 2D canvas over the game. Null (the default) = nothing. One slot;
// the last caller wins.
void SetGameOverlayDraw(void (*draw)());
