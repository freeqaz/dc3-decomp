"""Negative controls for access_specifier_scan.

The point of these is that each one can FAIL. Every case below was checked by
sabotaging the scanner and confirming the test goes red -- a test that cannot
fail is not a test, and this repo has already shipped two of those.
"""
import os
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
ANALYSIS = os.path.dirname(HERE)
sys.path.insert(0, ANALYSIS)

import access_specifier_scan as A  # noqa: E402
from coverage import CoverageReport  # noqa: E402


def _cov(n):
    c = CoverageReport("t", allow_truncation=True)
    c.universe(n, "synthetic")
    return c


# --------------------------------------------------------------------------- #
# key parsing
# --------------------------------------------------------------------------- #

def test_access_char_is_after_the_final_at_at():
    # A template name embeds '@@'. Using find() instead of rfind() picks the
    # wrong character and silently reclassifies every templated member.
    k, ch = A.access_blind_key("??1?$vector@PAVJsonObject@@V?$X@@stlpmtx_std@@QAA@XZ")
    assert ch == "Q"


def test_real_dtor_manglings_parse_to_the_documented_access():
    assert A.access_blind_key("??_GJsonObject@@MAAPAXI@Z")[1] == "M"
    assert A.access_blind_key("??_ERndVelocityBuffer@@EAAPAXI@Z")[1] == "E"
    assert A.access_blind_key("??_GJsonObject@@UAAPAXI@Z")[1] == "U"


def test_data_symbols_are_not_treated_as_members():
    # '3' is a data storage code, not a member-access code.
    assert A.access_blind_key("?sFoo@@3MA") is None


def test_access_classes_partition_the_code_space():
    assert A.access_class("A") == "private"
    assert A.access_class("M") == "protected"
    assert A.access_class("U") == "public"
    # The three sets must not overlap, or a finding could be rendered two ways.
    assert not (A.PRIVATE & A.PROTECTED)
    assert not (A.PROTECTED & A.PUBLIC)
    assert not (A.PRIVATE & A.PUBLIC)


# --------------------------------------------------------------------------- #
# comparator
# --------------------------------------------------------------------------- #

def test_disjoint_access_is_reported():
    cov = _cov(1)
    found, _, _ = A.compare({"k\x00": {"U"}}, {"k\x00": {"M"}}, cov)
    assert [f["symbol"] for f in found] == ["k?"]
    assert found[0]["ours_access"] == "public"
    assert found[0]["target_access"] == "protected"


def test_exact_agreement_is_not_reported():
    cov = _cov(1)
    found, agree, _ = A.compare({"k\x00": {"Q"}}, {"k\x00": {"Q"}}, cov)
    assert found == []
    assert agree == 1


def test_partial_overlap_is_counted_but_not_reported():
    # A symbol legitimately appears under more than one spelling across objects.
    # Sharing ANY spelling is not evidence, but it must still be counted.
    cov = _cov(1)
    found, _, partial = A.compare({"k\x00": {"U", "M"}}, {"k\x00": {"M"}}, cov)
    assert found == []
    assert partial == 1


def test_absent_from_target_is_dropped_not_examined():
    cov = _cov(1)
    A.compare({"k\x00": {"U"}}, {}, cov)
    d = cov.as_dict()
    assert d["dropped"]["absent-from-target"] == 1
    assert d["examined"] == 0


def test_denominator_balances_across_every_disposition():
    cov = _cov(4)
    ours = {
        "a\x00": {"U"},          # finding
        "b\x00": {"Q"},          # agrees
        "c\x00": {"U", "M"},     # partial
        "d\x00": {"U"},          # absent
    }
    tgt = {"a\x00": {"M"}, "b\x00": {"Q"}, "c\x00": {"M"}}
    A.compare(ours, tgt, cov)
    assert cov.unaccounted == 0


def test_negative_control_fixing_our_side_clears_the_finding():
    cov = _cov(1)
    found, _, _ = A.compare({"k\x00": {"M"}}, {"k\x00": {"M"}}, cov)
    assert found == []


# --------------------------------------------------------------------------- #
# live corpus -- skipped rather than silently passing when the tree is unbuilt
# --------------------------------------------------------------------------- #

@pytest.mark.skipif(not os.path.exists(A.DEFAULT_MAP),
                    reason="linker map absent (no orig/ in this worktree)")
def test_map_still_spells_the_known_instances_the_documented_way():
    tgt = A.load_target(A.DEFAULT_MAP)
    key_json = A.access_blind_key("??_GJsonObject@@MAAPAXI@Z")[0]
    key_vel = A.access_blind_key("??_ERndVelocityBuffer@@EAAPAXI@Z")[0]
    assert "M" in tgt.get(key_json, set()), "map no longer spells JsonObject dtor protected"
    assert "E" in tgt.get(key_vel, set()), "map no longer spells RndVelocityBuffer dtor private"
