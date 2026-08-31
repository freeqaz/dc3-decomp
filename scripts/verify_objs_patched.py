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

`--check-compile-edge` (same edge, before `--check`)
    Asserts that every MSVC compile rule in `build.ninja` still carries the
    per-object build-metadata normalisation.  That pass is the one thing in the
    chain that runs INSIDE the compile edge rather than after it, because
    `ninja <one>.obj` reaches no post-compile edge and MSVC stamps the wall
    clock into every object it writes -- so without it a per-target rebuild of
    unchanged source yields a different hash every time, and any control of the
    shape "I edited something, the bytes moved, therefore it matters" passes
    regardless.  Nothing else in this repo would notice the wiring being
    dropped: a full build would still end up normalised (the batch pass
    follows), `--check` would still pass, and only the per-target path would
    quietly go back to measuring the clock.

The manifest is content-keyed on purpose: `scripts/obj_patch_io.py` preserves
each object's mtime across the in-place rewrite (see its docstring for why),
so the patch state of this tree is NOT visible in any timestamp.

An EMPTY object tree is a refusal, not a pass
---------------------------------------------
Measured 2026-08-22 on a scratch tree with `build/373307D9/{src,obj}` present
and empty:

    verify_objs_patched.py --check            -> exit 0   (silent)
    verify_objs_patched.py --emit             -> "0 objects verified patched,
                                                  tree_sha256=e3b0c44298fc1c14"
    verify_objs_patched.py --verify-manifest  -> "OK: 0 objects match"

`e3b0c442...` is the SHA-256 of the empty string.  Every one of the five
patchers guards `src_dir.exists()` and then iterates a glob, so an existing but
unpopulated directory yields "0 pending patches" -- which is the same value as
"this tree is a perfect fixed point."  The claim "the tree is fully patched"
was true of a tree with nothing in it, and `patch_guard.ensure_patched_tree`
(which is what stands between an agent and a measurement) accepted it.

That matters because the downstream reading is not "no objects" but "0.0% on
every function", which is indistinguishable from an unstarted unit -- the false
`exhausted` shape.  So `--check`, `--emit` and `--verify-manifest` now all
REFUSE an empty universe (exit 3) and all three PRINT THEIR DENOMINATOR on
success.  A count with no denominator cannot be audited after the fact.
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
    # Not a name/storage-class rewrite like the five above -- it zeroes MSVC's
    # clock-derived COFF TimeDateStamp and CodeView S_OBJNAME signature.  It is
    # in this list because it is in the same chain and has the same failure
    # mode: a tree that skipped it looks finished and is not byte-reproducible,
    # which silently degrades every byte-identity control run over it (#150).
    "obj_build_metadata_patcher.py",
]

MANIFEST_VERSION = 1

#: Exit code for "this tree has no objects to vouch for".  Distinct from 1
#: (pending patches / drift) and 2 (no manifest) so a caller can tell "the
#: instrument had nothing to measure" from "the instrument measured a problem".
EXIT_EMPTY_UNIVERSE = 3


#: The MSVC compile rules `tools/project.py` emits.  Each must carry the
#: per-object normalisation, or `ninja <one>.obj` goes back to being
#: unmeasurable.  Named explicitly rather than "every rule matching msvc*" so
#: that ADDING a compile rule without wiring it is a failure, not a silent
#: widening of the exemption.
COMPILE_RULES = ("msvc", "msvc_pch", "msvc_pch_create")

#: What each of those commands must invoke.
COMPILE_EDGE_PASS = "obj_build_metadata_patcher.py"

#: Exit code for "the compile edges no longer carry the per-object pass".
#: Distinct from 1 (tree drift) because the tree can be perfectly patched while
#: this is broken -- the batch pass still runs on a full build.
EXIT_COMPILE_EDGE = 4


class EmptyObjectTreeError(RuntimeError):
    """`build/<version>/src` holds no objects, so nothing can be vouched for."""


class CompileEdgeUnwiredError(RuntimeError):
    """A `build.ninja` MSVC rule lost the per-object build-metadata pass."""


def ninja_rule_commands(text: str) -> dict:
    """`{rule_name: command}` for every `rule` block in a build.ninja.

    Ninja continues a line with a trailing `$`, and every rule body line is
    indented; both are handled here rather than by a regex over the raw text,
    because the commands this checks are long enough to always be wrapped.
    """
    # Join ninja's `$`-continuations first, so a `command =` value that spans
    # eight physical lines is one logical line.
    logical, buf = [], ""
    for raw in text.splitlines():
        if raw.endswith("$") and not raw.endswith("$$"):
            buf += raw[:-1]
            continue
        logical.append(buf + raw)
        buf = ""
    if buf:
        logical.append(buf)

    out, current = {}, None
    for line in logical:
        if line.startswith("rule "):
            current = line[5:].strip()
            continue
        if not line[:1].isspace():
            if line.strip():
                current = None
            continue
        if current is None:
            continue
        stripped = line.strip()
        if stripped.startswith("command"):
            key, _, value = stripped.partition("=")
            if key.strip() == "command":
                out[current] = " ".join(value.split())
    return out


def check_compile_edge(repo: Path, quiet: bool = False) -> int:
    """Assert every MSVC compile rule still runs the per-object pass."""
    ninja_file = repo / "build.ninja"
    if not ninja_file.exists():
        print(f"REFUSE: {ninja_file} is absent, so nothing can be said about "
              f"the compile edges. Run `python3 configure.py` first. This is a "
              f"refusal and not a pass: an absent build.ninja contains zero "
              f"unwired rules, which is the same count a correctly wired one "
              f"reports.", file=sys.stderr)
        return EXIT_COMPILE_EDGE
    commands = ninja_rule_commands(ninja_file.read_text())
    missing_rules = [r for r in COMPILE_RULES if r not in commands]
    unwired = [r for r in COMPILE_RULES
               if r in commands and COMPILE_EDGE_PASS not in commands[r]]
    if not missing_rules and not unwired:
        if not quiet:
            print(f"[patch-state] compile edges wired: {len(COMPILE_RULES)} of "
                  f"{len(COMPILE_RULES)} MSVC rules in build.ninja run "
                  f"{COMPILE_EDGE_PASS} on their own output")
        return 0
    print("=" * 72, file=sys.stderr)
    print("MSVC COMPILE EDGES NO LONGER NORMALIZE BUILD METADATA", file=sys.stderr)
    print("=" * 72, file=sys.stderr)
    if missing_rules:
        print(f"  rules absent from build.ninja entirely: "
              f"{', '.join(missing_rules)}", file=sys.stderr)
    if unwired:
        print(f"  rules that do not run {COMPILE_EDGE_PASS}: "
              f"{', '.join(unwired)}", file=sys.stderr)
    print("\nMSVC stamps the wall clock into every object it writes (COFF "
          "TimeDateStamp, CodeView S_OBJNAME signature). The post-compile "
          "batch pass zeroes them, but it hangs off `all_source` and a "
          "targeted `ninja build/.../Foo.obj` reaches none of it. Without the "
          "per-object pass in the compile rule, rebuilding ONE object from "
          "UNCHANGED source produces a DIFFERENT hash every time -- so any "
          "control shaped 'I edited something, the object hash moved, "
          "therefore the mechanism works' passes whether or not it does. "
          "Measured on TypeProps.obj: dfeda314... then e14ac6e8... .\n"
          "Fix: restore `config.obj_postprocess_cmd` in configure.py and "
          "re-run `python3 configure.py`.", file=sys.stderr)
    return EXIT_COMPILE_EDGE


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


def require_non_empty(repo: Path, mode: str) -> list[Path]:
    """Return the objects, or REFUSE if there are none.

    The whole file is a statement about a population.  Making that population's
    size an explicit precondition is the difference between "measured and
    clean" and "could not measure", which every one of these three modes used
    to collapse into exit 0.
    """
    objs = objects(repo)
    if objs:
        return objs
    d = src_dir(repo)
    raise EmptyObjectTreeError(
        f"REFUSING TO VOUCH FOR {repo} ({mode}): {d} contains NO .obj files "
        f"({'the directory does not exist' if not d.exists() else 'the directory is empty'}).\n\n"
        f"Every post-compile patcher reports '0 pending' over an empty glob, "
        f"and 0 pending is the same number a perfectly patched tree reports. "
        f"A pass here would assert that a tree with nothing in it is a fixed "
        f"point of five passes that never ran -- and a diff taken against it "
        f"scores 0.0% on every function, which reads as an unstarted unit "
        f"rather than as an error.\n\n"
        f"Run a full `ninja` in that directory first. If DC3_VERSION is set "
        f"(currently {os.environ.get('DC3_VERSION', '<unset>')!r}), note that "
        f"the five obj_*_patcher.py scripts HARDCODE 373307D9 while this "
        f"script honours the variable, so the two halves would describe "
        f"different trees."
    )


def run_check(repo: Path) -> int:
    """Dry-run every patcher; non-zero if the tree is not a fixed point."""
    objs = require_non_empty(repo, "--check")
    failures = []
    for script in PATCHERS:
        p = subprocess.run(
            [sys.executable, str(repo / "scripts" / script), "--batch", "--check"],
            cwd=str(repo), capture_output=True, text=True)
        if p.returncode != 0:
            failures.append((script, p.returncode,
                             (p.stderr or p.stdout).strip().splitlines()[-3:]))
    if not failures:
        # State the denominator. "0 pending" is only meaningful next to the
        # number of objects it was 0 out of.
        print(f"[patch-state] fixed point: {len(PATCHERS)} passes have no "
              f"pending work over {len(objs)} objects in "
              f"{src_dir(repo).relative_to(repo)}")
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
    objs = require_non_empty(repo, "--emit")
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
    if not recorded:
        print(f"REFUSE: {mpath} vouches for ZERO objects (n_objects="
              f"{doc.get('n_objects')}, tree_sha256="
              f"{str(doc.get('tree_sha256'))[:16]} -- e3b0c442... is the "
              f"sha256 of the empty string). A manifest of nothing matches a "
              f"tree of nothing, and this function would have printed "
              f"'OK: 0 objects match'. Run a full `ninja` and let the "
              f"post-compile edge re-emit it.", file=sys.stderr)
        return EXIT_EMPTY_UNIVERSE
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
    ap.add_argument("--check-compile-edge", action="store_true",
                    help="fail if build.ninja's MSVC rules no longer run the "
                         "per-object build-metadata pass")
    ap.add_argument("--emit", action="store_true",
                    help="write build/<version>/patch_state.json")
    ap.add_argument("--verify-manifest", action="store_true",
                    help="recompute patch_state.json and fail on any drift")
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args()
    repo = Path(a.repo).resolve()
    if not (a.check or a.emit or a.verify_manifest or a.check_compile_edge):
        a.check = a.emit = True
    rc = 0
    if a.check_compile_edge:
        rc = check_compile_edge(repo, quiet=a.quiet)
        if rc:
            return rc
    try:
        if a.check:
            rc = run_check(repo)
            if rc:
                return rc
        if a.emit:
            rc = emit(repo)
            if rc:
                return rc
    except EmptyObjectTreeError as exc:
        print(f"[patch-state] {exc}", file=sys.stderr)
        return EXIT_EMPTY_UNIVERSE
    if a.verify_manifest:
        rc = verify_manifest(repo, quiet=a.quiet)
    return rc


if __name__ == "__main__":
    sys.exit(main())
