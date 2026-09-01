"""Make an in-place source sabotage actually reach the interpreter that runs it.

THE DEFECT THIS EXISTS FOR
--------------------------
CPython validates a cached ``.pyc`` against the source's **(mtime, size)** only,
and the mtime it stores is ``int(st.st_mtime)`` -- **whole seconds**.  So a
sabotage harness that

  1. rewrites a ``.py`` with an edit of the **same byte length**, and
  2. patches and restores **inside one second**,

leaves both validation fields untouched.  The child interpreter loads the
**stale bytecode**, the sabotage never executes, the guard stays green, and the
harness reports the case as ``NOT CAUGHT`` -- for a case it did not run.

It fails *safe* (nothing broken is declared working), but a flaky verifier is
worse than that sounds: it gets re-run until it agrees with you, which is how a
real ``NOT CAUGHT`` gets waved through.

MEASURED, not reasoned about.  ``tests/test_pyc_hygiene.py`` reproduces the
mechanism end to end with a two-file fixture and a ``"cleared"`` -> ``"blocked"``
swap (7 chars each, so the file length never moves):

    unprotected   0 / 6 sabotages caught   (window 0.015 s)
    protected     6 / 6 sabotages caught   (window 0.016 s)

Note it is **deterministically missed**, not merely flaky, once the patch and
restore are fast -- the flake in the field came from occasionally straddling a
second boundary.

THE THREE THINGS THAT FIX IT
----------------------------
All three, because each covers a hole the others leave:

  * ``drop_bytecode()``  -- delete the stale ``.pyc`` **after** writing the
    sabotage and **before** each run, so there is nothing to load;
  * ``-B`` on the child  -- so the run that *does* recompile does not write a
    fresh ``.pyc`` that the *restore* would then leave stale in the other
    direction;
  * ``PYTHONDONTWRITEBYTECODE=1`` in the child **env** -- because the child
    frequently spawns grandchildren (pytest -> ``sys.executable`` subprocesses),
    and ``-B`` does not propagate through ``subprocess`` while the env var does.

Use ``run_python()``, which does all three, rather than assembling them again.

WHAT THIS DOES NOT COVER
------------------------
Only CPython's bytecode cache.  Other ``(mtime, size)``-keyed caches in this
repo behave differently and must not be assumed fixed by association:

  * **ninja** compares mtimes at **nanosecond** granularity, so a same-second
    rewrite is still visible to it.  A harness that edits a ``.cpp`` and
    rebuilds is not exposed to *this* bug (but see ``obj_patch_io.py``, which
    preserves an object's mtime **on purpose**).
  * **objdiff's report cache** hashes object **content**, not timestamps.
"""
from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

__all__ = ["drop_bytecode", "child_env", "python_argv", "run_python",
           "is_length_preserving"]

#: Directory names never swept.  ``.git`` because nothing there is imported;
#: the venv names because sweeping a site-packages tree is slow and is not what
#: staled.  ⚠ If a harness ever sabotages a module that is imported from an
#: *installed* copy rather than from the working tree, this exclusion hides it --
#: sabotage the installed path directly, or drop the exclusion for that run.
_SKIP_DIRS = frozenset({".git", "venv", ".venv", "node_modules"})


def drop_bytecode(root: str | os.PathLike[str]) -> int:
    """Delete every ``.pyc`` under *root*.  Returns how many were removed.

    Call this AFTER writing a sabotage and BEFORE running anything that imports
    it.  Returning the count is deliberate: a caller that expects to have
    invalidated something can assert it did.
    """
    removed = 0
    for pycache in Path(root).rglob("__pycache__"):
        if _SKIP_DIRS.intersection(pycache.parts):
            continue
        for f in pycache.glob("*.pyc"):
            f.unlink(missing_ok=True)
            removed += 1
    return removed


def child_env(env: dict[str, str] | None = None) -> dict[str, str]:
    """``env`` (default ``os.environ``) with bytecode writing disabled.

    The env var, not just ``-B``: it is inherited by grandchildren, and these
    harnesses routinely spawn them.
    """
    return {**(os.environ if env is None else env), "PYTHONDONTWRITEBYTECODE": "1"}


def python_argv(*args: str) -> list[str]:
    """``[sys.executable, "-B", *args]``."""
    return [sys.executable, "-B", *args]


def run_python(args: list[str] | tuple[str, ...], *, root: str | os.PathLike[str],
               **kwargs) -> subprocess.CompletedProcess:
    """Run ``python -B <args>`` with the bytecode cache dropped and disabled.

    ``root`` is the tree to sweep for stale ``.pyc``.  Any other keyword is
    passed straight to :func:`subprocess.run`; ``env`` is wrapped by
    :func:`child_env` so a caller that customises the environment still gets the
    protection.
    """
    drop_bytecode(root)
    kwargs["env"] = child_env(kwargs.get("env"))
    kwargs.setdefault("capture_output", True)
    kwargs.setdefault("text", True)
    return subprocess.run(python_argv(*args), **kwargs)


def is_length_preserving(old: str, new: str) -> bool:
    """True when swapping *old* for *new* leaves the file's byte length alone.

    The exact condition that makes a sabotage invisible to the bytecode cache.
    Harnesses use it to LABEL such cases rather than to skip them -- they are
    the cleanest sabotages to write and must keep working.
    """
    return len(old.encode()) == len(new.encode())
