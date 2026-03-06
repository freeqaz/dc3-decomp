#!/usr/bin/env python3
"""Tests for source diagnostics (template + MakeString mismatch detection).

These test the detection functions in isolation using synthetic objdiff JSON —
no build system or Ghidra needed.
"""

import sys
import os
import pytest

# Add project root to path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from tools.analyze_function import (
    detect_template_mismatches,
    detect_makestring_mismatches,
    _decode_msvc_array_dimension,
    _extract_msvc_template_info,
    _extract_makestring_dimensions,
    TemplateMismatch,
    MakeStringMismatch,
)


# =============================================================================
# Helper: build synthetic objdiff instruction entries
# =============================================================================

def make_bl_instr(index, target_args, base_args, match_type="diff_arg"):
    """Build a synthetic objdiff instruction entry for a bl (call) pair."""
    return {
        "index": index,
        "target": {"opcode": "bl", "args": target_args},
        "base": {"opcode": "bl", "args": base_args},
        "match_type": match_type,
    }


def make_non_bl_instr(index, opcode="lwz"):
    """Build a non-bl instruction entry."""
    return {
        "index": index,
        "target": {"opcode": opcode, "args": "r3, 0x10(r31)"},
        "base": {"opcode": opcode, "args": "r3, 0x10(r31)"},
        "match_type": "equal",
    }


# =============================================================================
# Template mismatch detection tests
# =============================================================================

class TestTemplateMismatchDetection:
    def test_detects_different_template_params(self):
        """Two bl entries with same method but different template type → 1 mismatch."""
        instrs = [
            make_bl_instr(
                10,
                # vector<FilePath>::push_back
                "?push_back@?$vector@VFilePath@@V?$allocator@VFilePath@@@stlpmtx_std@@@stlpmtx_std@@QAAXABVFilePath@@@Z",
                # vector<String>::push_back
                "?push_back@?$vector@VString@@V?$allocator@VString@@@stlpmtx_std@@@stlpmtx_std@@QAAXABVString@@@Z",
            ),
        ]
        result = detect_template_mismatches(instrs)
        assert len(result) == 1
        assert result[0].index == 10
        assert result[0].base_name == "vector"

    def test_ignores_identical_symbols(self):
        """Same mangled symbol on both sides → 0 mismatches."""
        sym = "?push_back@?$vector@VString@@V?$allocator@VString@@@stlpmtx_std@@@stlpmtx_std@@QAAXABVString@@@Z"
        instrs = [make_bl_instr(5, sym, sym)]
        result = detect_template_mismatches(instrs)
        assert len(result) == 0

    def test_ignores_non_template_differences(self):
        """Different functions entirely (not template-related) → 0 mismatches."""
        instrs = [
            make_bl_instr(
                7,
                "?FuncA@ClassA@@QAAXXZ",
                "?FuncB@ClassB@@QAAXXZ",
            ),
        ]
        result = detect_template_mismatches(instrs)
        assert len(result) == 0

    def test_handles_equal_match_type_with_different_symbols(self):
        """match_type: 'equal' but symbols differ due to ICF → still detected."""
        instrs = [
            make_bl_instr(
                20,
                "??H?$_Bit_iter@_NPB_N@stlpmtx_std@@QBE?AV01@H@Z",
                "??H?$_Bit_iter@U_Bit_reference@stlpmtx_std@@@stlpmtx_std@@QBE?AV01@H@Z",
                match_type="equal",
            ),
        ]
        result = detect_template_mismatches(instrs)
        assert len(result) == 1
        assert result[0].base_name == "_Bit_iter"

    def test_ignores_non_bl_instructions(self):
        """Non-bl instructions should be skipped."""
        instrs = [make_non_bl_instr(0), make_non_bl_instr(1, "stw")]
        result = detect_template_mismatches(instrs)
        assert len(result) == 0

    def test_handles_missing_target_or_base(self):
        """Instructions with missing target or base should be skipped."""
        instrs = [
            {"index": 0, "target": {"opcode": "bl", "args": "foo"}, "base": {}},
            {"index": 1, "target": {}, "base": {"opcode": "bl", "args": "bar"}},
        ]
        result = detect_template_mismatches(instrs)
        assert len(result) == 0


# =============================================================================
# MSVC array dimension decoding tests
# =============================================================================

class TestMSVCArrayDimensionDecoding:
    def test_single_digit(self):
        """Single digit '6' → 7 (1-indexed encoding)."""
        assert _decode_msvc_array_dimension("6") == 7

    def test_single_digit_zero(self):
        """Single digit '0' → 1."""
        assert _decode_msvc_array_dimension("0") == 1

    def test_multi_digit_ba(self):
        """'BA@' → B=1, A=0 → 0x10 = 16."""
        assert _decode_msvc_array_dimension("BA@") == 16

    def test_multi_digit_cd(self):
        """'CD@' → C=2, D=3 → 0x23 = 35."""
        assert _decode_msvc_array_dimension("CD@") == 35

    def test_multi_digit_a(self):
        """'A@' → A=0 → 0."""
        assert _decode_msvc_array_dimension("A@") == 0

    def test_multi_digit_with_leading_zero(self):
        """'0BA@' → strip leading 0, then B=1, A=0 → 16."""
        assert _decode_msvc_array_dimension("0BA@") == 16

    def test_empty_string(self):
        """Empty string → None."""
        assert _decode_msvc_array_dimension("") is None

    def test_invalid_chars(self):
        """Invalid characters → None."""
        assert _decode_msvc_array_dimension("XYZ@") is None


# =============================================================================
# MSVC template info extraction tests
# =============================================================================

class TestMSVCTemplateInfoExtraction:
    def test_extracts_vector_template(self):
        """Extracts template class name and params from vector symbol."""
        mangled = "?push_back@?$vector@VString@@V?$allocator@VString@@@stlpmtx_std@@@stlpmtx_std@@QAAXABVString@@@Z"
        result = _extract_msvc_template_info(mangled)
        assert result is not None
        class_name, params = result
        assert class_name == "vector"

    def test_no_template(self):
        """Non-template symbol → None."""
        mangled = "?FuncA@ClassA@@QAAXXZ"
        result = _extract_msvc_template_info(mangled)
        assert result is None


# =============================================================================
# MakeString dimension extraction tests
# =============================================================================

class TestMakeStringDimensionExtraction:
    def test_extracts_dimensions_from_real_symbol(self):
        """Test extraction from a realistic MakeString mangled symbol."""
        # Synthetic but structurally valid MakeString symbol
        # MakeString<char[7], int, char[35]>
        # $$BY06D = char[7], $$CB = int, $$BY0CD@D = char[35]
        mangled = "??$MakeString@$$BY06D$$CB$$BY0CD@D@@YA?AVString@@XZ"
        result = _extract_makestring_dimensions(mangled)
        assert result is not None
        file_len, line, cond_len = result
        assert file_len == 7
        assert cond_len == 35

    def test_non_makestring_symbol(self):
        """Non-MakeString symbol → None."""
        result = _extract_makestring_dimensions("?FuncA@ClassA@@QAAXXZ")
        assert result is None


# =============================================================================
# MakeString mismatch detection tests
# =============================================================================

class TestMakeStringMismatchDetection:
    def test_detects_cond_length_mismatch(self):
        """Target and base MakeString with different #cond lengths → 1 mismatch."""
        # Target: file[7], line, cond[35]  ($$BY06D, $$CB, $$BY0CD@D)
        # Base:   file[7], line, cond[26]  ($$BY06D, $$CB, $$BY0BJ@D)
        instrs = [
            make_bl_instr(
                26,
                "??$MakeString@$$BY06D$$CB$$BY0CD@D@@YA?AVString@@XZ",
                "??$MakeString@$$BY06D$$CB$$BY0BJ@D@@YA?AVString@@XZ",
            ),
        ]
        result = detect_makestring_mismatches(instrs)
        assert len(result) == 1
        assert result[0].index == 26
        assert result[0].target_cond_length == 35
        # Base cond length depends on decoding 'BJ@' → B=1, J=9 → 0x19 = 25
        assert result[0].base_cond_length == 25
        assert result[0].target_cond_length != result[0].base_cond_length

    def test_ignores_identical_makestrings(self):
        """Same MakeString symbol → 0 mismatches."""
        sym = "??$MakeString@$$BY06D$$CB$$BY0CD@D@@YA?AVString@@XZ"
        instrs = [make_bl_instr(10, sym, sym)]
        result = detect_makestring_mismatches(instrs)
        assert len(result) == 0

    def test_ignores_non_makestring_bl(self):
        """bl to non-MakeString symbol → 0 mismatches."""
        instrs = [
            make_bl_instr(5, "?FuncA@@QAAXXZ", "?FuncB@@QAAXXZ"),
        ]
        result = detect_makestring_mismatches(instrs)
        assert len(result) == 0

    def test_ignores_file_only_difference(self):
        """Only __FILE__ array differs, #cond same → 0 mismatches."""
        # Same cond (CD@ = 35), different file lengths
        instrs = [
            make_bl_instr(
                15,
                "??$MakeString@$$BY06D$$CB$$BY0CD@D@@YA?AVString@@XZ",
                "??$MakeString@$$BY09D$$CB$$BY0CD@D@@YA?AVString@@XZ",  # file=10 vs file=7
            ),
        ]
        result = detect_makestring_mismatches(instrs)
        assert len(result) == 0

    def test_handles_non_bl_instructions(self):
        """Non-bl instructions → 0 mismatches."""
        instrs = [make_non_bl_instr(0)]
        result = detect_makestring_mismatches(instrs)
        assert len(result) == 0


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
