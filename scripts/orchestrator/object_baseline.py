#!/usr/bin/env python3
"""Per-unit content fingerprints of BOTH sides of every objdiff pair.

WHY THIS EXISTS
===============
`callee_gate.ensure_current_scan()` refuses a pattern scan whose recorded
`tool_version` disagrees with the installed `bin/objdiff-cli`, whose
`tree_verified` is 0, or that was written from a worktree.  Every one of those
is about the INSTRUMENT or the BOOKKEEPING.  None of them is about the two
things the scan actually measured:

  * the TARGET objects, `build/373307D9/obj/**.obj`, written by `dtk xex split`
    from `config/373307D9/symbols.txt`.  They are an undeclared ninja output
    (docs/decomp/patterns/target-objects-are-an-undeclared-build-output.md); a
    symbols.txt edit rewrites them and the recorded scan is then a set of
    findings about objects that no longer exist.
  * the BASE objects, `build/373307D9/src/**.obj`.  Any ordinary source commit
    rebuilds some of them, and the scan's per-function rows are then against
    bytes that have moved.

Both were measured, not hypothesised.  Scan 14 held three callee-class rows
against base objects that no longer existed.  The refresh that was supposed to
repair it (scan 16) raced a landing merge: it recorded `build_rev b91fc0cb5`,
fixed 3 stale rows and INTRODUCED 6.  A whole-repo commit rev is therefore not
provenance for a scan -- on an active repo it names *a* commit from the window,
not *the* tree that was diffed.

WHY PER-UNIT AND NOT A TREE VERDICT
===================================
A single tree-wide "the objects moved" boolean is permanently red on a repo
several lanes are landing into, and a guard that is always red is a guard people
route around -- this project has the receipts (`skip_budget.txt`, the ratchet
that was disarmable by rewording its own budget file).  A unit is the natural
grain: it is exactly one target object and one base object, it is the grain
objdiff itself diffs, and it is the grain `functions.unit` already carries.  So
the answer to "is this scan current?" becomes a LIST OF UNITS, and a caller that
only cares about `default/system/obj/Dir` can ask about that one unit while the
rest of the tree churns.

WHAT IS HASHED, AND WHAT IS NOT
===============================
Content, always -- never mtime.  `pacman` restores upstream mtimes and ninja's
staleness model is "input newer than output", so an mtime-keyed check is blind
by construction (CLAUDE.md, the 2026-09-01 toolchain incident).  Cost measured
on this tree: 3,203 objects / 165 MB, **0.12 s** serial, which is why this does
not bother with a parallel pool or a mtime fast path.

`objdiff.json` is the unit list because it is the same file objdiff reads: the
pairs fingerprinted here are exactly the pairs that were diffed.  A unit whose
object is absent fingerprints as `None`, which is a value, not an error -- 1,234
of this tree's 2,224 units have no base object at all (target-only library
units), and "absent then, absent now" is a match.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]

#: The unit list objdiff itself reads.  Not a second, hand-maintained list.
UNIT_CONFIG = "objdiff.json"

#: A unit's two sides.  These are the dict keys used everywhere below and the
#: two column names in `pattern_scan_units`.
SIDES = ("target", "base")

#: Directions a unit can be stale in.  The first three are hash disagreements;
#: the last two are set disagreements between the recorded scan and the tree.
DIRECTIONS = ("target", "base", "both", "unrecorded", "removed")

#: Which directions implicate each side.  `unrecorded` and `removed` implicate
#: both: the unit is in one list and not the other, so neither object can be
#: vouched for.
TARGET_DIRECTIONS = frozenset({"target", "both", "unrecorded", "removed"})
BASE_DIRECTIONS = frozenset({"base", "both", "unrecorded", "removed"})


class ObjectBaselineError(RuntimeError):
    """The unit list or the objects behind it could not be established."""


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def unit_paths(project_dir: Path | str = REPO_ROOT) -> dict[str, dict[str, str]]:
    """`unit name -> {"target": rel path, "base": rel path}` from objdiff.json.

    Paths are kept RELATIVE so a fingerprint taken in one checkout is comparable
    with one taken in another -- unlike `patch_state.json`'s keys, which are also
    relative, and unlike `S_OBJNAME`, which is not (see CLAUDE.md: `tree_sha256`
    is deliberately not comparable across worktrees).
    """
    cfg = Path(project_dir) / UNIT_CONFIG
    if not cfg.exists():
        raise ObjectBaselineError(
            f"{cfg} not found: without objdiff's own unit list there is no way to "
            f"say which target object pairs with which base object, and inventing "
            f"a second list is how the two drift apart.")
    try:
        doc = json.loads(cfg.read_text())
    except (OSError, ValueError) as e:
        raise ObjectBaselineError(f"{cfg} is unreadable: {e}") from e
    units = doc.get("units") or []
    if not units:
        raise ObjectBaselineError(
            f"{cfg} lists ZERO units -- refusing to fingerprint an empty tree. "
            f"An empty baseline matches every future tree and would read GREEN "
            f"forever, which is the failure this module exists to prevent.")
    out: dict[str, dict[str, str]] = {}
    for u in units:
        name = u.get("name")
        if not name:
            continue
        out[name] = {"target": u.get("target_path") or "",
                     "base": u.get("base_path") or ""}
    return out


def fingerprint_units(project_dir: Path | str = REPO_ROOT,
                      units: dict[str, dict[str, str]] | None = None,
                      ) -> dict[str, dict[str, str | None]]:
    """Content-hash both objects of every unit.  ~0.12 s over 3,203 objects.

    A missing object hashes to `None`.  That is deliberately a VALUE and not an
    omission: "this unit had no base object when the scan ran, and still has
    none" is a match, while "it had one and now it does not" is drift, and only
    a recorded `None` can tell those apart.
    """
    root = Path(project_dir)
    units = units if units is not None else unit_paths(root)
    out: dict[str, dict[str, str | None]] = {}
    for name, rels in units.items():
        row: dict[str, str | None] = {}
        for side in SIDES:
            rel = rels.get(side) or ""
            p = root / rel if rel else None
            row[side] = sha256(p) if (p is not None and p.is_file()) else None
        out[name] = row
    return out


def race_units(before: dict[str, dict[str, str | None]],
               after: dict[str, dict[str, str | None]]) -> set[str]:
    """Units whose objects moved BETWEEN two fingerprints of the same tree.

    The census brackets its sweep with two reads for the same reason it brackets
    the objdiff `--version`: some part of the measurement may have been taken
    against bytes that no longer exist, and a row that claims a baseline it did
    not measure defeats the very check it is feeding.  Such a unit is recorded
    `raced = 1` rather than dropped -- its findings are real, their baseline is
    just not attributable.
    """
    return {u for u in set(before) | set(after) if before.get(u) != after.get(u)}


def compare_fingerprints(recorded: dict[str, dict[str, str | None]],
                         live: dict[str, dict[str, str | None]],
                         ) -> dict[str, str]:
    """`unit -> direction` for every unit that has moved.  Empty == current.

    Directions: `target`, `base`, `both` (hash disagreements), `unrecorded` (the
    tree has a unit the scan never fingerprinted -- a source file added since)
    and `removed` (the scan fingerprinted a unit the tree no longer has).
    """
    stale: dict[str, str] = {}
    for unit in sorted(set(recorded) | set(live)):
        was, now = recorded.get(unit), live.get(unit)
        if was is None:
            stale[unit] = "unrecorded"
            continue
        if now is None:
            stale[unit] = "removed"
            continue
        moved = [s for s in SIDES if was.get(s) != now.get(s)]
        if not moved:
            continue
        stale[unit] = "both" if len(moved) == 2 else moved[0]
    return stale


def counts_by_direction(stale: dict[str, str]) -> dict[str, int]:
    out = {d: 0 for d in DIRECTIONS}
    for d in stale.values():
        out[d] = out.get(d, 0) + 1
    return {k: v for k, v in out.items() if v}


def sides_touched(stale: dict[str, str]) -> tuple[list[str], list[str]]:
    """`(units stale on the target side, units stale on the base side)`."""
    target = sorted(u for u, d in stale.items() if d in TARGET_DIRECTIONS)
    base = sorted(u for u, d in stale.items() if d in BASE_DIRECTIONS)
    return target, base


def render_stale(stale: dict[str, str], *, limit: int = 20,
                 indent: str = "  ") -> str:
    """A count, a per-direction breakdown, and the first `limit` units by name.

    Always a COUNT before a list: a truncated list read as a total is how a
    partial measurement becomes a claim about the binary.
    """
    if not stale:
        return f"{indent}0 units stale (both object baselines match the scan)"
    counts = counts_by_direction(stale)
    head = (f"{indent}{len(stale)} of the scan's units have moved since it ran: "
            + ", ".join(f"{n} {d}" for d, n in sorted(counts.items())))
    listed = sorted(stale.items())
    body = "".join(f"\n{indent}  {d:11s} {u}" for u, d in listed[:limit])
    more = ("" if len(listed) <= limit
            else f"\n{indent}  ... and {len(listed) - limit} more "
                 f"(pass a larger --list-stale to see them)")
    return head + body + more
