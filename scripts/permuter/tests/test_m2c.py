"""Tests for optional m2c permuter support."""

from __future__ import annotations

from scripts.permuter.composer import available_context_keys
from scripts.permuter.m2c import (
    extract_call_order,
    extract_guard_count,
    extract_last_call_name,
    extract_nesting_depth,
    extract_return_pattern,
)
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


def test_extract_nesting_depth_flat():
    code = """\
void func(void) {
    if (a) { return; }
    if (b) { return; }
    Work();
}
"""
    assert extract_nesting_depth(code) == 1


def test_extract_nesting_depth_nested():
    code = """\
void func(void) {
    if (a) {
        if (b) {
            if (c) {
                Work();
            }
        }
    }
}
"""
    assert extract_nesting_depth(code) >= 3


def test_extract_nesting_depth_no_ifs():
    code = "void func(void) { Work(); }"
    assert extract_nesting_depth(code) == 0


def test_extract_guard_count():
    code = """\
void func(void) {
    if (a == 0) { return; }
    if (b == 0) { return; }
    if (c < 0) { return; }
    DoWork();
}
"""
    assert extract_guard_count(code) == 3


def test_extract_guard_count_no_guards():
    code = """\
void func(void) {
    if (a) { DoA(); }
    if (b) { DoB(); }
}
"""
    assert extract_guard_count(code) == 0


def test_extract_return_pattern_single():
    code = "void func(void) { return 0; }"
    assert extract_return_pattern(code) == "single"


def test_extract_return_pattern_guard_chain():
    code = """\
void func(void) {
    if (a) { return 0; }
    if (b) { return 1; }
    return 2;
}
"""
    assert extract_return_pattern(code) == "guard_chain"


def test_extract_return_pattern_split_calls():
    code = """\
void func(void) {
    if (cond) {
        return GetA();
    }
    return GetB();
}
"""
    result = extract_return_pattern(code)
    assert result in ("split_calls", "guard_chain")


def test_extract_call_order():
    code = """\
void func(void) {
    Alpha();
    Beta();
    if (x) { Gamma(); }
    Alpha();
    Delta();
}
"""
    order = extract_call_order(code)
    assert order == ["Alpha", "Beta", "Gamma", "Delta"]


def test_extract_call_order_empty():
    code = "void func(void) { return 0; }"
    assert extract_call_order(code) == []


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
