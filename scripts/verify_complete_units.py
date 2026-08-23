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
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

CONFIG_NAME = "objdiff.json"

EXIT_OK = 0
EXIT_UNCREDITED = 1
EXIT_NO_CONFIG = 4
EXIT_EMPTY = 5


class UncreditedCompleteUnitError(RuntimeError):
    """A `complete: true` unit has no base object, so its 100% is unmeasured."""


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


def audit(project_dir: Path) -> tuple[str, list[str]]:
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

    offenders: list[str] = []
    for unit in population:
        name = unit.get("name", "<unnamed>")
        base_path = unit.get("base_path")
        if not base_path:
            offenders.append(
                f"    {name}\n"
                f"      no `base_path` -- objdiff credits EVERY function in "
                f"this unit at 100% (report.rs: `base.is_none() && "
                f"object.complete` => 100.0) with nothing compared."
            )
            continue
        p = project_dir / base_path
        if not p.exists():
            offenders.append(
                f"    {name}\n"
                f"      base_path `{base_path}` does not exist on disk. The "
                f"unit is marked complete, so its code is credited whole; "
                f"today objdiff errors on the missing read rather than "
                f"crediting, but that is a property of the binary, not a "
                f"contract."
            )
            continue
        if p.stat().st_size == 0:
            offenders.append(
                f"    {name}\n"
                f"      base_path `{base_path}` is ZERO BYTES. A .cpp that "
                f"stopped emitting an object (see task #142, Tex_Wgpu.cpp) "
                f"lands here, and the unit's credit is unbacked."
            )

    note = (
        f"{len(population) - len(offenders)}/{len(population)} `complete: true` "
        f"units have a non-empty base object "
        f"({len(units)} units examined in {CONFIG_NAME})"
    )
    return note, offenders


def check(project_dir: Path) -> str:
    """Return a one-line note, or raise naming every offending unit."""
    note, offenders = audit(project_dir)
    if offenders:
        raise UncreditedCompleteUnitError(
            f"REFUSING TO VOUCH FOR {Path(project_dir).resolve()}: "
            f"{len(offenders)} unit(s) marked `complete: true` have no base "
            f"object, so objdiff substitutes 100% for a measurement that never "
            f"ran and the HEADLINE MOVES UP.\n\n"
            + "\n".join(offenders)
            + f"\n\n{note}\n\n"
            f"Either build the missing object, or drop `complete: true` from "
            f"that unit in configure.py's unit table. Do NOT change objdiff: "
            f"the substitution is a sanctioned upstream hatch shared with "
            f"../rb3 and ../rb3-xenon."
        )
    return note


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


def _selftest() -> int:
    """Negative control: the checker must FAIL a config that is broken.

    A guard that has never been watched fail is a claim, not a check. This
    builds three synthetic configs in a temp dir -- one good, one with a
    `complete: true` unit whose object is missing, one with no complete units
    at all -- and asserts each lands on its own exit code.
    """
    import tempfile

    failures = []

    def run_case(label: str, doc: dict, objects: dict[str, bytes],
                 expect: type[Exception] | None):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for rel, data in objects.items():
                p = root / rel
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_bytes(data)
            (root / CONFIG_NAME).write_text(json.dumps(doc))
            try:
                note = check(root)
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

    if failures:
        print(f"SELFTEST FAILED: {len(failures)} case(s): {', '.join(failures)}")
        return 1
    print("SELFTEST PASSED: 7/7 cases, including 5 that must FAIL the checker.")
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
        note = check(project_dir)
    except UncreditedCompleteUnitError as exc:
        print(f"[complete-units-guard] {exc}", file=sys.stderr)
        return EXIT_UNCREDITED
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
