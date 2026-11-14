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

The data path must be all lower case (the game lower-cases file paths, PLAN
H-17).

## Before every session: the check

```sh
scripts/play.sh --check
```

Each line is OK, WARN (plays, something is lost) or FAIL (fix first), with
what to do. It checks the build, the game data, the desktop session, the GPU
(Vulkan), sound, the pose packages and model, and the camera: it opens it,
measures its frame rate and looks for a person (stand where you will dance).
`--no-person` skips that last part.

## Where to stand

The tracker needs your whole body in the picture, including your hands when
they are above your head. What the check measures is the truth; the numbers
below are starting points.

| | Starting point | Why |
|---|---|---|
| Distance | **2.3–2.7 m** from the camera (guess) | A Kinect plays at 1.8–3 m with a 58.5° x 45.6° view. A typical webcam's view is similar (~60–70° wide, ~45° high at 4:3); whole body plus raised hands at 45° high needs about 2.3 m for a 1.75 m person. Measured on the footage clip: 2.45 m left no room for raised hands, 2.66 m was "Looks good" |
| Camera height | Waist to chest, **0.8–1.2 m**, level (guess) | Looking down shortens the legs and the game's floor estimate tilts; the game levels a tilted camera but works best when it does not have to |
| Light | In front of you or from the side; no bright window behind you | MediaPipe loses limbs against a bright background; webcams also slow down in the dark (the check warns under 24 fps) |
| Background | Plain, nobody else in the picture | A second person is a second player (Battle wants exactly that; Perform does not) |
| Two players | Side by side, a step apart, both whole in the picture | Dance Battle starts only with two people on the sensor |

## Playing

```sh
scripts/play.sh                  # check, then play: 1280x720 window, camera 0
scripts/play.sh --camera 1       # another camera
scripts/play.sh --no-pose        # keyboard only, nothing scored
```

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

Space opens the pause menu but does not close it; resume with Continue (the
original's behaviour). The map is printed at startup.

**Quit** with Ctrl+Q or by closing the window. Esc is B, not quit.

The window is 1280x720. **Do not maximise it or make it fullscreen:** the
game then draws into the top-left corner (PLAN H-18); `--fullscreen` says so
and stays windowed. On a Wayland desktop the kit opens the window through
Xwayland, so a scaled (HiDPI) screen shows the whole picture.

## Notices on screen

A banner at the top means pose tracking stopped; the game carries on with a
stand-in skeleton and nothing you do is scored.

| Notice | Meaning | What to do |
|---|---|---|
| No camera: pose tracking off | No `/dev/video` device at that index | Plug in the camera, quit, start again (or `--camera N`) |
| Camera lost | The camera stopped sending pictures mid-session | Check the cable; quit and start again |
| Pose packages missing / Pose model missing / No python3 | The tracker could not start | `scripts/playtest/bootstrap.sh --fix` |
| Pose server did not start / exited / crashed | The tracker died; its own words are in the log | Send the session back |
| "Stand in front of the sensor" mid-song while you are in view | Known bug P-12 (the tracker loses you briefly) | Press Continue (Enter), keep dancing, mention the time |
| "Raise both hands to change Profile" bubbles | The game's sign-in hint; it lingers with two people (G-18) | Ignore |
| Kinect Tuner notice | That screen belongs to the 360 dashboard | Ignore |

## Known issues (PLAN open rows, 2026-11-13)

- **P-12** a dancer fully in view can be paused mid-song (above). Lane
  `np-tracking`.
- **W-10** once, the second song with the camera was never scored. If a song
  shows no move cards, say which song and which number it was.
- **L-4** the latency default for a live camera is not settled; scores may run
  low or high.
- **H-18** no fullscreen; a maximised window shows the game in a corner.
- **G-10** audio has not been judged by ear on this port: tell us how it
  sounds. **G-26** movies have no sound.
- **G-11** Rehearse shows no dancer.
- **G-22** Story's "Sign Into a Gamer Profile" does nothing. **G-25** a Battle
  tie is recorded as lost.

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
