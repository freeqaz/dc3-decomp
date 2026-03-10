"""Tests for optional m2c permuter support."""

from __future__ import annotations

from scripts.permuter.composer import available_context_keys
from scripts.permuter.m2c import extract_last_call_name
from scripts.permuter.tests.conftest import diag_with_branch_ops, make_context


def test_extract_last_call_name_returns_last_call():
    code = """\
void func(void) {
    First();
    if (cond) {
        Second();
    }
    FinalCall();
}
"""
    assert extract_last_call_name(code) == "FinalCall"


def test_extract_last_call_name_skips_keywords():
    code = """\
void func(void) {
    if (flag) {
        return;
    }
}
"""
    assert extract_last_call_name(code) is None


def test_available_context_keys_include_m2c():
    ctx = make_context(
        """\
void test_func() {
    Work();
}
""",
        "test_func",
        diag_with_branch_ops(),
    )
    ctx.m2c_code = "void test_func(void) { Work(); }"

    keys = available_context_keys(ctx)

    assert "m2c" in keys
