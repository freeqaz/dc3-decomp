#!/usr/bin/env python3
"""camera_video.py -- a playtest's camera video, on the song's time line.

scripts/play.sh --record-video (or DC3_PLAY_RECORD_VIDEO=1 in the machine's
playtest.env) makes the pose server write every frame it tracks to
<session>/camera.mkv, with camera.frames.tsv: line k = video frame k, its pose
packet's frame_id and capture timestamp ts (native/scripts/video_recorder.py).
The game's pose.dc3session has the same frame_id and ts for every frame it
accepted, with the song clock at acceptance -- and, in its header, steady_t0,
the zero of its wall column on the same clock.  So each video frame has a song
time at CAPTURE:

    song_at_capture = song - (wall + steady_t0 - ts)

(a session without steady_t0, from before it, assumes the quickest frame of
the song took no time from capture to acceptance, and says so).  A frame the
game did not take gets its neighbour's, moved by the difference in ts.

  camera_video.py info SESSION
        the video's parts, frames, drops, and per song segment of
        pose.dc3session (numbered as pose_session.py info numbers them) the
        video frames that cover it
  camera_video.py cut SESSION N OUT.mkv [--pre S] [--post S] [--fps F]
        segment N as a clip at a constant F fps (default 30) starting S
        seconds (default 8, or the count-in if longer) before song time 0:
        the shape scripts/playtest/footage_camera.py plays in step with the
        song.  Replay it in the game with
            scripts/play.sh --video OUT.mkv --video-pre S
        or through the bare server, with any MediaPipe options, with
            native/scripts/pose_server.py --video OUT.mkv [--conf 0.3 ...]
        OUT's stem + .cut.tsv says which recorded frame each clip frame is.
  camera_video.py detect SESSION N [--out FILE.tsv] [-- POSE_SERVER_OPTIONS]
        re-run the tracker -- pose_server.py's own MediaPipe backend, built
        from POSE_SERVER_OPTIONS (--conf, --max-persons, --probe-second-person,
        --model, ...) -- over segment N's recorded frames, every one, in
        order, with their own MediaPipe timestamps, and compare with what the
        game got live: per frame, people detected now against valid people in
        pose.dc3session.  No game, no window, no camera.

Sync, honestly: the clip places each frame at its capture song time, so a
song-synced replay shows the player as the camera saw her at that point of the
song.  It does not reproduce the camera's own exposure delay (a recorded file
declares 0 ms of scoring latency; the live camera left the game at its
100 ms -- pass DC3_POSE_LATENCY_MS=100 for a scoring-faithful replay), and
over a pause (the song clock stops, the camera does not) the clip holds the
last frame before the pause.
"""
import argparse
import bisect
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(REPO, "native", "scripts"))
import pose_session  # noqa: E402
import video_recorder  # noqa: E402

VIDEO_NAME = "camera.mkv"


class Row:
    __slots__ = ("part", "n", "frame_id", "ts", "ts_ms", "song", "matched", "live_valid")

    def __init__(self, part, n, frame_id, ts, ts_ms):
        self.part, self.n, self.frame_id, self.ts, self.ts_ms = part, n, frame_id, ts, ts_ms
        self.song = None
        self.matched = None     # the session frame the game accepted, if it did
        self.live_valid = -1    # its valid people (-1: the game did not take this frame)


def steady_t0(comments):
    for c in comments:
        f = c[1:].split()
        if len(f) == 2 and f[0] == "steady_t0":
            return float(f[1])
    return None


def load_video_rows(session, name=VIDEO_NAME):
    """(parts, rows) -- every recorded frame of every part, in capture order."""
    parts = video_recorder.parts_of(os.path.join(session, name))
    rows = []
    for i, p in enumerate(parts):
        _, prow, _ = video_recorder.read_sidecar(video_recorder.sidecar_path(p))
        rows.extend(Row(i, n, fid, ts, ms) for n, fid, ts, ms in prow)
    rows.sort(key=lambda r: r.ts)
    return parts, rows


class Timeline:
    """The session's song segments with the video rows placed on them."""

    def __init__(self, session, name=VIDEO_NAME):
        self.session = session
        self.parts, self.rows = load_video_rows(session, name)
        comments, self.frames = pose_session.load(os.path.join(session, "pose.dc3session"))
        self.t0 = steady_t0(comments)
        self.segs = pose_session.segments(self.frames)
        self.desc = pose_session.describe(self.frames, self.segs)
        self.by_ts = {}
        for r in self.rows:
            self.by_ts.setdefault(r.ts, r)
        self.ts_list = [r.ts for r in self.rows]

    def segment(self, n):
        """(description, rows) of segment n (1-based): the video rows from the
        first to the last frame of it the game accepted, with song times."""
        if not 1 <= n <= len(self.segs):
            raise ValueError(f"no segment {n} (there are {len(self.segs)})")
        a, b = self.segs[n - 1]
        sess = self.frames[a:b]
        for r in self.rows:
            r.song, r.matched, r.live_valid = None, None, -1
        matched = []
        for f in sess:
            if f.frame_id & pose_session.HELD_ID:
                continue   # the game's own repeat of a frame, not a camera frame
            ts = float(f.lines[0].split()[6])
            r = self.by_ts.get(ts)
            if r is not None and r.frame_id == f.frame_id and r.matched is None:
                r.matched = f
                matched.append((r, f))
        if not matched:
            return self.desc[n - 1], [], None
        if self.t0 is not None:
            delay = lambda r, f: f.wall + self.t0 - r.ts  # noqa: E731
            how = "steady_t0"
        else:
            floor = min(f.wall - r.ts for r, f in matched)
            delay = lambda r, f: (f.wall - r.ts) - floor  # noqa: E731
            how = "no steady_t0 in pose.dc3session: the quickest frame taken as 0 ms"
        for r, f in matched:
            r.live_valid = f.valid
            r.song = f.song - delay(r, f)
        lo = bisect.bisect_left(self.ts_list, matched[0][0].ts)
        hi = bisect.bisect_right(self.ts_list, matched[-1][0].ts)
        return self.desc[n - 1], self._fill(lo, hi), how

    def _fill(self, lo, hi, extend_before_s=0.0, extend_after_s=0.0):
        rows = self.rows
        # widen by time, then give every row a song time from its nearest
        # matched neighbour; keep it non-decreasing (a pause)
        t_lo = rows[lo].ts - extend_before_s
        t_hi = rows[hi - 1].ts + extend_after_s
        lo = bisect.bisect_left(self.ts_list, t_lo)
        hi = bisect.bisect_right(self.ts_list, t_hi)
        span = rows[lo:hi]
        last = None
        for r in span:
            if r.song is not None:
                last = r
            elif last is not None:
                r.song = last.song + (r.ts - last.ts)
        nxt = None
        for r in reversed(span):
            if r.matched is not None:
                nxt = r
            elif r.song is None and nxt is not None:
                r.song = nxt.song - (nxt.ts - r.ts)
        top = -math.inf
        for r in span:
            top = max(top, r.song)
            r.song = top
        return span

    def segment_with_margin(self, n, before_s, after_s):
        d, rows, how = self.segment(n)
        if not rows:
            return d, rows, how
        lo = self.rows.index(rows[0])
        hi = self.rows.index(rows[-1]) + 1
        return d, self._fill(lo, hi, before_s, after_s), how


class FrameReader:
    """Frames of the parts, by (part, n), read forwards."""

    def __init__(self, parts, cv2):
        self.parts, self.cv2 = parts, cv2
        self.cap, self.part, self.n, self.cur = None, None, -1, None

    def get(self, part, n):
        if part != self.part or n < self.n:
            if self.cap is not None:
                self.cap.release()
            self.cap = self.cv2.VideoCapture(self.parts[part])
            self.part, self.n, self.cur = part, -1, None
        while self.n < n:
            ok, f = self.cap.read()
            if not ok:
                return self.cur
            self.n += 1
            self.cur = f
        return self.cur

    def close(self):
        if self.cap is not None:
            self.cap.release()


def cmd_info(a):
    tl = Timeline(a.session)
    print(f"{a.session}: {len(tl.parts)} part(s), {len(tl.rows)} frames")
    for p in tl.parts:
        _, rows, counts = video_recorder.read_sidecar(video_recorder.sidecar_path(p))
        span = rows[-1][2] - rows[0][2] if len(rows) > 1 else 0.0
        size = os.path.getsize(p) if os.path.exists(p) else 0
        print(f"  {os.path.basename(p)}: {len(rows)} frames over {span:.1f} s "
              f"({len(rows) / span if span else 0:.1f} fps), {size / 1e6:.0f} MB, "
              f"{counts.get('dropped', 0)} dropped, {counts['stopped'] or 'CUT OFF (no closing line)'}")
    print(f"  time base: {'steady_t0 ' + format(tl.t0, '.3f') if tl.t0 is not None else 'none in pose.dc3session'}")
    for d in tl.desc:
        _, rows, _ = tl.segment(d["n"])
        took = sum(1 for r in rows if r.matched is not None)
        print(f"  seg{d['n']:02d}  {d['kind']:<11}  song {d['song_from']:8.2f}..{d['song_to']:8.2f} s  "
              f"{len(rows):6d} video frames, {took} of the game's {d['frames']} found in the video")
    return 0


def cmd_cut(a, cv2=None):
    if cv2 is None:
        import cv2
    tl = Timeline(a.session)
    d0 = tl.desc[a.n - 1] if 1 <= a.n <= len(tl.desc) else None
    if d0 is None:
        print(f"no segment {a.n} (there are {len(tl.desc)})", file=sys.stderr)
        return 2
    pre = a.pre if a.pre is not None else max(8.0, math.ceil(-d0["song_from"]))
    d, rows, how = tl.segment_with_margin(a.n, pre + 1.0, a.post + 1.0)
    if not rows:
        print(f"segment {a.n}: none of its frames are in the video", file=sys.stderr)
        return 3
    songs = [r.song for r in rows]
    reader = FrameReader(tl.parts, cv2)
    first = reader.get(rows[0].part, rows[0].n)
    h, w = first.shape[:2]
    writer = cv2.VideoWriter(a.out, cv2.VideoWriter_fourcc(*a.codec), a.fps, (w, h))
    if not writer.isOpened():
        print(f"cannot write {a.out} with {a.codec}", file=sys.stderr)
        return 4
    n_out = int(math.floor((d["song_to"] + a.post + pre) * a.fps)) + 1
    stem = os.path.splitext(a.out)[0]
    held = 0
    with open(stem + ".cut.tsv", "w") as side:
        side.write(f"# dc3-camera-cut v1\n# from {os.path.abspath(a.session)} segment {a.n} "
                   f"pre {pre:g} fps {a.fps:g} time base {how}\n# k song frame_id ts\n")
        for k in range(n_out):
            song = k / a.fps - pre
            i = bisect.bisect_right(songs, song) - 1
            if i < 0:
                i, held = 0, held + 1
            r = rows[i]
            f = reader.get(r.part, r.n)
            if f.shape[:2] != (h, w):
                f = cv2.resize(f, (w, h))
            writer.write(f)
            side.write(f"{k} {song:.4f} {r.frame_id} {r.ts!r}\n")
    writer.release()
    reader.close()
    print(f"{a.out}: segment {a.n} (song {d['song_from']:.2f}..{d['song_to']:.2f} s), {n_out} frames "
          f"at {a.fps:g} fps from {pre:g} s before song time 0 ({how}); {stem}.cut.tsv")
    print(f"  in the game:  scripts/play.sh --video {a.out} --video-pre {pre:g}")
    print(f"  bare server:  native/scripts/pose_server.py --video {a.out} [MediaPipe options]")
    return 0


def server_backend(server_args):
    """pose_server.py's own backend, from its own command line."""
    import pose_server
    args = pose_server.parse_args(list(server_args))
    backend, _, _ = pose_server._default_backend(args)
    return backend, args


def cmd_detect(a, cv2=None, make_backend=server_backend):
    if cv2 is None:
        import cv2
    tl = Timeline(a.session)
    d, rows, how = tl.segment(a.n)
    if not rows:
        print(f"segment {a.n}: none of its frames are in the video", file=sys.stderr)
        return 3
    backend, args = make_backend(a.server_args)
    max_persons = getattr(args, "max_persons", 6)
    reader = FrameReader(tl.parts, cv2)
    out = open(a.out, "w") if a.out else None
    if out:
        out.write("# part n frame_id ts song live_valid replay_persons\n")
    last_ms = -1.0
    both = live_only = replay_only = neither = 0
    for r in rows:
        f = reader.get(r.part, r.n)
        ms = max((r.ts - rows[0].ts) * 1000.0, last_ms + 1.0)   # strictly increasing
        last_ms = ms
        persons = list(backend.process(f, ms))[:max_persons]
        n_now = len(persons)
        if r.live_valid >= 0:
            live = r.live_valid > 0
            now = n_now > 0
            both += live and now
            live_only += live and not now
            replay_only += now and not live
            neither += not live and not now
        if out:
            out.write(f"{r.part} {r.n} {r.frame_id} {r.ts!r} {r.song:.4f} {r.live_valid} {n_now}\n")
    reader.close()
    if out:
        out.close()
    compared = both + live_only + replay_only + neither
    print(f"segment {a.n} (song {d['song_from']:.2f}..{d['song_to']:.2f} s): {len(rows)} frames re-tracked, "
          f"{compared} of them taken by the game live")
    print(f"  a person seen: both {both}, live only {live_only}, now only {replay_only}, neither {neither}")
    if a.out:
        print(f"  per frame: {a.out}")
    return 0


def main(argv=None, cv2=None, make_backend=server_backend):
    argv = list(sys.argv[1:] if argv is None else argv)
    server_args = []
    if "--" in argv:
        k = argv.index("--")
        argv, server_args = argv[:k], argv[k + 1:]
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("info")
    s.add_argument("session")
    s = sub.add_parser("cut")
    s.add_argument("session")
    s.add_argument("n", type=int)
    s.add_argument("out")
    s.add_argument("--pre", type=float, default=None)
    s.add_argument("--post", type=float, default=2.0)
    s.add_argument("--fps", type=float, default=30.0)
    s.add_argument("--codec", default="mp4v")
    s = sub.add_parser("detect")
    s.add_argument("session")
    s.add_argument("n", type=int)
    s.add_argument("--out")
    a = ap.parse_args(argv)
    a.server_args = server_args
    if a.cmd == "info":
        return cmd_info(a)
    if a.cmd == "cut":
        return cmd_cut(a, cv2)
    return cmd_detect(a, cv2, make_backend)


if __name__ == "__main__":
    sys.exit(main())
