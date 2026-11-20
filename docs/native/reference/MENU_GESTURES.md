# Menu gestures on a webcam: what was measured

The native port drives DC3's own menu-gesture detectors, unchanged
(`HandHeightGestureFilter` for the highlight, `DirectionGestureFilterSingleUser`
and `ArcDetector` for the select, both inside `HamNavList`), from a webcam
through MediaPipe instead of a Kinect. This page holds the numbers behind what
the port does differently in the menus, and behind the things that were tried
and not shipped. Measured 2026-11-19 and 20 on branch `np-menu-gestures`.

People have played this game on real webcams, menus included: on the GPD and
on a Mac, natively and in the browser. What does not exist is a measured
success **rate** from a live person, attempts counted against selects. Every
rate on this page comes from a scripted arm on a modelled camera and is
marked SYNTHETIC. The numbers marked REAL RECORDING are counts from recorded
GPD sessions played back; they compare builds on real camera frames, and they
are not rates either, because a recording does not say what the player was
trying to do.

## How it is measured

`scripts/menu_gesture_bench.py` drives a real engine through the real path:
pose socket, native sensor, `SkeletonUpdate`, `HamNavList`. It reads what
each list is doing from `GET /api/menu/state` (highlight, swipe amount, arc
nodes, every select with its source).

**The synthetic performer** raises an arm over about 0.45 s, nudges the hand
until the row it wants is lit, hovers, then sweeps the forearm across the
body, and tries again when nothing happens. Sweep size, speed, hover time and
aim error are drawn per trial from a seed. The camera is a pose every 1/rate
seconds with per-joint noise. `gpd` is the noise level used for every table
below: 7.5 to 29 mm a joint and axis, taken from frame-to-frame differences in
a real GPD webcam session at 7.5 frames a second, so it includes the player's
real movement and is an upper bound. `gpdvis` adds the wrist dropping under
MediaPipe's Tracked score as often as it does in the worse of the two
recordings (see "The Tracked gate").

What is counted, each as n of trials:

| term | meaning |
|---|---|
| first sweep | the first sweep selected the wanted row |
| within three | the wanted row was selected within three sweeps |
| wrong row | a select landed on another row |
| false select | a select before the performer swept at all |
| never | three sweeps, no select |
| read | the arm resting on one row for 5 s, drifting slowly, never sweeping. False if anything selects; "moved" if the highlight leaves the row |
| scan | 5 s of reading down the list row by row, 0.6 to 1.1 s a row, never sweeping. False if anything selects |
| idle | arms down for 5 s. False on any select or engagement |
| late | a select in the 1.2 s after a read or scan ends, while the arm comes down. Counted apart from read and scan false, as n of reads plus scans |
| stray | a select that no trial was watching for |

Lists: `main` (the main menu), `diff` (the difficulty list), `songs` (song
select, a row below the fifth, which needs the list scrolled) and `back` (the
help bar's Back, left hand).

**The recordings** are two playtest sessions recorded on the GPD on
2026-11-14 (`pose.dc3session`, 512 s and 928 s, camera at 3 to 11 frames a
second). A third session from the same machine has nobody in view during its
menus and was not used. `replay` plays the recorded skeleton stream by wall
time in front of one menu screen and counts the selects. Two cuts are used:

- **the windows**: 113 s around the 7 selects that the recorded game logged as
  made by gesture. These are moments where a player's sweep did select.
- **outside songs**: all 368 s of footage outside a song, in front of the main
  menu. Nobody in it was aiming at this screen's rows, so the count is "how
  often does this footage set a list off".

In those two sessions the players picked with a controller or the keyboard
for 50 of the 57 recorded selects, and by gesture for 7. Four of the 7 were
made with the song clock stopped and three with it running.

## Swipe, before and after

SYNTHETIC, `gpd` noise, all four lists pooled, 120 selects a run and rate.
"Before" is the base commit `da357dbf3` (4 runs at 30 Hz, 2 at 15 Hz, 4 at
8 Hz). "After" is this branch with its defaults (5, 6 and 10 runs).

| rate | | first sweep | within three | wrong row | false select | never | read: false | read: moved | scan: false | late | time, median / p90 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 30 Hz | before | 468/480 (98 %) | 477/480 | 1 | 2 | 0 | 5/128 | 10/128 | 1/96 | 4/224 | 1.6 / 4.5 s |
| 30 Hz | after | 581/600 (97 %) | 597/600 | 0 | 3 | 0 | 14/160 | 9/160 | 4/128 | 2/288 | 1.6 / 4.5 s |
| 15 Hz | before | 238/240 (99 %) | 240/240 | 0 | 0 | 0 | 1/64 | 5/64 | 1/32 | 3/96 | 1.7 / 4.5 s |
| 15 Hz | after | 696/720 (97 %) | 718/720 | 2 | 0 | 0 | 6/192 | 2/192 | 3/160 | 10/352 | 1.7 / 4.8 s |
| 8 Hz | before | 439/480 (91 %) | 480/480 | 0 | 0 | 0 | 5/128 | 16/128 | 1/32 | 2/160 | 1.8 / 5.2 s |
| 8 Hz | after | 1052/1200 (88 %) | 1196/1200 | 0 | 2 | 2 | 5/320 | 8/320 | 2/154 | 9/474 | 1.8 / 5.3 s |

Idle: 0 false in every cell (0/80 before, 0/168 after). Six scans of one
"after" cell at 8 Hz did not run (a late select had moved the screen on), so
that denominator is 154.

Side by side on the same seeds (three at 30 Hz, one at 15 and at 8 Hz), first
sweep before and after: 350/360 and 347/360, 119/120 and 115/120, 112/120 and
105/120.

**At 30 Hz the two builds run the same code in the menus**: the fill made no
frame in any 30 Hz cell on either. Those two rows therefore measure the spread
of the method. One binary on one seed gave 114 and then 116 first sweeps of
120, and 5/32 and then 1/32 reads with a select.

**Late selects.** About 2 times in 100, lowering the arm after reading picks
the row that was lit: 9/480 before, 21/1114 after. It is the image's detector
on this camera model and not something the branch changed; with exact joints
at 8 Hz it is 3/88 before and 2/160 after. Until 2026-11-20 the bench dropped
these selects. For the runs made before that the count comes from the engines'
logs (a select logged that no trial holds) and is a floor. The paired runs
were made with the bench that keeps them: no stray select in their 40 cells,
and the engines' logs hold no select the bench does not.

By list at 8 Hz, first sweep, before and after: `main` 114/120 and 270/300,
`diff` 115/120 and 269/300, `songs` 109/120 and 271/300, `back` 101/120 and
242/300. Back is the weak one at 8 Hz on both sides (84 % and 81 %), and it
gets there within three sweeps in 120/120 and 299/300. Median time to select
is 1.6 to 1.7 s on the short lists and 4.0 to 4.5 s on the song list, which
has to scroll first.

REAL RECORDING, selects counted per pass:

| cut | before | after |
|---|---|---|
| the windows (7 recorded gesture selects) | 4, 3, 2, 2 | 7 in each of 6 passes |
| outside songs (368 s), main menu | 8, 7, 6 | 2 in each of 5 passes |

The second row has no record of intent beyond the 7 logged selects: the extra
selects of the base build may be sweeps a player made or hands passing by.
Only the first row says something about reliability.

Read together: on the synthetic performer the branch costs first sweeps on a
slow camera, 97 % against 99 % at 15 Hz and 88 % against 91 % at 8 Hz, with
the wanted row picked within three sweeps in 99.7 % of trials or more on both
sides. What it buys there is a highlight that stays put under a resting hand
(8/320 moved against 16/128 at 8 Hz). On the recordings the base build lost
most of the sweeps that had selected when recorded, and this branch picks up
all seven. The two kinds of evidence disagree about the first sweep on a slow
camera; the default follows the recordings. `DC3_POSE_FILL_MENU=same
DC3_POSE_FILL_CLOCK=song` is the base behaviour.

## What changed

**Outside a song the pose fill is off (`DC3_POSE_FILL_MENU`, default `off`).**
The fill makes in-between frames for a slow camera so that scoring sees 30 a
second. Before this branch the same extrapolated frames went to the menu
detectors. SYNTHETIC, 8 Hz, `gpd`:

| menus' fill | first sweep | within three | wrong row | never | read: false | read: moved | time, median |
|---|---|---|---|---|---|---|---|
| `extrap` on the song clock (before) | 217/240 (90 %) | 240/240 | 0 | 0 | 3/64 | 8/64 | 1.8 s |
| `extrap`, clock carried on | 330/360 (92 %) | 359/360 | 1 | 0 | 4/96 | 12/96 | 1.8 s |
| `off` (default) | 310/360 (86 %) | 358/360 | 2 | 0 | 4/96 | 0/96 | 1.8 s |
| `interp` | 224/240 (93 %) | 240/240 | 0 | 0 | 1/64 | 1/64 | 2.0 s |
| `hold` | 196/240 (82 %) | 237/240 | 0 | 3 | 3/64 | 3/64 | 1.9 s |

(Other runs than the table above, where `off` is 1052/1200. Together:
1362/1560, 87 %.)

REAL RECORDING, the windows: `extrap` 4, 3, 2, 2 of 7; `off` 7, 7; `interp`
7, 5. Extrapolation overshoots a hand that stops, which is what moves the
highlight under a resting hand, and on real frames it broke up the sweeps.
`interp` is the alternative to try on a slow camera if sweeps get missed.

**The fill's clock no longer stops with the song clock (`DC3_POSE_FILL_CLOCK`,
default `stall`).** The fill timed camera frames on the song clock, which
stops outside a song: in the pause menu, between song previews and on the
results screens, 38 % and 47 % of the camera-frame intervals outside songs in
the two recordings. With it stopped `extrap` made no frames and `interp`
delivered none at all, camera frames included. The clock is now carried on at
UI time once it has stood still for 0.25 s. Commit `f4f05ea38` also says this
was why a noise-free Back sweep at 8 Hz failed (56 of 73). That attribution
was wrong: the failure is the arc detector's latch, below, and it happens
with the fill off too.

**Song and pause check** (one engine, 8 Hz camera): the fill is off in the
menus, `extrap` once the song starts and engaged 6.33 s in; paused, it is off
and released; resumed, it is engaged again about 0.14 s of song time later.

## The latch

`ArcDetector::SwipeFailed` sets `mHadProgress` when a sweep got past half way
and then failed, and the detector then refuses to start another until the
forearm has moved back at least 1 cm between two consecutive frames. With
exact joints at 8 Hz the hand never does that, so the latch stays set.
SYNTHETIC, no noise, 8 Hz: sweeps that set off with the latch set were taken 0
of 71 on the main menu and 0 of 106 on Back; without it, 36 of 38 and 24 of 26.
That is why the noise-free 8 Hz cells read worse than the noisy ones (515/600
first sweep, 72 never). It is an artifact of a hand with no noise: with `gpd`
noise 0 of 274 sweeps set off with the latch set. Nothing was changed for it.

Of those 274 sweeps, 35 were not taken: 17 ended with 2 nodes in the path, 9
with 3 and 9 with 4 or more.

## Tried and not shipped

**Scaling the arc detector's node cap by the camera rate.** `GetSwipeAmount`
holds a path of 2 nodes or fewer to half a swipe. The candidate lowered that
cap at low rates. SYNTHETIC, `gpd`, 360 selects a rate:

| rate | | first sweep | within three | false select | read: false | scan: false |
|---|---|---|---|---|---|---|
| 8 Hz | scaled | 318/360 (88 %) | 358/360 | | 3/96 | 2/96 |
| 8 Hz | the image's | 324/360 (90 %) | 360/360 | | 0/96 | 1/96 |
| 15 Hz | scaled | 339/360 (94 %) | | 3 | 8/96 | 8/96 |
| 15 Hz | the image's | 347/360 (96 %) | | 0 | 5/96 | 3/96 |

REAL RECORDING: the windows 7, 7 either way; outside songs 3, 3 scaled against
2, 2, 2. No gain in selects and more false ones. Dropped.

**Smoothing the hand.** Not built. The highlight moving under a resting hand
was the extrapolation (16/128 before, 8/320 after at 8 Hz), wrong rows are 2
in 2520,
and the latch shows that the arc detector needs the frame-to-frame movement a
filter would take away.

## The Tracked gate

`ArcDetector::TryToStartSwipe` puts the first node of a sweep's path down only
on a frame whose wrist and elbow are both Tracked. Every later node is taken
whatever its confidence. On a Kinect, Inferred means a guess behind an
occlusion. Natively a joint is Tracked when MediaPipe's visibility score is
0.6 or more, Inferred from 0.3, NotTracked below (`Skeleton_Native.cpp`).

REAL RECORDING, how often a raised arm (wrist 10 cm or more above the hip)
has its wrist or elbow under Tracked, counted in the recorded frames:

| footage | left arm | right arm |
|---|---|---|
| outside songs, both sessions | 469/898 (52 %) | 228/870 (26 %) |
| the worst window (one session, 54 s) | 158/205 (77 %) | 122/201 (61 %) |
| the same session's second window (32 s) | | 32/60 (53 %) |
| the other session's window (27 s) | 9/85 (11 %) | 3/44 (7 %) |

In the worse session's windows 244 of those frames were Inferred and 72
NotTracked, and a stretch under Tracked lasted 0.59 s on average (median
0.27 s). So on that camera the detector is refused a start on a large share
of frames. `gpdvis` models that worst window on the synthetic performer's
wrist: stretches under the Tracked score of 0.40 s on average, 0.23 s Tracked
between, 23 % of the low stretches under the NotTracked score.

**The switch: `DC3_MENU_ARC_START=inferred`** (default `tracked`, the image's
rule; `{native_settings set_menu_arc_start inferred}` at run time). A path may
then also start on a frame whose wrist and elbow the camera scored at least
Inferred. It reads the camera's own score and not the skeleton's, because the
pose estimate (`DC3_POSE_ESTIMATE`) hands the detectors every joint the camera
lost as Inferred, at a place it made up.

SYNTHETIC, `gpdvis`, two seeds, 240 selects a rate:

| rate | start | first sweep | within three | never | false select | read: false | scan: false | time, median |
|---|---|---|---|---|---|---|---|---|
| 30 Hz | `tracked` | 125/240 (52 %) | 215/240 | 25 | 0 | 1/64 | 0/64 | 4.2 s |
| 30 Hz | `inferred` | 206/240 (86 %) | 240/240 | 0 | 0 | 4/64 | 3/64 | 1.7 s |
| 15 Hz | `tracked` | 134/240 (56 %) | 208/240 | 32 | 0 | 0/64 | 2/64 | 3.6 s |
| 15 Hz | `inferred` | 208/240 (87 %) | 240/240 | 0 | 0 | 2/64 | 1/60 | 1.8 s |
| 8 Hz | `tracked` | 132/240 (55 %) | 210/240 | 30 | 0 | 0/64 | 2/64 | 3.7 s |
| 8 Hz | `inferred` | 195/240 (81 %) | 235/240 | 5 | 0 | 0/64 | 0/64 | 1.8 s |

Wrong rows 0 and idle 0/16 in every cell. Over the three rates: reads with a
select 1/192 under `tracked` and 6/192 under `inferred`; scans 4/192 and 4/188
(four scans of one `inferred` cell did not run, a late select having moved the
screen on). Late selects, from the engines' logs: 1 of 384 reads and scans
under `tracked`, 4 of 380 under `inferred`. For comparison, with the image's
rule on a wrist that is always seen (`gpd`, the "after" rows above) 25 of 672
reads had a select.

REAL RECORDING, two passes each: the windows 7, 7 under `tracked` and 8, 8
under `inferred`; outside songs 2, 2 and 3, 3. The seven recorded gesture
selects are picked either way. The one extra select is the same one in all
four `inferred` passes: the left hand's list, during a song's count-in, where
the player raises both arms. The path starts one frame earlier in the raise
and reaches 0.92 of a select on the way up (0.41 under `tracked`). In the game
no list is live at that moment, so this is not a menu false select, but it
shows what the switch costs: a path that starts earlier sees more of the arm
coming up. The right hand's sweeps in the windows are the same under both
(eight stretches reaching half a swipe, the same peaks).

Decision: shipped as a switch, off by default. On the synthetic performer it
is the largest effect measured on this branch, and it brings both rates back
to about where they are on a wrist that is always seen. On the recordings it
changes none of the selects that were made and adds one that was not meant.
The synthetic gain is an upper bound: the model is the worst window, and at
8 Hz it refuses 71 % of the frames on which the detector asks to start,
against 20 to 51 % in the replays. Whether sweeps get ignored on a real camera
often enough for this to matter is what the playtest can answer.

A first version read the skeleton's confidence. In the replay of the windows
it started a path on every frame the image had refused (37 and 43 of them,
left and right); with the camera's own score it is 8 of 32 and 13 of 42. The
rest were joints the camera had lost.

## Hold to select

`DC3_MENU_SELECT=hold|both` (default `swipe`; F1 steps through them): keep the
hand on a row and it is picked after 0.3 s of settling plus 1.2 s of hold
(`DC3_MENU_HOLD_MS`; 1.8 s for the left hand's list), with the list's own
slide as the progress bar and its own select on fire. The timer starts again
when the row changes or the hand drops. After a pick nothing holds again until
the hand has dropped for 0.3 s, moved 10 cm, or 2.5 s have passed. Never in
controller mode or in a song.

SYNTHETIC, 30 selects a cell at 30, 15 and 8 Hz; `gpd` and no noise on one
seed, `gpd` on a second:

| | first try | wrong row | time, median |
|---|---|---|---|
| `main`, `diff` | 540/540 | 0 | 1.9 s |
| `songs` | 270/270 | 0 | 4.4 to 4.7 s |
| `back` | 270/270 | 0 | 2.4 s |

1080 of 1080, at every rate, against the sweep's 88 % to 97 %. The cost is the
other column of the brief: a hand left resting on a row is a pick (every read,
by design), and reading down a list with the hand up can pick a row. Scans
with a select, at the default 1.2 s: `main` 1/88, `diff` 0/72, `songs` 20/88,
`back` 0/72; idle 0/72. With `DC3_MENU_HOLD_MS=1600`: `main` 0/16, `songs`
1/16; with 2000: 0 and 0. Time to select on the main menu goes 2.0, 2.3 and
2.7 s. On the second seed, run with the bench that keeps them: 1 late select
in 192 reads and scans (a hold that completed just as its scan ended), and no
stray one after 360 picks.

REAL RECORDING, selects per pass (the sweep's count in brackets): the windows
8 under `hold` and 12 under `both` (7); outside songs on the main menu 9 and 8
(2); on song select 7 and 5 (1). The footage has hands resting in the air, so
hold picks more than the sweep does. These are counts of selects from footage
not aimed at the screen, not false-select rates.

## Limits

- No success rate from a live person exists yet. The playtest is where it
  comes from: count attempts and picks per mode.
- The synthetic performer and the recordings disagree about the menus' fill
  (first sweep at 8 Hz: `off` 86 to 88 %, `extrap` 90 to 92 %; the windows:
  `off` 7 of 7, `extrap` 2 to 4).
- A replay count is not a rate, and replay timing is not bit-exact: the same
  build gave 4, 3, 2 and 2 on the same windows.
- The `gpd` noise level marks a wrist Inferred in 3.5 % of frames and an elbow
  never. The recordings are far worse than that (see "The Tracked gate").
  `gpdvis` models it and is used only in that section.
- The noise-free 8 Hz cells are a latch artifact and say nothing about a
  camera.
- Rare events spread widely from run to run (the 30 Hz rows): a difference of
  a few reads or first sweeps between two rows is inside that spread.
- The late selects were found on the last day. The scripted arm comes down in
  0.3 s and its forearm angle is reset in one step at the bottom, so the 2 in
  100 may overstate what a person's arm does. Nothing was changed for it.
- Twice during the runs `GET /api/menu/state` stopped answering in one engine
  of a matrix (17 trials lost from the "before" no-noise 8 Hz cell). It did
  not reproduce on its own.

## Re-running

```sh
# the matrix (one engine per list x rate x noise; about 10 minutes a seed)
python3 scripts/menu_gesture_bench.py run --out DIR \
    --lists main,diff,songs,back --rates 30,15,8 --noise none,gpd \
    --trials 30 --reads 8 --scans 8 --idles 2 --seed 1
python3 scripts/menu_gesture_bench.py table DIR1+DIR2    # "late", "stray": see above

# hold to select
python3 scripts/menu_gesture_bench.py run --out DIR --select hold \
    --env DC3_MENU_SELECT=hold --scans 8 ...

# a recording: the windows, or everything outside songs
python3 scripts/menu_gesture_bench.py replay --out DIR --screen main_screen \
    --window FILE:FROM:TO [--window ...] [--trace]
python3 scripts/menu_gesture_bench.py replay --out DIR --screen main_screen \
    --session FILE

# the switch end to end, 17 PASS/FAIL lines
python3 scripts/menu_gesture_bench.py check --out DIR

# unit tests
native/build/milo-tests --gtest_filter='MenuArc*:MenuHold*:MenuSelectSwitch*:PoseFill*'
```

Engines draw, so on this box every one of these needs the GPU environment and
no sandbox. A matrix cell is its own process and reads the bench script and
the engine binary when it starts: measure with a copy of the binary
(`--engine`) if the tree is being rebuilt.
