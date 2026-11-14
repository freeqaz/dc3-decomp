# DC3 Native Port — Scripted Input Flows
#
# These files drive the native port through menu screens via MILO_INPUT_SCRIPT.
#
# Format: one command per line (# comments, blank lines OK)
#
#   Absolute frame:    frame_number button_name
#   Wait for screen:   wait_screen screen_name
#   Relative offset:   +N button_name       (N frames after last wait satisfied)
#   Wake:              N wake / +N wake     "the player presses the wake button"
#   Highlight by name: +N highlight NAME / highlight NAME
#
# `highlight NAME` presses down/up (one press per 15 frames, each once the
# list has settled) until the focused HamNavList's highlighted entry is the
# data symbol NAME, e.g. `+30 highlight omg` then `+60 confirm`.  A list whose
# rows move -- downloadable songs (DC3_DLC_DIR) are listed among the disc's,
# sorted by difficulty -- does not move the flow, where a counted run of downs
# lands on a different song.  It logs
#   DC3 Input: highlight 'omg' satisfied at frame F (P presses)
# and the next relative offset counts from that frame.  If the focused
# component is not a HamNavList, NAME is not in its data (for 120 frames), or
# 3600 frames pass,
# it logs `DC3 Input: TIMEOUT highlight ...` and abandons the rest of the
# script: a flow that cannot find its song must not confirm a different one.
# Directive in the engine (milo-native-engine src/platform/Joypad_Native.cpp);
# the list query is dc3's native/src/platform/JoypadSelect_Native.cpp.  Flows
# that test scrolling itself (song-select-scroll.txt) and flows shared with
# the original under Xenia (ymca.txt, xenia-ymca.txt) keep counted presses.
#
# `wake` is for flows shared with the original game under Xenia, and for
# native runs with a pose provider (the default policy auto is then faithful),
# where a pad press outside controller mode only enters controller mode and
# controller mode times out after ~5 s without pad input: put one before each
# screen's first press there.  It does nothing when the game is already in
# controller mode (always under a forced policy -- the native default with no
# camera); otherwise it
# presses the wake button for one frame (l3, or MILO_INPUT_WAKE_BUTTON).  See
# docs/debugging/native.md "Controller Mode Policy".
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
