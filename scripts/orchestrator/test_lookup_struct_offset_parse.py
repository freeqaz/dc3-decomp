#!/usr/bin/env python3
"""Regression pin for the `lookup_struct_offset` MCP tool's offset parse.

Part of the 2026-08-17 negative-hex sweep (rb3-xenon task #116 family). The
handler hand-rolled `if offset_str.startswith("0x"): int(s,16) else int(s)`,
which is blind to a leading `-` and to an uppercase `0X` prefix: `-0x8` and
`0X38` fell to the bare `int()`, raised ValueError, and were returned to the
agent as "Error: Invalid offset format". A refusal, not a wrong lookup -- but a
refusal on spellings an agent reads straight out of a diff pane, and the same
class already carries `_parse_hex_or_int`, which handles all four prefixes and
is what `_resolve_offset_mismatches` uses.

This test pins the helper's contract, so a re-hand-rolled parse at any call
site is caught. It deliberately does NOT stand up a StructDB.
"""
from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))

mcp_server = pytest.importorskip("mcp_server")
_parse = mcp_server.DecompMCPServer._parse_hex_or_int


@pytest.mark.parametrize("text,want", [
    # THE defect: negative hex, and uppercase-prefixed hex.
    ("-0x8", -8),
    ("-0x38", -56),
    ("-0X38", -56),
    ("0X38", 56),
    # Scope pins: shapes the old hand-rolled parse already handled.
    ("0x38", 56),
    ("0x0", 0),
    ("56", 56),
    ("-56", -56),
    ("  0x38  ", 56),
])
def test_parse_hex_or_int(text, want):
    assert _parse(text) == want


@pytest.mark.parametrize("text", ["", "0x", "zz", "0xzz", "--8", "0x8g"])
def test_parse_hex_or_int_rejects_garbage(text):
    """The handler's `except ValueError` arm must still be reachable."""
    with pytest.raises(ValueError):
        _parse(text)


def test_handler_uses_the_shared_helper():
    """The call site must not re-hand-roll the prefix test.

    Cheap source assertion rather than standing up the whole MCP server: the
    defect was a *duplicated* parse drifting from the shared one, so what
    matters is that the duplicate is gone.
    """
    src = Path(mcp_server.__file__).read_text()
    start = src.index("def _lookup_struct_offset")
    body = src[start:start + 4000]
    assert "self._parse_hex_or_int(offset_str)" in body
    assert 'offset_str.startswith("0x")' not in body
