# Playtest runbook (Linux desktop)

One page for whoever runs a playtest of the native port: set the machine up
once, check it, play, quit, send the session back. PLAN
[track L](PLAN.md#track-l--live-playtest-readiness) rows L-1..L-3.

## Setting up a machine (once)

The player box builds only the game (`dc3-native`). It needs none of the
decomp toolchain (no wibo/MSVC, no `build/373307D9`, no `decomp.db`).

```sh
cd ~/code/milohax/dc3-decomp && git pull
cd ../milo-native-engine && git pull
cd ../dc3-decomp
scripts/playtest/bootstrap.sh                 # what is missing, in plain words
scripts/playtest/bootstrap.sh --fix           # install / build what it can
```

`--fix` installs into your home directory only: the pose tracker's Python
packages in `~/.local/share/dc3/pose-venv` (made by `uv` with Python 3.10,
pinned in `native/scripts/pose-requirements.txt`, ~450 MB), the three pose
models (checked by sha256), Dawn (from `../dc3-decomp-deps` or the pinned
release), the engine at the commit dc3 pins (only if the engine checkout is
clean), and `native/build/dc3-native`. System packages it only names
(`pacman -S ...`); it never runs sudo.

The game looks for that venv itself, so `./dc3-native` started any way (not
only through `play.sh`) runs the tracker in it. Its order: `DC3_POSE_PYTHON`
if you set it, then `${DC3_POSE_VENV:-~/.local/share/dc3/pose-venv}`, then the
checkout's `.venv`, then plain `python3`; the console log says which it took
(`Pose server python: ...`). If that Python lacks the packages, the on-screen
notice names it and `bootstrap.sh --fix`.

**Game data.** The game reads the archives in `orig-assets/gen`
(`main_xbox.hdr` + ten `.ark`, 6.6 GB) and, from `orig-assets/extracted`, only
the song folder list, `songs/songs.dta`, `songs/missing_song_data.dta` and the
skeleton milo (measured with strace over a boot and a whole song). `scripts/playtest/asset_manifest.py check` compares a box
against `native/playtest/assets-manifest.json`. The rest of `extracted/` is an
unpacked copy for tools and the web build; `extracted/sfx/gen/xma_pcm`,
`extracted/videos/*.webm` and `web-video-manifest.json` are generated for the
web build only, and a raw extraction's `.milo_xbox` files are smaller than this
box's inflated ones without being damaged. To copy just what the game needs
from a box that has it:

```sh
scripts/playtest/bootstrap.sh --fix --assets-from free@main-box:code/milohax/dc3-decomp/orig-assets
```

## Before every session: the check

```sh
scripts/play.sh --check
```

Each line is OK, WARN (plays, something is lost) or FAIL (fix first), with
what to do. It checks the build, the game data, the desktop session, the GPU
(Vulkan), sound, the pose packages and model, and the camera: it picks the
camera the way the tracker will (`--camera auto`, the default: the first
video-capture device), opens it, measures its frame rate and looks for a
person (stand where you will dance). `--no-person` skips that last part.

**Camera frame rate matters more than anything else on this list.** The game
scores each move from the poses it receives; a camera delivering 8 pictures
a second shows it a quarter of what 30 do, and the moves score rough and late
(a session at 7.7 accepted frames a second scored badly). The check WARNs
below 15. The usual cause is light: in a dim room a webcam lengthens its
exposure and slows down, so turn on more light or face a window, and check
again. (Do not change the camera's exposure settings by hand; camera
settings are being worked out in `np-pose-perf`.)

## Where to stand

The tracker needs your whole body in the picture, including your hands when
they are above your head. What the check measures is the truth; the numbers
below are starting points.

| | Starting point | Why |
|---|---|---|
| Distance | **2.3–2.7 m** from the camera (guess) | A Kinect plays at 1.8–3 m with a 58.5° x 45.6° view. A typical webcam's view is similar (~60–70° wide, ~45° high at 4:3); whole body plus raised hands at 45° high needs about 2.3 m for a 1.75 m person. Measured on the footage clip: 2.45 m left no room for raised hands, 2.66 m was "Looks good" |
| Camera height | Waist to chest, **0.8–1.2 m**, level (guess) | Looking down shortens the legs and the game's floor estimate tilts; the game levels a tilted camera but works best when it does not have to |
| Light | Bright, in front of you or from the side; no bright window behind you | MediaPipe loses limbs against a bright background; webcams slow down in the dark (the check warns under 15 fps, see above) |
| Background | Plain, nobody else in the picture | A second person is a second player (Battle wants exactly that; Perform does not) |
| Two players | Side by side, a step apart, both whole in the picture | Dance Battle starts only with two people on the sensor |

## Playing

```sh
scripts/play.sh                  # check, then play: 1280x720 window, the first camera
scripts/play.sh --camera /dev/v4l/by-id/usb-...   # one particular camera (ls /dev/v4l/by-id)
scripts/play.sh --no-pose        # keyboard only, nothing scored
```

With more than one camera plugged in, switch in the game: Options → Gameplay
Settings → **Camera**. Each select moves to the next camera, a notice names
it, tracking resumes on it a few seconds later, and the choice is remembered
for the next launch (`--camera` on the command line still wins for that run).
`python3 native/scripts/pose_server.py --list-cameras` prints what it will
cycle through.

Menus work with your hands, as on a Kinect: hold an arm out to the side to
grab that side's list, raise or lower the hand to move the highlight, sweep
the forearm across your body to pick. The left arm works the help bar (its
lower item is Back). Or use the keyboard; the first key press switches to
controller mode, and 5 s without a press hands the menus back to your hands.

| Key | Button | | Key | Button |
|---|---|---|---|---|
| Arrows | D-pad | | Space | Start (pause) |
| Enter | A | | Tab | Back |
| Esc, Backspace | B | | Q / E | LB / RB |
| X | X | | Z / C | LT / RT |
| Y | Y (Options) | | **Ctrl+Q** | **quit** |
| F11, Alt+Enter | fullscreen | | **F1** | menu select: swipe / hold / both |

Space opens the pause menu but does not close it; resume with Continue (the
original's behaviour). The map is printed at startup.

On song select, Left/Right jump to the previous/next section of the list,
Q/E move a screenful, and Y changes the sort (Skill Level, Title, Artist,
Source). The songs played last are at the top under Recently Played
([SONG_SELECT.md](SONG_SELECT.md)).

**Two ways to pick a row, to compare.** The original's is the sweep (`swipe`,
the default). The other is `hold`: keep your hand on the row and hold still;
after 0.3 s the row's bar starts to slide in as it does for a sweep, and when
it is full (1.5 s in all) the row is picked with the usual sound. Moving to
another row or dropping your arm starts it again, a scrolling list waits until
it stops, and after a pick nothing is picked again until you move your hand
(or 2.5 s pass), so one hold cannot carry you through two screens. It works
for the left arm's Back too, which takes a little longer (2.1 s) because
there is no row to aim at first. **F1** steps through `swipe`, `hold` and `both`
(either one picks) at any time, and a banner at the top says which is on;
`scripts/play.sh --menu-select hold` starts in one. F1 is not a controller
button: it does not take the menus away from your hands. What to compare:
how often the row you meant is the row you get, how long a pick takes, and
whether anything gets picked that you did not mean (with `hold`, a hand left
resting on a row IS a pick: read the list with your arm down, or keep the
hand moving). `DC3_MENU_HOLD_MS` (default 1200; `DC3_MENU_HOLD_LEFT_MS`,
default 1800, for Back) changes how long the bar takes. Measured with the
synthetic performer reading down a list with the hand up, 0.6 to 1.1 s a row:
at 1200 a row got picked in 1 of 88 five-second reads of the main menu and 20
of 88 of the song list, at 1600 in 0 and 1 of 16, at 2000 in none, for a pick
that takes 2.0, 2.3 and 2.7 s (the sweep: 1.6 s). If rows get picked while you
read, start with `DC3_MENU_HOLD_MS=1600`.

On a slow camera (the GPD's 7 to 11 frames a second) the menus now get the
camera's own frames: the in-between frames the game makes for scoring are for
songs only, because replaying two recorded GPD sessions they lost most of the
sweeps (2 to 4 picked of the 7 that were picked by gesture when recorded; 7
without them). `DC3_POSE_FILL_MENU=interp` is the alternative worth a try
there if sweeps get missed: smoother, about a fifth of a second behind your
hand. If sweeps are ignored although the highlight follows your hand, try
`DC3_MENU_ARC_START=inferred` (or, while it runs,
`{native_settings set_menu_arc_start inferred}` over `/api/dta/eval`): the
original starts a sweep only on a frame where the camera sees wrist and elbow
well, and on the GPD's recordings a raised arm was below that on a quarter to
half of the frames. Off by default: it also lets a sweep start while the arm
is still coming up. One thing to watch for with the sweep, in the original's
detector too: on the synthetic performer, lowering the arm after reading a
list picked the lit row about 2 times in 100. Numbers:
[reference/MENU_GESTURES.md](reference/MENU_GESTURES.md).

**Quit** with Ctrl+Q or by closing the window. Esc is B, not quit.

The window opens at 1280x720 and can be resized, maximised or made
fullscreen: the picture is scaled to fit, with black bars where the window's
shape differs from 16:9. **F11** or **Alt+Enter** toggles fullscreen;
`--fullscreen` starts that way. On a Wayland desktop the kit opens the window
through Xwayland (the game would fall back to it anyway: PLAN H-33).

### Back to the original, one switch each

Every addition to the menus has a switch that gives the original back. Set
it in the environment before starting the game.

| Addition | The original again |
|---|---|
| Song select: Recently Played, Artist sort, Left/Right by section, LB/RB by page, the remembered sort | `DC3_CLASSIC_SONG_SELECT=1` |
| A played console boots to the title screen | `DC3_BOOT_INTRO=1` |
| No pose fill in the menus, and a fill clock that does not stop | `DC3_POSE_FILL_MENU=same DC3_POSE_FILL_CLOCK=song` |
| Hold to select | off unless asked for: `DC3_MENU_SELECT=swipe` is the default |
| A sweep may start on an Inferred arm | off unless asked for: `DC3_MENU_ARC_START=tracked` is the default |

## Notices on screen

A banner at the top is the game telling you what a console's dashboard would
have. Most mean pose tracking stopped: the game carries on with a stand-in
skeleton and nothing you do is scored.

| Notice | Meaning | What to do |
|---|---|---|
| No camera: pose tracking off | No video-capture device (or, with `--camera N`, none at that number: a camera that re-connects can come back under another number, so `auto` is the default) | Plug in the camera, quit, start again with no `--camera` |
| Camera lost | The camera stopped sending pictures mid-session; the tracker looks for it again (by the same `--camera` rule) for about a minute | Check the cable; if it does not come back, quit and start again |
| Pose packages missing / Pose model missing / No python3 | The tracker could not start | `scripts/playtest/bootstrap.sh --fix` |
| Pose server did not start / exited / crashed | The tracker died; its own words are in the log | Send the session back |
| "Stand in front of the sensor" mid-song | You left the picture (or the tracker lost you) for about 2 s. Shorter losses are held (P-12, fixed). Continue is greyed out until the game sees you again | Step back into view, then Continue. If you were in view the whole time, mention the time |
| "Raise both hands to change Profile" bubbles | The game's sign-in hint; it lingers with two people (G-18) | Ignore |
| Kinect Tuner notice | That screen belongs to the 360 dashboard | Ignore |
| This display runs at 30 Hz (or another number under 50) | The display the game started on refreshes that many times a second, so the game draws that many frames a second however fast the machine is. Seen with a 4K TV on a laptop | Set the display to 60 Hz in the system's display settings; a 4K TV may need a 1080p mode, or an HDMI 2.0 cable or adapter. Then start the game again |

## Known issues (PLAN open rows, 2026-11-14)

- **Camera frame rate**: the main suspect for rough live scores (a session
  at 7.7 accepted frames a second). Light first; see the check above.
- **H-20** with a person moving in view, only the first pad press on a menu
  may land: use your hands for the menus, or step out of view to use a pad.
- **G-10** audio has not been judged by ear on this port: tell us how it
  sounds.
- **G-25** a Battle tie is recorded as lost (the original does the same).
- **H-24** memory grows about 90 MB a song (measured over 3 songs; longer
  is untested). If a long session slows down or quits, say after how many
  songs; quitting and restarting clears it.
- If a song shows no move cards, or you are paused while in view, say which
  song, which number it was in the session, and the time (W-10 and P-12 are
  fixed; a repeat is news).

## After the session: what to send back

When the game ends, `play.sh` prints a summary and packs the session folder:

```
Send back: ~/.local/share/dc3/playtest/sessions/20261114-153000.tar.gz (12M)
```

Send that file, plus a few words: what you played, what looked or felt wrong,
and roughly when (minutes into the session). The folder holds `engine.log`
(every message, move rating and frame-rate line), `pose.dc3session` (every
camera frame the game accepted, as the scorer saw it), the checks, the
machine, and the saves after the session.

Saves and settings live in `~/.local/share/dc3/playtest/{saves,cache}`, not the
developer's `~/.local/share/dc3/saves`: a playtest box starts as a new console
(tutorials included), developer saves never leak in, and each session's saves
are snapshotted into its folder. `--home DIR` uses another place.

## Replaying a session (developers)

A bug seen live replays offline from the session's `pose.dc3session`:

```sh
scripts/playtest/pose_session.py info pose.dc3session         # its song segments
scripts/playtest/pose_session.py cut pose.dc3session 4 song.dc3session
scripts/play.sh --replay song.dc3session                      # then play the same song
```

The replay holds the first frame through the menus, starts at the song's
count-in, and feeds the scorer every recorded frame at its recorded song time
with the recorded floor. The replay records itself too, so
`pose_session.py compare song.dc3session <replay session>/pose.dc3session`
says whether every frame arrived unchanged. Ratings then match the live run
as closely as two runs of the same input match each other: the scorer samples
the skeleton on render frames, so in real time a few borderline moves change
from run to run; with `DC3_FAST_TIME=1` a replay is repeatable move for move
(`scripts/playtest/replay_proof.py`, numbers in STATUS). Without
a camera, `scripts/play.sh --video clip.mp4` plays recorded footage through
the real tracker in step with the song (`scripts/playtest/make_footage.py`
cuts clips).
