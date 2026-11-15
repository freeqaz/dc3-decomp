#!/usr/bin/env python3
"""List every `delete` in the native build that goes through a class with a
destructor that is not virtual.

The native build compiles with -w, so clang never says this. It matters on a
Mac: when the class is abstract and its destructor is user-written, Apple clang
compiles the delete as a trap (the fitness screen crash of 2026-11-14,
QueueableCommand). Linux clang frees the block and runs no derived destructor,
so the Linux gate does not see it.

Each compile command in native/build/compile_commands.json is re-run as
-fsyntax-only with only these two warnings on:
    -Wdelete-abstract-non-virtual-dtor      (the class that can trap)
    -Wdelete-non-abstract-non-virtual-dtor  (frees with the base's size)
The precompiled header is dropped and its header included as text, so nothing
in the build directory is read or written besides compile_commands.json.

    scripts/native/delete_nonvirtual_sweep.py            # report, exit 0
    scripts/native/delete_nonvirtual_sweep.py --check    # exit 1 on a site not in --allow
    scripts/native/delete_nonvirtual_sweep.py --selftest # the check can see one

A class whose destructor is trivial (GameInput: the image frees it with
MemFree and runs nothing) does not trap; such classes go in ALLOWED with the
reason. Exit 2: the sweep could not examine every file (not a clean result).
"""
import argparse
import concurrent.futures
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile

WARNINGS = ["-Wdelete-abstract-non-virtual-dtor", "-Wdelete-non-abstract-non-virtual-dtor"]

# class -> why its non-virtual delete is left as the image has it
ALLOWED = {
    "GameInput": "trivial destructor: the image frees the block with MemFree "
                 "(Game::~Game, 0x82867A48) and no destructor call is emitted",
}

SITE = re.compile(r"^(?P<file>[^:\n]+):(?P<line>\d+):\d+: warning: delete called on "
                  r"(?:non-final )?'(?P<cls>[^']+)' that (?P<kind>is abstract|has virtual functions) "
                  r"but (?:has )?non-virtual destructor", re.M)


def syntax_only_command(entry):
    args = shlex.split(entry["command"]) if "command" in entry else list(entry["arguments"])
    kept, i = [], 0
    while i < len(args):
        a = args[i]
        if a == "-Xclang" and i + 3 < len(args) and args[i + 1] == "-include-pch":
            i += 4
            continue
        if a in ("-o", "-MF", "-MT", "-MQ"):
            i += 2
            continue
        if a in ("-w", "-c", "-MD", "-MMD"):
            i += 1
            continue
        kept.append(a)
        i += 1
    return kept + ["-fsyntax-only", "-Wno-everything", "-fno-color-diagnostics"] + WARNINGS


def run_one(entry):
    try:
        p = subprocess.run(syntax_only_command(entry), cwd=entry["directory"],
                           capture_output=True, text=True, errors="replace", timeout=900)
    except subprocess.TimeoutExpired:
        return entry["file"], None, "timed out"
    return entry["file"], p.returncode, p.stderr


def sweep(entries, jobs):
    sites, failed = set(), []
    with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
        for path, rc, err in pool.map(run_one, entries):
            if rc != 0:
                failed.append((path, (err or "").strip().splitlines()[:3]))
                continue
            for m in SITE.finditer(err):
                sites.add((os.path.normpath(m["file"]), int(m["line"]), m["cls"],
                           "abstract" if m["kind"] == "is abstract" else "concrete"))
    return sites, failed


def unique_cxx_entries(compile_commands):
    seen = {}
    for e in json.load(open(compile_commands)):
        if e["file"].endswith((".c", ".m")):
            continue
        seen.setdefault(e["file"], e)
    return list(seen.values())


def selftest(entries, jobs):
    """Compile a file holding one delete of each kind with a real command's
    flags; the sweep must report exactly those two."""
    with tempfile.TemporaryDirectory() as d:
        src = os.path.join(d, "probe.cpp")
        with open(src, "w") as f:
            f.write("struct SweepAbstract { virtual int F() = 0; ~SweepAbstract() {} };\n"
                    "struct SweepConcrete { virtual int F() { return 0; } ~SweepConcrete() {} };\n"
                    "void A(SweepAbstract *p) { delete p; }\n"
                    "void C(SweepConcrete *p) { delete p; }\n")
        probe = dict(entries[0])
        cmd = syntax_only_command(probe)
        probe["arguments"] = [src if a == probe["file"] else a for a in cmd]
        probe.pop("command", None)
        probe["file"] = src
        sites, failed = sweep([probe], jobs)
    got = sorted((cls, kind) for _f, _l, cls, kind in sites)
    want = [("SweepAbstract", "abstract"), ("SweepConcrete", "concrete")]
    if failed or got != want:
        print("SELFTEST FAILED: wanted %s, got %s, failed %s" % (want, got, failed))
        return 1
    print("selftest ok: the sweep reports a delete of each kind")
    return 0


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.dirname(os.path.dirname(here))
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--compile-commands", default=os.path.join(repo, "native", "build", "compile_commands.json"))
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 4) // 2))
    ap.add_argument("--check", action="store_true", help="exit 1 if a class outside ALLOWED is deleted this way")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if not os.path.exists(args.compile_commands):
        print("no %s: configure the native build first (scripts/native_configure.sh)" % args.compile_commands)
        return 2
    entries = unique_cxx_entries(args.compile_commands)
    if not entries:
        print("no C++ compile commands in %s" % args.compile_commands)
        return 2
    if args.selftest:
        return selftest(entries, args.jobs)

    sites, failed = sweep(entries, args.jobs)
    print("examined %d of %d source files; %d could not be compiled"
          % (len(entries) - len(failed), len(entries), len(failed)))
    for path, lines in failed[:10]:
        print("  NOT EXAMINED %s: %s" % (path, " | ".join(lines)))
    by_class = {}
    for path, line, cls, kind in sorted(sites):
        by_class.setdefault((cls, kind), []).append("%s:%d" % (os.path.relpath(path, repo), line))
    new = 0
    for (cls, kind), where in sorted(by_class.items()):
        allowed = ALLOWED.get(cls)
        new += allowed is None
        print("%s %s class %s, %d delete%s%s" % ("allowed" if allowed else "UNSAFE ", kind, cls,
                                                 len(where), "" if len(where) == 1 else "s",
                                                 (": " + allowed) if allowed else ""))
        for w in where:
            print("    " + w)
    if not by_class:
        print("no delete through a non-virtual destructor")
    if failed:
        return 2
    return 1 if (args.check and new) else 0


if __name__ == "__main__":
    sys.exit(main())
