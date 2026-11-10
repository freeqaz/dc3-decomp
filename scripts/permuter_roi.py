#!/usr/bin/env python3
"""Before you spend 600 builds on the permuter, ask whether it can pay.

WHY THIS EXISTS
===============
Measured 2026-11-10, four lanes in a row:

  * ``MoveDir::UpdateOverlay`` -- 595 non-equal rows. decomp-synth found a real
    chain in 4 depths (``and_split -> decl_reorder -> decl_move``, 87.60 ->
    88.32). Permuter: WORTH IT.
  * ``HollaBackMinigame::BeginMinigame`` -- 2 non-equal rows, both ``diff_arg``,
    a register-swap pair (r11<->r9) on a dead operand. 862 viable variants
    across the focused set and the full 112-pattern set, **0 improved**.
  * ``ProcCounter::SetEmulateFPS`` -- 1 insert + 1 delete, and they were the
    SAME instruction (``mr r3, r4``) at two different positions. 5 rounds,
    ~52 variants each, **0 improved**.
  * ``RndMesh::TransformNormal`` -- 23 ``diff_arg`` rows. Improved, but by a
    HAND lever (written addend order), not by the permuter.

The tool's own verdict text says "Do NOT accept before running the permuter
-- these patterns are typically permuter-class" *unconditionally*, including on
rows where the entire residual is one instruction's placement. This script is
the missing discriminator.

WHAT IT DOES NOT DO
===================
It is a HEURISTIC priced from four measured rows, not a law. It classifies the
row shape; it cannot tell you whether a particular insert/delete is a missing
block or a displaced one. A ``NO LEVERAGE`` answer means "expect zero, and be
surprised if you get more" -- it does not forbid the run. A ``WORTH IT`` answer
does not promise a win.

USAGE
=====
    scripts/permuter_roi.py '?SetEmulateFPS@ProcCounter@@AAAIH@Z'
    scripts/permuter_roi.py --diff-json /tmp/fn.json      # no rebuild
    scripts/permuter_roi.py --symbol ... --project-dir <worktree>

Exit codes:  0 = permuter is worth running   1 = no leverage, expect zero
             2 = could not measure

Every run says which of the three it is, so "no leverage" is never confused
with "could not look" -- the failure mode that has burned this project.
"""

import argparse
import json
import os
import sys
import tempfile

# Row kinds as objdiff reports them.
_DIFF_ARG = "diff_arg"
_INSERT = "insert"
_DELETE = "delete"
_REPLACE = "replace"
_DIFF_OP = "diff_op"


_EQUAL = "equal"


def _rows_from_objdiff_json(doc):
    """Normalise objdiff's JSON into [(idx, kind, mnemonic, operands)].

    The REAL shape (verified against objdiff-cli 4.2.9 output on
    ``?SetEmulateFPS@ProcCounter@@AAAIH@Z``, 2026-11-10) is::

        "instructions": [
          {"index": 34, "target": {"opcode": "mr", "args": "r3, r4"},
           "match_type": "delete"},
          {"index": 54, "base":   {"opcode": "mr", "args": "r3, r4"},
           "match_type": "insert"},
          ...
        ]

    ``match_type`` is one of ``equal``/``diff_arg``/``diff_op``/``replace``/
    ``delete``/``insert``. A ``delete`` row carries only ``target`` and an
    ``insert`` row only ``base`` -- so the instruction text must be read from
    whichever side is present, which is also exactly what makes the
    same-instruction-moved test work.

    An earlier draft of this parser guessed a ``left``/``right`` +
    ``diff_kind`` schema that objdiff does not emit; it returned zero rows and
    the tool said ``CANNOT MEASURE`` rather than classifying nothing, which is
    why the empty case below is a hard error and not an empty list.
    """
    raw = doc.get("instructions")
    if not isinstance(raw, list):
        return []

    rows = []
    for item in raw:
        if not isinstance(item, dict):
            continue
        kind = item.get("match_type")
        if not kind or kind == _EQUAL:
            continue
        ins = item.get("target") or item.get("base") or {}
        op = (ins.get("opcode") or "").strip()
        args = (ins.get("args") or "").strip()
        rows.append((item.get("index"), kind, op, args))

    rows.sort(key=lambda r: (r[0] if isinstance(r[0], int) else 0, r[1]))
    return rows


def classify(rows):
    """Classify a normalised row table. Pure, so it is directly testable.

    Returns (verdict, reason, detail) where verdict is one of
    "no_leverage", "worth_it", "unclear".
    """
    kinds = [r[1] for r in rows]
    n = len(rows)
    inserts = [r for r in rows if r[1] == _INSERT]
    deletes = [r for r in rows if r[1] == _DELETE]
    diff_args = [r for r in rows if r[1] == _DIFF_ARG]
    others = [r for r in rows if r[1] not in (_INSERT, _DELETE, _DIFF_ARG)]

    # 1. The same instruction at two positions: c2 copy placement.
    #    Measured on ProcCounter::SetEmulateFPS (2026-11-10): DELETE `mr r3, r4`
    #    at idx 34, INSERT `mr r3, r4` at idx 54 -- the whole 3.4-point gap.
    if len(inserts) == 1 and len(deletes) == 1 and n == 2:
        i_op, i_args = inserts[0][2], inserts[0][3]
        d_op, d_args = deletes[0][2], deletes[0][3]
        if (i_op, i_args) == (d_op, d_args):
            return (
                "no_leverage",
                "MOVED_INSTRUCTION_TIE: the one insert and the one delete are the "
                "SAME instruction at two positions",
                f"`{i_op} {i_args}` deleted at idx {deletes[0][0]}, "
                f"inserted at idx {inserts[0][0]}",
            )

    # 2. A couple of pure register swaps and nothing else.
    #    Measured on HollaBackMinigame::BeginMinigame (2026-11-10): 2 diff_arg,
    #    r11<->r9 on a dead `lwzx` operand; image itself splits 3-way/1-way
    #    across four textually identical call sites, so no text can decide it.
    if n <= 2 and len(diff_args) == n:
        return (
            "no_leverage",
            "DEAD_OPERAND_SWAP: at most two rows and all of them are register "
            "swaps -- a colouring tie, not a rearrangement",
            "; ".join(f"idx {r[0]}: `{r[2]} {r[3]}`" for r in diff_args),
        )

    # 3. Enough structural material for a search to have something to move.
    #    `MoveDir::UpdateOverlay` (595 rows, 50 ins + 60 del as ~48 clusters)
    #    improved within 4 depths. TransformNormal (23 diff_arg) improved by
    #    hand; the permuter was not needed, so this band is only "clear".
    if n >= 20 and (inserts or deletes or others):
        return (
            "worth_it",
            "STRUCTURAL: enough non-equal rows of mixed kinds that a beam search "
            "has real rearrangement freedom",
            f"{n} rows: {len(diff_args)} diff_arg, {len(inserts)} insert, "
            f"{len(deletes)} delete, {len(others)} other",
        )

    if n >= 20:
        return (
            "worth_it",
            "MANY_REGISTER_SWAPS: a large swap set usually has one liveness or "
            "scheduling cause, but the PERMUTER is not the tool for that -- fix "
            "the cause (search runs are cheap here, expect low yield)",
            f"{n} rows, all diff_arg",
        )

    return (
        "unclear",
        "UNCLEAR: too few rows to price from shape alone",
        f"{n} rows: {len(diff_args)} diff_arg, {len(inserts)} insert, "
        f"{len(deletes)} delete, {len(others)} other",
    )


def measure(symbol, project_dir):
    """Run objdiff for one symbol and return the normalised row table."""
    sys.path.insert(0, os.path.join(project_dir, "scripts"))
    from orchestrator import context_collector  # noqa: E402

    tmp = tempfile.mkdtemp(prefix="permuter-roi-")
    res = context_collector.run_objdiff_cli(symbol, project_dir, tmp)
    if not res.get("success"):
        raise RuntimeError(res.get("error") or "objdiff failed")
    doc = res.get("json")
    if not doc:
        # fall back to the file it wrote
        path = res.get("output_file_absolute")
        if path and os.path.exists(path):
            doc = json.load(open(path))
    if not doc:
        raise RuntimeError("objdiff produced no JSON")
    out = _rows_from_objdiff_json(doc)
    if not out:
        raise RuntimeError("objdiff JSON had no instruction rows (schema drift?)")
    return out, res.get("match_percent")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("symbol", nargs="?", help="mangled symbol")
    ap.add_argument("--symbol", dest="symbol_opt", help=argparse.SUPPRESS)
    ap.add_argument("--project-dir", default=os.getcwd())
    ap.add_argument("--diff-json", help="parse a saved objdiff JSON instead of building")
    args = ap.parse_args()

    symbol = args.symbol or args.symbol_opt

    if args.diff_json:
        if not os.path.exists(args.diff_json):
            print(f"CANNOT MEASURE: no such file {args.diff_json}", file=sys.stderr)
            return 2
        doc = json.load(open(args.diff_json))
        rows = _rows_from_objdiff_json(doc)
        pct = None
        if not rows:
            print("CANNOT MEASURE: that JSON has no instruction rows "
                  "(wrong schema, or a data symbol?)", file=sys.stderr)
            return 2
    else:
        if not symbol:
            ap.error("give a symbol or --diff-json")
        try:
            rows, pct = measure(symbol, args.project_dir)
        except Exception as exc:  # noqa: BLE001 - report, never pretend to know
            print(f"CANNOT MEASURE: {exc}", file=sys.stderr)
            return 2

    verdict, reason, detail = classify(rows)

    print(f"symbol: {symbol or args.diff_json}")
    if pct is not None:
        print(f"match:  {pct}")
    print(f"rows:   {len(rows)} non-equal")
    print(f"detail: {detail}")
    print()
    if verdict == "no_leverage":
        print("PERMUTER: NO LEVERAGE -- expect zero. The residual is a placement "
              "tie, not a rearrangement.")
        print(f"  {reason}")
        print("  This is a heuristic priced from measured rows, not a law: run it "
              "anyway if you have a concrete hypothesis.")
        return 1
    if verdict == "worth_it":
        print("PERMUTER: WORTH RUNNING.")
        print(f"  {reason}")
        print("  Remember the false-gain rule: compare the EQUAL row count, not "
              "just the headline.")
        return 0
    print("PERMUTER: UNCLEAR.")
    print(f"  {reason}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
