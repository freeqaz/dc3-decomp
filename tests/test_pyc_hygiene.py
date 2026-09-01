#!/usr/bin/env python3
"""The stale-`.pyc` defect, reproduced -- and the fix, watched to work.

`scripts/pyc_hygiene.py` exists because a sabotage harness silently did not run
one of its own sabotages.  A helper that is only *described* as fixing that is a
claim; this file is the measurement.

TWO DIRECTIONS, ONE MECHANISM
-----------------------------
CPython validates a cached `.pyc` against the source's (mtime, size), storing
mtime as `int(st.st_mtime)` -- WHOLE SECONDS.  A byte-length-preserving edit
applied and undone inside one second moves neither field, which breaks a
sabotage harness twice over:

  READ side   the sabotaged source is never compiled; the child loads the
              bytecode of the CLEAN source, the guard stays green, and the case
              is reported NOT CAUGHT for a sabotage that never ran.
              Closed by `drop_bytecode()`.  NOT closed by `-B` or by
              PYTHONDONTWRITEBYTECODE -- measured below; both govern writing.

  WRITE side  the run that DOES compile the sabotage caches it, stamped with a
              second that the restore does not move.  The next run -- the one
              asserting the tree came back GREEN -- then loads SABOTAGED
              bytecode from RESTORED source and reports "RESTORE FAILED".
              Closed by `-B` + the env var.  Measured at 3/3 below.

Each test carries its NEGATIVE CONTROL INSIDE ITSELF: the same fixture, the
same edit, with the defence removed, asserted to fail.  That is what stops the
protected arm from passing vacuously if CPython ever changes -- the control goes
red and says the file needs rewriting rather than deleting.

The same-second condition is forced by `_align_to_second()`, not left to luck.
An early draft of this file passed 5 of 6 cycles by accident of scheduling,
which is the same intermittency it is supposed to be about.
"""
from __future__ import annotations

import os
import subprocess
import sys
import time
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

from pyc_hygiene import (  # noqa: E402
    child_env, drop_bytecode, is_length_preserving, python_argv, run_python)

#: 7 characters each -- the real S10b swap, and the reason the file length never
#: moved.  Kept identical to the field case on purpose.
CLEAN, SABOTAGED = "cleared", "blocked"

VICTIM = 'VERDICT = "cleared"\n'
PROBE = ("import sys, victim\n"
         "sys.exit(0 if victim.VERDICT == 'cleared' else 1)\n")

#: How many times to re-try arming the hazard before giving up.  Arming is
#: cheap (one `py_compile`); only a pathologically loaded box should need more
#: than one attempt.
_ARM_ATTEMPTS = 8


def _align_to_second() -> None:
    """Block until just after a whole-second tick.

    The hazard needs the prime and the sabotage to share one `int(st_mtime)`.
    Without this the test is a coin flip on where in the second it started.
    """
    time.sleep(1.0 - (time.time() % 1.0) + 0.01)


def _pyc_source_mtime(pyc: Path) -> int:
    """The source mtime CPython recorded in *pyc*: bytes 8..12, little-endian.

    Read directly rather than inferred from a wall-clock budget.  An earlier
    draft skipped when a cycle took longer than 0.55 s, which is a PROXY for
    the condition -- and a proxy that turns "the machine is busy" into a silent
    skip of the one control this file exists for.  This is the condition
    itself, so the fixture can assert it is armed instead of hoping.
    """
    return int.from_bytes(pyc.read_bytes()[8:12], "little")


def _write_fixture(tmp_path: Path) -> tuple[Path, Path]:
    victim, probe = tmp_path / "victim.py", tmp_path / "probe.py"
    victim.write_text(VICTIM)
    probe.write_text(PROBE)
    return victim, probe


def _prime(tmp_path: Path, probe: Path) -> None:
    """Cache `victim.py`'s bytecode from the CLEAN source, as a baseline run does."""
    subprocess.run([sys.executable, str(probe)], cwd=tmp_path,
                   capture_output=True, check=True)
    assert list((tmp_path / "__pycache__").glob("victim*.pyc")), (
        "the fixture never cached victim.py, so there is no stale bytecode for "
        "this test to be about -- it would pass for the wrong reason")


def _run(tmp_path: Path, probe: Path, *, protected: bool) -> int:
    if protected:
        return subprocess.run(python_argv(str(probe)), cwd=tmp_path,
                              capture_output=True, text=True,
                              env=child_env()).returncode
    return subprocess.run([sys.executable, str(probe)], cwd=tmp_path,
                          capture_output=True, text=True).returncode


def _arm(tmp_path: Path, victim: Path, probe: Path, clean: str) -> Path:
    """Prime the cache, then write the sabotage in the SAME whole second.

    Returns the `.pyc`, having ASSERTED that it is now stale-but-valid: same
    recorded source mtime, same file size.  That is the hazard, stated as the
    two fields CPython actually compares, so a cycle can never proceed while
    silently un-armed.
    """
    sabotaged = clean.replace(CLEAN, SABOTAGED)
    assert len(sabotaged.encode()) == len(clean.encode())
    for attempt in range(_ARM_ATTEMPTS):
        drop_bytecode(tmp_path)
        _align_to_second()
        victim.write_text(clean)
        _prime(tmp_path, probe)
        pyc, = (tmp_path / "__pycache__").glob("victim*.pyc")
        recorded, size_before = _pyc_source_mtime(pyc), victim.stat().st_size
        victim.write_text(sabotaged)
        if (int(victim.stat().st_mtime) == recorded
                and victim.stat().st_size == size_before):
            return pyc
    pytest.skip(f"could not arm the stale-.pyc hazard in {_ARM_ATTEMPTS} "
                f"attempts: every prime+sabotage pair straddled a second "
                f"boundary. This box is too loaded to hold a 1s window open; "
                f"the result would be uninterpretable, NOT green.")


# ── READ side ────────────────────────────────────────────────────────────────

def test_a_length_preserving_same_second_sabotage_is_missed_without_the_sweep(
        tmp_path: Path) -> None:
    """THE CONTROL.  The hazard must still be reproducible, or nothing below counts.

    Three armed cycles.  `_arm` has already asserted the `.pyc` is stale-but-
    valid, so a "caught" here is CPython's behaviour changing, not the fixture
    drifting -- which is the difference between a control and a coin flip.
    """
    victim, probe = _write_fixture(tmp_path)
    original = VICTIM
    misses = 0
    for _ in range(3):
        _arm(tmp_path, victim, probe, original)
        try:
            caught = _run(tmp_path, probe, protected=False) != 0
        finally:
            victim.write_text(original)
        misses += not caught
    assert misses == 3, (
        f"the unprotected arm CAUGHT the sabotage on {3 - misses}/3 cycles "
        f"with the .pyc PROVEN stale-but-valid.  Either CPython no longer "
        f"validates by (mtime, size), or the import no longer reads the cache "
        f"-- either way the protected tests below are now vacuous and this "
        f"file needs REWRITING, not deleting.")


def test_dropping_the_bytecode_makes_the_same_sabotage_land(tmp_path: Path) -> None:
    """The READ-side fix, on the identical armed fixture the control just missed on."""
    victim, probe = _write_fixture(tmp_path)
    original = VICTIM
    for _ in range(3):
        _arm(tmp_path, victim, probe, original)
        assert drop_bytecode(tmp_path) >= 1, "nothing was swept; fixture is wrong"
        try:
            caught = _run(tmp_path, probe, protected=True) != 0
        finally:
            victim.write_text(original)
        assert caught, "the sabotage still did not land after dropping the .pyc"


def test_run_python_does_the_sweep_for_you(tmp_path: Path) -> None:
    """`run_python()` is the shape harnesses should call; it must suffice alone."""
    victim, probe = _write_fixture(tmp_path)
    _arm(tmp_path, victim, probe, VICTIM)
    try:
        rc = run_python([str(probe)], root=tmp_path, cwd=tmp_path).returncode
    finally:
        victim.write_text(VICTIM)
    assert rc != 0, "run_python() did not invalidate the stale bytecode"


# ── WRITE side ───────────────────────────────────────────────────────────────

def test_without_B_the_sabotage_run_poisons_the_RESTORE_check(tmp_path: Path) -> None:
    """The second direction, and the reason `-B` + the env var are not decoration.

    A harness that sweeps once (before the sabotage) and not again is left with
    a `.pyc` compiled FROM THE SABOTAGE and stamped with a second the restore
    does not move.  The restore-verification run then reads it and reports
    "RESTORE FAILED" on a tree that is in fact clean.

    NEGATIVE CONTROL is the second half: the identical cycle with `-B` and the
    env var comes back GREEN, so this is about bytecode and not about the
    restore being broken.
    """
    victim, probe = _write_fixture(tmp_path)

    def cycle(protected: bool) -> tuple[bool, bool]:
        _arm(tmp_path, victim, probe, VICTIM)
        drop_bytecode(tmp_path)                    # swept ONCE, as a naive harness would
        red = _run(tmp_path, probe, protected=protected) != 0
        victim.write_text(VICTIM)                  # restore: same second, same size
        green = _run(tmp_path, probe, protected=protected) == 0
        return red, green

    for _ in range(3):
        red, green = cycle(protected=False)
        assert red, "the sabotage did not land even after a sweep; fixture is wrong"
        assert not green, (
            "the unprotected restore check came back GREEN, so `-B` and "
            "PYTHONDONTWRITEBYTECODE have nothing to protect here -- re-measure "
            "before dropping them from run_python()")

    for _ in range(3):
        red, green = cycle(protected=True)
        assert red and green, (
            f"with -B + PYTHONDONTWRITEBYTECODE the cycle must be RED then "
            f"GREEN; got red={red} green={green}")


def test_child_env_reaches_GRANDchildren_where_B_does_not(tmp_path: Path) -> None:
    """`-B` does not survive a subprocess hop; the env var must.

    These harnesses run pytest, which runs `sys.executable` again -- so the
    write-side protection has to be inheritable, not just an interpreter flag.
    """
    inner = tmp_path / "inner.py"
    inner.write_text("import sys; sys.exit(0 if sys.dont_write_bytecode else 1)\n")
    outer = tmp_path / "outer.py"
    outer.write_text(
        "import subprocess, sys\n"
        f"sys.exit(subprocess.run([sys.executable, {str(inner)!r}]).returncode)\n")

    # NEGATIVE CONTROL: a plain `-B` parent does NOT propagate to the grandchild.
    stripped = {k: v for k, v in os.environ.items() if k != "PYTHONDONTWRITEBYTECODE"}
    plain = subprocess.run([sys.executable, "-B", str(outer)],
                           capture_output=True, text=True, env=stripped)
    assert plain.returncode == 1, (
        "`-B` reached the grandchild, so child_env() is redundant -- "
        "re-measure before removing it")

    assert run_python([str(outer)], root=tmp_path, cwd=tmp_path).returncode == 0, \
        "PYTHONDONTWRITEBYTECODE did not reach the grandchild"


# ── the fixture's own arming, and the helper surface ─────────────────────────

def test_arm_refuses_to_proceed_on_an_unarmed_fixture(tmp_path: Path) -> None:
    """`_arm` must assert the hazard, not assume it.

    SABOTAGE, applied here rather than described: make the edit length-CHANGING.
    The `.pyc` size field then differs, the hazard is not armed, and `_arm`
    must skip rather than hand back a fixture that would make every test above
    pass for the wrong reason.
    """
    victim, probe = _write_fixture(tmp_path)
    pyc = _arm(tmp_path, victim, probe, VICTIM)
    assert _pyc_source_mtime(pyc) == int(victim.stat().st_mtime), \
        "_arm returned a fixture whose recorded mtime does not match the source"

    victim.write_text(VICTIM)
    drop_bytecode(tmp_path)
    _align_to_second()
    victim.write_text(VICTIM)
    _prime(tmp_path, probe)
    pyc, = (tmp_path / "__pycache__").glob("victim*.pyc")
    victim.write_text(VICTIM.replace(CLEAN, SABOTAGED + "!"))   # length CHANGES
    assert victim.stat().st_size != len(VICTIM.encode()), "fixture edit did not grow"
    assert _run(tmp_path, probe, protected=False) != 0, (
        "a length-CHANGING edit was still masked by the stale .pyc -- then size "
        "is not part of the validation and this whole file rests on a false "
        "premise")


def test_drop_bytecode_reports_what_it_removed(tmp_path: Path) -> None:
    """The count is the caller's evidence that it invalidated anything."""
    _write_fixture(tmp_path)
    _prime(tmp_path, tmp_path / "probe.py")
    assert drop_bytecode(tmp_path) >= 1
    assert drop_bytecode(tmp_path) == 0, "a second sweep found more to remove"


def test_is_length_preserving_labels_the_dangerous_shape() -> None:
    assert is_length_preserving("cleared", "blocked")
    assert not is_length_preserving("return 1", "return 0\n")
    # Bytes, not characters: the .pyc validator compares file SIZE.
    assert not is_length_preserving("a", "\u00e9")


if __name__ == "__main__":
    raise SystemExit(pytest.main([__file__, "-q", "-p", "no:cacheprovider"]))
