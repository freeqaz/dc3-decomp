#!/usr/bin/env python3
"""camera_resolve.py -- which camera device --camera means, decided the way the
pose server decides it, so the kit's checks open the device the game will.

  camera_resolve.py [SPEC]      print the /dev/videoN node SPEC resolves to
                                (default auto); exit 1 and say why if none
  camera_resolve.py --json SPEC the whole answer as JSON

SPEC is `auto` (the first video-capture device), an index N (/dev/videoN), a
path, or a /dev/v4l/by-id name -- pose_server.py's --camera.  A camera can
re-connect under another number on its own (measured on the GPD Pocket:
8 times in one day, coming back as /dev/video1), so a fixed index is the
fragile choice and auto is the default.

The rule itself is pose_server.resolve_camera (native/scripts/pose_server.py);
this module only imports it and turns its answer into plain words. Until that
function exists in this tree (np-pose-camera) a minimal stand-in is used:
an index is /dev/videoN, auto is the lowest-numbered node.

Standard library only (preflight.py runs it before any pose package exists).
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, "native", "scripts"))

try:  # pose_server.py imports only the standard library at top level
    import pose_server as _ps
except Exception:  # noqa: BLE001 -- an unimportable server is reported by preflight
    _ps = None

BY_ID = "/dev/v4l/by-id"


class Devices:
    """The box's V4L2 nodes (pose_server.V4L2Devices when present). Tests
    substitute an object with the same methods."""

    by_id_dir = BY_ID

    def __init__(self):
        self._v4l2 = _ps.V4L2Devices() if _ps is not None and hasattr(_ps, "V4L2Devices") else None

    def available(self):
        return sys.platform.startswith("linux")

    def nodes(self):
        if self._v4l2 is not None:
            return self._v4l2.nodes()
        try:
            names = os.listdir("/dev")
        except OSError:
            return []
        nums = sorted(int(n[5:]) for n in names if n.startswith("video") and n[5:].isdigit())
        return [f"/dev/video{n}" for n in nums]

    def query(self, path):
        if self._v4l2 is not None:
            return self._v4l2.query(path)
        return os.path.exists(path), self.name(path)

    def name(self, node):
        try:
            with open(f"/sys/class/video4linux/{os.path.basename(node)}/name") as f:
                return f.read().strip()
        except OSError:
            return "?"

    def realpath(self, path):
        return os.path.realpath(path)

    def exists(self, path):
        return os.path.exists(path)

    def by_id(self):
        """{by-id name: node it points to}."""
        out = {}
        try:
            for n in sorted(os.listdir(self.by_id_dir)):
                out[n] = os.path.realpath(os.path.join(self.by_id_dir, n))
        except OSError:
            pass
        return out


def _fallback_resolve(spec, devices, log):
    """Stand-in for pose_server.resolve_camera in a tree that predates it."""
    if spec in ("", "auto"):
        nodes = devices.nodes()
        if not nodes:
            raise LookupError("no video-capture camera found (no /dev/video* nodes)")
        return nodes[0], f"{nodes[0]} [auto]"
    if spec.isdigit():
        node = f"/dev/video{int(spec)}"
        if not devices.exists(node):
            raise LookupError(f"camera {spec} ({node}) is not present")
        return node, f"{node} [camera {spec}]"
    path = spec if spec.startswith("/") else os.path.join(devices.by_id_dir, spec)
    if not devices.exists(path):
        raise LookupError(f"camera {spec} is not present")
    return devices.realpath(path), f"{path} [{spec}]"


def resolve(spec="auto", devices=None):
    """The answer for SPEC, as a dict:
      spec, node (the /dev/videoN the game will open, or None), desc,
      fallback (str: SPEC named a node that is missing or not a camera, and
        the server scanned for another -- what to tell the player), error
      (str: nothing to open), cameras ([(node, name, is_capture)]), by_id
      ({name: node})."""
    devices = devices or Devices()
    spec = "auto" if spec is None else str(spec).strip() or "auto"
    out = {"spec": spec, "node": None, "desc": "", "fallback": None, "error": None,
           "cameras": [], "by_id": {}}
    if devices.available():
        for n in devices.nodes():
            ok, desc = devices.query(n)
            out["cameras"].append((n, desc, bool(ok)))
        out["by_id"] = devices.by_id() if hasattr(devices, "by_id") else {}
    notes = []
    try:
        if _ps is not None and hasattr(_ps, "resolve_camera"):
            src, desc = _ps.resolve_camera(spec, devices, log=notes.append)
        else:
            src, desc = _fallback_resolve(spec, devices, notes.append)
    except Exception as e:  # noqa: BLE001 -- ServerExit / LookupError: nothing to open
        out["error"] = str(e)
        return out
    out["node"], out["desc"] = src, desc
    if notes and spec.isdigit():
        out["fallback"] = missing_index_words(spec, out)
    elif notes:
        out["fallback"] = "; ".join(notes)
    return out


def capture_names(r):
    return [(n, name) for n, name, ok in r["cameras"] if ok]


def missing_index_words(spec, r):
    """Plain words for --camera N when /dev/videoN is gone or not a camera."""
    node = f"/dev/video{int(spec)}"
    caps = capture_names(r)
    now = ", ".join(f"{os.path.basename(n)} '{name}'" for n, name in caps) or "no camera at all"
    gone = "does not exist" if not any(n == node for n, _, _ in r["cameras"]) \
        else "is not a camera"
    return (f"camera {spec} ({node}) {gone} - the camera may have re-connected under "
            f"another number (now: {now}). Use --camera auto (the default) or the "
            f"{BY_ID} name" + (f" ({', '.join(by_id_names(r, caps[0][0]))})"
                               if caps and by_id_names(r, caps[0][0]) else ""))


def by_id_names(r, node):
    return [n for n, target in r["by_id"].items() if target == node]


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    as_json = "--json" in argv
    if as_json:
        argv.remove("--json")
    r = resolve(argv[0] if argv else "auto")
    if as_json:
        print(json.dumps(r))
    elif r["node"] is not None:
        print(r["node"])
    else:
        print(r["error"], file=sys.stderr)
    return 0 if r["node"] is not None else 1


if __name__ == "__main__":
    sys.exit(main())
