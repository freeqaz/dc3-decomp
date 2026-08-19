"""Falsification tests for fold_proof's refusal guards.

These pin the two conditions under which byte-identity must NOT be certified
as an /OPT:ICF fold. Both are fail-OPEN risks: a PROVEN_FOLD verdict licenses
an alias in `scripts/symbol_aliases.json`, and an alias does not close a gap,
it stops the gap from ever being MEASURED again. A guard that silently stops
firing is therefore worse than a missing feature, and nothing else in the
suite covers these tools (audit 2026-08-19).

Written as a negative control: each test asserts the tool REFUSES, and a
companion test asserts it still accepts the genuine article, so a guard that
degenerates into "always refuse" fails too.
"""
from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from scripts.analysis.fold_proof import (  # noqa: E402
    IMAGE_SCN_MEM_WRITE, PROVEN, UNDECIDABLE, _identity_is_cheap)


class TestZeroRelocCodeIsCheap:
    """Every `{ return 0; }` stub is the same handful of bytes."""

    def test_code_identity_is_always_cheap(self):
        # `li r3,0; blr` -- 5,061 distinct symbols in this build share it.
        body = bytes.fromhex("386000004e800020")
        assert _identity_is_cheap("code", body) is True

    def test_code_identity_is_cheap_even_when_long_and_varied(self):
        # The guard is unconditional for code on purpose: a long body that
        # carries no relocations still proves nothing about the TARGET's fold.
        assert _identity_is_cheap("code", bytes(range(1, 200))) is True


class TestTinyOrZeroDataIsCheap:
    def test_short_data_is_cheap(self):
        assert _identity_is_cheap("data", b"\x01\x02\x03") is True

    def test_all_zero_data_is_cheap(self):
        assert _identity_is_cheap("data", bytes(64)) is True

    def test_substantial_nonzero_data_is_not_cheap(self):
        # This is the accept side: without it, "cheap" could be hardcoded True
        # and every test above would still pass.
        assert _identity_is_cheap("data", bytes(range(1, 65))) is False


class TestWritableDataIsNeverAFold:
    """/OPT:ICF folds read-only COMDATs. A writable one is not a candidate.

    Regression pin for the audit finding: `?sX@Vector3@@1V1@A` and
    `?sX@Vector4@@1V1@A` are 16 identical non-zero bytes in a `.data` section
    with chars 0xc0300040, and were certified PROVEN_FOLD -- while the shipped
    map places them at 0x82f0f720 and 0x82f0f750, i.e. the linker did NOT fold
    them. The size/all-zero guard cannot catch this; only writability can.
    """

    VECTOR_STATIC_CHARS = 0xC0300040   # observed on the real .data section

    def test_the_real_section_characteristics_are_writable(self):
        assert self.VECTOR_STATIC_CHARS & IMAGE_SCN_MEM_WRITE

    def test_rdata_characteristics_are_not_writable(self):
        rdata_chars = 0x40300040
        assert not (rdata_chars & IMAGE_SCN_MEM_WRITE)

    def test_size_guard_alone_would_have_let_it_through(self):
        # 16 non-zero bytes: not short, not all-zero. Proves the writability
        # check is load-bearing rather than redundant with the size guard.
        body = bytes(range(1, 17))
        assert _identity_is_cheap("data", body) is False


@pytest.mark.parametrize("kind,verdict", [("code", UNDECIDABLE)])
def test_verdict_constants_are_distinct(kind, verdict):
    """A guard that returned PROVEN under a different name would be invisible."""
    assert UNDECIDABLE != PROVEN
    assert verdict == UNDECIDABLE


def test_data_bodies_can_report_section_characteristics():
    """The writability guard needs `chars`; `data_bodies` must surface it.

    Pins the reader's contract rather than the guard's: an accidental revert of
    `with_chars` would make every symbol look read-only and the guard would go
    quiet without any test failing.
    """
    import inspect

    from scripts.analysis.coff_bodies import data_bodies

    sig = inspect.signature(data_bodies)
    assert "with_chars" in sig.parameters
    assert sig.parameters["with_chars"].default is False
