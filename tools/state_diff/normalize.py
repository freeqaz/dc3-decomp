"""Canonicalize raw probe output into a diffable snapshot.

Raw DTA output is not comparable across targets or even across runs: floats
carry noise in the low bits, colours arrive as packed ints, paths use different
separators, collections come back in hash-table order, and a handful of fields
are pure run-to-run noise (frame counters, timers).

This module turns raw records into a stable JSON structure. Everything that
makes two logically-identical states compare unequal must be handled HERE, not
in the differ — the differ's job is to rank real differences, not to guess
which ones are artefacts.
"""

from __future__ import annotations

import math
import re
from dataclasses import dataclass, field
from typing import Any

from .probe import ABSENT, Probe
from .transport import NULL_OBJ

SCHEMA_VERSION = 2

# --------------------------------------------------------------------------
# Tolerances. Different quantities need different rounding: a draw order of
# 0.5 vs 0.5001 is noise, but a texture size of 512 vs 513 is not, and euler
# angles derived from a matrix are numerically fragile near gimbal lock.
# --------------------------------------------------------------------------

DEFAULT_TOLERANCES = {
    # key: (absolute_epsilon, decimal_places)
    "default": (1e-4, 6),
    "translation": (1e-4, 5),   # world/local x,y,z
    "rotation": (1e-2, 3),      # pitch/roll/yaw, degrees, derived from matrix
    "scale": (1e-4, 5),
    "color": (0, 0),            # unpacked to 0-255 ints; exact
    "count": (0, 0),            # vert/face/width/height; exact
    "order": (1e-4, 5),         # draw_order
    "angle": (1e-3, 4),         # y_fov
    "plane": (1e-3, 4),         # near/far plane, fog start/end, range
}

#: field-name -> tolerance class. Matched by exact name then by suffix.
FIELD_CLASS = {
    "draw_order": "order",
    "y_fov": "angle",
    "near_plane": "plane",
    "far_plane": "plane",
    "range": "plane",
    "falloff_start": "plane",
    "fog_start": "plane",
    "fog_end": "plane",
    "fade_start": "plane",
    "fade_end": "plane",
    "sphere_radius": "translation",
    "num_verts": "count",
    "num_faces": "count",
    "width": "count",
    "height": "count",
    "bpp": "count",
    "size_kb": "count",
}

_SUFFIX_CLASS = [
    ("_pitch", "rotation"), ("_roll", "rotation"), ("_yaw", "rotation"),
    ("_sx", "scale"), ("_sy", "scale"), ("_sz", "scale"),
    ("_x", "translation"), ("_y", "translation"), ("_z", "translation"),
]

#: Fields that are inherently run-varying and are dropped before diffing.
#: This is the *static* elision list; the measured noise floor (noise.py)
#: adds to it empirically.
VOLATILE_FIELDS = {
    "frame", "uptime_s", "time", "timer", "seconds", "realsecs",
    "beat", "song_anim_frame", "elapsed",
}

#: Snapshot-header keys that are recorded but never diffed.
VOLATILE_HEADER = {"frame", "uptime_s", "captured_at", "duration_s", "stats"}

_HEX_ADDR = re.compile(r"0x[0-9a-fA-F]{6,}")
_DEC_ADDR = re.compile(r"\b\d{9,}\b")


def tolerance_class(field_name: str) -> str:
    if field_name in FIELD_CLASS:
        return FIELD_CLASS[field_name]
    for suffix, cls in _SUFFIX_CLASS:
        if field_name.endswith(suffix):
            return cls
    return "default"


# --------------------------------------------------------------------------
# Value canonicalization
# --------------------------------------------------------------------------


def canon_float(raw: str, field_name: str, tolerances: dict | None = None) -> Any:
    tol = (tolerances or DEFAULT_TOLERANCES)
    _, places = tol.get(tolerance_class(field_name), tol["default"])
    try:
        v = float(raw)
    except (TypeError, ValueError):
        return raw
    if math.isnan(v):
        return "NaN"
    if math.isinf(v):
        return "Inf" if v > 0 else "-Inf"
    v = round(v, places)
    # Collapse -0.0 to 0.0 so sign noise on a zero never shows as a diff.
    if v == 0:
        v = 0.0
    return v


def unpack_color(raw: str) -> Any:
    """Hmx::Color arrives PACKED as an int (PropSync.cpp:32 -> color.Pack()).

    Unpack into 0-255 channels so a diff reads "green 255 -> 0" instead of
    "int 4278255360 -> 4278190080". Channel order follows Hmx::Color::Pack
    (RGBA, alpha in the high byte).
    """
    try:
        p = int(raw)
    except (TypeError, ValueError):
        return raw
    if p < 0:
        p += 1 << 32
    return {
        "r": p & 0xFF,
        "g": (p >> 8) & 0xFF,
        "b": (p >> 16) & 0xFF,
        "a": (p >> 24) & 0xFF,
    }


def canon_path(raw: str) -> str:
    """Normalize a FilePath so Xbox and Linux captures compare equal."""
    s = raw.replace("\\", "/").strip()
    s = re.sub(r"^[a-zA-Z]:/", "/", s)          # drop drive letters (D:/, game:/)
    s = re.sub(r"^(game|dvd|cache|d):/*", "/", s, flags=re.I)
    s = re.sub(r"/+", "/", s)
    return s.lower()


def scrub_addresses(s: str) -> str:
    """Replace anything that looks like a pointer with a placeholder.

    Object names are stable identifiers, but a few engine strings embed
    addresses or auto-generated ids. Those would otherwise differ on every run
    and on every platform.
    """
    s = _HEX_ADDR.sub("<addr>", s)
    return _DEC_ADDR.sub("<num>", s)


#: Enum decode tables, so a report says "blend=kBlendSrc" not "blend=2".
#: Values from config/macros.dta (BLEND_ENUM / ZMODE_ENUM) and the
#: corresponding C++ enums in src/system/rndobj/Mat.h and Lit.h.
ENUMS = {
    "blend": {
        0: "kBlendDest", 1: "kBlendSrc", 2: "kBlendAdd", 3: "kBlendSrcAlpha",
        4: "kBlendSrcAlphaDestAlpha", 5: "kBlendSubtract", 6: "kBlendMultiply",
        7: "kPreMultAlpha",
    },
    "z_mode": {
        0: "kZModeDisable", 1: "kZModeNormal", 2: "kZModeTransparent",
        3: "kZModeForce", 4: "kZModeDecal",
    },
    "cull": {0: "kCullOff", 1: "kCullFront", 2: "kCullBack"},
    "tex_gen": {
        0: "kTexGenNone", 1: "kTexGenXfm", 2: "kTexGenSphere",
        3: "kTexGenProjected", 4: "kTexGenXfmOrigin", 5: "kTexGenEnviron",
    },
    "tex_wrap": {
        0: "kTexWrapClamp", 1: "kTexWrapRepeat", 2: "kTexWrapMirror",
        3: "kTexWrapBorder",
    },
    "light_type": {
        0: "kLightPoint", 1: "kLightDirectional", 2: "kLightFakeSpot",
        3: "kLightFloorSpot", 4: "kLightProjected", 5: "kLightFakeSpotProj",
    },
    "trans_constraint": {
        0: "kConstraintNone", 1: "kConstraintLocalRotate",
        2: "kConstraintParentWorld", 3: "kConstraintLookAtTarget",
        4: "kConstraintShadowTarget", 5: "kConstraintBillboardZ",
        6: "kConstraintBillboardXZ", 7: "kConstraintBillboardXYZ",
        8: "kConstraintFastBillboardXYZ",
    },
}


def canon_value(raw: str, field_name: str, kind: str,
                tolerances: dict | None = None) -> Any:
    """Turn one raw DTA string into its canonical JSON value."""
    if raw == ABSENT:
        return None                     # property does not exist on this class
    if raw == NULL_OBJ:
        return "<null>"

    if kind == "color":
        return unpack_color(raw)
    if kind == "float":
        return canon_float(raw, field_name, tolerances)
    if kind == "bool":
        try:
            return bool(int(raw))
        except (TypeError, ValueError):
            return raw
    if kind == "int":
        try:
            v = int(raw)
        except (TypeError, ValueError):
            return raw
        table = ENUMS.get(field_name)
        if table and v in table:
            return table[v]
        return v
    if kind == "obj":
        return scrub_addresses(raw)
    # sym / string
    if "path" in field_name or "file" in field_name:
        return canon_path(raw)
    return scrub_addresses(raw)


# --------------------------------------------------------------------------
# Snapshot assembly
# --------------------------------------------------------------------------


@dataclass
class Snapshot:
    probe: str
    target: str
    schema: int = SCHEMA_VERSION
    meta: dict = field(default_factory=dict)       # stable identity, diffed
    volatile: dict = field(default_factory=dict)   # recorded, never diffed
    objects: dict[str, dict] = field(default_factory=dict)
    scalars: dict[str, Any] = field(default_factory=dict)
    errors: list[str] = field(default_factory=list)

    def to_json(self) -> dict:
        return {
            "schema": self.schema,
            "probe": self.probe,
            "target": self.target,
            "meta": self.meta,
            "volatile": self.volatile,
            "objects": self.objects,
            "scalars": self.scalars,
            "errors": self.errors,
        }

    @staticmethod
    def from_json(d: dict) -> "Snapshot":
        return Snapshot(
            probe=d.get("probe", "?"),
            target=d.get("target", "?"),
            schema=d.get("schema", 0),
            meta=d.get("meta", {}),
            volatile=d.get("volatile", {}),
            objects=d.get("objects", {}),
            scalars=d.get("scalars", {}),
            errors=d.get("errors", []),
        )


def normalize(
    probe: Probe,
    raw: dict[str, dict],
    target_name: str,
    meta: dict | None = None,
    volatile: dict | None = None,
    tolerances: dict | None = None,
) -> Snapshot:
    """Build a canonical :class:`Snapshot` from raw probe records."""
    snap = Snapshot(probe=probe.id, target=target_name,
                    meta=dict(meta or {}), volatile=dict(volatile or {}))

    if probe.kind == "scalars":
        for k in sorted(raw):
            v = raw[k].get("value", "")
            if _is_volatile_key(k):
                snap.volatile[k] = v
                continue
            snap.scalars[k] = _autotype(v, k, tolerances)
        return snap

    kinds = {f.key: f.kind for f in probe.fields}
    for name in sorted(raw):
        rec = raw[name]
        if "_error" in rec:
            snap.errors.append(f"{name}: {rec['_error']}")
            continue
        out: dict[str, Any] = {}
        for key in sorted(rec):
            if key.startswith("_"):
                continue
            if _is_volatile_key(key):
                continue
            out[key] = canon_value(rec[key], key, kinds.get(key, "sym"), tolerances)
        if "_class" in rec:
            out["_class"] = rec["_class"]
        snap.objects[scrub_addresses(name)] = out
    return snap


def _is_volatile_key(key: str) -> bool:
    low = key.lower()
    return any(v in low for v in VOLATILE_FIELDS)


def _autotype(v: str, key: str, tolerances: dict | None = None) -> Any:
    """Best-effort typing for scalars-probe values (no declared kinds)."""
    if v in (ABSENT, ""):
        return None
    if v == NULL_OBJ:
        return "<null>"
    if re.fullmatch(r"-?\d+", v):
        return int(v)
    if re.fullmatch(r"-?\d*\.\d+(?:[eE][-+]?\d+)?", v):
        return canon_float(v, key, tolerances)
    return scrub_addresses(v)


def apply_noise_profile(snap: Snapshot, profile: dict) -> Snapshot:
    """Drop or annotate fields the measured noise floor marked unstable.

    ``profile`` is what :mod:`noise` writes: ``{"unstable": {"<probe>":
    {"<object-or-*>": ["field", ...]}}}``. A ``"*"`` object key means the field
    is unstable for EVERY object of that probe.
    """
    unstable = (profile.get("unstable") or {}).get(snap.probe, {})
    if not unstable:
        return snap
    globals_ = set(unstable.get("*", []))
    for name, rec in snap.objects.items():
        drop = globals_ | set(unstable.get(name, []))
        for f in drop:
            rec.pop(f, None)
    for f in globals_:
        snap.scalars.pop(f, None)
    for f in unstable.get("__scalars__", []):
        snap.scalars.pop(f, None)
    return snap
