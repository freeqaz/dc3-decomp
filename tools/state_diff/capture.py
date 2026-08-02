"""Capture driver: run a probe against a target, normalize, store a snapshot.

    python3 -m state_diff.capture --target native --probe draw_state -o a.json
    python3 -m state_diff.capture --list

By default the console transport caps are ENFORCED even for native captures,
so a native-only run proves the probe is viable on real hardware. Pass
``--no-console-caps`` to lift them (faster, native-only).
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

from .budget import BudgetError, Limits
from .normalize import Snapshot, apply_noise_profile, normalize
from .probe import load_all, run_probe
from .transport import Target, TransportError, make_target


def capture(
    target: Target,
    probe,
    limits: Limits,
    noise_profile: dict | None = None,
) -> Snapshot:
    t0 = time.time()
    raw, stats = run_probe(target, probe, limits=limits)

    meta = target.describe()
    volatile = {}
    if hasattr(target, "volatile"):
        volatile.update(target.volatile())
    # The active screen is recorded in the HEADER, not as a diffable field:
    # comparing two snapshots taken on different screens is meaningless, and
    # the differ checks this before reporting anything else.
    if hasattr(target, "current_screen"):
        s = target.current_screen()
        if s:
            meta["screen"] = s

    snap = normalize(probe, raw, target.name, meta=meta, volatile=volatile)
    snap.volatile["captured_at"] = time.time()
    snap.volatile["duration_s"] = round(time.time() - t0, 3)
    snap.volatile["stats"] = {
        "requests": stats.requests,
        "objects": stats.objects,
        "eval_failures": stats.eval_failures,
        "fallbacks": stats.fallbacks,
        "max_script": stats.max_script,
        "max_result": stats.max_result,
    }
    snap.errors.extend(stats.errors)
    if noise_profile:
        apply_noise_profile(snap, noise_profile)
    return snap


def build_limits(args) -> Limits:
    """Console caps are enforced by DEFAULT even on native, so a native-only
    run still proves the probe is viable on hardware."""
    if getattr(args, "no_console_caps", False):
        return Limits.unlimited()
    base = Limits.named(getattr(args, "transport", "portable"))
    return Limits(
        max_script=args.script_cap or base.max_script,
        max_result=args.result_cap or base.max_result,
        one_command=base.one_command,
        enforced=base.enforced,
        name=base.name,
    )


def add_common_args(ap: argparse.ArgumentParser) -> None:
    ap.add_argument("--target", default="native",
                    help="native | native:http://host:port | console")
    ap.add_argument("--transport", default="portable",
                    choices=["portable", "native_http", "post_eval",
                             "legacy_get", "unlimited"],
                    help="transport profile to size pages against. Default "
                         "'portable' = intersection of native (8192B body) and "
                         "console POST /dta/eval (16K in / 32K out)")
    ap.add_argument("--script-cap", type=int, default=None,
                    help="override max DTA script bytes per request")
    ap.add_argument("--result-cap", type=int, default=None,
                    help="override max reply bytes per request")
    ap.add_argument("--no-console-caps", action="store_true",
                    help="lift transport caps (native-only; probe may not run on hardware)")
    ap.add_argument("--noise-profile", type=Path,
                    help="noise profile JSON to elide unstable fields")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="state_diff.capture")
    add_common_args(ap)
    ap.add_argument("--probe", help="probe id (see --list)")
    ap.add_argument("-o", "--out", type=Path, help="output snapshot JSON")
    ap.add_argument("--list", action="store_true", help="list probes and exit")
    args = ap.parse_args(argv)

    probes = load_all()
    if args.list:
        for pid, p in sorted(probes.items()):
            print(f"{pid:14s} [{p.kind}] {p.doc}")
            if p.discriminates:
                print(f"{'':14s} discriminates: {p.discriminates}")
        return 0

    if not args.probe:
        ap.error("--probe is required (or use --list)")
    if args.probe not in probes:
        ap.error(f"unknown probe {args.probe!r}; try --list")

    profile = json.loads(args.noise_profile.read_text()) if args.noise_profile else None

    try:
        target = make_target(args.target)
    except NotImplementedError as e:
        print(f"error: {e}", file=sys.stderr)
        return 2

    if not target.health():
        print(f"error: target {args.target!r} is not responding", file=sys.stderr)
        return 2

    try:
        snap = capture(target, probes[args.probe], build_limits(args), profile)
    except BudgetError as e:
        print(f"budget error: {e}", file=sys.stderr)
        return 3
    except TransportError as e:
        print(f"transport error: {e}", file=sys.stderr)
        return 2

    payload = json.dumps(snap.to_json(), indent=2, sort_keys=True)
    if args.out:
        args.out.write_text(payload)
        st = snap.volatile.get("stats", {})
        print(
            f"{args.probe}: {len(snap.objects) or len(snap.scalars)} records, "
            f"{st.get('requests')} requests, max script {st.get('max_script')}B, "
            f"max reply {st.get('max_result')}B -> {args.out}"
        )
        if snap.errors:
            print(f"  {len(snap.errors)} error(s); first: {snap.errors[0]}")
    else:
        print(payload)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
