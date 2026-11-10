"""Tests for scripts/permuter_roi.py.

The property that matters is NOT "it says no_leverage a lot" -- it is that the
three states stay distinct:

    0  permuter worth running
    1  no leverage (a placement tie)
    2  could not measure

because the failure this tool exists to prevent is a lane spending 600 builds on
a residual the permuter cannot touch, and the failure it must not INTRODUCE is
telling a lane to skip a row that would have moved.

Every classifier case below is a shape measured on a real function on
2026-11-10; the negatives are deliberate controls so the rule cannot pass by
classifying everything the same way.
"""

import json
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
TOOL = REPO / "scripts" / "permuter_roi.py"

sys.path.insert(0, str(REPO / "scripts"))
import permuter_roi as roi  # noqa: E402


def run(*args):
    p = subprocess.run(
        [sys.executable, str(TOOL), *args], capture_output=True, text=True, timeout=60
    )
    return p.returncode, p.stdout + p.stderr


def row(idx, kind, op, args=""):
    return (idx, kind, op, args)


# --- the three measured shapes -------------------------------------------


def test_same_instruction_moved_is_no_leverage():
    """ProcCounter::SetEmulateFPS: DELETE `mr r3, r4` idx 34, INSERT same idx 54."""
    rows = [row(34, "delete", "mr", "r3, r4"), row(54, "insert", "mr", "r3, r4")]
    verdict, reason, _ = roi.classify(rows)
    assert verdict == "no_leverage", reason
    assert "MOVED_INSTRUCTION_TIE" in reason


def test_moved_instruction_with_different_operands_is_not_a_tie():
    """The control: same SHAPE (1 ins + 1 del) but different instructions must
    NOT be called a placement tie -- that is a real missing/extra instruction."""
    rows = [row(34, "delete", "li", "r3, 0"), row(54, "insert", "mr", "r3, r4")]
    verdict, _, _ = roi.classify(rows)
    assert verdict != "no_leverage"


def test_two_register_swaps_is_no_leverage():
    """HollaBackMinigame::BeginMinigame: 2 diff_arg, r11<->r9."""
    rows = [row(119, "diff_arg", "lwzx", "r10, r11, r9"),
            row(225, "diff_arg", "lwzx", "r10, r11, r9")]
    verdict, reason, _ = roi.classify(rows)
    assert verdict == "no_leverage", reason
    assert "DEAD_OPERAND_SWAP" in reason


def test_three_register_swaps_leaves_the_tie_band():
    """Boundary control: the rule is `<= 2`, and 3 must fall out of it."""
    rows = [row(i, "diff_arg", "lwzx", "r10, r11, r9") for i in (1, 2, 3)]
    verdict, _, _ = roi.classify(rows)
    assert verdict != "no_leverage"


def test_a_large_structural_row_set_is_worth_running():
    """MoveDir::UpdateOverlay's shape: hundreds of rows, mixed kinds."""
    rows = [row(i, "diff_arg", "lwz", "r3, 0x10(r4)") for i in range(40)]
    rows += [row(100 + i, "insert", "addi", f"r3, r3, 0x{i}") for i in range(10)]
    rows += [row(200 + i, "delete", "addi", f"r5, r6, 0x{100 + i}") for i in range(20)]
    verdict, reason, _ = roi.classify(rows)
    assert verdict == "worth_it", reason
    assert "STRUCTURAL" in reason


def test_a_SMALL_structural_set_is_worth_running():
    """A small set with genuinely DIFFERENT inserted and deleted instructions is
    still a real code-shape difference -- the tie rule must not swallow it."""
    rows = [row(10, "diff_arg", "mr", "r3, r4"),
            row(20, "insert", "addis", "r3, r0, 0x1"),
            row(30, "delete", "li", "r4, 0x0")]
    verdict, reason, _ = roi.classify(rows)
    assert verdict == "worth_it", reason
    assert "STRUCTURAL" in reason


def test_holmes_shape_is_a_DISPLACEMENT_not_a_structural_diff():
    """HolmesClientCacheFile, real rows: the two inserts are `ld r11,0x94(r31)`
    and `std r11,0x60(r31)` at idx 89/90; the two deletes are the SAME loads and
    stores with `r10` at idx 47/53. Recoloured and displaced, not missing."""
    rows = [
        (47, "delete", "ld", "r10, 0x94(r31)"),
        (48, "diff_arg", "mr", "r9, r3"),
        (53, "delete", "std", "r10, 0x60(r31)"),
        (89, "insert", "ld", "r11, 0x94(r31)"),
        (90, "insert", "std", "r11, 0x60(r31)"),
    ]
    verdict, reason, _ = roi.classify(rows)
    assert verdict == "no_leverage", reason
    assert "MOVED_INSTRUCTION_TIE" in reason


def test_a_register_recolour_alone_still_counts_as_a_displacement():
    """MemTruncate, real rows: insert `li r29,0x0` idx 36, delete `li r28,0x0`
    idx 39, plus a diff_arg `li r29,0x0`. Same operation, different destination
    register -- a literal text match would miss it and send a lane at a tie."""
    rows = [row(36, "insert", "li", "r29, 0x0"),
            row(38, "diff_arg", "li", "r29, 0x0"),
            row(39, "delete", "li", "r28, 0x0")]
    verdict, reason, _ = roi.classify(rows)
    assert verdict == "no_leverage", reason
    assert "MOVED_INSTRUCTION_TIE" in reason


def test_same_opcode_different_IMMEDIATE_is_not_a_displacement():
    """The control for the register-normalisation: dropping registers must not
    drop immediates too, or `li r3,0` would 'equal' `li r3,1`."""
    rows = [row(36, "insert", "li", "r3, 0x1"),
            row(39, "delete", "li", "r3, 0x0")]
    verdict, _, _ = roi.classify(rows)
    assert verdict != "no_leverage"


def test_a_large_all_diff_arg_set_is_worth_but_says_permuter_is_wrong_tool():
    """TransformNormal/regasm shapes: big swap set, no structural rows. The
    verdict is still worth_it (runs are cheap) but the reason must say the
    permuter is not the tool, so a lane fixes the liveness cause instead."""
    rows = [row(i, "diff_arg", "lfs", "") for i in range(25)]
    verdict, reason, _ = roi.classify(rows)
    assert verdict == "worth_it", reason
    assert "MANY_REGISTER_SWAPS" in reason
    assert "not the tool" in reason


# --- the three-state contract, through the CLI ---------------------------


def _objdiff_like(rows):
    """The REAL objdiff shape, copied from objdiff-cli 4.2.9 output.

    A `delete` row carries only `target`, an `insert` row only `base`. An
    earlier draft of these tests used a guessed `left`/`right` + `diff_kind`
    schema and passed against a parser that could not read the real thing at
    all -- which is why the fixtures now mirror the emitted JSON and not a
    convenient abstraction.
    """
    out = []
    for i, k, op, a in rows:
        ins = {"opcode": op, "args": a}
        item = {"index": i, "match_type": k}
        if k == "insert":
            item["base"] = ins
        else:
            item["target"] = ins
        out.append(item)
    return {"instructions": out}


def test_cli_exits_1_on_a_tie(tmp_path):
    f = tmp_path / "tie.json"
    f.write_text(json.dumps(_objdiff_like([(34, "delete", "mr", "r3, r4"),
                                           (54, "insert", "mr", "r3, r4")])))
    rc, out = run("--diff-json", str(f))
    assert rc == 1, out
    assert "NO LEVERAGE" in out


def test_cli_exits_0_when_worth_running(tmp_path):
    f = tmp_path / "big.json"
    f.write_text(json.dumps(_objdiff_like(
        [(i, "diff_arg", "lwz", "") for i in range(30)]
        + [(100 + i, "insert", "addi", "") for i in range(5)])))
    rc, out = run("--diff-json", str(f))
    assert rc == 0, out
    assert "WORTH RUNNING" in out


def test_cli_exits_2_when_it_cannot_measure(tmp_path):
    """`2` must be reachable and distinct from `1`, or a failed read reads as
    'no leverage, skip it'."""
    rc, out = run("--diff-json", str(tmp_path / "absent.json"))
    assert rc == 2, out
    assert "CANNOT MEASURE" in out


def test_cli_exits_2_on_a_json_with_no_instruction_rows(tmp_path):
    f = tmp_path / "empty.json"
    f.write_text(json.dumps({"left": {}}))
    rc, out = run("--diff-json", str(f))
    assert rc == 2, out
    assert "CANNOT MEASURE" in out


def test_parser_reads_the_present_side_and_skips_equal():
    """Real shape: `delete` carries only `target`, `insert` only `base`, and the
    57 `equal` rows must not be counted as mismatches (counting them would make
    every function look structural)."""
    doc = _objdiff_like(
        [(0, "equal", "mr", "r11, r3"),
         (34, "delete", "mr", "r3, r4"),
         (54, "insert", "mr", "r3, r4")]
    )
    rows = roi._rows_from_objdiff_json(doc)
    assert len(rows) == 2
    assert [r[1] for r in rows] == ["delete", "insert"]
    # both sides must yield the same text, or the tie is never detected
    assert rows[0][2:] == rows[1][2:] == ("mr", "r3, r4")


def test_parser_returns_empty_on_an_unknown_schema_so_the_cli_can_refuse():
    rows = roi._rows_from_objdiff_json({"left": {"instructions": []}})
    assert rows == []
