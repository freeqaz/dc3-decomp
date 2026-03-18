# DC3 Native Port — Scripted Input Flows
#
# These files drive the native port through menu screens via MILO_INPUT_SCRIPT.
#
# Format: one command per line (# comments, blank lines OK)
#
#   Absolute frame:    frame_number button_name
#   Wait for screen:   wait_screen screen_name
#   Relative offset:   +N button_name       (N frames after last wait satisfied)
#
# wait_screen blocks until TheUI->CurrentScreen() matches AND the
# transition is complete. Times out after 30s with a warning.
#
# Available buttons:
#   confirm/a — A button (kAction_Confirm / kPad_X / 0x40)
#   cancel/b  — B button (kAction_Cancel)
#   start     — Start button
#   up        — D-pad up
#   down      — D-pad down
#   left      — D-pad left
#   right     — D-pad right
#   x, y      — Face buttons
#   l1/lb, r1/rb, l2/lt, r2/rt — Bumpers/triggers
#   l3/ls, r3/rs — Stick clicks
#   option/back/select — Back button
#
# Usage:
#   MILO_HEADLESS=1 MILO_MAX_FRAMES=3000 \
#     MILO_INPUT_SCRIPT=scripts/dc3-input-flows/song-scroll-test.txt \
#     timeout 120 native/build/dc3-native 2>&1 | grep "DC3"
#
# Example:
#   wait_screen main_screen
#   +30 confirm
#   wait_screen choose_mode_screen
#   +30 confirm
#
# Timing notes:
#   - Each button is active for exactly 1 frame
#   - Space relative offsets 30+ frames apart (scrolls need ~10 frames to complete)
#   - Absolute frame numbers still work (backwards compatible)
#
# Requires: HamNavList IsAnimating() bypass (#ifdef HX_NATIVE in HamNavList.cpp)
# Without it, all button input is rejected because IsAnimating() stays true forever.
