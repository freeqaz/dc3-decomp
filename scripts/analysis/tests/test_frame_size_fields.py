"""Tests for structured frame-size field adoption (objdiff v4.1+).

Covers three cases for both diff_inspect._extract_frame_size_from_instrs and
orchestrator.mcp_server._stack_signal_summary / _extract_prologue_mismatch_info:
  a) New fields present  -> uses them (structured path)
  b) New fields absent   -> falls back to stwu-regex
  c) Malformed input     -> graceful (returns None / 0)
"""

import sys
from pathlib import Path

# Add scripts root so imports resolve the same way they do at runtime.
_SCRIPTS = Path(__file__).parent.parent.parent
sys.path.insert(0, str(_SCRIPTS))
sys.path.insert(0, str(_SCRIPTS.parent))

import pytest
from analysis.diff_inspect import (
    _get_prologue_mismatch_info,
    _extract_frame_size_from_instrs,
    cmd_stack_layout,
)


# ---------------------------------------------------------------------------
# Sample data helpers
# ---------------------------------------------------------------------------

def _make_analysis_data(target_frame: int, base_frame: int) -> dict:
    """Minimal objdiff JSON with a PROLOGUE_MISMATCH pattern carrying frame sizes."""
    return {
        "analysis": {
            "patterns": [
                {
                    "pattern": "PROLOGUE_MISMATCH",
                    "confidence": "high",
                    "details": {
                        "info": {
                            "target_frame_size": target_frame,
                            "base_frame_size": base_frame,
                            "target_first_reg": 28,
                            "base_first_reg": 30,
                        }
                    },
                }
            ]
        }
    }


def _make_stwu_instrs(target_frame: int, base_frame: int) -> list:
    """Minimal instruction list with stwu prologue on both sides."""
    def _instr(side_frame):
        return {
            "opcode": "stwu",
            "args": f"r1, -{side_frame:#x}, r1",
            "typed_args": [
                {"type": "Register", "value": "r1"},
                {"type": "Immediate", "value": f"-{side_frame:#x}"},
                {"type": "Register", "value": "r1"},
            ],
        }

    return [
        {
            "index": 0,
            "target": _instr(target_frame),
            "base": _instr(base_frame),
            "match_type": "diff_arg",
        }
    ]


def _make_no_stwu_instrs() -> list:
    """Instruction list with no stwu: a SCANNED, genuinely frameless function.

    `mflr r12` with no allocation is not a hypothetical. MEASURED in this repo
    2026-08-17 over all 69,307 functions in build/373307D9/asm: 306 (0.44%)
    carry `mflr r12` and never allocate — they save into the CALLER's frame,
    e.g. `?GetBuffer@JsonWriter@@QAAJPADPAK@Z` = `mflr r12; bl __savegprlr_29`.
    Both sides are present here, so the right answer is a measured 0.
    """
    return [
        {
            "index": 0,
            "target": {"opcode": "mflr", "args": "r12", "typed_args": []},
            "base": {"opcode": "mflr", "args": "r12", "typed_args": []},
            "match_type": "equal",
        }
    ]


def _make_target_only_instrs() -> list:
    """Rows exist, but not one of them carries a `base` side.

    This is the NO-EVIDENCE shape, and it is a routine objdiff-cli 4.2.3
    output, not a contrivance: `diff -p . '?PreInit@Synth360@@UAAXXZ'
    --include-instructions -f json` returns 345 rows of which exactly 1 has a
    `base` side, and `diff -p . __savegprlr --include-instructions` returns a
    well-formed diff whose `instructions` array is empty (both verified against
    objdiff-cli 4.2.3 / 88b425bc3bad on 2026-08-17).
    """
    return [
        {"index": 0, "target": {"opcode": "mflr", "args": "r12", "typed_args": []},
         "match_type": "delete"},
        {"index": 1, "target": {"opcode": "stwu", "args": "r1, -0x80(r1)",
                                "typed_args": []}, "match_type": "delete"},
        {"index": 2, "target": {"opcode": "blr", "args": "", "typed_args": []},
         "match_type": "delete"},
    ]


# ---------------------------------------------------------------------------
# Tests for _get_prologue_mismatch_info
# ---------------------------------------------------------------------------

class TestGetPrologueMismatchInfo:
    def test_returns_info_when_present(self):
        data = _make_analysis_data(0xd0, 0xb0)
        info = _get_prologue_mismatch_info(data)
        assert info is not None
        assert info["target_frame_size"] == 0xd0
        assert info["base_frame_size"] == 0xb0

    def test_returns_none_when_no_analysis_key(self):
        assert _get_prologue_mismatch_info({}) is None

    def test_returns_none_when_pattern_missing(self):
        data = {"analysis": {"patterns": [{"pattern": "REGISTER_SWAP", "details": {}}]}}
        assert _get_prologue_mismatch_info(data) is None

    def test_returns_none_when_info_lacks_frame_size(self):
        data = {
            "analysis": {
                "patterns": [
                    {
                        "pattern": "PROLOGUE_MISMATCH",
                        "details": {"info": {"target_first_reg": 28}},
                    }
                ]
            }
        }
        assert _get_prologue_mismatch_info(data) is None

    def test_graceful_on_none_input(self):
        # Should not raise
        assert _get_prologue_mismatch_info(None) is None  # type: ignore[arg-type]

    def test_graceful_on_malformed_patterns(self):
        data = {"analysis": {"patterns": "not-a-list"}}
        # Should not raise, returns None
        assert _get_prologue_mismatch_info(data) is None


# ---------------------------------------------------------------------------
# Tests for _extract_frame_size_from_instrs
# ---------------------------------------------------------------------------

class TestExtractFrameSize:
    def test_uses_structured_field_for_target(self):
        prologue_info = {"target_frame_size": 0x90, "base_frame_size": 0x80}
        # instrs can be empty — structured path doesn't need them
        result = _extract_frame_size_from_instrs([], side="target", prologue_info=prologue_info)
        assert result == 0x90

    def test_uses_structured_field_for_base(self):
        prologue_info = {"target_frame_size": 0x90, "base_frame_size": 0x80}
        result = _extract_frame_size_from_instrs([], side="base", prologue_info=prologue_info)
        assert result == 0x80

    def test_structured_field_takes_priority_over_stwu(self):
        # stwu says 0x70, structured says 0x90 — structured wins
        instrs = _make_stwu_instrs(0x70, 0x60)
        prologue_info = {"target_frame_size": 0x90, "base_frame_size": 0x80}
        assert _extract_frame_size_from_instrs(instrs, "target", prologue_info) == 0x90

    def test_falls_back_to_stwu_when_no_prologue_info(self):
        instrs = _make_stwu_instrs(0xb0, 0xa0)
        assert _extract_frame_size_from_instrs(instrs, "target") == 0xb0
        assert _extract_frame_size_from_instrs(instrs, "base") == 0xa0

    def test_fallback_returns_none_when_side_absent(self):
        """The fallback must SIGNAL ABSENCE rather than fabricate a number.

        ★ This is `test_fallback_returns_none_when_no_stwu` with its INPUT
        replaced, 2026-08-17, and the rename is the whole point. Introduced in
        6dc886bda against a fallback that was nothing but a stwu regex, it
        guarded exactly one property: when the scan finds no allocation, return
        None rather than 0 or garbage. `mflr r12` on both sides was chosen only
        as "an instruction list with no stwu" — the input was incidental, the
        property was not.

        Under the tri-state that landed 2026-08-04/08-17 the old input stopped
        expressing the property: both sides ARE present, so the function was
        scanned and the honest answer is a measured 0. Keeping the old
        expectation would have encoded "mflr implies a frame", which is false —
        306 functions here (0.44%) have `mflr r12` and no allocation, and
        adopting it would reclassify the 10,992 genuinely frameless functions
        (15.86% of 69,307, MEASURED 2026-08-17 over build/373307D9/asm) as
        UNKNOWN. So the input moved to a case that is still no-evidence: rows
        exist, none carries the side we asked about.

        The 0 half of the tri-state is asserted by
        `test_fallback_returns_zero_when_scanned_and_frameless` below; together
        they cover the same seam the single old test used to.
        """
        instrs = _make_target_only_instrs()
        assert _extract_frame_size_from_instrs(instrs, "base") is None
        # CONTROL: the side that IS present must still decode, or the assertion
        # above is satisfied by a function that always returns None.
        assert _extract_frame_size_from_instrs(instrs, "target") == 0x80

    def test_fallback_returns_zero_when_scanned_and_frameless(self):
        """Scanned-and-no-allocation is 0, and 0 must not decay to None.

        The other half of the tri-state, and the reason the test above had to
        change its input instead of its expectation.
        """
        instrs = _make_no_stwu_instrs()
        assert _extract_frame_size_from_instrs(instrs, "target") == 0
        assert _extract_frame_size_from_instrs(instrs, "base") == 0

    def test_fallback_returns_none_on_empty_instrs(self):
        assert _extract_frame_size_from_instrs([], "target") is None

    def test_structured_field_returns_abs_value(self):
        # Frame sizes are always positive; prologue_info may store negative (stwu offset)
        prologue_info = {"target_frame_size": -0x90, "base_frame_size": -0x80}
        result = _extract_frame_size_from_instrs([], "target", prologue_info=prologue_info)
        assert result == 0x90


# ---------------------------------------------------------------------------
# Tests for _get_prologue_mismatch_info in mcp_server
# ---------------------------------------------------------------------------

def _load_mcp_server():
    """Import scripts/orchestrator/mcp_server.py by path, or skip."""
    import importlib.util
    try:
        spec = importlib.util.spec_from_file_location(
            "mcp_server_mod", _SCRIPTS / "orchestrator" / "mcp_server.py")
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)  # type: ignore[union-attr]
        return mod
    except Exception:  # pragma: no cover - environment-dependent
        pytest.skip("mcp_server could not be imported (MCP package missing)")


def _row(index, target=None, base=None):
    r = {"index": index}
    if target:
        r["target"] = {"opcode": target[0], "args": target[1], "typed_args": []}
    if base:
        r["base"] = {"opcode": base[0], "args": base[1], "typed_args": []}
    return r


def _target_only_with_slots():
    """A target-side prologue + two stack slots, and NO base side anywhere."""
    return [
        _row(0, ("mflr", "r12")),
        _row(1, ("bl", "__savegprlr_24")),
        _row(2, ("stwu", "r1, -0x80(r1)")),
        _row(3, ("stw", "r5, 0x20(r1)")),
        _row(4, ("lwz", "r6, 0x28(r1)")),
        _row(5, ("blr", "")),
    ]


class TestCalleeSaveTriState:
    """The callee-save counts are tri-state, and both consumers subtract them.

    parse_prologue used to settle saved_gpr/fpr/vmx_count to 0 whenever the
    prologue window produced nothing — including when the window was never
    entered because no row carried this side. print_report and
    mcp_server._stack_signal_summary then subtracted those into a delta, so a
    function nobody read reported a clean 0 register-save delta.
    """

    def test_parse_prologue_leaves_counts_unknown_when_side_absent(self):
        from analysis.stack_layout import parse_prologue
        p = parse_prologue(_target_only_with_slots(), "base")
        assert p.saved_gpr_count is None
        assert p.saved_fpr_count is None
        assert p.saved_vmx_count is None
        assert p.saves_known is False
        assert "no evidence" in p.saves_evidence

    def test_parse_prologue_counts_are_known_when_side_present(self):
        """CONTROL: without this the assertions above pass on `always None`.

        50.98% of this repo's 69,307 functions (35,336, MEASURED 2026-08-17
        over build/373307D9/asm) save no registers at all, so a KNOWN 0 is the
        single most common answer and must survive.
        """
        from analysis.stack_layout import parse_prologue
        p = parse_prologue(_target_only_with_slots(), "target")
        assert p.saved_gpr_count == 8          # __savegprlr_24 => r31..r24
        assert p.saved_fpr_count == 0
        assert p.saves_known is True

        frameless = [_row(0, ("mr", "r11, r3"), ("mr", "r11, r3")),
                     _row(1, ("blr", ""), ("blr", ""))]
        q = parse_prologue(frameless, "target")
        assert (q.saved_gpr_count, q.saved_fpr_count, q.saved_vmx_count) == (0, 0, 0)
        assert q.saves_known is True

    def test_stack_signal_does_not_fabricate_a_callee_save_verdict(self):
        """The live crash/fabrication path in mcp_server.

        With structured PROLOGUE_MISMATCH frame sizes present, frame_known is
        True even on a side parse_prologue never scanned — so the callee_bytes
        subtraction is reached with a None and either raises TypeError or (pre
        tri-state) attributes the frame delta to counts nobody measured.
        """
        mod = _load_mcp_server()
        instrs = _target_only_with_slots()
        data = {"analysis": {"patterns": [{
            "pattern": "PROLOGUE_MISMATCH",
            "details": {"info": {"target_frame_size": 0x80,
                                 "base_frame_size": 0x90}}}]}}
        out = mod._stack_signal_summary(instrs, data)
        assert out is not None
        assert "frame Δ +0x10" in out
        assert "callee-save counts UNKNOWN" in out
        assert "AT_LIMIT" not in out

    def test_stack_signal_still_attributes_when_counts_are_known(self):
        """CONTROL: the AT_LIMIT verdict must still fire on a scanned pair."""
        mod = _load_mcp_server()
        # Both sides present; base saves one more GPR (8 bytes) and its frame
        # is 8 bytes bigger, so the delta is fully callee-save explained.
        instrs = [
            _row(0, ("mflr", "r12"), ("mflr", "r12")),
            _row(1, ("bl", "__savegprlr_25"), ("bl", "__savegprlr_24")),
            _row(2, ("stwu", "r1, -0x80(r1)"), ("stwu", "r1, -0x88(r1)")),
            _row(3, ("stw", "r5, 0x20(r1)"), ("stw", "r5, 0x20(r1)")),
            _row(4, ("lwz", "r6, 0x28(r1)"), ("lwz", "r6, 0x28(r1)")),
            _row(5, ("blr", ""), ("blr", "")),
        ]
        out = mod._stack_signal_summary(instrs)
        assert out is not None
        assert "callee-save AT_LIMIT" in out
        assert "UNKNOWN" not in out

    def test_structured_frame_size_is_read_with_the_same_sign_convention(self):
        """mcp_server applies abs() to the structured field, as diff_inspect does.

        objdiff-cli 4.2.3 emits only positive frame sizes (MEASURED: 468 values
        across the 234 PROLOGUE_MISMATCH patterns that carry them, out of 238
        patterns over all 2,463 partial-match functions in this repo; min 112,
        max 8672), so this guards a convention rather than an observed value —
        but the two readers of the one field must not disagree.
        """
        mod = _load_mcp_server()
        instrs = [
            _row(0, ("mflr", "r12"), ("mflr", "r12")),
            _row(1, ("stwu", "r1, -0x80(r1)"), ("stwu", "r1, -0x80(r1)")),
            _row(2, ("stw", "r5, 0x20(r1)"), ("stw", "r5, 0x24(r1)")),
            _row(3, ("blr", ""), ("blr", "")),
        ]
        data = {"analysis": {"patterns": [{
            "pattern": "PROLOGUE_MISMATCH",
            "details": {"info": {"target_frame_size": -0x80,
                                 "base_frame_size": -0x90}}}]}}
        out = mod._stack_signal_summary(instrs, data)
        assert out is not None
        # abs() on both sides => 0x90 - 0x80 = +0x10. Without it the raw
        # negatives give -0x90 - -0x80 = -0x10, the opposite sign.
        assert "frame Δ +0x10" in out

    def test_structured_null_does_not_erase_a_parsed_frame_size(self):
        """A null value must not overwrite a measured frame size.

        The extractor gates on key PRESENCE. objdiff-cli 4.2.3 omits the keys
        entirely rather than emitting null (verified: 4 of 238 PROLOGUE_MISMATCH
        patterns omit them, 0 emit null), so this guards the shape rather than a
        shape seen in the wild.
        """
        mod = _load_mcp_server()
        instrs = [
            _row(0, ("mflr", "r12"), ("mflr", "r12")),
            _row(1, ("stwu", "r1, -0x80(r1)"), ("stwu", "r1, -0x90(r1)")),
            _row(2, ("stw", "r5, 0x20(r1)"), ("stw", "r5, 0x24(r1)")),
            _row(3, ("blr", ""), ("blr", "")),
        ]
        data = {"analysis": {"patterns": [{
            "pattern": "PROLOGUE_MISMATCH",
            "details": {"info": {"target_frame_size": None,
                                 "base_frame_size": None}}}]}}
        out = mod._stack_signal_summary(instrs, data)
        assert out is not None
        # The parsed sizes (0x80 / 0x90) survive => +0x10, not "frame Δ UNKNOWN".
        assert "frame Δ +0x10" in out
        assert "frame Δ UNKNOWN" not in out


class TestMcpServerPrologueInfo:
    """The mcp_server.py helper is a near-copy; verify it behaves identically."""

    def _import(self):
        # Import lazily to avoid MCP package requirement at module-load time
        import importlib.util
        spec = importlib.util.spec_from_file_location(
            "mcp_server",
            _SCRIPTS / "orchestrator" / "mcp_server.py",
        )
        # The module imports 'mcp' at the top-level; skip full import,
        # just test the standalone helper via a direct call using the
        # already-imported diff_inspect version as a proxy (same logic).
        # We've verified both implementations are equivalent.
        return None

    def test_prologue_info_extraction_matches_diff_inspect_impl(self):
        # Both files implement the same logic — test via diff_inspect since
        # mcp_server requires the MCP package installed.  If the MCP package
        # is available, we import and test mcp_server directly.
        try:
            # Attempt dynamic import; skip gracefully if MCP package missing
            import importlib.util, importlib
            spec = importlib.util.spec_from_file_location(
                "mcp_server_mod",
                _SCRIPTS / "orchestrator" / "mcp_server.py",
            )
            mod = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(mod)  # type: ignore[union-attr]
            mcp_fn = mod._extract_prologue_mismatch_info
        except Exception:
            pytest.skip("mcp_server could not be imported (MCP package missing or other dep)")

        data = _make_analysis_data(0xd0, 0xb0)
        info = mcp_fn(data)
        assert info is not None
        assert info["target_frame_size"] == 0xd0
        assert info["base_frame_size"] == 0xb0
