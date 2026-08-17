#!/usr/bin/env python3
"""Tests for the analyze_function MCP adapter.

TRIMMED 2026-08-17. This file was added by 2e661f2c2 ("progress") carrying 24
tests, 23 of which were written against an API that has never existed.

    from tools.analyze_function import (
        detect_template_mismatches, detect_makestring_mismatches,
        _decode_msvc_array_dimension, _extract_msvc_template_info,
        _extract_makestring_dimensions, TemplateMismatch, MakeStringMismatch,
    )

Not one of those seven names has a definition in any commit on any branch:
`git log --all -S'def <name>'` is empty for all five functions and
`-S'class <name>'` is empty for both dataclasses. `git show
2e661f2c2:tools/analyze_function.py` does not contain them either, so the file
was broken at birth and has never passed -- it errored at collection with
`ImportError: cannot import name 'detect_template_mismatches'` from the moment
it landed. It escaped review inside a 60-file commit titled "progress".

The design those 23 tests describe is real and is written down in
docs/sessions/2026-03-05-analyze-function-diagnostics.md, which claims the
implementation landed. It did not. Removing the tests does not remove the idea;
if someone implements the detectors, `git show <this commit>^:tests/test_source_diagnostics.py`
has the full specification, boundary cases included.

What remains below is the one test that exercises shipped code: MCPClient is
defined at tools/analyze_function.py:373 and this test passes.
"""

import sys
import os

# Add project root to path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# =============================================================================
# MCPClient adapter tests
# =============================================================================

class TestMCPClientAdapter:
    def test_adapter_initialize_returns_bool(self):
        """Verify initialize() returns True/False, never raises."""
        from tools.analyze_function import MCPClient
        client = MCPClient(base_url="http://127.0.0.1:99999/mcp", quiet=True)
        result = client.initialize()
        assert isinstance(result, bool)
        assert result is False  # Can't connect to nonexistent port
