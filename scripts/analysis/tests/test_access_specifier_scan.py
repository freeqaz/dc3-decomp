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
# THE REGRESSION PIN
#
# 2026-09-16.  `access_blind_key` used to be `name.rfind("@@")` and the
# docstring argued FOR it.  It is wrong for the common case, and wrong in two
# DIFFERENT ways that this pin separates:
#
#   * it DROPS a member function whose parameter list names a class, because
#     the last `@@` belongs to the parameter's type and the character after it
#     is `Z`;
#   * it FABRICATES a member function out of a static DATA member whose type
#     names a class, because there the character after the last `@@` happens to
#     land in A-X.
#
# Swapping `rfind` for `find` is not the fix -- it walks into the template
# argument list instead -- so the pin below requires BOTH wrong spellings to
# fail, and includes a shape on which both wrong spellings are RIGHT so the pin
# cannot be satisfied by a parser that simply rejects everything.
#
# Each tuple is (symbol, expected access char or None, note).
# --------------------------------------------------------------------------- #
PIN = [
    ("?SendDoneImpl@StreamReceiver360@@UAA_NXZ", "U",
     "plain method, no class parameter -- the shape BOTH wrong spellings get right"),
    ("?Load@RndFlare@@UAAXAAVBinStream@@@Z", "U",
     "method with a class parameter -- rfind lands on BinStream's '@@'"),
    ("?end@?$vector@DV?$StlNodeAlloc@D@stlpmtx_std@@@stlpmtx_std@@QAAPADXZ", "Q",
     "nested template -- find lands inside the template argument list"),
    ("?sInterpMessage@PropKeys@@2VMessage@@A", None,
     "static DATA member (code '2') whose type names a class -- rfind reads the "
     "'A' after Message's '@@' and invents a private member function"),
]


def _rfind_key(name):
    """The exact spelling this file replaced, kept as an executable control."""
    i = name.rfind("@@")
    if i < 0 or i + 3 > len(name):
        return None
    ch = name[i + 2]
    if ch not in A.ACCESS_CHARS:
        return None
    return name[: i + 2] + "\x00" + name[i + 3:], ch


def _find_key(name):
    """The naive 'fix' -- swap rfind for find -- also kept as a control."""
    i = name.find("@@")
    if i < 0 or i + 3 > len(name):
        return None
    ch = name[i + 2]
    if ch not in A.ACCESS_CHARS:
        return None
    return name[: i + 2] + "\x00" + name[i + 3:], ch


def _ch(result):
    return None if result is None else result[1]


@pytest.mark.parametrize("sym,want,note", PIN, ids=[p[0][:40] for p in PIN])
def test_regression_pin_tokeniser_reads_the_access_code(sym, want, note):
    assert _ch(A.access_blind_key(sym)) == want, note


def test_regression_pin_is_not_vacuous_the_old_rfind_spelling_fails_it():
    """At least one PIN shape must be WRONG under rfind, or the pin proves nothing."""
    wrong = [(s, want, _ch(_rfind_key(s))) for s, want, _n in PIN
             if _ch(_rfind_key(s)) != want]
    assert wrong, "rfind agrees on every pinned shape -- this pin cannot fail"
    # and specifically: one DROP and one FABRICATION, the two distinct harms.
    got = {s: g for s, _w, g in wrong}
    assert got["?Load@RndFlare@@UAAXAAVBinStream@@@Z"] is None, \
        "rfind should DROP a method whose parameter names a class"
    assert got["?sInterpMessage@PropKeys@@2VMessage@@A"] is not None, \
        "rfind should FABRICATE a member access for a static data member"


def test_regression_pin_is_not_vacuous_the_naive_find_spelling_fails_it_too():
    """The obvious 'fix' must fail the pin as well, or the pin licenses it."""
    wrong = [(s, want, _ch(_find_key(s))) for s, want, _n in PIN
             if _ch(_find_key(s)) != want]
    assert wrong, "find agrees on every pinned shape -- this pin licenses the naive fix"
    assert any(s.startswith("?end@?$vector@") for s, _w, _g in wrong), \
        "find should get the nested-template shape wrong"


def test_regression_pin_has_a_shape_both_wrong_spellings_get_right():
    """Otherwise 'reject everything' would pass every case above."""
    control = "?SendDoneImpl@StreamReceiver360@@UAA_NXZ"
    assert _ch(_rfind_key(control)) == "U"
    assert _ch(_find_key(control)) == "U"
    assert _ch(A.access_blind_key(control)) == "U"


# --------------------------------------------------------------------------- #
# key parsing
# --------------------------------------------------------------------------- #

def test_a_template_name_embedding_at_at_is_not_mistaken_for_the_terminator():
    # 2026-09-16: this case used to be spelled with the synthetic string
    # `??1?$vector@PAVJsonObject@@V?$X@@stlpmtx_std@@QAA@XZ` and was named
    # `test_access_char_is_after_the_final_at_at`, asserting the contract that
    # is now retracted.  That string is not a decodable mangling (its template
    # argument list is unterminated), so it could only ever be checked by a
    # parser that does not parse.  Replaced with real symbols from our objects.
    assert _ch(A.access_blind_key(
        "?end@?$vector@DV?$StlNodeAlloc@D@stlpmtx_std@@@stlpmtx_std@@QAAPADXZ")) == "Q"
    assert _ch(A.access_blind_key(
        "??0?$BufLock@UD3DIndexBuffer@@@@QAA@PAUD3DIndexBuffer@@I@Z")) == "Q"


def test_real_dtor_manglings_parse_to_the_documented_access():
    assert A.access_blind_key("??_GJsonObject@@MAAPAXI@Z")[1] == "M"
    assert A.access_blind_key("??_ERndVelocityBuffer@@EAAPAXI@Z")[1] == "E"
    assert A.access_blind_key("??_GJsonObject@@UAAPAXI@Z")[1] == "U"


def test_data_symbols_are_not_treated_as_members():
    # '3' is a data storage code, not a member-access code.
    assert A.access_blind_key("?sFoo@@3MA") is None
    assert A.access_blind_key("?BITMAP_REV@@3EA") is None


def test_free_functions_and_vtables_are_not_treated_as_members():
    assert A.access_blind_key("?PathName@@YAPBDPBVObject@Hmx@@@Z") is None
    assert A.access_blind_key("??_7ADSR@@6B@") is None


def test_an_unparsable_name_raises_rather_than_returning_a_guess():
    """The whole defect was a parser that could not fail, only be wrong."""
    with pytest.raises(A.MangleError):
        A.code_index("?truncated@")
    with pytest.raises(A.MangleError):
        A.code_index("not_mangled_at_all")


def test_every_name_gets_exactly_one_disposition():
    """classify_name must never return both a key and a drop reason, nor neither."""
    for name in ("?Load@RndFlare@@UAAXAAVBinStream@@@Z", "?BITMAP_REV@@3EA",
                 "??_C@_01BDACAMKP@h?$AA@", "??_R0?AUCmdAddPlaylistToRC@@@8",
                 "plain_c_symbol", "?truncated@"):
        key, ch, reason = A.classify_name(name)
        assert (key is None) == (reason is not None), name
        assert (ch is None) == (key is None), name


def test_access_classes_partition_the_code_space():
    assert A.access_class("A") == "private"
    assert A.access_class("M") == "protected"
    assert A.access_class("U") == "public"
    # The three sets must not overlap, or a finding could be rendered two ways.
    assert not (A.PRIVATE & A.PROTECTED)
    assert not (A.PROTECTED & A.PUBLIC)
    assert not (A.PRIVATE & A.PUBLIC)


def test_encoded_numbers_decode_a_digit_as_value_plus_one():
    """`Y06` is one dimension of seven, not zero dimensions.  Reading it the
    other way desynchronised every MakeString instantiation in the binary."""
    assert A._num_value("6", 0)[0] == 7
    assert A._num_value("BA@", 0)[0] == 16
    assert _ch(A.access_blind_key(
        "??$MakeString@$$BY06$$CBDH$$BY0L@$$CBD@@YAPBDPBDAAY06$$CBDABHAAY0L@"
        "$$CBD@Z")) is None      # free function: Y


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


def test_accounting_is_per_NAME_not_per_key():
    """Several spellings collapse into one key; the denominator counts names.

    Without this the coverage block would under-count by exactly the number of
    symbols that disagree about access -- i.e. by the population being hunted.
    """
    cov = _cov(5)
    A.compare({"a\x00": {"U", "M"}, "d\x00": {"U"}},
              {"a\x00": {"M"}},
              cov, key_names={"a\x00": 2, "d\x00": 3})
    d = cov.as_dict()
    assert d["examined"] == 2
    assert d["dropped"]["absent-from-target"] == 3
    assert cov.unaccounted == 0


def test_negative_control_fixing_our_side_clears_the_finding():
    cov = _cov(1)
    found, _, _ = A.compare({"k\x00": {"M"}}, {"k\x00": {"M"}}, cov)
    assert found == []


# --------------------------------------------------------------------------- #
# denominator independence
# --------------------------------------------------------------------------- #

def test_universe_pass_does_not_classify():
    """`load_our_symbol_names` must keep non-mangled symbols, or `universe`
    becomes `examined + drops` by construction and coverage.py's exit-4
    arithmetic check is vacuous -- which is how the old 42,811 denominator
    hid 64,682 parse failures."""
    import inspect
    src = inspect.getsource(A.load_our_symbol_names)
    assert "startswith" not in src, \
        "the denominator pass must not filter on the leading '?'"


def test_classify_ours_routes_every_name_through_drop_or_a_key():
    cov = _cov(4)
    names = ["?Load@RndFlare@@UAAXAAVBinStream@@@Z", "?BITMAP_REV@@3EA",
             "plain_c_symbol", "??_C@_01BDACAMKP@h?$AA@"]
    ours, key_names, reasons, _ex = A.classify_ours(names, cov)
    assert sum(key_names.values()) + cov.dropped_total == len(names)
    assert set(reasons) == {"data-storage-code-no-member-access",
                            "not-an-msvc-mangled-name", "string-literal"}
    assert len(ours) == 1


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


@pytest.mark.skipif(not os.path.exists(A.DEFAULT_REPORT),
                    reason="report.json absent (unbuilt tree)")
def test_objdiff_demangler_agrees_with_the_tokeniser():
    """The independent instrument.  objdiff spells the access in words; if the
    tokeniser and the demangler ever disagree, one of them is wrong and this
    scan's output is not evidence of anything."""
    agree, disagree, examples = A.cross_check_demangler(A.DEFAULT_REPORT)
    assert agree > 10000, f"cross-check examined only {agree} symbols -- vacuous"
    assert disagree == 0, f"{disagree} disagreements, e.g. {examples[:3]}"
