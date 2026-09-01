#!/usr/bin/env python3
"""Detect a SYSTEM TOOLCHAIN that moved underneath a configured native build dir.

WHY THIS EXISTS
---------------
On 2026-09-01 04:46 a routine `pacman -Syu` on this box upgraded, in one
transaction, gtest 1.17.0 -> 1.18.0, ffmpeg 8.1.2 -> 9.0.1, glfw, and
nvidia-utils 610.43.03 -> 610.57.04.  Every configured `native/build` in every
checkout became unbuildable, and *nothing in the build system could see it*:

  * `native/build/build.ninja` had `/usr/lib/libgtest.so.1.17.0` baked in as an
    explicit link input.  That file no longer exists.
  * `native/build/CMakeCache.txt` still recorded
    `FIND_PACKAGE_MESSAGE_DETAILS_GTest ... [v1.17.0()]`.
  * `main`'s `milo-tests` binary had SIX unresolvable `DT_NEEDED` entries
    (`libgtest.so.1.17.0`, `libgtest_main.so.1.17.0`, `libavformat.so.62`,
    `libavcodec.so.62`, `libswscale.so.9`, `libavutil.so.60`) — it could not be
    exec'd at all.

CMake had declared the dependency correctly: `/usr/lib/cmake/GTest/GTestConfig.cmake`
IS an input of the `build.ninja` regeneration edge.  Ninja still refused to
re-run cmake, because **pacman restores the upstream tarball's mtimes**, so the
NEWER gtest 1.18 files landed with an mtime of 2026-08-30 21:08 — OLDER than the
`build.ninja` written 2026-08-31 17:05.  Ninja's entire staleness model is
"input newer than output"; a package manager that moves mtimes BACKWARDS defeats
it silently and completely.

That is the general hazard, and it has a nastier form than the one that bit us.
The loud form is a *deleted* soname: ninja hard-errors with "missing and no
known rule to make it".  The quiet form is a library whose path is unchanged but
whose CONTENT and HEADERS moved (an in-place ABI bump): ninja recompiles
nothing, because the headers' mtimes also went backwards, and you link
last-month's object files against this-month's shared library.  Nothing fails
until something behaves oddly at runtime.

So this check does not look at mtimes at all.  It records the CONTENT HASH of
every external file the build dir depends on, and compares.

WHAT IT MEASURES
----------------
Every absolute path outside the repo and outside the build dir that appears in
`build.ninja` and looks like a cmake module or a library (65 files, ~29 MB on
this box — hashing them all costs ~0.1 s).  Two classes, because the right
remedy differs:

  library   .so / .a / .so.N  — a change here can be an ABI change, so already
                               compiled object files are suspect and the
                               remedy is reconfigure + CLEAN rebuild.
  cmake     .cmake            — a change here alters generated build rules, so
                               the remedy is reconfigure only.

EXIT CODES (this script's own space; native_test.sh maps them onto its own)
  0  current — every recorded external input still exists with the same content
  1  usage / internal error
  2  MOVED    — at least one external input is MISSING.  The build dir cannot
                link, or the binaries it already produced cannot be exec'd.
  3  DRIFTED  — everything still resolves, but content changed under at least
                one path.
  4  NO FINGERPRINT — this build dir predates the check (or was configured by
                bare cmake).  Not a failure; record one and move on.

Usage:
  native_toolchain_check.py --record <build-dir>   # write the fingerprint
  native_toolchain_check.py --check  <build-dir>   # compare (default)
  native_toolchain_check.py --check  <build-dir> --quiet
"""

import argparse
import hashlib
import os
import re
import subprocess
import sys

FINGERPRINT_NAME = "toolchain_fingerprint.txt"

# A path is "external" if it is absolute, outside the repo, outside the build
# dir, and looks like something whose content can change the build.  Deliberately
# NOT every absolute path in build.ninja: that file also names the compiler's
# own driver, /usr/bin/cmake, and a pile of one-off probe outputs, none of which
# we want to churn a clean rebuild over.
_LIB_RE = re.compile(r"\.(so|a)(\.[0-9][0-9.]*)?$")
_CMAKE_RE = re.compile(r"\.cmake$")


def classify(path):
    if _LIB_RE.search(path):
        return "library"
    if _CMAKE_RE.search(path):
        return "cmake"
    return None


def external_inputs(build_dir, repo_root):
    """Absolute external paths referenced by build.ninja, classified."""
    ninja = os.path.join(build_dir, "build.ninja")
    if not os.path.isfile(ninja):
        return None
    with open(ninja, encoding="utf-8", errors="replace") as fh:
        text = fh.read()

    found = {}
    # Ninja escapes ':' and ' ' with '$'; splitting on whitespace/':' and
    # trimming a trailing '$' is sufficient for the absolute paths we want.
    for tok in re.findall(r"(?<![\w/.-])/[^\s:|]+", text):
        tok = tok.rstrip("$")
        if tok.startswith(repo_root + os.sep) or tok == repo_root:
            continue
        if tok.startswith(build_dir + os.sep) or tok == build_dir:
            continue
        kind = classify(tok)
        if kind is None:
            continue
        found[tok] = kind
    return found


def digest(path):
    """sha256 of the file's CONTENT, following symlinks. Never an mtime."""
    h = hashlib.sha256()
    try:
        with open(path, "rb") as fh:
            for chunk in iter(lambda: fh.read(1 << 20), b""):
                h.update(chunk)
    except OSError:
        return None
    return h.hexdigest()


def compute(build_dir, repo_root):
    inputs = external_inputs(build_dir, repo_root)
    if inputs is None:
        return None
    rows = []
    for path in sorted(inputs):
        rows.append((inputs[path], path, digest(path)))
    return rows


def unresolved_needed(build_dir):
    """DT_NEEDED entries of the built required targets that ld.so cannot find.

    This is the check that would have said, in one line, what took an hour on
    2026-09-01: main's milo-tests simply could not start.  It is independent of
    build.ninja — it interrogates the artifact itself — so it still fires when
    the build dir has been reconfigured but not rebuilt.
    """
    targets_file = os.path.join(build_dir, "milo_test_required_targets.txt")
    if not os.path.isfile(targets_file):
        return {}
    with open(targets_file, encoding="utf-8") as fh:
        targets = fh.read().split()
    out = {}
    for t in targets:
        binpath = os.path.join(build_dir, t)
        if not os.path.isfile(binpath):
            continue
        try:
            res = subprocess.run(
                ["ldd", binpath], capture_output=True, text=True, timeout=60
            )
        except (OSError, subprocess.SubprocessError):
            continue
        missing = [
            ln.split("=>")[0].strip()
            for ln in res.stdout.splitlines()
            if "not found" in ln
        ]
        if missing:
            out[t] = missing
    return out


def write_fingerprint(build_dir, rows):
    path = os.path.join(build_dir, FINGERPRINT_NAME)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(
            "# native toolchain fingerprint -- scripts/native_toolchain_check.py\n"
            "# CONTENT hashes, never mtimes: pacman restores upstream mtimes, so a\n"
            "# newer package can install OLDER files and defeat ninja entirely.\n"
        )
        for kind, p, d in rows:
            fh.write("%s %s %s\n" % (kind, d or "MISSING", p))
    return path


def read_fingerprint(build_dir):
    path = os.path.join(build_dir, FINGERPRINT_NAME)
    if not os.path.isfile(path):
        return None
    rows = []
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split(" ", 2)
            if len(parts) != 3:
                continue
            rows.append((parts[0], parts[2], None if parts[1] == "MISSING" else parts[1]))
    return rows


def main():
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("build_dir")
    ap.add_argument("--record", action="store_true")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    build_dir = os.path.abspath(args.build_dir)
    repo_root = os.path.abspath(
        os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    )

    rows = compute(build_dir, repo_root)
    if rows is None:
        print(
            "error: %s has no build.ninja; nothing to fingerprint." % build_dir,
            file=sys.stderr,
        )
        return 1

    if args.record:
        path = write_fingerprint(build_dir, rows)
        if not args.quiet:
            print(
                "toolchain fingerprint recorded: %d external inputs -> %s"
                % (len(rows), path)
            )
        return 0

    old = read_fingerprint(build_dir)
    now = {(k, p): d for k, p, d in rows}

    missing = sorted(p for (k, p), d in now.items() if d is None)
    changed_lib = []
    changed_cmake = []
    if old is not None:
        oldmap = {(k, p): d for k, p, d in old}
        for (kind, p), d in sorted(now.items()):
            prev = oldmap.get((kind, p))
            if prev is None or d is None or d == prev:
                continue
            (changed_lib if kind == "library" else changed_cmake).append(p)

    broken = unresolved_needed(build_dir)

    if missing or broken:
        if not args.quiet:
            print("TOOLCHAIN MOVED")
            for p in missing:
                print("  missing external input   : %s" % p)
            for t, libs in sorted(broken.items()):
                for lib in libs:
                    print("  %-22s cannot load: %s" % (t, lib))
            print("REMEDY: clean-rebuild")
        return 2

    if old is None:
        if not args.quiet:
            print("NO FINGERPRINT (build dir predates this check)")
        return 4

    if changed_lib or changed_cmake:
        if not args.quiet:
            print("TOOLCHAIN DRIFTED")
            for p in changed_lib:
                print("  library content changed : %s" % p)
            for p in changed_cmake:
                print("  cmake module changed    : %s" % p)
            # A library that changed under an unchanged path is the QUIET case:
            # same soname, new ABI, and ninja will not recompile one object
            # file because the headers' mtimes moved backwards too. Only a
            # clean rebuild is honest. A cmake module change alters generated
            # rules, not object code, so reconfiguring is enough.
            print("REMEDY: %s" % ("clean-rebuild" if changed_lib else "reconfigure"))
        return 3

    if not args.quiet:
        print("toolchain current (%d external inputs unchanged)" % len(rows))
    return 0


if __name__ == "__main__":
    sys.exit(main())
