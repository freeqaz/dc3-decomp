"""Measure the run-to-run noise floor of a probe on a SINGLE target.

Why this is not optional: the engine is a live simulation. Even with the game
paused, animation timers, allocation order, uninitialised reads and RNG mean
that capturing the same probe twice from the same build does not produce
identical output. Any field that varies here is, by definition, incapable of
proving a divergence between two different targets — and reporting it as one
burns the user's limited hardware time chasing nothing.

So: capture the same probe N times from one target, and record every field that
did not hold still. The differ then suppresses those fields (or, with
``--include-unstable``, demotes them to INFO).

Output profile::

    {
      "target": "native",
      "probe": "draw_state",
      "runs": 5,
      "unstable": {"draw_state": {"*": ["field"], "<object>": ["field"]}},
      "summary": {...}
    }

A field is promoted to the ``"*"`` (all objects) bucket when it is unstable for
a majority of the objects that carry it, which keeps the profile small and
makes it generalize to objects that appear later.
"""

from __future__ import annotations

import argparse
import json
import statistics
import sys
import time
from pathlib import Path

from .budget import Limits
from .capture import add_common_args, apply_scope, build_limits, capture
from .normalize import Snapshot
from .probe import load_all
from .transport import make_target

#: A field unstable on at least this fraction of objects becomes a "*" entry.
GENERALIZE_AT = 0.5


def _values_differ(a, b) -> bool:
    return json.dumps(a, sort_keys=True) != json.dumps(b, sort_keys=True)


def measure(
    target,
    probe,
    limits: Limits,
    runs: int = 5,
    settle_s: float = 0.5,
) -> tuple[dict, list[Snapshot]]:
    """Capture ``probe`` ``runs`` times and summarise what moved."""
    snaps: list[Snapshot] = []
    for i in range(runs):
        if i:
            time.sleep(settle_s)
        snaps.append(capture(target, probe, limits))

    # ---- object presence churn ----------------------------------------
    keysets = [set(s.objects) for s in snaps]
    stable_keys = set.intersection(*keysets) if keysets else set()
    churn = sorted(set.union(*keysets) - stable_keys) if keysets else []

    # ---- per-(object, field) instability ------------------------------
    unstable_pairs: dict[str, set[str]] = {}
    field_totals: dict[str, int] = {}
    field_unstable: dict[str, int] = {}

    for name in sorted(stable_keys):
        recs = [s.objects[name] for s in snaps]
        fields = set().union(*(set(r) for r in recs))
        for f in sorted(fields):
            if f.startswith("_"):
                continue
            field_totals[f] = field_totals.get(f, 0) + 1
            first = recs[0].get(f)
            if any(_values_differ(first, r.get(f)) for r in recs[1:]):
                unstable_pairs.setdefault(name, set()).add(f)
                field_unstable[f] = field_unstable.get(f, 0) + 1

    # ---- scalars ------------------------------------------------------
    scalar_unstable: set[str] = set()
    all_scalar_keys = set().union(*(set(s.scalars) for s in snaps)) if snaps else set()
    for k in sorted(all_scalar_keys):
        first = snaps[0].scalars.get(k)
        if any(_values_differ(first, s.scalars.get(k)) for s in snaps[1:]):
            scalar_unstable.add(k)

    # ---- generalize -----------------------------------------------------
    generalized = {
        f for f, n in field_unstable.items()
        if field_totals.get(f) and n / field_totals[f] >= GENERALIZE_AT
    }
    per_object = {
        name: sorted(fs - generalized)
        for name, fs in unstable_pairs.items()
        if fs - generalized
    }

    unstable: dict[str, list[str]] = {}
    if generalized:
        unstable["*"] = sorted(generalized)
    if scalar_unstable:
        unstable["__scalars__"] = sorted(scalar_unstable)
    unstable.update(per_object)

    # Scalars probes have no objects; their cells are the scalar keys, and
    # counting only object cells made them report a misleading "0 cells".
    total_cells = sum(field_totals.values()) + len(all_scalar_keys)
    unstable_cells = (sum(len(v) for v in unstable_pairs.values())
                      + len(scalar_unstable))

    profile = {
        "target": snaps[0].target if snaps else "?",
        "probe": probe.id,
        "runs": runs,
        "settle_s": settle_s,
        "transport": limits.name,
        # A noise floor is only valid for the scope it was measured over: a
        # panel dir full of animating UI is a different measurement from the
        # static globals in `main`.
        "scope_dir": probe.scope.dir,
        "measured_at": time.time(),
        "unstable": {probe.id: unstable},
        "summary": {
            "objects_stable": len(stable_keys),
            "scalars": len(all_scalar_keys),
            "objects_churned": len(churn),
            "churned_names": churn[:20],
            "field_cells": total_cells,
            "unstable_cells": unstable_cells,
            "unstable_pct": round(100.0 * unstable_cells / total_cells, 4)
                            if total_cells else 0.0,
            "generalized_fields": sorted(generalized),
            "unstable_scalars": sorted(scalar_unstable),
            "per_object_unstable": len(per_object),
            "requests_per_run": [
                s.volatile.get("stats", {}).get("requests") for s in snaps
            ],
            "duration_s": [s.volatile.get("duration_s") for s in snaps],
            "capture_errors": [len(s.errors) for s in snaps],
        },
    }
    return profile, snaps


def merge_profiles(profiles: list[dict]) -> dict:
    """Combine per-probe profiles into one file the differ can consume."""
    out: dict = {"unstable": {}, "probes": {}}
    for p in profiles:
        out["unstable"].update(p["unstable"])
        out["probes"][p["probe"]] = p["summary"] | {
            "runs": p["runs"], "target": p["target"], "transport": p.get("transport"),
            "scope_dir": p.get("scope_dir"),
        }
    return out


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="state_diff.noise")
    add_common_args(ap)
    ap.add_argument("--probe", action="append",
                    help="probe id (repeatable; default: all)")
    ap.add_argument("--runs", type=int, default=5)
    ap.add_argument("--settle", type=float, default=0.5,
                    help="seconds between runs")
    ap.add_argument("-o", "--out", type=Path, required=True,
                    help="output noise profile JSON")
    args = ap.parse_args(argv)

    probes = load_all()
    ids = args.probe or sorted(probes)
    limits = build_limits(args)

    try:
        target = make_target(args.target)
    except NotImplementedError as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    if not target.health():
        print(f"error: target {args.target!r} is not responding", file=sys.stderr)
        return 2

    results = []
    for pid in ids:
        if pid not in probes:
            print(f"skip unknown probe {pid}", file=sys.stderr)
            continue
        try:
            profile, _ = measure(target, apply_scope(probes[pid], args), limits,
                                 args.runs, args.settle)
        except Exception as e:  # noqa: BLE001 - one bad probe must not kill the sweep
            print(f"{pid}: FAILED: {type(e).__name__}: {e}", file=sys.stderr)
            continue
        s = profile["summary"]
        print(
            f"{pid:14s} runs={args.runs} objects={s['objects_stable']} "
            f"cells={s['field_cells']} unstable={s['unstable_cells']} "
            f"({s['unstable_pct']}%) churn={s['objects_churned']}"
        )
        if s["generalized_fields"]:
            print(f"{'':14s} always-unstable fields: {s['generalized_fields']}")
        if s["unstable_scalars"]:
            print(f"{'':14s} unstable scalars: {s['unstable_scalars']}")
        results.append(profile)

    merged = merge_profiles(results)
    args.out.write_text(json.dumps(merged, indent=2, sort_keys=True))

    tot_cells = sum(p["summary"]["field_cells"] for p in results)
    tot_unstable = sum(p["summary"]["unstable_cells"] for p in results)
    pct = round(100.0 * tot_unstable / tot_cells, 4) if tot_cells else 0.0
    print(f"\nNOISE FLOOR: {tot_unstable}/{tot_cells} field cells "
          f"({pct}%) varied across {args.runs} runs -> {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
