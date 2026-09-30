"""Regression pins for this_offset_scan (2026-09-30).

Both were WATCHED FAILING against the unfixed scanner before the fix landed
(commit history on branch fix-thisoff: the pins commit precedes the fixes).

Defect 1 -- a stale score hid a new bug.  `triage()` excused any row on a
function report.json scored exactly 100.0, but report.json is regenerated only
by a full `ninja`; after `ninja <one>.obj` the object is newer than the report
and a freshly-injected wrong field sat in `contradicted-by-100pct`.

Defect 2 -- a free operator read as a member.  `??6@YAAAVBinStream@@...` (a free
operator<<) was classified as a member of class `YAAAVBinStream`, so the stream
in r3 was treated as `this`.
"""
import os
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
ANALYSIS = os.path.dirname(HERE)
sys.path.insert(0, ANALYSIS)

import this_offset_scan as T  # noqa: E402

REPO = os.path.dirname(os.path.dirname(ANALYSIS))
FLARE_T = os.path.join(REPO, "build", "373307D9", "obj", "system", "rndobj", "Flare.obj")
FLARE_B = os.path.join(REPO, "build", "373307D9", "src", "system", "rndobj", "Flare.obj")
LOAD = "?Load@RndFlare@@UAAXAAVBinStream@@@Z"


@pytest.mark.parametrize("sym", [
    "??6@YAAAVBinStream@@AAV0@ABVRndParticle@@@Z",          # operator<<
    "??5@YAAAVBinStreamRev@@AAV0@AAUEyeDesc@CharEyes@@@Z",  # operator>>
    "??6@YAXAAVBinStream@@ABUPoint@CharHair@@@Z",           # void operator<<
    "??O@YA_NABVFoo@@0@Z",                                  # operator>
    "??2@YAPAXI@Z",                                         # operator new
])
def test_free_special_operator_is_not_a_member(sym):
    assert T.classify_symbol(sym)[0] == "free-or-static"


def test_member_operator_keeps_its_class():
    assert T.classify_symbol("??6BinStream@@QAAAAV0@H@Z") == ("member", "BinStream")


def test_nested_special_member_names_inner_class():
    assert T.classify_symbol("??1SubMode@PartyModeMgr@@QAA@XZ") == ("member", "SubMode")


def test_ordinary_members_unchanged():
    assert T.classify_symbol(LOAD) == ("member", "RndFlare")
    assert T.classify_symbol("?Load@Object@Hmx@@UAAXAAVBinStream@@@Z") == ("member", "Object")
    assert T.classify_symbol("??0RndFlare@@IAA@XZ") == ("member", "RndFlare")
    assert T.classify_symbol("?Init@RndFlare@@SAXXZ")[0] == "free-or-static"


@pytest.mark.skipif(not (os.path.exists(FLARE_T) and os.path.exists(FLARE_B)),
                    reason="live Flare objects absent -- NOT a pass")
def test_stale_report_does_not_excuse_a_row():
    results = []

    def check(label, cond):
        results.append((label, bool(cond)))

    T._pin_stale_report(check, FLARE_T, FLARE_B, LOAD)
    failed = [label for label, ok in results if not ok]
    assert results, "the pin ran no checks"
    assert not failed, failed
