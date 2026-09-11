#!/usr/bin/env python3
"""Which TREE is this script acting on?  Answer it from the INVOCATION, never
from `Path(__file__).resolve()`.

The defect this exists to make impossible
-----------------------------------------
`scripts/measure_progress.sh` builds its baseline in a throwaway git worktree
and (until 2026-09-11) replaced that worktree's `scripts/` with a symlink to
the MAIN checkout's `scripts/`.  Every script that opened with

    PROJECT_ROOT = Path(__file__).resolve().parent.parent

then answered about the wrong tree, because `.resolve()` follows that symlink:
invoked as `<worktree>/scripts/foo.py`, it computed `<main>` and acted there.
The build edges run `python3 scripts/<name>.py` with cwd set to the tree being
built, so *every* post-compile guard and patcher in the baseline build was
pointed at main.  Measured 2026-09-11 in a baseline-shaped worktree holding
exactly one object and its own matching manifest::

    $ cd /tmp/claude/mp-repro                       # scripts/ -> <main>/scripts
    $ python3 scripts/verify_objs_patched.py --verify-manifest
    BUILD TREE DRIFTED SINCE IT WAS LAST VERIFIED PATCHED
    manifest written 2026-09-11T02:04:57Z over 990 objects
      817 content differs: build/373307D9/src/App.obj ...
    exit=1
    $ python3 scripts/verify_objs_patched.py --repo "$PWD" --verify-manifest
    [patch-state] OK: 1 objects match (tree_sha256=d27934327c69cf72)
    exit=0

    $ python3 scripts/verify_split_current.py --check
    [split-guard] split current (3 config inputs match .../split_inputs.stamp)
    exit=0        # in a tree that has never been split and has no stamp at all

    $ python3 scripts/obj_anon_ns_patcher.py --batch
    Assignment rules: majority=152, template=797, ... token=1059
                  # 990 of MAIN's objects, from a tree with one fake .obj.
                  # The ninja edge runs this same command with --apply.

The failure shapes, in order of how bad they are: a guard that vouches for a
tree it never looked at; a guard that fails a clean tree and names another
tree's paths (the report that started this -- main was mid-churn, so the
baseline build died quoting main's objects); and a *patcher* that WRITES into
another tree's build directory.

The rule
--------
The tree a script acts on is the tree whose `scripts/` the caller NAMED, i.e.
`os.path.abspath(__file__)` with symlinks left alone -- or an explicit
`--repo` / `--project-dir`, which always wins.  `os.path.realpath` is the wrong
function here: it answers "where does this file really live", and that is a
question about the filesystem, not about the measurement.

Why not resolve the root from `os.getcwd()` instead?  Because a script invoked
by absolute path from an unrelated directory (`cd /tmp; python3
/repo/scripts/x.py`) would then have no tree at all, and the ninja edges --
the invocation that actually matters -- already agree with the invocation path.
When the two DO disagree and both look like project trees, that is worth a line
on stderr rather than a silent choice, so the warning below says so.

This module is deliberately dependency-free and importable from a symlinked
`scripts/` directory (it is then main's copy, reached through the symlink --
which is fine, since it derives everything from the CALLER's `__file__`).
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

__all__ = ["project_root", "invoked_root", "scripts_dir_is_symlink"]

#: Files that mark a directory as a DC3 project tree.  Only used for the
#: advisory cwd-vs-invocation note; nothing is gated on them.
_PROJECT_MARKERS = ("configure.py", "objdiff.json", "build.ninja")

_WARNED: set[str] = set()


def invoked_root(script_file: str, levels: int = 1) -> Path:
    """The project root implied by HOW `script_file` was invoked.

    `levels` is how many directories up from the script the root is: 1 for
    `scripts/foo.py`, 2 for `scripts/analysis/foo.py`.

    Symlinks are deliberately NOT resolved -- see the module docstring.
    `os.path.abspath` normalises `..` lexically and makes a relative
    invocation (`python3 scripts/foo.py`) absolute against the cwd, which is
    exactly what the ninja edges do.
    """
    p = Path(os.path.abspath(script_file))
    return p.parents[levels]


def scripts_dir_is_symlink(script_file: str) -> tuple[bool, Path, Path]:
    """Is the directory holding `script_file` a symlink into another tree?

    Returns ``(is_symlink, invoked_dir, real_dir)``.  This is the condition
    that used to silently redirect a whole build's worth of guards.
    """
    d = Path(os.path.abspath(script_file)).parent
    real = Path(os.path.realpath(d))
    return (real != d, d, real)


def project_root(script_file: str, explicit=None, levels: int = 1,
                 warn: bool = True) -> Path:
    """Resolve the tree this invocation is about.

    Order: an explicit ``--repo`` / ``--project-dir`` value wins; otherwise the
    invocation path (`invoked_root`).  Never `realpath` of the script.

    With `warn`, a one-line note goes to stderr when the script's directory is
    a symlink into a different tree, or when the cwd is a *different* project
    tree from the invocation root.  Both mean "two trees are in play"; neither
    changes the answer, and neither is an error.
    """
    if explicit:
        return Path(explicit).resolve()

    root = invoked_root(script_file, levels)
    if warn:
        _warn_once(script_file, root)
    return root


def _warn_once(script_file: str, root: Path) -> None:
    name = os.path.basename(script_file)
    symlinked, invoked_dir, real_dir = scripts_dir_is_symlink(script_file)
    key = f"{name}:{root}"
    if key in _WARNED:
        return
    if symlinked:
        _WARNED.add(key)
        print(
            f"[project-root] {name}: {invoked_dir} is a symlink to {real_dir}; "
            f"acting on {root} (the tree it was invoked FOR). "
            f"Path(__file__).resolve() would have acted on {real_dir.parent}. "
            f"Pass --repo/--project-dir to be explicit.",
            file=sys.stderr,
        )
        return
    cwd = Path(os.getcwd())
    if cwd != root and _looks_like_project(cwd) and _looks_like_project(root):
        _WARNED.add(key)
        print(
            f"[project-root] {name}: cwd is {cwd} but this invocation names "
            f"{root}; acting on {root}. Pass --repo/--project-dir to be "
            f"explicit.",
            file=sys.stderr,
        )


def _looks_like_project(d: Path) -> bool:
    return any((d / m).exists() for m in _PROJECT_MARKERS)


if __name__ == "__main__":  # pragma: no cover - trivial CLI
    print(project_root(sys.argv[0] if len(sys.argv) < 2 else sys.argv[1]))
