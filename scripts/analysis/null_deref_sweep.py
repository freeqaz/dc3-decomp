#!/usr/bin/env python3
"""Sweep the decomp TUs with clang's static analyzer for the "conjunction proves
null, then deref anyway" class.

Background
----------
`MoveDir::PostUpdateFilters` computes

    bool active = feedback && playerData && playerData->IsPlaying();
    if (!active) { feedback->ResetErrors(); }

The `!active` arm is entered precisely when `feedback` may be null, and then
dereferences it.  On the Xbox 360 guest page 0 (0x0-0x10000) is mapped
readable/writable/zeroed, so a small-offset load or store through a null `this`
is absorbed.  Linux never maps page 0, so the identical access SIGSEGVs.  The
class is therefore "unguarded deref of a provably-null pointer", not "hardcoded
pointer size".

Two hand-written regexes were tried for this and BOTH failed (recorded so they
are not retried): a narrow one required the conjunction be bound to a named
`bool` and missed every inline `if (p && p->x())`; a widened one returned 144
near-all-false hits because a regex cannot tell which side of a branch a deref
sits on.  The instrument that works is clang's own path-sensitive analyzer,
which reports the full "Assuming 'p' is null / Left side of '&&' is false /
Taking true branch" trail.

Usage
-----
    scripts/analysis/null_deref_sweep.py --selftest      # validate the instrument
    scripts/analysis/null_deref_sweep.py --jobs 16       # full sweep
    scripts/analysis/null_deref_sweep.py --file src/system/ui/UI.cpp

`--selftest` re-introduces the MoveDir bug in a scratch copy and asserts the
analyzer names it, then asserts the analyzer is silent on the fixed file.  A
sweep whose instrument has not been shown to fire on a known instance is not
evidence of anything.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_CC = os.path.join(REPO, "native", "build", "compile_commands.json")

# Checkers that can express "this pointer was proven null on this path".
CHECKERS = "core.CallAndMessage,core.NullDereference,core.NonNullParamChecker"

# The analyzer's own words for the shape we are hunting.
PROVES_NULL = re.compile(
    r"Assuming '([A-Za-z_][A-Za-z0-9_]*)' is null"
    r"|Assuming pointer value is null"
    r"|'([A-Za-z_][A-Za-z0-9_]*)' is null"
)


def load_units(cc_path: str, only: str | None) -> list[dict]:
    with open(cc_path) as f:
        db = json.load(f)
    src_root = os.path.join(REPO, "src") + os.sep
    seen: dict[str, dict] = {}
    for entry in db:
        path = entry["file"]
        if not path.startswith(src_root):
            continue
        if only and not path.endswith(only):
            continue
        seen.setdefault(path, entry)  # one command per TU; targets duplicate them
    return sorted(seen.values(), key=lambda e: e["file"])


def analyze_command(entry: dict, override_file: str | None = None) -> list[str]:
    cmd = shlex.split(entry["command"]) if "command" in entry else list(entry["arguments"])
    out: list[str] = []
    skip = 0
    for i, arg in enumerate(cmd):
        if skip:
            skip -= 1
            continue
        if arg in ("-o", "-MF", "-MT", "-MQ"):
            skip = 1
            continue
        # CMake's PCH: `-Winvalid-pch -Xclang -include-pch -Xclang <x.pch> -include <x.hxx>`.
        # The .pch only exists for targets that have actually been built, and a
        # stale/absent one is a hard error -- so drop the precompiled form and
        # let the surviving `-include <x.hxx>` pull the same header in as text.
        # (This cost the first run of this sweep a false "instrument is blind"
        # verdict: every TU died with "unable to read PCH file" and reported
        # zero findings, which looks exactly like a clean sweep.)
        if arg == "-Winvalid-pch":
            continue
        if arg == "-include-pch":
            if out and out[-1] == "-Xclang":
                out.pop()
            skip = 2  # -Xclang <path/to.pch>
            continue
        if arg == "-c":
            continue
        if arg.startswith("-Werror"):
            continue
        if i > 0 and arg == entry["file"]:
            continue
        out.append(arg)
    out += [
        "--analyze",
        "-Xclang",
        "-analyzer-output=text",
        "-Xclang",
        "-analyzer-checker=" + CHECKERS,
        # NB: do NOT add -fsyntax-only here. It overrides the -analyze action,
        # and the analyzer then exits 0 having emitted nothing at all -- which
        # is indistinguishable from "swept clean". Caught by --selftest.
        override_file or entry["file"],
    ]
    return out


def run_one(entry: dict, timeout: int, override_file: str | None = None) -> dict:
    cmd = analyze_command(entry, override_file)
    rel = os.path.relpath(override_file or entry["file"], REPO)
    try:
        proc = subprocess.run(
            cmd,
            cwd=entry["directory"],
            capture_output=True,
            text=True,
            timeout=timeout,
        )
        hard = [l for l in proc.stderr.splitlines() if ": error:" in l or ": fatal error:" in l]
        return {
            "file": rel,
            "rc": proc.returncode,
            "stderr": proc.stderr,
            "timeout": False,
            "errors": hard,
        }
    except subprocess.TimeoutExpired:
        return {"file": rel, "rc": None, "stderr": "", "timeout": True, "errors": []}


def parse_reports(stderr: str) -> list[dict]:
    """Group the analyzer's text output into one record per warning."""
    reports: list[dict] = []
    cur: dict | None = None
    for line in stderr.splitlines():
        m = re.match(r"^(.*?):(\d+):(\d+): (warning|note): (.*)$", line)
        if not m:
            continue
        path, lineno, _col, kind, msg = m.groups()
        if kind == "warning":
            cur = {"path": path, "line": int(lineno), "message": msg, "trail": []}
            reports.append(cur)
        elif cur is not None:
            cur["trail"].append(msg)
    return reports


# Only these warnings are about dereferencing a pointer. Without this filter the
# class picks up core.DivideZero and cplusplus.NewDeleteLeaks reports whose
# TRAIL happens to contain a short-circuit note -- 3 of 23 on the 2026-08-21
# sweep -- and the headline count overstates the class by that much.
DEREF_WARNINGS = (
    "Called C++ object pointer is null",
    "dereference of a null pointer",
    "Dereference of null pointer",
    "Forming reference to null pointer",
    "Access to field",
)


def classify(report: dict) -> str:
    """conjunction-proves-null  |  other-null  |  not-a-deref  |  unrelated"""
    trail = " || ".join(report["trail"])
    proves = bool(PROVES_NULL.search(trail))
    branch = "Left side of '&&' is false" in trail or "Left side of '||' is true" in trail
    is_deref = any(w in report["message"] for w in DEREF_WARNINGS)
    if proves and branch:
        return "conjunction-proves-null" if is_deref else "not-a-deref"
    if proves:
        return "other-null" if is_deref else "not-a-deref"
    return "unrelated"


SELFTEST_GUARDED = """            if (feedback) {
                feedback->ResetErrors();
            }"""
SELFTEST_UNGUARDED = """            feedback->ResetErrors();"""


def selftest(units: list[dict], timeout: int) -> int:
    """Positive control (bug re-introduced) then negative control (fixed file)."""
    movedir = os.path.join(REPO, "src", "system", "hamobj", "MoveDir.cpp")
    entry = next((u for u in units if u["file"] == movedir), None)
    if entry is None:
        print("SELFTEST: MoveDir.cpp not in the compile database", file=sys.stderr)
        return 2

    with open(movedir) as f:
        fixed_text = f.read()
    if SELFTEST_GUARDED not in fixed_text:
        print("SELFTEST: the guard's exact text moved; re-aim the control", file=sys.stderr)
        return 2
    broken_text = fixed_text.replace(SELFTEST_GUARDED, SELFTEST_UNGUARDED, 1)

    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        # Positive control: the analyzer must name the re-introduced bug.
        broken = os.path.join(os.path.dirname(movedir), ".selftest_MoveDir_broken.cpp")
        try:
            with open(broken, "w") as f:
                f.write(broken_text)
            res = run_one(entry, timeout, override_file=broken)
            if res["errors"]:
                print("SELFTEST FAIL: the analyzer could not compile the control:")
                for e in res["errors"][:5]:
                    print("   " + e)
                ok = False
            hits = [r for r in parse_reports(res["stderr"]) if classify(r) == "conjunction-proves-null"]
            named = [r for r in hits if "MoveDir" in r["path"]]
            print(f"SELFTEST positive control: {len(named)} conjunction-proves-null report(s)")
            for r in named:
                print(f"   {os.path.basename(r['path'])}:{r['line']}: {r['message']}")
            if not named:
                print("SELFTEST FAIL: instrument is blind to the bug it exists to find")
                ok = False
        finally:
            if os.path.exists(broken):
                os.unlink(broken)

        # Negative control: silence on the fixed file.
        res = run_one(entry, timeout)
        hits = [
            r
            for r in parse_reports(res["stderr"])
            if classify(r) == "conjunction-proves-null" and "MoveDir" in r["path"]
        ]
        print(f"SELFTEST negative control: {len(hits)} report(s) on the fixed file")
        for r in hits:
            print(f"   {os.path.basename(r['path'])}:{r['line']}: {r['message']}")
        if hits:
            print("SELFTEST FAIL: instrument fires on the fixed file")
            ok = False
        _ = tmp
    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--compile-commands", default=DEFAULT_CC)
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 8)
    ap.add_argument("--timeout", type=int, default=600)
    ap.add_argument("--file", help="analyze only TUs whose path ends with this")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--json-out")
    args = ap.parse_args()

    units = load_units(args.compile_commands, args.file)
    if not units:
        print("no matching TUs in the compile database", file=sys.stderr)
        return 2

    if args.selftest:
        return selftest(units, args.timeout)

    print(f"sweeping {len(units)} decomp TUs with {args.jobs} workers", flush=True)
    results = []
    timeouts = []
    broken_tus = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futs = {pool.submit(run_one, u, args.timeout): u for u in units}
        done = 0
        for fut in concurrent.futures.as_completed(futs):
            res = fut.result()
            done += 1
            if done % 50 == 0:
                print(f"  {done}/{len(units)}", flush=True)
            if res["timeout"]:
                timeouts.append(res["file"])
                continue
            if res["errors"]:
                # A TU that did not compile contributes no findings; counting it
                # as "clean" is how a sweep reports a silent zero.
                broken_tus.append((res["file"], res["errors"][0]))
                continue
            for rep in parse_reports(res["stderr"]):
                rep["tu"] = res["file"]
                rep["klass"] = classify(rep)
                results.append(rep)

    # Deduplicate: the same header-inlined report can surface from several TUs.
    uniq: dict[tuple, dict] = {}
    for r in results:
        uniq.setdefault((r["path"], r["line"], r["message"]), r)
    results = sorted(uniq.values(), key=lambda r: (r["path"], r["line"]))

    conj = [r for r in results if r["klass"] == "conjunction-proves-null"]
    print()
    analyzed = len(units) - len(timeouts) - len(broken_tus)
    print(
        f"DENOMINATOR: {len(units)} TUs in universe, {analyzed} analyzed, "
        f"{len(timeouts)} timed out, {len(broken_tus)} failed to compile"
    )
    for f, e in broken_tus[:20]:
        print(f"  FAILED-TO-COMPILE {f}: {e}")
    print(f"REPORTS: {len(results)} unique")
    print(f"  conjunction-proves-null : {len(conj)}")
    print(f"  other-null              : {sum(1 for r in results if r['klass'] == 'other-null')}")
    print(f"  not-a-deref             : {sum(1 for r in results if r['klass'] == 'not-a-deref')}")
    print(f"  unrelated               : {sum(1 for r in results if r['klass'] == 'unrelated')}")
    if timeouts:
        print("TIMED OUT: " + ", ".join(timeouts))
    print()
    for r in conj:
        print(f"{os.path.relpath(r['path'], REPO)}:{r['line']}: {r['message']}")
        for t in r["trail"][-6:]:
            print(f"      | {t}")

    if args.json_out:
        with open(args.json_out, "w") as f:
            json.dump(
                {"units": len(units), "timeouts": timeouts, "failed_to_compile": broken_tus, "reports": results}, f, indent=2
            )
    return 0


if __name__ == "__main__":
    sys.exit(main())
