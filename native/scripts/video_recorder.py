"""video_recorder.py -- the camera frames pose_server.py feeds MediaPipe, to a file.

pose_server.py --record-video PATH (the game passes it from
DC3_POSE_RECORD_VIDEO; scripts/play.sh sets that to <session>/camera.mkv when
video recording is on) hands every frame it is about to give MediaPipe to
VideoRecorder.offer(), together with the frame id and capture timestamp it
puts on that frame's pose packet.  A thread of its own encodes them; the
tracker's side is one copy and a put_nowait() on a bounded queue, so writing
can never hold up tracking.  A frame that finds the queue full is DROPPED and
counted -- never waited for.

What it writes, next to each other:

  PATH                 the frames, at the size the camera delivered them (no
                       resize: what MediaPipe saw), --record-codec (default
                       mp4v; MPEG-4 part 2 is what pip OpenCV's FFmpeg can
                       encode -- it has no H.264 encoder), in the container
                       PATH's extension names.  .mkv is the default because a
                       Matroska file cut off by a kill still plays up to the
                       cut; an .mp4 without its closing index does not.
  STEM.frames.tsv      one line per frame IN the video, in order: line k (not
                       counting # lines) is video frame k.

                         # dc3-camera-frames v1
                         # video camera.mkv codec mp4v size 640x480 nominal_fps 30 ...
                         # n frame_id ts ts_ms
                         0 1234 81234.567891234 0.0

                       frame_id and ts are exactly the pose packet's (ts is
                       time.monotonic() at capture), so they match the
                       frame_id and ts columns of the game's pose.dc3session
                       for every frame the game accepted -- and, through it,
                       song time.  ts_ms is the timestamp MediaPipe was given.
                       "# status ..." lines (every STATUS_EVERY frames) and a
                       closing "# stopped reason=... written=N dropped=D
                       bytes=B" line carry the counts; a file without the
                       closing line was cut off by a kill.

A file that already exists is never overwritten: a second server (the game
restarts a stalled one; a camera switch restarts it too) or a change of frame
size starts the next part, STEM-2.EXT, STEM-3.EXT, ... each with its own
sidecar.  Recording stops for good -- one log line -- when the parts reach
max_bytes or the disk's free space falls below min_free_bytes.

Log lines (stderr, grep VIDEO_RECORD): one when a part starts (path, size,
codec), one when it stops (path, frames written, frames dropped, MB, why).
"""

import os
import queue
import sys
import threading
import time

SIDECAR_HEADER = "# dc3-camera-frames v1"
SIDECAR_SUFFIX = ".frames.tsv"
STATUS_EVERY = 30          # frames between status lines (and size checks / flushes)
FREE_CHECK_EVERY = 300     # frames between free-space checks
DEFAULT_MAX_GB = 24.0      # ~3 h needs 1.5-9 GB at 640x480 (see the PLAN row); stop well short of a full disk
DEFAULT_MIN_FREE_GB = 10.0
DEFAULT_QUEUE_FRAMES = 60  # 2 s at 30 fps, ~55 MB of 640x480 frames
DEFAULT_CODEC = "mp4v"
GB = 1e9


def sidecar_path(video_path):
    stem, _ = os.path.splitext(video_path)
    return stem + SIDECAR_SUFFIX


def part_path(path, n):
    """Part n of PATH: PATH itself for 1, STEM-n.EXT after."""
    if n <= 1:
        return path
    stem, ext = os.path.splitext(path)
    return f"{stem}-{n}{ext}"


def first_free_part(path, start=1, exists=os.path.exists):
    n = start
    while exists(part_path(path, n)) or exists(sidecar_path(part_path(path, n))):
        n += 1
    return n


def parts_of(path, exists=os.path.exists):
    """Every recorded part of PATH, in order (stops at the first gap)."""
    out, n = [], 1
    while exists(part_path(path, n)) or exists(sidecar_path(part_path(path, n))):
        out.append(part_path(path, n))
        n += 1
    return out


def _log(msg):
    print(f"VIDEO_RECORD: {msg}", file=sys.stderr, flush=True)


class VideoRecorder:
    """See the module docstring.  `cv2` provides VideoWriter/VideoWriter_fourcc
    (injected by tests); `statvfs` and `getsize` are injectable too."""

    def __init__(self, path, cv2, fps=30.0, codec=DEFAULT_CODEC, max_bytes=DEFAULT_MAX_GB * GB,
                 min_free_bytes=DEFAULT_MIN_FREE_GB * GB, queue_frames=DEFAULT_QUEUE_FRAMES,
                 log=_log, statvfs=os.statvfs, getsize=os.path.getsize, copy=True):
        self.path = os.path.abspath(path)
        self.cv2 = cv2
        self.fps = float(fps) if fps and fps > 0 else 30.0
        self.codec = codec
        self.max_bytes = max_bytes
        self.min_free_bytes = min_free_bytes
        self.log = log
        self.statvfs = statvfs
        self.getsize = getsize
        self.copy = copy
        self.queue = queue.Queue(maxsize=max(1, int(queue_frames)))
        self.offered = 0
        self.dropped = 0           # all parts: the queue was full
        self.after_stop = 0        # offered after recording stopped (cap, disk, error)
        self.written = 0           # all parts
        self.stopped = None        # why recording stopped, once it has
        self.parts = []            # [{path, sidecar, written, dropped, bytes, size}]
        self._part = None
        self._part_dropped = 0     # drops while the current part is open
        self._done_bytes = 0       # closed parts' bytes
        self._next_n = 1
        os.makedirs(os.path.dirname(self.path) or ".", exist_ok=True)
        self._thread = threading.Thread(target=self._run, name="video-record", daemon=True)
        self._thread.start()

    # ---- the tracker's side: never blocks ---------------------------------
    def offer(self, frame, frame_id, ts, ts_ms):
        """Queue one frame; True if queued.  Never waits."""
        self.offered += 1
        if self.stopped is not None:
            self.after_stop += 1
            return False
        try:
            self.queue.put_nowait((frame.copy() if self.copy else frame, frame_id, ts, ts_ms))
        except queue.Full:
            self.dropped += 1
            self._part_dropped += 1
            return False
        return True

    def close(self, timeout_s=1.0):
        """Finish: frames already queued are written for up to timeout_s (the
        game SIGKILLs a server 2 s after SIGTERM), the rest are dropped and
        counted.  Safe to call twice."""
        deadline = time.monotonic() + timeout_s
        while True:
            try:
                self.queue.put(None, timeout=max(0.0, deadline - time.monotonic()))
                break
            except queue.Full:
                if time.monotonic() >= deadline:
                    self._stop_flag = True
                    break
        self._thread.join(max(0.05, deadline - time.monotonic()) + 0.5)

    _stop_flag = False   # close() could not queue its sentinel in time

    # ---- the writer thread -------------------------------------------------
    def _run(self):
        try:
            while True:
                if self._stop_flag:
                    self._drain_as_dropped()
                    self._finish("closed")
                    return
                try:
                    item = self.queue.get(timeout=0.25)
                except queue.Empty:
                    continue
                if item is None:
                    self._finish("closed")
                    return
                if self.stopped is not None:
                    self.after_stop += 1
                    continue
                self._write(*item)
        except Exception as e:  # noqa: BLE001 -- recording must never take the tracker down
            self._finish(f"error ({e.__class__.__name__}: {e})")

    def _drain_as_dropped(self):
        while True:
            try:
                item = self.queue.get_nowait()
            except queue.Empty:
                return
            if item is not None:
                self.dropped += 1
                self._part_dropped += 1

    def _open_part(self, size):
        n = first_free_part(self.path, self._next_n)
        self._next_n = n + 1
        vpath = part_path(self.path, n)
        w, h = size
        writer = self.cv2.VideoWriter(vpath, self.cv2.VideoWriter_fourcc(*self.codec), self.fps, (w, h))
        if not writer.isOpened():
            self._stop(f"the {self.codec} writer would not open {vpath}")
            return False
        side = open(sidecar_path(vpath), "w")
        side.write(f"{SIDECAR_HEADER}\n# video {os.path.basename(vpath)} codec {self.codec} "
                   f"size {w}x{h} nominal_fps {self.fps:g} started "
                   f"{time.strftime('%Y-%m-%dT%H:%M:%S%z')} pid {os.getpid()}\n"
                   "# n frame_id ts ts_ms   (ts: time.monotonic() at capture = the pose packet's "
                   "timestamp = pose.dc3session's ts column)\n")
        side.flush()
        self._part = {"path": vpath, "sidecar": sidecar_path(vpath), "writer": writer, "fh": side,
                      "written": 0, "size": size, "bytes": 0}
        self.parts.append(self._part)
        self.log(f"recording the camera frames to {vpath} ({w}x{h} {self.codec}, nominal "
                 f"{self.fps:g} fps; stops at {self.max_bytes / GB:.1f} GB or "
                 f"{self.min_free_bytes / GB:.1f} GB free; timestamps {sidecar_path(vpath)})")
        return True

    def _close_part(self, reason):
        p = self._part
        if p is None:
            return
        self._part = None
        try:
            p["writer"].release()
        except Exception:  # noqa: BLE001
            pass
        p["bytes"] = self._size(p["path"])
        p["dropped"] = self._part_dropped
        self._part_dropped = 0   # drops from here on belong to the next part
        self._done_bytes += p["bytes"]
        p["fh"].write(f"# stopped reason={reason.replace(' ', '_')} written={p['written']} "
                      f"dropped={p['dropped']} bytes={p['bytes']}\n")
        p["fh"].close()
        self.log(f"stopped {p['path']}: {p['written']} frames written, {p['dropped']} dropped, "
                 f"{p['bytes'] / 1e6:.1f} MB ({reason})")

    def _size(self, path):
        try:
            return self.getsize(path)
        except OSError:
            return 0

    def _stop(self, reason):
        if self.stopped is None:
            self.stopped = reason
            if self._part is None:
                self.log(f"not recording: {reason}")
        self._close_part(reason)
        self._drain_as_dropped_after_stop()

    def _drain_as_dropped_after_stop(self):
        while True:
            try:
                item = self.queue.get_nowait()
            except queue.Empty:
                return
            if item is None:
                self.queue.put_nowait(None)   # keep close()'s sentinel for _run
                return
            self.after_stop += 1

    def _finish(self, reason):
        if self.stopped is None:
            self.stopped = reason
        self._close_part(reason)

    def _write(self, frame, frame_id, ts, ts_ms):
        h, w = frame.shape[:2]
        if self._part is not None and self._part["size"] != (w, h):
            self._close_part(f"frame size changed to {w}x{h}")
        if self._part is None and not self._open_part((w, h)):
            return
        p = self._part
        p["writer"].write(frame)
        p["fh"].write(f"{p['written']} {frame_id} {ts!r} {ts_ms!r}\n")
        p["written"] += 1
        self.written += 1
        if p["written"] % STATUS_EVERY == 0:
            p["bytes"] = self._size(p["path"])
            p["fh"].write(f"# status written={p['written']} dropped={self._part_dropped} "
                          f"bytes={p['bytes']}\n")
            p["fh"].flush()
            if self._done_bytes + p["bytes"] >= self.max_bytes:
                self._stop(f"size cap {self.max_bytes / GB:.1f} GB reached")
                return
        if self.written % FREE_CHECK_EVERY == 0:
            try:
                st = self.statvfs(os.path.dirname(p["path"]))
                free = st.f_bavail * st.f_frsize
            except OSError:
                free = None
            if free is not None and free < self.min_free_bytes:
                self._stop(f"disk free {free / GB:.1f} GB under {self.min_free_bytes / GB:.1f} GB")


def read_sidecar(path):
    """(header comments, [(n, frame_id, ts, ts_ms)], counts) of one part's
    sidecar.  counts: the last status/stopped line's fields (ints), plus
    'stopped' (the reason, or None when the file was cut off)."""
    comments, rows, counts = [], [], {"stopped": None}
    with open(path) as fh:
        first = fh.readline().rstrip("\n")
        if first != SIDECAR_HEADER:
            raise ValueError(f"{path}: not a dc3-camera-frames v1 file (first line {first!r})")
        comments.append(first)
        for line in fh:
            line = line.rstrip("\n")
            if not line:
                continue
            if line.startswith("#"):
                comments.append(line)
                f = line[1:].split()
                if f and f[0] in ("status", "stopped"):
                    for kv in f[1:]:
                        k, _, v = kv.partition("=")
                        if k == "reason":
                            counts["stopped"] = v
                        elif v.lstrip("-").isdigit():
                            counts[k] = int(v)
                continue
            f = line.split()
            if len(f) != 4:
                continue   # a line cut off by a kill
            rows.append((int(f[0]), int(f[1]), float(f[2]), float(f[3])))
    return comments, rows, counts
