#!/usr/bin/env python3
"""Refuse to report over a `complete: true` unit that has no base object.

Why this exists
---------------
`objdiff.json` lets a unit carry `metadata.complete: true`.  In
`objdiff-cli`'s `report_object` (objdiff-cli/src/cmd/report.rs) that flag is
not decoration -- it is a **substitution for a measurement**:

    let match_percent = match symbol_diff.match_percent {
        Some(pct) => pct,
        None if base.is_none() && object.complete.unwrap_or(false) => {
            // No target object but unit is marked complete: assume 100% match
            100.0
        }
        None => 0.0,
    };

`base` there is the **base object** -- the decompiled side, `base_path`.  So a
unit whose `base_path` is absent, and which is marked complete, has every one
of its functions credited at **100%** without a single instruction being
compared, and the unit's `complete_code` is set to its `total_code` outright
(same file, `if metadata.complete.unwrap_or(false)`).  The 2026-08-22
unfalsifiable-instrument audit measured this directly: 20 functions credited
100%, one of which a real diff scores at 69%.

That behaviour is a **sanctioned upstream escape hatch** and is deliberately
NOT being changed -- objdiff is shared with `../rb3` and `../rb3-xenon` by
symlink, and every consumer's tracked percentage would move.  What is missing
is a statement, on this repo's own report edge, that the hatch is not currently
firing here.

Why it is not hypothetical
--------------------------
Measured on `a8fead7b1`: **968 of dc3's 2,224 units are `complete: true`, and
all 968 have a base object on disk.**  Nothing is credited without measurement
today.  But that is a property of the tree, not of the build, and open task
#142 is exactly the input that breaks it: `native/.../Tex_Wgpu.cpp` compiles to
no object and `dc3-native` still links.  A `.cpp` that stops emitting an object
inside a `complete: true` unit turns 100% of that unit into free credit, and
the headline moves UP.  A silent regression that reads as progress is the worst
shape a measurement defect can take, so this is a gate, not a report.

Two failure shapes, both checked
--------------------------------
1. `base_path` **absent from the unit entry**.  This is the shape that reaches
   the substitution above: `base` is `None`, the hatch fires, silent +100%.
2. `base_path` **present but the file is missing or empty on disk**.  objdiff
   errors out of `report generate` here rather than crediting (`obj::read::read`
   is `?`-propagated), so today this is loud.  It is checked anyway: it is
   cheap, it is the same class of defect, and "loud" is a property of the
   current objdiff, not a contract.

What this deliberately does NOT check
-------------------------------------
Whether the base object is *correct*, patched, or current.  That is
`scripts/verify_objs_patched.py` (base side) and `scripts/verify_split_current.py`
(target side).  This file answers exactly one question: **is any unit being
credited 100% without a base object to measure against?**

"Empty right now" is not the same as "nothing emits it" (task #149)
-------------------------------------------------------------------
Both of the disk shapes above are *also* what an object looks like for the few
tens of milliseconds while `cl.exe` is writing it.  Measured in a worktree
2026-08-23, one incremental `ninja -j 16` over 220 recompiled units, polling
this checker in a tight loop:

    polls                                   19,852
    polls with at least one offender           264   (1.3%)
    distinct zero-byte windows                 169
    window duration           min 26 ms / med 58 ms / max 552 ms

Every one of those 169 was a file **present at zero bytes** -- `missing_polls`
was 0 -- so MSVC creates the object and fills it, rather than writing a temp
and renaming.  A guard that reddens on that is a guard people disable, and then
the hole it covers is open again with everyone believing it is closed.

Two fixes, in order of strength.

**1. A real happens-after relationship (the primary fix, and the one that
covers the build).**  `tools/project.py` gives the `complete_units_check` edge
an `order_only` dependency on `all_source` and `post-compile`, so ninja cannot
schedule this check until every compile edge and every post-compile patcher has
finished.  That edge passes `--ordered-after-compile`, which turns the
tolerance below OFF: having *earned* quiescence structurally, the build path
gets the strict, definitive answer, and a genuine task-#142 object still exits
1 with the unit named.  `tests/test_complete_units.py` asserts the flag and the
`order_only` deps travel together, because either one alone is wrong.

The stamp-bracket pattern from `verify_split_current.py` (`--begin` / `--complete`
around the writer, `running` vs `complete`) was considered and NOT copied: the
split is **one** edge, so bracketing it is a two-line change, whereas
compilation is 2,000+ edges and "bracket every writer" is precisely the
relationship ninja's dependency graph already expresses.  Re-implementing it in
a stamp would be a second, weaker copy of the build graph.

**2. A quiescence precondition (for invocation OUTSIDE ninja).**  The checker is
also run by hand and by `tests/test_complete_units.py`, where no edge orders
anything.  There, if *every* offender is of the missing/empty kind AND a `ninja`
building this same tree is alive, the checker refuses to answer: exit 6,
`BuildInFlightError`.  It does not pass -- a real defect during a build is still
a non-zero exit with the units named -- it declines to claim which of the two
causes it is looking at, exactly as the split guard's `running` state does.

Deliberately narrow, so this cannot become a way to launder a red:

  * The `no base_path` shape is **never** excused.  A missing config key is not
    something a compiler can be in the middle of; only disk shapes are.
  * A mix (one config offender plus some empty objects) is red, not
    indeterminate.
  * The build must be in **this** tree.  A `ninja` in another worktree is not
    detected and excuses nothing (`comm == "ninja"` plus either `cwd ==
    project_dir` or an open fd on this tree's `.ninja_log`/`.ninja_deps`).
  * `--ordered-after-compile` disables it entirely.

What was measured and REJECTED: probing `/proc/*/fd` for the process writing the
object.  It sounds like a positive signal rather than a timing guess, but over
those same 169 windows it **failed to find a writer in 38 of them (22%)** -- the
scan itself takes longer than a median window, so by the time it runs the write
has completed and the fd is closed.  A discriminator that is blind to a fifth of
the population is worse than useless here, because its silence looks like proof.
Detecting the *build* instead of the *write* is the same idea against a target
that lasts minutes rather than 58 ms: over a 25 s build it was present in 25 of
25 one-second samples.

An empty population is a REFUSAL, not a pass
--------------------------------------------
A check that would also print OK over an objdiff.json with no units -- or with
no `complete: true` units -- is a check that cannot report the failure of its
own input.  Zero units exits 5; zero `complete: true` units exits 5.  Success
always prints its denominator, so a passing run can be audited after the fact.
`--selftest` runs the negative control (a synthetic config whose complete unit
is missing its object) and fails if the checker passes it.

Exit codes
----------
    0   every `complete: true` unit has a non-empty base object
    1   at least one does not (the message names every offending unit)
    4   objdiff.json is missing or unparseable -- state cannot be established
    5   empty universe (no units) or empty population (no `complete: true`
        units): there was nothing to measure, which is not the same as clean
    6   INDETERMINATE: objects are missing/empty, but a `ninja` building this
        same tree is alive, so this is what a mid-write object looks like too.
        Not a pass. Re-run when the build is quiescent to get 0 or 1.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

CONFIG_NAME = "objdiff.json"

EXIT_OK = 0
EXIT_UNCREDITED = 1
EXIT_NO_CONFIG = 4
EXIT_EMPTY = 5
EXIT_INDETERMINATE = 6

#: Offender shapes.  Only the two DISK shapes can be explained by a build in
#: flight; `NO_BASE_PATH` is a config defect and is never excused.
SHAPE_NO_BASE_PATH = "no_base_path"
SHAPE_MISSING = "missing"
SHAPE_EMPTY = "empty"

DISK_SHAPES = frozenset({SHAPE_MISSING, SHAPE_EMPTY})

#: Files a running `ninja` holds open for the duration of a build.  Measured
#: 2026-08-23: present in 24 of 25 one-second samples across a 25 s build (the
#: missing sample is the first second, before ninja has loaded the graph and
#: therefore before any compile has started).
NINJA_OPEN_FILES = (".ninja_log", ".ninja_deps")


class Offender:
    """One unit credited complete without a base object to have measured.

    A plain class, not a `@dataclass`: `tests/test_complete_units.py` loads this
    file through `importlib.util.spec_from_file_location` WITHOUT registering it
    in `sys.modules`, and dataclasses on Python 3.10 resolve `cls.__module__`
    through `sys.modules` while processing annotations -- which raises
    `AttributeError: 'NoneType' object has no attribute '__dict__'` at import
    time. The tests must be able to load the checker exactly as they do.
    """

    __slots__ = ("unit", "shape", "text")

    def __init__(self, unit: str, shape: str, text: str):
        self.unit = unit
        self.shape = shape
        self.text = text

    def __str__(self) -> str:  # the message the guard prints
        return self.text

    def __repr__(self) -> str:
        return f"Offender({self.unit!r}, {self.shape!r})"


class UncreditedCompleteUnitError(RuntimeError):
    """A `complete: true` unit has no base object, so its 100% is unmeasured."""


class BuildInFlightError(RuntimeError):
    """Objects are missing/empty AND a ninja is writing this tree right now.

    Neither a pass nor a refusal to vouch: a refusal to *guess which one*.  The
    same disk shape is produced by a .cpp that stopped emitting an object and by
    `cl.exe` 30 ms into writing one, and only quiescence tells them apart.
    """


class EmptyPopulationError(RuntimeError):
    """There was nothing to check -- which is not the same as nothing wrong."""


class MissingConfigError(RuntimeError):
    """`objdiff.json` could not be read, so no claim can be made."""


def load_units(project_dir: Path) -> list[dict]:
    cfg = project_dir / CONFIG_NAME
    if not cfg.exists():
        raise MissingConfigError(
            f"REFUSING TO VOUCH FOR {project_dir}: {cfg} is absent, so which "
            f"units carry `complete: true` cannot be established. Run "
            f"`configure.py` in that directory and retry."
        )
    try:
        doc = json.loads(cfg.read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise MissingConfigError(
            f"REFUSING TO VOUCH FOR {project_dir}: {cfg} could not be parsed "
            f"({exc}). An unreadable config is an unknown state, not a clean one."
        ) from None
    units = doc.get("units")
    if not isinstance(units, list):
        raise MissingConfigError(
            f"REFUSING TO VOUCH FOR {project_dir}: {cfg} has no `units` array "
            f"(found {type(units).__name__}). A typo'd key must not read as "
            f"'no units are complete'."
        )
    return units


def is_complete(unit: dict) -> bool:
    return (unit.get("metadata") or {}).get("complete") is True


def ninja_builds_in_flight(project_dir: Path) -> list[str]:
    """Live `ninja` processes building THIS tree, as "pid (how it was matched)".

    Two independent matches, because neither alone covers how builds get
    started here:

      * `cwd == project_dir` -- a bare `ninja` run from the tree root, which is
        how every lane and every doc in this repo invokes it; and
      * an open descriptor on this tree's `.ninja_log` / `.ninja_deps` -- which
        also catches `ninja -C <tree>` from somewhere else.

    Only processes whose `comm` is exactly `ninja` are examined, so the fd scan
    costs a handful of `readdir`s rather than a walk of every fd on the box.
    That matters: the naive whole-box scan is what made the per-object writer
    probe too slow to be trusted (see the module docstring).

    Returns [] on any platform without `/proc`, and on any permission error --
    "no signal" degrades to strict, never to tolerant.
    """
    project_dir = Path(project_dir).resolve()
    proc = Path("/proc")
    if not proc.is_dir():
        return []

    watched: dict[tuple[int, int], str] = {}
    for rel in NINJA_OPEN_FILES:
        try:
            st = (project_dir / rel).stat()
        except OSError:
            continue
        watched[(st.st_dev, st.st_ino)] = rel

    found: list[str] = []
    try:
        entries = os.listdir(proc)
    except OSError:
        return []
    for pid in entries:
        if not pid.isdigit():
            continue
        try:
            comm = (proc / pid / "comm").read_text().strip()
        except OSError:
            continue
        if comm != "ninja":
            continue
        try:
            if os.readlink(proc / pid / "cwd") == str(project_dir):
                found.append(f"{pid} (cwd)")
                continue
        except OSError:
            pass
        if not watched:
            continue
        fd_dir = proc / pid / "fd"
        try:
            fds = os.listdir(fd_dir)
        except OSError:
            continue
        for fd in fds:
            try:
                st = os.stat(fd_dir / fd)
            except OSError:
                continue
            rel = watched.get((st.st_dev, st.st_ino))
            if rel:
                found.append(f"{pid} (open {rel})")
                break
    return sorted(found)


def audit(project_dir: Path) -> tuple[str, list[Offender]]:
    """Return (note, offenders).  Raises on an unestablishable state.

    `offenders` is a list of human-readable lines, one per unit that is
    credited complete without a base object to have measured.
    """
    project_dir = Path(project_dir).resolve()
    units = load_units(project_dir)

    if not units:
        raise EmptyPopulationError(
            f"REFUSING TO VOUCH FOR {project_dir}: {CONFIG_NAME} declares ZERO "
            f"units. Every per-unit assertion is vacuously true over an empty "
            f"list, and `complete_units: 0 / 0` renders as 100%. This is the "
            f"'nothing measured' state, reported as such rather than as OK."
        )

    population = [u for u in units if is_complete(u)]
    if not population:
        raise EmptyPopulationError(
            f"REFUSING TO VOUCH FOR {project_dir}: none of the {len(units)} "
            f"units in {CONFIG_NAME} carry `metadata.complete: true`, so this "
            f"check examined nothing. dc3 had 968 of 2,224 on 2026-08-23; a "
            f"drop to zero means the config was regenerated wrong, not that "
            f"the hatch is closed. If a consumer genuinely has no complete "
            f"units, it does not need this guard on its report edge."
        )

    offenders: list[Offender] = []
    for unit in population:
        name = unit.get("name", "<unnamed>")
        base_path = unit.get("base_path")
        if not base_path:
            offenders.append(Offender(
                name, SHAPE_NO_BASE_PATH,
                f"    {name}\n"
                f"      no `base_path` -- objdiff credits EVERY function in "
                f"this unit at 100% (report.rs: `base.is_none() && "
                f"object.complete` => 100.0) with nothing compared."
            ))
            continue
        p = project_dir / base_path
        if not p.exists():
            offenders.append(Offender(
                name, SHAPE_MISSING,
                f"    {name}\n"
                f"      base_path `{base_path}` does not exist on disk. The "
                f"unit is marked complete, so its code is credited whole; "
                f"today objdiff errors on the missing read rather than "
                f"crediting, but that is a property of the binary, not a "
                f"contract."
            ))
            continue
        if p.stat().st_size == 0:
            offenders.append(Offender(
                name, SHAPE_EMPTY,
                f"    {name}\n"
                f"      base_path `{base_path}` is ZERO BYTES. A .cpp that "
                f"stopped emitting an object (see task #142, Tex_Wgpu.cpp) "
                f"lands here, and the unit's credit is unbacked."
            ))

    note = (
        f"{len(population) - len(offenders)}/{len(population)} `complete: true` "
        f"units have a non-empty base object "
        f"({len(units)} units examined in {CONFIG_NAME})"
    )
    return note, offenders


def check(project_dir: Path, ordered_after_compile: bool = False) -> str:
    """Return a one-line note, or raise naming every offending unit.

    `ordered_after_compile` asserts that the caller has already established
    quiescence -- see `--ordered-after-compile`.  It makes the answer strict and
    definitive: an offender is an offender, and a build running in this tree is
    reported as context rather than accepted as an explanation.
    """
    project_dir = Path(project_dir).resolve()
    note, offenders = audit(project_dir)
    if not offenders:
        return note

    # Only the disk shapes can be mid-write.  A missing `base_path` KEY is a
    # config defect; no compiler is ever halfway through producing one, so a
    # build in flight must not launder it -- and a mix of the two is red for
    # the config offender's sake.
    all_disk = all(o.shape in DISK_SHAPES for o in offenders)
    in_flight = ninja_builds_in_flight(project_dir)

    if all_disk and in_flight and not ordered_after_compile:
        raise BuildInFlightError(
            f"CANNOT ESTABLISH STATE FOR {project_dir}: {len(offenders)} "
            f"`complete: true` unit(s) have a missing or ZERO-BYTE base object, "
            f"but a ninja is building this tree right now "
            f"(pid {', '.join(in_flight)}).\n\n"
            + "\n".join(str(o) for o in offenders)
            + f"\n\n{note}\n\n"
            f"An object being written by cl.exe is present at zero bytes for a "
            f"median of 58 ms (measured 2026-08-23; 169 such windows in one "
            f"220-unit incremental build), which is byte-for-byte the same disk "
            f"shape as a .cpp that stopped emitting an object. This is NOT a "
            f"pass -- it is a refusal to guess which of the two it is. Re-run "
            f"once the build is quiescent: you will get 0 or 1, and 1 is real.\n"
            f"(The ninja edge in tools/project.py does not reach this branch: "
            f"it is order-only after `all_source` and `post-compile` and passes "
            f"--ordered-after-compile, so it gets the strict answer.)"
        )

    context = ""
    if in_flight:
        why = []
        if ordered_after_compile:
            why.append(
                "this run asserted --ordered-after-compile, so quiescence was "
                "already established by the build graph")
        if not all_disk:
            why.append(
                "at least one offender is a missing `base_path` KEY, which no "
                "build can be halfway through writing")
        context = (
            "\n\nNOTE: a ninja is also building this tree (pid "
            + ", ".join(in_flight)
            + "), but the verdict above is strict because "
            + "; and ".join(why)
            + "."
        )

    raise UncreditedCompleteUnitError(
        f"REFUSING TO VOUCH FOR {project_dir}: "
        f"{len(offenders)} unit(s) marked `complete: true` have no base "
        f"object, so objdiff substitutes 100% for a measurement that never "
        f"ran and the HEADLINE MOVES UP.\n\n"
        + "\n".join(str(o) for o in offenders)
        + f"\n\n{note}\n\n"
        f"Either build the missing object, or drop `complete: true` from "
        f"that unit in configure.py's unit table. Do NOT change objdiff: "
        f"the substitution is a sanctioned upstream hatch shared with "
        f"../rb3 and ../rb3-xenon."
        + context
    )


def _state_digest(project_dir: Path) -> str:
    """Digest the VERIFIED STATE, not the objects.

    Deliberately excludes object sizes and mtimes: a recompile must not move
    this stamp, or the `always` edge below would re-fire REPORT and REPORT RAW
    (~14 s) on every build and rewrite report.json on a tree where nothing
    moved -- which then churns the decomp.db metadata sync. The digest covers
    the set of `complete: true` units and the base_path each one claims, which
    is exactly what has to change for this check's verdict to change.
    """
    project_dir = Path(project_dir).resolve()
    units = load_units(project_dir)
    rows = sorted(
        f"{u.get('name', '')}\t{u.get('base_path') or ''}"
        for u in units if is_complete(u)
    )
    payload = f"complete_units_v1\t{len(units)}\t{len(rows)}\n" + "\n".join(rows)
    return hashlib.sha256(payload.encode()).hexdigest() + "\n"


class _null_ctx:
    """`with _null_ctx() as pid:` -> None.  Keeps run_case single-shaped."""

    def __enter__(self):
        return None

    def __exit__(self, *exc):
        return False


class fake_ninja_build:  # noqa: N801 - it is a context manager, not a class API
    """Run a REAL process named `ninja` with its cwd inside `project_dir`.

    The in-flight branch is only worth having if it has been watched to fire
    and watched not to, and neither is testable against a genuine 58 ms
    compile window.  So the control is a real process that the real detector
    really finds: a copy of `sleep` named `ninja`, whose `comm` is therefore
    `ninja`, started with `cwd == project_dir`.  Nothing is stubbed, monkey-
    patched, or injected -- `ninja_builds_in_flight` runs unmodified.

    Yields the pid, or None if this platform cannot host the control (no
    `/proc`, or no `sleep` to copy); a caller that gets None must SKIP and say
    so, never pass.

    Exposed at module scope so `tests/test_complete_units.py` and `--selftest`
    share one implementation of the control.
    """

    def __init__(self, project_dir: Path, seconds: int = 120):
        self.project_dir = Path(project_dir).resolve()
        self.seconds = seconds
        self._tmp = None
        self._proc = None

    def __enter__(self):
        import shutil
        import subprocess
        import tempfile
        import time

        if not Path("/proc").is_dir():
            return None
        sleep_bin = shutil.which("sleep")
        if not sleep_bin:
            return None
        self._tmp = tempfile.TemporaryDirectory()
        fake = Path(self._tmp.name) / "ninja"
        shutil.copy2(sleep_bin, fake)
        fake.chmod(0o755)
        self._proc = subprocess.Popen(
            [str(fake), str(self.seconds)], cwd=str(self.project_dir),
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        # Do not race the control itself: wait until the detector can see it.
        deadline = time.time() + 10.0
        while time.time() < deadline:
            if ninja_builds_in_flight(self.project_dir):
                return self._proc.pid
            time.sleep(0.02)
        self.__exit__(None, None, None)
        return None

    def __exit__(self, *exc):
        import time
        if self._proc is not None:
            self._proc.kill()
            self._proc.wait()
            self._proc = None
            # The verdict flips back only once the pid is gone from /proc.
            deadline = time.time() + 10.0
            while time.time() < deadline and ninja_builds_in_flight(self.project_dir):
                time.sleep(0.02)
        if self._tmp is not None:
            self._tmp.cleanup()
            self._tmp = None
        return False


def _selftest() -> int:
    """Negative control: the checker must FAIL a config that is broken.

    A guard that has never been watched fail is a claim, not a check. This
    builds synthetic configs in a temp dir -- a good one, ones whose complete
    unit has no object, one with no complete units at all -- and asserts each
    lands on its own exit code.  The last four cases are the task-#149 race:
    the same broken fixture must read INDETERMINATE with a build in flight and
    RED without one, and must stay RED either way for the shapes a build cannot
    explain.
    """
    import tempfile

    failures = []
    skipped = []

    def run_case(label: str, doc: dict, objects: dict[str, bytes],
                 expect: type[Exception] | None, *, in_flight: bool = False,
                 ordered_after_compile: bool = False):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for rel, data in objects.items():
                p = root / rel
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_bytes(data)
            (root / CONFIG_NAME).write_text(json.dumps(doc))
            with fake_ninja_build(root) if in_flight else _null_ctx() as pid:
                if in_flight and pid is None:
                    print(f"  [skip] {label}: no /proc or no `sleep` to build "
                          f"the in-flight control with -- NOT counted as a pass")
                    skipped.append(label)
                    return
                try:
                    note = check(root, ordered_after_compile=ordered_after_compile)
                except Exception as exc:  # noqa: BLE001 - classifying is the point
                    got: type[Exception] | None = type(exc)
                    detail = str(exc).splitlines()[0]
                else:
                    got, detail = None, note
            ok = (got is expect) if expect is None else issubclass(got or type(None), expect)
            print(f"  [{'ok' if ok else 'FAIL'}] {label}: "
                  f"expected {expect.__name__ if expect else 'pass'}, "
                  f"got {got.__name__ if got else 'pass'} -- {detail[:90]}")
            if not ok:
                failures.append(label)

    def unit(name, base_path, complete):
        u = {"name": name, "target_path": "t/x.obj",
             "metadata": {"complete": complete}}
        if base_path is not None:
            u["base_path"] = base_path
        return u

    print("verify_complete_units --selftest")
    run_case(
        "good config passes",
        {"units": [unit("a", "b/a.obj", True), unit("b", "b/b.obj", False)]},
        {"b/a.obj": b"\x01", "b/b.obj": b"\x01"},
        None,
    )
    run_case(
        "complete unit with NO base_path is caught",
        {"units": [unit("a", None, True), unit("b", "b/b.obj", False)]},
        {"b/b.obj": b"\x01"},
        UncreditedCompleteUnitError,
    )
    run_case(
        "complete unit whose object was deleted is caught",
        {"units": [unit("a", "b/a.obj", True)]},
        {},
        UncreditedCompleteUnitError,
    )
    run_case(
        "complete unit whose object is zero bytes is caught",
        {"units": [unit("a", "b/a.obj", True)]},
        {"b/a.obj": b""},
        UncreditedCompleteUnitError,
    )
    run_case(
        "no complete units at all is a refusal, not a pass",
        {"units": [unit("a", "b/a.obj", False)]},
        {"b/a.obj": b"\x01"},
        EmptyPopulationError,
    )
    run_case(
        "zero units is a refusal, not a pass",
        {"units": []},
        {},
        EmptyPopulationError,
    )
    run_case(
        "typo'd units key is a refusal, not a pass",
        {"unit": [unit("a", "b/a.obj", True)]},
        {"b/a.obj": b"\x01"},
        MissingConfigError,
    )

    # -- task #149: the mid-write race, and the four ways it must NOT be a
    #    licence to go quiet.  Same fixture, four verdicts.
    zero_byte = ({"units": [unit("a", "b/a.obj", True)]}, {"b/a.obj": b""})
    run_case(
        "ZERO-BYTE object WITH a build in flight is INDETERMINATE, not red",
        *zero_byte, BuildInFlightError, in_flight=True,
    )
    run_case(
        "...and the SAME fixture with no build in flight is still RED",
        *zero_byte, UncreditedCompleteUnitError,
    )
    run_case(
        "...and --ordered-after-compile is RED even with a build in flight",
        *zero_byte, UncreditedCompleteUnitError,
        in_flight=True, ordered_after_compile=True,
    )
    run_case(
        "a missing `base_path` KEY is RED even with a build in flight "
        "(no compiler is halfway through writing a config key)",
        {"units": [unit("a", None, True)]}, {},
        UncreditedCompleteUnitError, in_flight=True,
    )

    total = 11
    if failures:
        print(f"SELFTEST FAILED: {len(failures)} case(s): {', '.join(failures)}")
        return 1
    if skipped:
        print(f"SELFTEST INCOMPLETE: {len(skipped)}/{total} case(s) skipped "
              f"(no in-flight control on this platform): {', '.join(skipped)}")
        return 3
    print(f"SELFTEST PASSED: {total}/{total} cases -- 1 that must pass, 9 that "
          f"must FAIL the checker, and 1 that must fail it DIFFERENTLY "
          f"(indeterminate, not red) because a build was in flight.")
    return 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--project-dir", default=str(REPO_ROOT),
                    help="Project root to operate on (default: this repo)")
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true",
                      help="Exit non-zero if any `complete: true` unit lacks a "
                           "base object")
    mode.add_argument("--selftest", action="store_true",
                      help="Run the negative control: the checker must refuse "
                           "a deliberately broken config")
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--ordered-after-compile", action="store_true",
                    help="Assert that the CALLER has already established "
                         "quiescence, and answer strictly: a missing or empty "
                         "object is an offender (exit 1) even if a ninja is "
                         "alive in this tree, instead of exit 6. Pass this ONLY "
                         "from a build edge that carries an order-only "
                         "dependency on every compile edge -- tools/project.py's "
                         "`complete_units_check` edge does, and "
                         "tests/test_complete_units.py asserts the flag and "
                         "those deps travel together. Outside such an edge it "
                         "re-opens the mid-write race (task #149).")
    ap.add_argument("--stamp-out", default=None,
                    help="With --check: write a digest of the verified state to "
                         "this path, but ONLY when it differs. The ninja edge is "
                         "`always`-dirty by design (a vanished object is "
                         "mtime-invisible to objdiff.json), so without "
                         "write-if-changed + restat every build would re-run "
                         "REPORT and REPORT RAW on a tree where nothing moved.")
    args = ap.parse_args(argv)

    if args.selftest:
        return _selftest()

    project_dir = Path(args.project_dir).resolve()

    try:
        note = check(project_dir,
                     ordered_after_compile=args.ordered_after_compile)
    except UncreditedCompleteUnitError as exc:
        print(f"[complete-units-guard] {exc}", file=sys.stderr)
        return EXIT_UNCREDITED
    except BuildInFlightError as exc:
        print(f"[complete-units-guard] {exc}", file=sys.stderr)
        return EXIT_INDETERMINATE
    except EmptyPopulationError as exc:
        print(f"[complete-units-guard] {exc}", file=sys.stderr)
        return EXIT_EMPTY
    except MissingConfigError as exc:
        print(f"[complete-units-guard] {exc}", file=sys.stderr)
        return EXIT_NO_CONFIG

    if args.stamp_out:
        digest = _state_digest(project_dir)
        out = Path(args.stamp_out)
        out.parent.mkdir(parents=True, exist_ok=True)
        if not out.exists() or out.read_text() != digest:
            out.write_text(digest)

    if not args.quiet:
        print(f"[complete-units-guard] {note}")
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
