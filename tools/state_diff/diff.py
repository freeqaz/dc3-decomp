"""Structural differ: compare two snapshots, emit a RANKED divergence report.

Two design commitments make this useful rather than noisy:

**Ranking by visual impact, not by magnitude.** A material that lost its
texture and a transform that moved by 0.0002 are both "one field differs", but
only one of them is why the screen looks wrong. Severity comes from a table of
what each field *does to a pixel* (:data:`SEVERITY`), with predicates for the
cases where the transition matters more than the field (an object reference
going ``<null>``, a bounding sphere collapsing to radius 0, a texture reporting
0 KB resident).

**Collapsing.** "47 meshes all lost the same material" is ONE finding, not 47.
Findings are keyed by ``(field, old, new)`` and merged across objects, so the
report length tracks the number of distinct *causes*, not the number of
affected objects.

Findings are also suppressed when the noise floor says a field is unstable, and
the whole report is gated on the two sides being in comparable states (same
screen, neither mid-transition).
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from typing import Any

from .normalize import Snapshot, tolerance_class, DEFAULT_TOLERANCES

# Severity tiers. Lower sorts first.
BLOCKER = 0    # comparison itself is invalid
CRITICAL = 1   # object/material/texture missing or unbound: renders wrong or not at all
HIGH = 2       # render mode / frustum / lighting: whole-surface or whole-screen effect
MEDIUM = 3     # numeric parameters with visible but bounded effect
LOW = 4        # small numeric deltas, derived quantities
INFO = 5       # noise-floor annotated, or informational

SEVERITY_NAMES = {
    BLOCKER: "BLOCKER", CRITICAL: "CRITICAL", HIGH: "HIGH",
    MEDIUM: "MEDIUM", LOW: "LOW", INFO: "INFO",
}

#: field -> (severity, category). The single most important table in the tool.
SEVERITY: dict[str, tuple[int, str]] = {
    # -- binding / visibility: if these are wrong nothing else matters ------
    "showing": (CRITICAL, "visibility"),
    "is_up": (CRITICAL, "visibility"),
    "is_loaded": (CRITICAL, "visibility"),
    "loaded_dir": (CRITICAL, "binding"),
    "mat": (CRITICAL, "binding"),
    "diffuse_tex": (CRITICAL, "binding"),
    "cam": (CRITICAL, "binding"),
    "environ": (CRITICAL, "binding"),
    "hud": (CRITICAL, "binding"),
    "show_hud": (CRITICAL, "visibility"),
    "geom_owner": (CRITICAL, "binding"),
    "ambient_fog_owner": (CRITICAL, "binding"),
    "trans_parent": (CRITICAL, "hierarchy"),

    # -- render modes: change every pixel of a surface ---------------------
    "blend": (HIGH, "render_mode"),
    "z_mode": (HIGH, "render_mode"),
    "cull": (HIGH, "render_mode"),
    "stencil_mode": (HIGH, "render_mode"),
    "alpha_write": (HIGH, "render_mode"),
    "alpha_cut": (HIGH, "render_mode"),
    "prelit": (HIGH, "render_mode"),
    "per_pixel_lit": (HIGH, "render_mode"),
    "use_environ": (HIGH, "render_mode"),
    "tex_gen": (HIGH, "render_mode"),
    "tex_wrap": (HIGH, "render_mode"),
    "next_pass": (HIGH, "render_mode"),
    "normal_map": (HIGH, "binding"),
    "emissive_map": (HIGH, "binding"),
    "environ_map": (HIGH, "binding"),
    "diffuse_tex2": (HIGH, "binding"),
    "texture": (HIGH, "binding"),
    "cube_texture": (HIGH, "binding"),

    # -- camera frustum: wrong here makes EVERYTHING look wrong ------------
    "y_fov": (HIGH, "camera"),
    "near_plane": (HIGH, "camera"),
    "far_plane": (HIGH, "camera"),
    "rect_x": (HIGH, "camera"), "rect_y": (HIGH, "camera"),
    "rect_w": (HIGH, "camera"), "rect_h": (HIGH, "camera"),
    "z_range_x": (HIGH, "camera"), "z_range_y": (HIGH, "camera"),

    # -- lighting / environment: whole-scene tint or exposure --------------
    "light_type": (HIGH, "lighting"),
    "intensity": (HIGH, "lighting"),
    "ambient_color": (HIGH, "lighting"),
    "fog_enable": (HIGH, "lighting"),
    "fog_color": (HIGH, "lighting"),
    "tone_map": (HIGH, "lighting"),
    "use_color_adjust": (HIGH, "lighting"),
    "exposure": (HIGH, "lighting"),

    # -- numeric parameters ------------------------------------------------
    "color": (MEDIUM, "color"),
    "alpha": (MEDIUM, "color"),
    "range": (MEDIUM, "lighting"),
    "falloff_start": (MEDIUM, "lighting"),
    "fog_start": (MEDIUM, "lighting"), "fog_end": (MEDIUM, "lighting"),
    "ambient_alpha": (MEDIUM, "lighting"),
    "white_point": (MEDIUM, "lighting"),
    "brightness": (MEDIUM, "grading"), "contrast": (MEDIUM, "grading"),
    "saturation": (MEDIUM, "grading"), "hue": (MEDIUM, "grading"),
    "lightness": (MEDIUM, "grading"),
    "draw_order": (MEDIUM, "draw_order"),
    "num_verts": (MEDIUM, "geometry"), "num_faces": (MEDIUM, "geometry"),
    "width": (MEDIUM, "texture"), "height": (MEDIUM, "texture"),
    "bpp": (MEDIUM, "texture"), "size_kb": (MEDIUM, "texture"),
    "file_path": (MEDIUM, "texture"),
    "alpha_threshold": (MEDIUM, "render_mode"),
    "class": (CRITICAL, "hierarchy"),
    "type": (MEDIUM, "hierarchy"),
    "owner_dir": (MEDIUM, "hierarchy"),
    "focus_component": (MEDIUM, "ui"),
    "paused": (MEDIUM, "ui"),
}

#: Suffix fallbacks for the transform fields.
_SUFFIX_SEVERITY = [
    ("_pitch", (LOW, "rotation")), ("_roll", (LOW, "rotation")),
    ("_yaw", (LOW, "rotation")),
    ("_sx", (MEDIUM, "scale")), ("_sy", (MEDIUM, "scale")),
    ("_sz", (MEDIUM, "scale")),
    ("_x", (MEDIUM, "translation")), ("_y", (MEDIUM, "translation")),
    ("_z", (MEDIUM, "translation")),
]


def classify(field_name: str, a: Any, b: Any) -> tuple[int, str]:
    """Severity + category for one field transition."""
    # An object reference dropping to null is always worse than swapping it:
    # a null material/texture/dir renders nothing at all.
    if b == "<null>" and a not in ("<null>", None):
        return CRITICAL, "unbound"
    if a == "<null>" and b not in ("<null>", None):
        return CRITICAL, "unbound"
    # A property present on one side and absent on the other means the two
    # sides disagree about the object's CLASS, not just its value.
    if a is None or b is None:
        return HIGH, "schema"
    # A bounding sphere collapsing to 0 disables culling / culls everything.
    if field_name == "sphere_radius" and (a == 0 or b == 0):
        return CRITICAL, "culling"
    # A texture with no resident bytes is an asset that never loaded.
    if field_name == "size_kb" and (a == 0 or b == 0):
        return CRITICAL, "texture_load"

    if field_name in SEVERITY:
        return SEVERITY[field_name]
    for suffix, sc in _SUFFIX_SEVERITY:
        if field_name.endswith(suffix):
            return sc
    return LOW, "other"


def _significant(field_name: str, a: Any, b: Any) -> bool:
    """True when a numeric difference exceeds the field's tolerance."""
    if isinstance(a, bool) or isinstance(b, bool):
        return a != b
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        eps, _ = DEFAULT_TOLERANCES.get(
            tolerance_class(field_name), DEFAULT_TOLERANCES["default"]
        )
        return abs(float(a) - float(b)) > eps
    return a != b


@dataclass
class Finding:
    severity: int
    category: str
    probe: str
    field: str
    summary: str
    objects: list[str] = field(default_factory=list)
    a: Any = None
    b: Any = None
    unstable: bool = False

    @property
    def count(self) -> int:
        return max(1, len(self.objects))

    def to_json(self) -> dict:
        return {
            "severity": SEVERITY_NAMES[self.severity],
            "rank": self.severity,
            "category": self.category,
            "probe": self.probe,
            "field": self.field,
            "summary": self.summary,
            "count": self.count,
            "objects": self.objects[:12],
            "objects_truncated": max(0, len(self.objects) - 12),
            "a": self.a,
            "b": self.b,
            "unstable": self.unstable,
        }


def _fmt(v: Any) -> str:
    if isinstance(v, dict) and set(v) >= {"r", "g", "b"}:
        return f"rgba({v['r']},{v['g']},{v['b']},{v.get('a', 255)})"
    if v is None:
        return "<absent>"
    return str(v)


def diff_snapshots(
    a: Snapshot,
    b: Snapshot,
    noise_profile: dict | None = None,
    include_unstable: bool = False,
    max_objects_per_finding: int = 200,
) -> list[Finding]:
    """Compare two snapshots and return findings sorted by rank then count."""
    findings: list[Finding] = []
    probe = a.probe
    unstable = ((noise_profile or {}).get("unstable") or {}).get(probe, {})
    unstable_global = set(unstable.get("*", []))

    # ---- gate: is the comparison even valid? ---------------------------
    if a.probe != b.probe:
        findings.append(Finding(
            BLOCKER, "context", probe, "probe",
            f"snapshots are from DIFFERENT probes ({a.probe} vs {b.probe}); "
            "nothing below is comparable",
            a=a.probe, b=b.probe))
        return findings

    sa, sb = a.meta.get("screen"), b.meta.get("screen")
    if sa and sb and sa != sb:
        findings.append(Finding(
            BLOCKER, "context", probe, "screen",
            f"targets are on DIFFERENT screens ({sa} vs {sb}) — sync them "
            "before trusting any finding below",
            a=sa, b=sb))

    for snap, side in ((a, "a"), (b, "b")):
        if snap.errors:
            findings.append(Finding(
                BLOCKER, "context", probe, "errors",
                f"snapshot {side} ({snap.target}) recorded {len(snap.errors)} "
                f"capture error(s); coverage is incomplete: {snap.errors[0]}",
                a=len(a.errors), b=len(b.errors)))
            break

    # ---- scalars -------------------------------------------------------
    for key in sorted(set(a.scalars) | set(b.scalars)):
        va, vb = a.scalars.get(key), b.scalars.get(key)
        if va == vb:
            continue
        if key in unstable_global and not include_unstable:
            continue
        sev, cat = classify(key.rsplit(".", 1)[-1], va, vb)
        if key.startswith("ui.") or key.startswith("rnd."):
            sev = min(sev, HIGH)
        findings.append(Finding(
            sev, cat, probe, key,
            f"{key}: {_fmt(va)} -> {_fmt(vb)}",
            a=va, b=vb, unstable=key in unstable_global))

    # ---- object presence ----------------------------------------------
    ka, kb = set(a.objects), set(b.objects)
    only_a, only_b = sorted(ka - kb), sorted(kb - ka)
    if only_a:
        findings.append(Finding(
            CRITICAL, "presence", probe, "__missing__",
            f"{len(only_a)} object(s) present on {a.target} but MISSING on "
            f"{b.target}",
            objects=only_a[:max_objects_per_finding]))
    if only_b:
        findings.append(Finding(
            CRITICAL, "presence", probe, "__extra__",
            f"{len(only_b)} object(s) present on {b.target} but not on "
            f"{a.target}",
            objects=only_b[:max_objects_per_finding]))

    # ---- field differences, collapsed by (field, old, new) -------------
    groups: dict[tuple, Finding] = {}
    for name in sorted(ka & kb):
        ra, rb = a.objects[name], b.objects[name]
        obj_unstable = unstable_global | set(unstable.get(name, []))
        for fname in sorted(set(ra) | set(rb)):
            if fname.startswith("_"):
                continue
            va, vb = ra.get(fname), rb.get(fname)
            if not _significant(fname, va, vb):
                continue
            is_unstable = fname in obj_unstable
            if is_unstable and not include_unstable:
                continue
            sev, cat = classify(fname, va, vb)
            if is_unstable:
                sev = INFO
            key = (fname, json.dumps(va, sort_keys=True), json.dumps(vb, sort_keys=True))
            f = groups.get(key)
            if f is None:
                f = Finding(sev, cat, probe, fname,
                            f"{fname}: {_fmt(va)} -> {_fmt(vb)}",
                            a=va, b=vb, unstable=is_unstable)
                groups[key] = f
            if len(f.objects) < max_objects_per_finding:
                f.objects.append(name)
            else:
                f.objects.append(name)  # counted; rendering truncates
    findings.extend(groups.values())

    findings.sort(key=lambda f: (f.severity, -f.count, f.field))
    return findings


def render_report(findings: list[Finding], a: Snapshot, b: Snapshot,
                  limit: int = 40) -> str:
    """Human-readable ranked report."""
    out: list[str] = []
    out.append(f"probe: {a.probe}")
    out.append(f"  A = {a.target}  ({len(a.objects)} objects, "
               f"{len(a.scalars)} scalars, screen={a.meta.get('screen', '?')})")
    out.append(f"  B = {b.target}  ({len(b.objects)} objects, "
               f"{len(b.scalars)} scalars, screen={b.meta.get('screen', '?')})")
    if not findings:
        out.append("\nNO DIVERGENCE (within tolerance and noise floor).")
        return "\n".join(out)

    by_sev: dict[int, int] = {}
    for f in findings:
        by_sev[f.severity] = by_sev.get(f.severity, 0) + 1
    tally = "  ".join(f"{SEVERITY_NAMES[s]}={n}" for s, n in sorted(by_sev.items()))
    out.append(f"\n{len(findings)} finding(s):  {tally}\n")

    for i, f in enumerate(findings[:limit], 1):
        flag = " [UNSTABLE]" if f.unstable else ""
        out.append(f"{i:3d}. [{SEVERITY_NAMES[f.severity]:8s}] {f.category:14s} "
                   f"{f.summary}{flag}")
        if f.objects:
            shown = ", ".join(f.objects[:6])
            more = f" (+{len(f.objects) - 6} more)" if len(f.objects) > 6 else ""
            out.append(f"       {f.count} object(s): {shown}{more}")
    if len(findings) > limit:
        out.append(f"\n... {len(findings) - limit} more finding(s); "
                   "use --json for the full list.")
    return "\n".join(out)


def main(argv=None) -> int:
    import argparse

    ap = argparse.ArgumentParser(prog="state_diff.diff")
    ap.add_argument("a", help="snapshot A (baseline, e.g. console)")
    ap.add_argument("b", help="snapshot B (e.g. native)")
    ap.add_argument("--noise-profile", help="noise profile JSON")
    ap.add_argument("--include-unstable", action="store_true",
                    help="report fields the noise floor marked unstable")
    ap.add_argument("--json", action="store_true", help="emit JSON findings")
    ap.add_argument("--limit", type=int, default=40)
    ap.add_argument("--fail-at", default="none",
                    choices=["none", "blocker", "critical", "high", "medium", "low"],
                    help="exit non-zero if a finding at/above this rank exists")
    args = ap.parse_args(argv)

    from pathlib import Path

    sa = Snapshot.from_json(json.loads(Path(args.a).read_text()))
    sb = Snapshot.from_json(json.loads(Path(args.b).read_text()))
    profile = json.loads(Path(args.noise_profile).read_text()) if args.noise_profile else None

    findings = diff_snapshots(sa, sb, profile, args.include_unstable)

    if args.json:
        print(json.dumps([f.to_json() for f in findings], indent=2))
    else:
        print(render_report(findings, sa, sb, args.limit))

    thresholds = {"blocker": BLOCKER, "critical": CRITICAL, "high": HIGH,
                  "medium": MEDIUM, "low": LOW}
    if args.fail_at != "none":
        t = thresholds[args.fail_at]
        if any(f.severity <= t for f in findings):
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
