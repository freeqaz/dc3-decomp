#!/usr/bin/env python3
"""Run the five post-compile object patchers over ONE object, off the build graph.

Why this exists
---------------
configure.py hangs the patcher chain off the `post-compile` phony, not off the
compile edge (see the block at configure.py:362-490).  So `ninja <one .obj>` --
and equally a harness that replays the compile argv out of `ninja -t commands`,
which is what decomp-synth's permuter does -- produces RAW COMPILER OUTPUT.
That is harmless while the ruler is `functionRelocDiffs=none`, which never reads
a relocation target's name, and it is a false-near-miss factory at `name_check`,
which is what this repo's own objdiff.json declares in its `options` block.
decomp-synth refuses to score in that combination (decomp_synth/patch_state.py)
rather than throw away cracks silently.  This script is the condition that
refusal asks for.

What it is NOT: a replacement for `post-compile`.  It patches the ONE object it
is pointed at, in place, and it does not touch the build tree unless you point
it at the build tree.  The permuter points it at a private candidate object in
a temp dir, which is why `--obj` is separable from `--unit`.

It is also NOT `scripts/create_data_stubs.py`, the first `post-compile` step.
That one MINTS whole `.obj` files for `lbl_*` resolution; it is not a per-object
rewrite and there is nothing about it to run on a candidate.

`--unit` vs `--obj`
-------------------
Three of the five patchers pair our object against the retail TARGET object,
and a fourth (anon_ns) indexes against it.  All four find that target from the
object's path relative to build/<id>/src, by PLAIN RELPATH -- unlike rb3-xenon,
nothing here consults objdiff.json.  A candidate object does not live under
build/<id>/src, so the two are separate arguments:

  --unit  the LOGICAL unit path (relative to --src-dir) -- decides pairing
  --obj   the actual file to read and rewrite (default: <src-dir>/<unit>)

Chain order is configure.py's (and verify_objs_patched.py's PATCHERS list), and
it is load-bearing: all five read-modify-write the same bytes, and the build
serializes them through a stamp chain for exactly that reason.

Cross-object state
------------------
Only obj_anon_ns_patcher has any: a name->hash index over the RETAIL objects in
build/<id>/obj.  It reads NOTHING from the compiled tree -- `process_batch`
walks `decomp_by_relpath` only to iterate, never to build the index -- so a
single-TU recompile provably cannot move it, and per-object patching gives the
same bytes as the full graph.  Proven by control, not by argument:
<decomp-bench>/archive/runs/2026-08-21-dc3-patch-chain/.

That index costs a few seconds to build and the permuter runs this per
candidate, so it is memoized to build/<id>/anon_ns_index.pkl behind a
fingerprint of every input (retail obj sizes+mtimes, AND the patcher scripts'
own sizes+mtimes -- dc3 has been bitten once already by a rewritten patcher
whose stamp did not re-fire, configure.py:482-495).  A stale or unreadable cache
is rebuilt, never trusted; `--no-cache` skips it.

Usage:
    python3 scripts/obj_patch_chain.py --apply --unit system/movie/TexMovie.obj
    python3 scripts/obj_patch_chain.py --apply --unit <rel> --obj /tmp/cand.obj
    python3 scripts/obj_patch_chain.py --apply --pairs-json pairs.json
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
import pickle
import sys
import tempfile
from collections import defaultdict
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPTS_DIR.parent
BUILD_ID = os.environ.get("DC3_VERSION", "373307D9")

#: The five, in configure.py's order.  Kept in step with
#: verify_objs_patched.py's PATCHERS list, which is the build's own statement of
#: what a fully patched tree is a fixed point of.
PATCHER_MODULES = [
    "obj_anon_ns_patcher",
    "obj_dynamic_init_patcher",
    "obj_guard_patcher",
    "obj_bool_mangle_patcher",
    "obj_atexit_scope_patcher",
]
COUNT_KEYS = ["anon_ns", "dynamic_init", "guard", "bool_mangle", "atexit_scope"]

#: Bumped whenever the pickle's shape or the index derivation changes, so a
#: cache written by an older script can never be read as if it were current.
_CACHE_VERSION = 1


def _load(name: str):
    """Import a sibling patcher by filename (they are scripts, not a package)."""
    spec = importlib.util.spec_from_file_location(name, SCRIPTS_DIR / f"{name}.py")
    mod = importlib.util.module_from_spec(spec)
    sys.modules.setdefault(name, mod)  # they `from obj_patch_io import ...`
    spec.loader.exec_module(mod)
    return mod


anon_ns = _load("obj_anon_ns_patcher")
dynamic_init = _load("obj_dynamic_init_patcher")
guard = _load("obj_guard_patcher")
bool_mangle = _load("obj_bool_mangle_patcher")
atexit_scope = _load("obj_atexit_scope_patcher")


# ── obj_anon_ns_patcher's cross-object index ────────────────────────────────


def _fingerprint(obj_dir: Path) -> str:
    """Everything the index reads, cheaply.

    Retail objects by (relpath, size, mtime_ns) -- a re-SPLIT rewrites them and
    moves both.  The compiled tree is deliberately ABSENT, which is the whole
    reason a candidate recompile does not invalidate this: dc3's
    `process_batch` builds `orig_index` and the global index purely from
    `orig_by_relpath`, and touches `decomp_by_relpath` only to iterate.

    The patcher sources are in here too.  A rewritten patcher that kept the same
    inputs would otherwise be served an index derived by the old code -- the
    exact "landed change measures as nothing" failure configure.py records.
    """
    h = hashlib.sha256()
    h.update(f"v{_CACHE_VERSION}\n".encode())
    for root, _dirs, files in sorted(os.walk(obj_dir)):
        for f in sorted(files):
            if not f.endswith(".obj") or f.startswith("auto_"):
                continue
            p = os.path.join(root, f)
            st = os.stat(p)
            h.update(f"{os.path.relpath(p, obj_dir)}\0{st.st_size}\0"
                     f"{st.st_mtime_ns}\n".encode())
    for name in PATCHER_MODULES + ["obj_patch_io", "obj_patch_chain"]:
        try:
            st = (SCRIPTS_DIR / f"{name}.py").stat()
            h.update(f"src\0{name}\0{st.st_size}\0{st.st_mtime_ns}\n".encode())
        except OSError:
            h.update(f"src\0{name}\0missing\n".encode())
    return h.hexdigest()


def _build_anon_index(obj_dir: Path, src_dir: Path) -> dict:
    """Reproduce obj_anon_ns_patcher.process_batch's index build, verbatim.

    Kept structurally parallel to that function on purpose: if the two ever
    disagree the per-object path stops being the full graph's answer, and the
    only defence is that this is a transcription short enough to diff by eye.
    """
    orig_by_relpath, _decomp_by_relpath = anon_ns.build_obj_mappings(obj_dir, src_dir)

    orig_index = {}
    g_templates = defaultdict(set)
    g_tokens = defaultdict(set)
    for relpath, abspath in orig_by_relpath.items():
        templates, tokens, weights = anon_ns.index_object(abspath)
        if not templates:
            continue
        orig_index[relpath] = (templates, tokens, weights)
        for k, v in templates.items():
            g_templates[k] |= v
        for k, v in tokens.items():
            g_tokens[k] |= v

    return {
        "orig_by_relpath": orig_by_relpath,
        "orig_index": orig_index,
        "global_index": (dict(g_templates), dict(g_tokens)),
    }


def load_anon_index(obj_dir: Path, src_dir: Path, cache_path: Path | None) -> dict:
    fp = _fingerprint(obj_dir)
    if cache_path is not None:
        try:
            with open(cache_path, "rb") as fh:
                doc = pickle.load(fh)
            if doc.get("fingerprint") == fp:
                return doc["index"]
        except Exception:
            pass  # corrupt, stale, absent, half-written by a peer -- rebuild
    index = _build_anon_index(obj_dir, src_dir)
    if cache_path is not None:
        # Atomic: several permuter workers race here, and a half-written pickle
        # read by the next one would be a silent wrong index, not a crash.
        try:
            cache_path.parent.mkdir(parents=True, exist_ok=True)
            fd, tmp = tempfile.mkstemp(dir=str(cache_path.parent), suffix=".tmp")
            with os.fdopen(fd, "wb") as fh:
                pickle.dump({"fingerprint": fp, "index": index}, fh,
                            protocol=pickle.HIGHEST_PROTOCOL)
            os.replace(tmp, cache_path)
        except Exception:
            pass  # the cache is an optimisation; never fail the patch over it
    return index


# ── the chain ───────────────────────────────────────────────────────────────


def _patch_anon_ns(obj_path: Path, unit_rel: str, index: dict) -> int:
    """obj_anon_ns_patcher.process_batch's per-file body, for one object.

    Every skip below is one of that loop's, in its order.  `hashless_names` is
    omitted deliberately: it only PRINTS an out-of-reach report and writes
    nothing.
    """
    data = obj_path.read_bytes()
    if not anon_ns.ANON_NS_PATTERN.search(data):
        return 0
    if unit_rel not in index["orig_by_relpath"]:
        return 0
    if unit_rel not in index["orig_index"]:
        return 0
    edits, _stats, unresolved = anon_ns.plan_object(
        data, index["orig_index"][unit_rel], index["global_index"])
    if unresolved:
        return 0
    changed = {o: h for o, h in edits.items() if data[o:o + 8] != h}
    if not changed:
        return 0
    anon_ns.write_patched_obj(str(obj_path), anon_ns.apply_edits(data, edits))
    return len(changed)


def patch_one(obj_path: Path, unit_rel: str, obj_dir: Path,
              index: dict, verbose: bool = False) -> dict:
    """Run all five patchers, in configure.py's order, on one object.

    Every pairing rule below is the one that patcher's own batch pass uses --
    plain relpath into build/<id>/obj, skipping the unit when the target is not
    there.  Do not "improve" that here: the point is to be the same answer as
    the build graph, including where the build graph does nothing.
    """
    counts = {}
    counts["anon_ns"] = _patch_anon_ns(obj_path, unit_rel, index)

    counts["dynamic_init"] = len(dynamic_init.patch_obj(str(obj_path), apply=True))

    orig = obj_dir / unit_rel
    if orig.exists():
        counts["guard"] = guard.patch_obj_file(
            str(obj_path), str(orig), apply=True)[0]
        counts["bool_mangle"] = bool_mangle.patch_obj_file(
            str(obj_path), str(orig), apply=True)[0]
        try:
            # NOTE the argument order: `target` is RETAIL, `base` is ours, and
            # the rewrite lands on `base`.  Reversing it patches the retail tree.
            counts["atexit_scope"] = atexit_scope.patch_obj_pair(
                str(orig), str(obj_path), apply=True)["num_renamed"]
        except Exception as exc:
            # Batch swallows a per-pair exception and moves on; mirror that
            # rather than failing a score over one unit's symbol table.
            counts["atexit_scope"] = 0
            if verbose:
                print(f"  atexit_scope ERROR on {unit_rel}: {exc}", file=sys.stderr)
    else:
        counts["guard"] = counts["bool_mangle"] = counts["atexit_scope"] = 0
    return counts


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--unit", help="unit path relative to --src-dir (decides pairing)")
    ap.add_argument("--obj", help="the file to patch (default: <src-dir>/<unit>)")
    ap.add_argument("--pairs-json",
                    help='JSON list of {"unit": <rel>, "obj": <path>} -- one '
                         "index build amortised over the whole batch")
    ap.add_argument("--obj-dir", default=str(PROJECT_ROOT / "build" / BUILD_ID / "obj"))
    ap.add_argument("--src-dir", default=str(PROJECT_ROOT / "build" / BUILD_ID / "src"))
    ap.add_argument("--cache",
                    default=str(PROJECT_ROOT / "build" / BUILD_ID / "anon_ns_index.pkl"))
    ap.add_argument("--no-cache", action="store_true")
    ap.add_argument("--apply", action="store_true",
                    help="required; this script has no dry run (it is a chain of "
                         "five in-place rewrites, and a dry run of the chain is "
                         "not a dry run of any patcher in it)")
    ap.add_argument("--verbose", "-v", action="store_true")
    args = ap.parse_args(argv)

    if not args.apply:
        ap.error("--apply is required")
    obj_dir, src_dir = Path(args.obj_dir), Path(args.src_dir)

    if args.pairs_json:
        pairs = [(str(d["unit"]), Path(d.get("obj") or src_dir / d["unit"]))
                 for d in json.loads(Path(args.pairs_json).read_text())]
    elif args.unit:
        pairs = [(args.unit, Path(args.obj) if args.obj else src_dir / args.unit)]
    else:
        ap.error("give --unit or --pairs-json")

    missing = [str(p) for _u, p in pairs if not p.exists()]
    if missing:
        print(f"ERROR: no such object: {', '.join(missing)}", file=sys.stderr)
        return 1

    index = load_anon_index(obj_dir, src_dir,
                            None if args.no_cache else Path(args.cache))

    total = defaultdict(int)
    for unit_rel, obj_path in pairs:
        counts = patch_one(obj_path, os.path.normpath(unit_rel), obj_dir,
                           index, verbose=args.verbose)
        for k, v in counts.items():
            total[k] += v
        if args.verbose:
            print(f"{unit_rel} -> {obj_path}: "
                  + ", ".join(f"{k}={v}" for k, v in counts.items()))
    print("[patch-chain] " + str(len(pairs)) + " object(s): "
          + ", ".join(f"{k}={total[k]}" for k in COUNT_KEYS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
