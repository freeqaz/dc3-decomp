#!/usr/bin/env python3
"""Refuse to measure a build tree that skipped the post-compile patchers.

Why this exists
---------------
`configure.py` chains six `post-compile` edges after the compile edges --
`create_data_stubs.py` plus the five `obj_*_patcher.py` passes -- and then
`scripts/verify_objs_patched.py --check --emit`.  Those edges are DOWNSTREAM of
the objects, taking the `all_source` phony as an implicit input.  Ninja builds
only a named target's ANCESTORS, so `ninja build/<v>/src/Foo.obj` stops exactly
one edge short of every patcher, and the fresh compile OVERWRITES the
previously-patched bytes.  `scripts/verify_objs_patched.py`'s docstring has said
so since 2026-08-09.

That was treated as a caveat for humans typing ninja by hand.  It is not:
`objdiff-cli diff --build` (without `--full-build`) IS
`ninja <base_obj_path>` -- a single-object target -- and `run_objdiff` passed
`--build` on every call.  So the decomp inner loop's own measurement tool
unpatched one object per call, answered from it, and left the tree that way for
whatever measured next.

Measured 2026-08-20 in `wt/objdiff-patchfix`, `default/lazer/game/BustAMovePanel`,
one `.cpp` touched and one ordinary `run_objdiff` call made:

    ruler                                    patched      unpatched
    unit matched_code_percent               44.18098     43.693924   (-0.487)
    unit matched_functions_percent          93.902435    92.68293    (-1.220)
    ?SetUpMoveNames@BustAMovePanel@@AAAXXZ     100.0       99.86842
    whole-build matched_code_percent        43.987103    43.985767   (-0.00134)

...while `run_objdiff` itself reported `100.0% canonical -- Complete (High)`
for `SetUpMoveNames` in BOTH states.  That is the defect's real shape: the bias
is invisible on the tool that causes it and shows up on `report.json`,
`measure_progress.sh` and `query_functions.current_percent` instead, so the two
rulers silently disagree and neither looks wrong.  The lost bytes were the
anonymous-namespace hash (`?A0x08878e05` vs `?A0xc73cd9f6`), one static guard's
storage class, and 13 `??__F...` atexit scope-counter renames.

The contract here
-----------------
Build through `post-compile`, never through the bare `.obj`, and then ASSERT
the manifest.  If either half fails, raise -- callers must surface the error
instead of diffing.  Silently answering low is the behaviour being removed; it
is not to be replaced with silently answering some other way.

`post-compile` reaches every object through `all_source`, so the specific `.obj`
a caller cares about is still compiled first; the patch stamps then re-fire
because `scripts/obj_patch_io.py` preserves each object's mtime, which is what
makes an object newer than a stamp mean "this one needs patching again" instead
of an endless recompile/repatch oscillation.

Cost, measured on this tree: `ninja post-compile` after touching one `.cpp` is
~12 s and leaves the tree verified; on an already-consistent tree it is
`ninja: no work to do.` in ~0.05 s.  `--verify-manifest` over 989 objects is
~0.4 s.  So the guard costs nothing on the repeat calls that dominate a lane
and buys back the one-way-low bias on the calls that follow an edit.
"""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
import time
from pathlib import Path

__all__ = [
    "UnpatchedTreeError",
    "StaleSplitError",
    "ensure_patched_tree",
    "ensure_split_current",
    "POST_COMPILE_TARGET",
]

#: The ninja target that owns the patch passes (see `configure.py`
#: `custom_build_steps`).  Naming the `.obj` instead is the defect.
POST_COMPILE_TARGET = "post-compile"

#: Ninja's own default; `objdiff-cli --build` reads the same two keys, so the
#: guard drives the build exactly the way the tool it replaces did.
_DEFAULT_MAKE = "ninja"

_BUILD_TIMEOUT = 3600
_VERIFY_TIMEOUT = 600


#: How long to wait out a split that is recorded as in-flight before giving up.
#: `dtk xex split` takes ~12 s on this tree; the ceiling is generous because the
#: alternative to waiting is answering 341 functions low.
_SPLIT_WAIT_SECONDS = 180
_SPLIT_POLL_SECONDS = 2.0


class StaleSplitError(RuntimeError):
    """The TARGET objects do not correspond to the current split config.

    The sibling of `UnpatchedTreeError` for the other side of the diff. The base
    side is compiled by declared ninja edges; the target side is written by
    `dtk xex split`, whose 2,223 objects are undeclared outputs that no edge
    stats and no `provenance` block describes. A report taken over target
    objects split from a different `config/<v>/symbols.txt` -- or over a tree a
    split is rewriting right now -- silently reads LOW, because a function whose
    target-side name no longer matches simply scores 0.0%.
    """


class UnpatchedTreeError(RuntimeError):
    """The tree is not a verified fixed point of the post-compile patchers.

    Deliberately a hard error, in the same spirit as `ShadowDatabaseError`:
    a measurement taken from a partially-patched tree is not a slightly worse
    measurement, it is a measurement of symbol names, storage classes and
    relocations that this project does not match against.
    """


def _make_command(project_dir: Path) -> list[str]:
    """Mirror `objdiff-cli --build`: `custom_make` + `custom_args` or `ninja`."""
    make, args = _DEFAULT_MAKE, []
    cfg = project_dir / "objdiff.json"
    if cfg.exists():
        try:
            doc = json.loads(cfg.read_text())
        except (OSError, json.JSONDecodeError):
            doc = {}
        make = doc.get("custom_make") or _DEFAULT_MAKE
        args = list(doc.get("custom_args") or [])
    return [make, *args]


def _tail(text: str, n: int = 25) -> str:
    lines = [ln for ln in (text or "").splitlines() if ln.strip()]
    return "\n".join(lines[-n:])


def ensure_split_current(project_dir: Path | str, *,
                         wait_seconds: float = _SPLIT_WAIT_SECONDS) -> str:
    """Assert `project_dir`'s target objects came from its current split config.

    Returns a one-line note, or raises `StaleSplitError`.

    A split recorded as IN FLIGHT is waited out rather than refused: it is a
    transient state that resolves in ~12 s, and in the main repo a handful of
    lanes build concurrently, so refusing outright would turn one lane's `ninja`
    into another lane's error. A split that is still running at the deadline, or
    a config that has genuinely drifted, raises.
    """
    project_dir = Path(project_dir).resolve()
    checker = project_dir / "scripts" / "verify_split_current.py"
    if not checker.exists():
        # Older trees (and sibling repos) predate the guard. Say so rather than
        # inventing a verdict -- but do not block: this returns a note the
        # caller can print, exactly as an absent alias map would.
        return "split currency NOT checked (scripts/verify_split_current.py absent)"

    sys.path.insert(0, str(project_dir / "scripts"))
    try:
        import importlib
        import importlib.util
        spec = importlib.util.spec_from_file_location(
            "_dc3_verify_split_current", checker)
        module = importlib.util.module_from_spec(spec)
        assert spec.loader is not None
        spec.loader.exec_module(module)
    finally:
        try:
            sys.path.remove(str(project_dir / "scripts"))
        except ValueError:
            pass

    deadline = time.monotonic() + max(0.0, wait_seconds)
    last: Exception | None = None
    while True:
        try:
            return str(module.check(project_dir))
        except module.StaleSplitError as exc:
            last = exc
            in_flight = f"`{module.STATE_RUNNING}`" in str(exc)
            if not in_flight or time.monotonic() >= deadline:
                break
            time.sleep(_SPLIT_POLL_SECONDS)

    raise StaleSplitError(str(last))


def ensure_patched_tree(project_dir: Path | str, *, build: bool = True) -> str:
    """Bring `project_dir`'s object tree to the post-compile fixed point.

    Returns a one-line note suitable for echoing to the caller.  Raises
    `UnpatchedTreeError` -- never returns a plausible number -- if the tree
    cannot be brought to, or verified at, that fixed point.

    `build=False` skips the build (the caller asked for a read-only look) but
    still verifies, because reading an unpatched object is the failure being
    prevented, not the build.
    """
    project_dir = Path(project_dir).resolve()
    verify = project_dir / "scripts" / "verify_objs_patched.py"
    if not verify.exists():
        raise UnpatchedTreeError(
            f"{verify} is absent, so this tree's patch state cannot be "
            f"established. Refusing to measure: a diff taken here would "
            f"describe raw compiler output, not the shape this project "
            f"matches against."
        )

    notes = []

    if build:
        make = _make_command(project_dir)
        if shutil.which(make[0]) is None and not Path(make[0]).exists():
            raise UnpatchedTreeError(
                f"build tool `{make[0]}` not found on PATH. Refusing to "
                f"measure rather than diffing whatever objects happen to be "
                f"on disk."
            )
        cmd = [*make, POST_COMPILE_TARGET]
        try:
            proc = subprocess.run(
                cmd, cwd=str(project_dir), capture_output=True, text=True,
                timeout=_BUILD_TIMEOUT,
            )
        except subprocess.TimeoutExpired:
            raise UnpatchedTreeError(
                f"`{' '.join(cmd)}` timed out after {_BUILD_TIMEOUT}s in "
                f"{project_dir}."
            ) from None
        if proc.returncode != 0:
            raise UnpatchedTreeError(
                f"`{' '.join(cmd)}` failed (exit {proc.returncode}) in "
                f"{project_dir}.\n\n{_tail(proc.stderr) or _tail(proc.stdout)}\n\n"
                f"The measurement is NOT being reported: whatever objects are "
                f"on disk did not come from a complete build."
            )
        head = (proc.stdout or "").strip().splitlines()
        notes.append(head[-1] if head else "post-compile up to date")

    try:
        proc = subprocess.run(
            [sys.executable, str(verify), "--verify-manifest", "--quiet"],
            cwd=str(project_dir), capture_output=True, text=True,
            timeout=_VERIFY_TIMEOUT,
        )
    except subprocess.TimeoutExpired:
        raise UnpatchedTreeError(
            f"verify_objs_patched.py --verify-manifest timed out after "
            f"{_VERIFY_TIMEOUT}s in {project_dir}."
        ) from None

    if proc.returncode != 0:
        remedy = (
            "Run `ninja` in that directory, then retry."
            if not build else
            "`ninja post-compile` ran and the tree STILL does not match its "
            "manifest -- that is a regression of the build graph itself, not "
            "a stale tree. See docs/tools/BUILD_SYSTEM.md."
        )
        raise UnpatchedTreeError(
            f"REFUSING TO MEASURE {project_dir}: its objects are not a "
            f"verified fixed point of the post-compile patchers, so every "
            f"symbol name, storage class and relocation in them describes raw "
            f"compiler output. A diff taken here reads LOW and one-directional "
            f"(measured -0.487 pp of unit matched_code on one object).\n\n"
            f"{_tail(proc.stderr) or _tail(proc.stdout)}\n\n{remedy}"
        )

    notes.append((proc.stdout or proc.stderr or "").strip() or "patch state verified")

    # The other side of the diff. `--verify-manifest` above vouches for
    # build/<v>/src/**.obj (compiled, declared, stamped); nothing in it looks at
    # build/<v>/obj/**.obj, which `dtk xex split` writes as an UNDECLARED output
    # and which decides every target-side symbol name. A tree can be a perfect
    # post-compile fixed point and still be measured 341 functions low because
    # the split has not caught up with config/<v>/symbols.txt.
    #
    # Narrow carve-out: with `build=False` the caller has explicitly asked not
    # to run ninja, so a tree that has never recorded a split cannot be fixed
    # from here. That one case degrades to a note. Drift and a stuck in-flight
    # split still raise in both modes -- those are wrong answers, not absences.
    try:
        notes.append(ensure_split_current(project_dir))
    except StaleSplitError as exc:
        never_split = "is missing or unreadable" in str(exc)
        if never_split and not build:
            notes.append("split currency UNKNOWN (no stamp; build=False)")
        else:
            raise

    return " | ".join(n for n in notes if n)
