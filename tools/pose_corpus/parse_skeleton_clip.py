#!/usr/bin/env python3
"""Parser for Dance Central 3 ``.clp`` skeleton-clip files (SkeletonClip recordings).

DC3 ships a complete Kinect skeleton recorder in ``src/system/gesture/SkeletonClip.cpp``.
When song recording is enabled the game writes one ``.clp`` file per song containing every
polled ``SkeletonFrame`` for the active player.  This module recovers that on-disk format
and decodes it into numpy arrays so recordings from a real Xbox 360 + Kinect can be used
as ground truth for the native (MediaPipe) pose pipeline.

===============================================================================
FORMAT SPEC  (recovered from the decompiled, byte-matching source)
===============================================================================

Sources of truth (all at or near 100% objdiff match against the retail/debug binary):

  SkeletonClip::WriteClipHeader     src/system/gesture/SkeletonClip.cpp:552   (99.3%, regswap only)
  SkeletonClip::WriteClipFrame      src/system/gesture/SkeletonClip.cpp:564   (100%)
  SkeletonClip::WriteClip           src/system/gesture/SkeletonClip.cpp:581   (100%)
  SkeletonClip::StopRecordingNoClear src/system/gesture/SkeletonClip.cpp:593  (100%)
  SkeletonClip::LoadClipFromFile    src/system/gesture/SkeletonClip.cpp:657   (100%)
  SkeletonClip::LoadFrame           src/system/gesture/SkeletonClip.cpp:72    (99.8%, regswap only)

-------------------------------------------------------------------------------
ENDIANNESS -- LITTLE-ENDIAN, even though the Xbox 360 is big-endian.
-------------------------------------------------------------------------------
Both the writer and the reader construct the stream with ``lilEndian = true``::

    StopRecordingNoClear: new FileStream(mFile.c_str(), FileStream::kWrite, true)
    LoadClipFromFile:     FileStream fs(str.c_str(), FileStream::kRead, true)

``FileStream(const char*, FileType, bool lilEndian)`` forwards that flag to
``BinStream(bool littleEndian)``.  ``BinStream::WriteEndian`` byte-swaps when
``mLittleEndian`` is set on a big-endian host, so on the Xbox 360 every multi-byte
scalar in the file is stored LITTLE-endian.  Single bytes (``bool``, ``unsigned char``)
go through ``Write()`` directly and are therefore unaffected.

-------------------------------------------------------------------------------
PRIMITIVE ENCODINGS (utl/BinStream.h, utl/BinStream.cpp)
-------------------------------------------------------------------------------
  int / uint / float      4 bytes, little-endian
  bool                    1 byte  (0 / 1); on read, ``!= 0``
  unsigned char           1 byte
  const char* / String    int32 length, then ``length`` raw bytes, NO NUL terminator
  Symbol                  uint32 length, then ``length`` raw bytes, NO NUL terminator
  DateTime                6 bytes: sec, min, hour, day, month, year   (each 1 byte,
                          ``year`` is years-since-1900-style: see DateTime::Year())
  Vector3                 3 floats = 12 bytes  (x, y, z)  -- the in-memory struct is
                          16 bytes because of a private SIMD pad, but the pad is NOT
                          serialized (``operator<<(BinStream&, const Vector3&)``)
  Hmx::Color              4 floats = 16 bytes  (red, green, blue, alpha)
  PaddedJointPos          serialized AS A Vector3 = 12 bytes.  PaddedJointPos is
                          ``{float x, y, z, _pad;}`` with an implicit
                          ``operator const Vector3&()``; the only viable stream
                          overload is the Vector3 one, so ``_pad`` is dropped.

-------------------------------------------------------------------------------
FILE LAYOUT
-------------------------------------------------------------------------------
The writer (``WriteClipHeader``) always emits ``version = 8``.  The reader
(``LoadClipFromFile``) understands versions 1..8 (version 0 is explicitly rejected:
"Version 0 clips no longer supported").  Version-8 layout, offsets in bytes:

  HEADER
    +0x00  int32   version            == 8   (writer: ``stream << 8``)
    +0x04  u8[6]   time_recorded      sec, min, hour, day, month, year
    +0x0A  int32   song_len           }  Symbol mSong   (song shortname, e.g. "aroundtheworld")
    +0x0E  u8[n]   song_bytes         }  empty string when not recorded from a song
           int32   difficulty         enum Difficulty: 0=easy 1=medium 2=expert 3=beginner
                                      4 (kNumDifficulties) == "not a song recording"
           int32   build_len          }  build string; taken from SystemConfig "version"
           u8[n]   build_bytes        }  array, else the literal "milo"
           int32   frame_count

  THEN ``frame_count`` FRAME RECORDS, each:
    +0x00  int32   frame_number       NUI frame counter          (v>6 only)
    +0x04  int32   elapsed_ms         ms since previous NUI frame (v>1 only)
    +0x08  float32 floor_normal_x     }
    +0x0C  float32 floor_normal_y     }  Vector3, 12 bytes       (v>1 only)
    +0x10  float32 floor_normal_z     }
    +0x14  float32 floor_clip_r       }
    +0x18  float32 floor_clip_g       }  Hmx::Color, 16 bytes    (v>1 only)
    +0x1C  float32 floor_clip_b       }  (actually the NUI floor clip plane, reinterpreted
    +0x20  float32 floor_clip_a       }   from Vector4 -- a=plane distance)
    +0x24  u8      is_tracked
    IF is_tracked (or version < 2):
    +0x25  20 x {                     kNumJoints == 20
             float32 x, y, z          PaddedJointPos as Vector3, 12 bytes
             int32   tracking_state   JointConfidence: 0=NotTracked 1=Inferred 2=Tracked
           }                          = 16 bytes per joint, 320 bytes total
   +0x165  int32   quality_flags      NUI clipped-edge bits
   +0x169  int32   tracking_id        NUI skeleton tracking id
    END IF
   +0x16D  float32 song_seconds       MoveDir::SongSeconds() at capture time (v>2 only;
                                      v==1 stores a length-prefixed String here instead,
                                      v==2 stores nothing)

  Tracked frame size (v8):   4+4+12+16+1+320+4+4+4 = 369 bytes
  Untracked frame size (v8): 4+4+12+16+1+4         =  41 bytes

Version deltas handled by ``LoadFrame`` / ``LoadClipFromFile``:
  v<=6 : no ``frame_number``; defaults to 0
  v<=1 : no ``elapsed_ms`` / floor normal / floor clip plane; defaults are
         elapsed_ms=0x21 (33 ms), normal=(0,0,1), plane=(0,0,1,0); joints are ALWAYS read
  1<v<8: an extra single ``bool`` byte immediately after the version field in the header
  v<=3 : no time_recorded / song / difficulty in the header
  v<=5 : no build string in the header (implied "milo")
  v<=4 : no frame_count in the header -- frames are read until EOF
  v==1 : each frame ends with a length-prefixed String instead of ``song_seconds``
  v==2 : each frame has no trailing field at all

-------------------------------------------------------------------------------
JOINT ORDER  (enum SkeletonJoint, src/system/gesture/BaseSkeleton.h)
-------------------------------------------------------------------------------
  0 hip_center      5 elbow_left      10 wrist_right    15 hip_right
  1 spine           6 wrist_left      11 hand_right     16 knee_right
  2 shoulder_center 7 hand_left       12 hip_left       17 ankle_right
  3 head            8 shoulder_right  13 knee_left      18 foot_left
  4 shoulder_left   9 elbow_right     14 ankle_left     19 foot_right

Positions are in the Kinect camera space the game uses (metres; +X right, +Y up,
+Z away from the camera in NUI convention).  ``RecordedFrame::MakeSkeletonFrame``
copies them straight into ``SkeletonData::mJointPositions`` and sets
``mHipCenter = mJointPositions[0]``, so no extra transform is applied on load.

===============================================================================
USAGE
===============================================================================
    python3 parse_skeleton_clip.py path/to/recording.clp            # summary
    python3 parse_skeleton_clip.py path/to/recording.clp --npz out.npz
    python3 parse_skeleton_clip.py --selftest                       # round-trip test

    from parse_skeleton_clip import parse_clip
    clip = parse_clip("recording.clp")
    clip.positions        # (num_frames, 20, 3) float32
    clip.tracking_state   # (num_frames, 20) int32
    clip.song_seconds     # (num_frames,) float32
"""

from __future__ import annotations

import argparse
import io
import struct
import sys
from dataclasses import dataclass, field
from typing import BinaryIO, List, Optional

try:
    import numpy as np
except ImportError:  # pragma: no cover - numpy is expected but keep the error actionable
    np = None  # type: ignore


# --- constants mirrored from the decomp -------------------------------------------------

NUM_JOINTS = 20  # kNumJoints, src/system/gesture/BaseSkeleton.h

JOINT_NAMES = [
    "hip_center",       # kJointHipCenter
    "spine",            # kJointSpine
    "shoulder_center",  # kJointShoulderCenter
    "head",             # kJointHead
    "shoulder_left",    # kJointShoulderLeft
    "elbow_left",       # kJointElbowLeft
    "wrist_left",       # kJointWristLeft
    "hand_left",        # kJointHandLeft
    "shoulder_right",   # kJointShoulderRight
    "elbow_right",      # kJointElbowRight
    "wrist_right",      # kJointWristRight
    "hand_right",       # kJointHandRight
    "hip_left",         # kJointHipLeft
    "knee_left",        # kJointKneeLeft
    "ankle_left",       # kJointAnkleLeft
    "hip_right",        # kJointHipRight
    "knee_right",       # kJointKneeRight
    "ankle_right",      # kJointAnkleRight
    "foot_left",        # kJointFootLeft
    "foot_right",       # kJointFootRight
]
assert len(JOINT_NAMES) == NUM_JOINTS

# enum JointConfidence, src/system/gesture/BaseSkeleton.h
TRACKING_STATE_NAMES = {0: "not_tracked", 1: "inferred", 2: "tracked"}

# enum Difficulty, src/system/hamobj/Difficulty.h
DIFFICULTY_NAMES = {0: "easy", 1: "medium", 2: "expert", 3: "beginner", 4: "n/a"}

WRITER_VERSION = 8  # SkeletonClip::WriteClipHeader always emits 8

# The writer/reader both open the FileStream with lilEndian=true.
_LE = "<"


class ClipFormatError(Exception):
    """Raised when a .clp file does not conform to the recovered format."""


# --- low-level BinStream primitives -----------------------------------------------------


class _Reader:
    """Mirrors the subset of BinStream used by SkeletonClip, little-endian."""

    def __init__(self, buf: bytes):
        self.buf = buf
        self.pos = 0

    def remaining(self) -> int:
        return len(self.buf) - self.pos

    def eof(self) -> bool:
        return self.pos >= len(self.buf)

    def _take(self, n: int) -> bytes:
        if self.pos + n > len(self.buf):
            raise ClipFormatError(
                "unexpected end of file: wanted %d bytes at offset 0x%X, only %d left"
                % (n, self.pos, self.remaining())
            )
        out = self.buf[self.pos : self.pos + n]
        self.pos += n
        return out

    def i32(self) -> int:
        return struct.unpack(_LE + "i", self._take(4))[0]

    def u32(self) -> int:
        return struct.unpack(_LE + "I", self._take(4))[0]

    def f32(self) -> float:
        return struct.unpack(_LE + "f", self._take(4))[0]

    def u8(self) -> int:
        return self._take(1)[0]

    def boolean(self) -> bool:
        return self.u8() != 0

    def string(self) -> str:
        # BinStream::operator>>(String&) / ReadString(): int32 length + raw bytes.
        n = self.i32()
        if n < 0 or n > self.remaining():
            raise ClipFormatError(
                "bad string length %d at offset 0x%X" % (n, self.pos - 4)
            )
        return self._take(n).decode("latin-1")

    def vec3(self):
        return struct.unpack(_LE + "3f", self._take(12))

    def color(self):
        return struct.unpack(_LE + "4f", self._take(16))

    def datetime(self):
        b = self._take(6)
        # DateTime { u8 mSec, mMin, mHour, mDay, mMonth, mYear; }
        return DateTimeRec(sec=b[0], minute=b[1], hour=b[2], day=b[3], month=b[4], year=b[5])


class _Writer:
    """Mirrors BinStream's writer side; only used by the selftest."""

    def __init__(self) -> None:
        self.buf = io.BytesIO()

    def i32(self, v: int) -> None:
        self.buf.write(struct.pack(_LE + "i", v))

    def u32(self, v: int) -> None:
        self.buf.write(struct.pack(_LE + "I", v))

    def f32(self, v: float) -> None:
        self.buf.write(struct.pack(_LE + "f", v))

    def u8(self, v: int) -> None:
        self.buf.write(bytes([v & 0xFF]))

    def boolean(self, v: bool) -> None:
        self.u8(1 if v else 0)

    def string(self, s: str) -> None:
        raw = s.encode("latin-1")
        self.i32(len(raw))
        self.buf.write(raw)

    def vec3(self, v) -> None:
        self.buf.write(struct.pack(_LE + "3f", *v))

    def color(self, v) -> None:
        self.buf.write(struct.pack(_LE + "4f", *v))

    def datetime(self, dt: "DateTimeRec") -> None:
        self.buf.write(bytes([dt.sec, dt.minute, dt.hour, dt.day, dt.month, dt.year]))

    def getvalue(self) -> bytes:
        return self.buf.getvalue()


# --- data model -------------------------------------------------------------------------


@dataclass
class DateTimeRec:
    sec: int
    minute: int
    hour: int
    day: int
    month: int
    year: int

    def iso(self) -> str:
        # DateTime::Year() returns mYear + 1900 in the Milo engine.
        return "%04d-%02d-%02d %02d:%02d:%02d" % (
            self.year + 1900,
            self.month + 1,
            self.day,
            self.hour,
            self.minute,
            self.sec,
        )


@dataclass
class SkeletonClipData:
    version: int
    time_recorded: Optional[DateTimeRec]
    song: str
    difficulty: int
    build: str
    frame_count: int

    frame_number: "np.ndarray"     # (N,)     int32
    elapsed_ms: "np.ndarray"       # (N,)     int32
    floor_normal: "np.ndarray"     # (N, 3)   float32
    floor_clip_plane: "np.ndarray" # (N, 4)   float32
    is_tracked: "np.ndarray"       # (N,)     bool
    positions: "np.ndarray"        # (N, 20, 3) float32 (NaN-free; zeros for untracked)
    tracking_state: "np.ndarray"   # (N, 20)  int32
    quality_flags: "np.ndarray"    # (N,)     int32
    tracking_id: "np.ndarray"      # (N,)     int32
    song_seconds: "np.ndarray"     # (N,)     float32

    path: str = ""
    trailing_bytes: int = 0

    @property
    def difficulty_name(self) -> str:
        return DIFFICULTY_NAMES.get(self.difficulty, "unknown(%d)" % self.difficulty)

    @property
    def duration_seconds(self) -> float:
        if self.frame_count == 0:
            return 0.0
        return float(self.song_seconds[-1] - self.song_seconds[0])


# --- parsing ----------------------------------------------------------------------------


def _read_frame(r: _Reader, version: int) -> dict:
    """Mirror of SkeletonClip::LoadFrame(BinStream&, RecordedFrame&, int version)."""
    out = {}

    if version > 6:
        out["frame_number"] = r.i32()
    else:
        out["frame_number"] = 0

    if version > 1:
        out["elapsed_ms"] = r.i32()
        out["floor_normal"] = r.vec3()
        out["floor_clip_plane"] = r.color()
    else:
        out["elapsed_ms"] = 0x21
        out["floor_normal"] = (0.0, 0.0, 1.0)
        out["floor_clip_plane"] = (0.0, 0.0, 1.0, 0.0)

    is_tracked = r.boolean()
    out["is_tracked"] = is_tracked

    positions = [(0.0, 0.0, 0.0)] * NUM_JOINTS
    states = [0] * NUM_JOINTS
    quality_flags = 0
    tracking_id = 0

    if is_tracked or version < 2:
        positions = []
        states = []
        for _ in range(NUM_JOINTS):
            positions.append(r.vec3())
            states.append(r.i32())
        quality_flags = r.i32()
        tracking_id = r.i32()

    out["positions"] = positions
    out["tracking_state"] = states
    out["quality_flags"] = quality_flags
    out["tracking_id"] = tracking_id

    if version == 1:
        r.string()  # discarded by the game too
        out["song_seconds"] = 0.0
    elif version > 2:
        out["song_seconds"] = r.f32()
    else:  # version == 2: no trailing field
        out["song_seconds"] = 0.0

    return out


def parse_clip_bytes(data: bytes, path: str = "<bytes>") -> SkeletonClipData:
    """Parse a .clp payload. Mirrors SkeletonClip::LoadClipFromFile."""
    if np is None:
        raise RuntimeError("numpy is required: pip install numpy")
    if not data:
        raise ClipFormatError("file is empty (the game rejects these too)")

    r = _Reader(data)
    version = r.i32()
    if version == 0:
        raise ClipFormatError(
            "version 0 clips are not supported (the game refuses them as well)"
        )
    if version < 0 or version > WRITER_VERSION:
        raise ClipFormatError(
            "unknown clip version %d (writer emits %d, reader handles 1..%d) -- "
            "wrong file type or wrong endianness?" % (version, WRITER_VERSION, WRITER_VERSION)
        )

    if 1 < version < 8:
        r.boolean()  # unused flag, read and discarded by LoadClipFromFile

    time_recorded = None
    song = ""
    difficulty = 4  # kNumDifficulties == "not a song recording"
    if version > 3:
        time_recorded = r.datetime()
        song = r.string()  # Symbol -> same wire format as String
        difficulty = r.i32()

    if version > 5:
        build = r.string()
    else:
        build = "milo"

    frames: List[dict] = []
    if version > 4:
        frame_count = r.i32()
        if frame_count < 0:
            raise ClipFormatError("negative frame count %d" % frame_count)
        for i in range(frame_count):
            try:
                frames.append(_read_frame(r, version))
            except ClipFormatError:
                # The game does the same: "Bad clip data, truncating from %d frames to %d"
                sys.stderr.write(
                    "warning: bad clip data, truncating from %d frames to %d\n"
                    % (frame_count, i)
                )
                break
    else:
        while not r.eof():
            frames.append(_read_frame(r, version))

    n = len(frames)
    clip = SkeletonClipData(
        version=version,
        time_recorded=time_recorded,
        song=song,
        difficulty=difficulty,
        build=build,
        frame_count=n,
        frame_number=np.array([f["frame_number"] for f in frames], dtype=np.int32),
        elapsed_ms=np.array([f["elapsed_ms"] for f in frames], dtype=np.int32),
        floor_normal=np.array(
            [f["floor_normal"] for f in frames], dtype=np.float32
        ).reshape(n, 3),
        floor_clip_plane=np.array(
            [f["floor_clip_plane"] for f in frames], dtype=np.float32
        ).reshape(n, 4),
        is_tracked=np.array([f["is_tracked"] for f in frames], dtype=bool),
        positions=np.array([f["positions"] for f in frames], dtype=np.float32).reshape(
            n, NUM_JOINTS, 3
        ),
        tracking_state=np.array(
            [f["tracking_state"] for f in frames], dtype=np.int32
        ).reshape(n, NUM_JOINTS),
        quality_flags=np.array([f["quality_flags"] for f in frames], dtype=np.int32),
        tracking_id=np.array([f["tracking_id"] for f in frames], dtype=np.int32),
        song_seconds=np.array([f["song_seconds"] for f in frames], dtype=np.float32),
        path=path,
        trailing_bytes=r.remaining(),
    )
    return clip


def parse_clip(path: str) -> SkeletonClipData:
    with open(path, "rb") as fh:
        return parse_clip_bytes(fh.read(), path=path)


# --- reporting --------------------------------------------------------------------------


def summarize(clip: SkeletonClipData, out: BinaryIO = None) -> str:
    lines = []
    a = lines.append
    a("file:            %s" % clip.path)
    a("clip version:    %d" % clip.version)
    a("recorded:        %s" % (clip.time_recorded.iso() if clip.time_recorded else "n/a"))
    a("song:            %r" % clip.song)
    a("difficulty:      %s (%d)" % (clip.difficulty_name, clip.difficulty))
    a("build:           %r" % clip.build)
    a("frames:          %d" % clip.frame_count)
    if clip.trailing_bytes:
        a("trailing bytes:  %d  (unexpected -- format mismatch?)" % clip.trailing_bytes)

    if clip.frame_count == 0:
        return "\n".join(lines)

    a(
        "song seconds:    %.3f .. %.3f  (duration %.3f s)"
        % (clip.song_seconds[0], clip.song_seconds[-1], clip.duration_seconds)
    )
    if clip.duration_seconds > 0:
        a("effective fps:   %.2f" % (clip.frame_count / clip.duration_seconds))
    a(
        "elapsed_ms:      min %d  median %d  max %d"
        % (
            int(clip.elapsed_ms.min()),
            int(np.median(clip.elapsed_ms)),
            int(clip.elapsed_ms.max()),
        )
    )
    a(
        "tracked frames:  %d / %d (%.1f%%)"
        % (
            int(clip.is_tracked.sum()),
            clip.frame_count,
            100.0 * clip.is_tracked.mean(),
        )
    )
    ids = np.unique(clip.tracking_id[clip.is_tracked]) if clip.is_tracked.any() else []
    a("tracking ids:    %s" % (", ".join(str(int(i)) for i in ids) or "none"))
    qf = np.unique(clip.quality_flags[clip.is_tracked]) if clip.is_tracked.any() else []
    a("quality flags:   %s" % (", ".join("0x%X" % int(q) for q in qf) or "none"))

    if clip.is_tracked.any():
        fn = clip.floor_normal[clip.is_tracked]
        a(
            "floor normal:    mean (%.4f, %.4f, %.4f)"
            % (fn[:, 0].mean(), fn[:, 1].mean(), fn[:, 2].mean())
        )
        fp = clip.floor_clip_plane[clip.is_tracked]
        a("floor plane w:   mean %.4f" % fp[:, 3].mean())

    a("")
    a("tracking-state histogram (all frames x joints):")
    total = clip.tracking_state.size
    for state, name in sorted(TRACKING_STATE_NAMES.items()):
        cnt = int((clip.tracking_state == state).sum())
        a("  %-12s %10d  (%5.1f%%)" % (name, cnt, 100.0 * cnt / total if total else 0.0))
    other = int(
        (~np.isin(clip.tracking_state, list(TRACKING_STATE_NAMES))).sum()
    )
    if other:
        a("  %-12s %10d" % ("other", other))

    a("")
    a("per-joint position ranges (metres, tracked frames only):")
    a(
        "  %-16s %-22s %-22s %-22s %s"
        % ("joint", "x min..max", "y min..max", "z min..max", "tracked%")
    )
    mask = clip.is_tracked
    for j, name in enumerate(JOINT_NAMES):
        if not mask.any():
            a("  %-16s (no tracked frames)" % name)
            continue
        p = clip.positions[mask, j, :]
        st = clip.tracking_state[mask, j]
        pct = 100.0 * float((st == 2).mean())
        a(
            "  %-16s %8.3f..%8.3f   %8.3f..%8.3f   %8.3f..%8.3f   %5.1f%%"
            % (
                name,
                p[:, 0].min(), p[:, 0].max(),
                p[:, 1].min(), p[:, 1].max(),
                p[:, 2].min(), p[:, 2].max(),
                pct,
            )
        )
    return "\n".join(lines)


def save_npz(clip: SkeletonClipData, path: str) -> None:
    np.savez_compressed(
        path,
        version=clip.version,
        song=clip.song,
        difficulty=clip.difficulty,
        build=clip.build,
        joint_names=np.array(JOINT_NAMES),
        frame_number=clip.frame_number,
        elapsed_ms=clip.elapsed_ms,
        floor_normal=clip.floor_normal,
        floor_clip_plane=clip.floor_clip_plane,
        is_tracked=clip.is_tracked,
        positions=clip.positions,
        tracking_state=clip.tracking_state,
        quality_flags=clip.quality_flags,
        tracking_id=clip.tracking_id,
        song_seconds=clip.song_seconds,
    )


# --- writer (selftest only; mirrors WriteClipHeader / WriteClipFrame) -------------------


def build_clip_bytes(
    frames: List[dict],
    song: str = "aroundtheworld",
    difficulty: int = 2,
    build: str = "milo",
    time_recorded: Optional[DateTimeRec] = None,
) -> bytes:
    """Synthesize a version-8 .clp exactly as SkeletonClip::WriteClip would.

    ``frames`` entries use the same keys ``_read_frame`` produces.
    """
    w = _Writer()
    # --- WriteClipHeader ---
    w.i32(WRITER_VERSION)                        # stream << 8
    w.datetime(time_recorded or DateTimeRec(11, 22, 13, 4, 6, 112))
    w.string(song)                               # stream << mSong    (Symbol)
    w.i32(difficulty)                            # stream << mDifficulty
    w.string(build)                              # stream << str      (const char*)
    w.i32(len(frames))                           # stream << (int)size()

    # --- WriteClipFrame, once per frame ---
    for f in frames:
        w.i32(f["frame_number"])
        w.i32(f["elapsed_ms"])
        w.vec3(f["floor_normal"])
        w.color(f["floor_clip_plane"])
        w.boolean(f["is_tracked"])
        if f["is_tracked"]:
            for j in range(NUM_JOINTS):
                w.vec3(f["positions"][j])
                w.i32(f["tracking_state"][j])
            w.i32(f["quality_flags"])
            w.i32(f["tracking_id"])
        w.f32(f["song_seconds"])
    return w.getvalue()


# --- selftest ---------------------------------------------------------------------------


def _synth_frames(n: int = 24) -> List[dict]:
    frames = []
    for i in range(n):
        tracked = i % 7 != 3  # sprinkle in untracked frames to exercise both branches
        positions = []
        states = []
        for j in range(NUM_JOINTS):
            positions.append(
                (
                    0.01 * j - 0.1 + 0.001 * i,
                    1.5 - 0.05 * j + 0.002 * i,
                    2.5 + 0.003 * i,
                )
            )
            states.append((i + j) % 3)
        frames.append(
            dict(
                frame_number=1000 + i,
                elapsed_ms=33,
                floor_normal=(0.0, 0.9975, -0.0707),
                floor_clip_plane=(0.0, 0.9975, -0.0707, -1.05),
                is_tracked=tracked,
                positions=positions if tracked else [(0.0, 0.0, 0.0)] * NUM_JOINTS,
                tracking_state=states if tracked else [0] * NUM_JOINTS,
                quality_flags=0x3 if tracked else 0,
                tracking_id=7 if tracked else 0,
                song_seconds=0.0333 * i,
            )
        )
    return frames


def selftest() -> int:
    if np is None:
        print("FAIL: numpy is required for the selftest")
        return 1

    failures = []

    def check(cond: bool, msg: str) -> None:
        if not cond:
            failures.append(msg)

    frames = _synth_frames()
    blob = build_clip_bytes(frames, song="aroundtheworld", difficulty=2, build="dc3_r1234")

    # 1) exact byte-size check against the documented layout
    n_tracked = sum(1 for f in frames if f["is_tracked"])
    n_untracked = len(frames) - n_tracked
    header = 4 + 6 + (4 + len("aroundtheworld")) + 4 + (4 + len("dc3_r1234")) + 4
    expected = header + n_tracked * 369 + n_untracked * 41
    check(
        len(blob) == expected,
        "size mismatch: got %d bytes, layout predicts %d" % (len(blob), expected),
    )

    # 2) header bytes are little-endian
    check(blob[0:4] == b"\x08\x00\x00\x00", "version field is not LE int32 8")

    # 3) round-trip
    clip = parse_clip_bytes(blob, path="<selftest>")
    check(clip.version == 8, "version round-trip")
    check(clip.song == "aroundtheworld", "song round-trip: %r" % clip.song)
    check(clip.difficulty == 2, "difficulty round-trip")
    check(clip.build == "dc3_r1234", "build round-trip: %r" % clip.build)
    check(clip.frame_count == len(frames), "frame count round-trip")
    check(clip.trailing_bytes == 0, "trailing bytes: %d" % clip.trailing_bytes)
    check(clip.time_recorded is not None, "time_recorded missing")

    for i, f in enumerate(frames):
        check(int(clip.frame_number[i]) == f["frame_number"], "frame_number[%d]" % i)
        check(int(clip.elapsed_ms[i]) == f["elapsed_ms"], "elapsed_ms[%d]" % i)
        check(bool(clip.is_tracked[i]) == f["is_tracked"], "is_tracked[%d]" % i)
        check(
            abs(float(clip.song_seconds[i]) - f["song_seconds"]) < 1e-6,
            "song_seconds[%d]" % i,
        )
        check(
            np.allclose(clip.floor_normal[i], f["floor_normal"], atol=1e-6),
            "floor_normal[%d]" % i,
        )
        check(
            np.allclose(clip.floor_clip_plane[i], f["floor_clip_plane"], atol=1e-6),
            "floor_clip_plane[%d]" % i,
        )
        if f["is_tracked"]:
            check(
                np.allclose(clip.positions[i], np.array(f["positions"]), atol=1e-6),
                "positions[%d]" % i,
            )
            check(
                (clip.tracking_state[i] == np.array(f["tracking_state"])).all(),
                "tracking_state[%d]" % i,
            )
            check(int(clip.quality_flags[i]) == f["quality_flags"], "quality_flags[%d]" % i)
            check(int(clip.tracking_id[i]) == f["tracking_id"], "tracking_id[%d]" % i)

    # 4) shapes
    check(clip.positions.shape == (len(frames), NUM_JOINTS, 3), "positions shape")
    check(clip.tracking_state.shape == (len(frames), NUM_JOINTS), "tracking_state shape")

    # 5) empty-file and version-0 rejection behave like the game
    try:
        parse_clip_bytes(b"")
        failures.append("empty file should raise")
    except ClipFormatError:
        pass
    try:
        parse_clip_bytes(struct.pack("<i", 0))
        failures.append("version 0 should raise")
    except ClipFormatError:
        pass

    # 6) a big-endian-looking header must be rejected rather than silently misparsed
    try:
        parse_clip_bytes(struct.pack(">i", 8) + blob[4:])
        failures.append("big-endian header should be rejected")
    except ClipFormatError:
        pass

    # 7) truncation is handled gracefully (matches the game's truncate-and-warn)
    truncated = parse_clip_bytes(blob[: header + 369 * 3 + 100], path="<truncated>")
    check(truncated.frame_count < len(frames), "truncated clip should lose frames")

    # 8) the summary formatter must not blow up
    summarize(clip)

    if failures:
        print("SELFTEST FAILED (%d):" % len(failures))
        for f in failures:
            print("  - %s" % f)
        return 1

    print("SELFTEST PASSED")
    print("  synthesized %d frames (%d tracked, %d untracked) -> %d bytes"
          % (len(frames), n_tracked, n_untracked, len(blob)))
    print("  layout check: header %d B, tracked frame 369 B, untracked frame 41 B"
          % header)
    return 0


def main(argv: List[str]) -> int:
    ap = argparse.ArgumentParser(
        description="Parse Dance Central 3 .clp skeleton-clip recordings.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="See the module docstring for the full byte-level format spec.",
    )
    ap.add_argument("clip", nargs="?", help="path to a .clp file")
    ap.add_argument("--npz", help="also write the decoded arrays to this .npz")
    ap.add_argument("--selftest", action="store_true", help="run the round-trip selftest")
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()
    if not args.clip:
        ap.print_help()
        return 2

    clip = parse_clip(args.clip)
    print(summarize(clip))
    if args.npz:
        save_npz(clip, args.npz)
        print("\nwrote %s" % args.npz)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
