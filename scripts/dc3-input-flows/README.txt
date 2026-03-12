# DC3 Native Port — Scripted Input Flows
#
# These files drive the native port through menu screens via MILO_INPUT_SCRIPT.
#
# Format: one command per line
#   frame_number button_name
#   # comments start with #
#
# Available buttons:
#   confirm   — A button (kAction_Confirm / kPad_X / 0x40)
#   cancel    — B button (kAction_Cancel)
#   start     — Start button
#   up        — D-pad up
#   down      — D-pad down
#   left      — D-pad left
#   right     — D-pad right
#
# Usage:
#   MILO_HEADLESS=1 MILO_MAX_FRAMES=2000 \
#     MILO_INPUT_SCRIPT=scripts/dc3-input-flows/ymca.txt \
#     timeout 120 native/build/dc3-native 2>&1 | grep goto_screen
#
# Timing notes:
#   - Boot screens auto-advance in ~300 frames (attract → main_screen)
#   - main_screen is ready at frame ~380
#   - Space buttons 100+ frames apart (engine frame counter can skip)
#   - Each button is active for exactly 1 frame
#
# Requires: HamNavList IsAnimating() bypass (#ifdef HX_NATIVE in HamNavList.cpp)
# Without it, all button input is rejected because IsAnimating() stays true forever.
