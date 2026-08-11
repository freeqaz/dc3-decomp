#!/usr/bin/env python3
"""Assert that `build/<version>/src/**/*.obj` is a FIXED POINT of the five
post-compile patchers -- and record a content manifest so a later bypass is
detectable from outside the build.

Why
---
The five patchers rewrite ninja's own outputs in place.  For a year the patch
stamps depended only on `|| all_source` (order-only), so nothing made them
re-run when an object was recompiled: an incremental build -- or the *second*
`ninja` of any pair, because an in-place rewrite invalidates `.ninja_deps` and
forces a recompile -- silently produced a tree of fresh, UNPATCHED objects and
said "no work to do".  Measured 2026-08-09 on dc3 `21f7f331`: 277 of 980
objects reverted, and the ADDR_IDENTITY witness derived from that tree fell
from 60 pairings to 53 with no diagnostic anywhere.

That is the failure class this file exists for: **a build that omits a
post-processing pass and announces nothing.**  A fix that merely works is not
enough -- the degraded state has to be loud.

Two checks, because there are two ways to reach the degraded state
-----------------------------------------------------------------
`--check` (wired into the default build, after the patch stamps)
    Re-runs every patcher in dry-run and FAILS THE BUILD if any of them would
    still change a file.  This is the strongest available statement -- the tree
    is a fixed point of the passes -- and it uses the patchers' own detection
    logic, so it cannot drift from them.  It catches a regression of the
    dependency graph itself, including "someone added a sixth patcher and
    forgot the edge".

`--emit` (same edge, after a passing `--check`)
    Writes `build/<version>/patch_state.json`: sha256 of every object at the
    moment the tree was verified patched.  A `--verify-manifest` run recomputes
    it.  This catches what no build-time check can: a tool or an agent that
    compiles a single TU outside the full graph (a targeted
    `ninja build/.../Foo.obj` does not pull in the post-compile edges at all)
    and leaves one unpatched object behind in a tree that otherwise looks
    finished.  Consumers that read this tree -- notably the decomp-synth
    ADDR_IDENTITY witness -- can check the manifest without a toolchain and
    without parsing an object.

The manifest is content-keyed on purpose: `scripts/obj_patch_io.py` preserves
each object's mtime across the in-place rewrite (see its docstring for why),
so the patch state of this tree is NOT visible in any timestamp.
"""

import argparse
import hashlib
import json
import os
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
VERSION = os.environ.get("DC3_VERSION", "373307D9")

#: In the order `configure.py` chains them.  Each accepts `--batch --check`.
PATCHERS = [
    "obj_anon_ns_patcher.py",
    "obj_dynamic_init_patcher.py",
    "obj_guard_patcher.py",
    "obj_bool_mangle_patcher.py",
    "obj_atexit_scope_patcher.py",
]

MANIFEST_VERSION = 1


def src_dir(repo: Path) -> Path:
    return repo / "build" / VERSION / "src"


def objects(repo: Path):
    return sorted(p for p in src_dir(repo).rglob("*.obj") if p.is_file())


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def run_check(repo: Path) -> int:
    """Dry-run every patcher; non-zero if the tree is not a fixed point."""
    failures = []
    for script in PATCHERS:
        p = subprocess.run(
            [sys.executable, str(repo / "scripts" / script), "--batch", "--check"],
            cwd=str(repo), capture_output=True, text=True)
        if p.returncode != 0:
            failures.append((script, p.returncode,
                             (p.stderr or p.stdout).strip().splitlines()[-3:]))
    if not failures:
        return 0
    print("=" * 72, file=sys.stderr)
    print("BUILD TREE IS NOT FULLY PATCHED", file=sys.stderr)
    print("=" * 72, file=sys.stderr)
    print(f"{len(failures)} of {len(PATCHERS)} post-compile passes still have "
          f"pending work in {src_dir(repo)}.", file=sys.stderr)
    print("These objects were COMPILED but never POST-PROCESSED, so every "
          "symbol name, storage class and relocation they carry describes the "
          "raw compiler output and not the shape this project matches "
          "against.  Anything measured from this tree is wrong.", file=sys.stderr)
    for script, rc, tail in failures:
        print(f"\n  {script} (exit {rc}):", file=sys.stderr)
        for line in tail:
            print(f"    {line}", file=sys.stderr)
    print("\nFix: run a full `ninja` (the patch stamps take `all_source` as a "
          "real input, so they re-run behind any recompile).  If this fires "
          "during a full build, the dependency graph in configure.py has "
          "regressed -- see docs/tools/BUILD_SYSTEM.md.", file=sys.stderr)
    return 1


def emit(repo: Path) -> int:
    objs = objects(repo)
    entries = {}
    for p in objs:
        st = p.stat()
        entries[str(p.relative_to(repo))] = {
            "sha256": sha256(p), "size": st.st_size, "mtime_ns": st.st_mtime_ns}
    tree = hashlib.sha256(
        "".join(f"{k}:{v['sha256']}\n" for k, v in sorted(entries.items()))
        .encode()).hexdigest()
    doc = {
        "manifest_version": MANIFEST_VERSION,
        "build_id": VERSION,
        "generated_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "patchers": PATCHERS,
        "n_objects": len(entries),
        "tree_sha256": tree,
        "objects": entries,
    }
    out = repo / "build" / VERSION / "patch_state.json"
    tmp = out.with_suffix(".json.tmp")
    tmp.write_text(json.dumps(doc, indent=1, sort_keys=True))
    tmp.replace(out)
    print(f"[patch-state] {len(entries)} objects verified patched, "
          f"tree_sha256={tree[:16]} -> {out.relative_to(repo)}")
    return 0


def verify_manifest(repo: Path, quiet: bool = False) -> int:
    """Recompute the manifest and refuse if any object drifted from it.

    This is the check a CONSUMER of the tree runs.  It needs no toolchain, no
    compiler and no COFF parsing -- only the manifest and the objects.
    """
    mpath = repo / "build" / VERSION / "patch_state.json"
    if not mpath.exists():
        print(f"REFUSE: {mpath} is absent -- this build tree has never been "
              f"verified patched.  Run `ninja` in it.", file=sys.stderr)
        return 2
    doc = json.loads(mpath.read_text())
    recorded = doc.get("objects") or {}
    drift, missing, extra = [], [], []
    for rel, ent in sorted(recorded.items()):
        p = repo / rel
        if not p.exists():
            missing.append(rel)
            continue
        st = p.stat()
        if st.st_size != ent["size"] or sha256(p) != ent["sha256"]:
            drift.append(rel)
    have = {str(p.relative_to(repo)) for p in objects(repo)}
    extra = sorted(have - set(recorded))
    if not (drift or missing or extra):
        if not quiet:
            print(f"[patch-state] OK: {len(recorded)} objects match "
                  f"{doc['generated_utc']} (tree_sha256="
                  f"{doc['tree_sha256'][:16]})")
        return 0
    print("=" * 72, file=sys.stderr)
    print("BUILD TREE DRIFTED SINCE IT WAS LAST VERIFIED PATCHED", file=sys.stderr)
    print("=" * 72, file=sys.stderr)
    print(f"manifest written {doc.get('generated_utc')} over "
          f"{doc.get('n_objects')} objects", file=sys.stderr)
    for label, rows in (("content differs", drift), ("now missing", missing),
                        ("not in the manifest", extra)):
        if rows:
            print(f"  {len(rows)} {label}:", file=sys.stderr)
            for r in rows[:10]:
                print(f"    {r}", file=sys.stderr)
            if len(rows) > 10:
                print(f"    ... and {len(rows) - 10} more", file=sys.stderr)
    print("\nAn object that changed without the manifest being rewritten was "
          "produced OUTSIDE the full build graph (a targeted "
          "`ninja build/.../Foo.obj`, or a tool compiling one TU), so the "
          "post-compile patch passes never ran on it.  Re-run a full `ninja` "
          "before measuring anything from this tree.", file=sys.stderr)
    return 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--repo", default=str(REPO), help="repo root (default: this checkout)")
    ap.add_argument("--check", action="store_true",
                    help="dry-run every patcher; fail if the tree is not a fixed point")
    ap.add_argument("--emit", action="store_true",
                    help="write build/<version>/patch_state.json")
    ap.add_argument("--verify-manifest", action="store_true",
                    help="recompute patch_state.json and fail on any drift")
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args()
    repo = Path(a.repo).resolve()
    if not (a.check or a.emit or a.verify_manifest):
        a.check = a.emit = True
    rc = 0
    if a.check:
        rc = run_check(repo)
        if rc:
            return rc
    if a.emit:
        rc = emit(repo)
        if rc:
            return rc
    if a.verify_manifest:
        rc = verify_manifest(repo, quiet=a.quiet)
    return rc


if __name__ == "__main__":
    sys.exit(main())
