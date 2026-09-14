#!/usr/bin/env python3
"""Do the two sides CALL the same things the same number of times?

WHY THIS EXISTS
===============
objdiff's ``WRONG_CALLEE`` detector reports a call site where the target names
one callee and our build names another.  In a function whose blocks are laid
out differently from the image, that pairing is objdiff's *alignment guess*,
not a source fact: once an insert/delete cluster shifts the two instruction
streams relative to each other, the ok-path ``bl atoi`` lines up against the
fail-path ``bl ??1String@@UAA@XZ`` and the detector fires ``LikelyFixable`` on
a function that calls exactly the right things.

The discriminator is cheap and the detector does not apply it:

    if our build emits callee X exactly as many times as the image does,
    then "we call Y where the image calls X" cannot be a missing or
    substituted call.  It is a layout difference.

MEASURED PROVENANCE (w6-e lane, 2026-09-14, dc3-decomp @ 4a326188e)
-------------------------------------------------------------------
A freshly derived ``name_check`` census (48,290 of 48,348 functions examined)
put the adjudicable population at **7 net WRONG_CALLEE rows**.  Of those 7:

  * **1 was a real defect** -- ``ParseStatusCode`` (HttpGet), fixed in 55402ce2c.
  * **6 were artifacts**, and **5 of the 6 were settled by this test alone**:
      ``CacheResource``      MovieExtension  1 vs 1   (block swap)
      ``Trie::remove``       see below                (retail tail-merge)
      ``TransformCrowd <<``  Save            1 vs 1   (inline depth)
      ``StartVoiceThreadEntry`` GetTickCount 1 vs 1   (frame drift)
      ``mmioSetBuffer``      LocalReAlloc    1 vs 1   (loop shape)
    The 6th (``MoveDir::UpdateOverlay``) was already adjudicated in-source.

THE ACCUSED CALLEE IS THE QUESTION, NOT THE WHOLE MULTISET
----------------------------------------------------------
``CacheResource`` is why this distinction is load-bearing.  Its *full* multiset
DIFFERS -- ``??1String@@UAA@XZ`` is 1 in the image and 2 in our build -- while
the **accused** callee ``?MovieExtension@@...`` is 1 on both sides.  A tool that
only answered "are the multisets equal?" would have called that row unresolved
and sent the lane hunting a missing ``MovieExtension`` call that is right there.
So pass ``--accused`` with the name from the WRONG_CALLEE row and read the
per-callee verdict; the whole-multiset delta is context, not the answer.

WHAT A VERDICT DOES AND DOES NOT PROVE
--------------------------------------
``EQUAL`` proves the name is not missing and not substituted.  It does NOT
prove the call site is in the right place, that the arguments are right, or
that the function matches -- ``Trie::remove`` is 85.5%% with an equal
``check_index`` count.  It retires the *wrong-callee* hypothesis only.

``DIFFERS`` is a lead, not a defect.  Retail tail-merging and our own failure
to share a cleanup block both show up here.  ``Trie::remove`` is the worked
example: 5 ``delete_node`` call sites in source, 4 ``bl``s in the image,
because retail merged ``delete_node(1)`` into the shared
``delete_node(curIdx)`` (``li r4,0x1; b .L_827FE708``).  Same callee, same
argument, same behaviour.  Read a DIFFERS and go look at the listing.

THE SELFTEST WAS SABOTAGED BEFORE IT WAS BELIEVED
--------------------------------------------------
``--selftest`` pins the comparator in BOTH directions, so neither a
constant-EQUAL nor a constant-DIFFERS implementation survives it.  Four
deliberate defects, all caught (exit 1), 2026-09-14:

  * ``compare()`` returns ``{}``                    -> caught
  * ``compare()`` returns a fixed non-empty delta   -> caught
  * ``verdict_for()`` always returns EQUAL          -> caught
  * extraction yields empty multisets               -> caught (degeneracy guard)

That guard matters more than it looks: if the JSON shape changes and
extraction silently yields nothing, every function compares "equal" and the
tool becomes a rubber stamp for exactly the hypothesis it exists to test.

It earned its keep immediately.  This lane's first ``Trie::remove`` writeup
claimed "5 delete_node call sites here, 4 in the image" -- comparing the
image's EMITTED count against SOURCE call sites.  The selftest failed on
those numbers and forced the correction: our object emits 4 too, and the real
asymmetry is ``check_index`` at 29 vs 30.  A hand count of source text is not
a measurement of what was emitted.

READ-ONLY.  Touches ``report.json`` (unit lookup) and the two object files.
It never opens ``decomp.db`` and never writes anything.
"""

from __future__ import annotations

import argparse
import collections
import json
import os
import subprocess
import sys
from pathlib import Path

TITLE = "373307D9"


# --------------------------------------------------------------------------
# extraction
# --------------------------------------------------------------------------
def project_root(explicit: str | None) -> Path:
    if explicit:
        return Path(explicit).resolve()
    env = os.environ.get("REPO_ROOT")
    if env:
        return Path(env).resolve()
    return Path(__file__).resolve().parents[2]


def unit_of_symbol(root: Path, symbol: str) -> str:
    """Find which unit defines `symbol`, from report.json. Read-only."""
    report = root / "build" / TITLE / "report.json"
    if not report.is_file():
        raise SystemExit(f"no report.json at {report} -- run a full `ninja` first")
    data = json.loads(report.read_text())
    hits = [
        u["name"]
        for u in data.get("units", [])
        for f in (u.get("functions") or [])
        if f.get("name") == symbol
    ]
    if not hits:
        raise SystemExit(f"symbol not found in report.json: {symbol}")
    if len(set(hits)) > 1:
        raise SystemExit(
            f"symbol {symbol} is defined in {len(set(hits))} units "
            f"({', '.join(sorted(set(hits))[:4])}...); pass --unit"
        )
    return hits[0]


def object_paths(root: Path, unit: str) -> tuple[Path, Path]:
    """(target_obj, base_obj) for a unit name like 'default/system/utl/trie'."""
    rel = unit.split("/", 1)[1] if unit.startswith("default/") else unit
    tgt = root / "build" / TITLE / "obj" / f"{rel}.obj"
    base = root / "build" / TITLE / "src" / f"{rel}.obj"
    for p, what in ((tgt, "target"), (base, "base")):
        if not p.is_file():
            raise SystemExit(f"missing {what} object: {p}")
    return tgt, base


def callee_multisets(
    root: Path, symbol: str, unit: str
) -> tuple[collections.Counter, collections.Counter]:
    """Count `bl <symbol>` per side. Alignment-independent: each side is
    counted on its own, never through objdiff's row pairing."""
    tgt, base = object_paths(root, unit)
    cli = root / "bin" / "objdiff-cli"
    proc = subprocess.run(
        [
            str(cli), "diff",
            "-1", str(tgt), "-2", str(base),
            symbol, "--include-instructions", "-f", "json", "-o", "-",
        ],
        capture_output=True, text=True,
    )
    if proc.returncode != 0 or not proc.stdout.strip():
        raise SystemExit(
            f"objdiff-cli failed for {symbol} in {unit}: "
            f"{(proc.stderr or '').strip()[:300]}"
        )
    rows = json.loads(proc.stdout).get("instructions", [])
    out = {}
    for side in ("target", "base"):
        c: collections.Counter = collections.Counter()
        for row in rows:
            ins = row.get(side)
            if not ins or ins.get("opcode") != "bl":
                continue
            for a in ins.get("typed_args") or []:
                if a.get("type") == "Symbol":
                    c[a["value"]] += 1
                    break
        out[side] = c
    return out["target"], out["base"]


# --------------------------------------------------------------------------
# comparison  (pure -- the part the sabotage control exercises)
# --------------------------------------------------------------------------
def compare(
    tgt: collections.Counter, base: collections.Counter
) -> dict[str, tuple[int, int]]:
    """Every callee whose counts disagree -> {name: (target_n, base_n)}."""
    return {
        name: (tgt.get(name, 0), base.get(name, 0))
        for name in sorted(set(tgt) | set(base))
        if tgt.get(name, 0) != base.get(name, 0)
    }


def verdict_for(
    accused: str, tgt: collections.Counter, base: collections.Counter
) -> tuple[str, int, int]:
    t, b = tgt.get(accused, 0), base.get(accused, 0)
    if t == b and t > 0:
        return "EQUAL", t, b
    if t == b == 0:
        return "ABSENT_BOTH", t, b
    return "DIFFERS", t, b


# --------------------------------------------------------------------------
# selftest -- must be able to FAIL
# --------------------------------------------------------------------------
# Both controls are REAL, measured on this tree. They pin the comparator in
# BOTH directions, so a constant-EQUAL or constant-DIFFERS implementation
# fails one of them. The counts are the numbers this lane measured by hand
# against the dtk .s listing on 2026-09-14.
NEG_CONTROL = {  # must read DIFFERS -- we emit one bounds check the image does not
    "symbol": "?remove@Trie@@QAAXI@Z",
    "unit": "default/system/utl/trie",
    "callee": "?check_index@Trie@@QAAXI@Z",
    "target_n": 29,
    "base_n": 30,
    # If this control stops differing, there are exactly two causes and they
    # need opposite responses: (a) the extra check_index was FIXED, in which
    # case re-point the control at another measured DIFFERS row -- do not
    # delete it; or (b) extraction broke and is under-counting. Check
    # `--selftest`'s non-empty assertions above before assuming (a).
}
POS_CONTROL = {  # must read EQUAL -- the accused callee is present on both sides
    "symbol": "?CacheResource@@YAPBDPBDAAW4CacheResourceResult@@@Z",
    "unit": "default/system/rndobj/Utl",
    "callee": "?MovieExtension@@YAPBDPBDW4Platform@@@Z",
    "n": 1,
}


def selftest(root: Path) -> int:
    ok = True

    def say(good: bool, msg: str, detail: str = "") -> None:
        nonlocal ok
        ok = ok and good
        print(f"  [{'ok' if good else 'FAIL'}] {msg}{('  ' + detail) if detail else ''}")

    print("callee_multiset --selftest")

    # (1) pure-comparator sabotage: a perturbed multiset MUST be flagged.
    a = collections.Counter({"f": 2, "g": 1})
    say(compare(a, a) == {}, "identical multisets compare clean")
    say(
        compare(a, collections.Counter({"f": 1, "g": 1})) == {"f": (2, 1)},
        "a perturbed count is flagged",
    )
    say(
        compare(a, collections.Counter({"f": 2, "g": 1, "h": 1})) == {"h": (0, 1)},
        "a base-only callee is flagged",
    )

    # (2) degeneracy: extraction must actually find calls. An empty-vs-empty
    #     comparison is trivially "equal" and would make every verdict vacuous.
    try:
        t, b = callee_multisets(root, NEG_CONTROL["symbol"], NEG_CONTROL["unit"])
    except SystemExit as exc:
        say(False, "extraction ran", str(exc))
        return 1
    say(sum(t.values()) > 0, "target side is non-empty", f"{sum(t.values())} calls")
    say(sum(b.values()) > 0, "base side is non-empty", f"{sum(b.values())} calls")

    # (3) NEGATIVE CONTROL -- a case known to DIFFER, with the exact counts.
    v, tn, bn = verdict_for(NEG_CONTROL["callee"], t, b)
    say(
        v == "DIFFERS"
        and tn == NEG_CONTROL["target_n"]
        and bn == NEG_CONTROL["base_n"],
        "known-DIFFERS case reads DIFFERS with the measured counts",
        f"{NEG_CONTROL['callee'].split('@')[0]} target={tn} base={bn} "
        f"(expected {NEG_CONTROL['target_n']}/{NEG_CONTROL['base_n']})"
        + ("" if v == "DIFFERS" else "  <- either the extra call was FIXED "
           "(re-point this control) or extraction is under-counting"),
    )

    # (4) POSITIVE CONTROL -- a case known EQUAL on the accused callee, in a
    #     function whose FULL multiset differs. Pins the per-callee path.
    try:
        t2, b2 = callee_multisets(root, POS_CONTROL["symbol"], POS_CONTROL["unit"])
    except SystemExit as exc:
        say(False, "positive-control extraction ran", str(exc))
        return 1
    v2, tn2, bn2 = verdict_for(POS_CONTROL["callee"], t2, b2)
    say(
        v2 == "EQUAL" and tn2 == POS_CONTROL["n"],
        "known-EQUAL accused callee reads EQUAL",
        f"{POS_CONTROL['callee'].split('@')[0]} target={tn2} base={bn2}",
    )
    say(
        compare(t2, b2) != {},
        "...inside a function whose FULL multiset differs (per-callee path is live)",
        f"{len(compare(t2, b2))} differing callee(s)",
    )

    print("SELFTEST PASSED" if ok else "SELFTEST FAILED")
    return 0 if ok else 1


# --------------------------------------------------------------------------
def main() -> int:
    ap = argparse.ArgumentParser(
        description="Do the image and our build call the same callees the same "
        "number of times? Retires the wrong-callee hypothesis on a "
        "WRONG_CALLEE row. Read-only."
    )
    ap.add_argument("symbol", nargs="?", help="function symbol (mangled)")
    ap.add_argument("--unit", help="unit name, e.g. default/system/utl/trie")
    ap.add_argument(
        "--accused",
        action="append",
        default=[],
        help="callee name from the WRONG_CALLEE row (repeatable). This is the "
        "question; the whole-multiset delta is only context.",
    )
    ap.add_argument("--project", help="tree to measure (default: this checkout)")
    ap.add_argument("--json", help="write the full result here")
    ap.add_argument("--selftest", action="store_true", help="run the controls and exit")
    args = ap.parse_args()

    root = project_root(args.project)
    if args.selftest:
        return selftest(root)
    if not args.symbol:
        ap.error("a symbol is required (or --selftest)")

    unit = args.unit or unit_of_symbol(root, args.symbol)
    tgt, base = callee_multisets(root, args.symbol, unit)

    if not tgt and not base:
        print(f"NO CALLS: neither side emits a `bl` in {args.symbol}.")
        print("A multiset verdict here would be vacuous -- nothing was measured.")
        return 3

    delta = compare(tgt, base)
    print(f"symbol : {args.symbol}")
    print(f"unit   : {unit}")
    print(f"calls  : target {sum(tgt.values())}, base {sum(base.values())}")

    exit_code = 0
    if args.accused:
        print("\naccused callee(s) from the WRONG_CALLEE row:")
        for name in args.accused:
            v, t, b = verdict_for(name, tgt, base)
            note = {
                "EQUAL": "count matches -> NOT missing, NOT substituted. "
                         "The row is alignment, not a wrong callee.",
                "DIFFERS": "count asymmetry -> read the listing before concluding "
                           "(tail-merge and unshared cleanup look like this).",
                "ABSENT_BOTH": "neither side calls this -- check the name/unit.",
            }[v]
            print(f"  {v:11s} target={t} base={b}  {name}")
            print(f"              {note}")
            if v != "EQUAL":
                exit_code = 1

    print(f"\nfull multiset: {'IDENTICAL' if not delta else f'{len(delta)} differ'}")
    for name, (t, b) in delta.items():
        print(f"  target={t} base={b}  {name}")
    if not args.accused and delta:
        exit_code = 1

    if args.json:
        Path(args.json).write_text(
            json.dumps(
                {
                    "symbol": args.symbol,
                    "unit": unit,
                    "target": dict(tgt),
                    "base": dict(base),
                    "delta": {k: {"target": v[0], "base": v[1]} for k, v in delta.items()},
                    "accused": {
                        n: dict(zip(("verdict", "target", "base"), verdict_for(n, tgt, base)))
                        for n in args.accused
                    },
                },
                indent=2,
            )
        )
    return exit_code


if __name__ == "__main__":
    sys.exit(main())
