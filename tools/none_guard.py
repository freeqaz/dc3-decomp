#!/usr/bin/env python3
"""Keep an automated source fix from buying relocation names with real bytes.

Every fixer in this lane repairs a symbol NAME, and the claim each of them
makes is that no instruction moves.  That claim is checkable: build, and read
`functionRelocDiffs=none` -- the ruler that ignores relocation names entirely.
If it moved, the edit changed codegen and is not the fix it looked like.

This is the harness for that, usable on its own or from a fixer:

    none_guard.py --baseline out.json                 # before editing
    none_guard.py --check out.json --revert           # after editing

`--revert` bisects by consequence rather than by halves: it reverts the source
file behind every unit that LOST a complete function, rebuilds, and repeats
until `none` is whole again.  That converges in a couple of builds because the
report already names the casualties, and it keeps the part of the wave that was
genuinely free instead of throwing the whole thing away.

It is a guard, not a proof of correctness: a `none` that does not move says the
bytes are unchanged, which is exactly the claim -- nothing more.
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

RULER = ["-c", "functionRelocDiffs=none"]


def build(repo):
    r = subprocess.run(["ninja"], cwd=repo, capture_output=True, text=True)
    if r.returncode:
        sys.exit("BUILD FAILED:\n" + (r.stdout + r.stderr)[-3000:])


def report(repo, out):
    subprocess.run(["objdiff-cli", "report", "generate", "-p", ".", *RULER,
                    "-o", str(out)], cwd=repo, check=True,
                   capture_output=True, text=True)
    d = json.loads(Path(repo, out).read_text() if not Path(out).is_absolute()
                   else Path(out).read_text())
    complete = {(u["name"], f["name"]) for u in d["units"]
                for f in u.get("functions", [])
                if f.get("fuzzy_match_percent") == 100.0}
    return d["measures"], complete


def source_of(repo, units):
    idx = {u["name"]: u.get("metadata", {}).get("source_path")
           for u in json.loads(Path(repo, "objdiff.json").read_text())["units"]}
    return {idx[u] for u in units if idx.get(u)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=".")
    ap.add_argument("--baseline", help="write the pre-edit `none` report here")
    ap.add_argument("--check", help="compare against this pre-edit report")
    ap.add_argument("--revert", action="store_true",
                    help="git checkout the sources behind any regressed unit "
                         "and repeat until `none` is restored")
    ap.add_argument("--max-rounds", type=int, default=4)
    args = ap.parse_args()
    repo = Path(args.repo).resolve()

    if args.baseline:
        build(repo)
        m, _ = report(repo, Path(args.baseline).resolve())
        print(f"baseline none: {m['matched_code_percent']:.6f}% "
              f"({m['matched_code']} bytes)")
        return 0

    base = json.loads(Path(args.check).read_text())
    before = {(u["name"], f["name"]) for u in base["units"]
              for f in u.get("functions", [])
              if f.get("fuzzy_match_percent") == 100.0}
    bm = base["measures"]

    tmp = Path(args.check).with_suffix(".now.json")
    for rnd in range(args.max_rounds):
        build(repo)
        m, now = report(repo, tmp)
        lost = before - now
        print(f"round {rnd}: none {bm['matched_code_percent']:.6f}% -> "
              f"{m['matched_code_percent']:.6f}%, {len(lost)} function(s) lost")
        if not lost:
            print("none is intact -- the edits moved no instruction")
            return 0
        if not args.revert:
            for u, f in sorted(lost)[:20]:
                print(f"   LOST  {u}  {f}")
            return 1
        srcs = source_of(repo, {u for u, _ in lost})
        if not srcs:
            sys.exit("regressed units have no source path -- cannot revert")
        print("   reverting: " + ", ".join(sorted(srcs)))
        subprocess.run(["git", "checkout", "--", *sorted(srcs)],
                       cwd=repo, check=True)
    sys.exit(f"still regressed after {args.max_rounds} rounds")


if __name__ == "__main__":
    sys.exit(main())
