#!/usr/bin/env python3
"""check_link_duplicates -- fail when a strong duplicate definition returns.

Why
---
Every Linux native target links with `-Wl,--allow-multiple-definition`, so two
strong definitions of one symbol produce no diagnostic: GNU ld silently keeps
whichever it saw first. Apple's ld has no such mode for the default linker (the
build leans on `-ld_classic -multiply_defined,suppress`, which is deprecated),
so every duplicate that Linux tolerates is a macOS link error -- and on Linux
it is a latent "the wrong body won" bug whenever link order changes.

On 2026-11-13 relinking dc3-native without the flag reported 23 duplicates
(docs/native/MACOS.md item 4). The dc3-side ones are removed; the ones whose
loser lives in ../milo-native-engine wait on an engine change and are listed,
with that reason, in native/tests/link_duplicates_allow.txt.

What it does
------------
1. Reads the target's REAL link command from ninja (`ninja -t commands`), not a
   reconstruction.
2. Re-runs it with `--allow-multiple-definition` removed, writing the output to
   a temporary file (the build's own binary is never touched).
3. Collects every `multiple definition of` (GNU ld) / `duplicate symbol` (lld)
   diagnostic and compares the set with the allowlist.

Exit status
-----------
0  the duplicate set equals the allowlist
1  a duplicate that is NOT allowlisted (the gate failing: one came back)
3  an allowlisted duplicate is gone -- remove it from the allowlist (ratchet,
   so a fixed duplicate stays fixed)
4  the relink failed for a reason other than duplicates (undefined symbol,
   missing object: build the target first); its stderr is printed
2  usage / environment error

--selftest is the negative control: it compiles a one-line object that
re-defines a symbol dc3-native already defines strongly, appends it to the
same link line, and REQUIRES the check to report that symbol. A checker that
cannot see an injected duplicate exits 5 ("vacuous") instead of 0.
"""

from __future__ import annotations

import argparse
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

ALLOW_FLAG = "-Wl,--allow-multiple-definition"
GNU_RE = re.compile(r"multiple definition of [`'‘](.+?)['’]")
LLD_RE = re.compile(r"duplicate symbol: (.+)$")
# A symbol dc3-native defines strongly exactly once (src/App.cpp); the selftest
# defines it a second time.
SELFTEST_SYMBOL = "gDrawJmpBufSet"


def link_command(build_dir: str, ninja: str, target: str) -> list[str]:
    out = subprocess.run([ninja, "-C", build_dir, "-t", "commands", target],
                         capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit(f"error: ninja -t commands {target} failed:\n{out.stderr}")
    lines = [l for l in out.stdout.splitlines() if l.strip()]
    if not lines:
        sys.exit(f"error: ninja knows no commands for {target}")
    cmd = lines[-1].strip()
    # CMake's Ninja link rule: ": && <link> && :" (POSIX) -- keep the middle.
    if cmd.startswith(": &&"):
        cmd = cmd[len(": &&"):]
    if cmd.endswith("&& :"):
        cmd = cmd[: -len("&& :")]
    if "&&" in cmd:
        sys.exit(f"error: unexpected compound link command for {target}:\n{cmd[:400]}")
    argv = shlex.split(cmd)
    if not any("-o" == a for a in argv):
        sys.exit("error: link command has no -o; not a link line?")
    return argv


def relink(argv: list[str], build_dir: str, out_path: str,
           extra_inputs: list[str]) -> tuple[int, str]:
    new: list[str] = []
    i = 0
    saw_flag = False
    while i < len(argv):
        a = argv[i]
        if a == ALLOW_FLAG:
            saw_flag = True
            i += 1
            continue
        if a == "-o":
            new += ["-o", out_path]
            i += 2
            continue
        if a == "-Xlinker" and i + 1 < len(argv) and argv[i + 1].startswith("--dependency-file"):
            i += 2
            continue
        new.append(a)
        i += 1
    if not saw_flag:
        print(f"note: the link line has no {ALLOW_FLAG}; checking it as-is")
    # Extra objects go right after the compiler driver's first input, i.e.
    # anywhere before the libraries -- append before the first -l / .a.
    if extra_inputs:
        pos = next((k for k, a in enumerate(new) if a.startswith("-l") or a.endswith(".a")),
                   len(new))
        new[pos:pos] = extra_inputs
    # lld stops after 20 errors by default; GNU ld reports every duplicate.
    if any(a in ("-fuse-ld=lld", "-fuse-ld=ld.lld") for a in new):
        new.append("-Wl,--error-limit=0")
    p = subprocess.run(new, cwd=build_dir, capture_output=True, text=True)
    return p.returncode, p.stderr


def parse_dups(stderr: str) -> set[str]:
    found: set[str] = set()
    for line in stderr.splitlines():
        m = GNU_RE.search(line) or LLD_RE.search(line.strip())
        if m:
            found.add(m.group(1).strip())
    return found


def non_duplicate_errors(stderr: str) -> list[str]:
    errs = []
    for line in stderr.splitlines():
        if GNU_RE.search(line) or LLD_RE.search(line.strip()):
            continue
        if "first defined here" in line or "defined at" in line:
            continue
        if re.search(r"\berror\b|undefined reference|cannot find|No such file", line):
            if "linker command failed" in line or "collect2: error: ld returned" in line:
                continue
            errs.append(line)
    return errs


def load_allow(path: str) -> set[str]:
    allow: set[str] = set()
    if not path:
        return allow
    with open(path) as f:
        for raw in f:
            line = raw.split("#", 1)[0].strip()
            if line:
                allow.add(line)
    return allow


def selftest(args, argv: list[str]) -> int:
    cxx = argv[0]
    tmp = tempfile.mkdtemp(prefix="dupcheck-selftest-")
    src = os.path.join(tmp, "dup.cpp")
    obj = os.path.join(tmp, "dup.o")
    with open(src, "w") as f:
        f.write(f"bool {SELFTEST_SYMBOL} = false;\n")
    c = subprocess.run([cxx, "-c", src, "-o", obj], capture_output=True, text=True)
    if c.returncode != 0:
        print(f"error: selftest could not compile its injected object:\n{c.stderr}")
        return 2
    rc, err = relink(argv, args.build_dir, os.path.join(tmp, "out"), [obj])
    dups = parse_dups(err)
    if SELFTEST_SYMBOL not in dups:
        print(f"SELFTEST FAILED (vacuous): injected a second strong {SELFTEST_SYMBOL} "
              f"and the check did not report it. link rc={rc}, duplicates seen={sorted(dups)}")
        return 5
    print(f"selftest OK: the injected duplicate {SELFTEST_SYMBOL} was reported "
          f"({len(dups)} duplicate(s) seen in total)")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--build-dir", default="native/build")
    ap.add_argument("--target", default="dc3-native")
    ap.add_argument("--ninja", default=shutil.which("ninja") or "ninja")
    ap.add_argument("--allow", default=os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "..",
        "native", "tests", "link_duplicates_allow.txt"))
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if not os.path.isdir(args.build_dir):
        print(f"error: no build dir {args.build_dir}")
        return 2
    argv = link_command(args.build_dir, args.ninja, args.target)
    if args.selftest:
        return selftest(args, argv)

    allow = load_allow(args.allow)
    with tempfile.TemporaryDirectory(prefix="dupcheck-") as tmp:
        rc, err = relink(argv, args.build_dir, os.path.join(tmp, args.target), [])
    dups = parse_dups(err)
    other = non_duplicate_errors(err)
    if other:
        print(f"FAIL (4): relinking {args.target} failed for a reason other than "
              f"duplicate definitions (is the target built?):")
        for l in other[:40]:
            print("   ", l)
        return 4
    if rc != 0 and not dups:
        print(f"FAIL (4): relink exited {rc} with no recognisable diagnostic:\n{err[-4000:]}")
        return 4

    new = sorted(dups - allow)
    gone = sorted(allow - dups)
    print(f"{args.target}: {len(dups)} duplicate strong definition(s) without "
          f"{ALLOW_FLAG}; {len(allow)} allowlisted ({os.path.relpath(args.allow)})")
    for s in sorted(dups & allow):
        print(f"    allowlisted: {s}")
    if new:
        print("FAIL (1): duplicate definition(s) NOT in the allowlist -- remove the")
        print("loser (a stub/fallback) rather than adding it here:")
        for s in new:
            print(f"    NEW: {s}")
        for line in err.splitlines():
            if any(s in line for s in new):
                print("      ", line.strip()[:300])
        return 1
    if gone:
        print("FAIL (3): allowlisted duplicate(s) no longer occur -- delete these lines")
        print("from the allowlist so the fix stays locked in:")
        for s in gone:
            print(f"    GONE: {s}")
        return 3
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
