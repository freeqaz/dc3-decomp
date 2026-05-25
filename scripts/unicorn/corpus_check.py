#!/usr/bin/env python3
"""Unicorn equivalence checker regression suite.

Reads corpus.yaml, re-runs the unicorn comparator against each entry,
reports verdict/class deltas. Used as a gate before bumping
SIGNAL_VERSION in any phase that changes verdict semantics.

Exit codes:
  0 — every entry matches its expected verdict and class
  1 — at least one TP_BROKEN or DIVERGENT_CLASS_CHANGED (regression)
  2 — at least one STILL_FP (FP that we want fixed remains an FP)
  3 — at least one FP_FIXED but TPs also moved (mixed result, review)

Usage:
  python3 scripts/unicorn/corpus_check.py
  python3 scripts/unicorn/corpus_check.py --verbose
  python3 scripts/unicorn/corpus_check.py --signal-version 2  # if running ahead of a bump
"""

import argparse
import os
import sys
import time
from collections import Counter, defaultdict

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, PROJECT_ROOT)

import yaml  # type: ignore  # noqa

from scripts.unicorn_runner.coff import COFFParser
from scripts.unicorn_runner.comparator import classify_divergence
from scripts.unicorn_runner.engine import UnicornEngine
from scripts.unicorn_runner.run import (
    resolve_unit,
    _run_comparison_core,
    EXIT_EQUIVALENT, EXIT_DIVERGENT, EXIT_ERROR, EXIT_SKIPPED,
)
from scripts.unicorn_runner.signal_version import SIGNAL_VERSION


CORPUS_PATH = os.path.join(PROJECT_ROOT, "scripts", "unicorn", "corpus.yaml")


def _load_corpus(path):
    with open(path) as f:
        data = yaml.safe_load(f) or {}
    return {
        "known_tps": data.get("known_tps") or [],
        "known_divergent": data.get("known_divergent") or [],
        "known_fps": data.get("known_fps") or [],
    }


def _run_one(entry, engine, coff_cache):
    """Execute one corpus entry. Returns (verdict_str, class_str, error_msg)."""
    sym = entry["symbol"]
    unit = entry["unit"]
    try:
        decomp_path, orig_path = resolve_unit(unit, project_root=PROJECT_ROOT)
    except ValueError as e:
        return "UNIT_LOOKUP_FAILED", None, str(e)
    if not os.path.exists(decomp_path) or not os.path.exists(orig_path):
        return "OBJ_MISSING", None, f"decomp={os.path.exists(decomp_path)} orig={os.path.exists(orig_path)}"
    if decomp_path not in coff_cache:
        coff_cache[decomp_path] = COFFParser(decomp_path)
    if orig_path not in coff_cache:
        coff_cache[orig_path] = COFFParser(orig_path)

    exit_code, bundle, _, err = _run_comparison_core(
        sym, coff_cache[decomp_path], coff_cache[orig_path],
        timeout=5_000_000, engine=engine,
    )

    if exit_code == EXIT_EQUIVALENT:
        return "EQUIVALENT", None, None
    if exit_code == EXIT_DIVERGENT and bundle is not None:
        cls = classify_divergence(
            bundle.result, bundle.decomp_result, bundle.orig_result,
            bundle.decomp_relocs, bundle.orig_relocs,
        )
        return "DIVERGENT", cls, None
    if exit_code == EXIT_SKIPPED:
        return "SKIPPED", None, err
    return "ERROR", None, err


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Show per-entry detail")
    parser.add_argument("--signal-version", type=int, default=None,
                        help="Override displayed signal version (for pre-bump dry runs)")
    parser.add_argument("--corpus", default=CORPUS_PATH,
                        help=f"Path to corpus YAML (default: {CORPUS_PATH})")
    args = parser.parse_args()

    sv = args.signal_version if args.signal_version is not None else SIGNAL_VERSION

    corpus = _load_corpus(args.corpus)
    n_tp = len(corpus["known_tps"])
    n_div = len(corpus["known_divergent"])
    n_fp = len(corpus["known_fps"])
    print(f"Corpus check at SIGNAL_VERSION={sv}")
    print(f"  known_tps:        {n_tp}")
    print(f"  known_divergent:  {n_div}")
    print(f"  known_fps:        {n_fp}")
    print()

    engine = UnicornEngine()
    coff_cache = {}

    counts = Counter()
    failures = []  # (severity, kind, entry, observed_verdict, observed_class)
    t_start = time.time()

    def check(section_name, entries, want_verdict):
        for entry in entries:
            sym = entry["symbol"]
            expected_class = entry.get("expected_class")
            observed_verdict, observed_class, err = _run_one(entry, engine, coff_cache)

            ok = (observed_verdict == want_verdict)
            class_ok = True
            if want_verdict == "DIVERGENT" and expected_class is not None:
                class_ok = (observed_class == expected_class)

            if observed_verdict in ("UNIT_LOOKUP_FAILED", "OBJ_MISSING",
                                    "ERROR", "SKIPPED"):
                counts["INFRA_ERROR"] += 1
                failures.append(("warn", "INFRA_ERROR", entry,
                                 observed_verdict, observed_class, err))
                continue

            if section_name == "known_tps":
                if ok:
                    counts["STILL_TP"] += 1
                else:
                    counts["TP_BROKEN"] += 1
                    failures.append(("fail", "TP_BROKEN", entry,
                                     observed_verdict, observed_class, None))
            elif section_name == "known_divergent":
                if ok and class_ok:
                    counts["STILL_DIV"] += 1
                elif ok and not class_ok:
                    counts["DIV_CLASS_CHANGED"] += 1
                    failures.append(("fail", "DIV_CLASS_CHANGED", entry,
                                     observed_verdict, observed_class, None))
                else:
                    counts["DIV_FLIPPED"] += 1
                    failures.append(("fail", "DIV_FLIPPED", entry,
                                     observed_verdict, observed_class, None))
            elif section_name == "known_fps":
                # known_fps are entries we WANT fixed. expected_verdict is
                # what they currently produce (DIVERGENT) — when the fix
                # lands, observed will become EQUIVALENT.
                if ok:
                    counts["STILL_FP"] += 1
                    failures.append(("info", "STILL_FP", entry,
                                     observed_verdict, observed_class, None))
                else:
                    counts["FP_FIXED"] += 1

    check("known_tps", corpus["known_tps"], "EQUIVALENT")
    check("known_divergent", corpus["known_divergent"], "DIVERGENT")
    check("known_fps", corpus["known_fps"], "DIVERGENT")

    elapsed = time.time() - t_start
    total_runs = n_tp + n_div + n_fp
    print(f"Ran {total_runs} entries in {elapsed:.1f}s "
          f"({elapsed/max(total_runs,1)*1000:.1f} ms/entry)")
    print()

    print("Counts:")
    for k in ("STILL_TP", "TP_BROKEN",
              "STILL_DIV", "DIV_CLASS_CHANGED", "DIV_FLIPPED",
              "STILL_FP", "FP_FIXED", "INFRA_ERROR"):
        if counts[k] > 0:
            print(f"  {k:20s} {counts[k]}")

    if args.verbose or failures:
        print()
        for severity, kind, entry, obs_v, obs_c, err in failures:
            tag = {"fail": "FAIL", "warn": "WARN", "info": "INFO"}[severity]
            extra = f" err={err}" if err else ""
            expected_v = entry.get("expected_verdict")
            expected_c = entry.get("expected_class")
            print(f"  [{tag}] {kind} {entry['symbol'][:70]}")
            print(f"         expected={expected_v}/{expected_c}  "
                  f"observed={obs_v}/{obs_c}{extra}")

    # Exit code
    has_fail = any(s == "fail" for s, *_ in failures)
    has_still_fp = counts["STILL_FP"] > 0
    has_fp_fixed = counts["FP_FIXED"] > 0
    if has_fail:
        return 1
    if has_still_fp and not has_fp_fixed:
        return 2
    if has_fp_fixed and counts["TP_BROKEN"] > 0:
        return 3
    return 0


if __name__ == "__main__":
    sys.exit(main())
