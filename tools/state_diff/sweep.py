"""Sweep mode: drive one property through a range and measure the pixels.

A state diff answers *"do the two sides hold the same values?"*. It cannot
answer *"is this object being drawn where those values say it should be?"* —
and the highest-value UI bugs found so far were exactly that shape: every
state-visible field was **correct on the broken build**, so a static diff
reported nothing.

The technique that broke that case open was to set one property to a series of
values and watch what the pixels did. Sweeping `motd.lbl`'s `local_xfm x` and
measuring which screen columns stayed pinned separated *"the geometry is
wrong"* from *"something is occluding it / not advancing"* in a single pass.
This module promotes that from a footnote to a mode::

    python3 -m state_diff.sweep \\
        --dir panel:main_panel --object motd.lbl \\
        --prop 'local_xfm x' --range -400 600 --steps 6 \\
        --region 40,570,1100,615 -o sweep.json

Each step reports a NUMBER, not "look at these images":

* ``fg_*``    — bounding box / pixel count of non-background pixels in the
  region, thresholded on channel maximum.
* ``delta_*`` — bounding box / pixel count of pixels that differ from a
  baseline frame captured at the property's original value. This is the more
  robust of the two: it needs no notion of "background", only "what moved".

The per-edge summary is the actual finding. An edge whose ``spread`` is 0
across the whole sweep is **pinned** — geometry that should have moved and did
not, i.e. a clip plane, a stale parent transform, or an occluder.

**This works headless.** ``/api/screenshot`` renders and returns a PNG under
``MILO_HEADLESS=1`` on a machine with no display and no GPU (verified live:
1280x720 RGBA). A sandboxed agent with no framebuffer can therefore close the
loop visually, which is the whole point of the mode.

**The original value is always restored**, including on exception, on a failed
step, and on Ctrl-C — the restore is a ``finally`` and the read-back is
verified and reported in ``restored``. A sweep that leaves the engine perturbed
would silently poison every capture taken after it.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
from dataclasses import asdict, dataclass, field
from pathlib import Path

from .budget import Limits, validate_script
from .probe import assert_enumerated, dta_quote
from .transport import (DirSpecError, NativeHttpTarget, Target, TransportError,
                        dir_expr, make_target)

#: Property path components must be bare identifiers. The path is embedded in a
#: `(...)` property expression, so anything else is a DTA injection surface.
_PROP_TOKEN = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")

KIND_DEFAULT = {"float": "0.0", "int": "0", "bool": "0"}


class SweepError(RuntimeError):
    pass


def parse_prop(spec: str) -> list[str]:
    """``'local_xfm x'`` / ``'local_xfm.x'`` -> ``['local_xfm', 'x']``."""
    parts = [p for p in re.split(r"[\s.]+", (spec or "").strip()) if p]
    if not parts:
        raise SweepError("--prop is empty")
    for p in parts:
        if not _PROP_TOKEN.match(p):
            raise SweepError(
                f"property component {p!r} is not a bare identifier; refusing "
                "to embed it in a DTA property path"
            )
    return parts


def parse_region(spec: str | None, width: int, height: int) -> tuple[int, int, int, int]:
    """``'x0,y0,x1,y1'`` clipped to the image; ``None`` = the whole frame."""
    if not spec:
        return (0, 0, width, height)
    try:
        x0, y0, x1, y1 = (int(v) for v in spec.split(","))
    except ValueError:
        raise SweepError(
            f"--region {spec!r} must be four integers 'x0,y0,x1,y1'"
        ) from None
    x0, x1 = sorted((max(0, x0), min(width, x1)))
    y0, y1 = sorted((max(0, y0), min(height, y1)))
    if x1 <= x0 or y1 <= y0:
        raise SweepError(f"--region {spec!r} is empty after clipping to "
                         f"{width}x{height}")
    return (x0, y0, x1, y1)


def parse_values(args) -> list[float]:
    if args.values:
        try:
            return [float(v) for v in args.values.split(",") if v.strip()]
        except ValueError:
            raise SweepError(f"--values {args.values!r} is not a numeric list") from None
    if args.range is None:
        raise SweepError("give either --values or --range LO HI [--steps N]")
    lo, hi = args.range
    n = max(2, args.steps)
    return [lo + (hi - lo) * i / (n - 1) for i in range(n)]


def format_value(v: float, kind: str) -> str:
    if kind == "int":
        return "%d" % int(round(v))
    if kind == "bool":
        return "1" if v else "0"
    return "%.9g" % v


# --------------------------------------------------------------------------
# Pixel measurement
# --------------------------------------------------------------------------


def _numpy():
    try:
        import numpy  # noqa: PLC0415
    except ImportError as e:  # pragma: no cover - environment dependent
        raise SweepError(
            "sweep needs numpy (and Pillow) for pixel measurement: "
            "pip install numpy pillow"
        ) from e
    return numpy


def decode_png(data: bytes):
    """PNG bytes -> HxWx3 int16 array (alpha dropped, it is always opaque here)."""
    np = _numpy()
    try:
        from PIL import Image  # noqa: PLC0415
    except ImportError as e:  # pragma: no cover
        raise SweepError("sweep needs Pillow: pip install pillow") from e
    import io
    with Image.open(io.BytesIO(data)) as im:
        return np.asarray(im.convert("RGB")).astype(np.int16)


def _bbox(mask) -> dict:
    """Bounding box of a boolean mask, in ABSOLUTE image coordinates.

    Absolute, not region-relative: a caller comparing two sweeps with slightly
    different --region values would otherwise silently compare different
    coordinate systems.
    """
    np = _numpy()
    ys, xs = np.nonzero(mask)
    if xs.size == 0:
        return {"px": 0, "x0": None, "x1": None, "y0": None, "y1": None,
                "cx": None, "cy": None}
    return {
        "px": int(xs.size),
        "x0": int(xs.min()), "x1": int(xs.max()),
        "y0": int(ys.min()), "y1": int(ys.max()),
        "cx": round(float(xs.mean()), 2), "cy": round(float(ys.mean()), 2),
    }


def measure(img, region, baseline=None, bg_threshold: int = 24,
            delta_threshold: int = 24) -> dict:
    """Measure one frame inside ``region``.

    ``fg``     thresholded foreground: ``max(r,g,b) > bg_threshold``.
    ``delta``  pixels whose summed absolute channel difference from
               ``baseline`` exceeds ``delta_threshold``. Only when a baseline
               is given.
    """
    np = _numpy()
    x0, y0, x1, y1 = region
    sub = img[y0:y1, x0:x1]
    fg = sub.max(axis=2) > bg_threshold
    out = {
        "fg": _shift(_bbox(fg), x0, y0),
        "mean_luma": round(float(sub.mean()), 3),
    }
    if baseline is not None:
        base = baseline[y0:y1, x0:x1]
        if base.shape != sub.shape:
            raise SweepError(
                f"baseline frame is {base.shape}, this frame is {sub.shape}; "
                "the render surface resized mid-sweep"
            )
        d = np.abs(sub - base).sum(axis=2) > delta_threshold
        out["delta"] = _shift(_bbox(d), x0, y0)
    return out


def _shift(box: dict, dx: int, dy: int) -> dict:
    for k, off in (("x0", dx), ("x1", dx), ("cx", dx),
                   ("y0", dy), ("y1", dy), ("cy", dy)):
        if box.get(k) is not None:
            box[k] = box[k] + off
    return box


# --------------------------------------------------------------------------
# Series analysis — the part that turns pixels into a finding
# --------------------------------------------------------------------------

EDGES = ("x0", "x1", "y0", "y1", "cx", "cy", "px")


def _median(vals: list[float]) -> float:
    s = sorted(vals)
    n = len(s)
    return s[n // 2] if n % 2 else (s[n // 2 - 1] + s[n // 2]) / 2.0


def reduce_repeats(boxes: list[dict]) -> dict:
    """Collapse N measurements of the SAME value into median + spread.

    ``--repeat`` is what makes a sweep self-validating: the spread here IS the
    sweep's noise floor for this object, measured in the same session, under
    the same animation. Without it a moving UI element (a revealing label, a
    scrolling marquee) produces a series that looks like signal. Measured live
    on `motd.lbl`, whose text reveal is still running: 6 captures at ONE
    unchanged value gave delta-x1 spread 166 px and delta-px spread 1053 —
    larger than several of the steps in a naive sweep over the same property.
    """
    out: dict = {}
    for k in EDGES:
        vals = [b.get(k) for b in boxes if b and b.get(k) is not None]
        if not vals:
            out[k] = None
            out[k + "_spread"] = None
            continue
        out[k] = _median(vals)
        out[k + "_spread"] = round(max(vals) - min(vals), 3)
    out["n"] = len(boxes)
    return out


def noise_floor(series: list[dict], which: str = "fg") -> dict:
    """Worst per-edge spread seen within any single value's repeats."""
    out: dict = {}
    for edge in EDGES:
        spreads = [
            (s["measure"].get(which) or {}).get(edge + "_spread")
            for s in series
        ]
        spreads = [v for v in spreads if v is not None]
        out[edge] = max(spreads) if spreads else None
    return out


def analyse(series: list[dict], which: str = "fg") -> dict:
    """Per-edge spread / monotonicity across the sweep.

    ``pinned`` is the interesting output: an edge that did not move at all
    while the property swept its whole range. In the field case the label's
    LEFT edge stayed at x=53 for every value while the right edge tracked the
    property — which is what said "clipped", not "mispositioned".
    """
    floor = noise_floor(series, which)
    out: dict[str, dict] = {}
    for edge in EDGES:
        vals = [(s["measure"].get(which) or {}).get(edge) for s in series]
        known = [v for v in vals if v is not None]
        if not known:
            out[edge] = {"values": vals, "spread": None, "pinned": None,
                         "monotonic": None, "noise": floor.get(edge),
                         "above_noise": None}
            continue
        inc = all(b >= a for a, b in zip(known, known[1:]))
        dec = all(b <= a for a, b in zip(known, known[1:]))
        spread = round(max(known) - min(known), 3)
        n = floor.get(edge)
        out[edge] = {
            "values": vals,
            "spread": spread,
            "pinned": spread == 0 and len(known) == len(vals),
            "monotonic": bool(inc or dec) and spread > 0,
            "direction": "up" if (inc and spread > 0) else
                         ("down" if (dec and spread > 0) else "flat"),
            "missing": len(vals) - len(known),
            # A movement smaller than the same-value spread is not a movement.
            # None means --repeat was 1, i.e. the noise floor is UNMEASURED —
            # deliberately not the same as "zero".
            "noise": n,
            "above_noise": None if n is None else spread > n,
        }
    return out


def flag_saturated(analysis: dict, region: list[int]) -> str | None:
    """Warn when the foreground threshold is not discriminating anything.

    A region whose fg bbox is the region itself at every step is telling you
    the background is above ``--bg-threshold``, not that the object is pinned.
    Reporting that as ``pinned`` would be a confident wrong answer, which is
    the failure mode this whole tool is built to avoid.
    """
    x0, y0, x1, y1 = region
    box = {k: (analysis.get(k) or {}).get("values") or [] for k in
           ("x0", "x1", "y0", "y1")}
    if not box["x0"]:
        return None
    edges = ((box["x0"], x0), (box["x1"], x1 - 1),
             (box["y0"], y0), (box["y1"], y1 - 1))
    if all(vals and all(v == want for v in vals) for vals, want in edges):
        return ("fg bbox equals the whole --region at every step: the "
                "background is above --bg-threshold, so 'pinned' here means "
                "the threshold is not discriminating. Raise --bg-threshold or "
                "use the delta measure.")
    return None


# --------------------------------------------------------------------------
# Driver
# --------------------------------------------------------------------------


@dataclass
class SweepResult:
    target: str = "native"
    dir: str = "main"
    dir_expr: str = "main"
    object: str = ""
    object_class: str = ""
    prop: list[str] = field(default_factory=list)
    kind: str = "float"
    original: str = ""
    restored: bool = False
    restored_to: str = ""
    region: list[int] = field(default_factory=list)
    image_size: list[int] = field(default_factory=list)
    bg_threshold: int = 24
    delta_threshold: int = 24
    settle_frames: int = 1
    settle_s: float = 0.0
    repeat: int = 1
    baseline: dict = field(default_factory=dict)
    series: list[dict] = field(default_factory=list)
    analysis: dict = field(default_factory=dict)
    noise: dict = field(default_factory=dict)
    warnings: list[str] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)

    def to_json(self) -> dict:
        return {"tool": "state_diff.sweep", **asdict(self)}


def _obj_prefix(dexpr: str, name: str) -> str:
    return "{do ($d %s) ($o {find_obj $d %s}) " % (dexpr, dta_quote(name))


def read_prop(target: Target, dexpr: str, name: str, path: list[str],
              kind: str, limits: Limits) -> str:
    """Read one property as a lossless string (``%.9g`` round-trips float32)."""
    p = " ".join(path)
    if kind == "float":
        body = '{sprintf "%%.9g" {$o get (%s) 0.0}}' % p
    else:
        body = '{sprintf "%%d" {$o get (%s) %s}}' % (p, KIND_DEFAULT[kind])
    expr = _obj_prefix(dexpr, name) + '{if_else $o %s "<null>"}}' % body
    validate_script(expr, limits, "sweep read")
    res = target.eval_dta(expr)
    if not res.ok:
        raise SweepError(f"reading ({p}) on {name}: {res.error}")
    if res.text == "<null>":
        raise SweepError(f"{name} resolved to null in {dexpr}")
    return res.text


def write_prop(target: Target, dexpr: str, name: str, path: list[str],
               literal: str, limits: Limits) -> str:
    """Set the property and return the value read back, so a silently-clamped
    or ignored write is visible in the series rather than misread as a null
    result."""
    p = " ".join(path)
    expr = (_obj_prefix(dexpr, name)
            + "{$o set (%s) %s}" % (p, literal)
            + '{sprintf "%%.9g" {$o get (%s) 0.0}}}' % p)
    validate_script(expr, limits, "sweep write")
    res = target.eval_dta(expr)
    if not res.ok:
        raise SweepError(f"setting ({p})={literal} on {name}: {res.error}")
    return res.text


def run_sweep(
    target: NativeHttpTarget,
    dir_spec: str,
    name: str,
    path: list[str],
    values: list[float],
    kind: str = "float",
    region_spec: str | None = None,
    bg_threshold: int = 24,
    delta_threshold: int = 24,
    settle_frames: int = 1,
    settle_s: float = 0.0,
    repeat: int = 1,
    limits: Limits | None = None,
    save_frames: Path | None = None,
) -> SweepResult:
    limits = limits or Limits.portable()
    repeat = max(1, repeat)
    dexpr = dir_expr(dir_spec)
    res = SweepResult(
        target=target.name, dir=dir_spec, dir_expr=dexpr, object=name,
        prop=list(path), kind=kind, bg_threshold=bg_threshold,
        delta_threshold=delta_threshold, settle_frames=settle_frames,
        settle_s=settle_s, repeat=repeat,
    )

    # Pass 1: the object must have been ENUMERATED, exactly as a probe would.
    # Naming an object that does not exist faults the title, and a sweep that
    # types a name straight into a `set` would be the one place in this tool
    # where a typo is destructive rather than merely wrong.
    roster_errors: list[str] = []
    roster = target.roster(dir_spec, errors=roster_errors)
    res.errors.extend(roster_errors)
    by_name = {r.name: r for r in roster}
    assert_enumerated([name], set(by_name), "sweep")
    res.object_class = by_name[name].type

    def shot():
        if settle_s:
            time.sleep(settle_s)
        for _ in range(max(0, settle_frames)):
            target.screenshot()          # each call blocks for a rendered frame
        return target.screenshot()

    original = read_prop(target, dexpr, name, path, kind, limits)
    res.original = original

    try:
        raw = shot()
        base_img = decode_png(raw)
        h, w = base_img.shape[0], base_img.shape[1]
        res.image_size = [w, h]
        region = parse_region(region_spec, w, h)
        res.region = list(region)
        if save_frames:
            save_frames.mkdir(parents=True, exist_ok=True)
            (save_frames / "baseline.png").write_bytes(raw)
        res.baseline = measure(base_img, region, None, bg_threshold,
                               delta_threshold)

        for i, v in enumerate(values):
            literal = format_value(v, kind)
            entry: dict = {"value": v, "literal": literal}
            try:
                entry["applied"] = write_prop(target, dexpr, name, path,
                                              literal, limits)
                reps = []
                for k in range(repeat):
                    raw = shot()
                    if save_frames:
                        (save_frames / f"{i:03d}_{literal}_{k}.png"
                         ).write_bytes(raw)
                    reps.append(measure(decode_png(raw), region, base_img,
                                        bg_threshold, delta_threshold))
                entry["measure"] = {
                    "fg": reduce_repeats([r["fg"] for r in reps]),
                    "delta": reduce_repeats([r.get("delta", {}) for r in reps]),
                    "mean_luma": round(
                        sum(r["mean_luma"] for r in reps) / len(reps), 3),
                }
            except (SweepError, TransportError) as e:
                entry["error"] = f"{type(e).__name__}: {e}"
                entry["measure"] = {}
                res.errors.append(f"value {literal}: {e}")
            res.series.append(entry)

        res.analysis = {"fg": analyse(res.series, "fg"),
                        "delta": analyse(res.series, "delta")}
        res.noise = {"fg": noise_floor(res.series, "fg"),
                     "delta": noise_floor(res.series, "delta")}
        sat = flag_saturated(res.analysis["fg"], res.region)
        if sat:
            res.warnings.append(sat)
        if repeat == 1:
            res.warnings.append(
                "--repeat 1: the sweep's own noise floor was NOT measured, so "
                "'above_noise' is null everywhere. Re-run with --repeat 3+ "
                "before trusting a small movement; animating UI objects have "
                "been measured at >100px of same-value spread."
            )
    finally:
        # Unconditional: an exception, a transport drop or a Ctrl-C must not
        # leave the engine holding a swept value, or every capture taken
        # afterwards silently measures the perturbation instead of the game.
        try:
            back = write_prop(target, dexpr, name, path, original, limits)
            res.restored_to = back
            res.restored = _close(back, original)
            if not res.restored:
                res.errors.append(
                    f"RESTORE MISMATCH: set ({' '.join(path)}) back to "
                    f"{original} but read {back}; the engine may be perturbed"
                )
        except Exception as e:  # noqa: BLE001 - never mask the original failure
            res.restored = False
            res.errors.append(f"RESTORE FAILED: {type(e).__name__}: {e}")
    return res


def _close(a: str, b: str) -> bool:
    try:
        return abs(float(a) - float(b)) <= 1e-4 * max(1.0, abs(float(b)))
    except ValueError:
        return a == b


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------


def report(res: SweepResult) -> str:
    lines = [
        f"sweep {res.object} [{res.object_class}] ({' '.join(res.prop)}) "
        f"in {res.dir} -> {res.dir_expr}",
        f"  frame {res.image_size[0]}x{res.image_size[1]}  region {res.region}"
        f"  original={res.original}  restored={res.restored}"
        f"  repeat={res.repeat}",
        "",
        f"  {'value':>12} {'applied':>12} | {'fg px':>8} {'x0':>5} {'x1':>5} "
        f"{'y0':>5} {'y1':>5} | {'d px':>8} {'x0':>5} {'x1':>5} {'y0':>5} {'y1':>5}",
    ]

    def cells(box):
        if not box:
            return f"{'-':>8} {'-':>5} {'-':>5} {'-':>5} {'-':>5}"
        def f(k):
            v = box.get(k)
            return "-" if v is None else (int(v) if float(v).is_integer() else v)
        return (f"{f('px'):>8} {f('x0'):>5} {f('x1'):>5} "
                f"{f('y0'):>5} {f('y1'):>5}")

    lines.append(f"  {'(baseline)':>12} {res.original:>12} | "
                 f"{cells(res.baseline.get('fg'))} | {cells(None)}")
    for s in res.series:
        m = s.get("measure") or {}
        lines.append(
            f"  {s['literal']:>12} {str(s.get('applied', '?')):>12} | "
            f"{cells(m.get('fg'))} | {cells(m.get('delta'))}"
            + ("   " + s["error"] if s.get("error") else "")
        )

    for which in ("fg", "delta"):
        a = res.analysis.get(which) or {}
        floor = res.noise.get(which) or {}
        pinned = [e for e in EDGES if (a.get(e) or {}).get("pinned")]
        moved = []
        for e in EDGES:
            info = a.get(e) or {}
            if not info.get("spread"):
                continue
            n = info.get("noise")
            tag = "" if n is None else (" >noise" if info["above_noise"]
                                        else " <=NOISE")
            moved.append(f"{e}({info['direction']},spread={info['spread']}{tag})")
        lines.append("")
        lines.append(f"  {which}: pinned={pinned or '-'}  moved={moved or '-'}")
        if res.repeat > 1:
            nz = {k: v for k, v in floor.items() if v}
            lines.append(f"       same-value noise floor: {nz or 'zero'}")
    for w in res.warnings:
        lines.append("")
        lines.append(f"  WARNING: {w}")
    if res.errors:
        lines.append("")
        lines.append(f"  {len(res.errors)} error(s):")
        lines += [f"    - {e}" for e in res.errors]
    return "\n".join(lines)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        prog="state_diff.sweep",
        description="Drive one property through a range of values and measure "
                    "the rendered pixels at each. Works headless.")
    ap.add_argument("--target", default="native",
                    help="native | native:http://host:port")
    ap.add_argument("--dir", dest="dir_spec", default="main",
                    help="'main', 'panel:<panel>' or a '{...}' DTA expression")
    ap.add_argument("--object", required=True, help="object name to sweep")
    ap.add_argument("--prop", required=True,
                    help="property path, e.g. 'local_xfm x'")
    ap.add_argument("--kind", default="float", choices=["float", "int", "bool"])
    ap.add_argument("--values", help="comma-separated values")
    ap.add_argument("--range", nargs=2, type=float, metavar=("LO", "HI"))
    ap.add_argument("--steps", type=int, default=6)
    ap.add_argument("--region", help="x0,y0,x1,y1 measurement window "
                                     "(default: whole frame)")
    ap.add_argument("--bg-threshold", type=int, default=24,
                    help="max(r,g,b) above this counts as foreground")
    ap.add_argument("--delta-threshold", type=int, default=24,
                    help="summed |channel| difference vs baseline that counts "
                         "as changed")
    ap.add_argument("--settle-frames", type=int, default=1,
                    help="frames to render and discard after each set")
    ap.add_argument("--settle", type=float, default=0.0,
                    help="seconds to wait after each set")
    ap.add_argument("--repeat", type=int, default=1,
                    help="captures per value. >1 measures the sweep's OWN "
                         "noise floor in the same session, which is the only "
                         "way to tell a real movement from an animating UI "
                         "object. Use 3+ for anything you intend to act on.")
    ap.add_argument("--save-frames", type=Path,
                    help="directory to write each captured PNG into")
    ap.add_argument("-o", "--out", type=Path, help="output sweep JSON")
    args = ap.parse_args(argv)

    try:
        path = parse_prop(args.prop)
        values = parse_values(args)
        dir_expr(args.dir_spec)
    except (SweepError, DirSpecError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 3

    target = make_target(args.target)
    if not isinstance(target, NativeHttpTarget):
        print("error: sweep needs /api/screenshot, which only the native "
              "target provides", file=sys.stderr)
        return 2
    if not target.health():
        print(f"error: target {args.target!r} is not responding", file=sys.stderr)
        return 2

    try:
        res = run_sweep(
            target, args.dir_spec, args.object, path, values, kind=args.kind,
            region_spec=args.region, bg_threshold=args.bg_threshold,
            delta_threshold=args.delta_threshold,
            settle_frames=args.settle_frames, settle_s=args.settle,
            repeat=args.repeat, save_frames=args.save_frames,
        )
    except (SweepError, DirSpecError, TransportError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 2

    print(report(res))
    if args.out:
        args.out.write_text(json.dumps(res.to_json(), indent=2, sort_keys=True))
        print(f"\n-> {args.out}")
    return 1 if res.errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
