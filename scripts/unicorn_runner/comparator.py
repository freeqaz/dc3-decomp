"""Comparison logic for Unicorn function execution results."""

import json
import struct

from .memory_map import OBJECT_BASE, GLOBAL_BASE, REGION_SIZE


class ComparisonResult:
    """Result of comparing two execution results."""

    def __init__(self, verdict, details=None, warnings=None):
        self.verdict = verdict          # "EQUIVALENT" or "DIVERGENT"
        self.details = details or {}
        self.warnings = warnings or []

    def to_dict(self):
        """Serialize to a JSON-compatible dict."""
        return {
            "verdict": self.verdict,
            "details": self.details,
            "warnings": self.warnings,
        }


def compare_call_logs(decomp_log, orig_log):
    """Compare call logs by execution sequence.

    Returns (verdict, details) tuple.
    """
    if len(decomp_log) != len(orig_log):
        # Find where the shorter log diverges from the longer
        min_len = min(len(decomp_log), len(orig_log))
        first_arg_diff = None
        for i in range(min_len):
            for reg in ("r3", "r4", "r5", "r6"):
                if decomp_log[i]["args"][reg] != orig_log[i]["args"][reg]:
                    first_arg_diff = i
                    break
            if first_arg_diff is not None:
                break
        return "DIVERGENT", {
            "reason": "call_count_mismatch",
            "decomp_calls": len(decomp_log),
            "orig_calls": len(orig_log),
            "matched_prefix": first_arg_diff if first_arg_diff is not None else min_len,
        }

    for i, (d, o) in enumerate(zip(decomp_log, orig_log)):
        for reg in ("r3", "r4", "r5", "r6"):
            dv = d["args"][reg]
            ov = o["args"][reg]
            if dv != ov:
                return "DIVERGENT", {
                    "reason": "call_arg_mismatch",
                    "call_index": i,
                    "register": reg,
                    "decomp_val": dv,
                    "orig_val": ov,
                    "decomp_args": d["args"],
                    "orig_args": o["args"],
                }

    return "EQUIVALENT", {}


def compare_memory(decomp_mem, orig_mem, base, size):
    """Compare memory regions word-by-word.

    Returns list of (address, decomp_word, orig_word) tuples for differences.
    """
    diffs = []
    for i in range(0, size, 4):
        dw_d = struct.unpack_from(">I", decomp_mem, i)[0]
        dw_o = struct.unpack_from(">I", orig_mem, i)[0]
        if dw_d != dw_o:
            diffs.append((base + i, dw_d, dw_o))
    return diffs


def build_offset_symbol_map(orig_relocs):
    """Map function-relative offsets to original symbol names (REL24 only)."""
    return {r["offset"]: r["symbol_name"]
            for r in orig_relocs if r["type_name"] == "REL24"}


def check_call_targets(decomp_relocs, orig_relocs, decomp_log, orig_log):
    """Best-effort check: do corresponding calls target the same function?

    Returns list of warning strings.
    """
    orig_offset_map = {r["offset"]: r["symbol_name"]
                       for r in orig_relocs if r["type_name"] == "REL24"}
    decomp_offset_map = {r["offset"]: r["symbol_name"]
                         for r in decomp_relocs if r["type_name"] == "REL24"}

    warnings = []
    for i, (d, o) in enumerate(zip(decomp_log, orig_log)):
        d_sym = decomp_offset_map.get(d.get("source_offset"))
        o_sym = orig_offset_map.get(o.get("source_offset"))
        if d_sym and o_sym and d_sym != o_sym:
            warnings.append(f"Call #{i}: decomp targets {d_sym}, "
                          f"original targets {o_sym}")
    return warnings


def compare(decomp_result, orig_result, decomp_relocs, orig_relocs):
    """Compare two execution results and produce a verdict.

    Args:
        decomp_result: ExecutionResult from decomp
        orig_result: ExecutionResult from original
        decomp_relocs: list of decomp relocations (for diagnostics)
        orig_relocs: list of original relocations (for diagnostics)

    Returns:
        ComparisonResult with verdict and details
    """
    # Check for execution errors
    if decomp_result.error and orig_result.error:
        if decomp_result.error == orig_result.error:
            # Both crashed identically (e.g. null dispatch) — treat as equivalent
            return ComparisonResult("EQUIVALENT", {
                "matching_error": decomp_result.error,
            }, warnings=[f"Both sides hit identical error: {decomp_result.error}"])
        else:
            return ComparisonResult("DIVERGENT", {
                "reason": "error_mismatch",
                "decomp_error": decomp_result.error,
                "orig_error": orig_result.error,
            })
    if decomp_result.error:
        return ComparisonResult("DIVERGENT", {
            "reason": "decomp_error",
            "error": decomp_result.error,
        })
    if orig_result.error:
        return ComparisonResult("DIVERGENT", {
            "reason": "orig_error",
            "error": orig_result.error,
        })

    # Primary: call log comparison
    verdict, details = compare_call_logs(
        decomp_result.call_log, orig_result.call_log)
    if verdict == "DIVERGENT":
        return ComparisonResult(verdict, details)

    # Primary: return value comparison
    if decomp_result.r3 != orig_result.r3:
        return ComparisonResult("DIVERGENT", {
            "reason": "return_value_mismatch",
            "decomp_r3": decomp_result.r3,
            "orig_r3": orig_result.r3,
        })

    # Primary: float return value comparison
    if decomp_result.f1 != orig_result.f1:
        return ComparisonResult("DIVERGENT", {
            "reason": "fpr_return_mismatch",
            "decomp_f1": decomp_result.f1,
            "orig_f1": orig_result.f1,
        })

    # Primary: memory comparison
    obj_diffs = compare_memory(
        decomp_result.object_memory, orig_result.object_memory,
        OBJECT_BASE, REGION_SIZE)
    globals_diffs = compare_memory(
        decomp_result.globals_memory, orig_result.globals_memory,
        GLOBAL_BASE, REGION_SIZE)

    if obj_diffs or globals_diffs:
        return ComparisonResult("DIVERGENT", {
            "reason": "memory_mismatch",
            "object_diffs": obj_diffs[:20],  # cap for readability
            "globals_diffs": globals_diffs[:20],
        })

    # Secondary diagnostic: offset-matched symbol check
    warnings = check_call_targets(
        decomp_relocs, orig_relocs,
        decomp_result.call_log, orig_result.call_log)

    details = {
        "call_count": len(decomp_result.call_log),
        "r3": decomp_result.r3,
        "f1": decomp_result.f1,
    }

    return ComparisonResult("EQUIVALENT", details, warnings)


def format_result(result, decomp_result, orig_result, decomp_relocs, orig_relocs, verbose=False):
    """Format a ComparisonResult for display."""
    lines = []

    if result.verdict == "EQUIVALENT":
        lines.append("EQUIVALENT")
        matching_error = result.details.get("matching_error")
        if matching_error:
            lines.append(f"  Note: both sides hit identical error: {matching_error}")
            return "\n".join(lines)
        call_count = result.details.get("call_count", 0)
        lines.append(f"  Calls: {call_count} matched (args identical at each position)")

        if verbose and decomp_result.call_log:
            orig_offset_map = build_offset_symbol_map(orig_relocs)
            for entry in decomp_result.call_log:
                # Try to resolve symbol name from original relocs
                sym = orig_offset_map.get(entry["source_offset"], f"<tramp@0x{entry['trampoline_addr']:08X}>")
                lines.append(f"    #{entry['call_index']} {sym}  "
                           f"r3=0x{entry['args']['r3']:08X} "
                           f"r4=0x{entry['args']['r4']:08X} "
                           f"r5=0x{entry['args']['r5']:08X} "
                           f"r6=0x{entry['args']['r6']:08X}")

        lines.append(f"  Return: r3 = 0x{result.details.get('r3', 0):08X} (both)")
        f1 = result.details.get('f1', 0)
        if f1 != 0:
            lines.append(f"  Return: f1 = 0x{f1:016X} (both)")
        lines.append(f"  Memory: 0 diffs in object region, 0 diffs in globals")

        if result.warnings:
            lines.append("  Warnings:")
            for w in result.warnings:
                lines.append(f"    {w}")

    elif result.verdict == "DIVERGENT":
        lines.append("DIVERGENT")
        reason = result.details.get("reason", "unknown")

        if reason == "call_count_mismatch":
            d_count = result.details['decomp_calls']
            o_count = result.details['orig_calls']
            matched = result.details.get('matched_prefix', 0)
            lines.append(f"  Call count mismatch: decomp={d_count}, orig={o_count}")

            # Show the matched prefix and where divergence starts
            orig_offset_map = build_offset_symbol_map(orig_relocs)
            min_count = min(d_count, o_count)
            show_matched = min(matched, 5)  # cap display
            show_extra = 5

            if matched > 0:
                lines.append(f"  Matched calls before divergence ({matched} total):")
                start = max(0, matched - show_matched)
                if start > 0:
                    lines.append(f"    ... ({start} earlier calls omitted)")
                for i in range(start, matched):
                    d = decomp_result.call_log[i]
                    sym = orig_offset_map.get(orig_result.call_log[i].get("source_offset"),
                                              f"<call#{i}>")
                    lines.append(f"    #{i} {sym}  "
                               f"r3=0x{d['args']['r3']:08X} "
                               f"r4=0x{d['args']['r4']:08X} "
                               f"r5=0x{d['args']['r5']:08X} "
                               f"r6=0x{d['args']['r6']:08X}  (both match)")

            if matched < min_count:
                # Args diverged before count diverged
                i = matched
                d = decomp_result.call_log[i]
                o = orig_result.call_log[i]
                sym = orig_offset_map.get(o.get("source_offset"), f"<call#{i}>")
                lines.append(f"  First arg mismatch at call #{i} ({sym} @ offset 0x{o.get('source_offset', 0):X}):")
                lines.append(f"    Decomp:   r3=0x{d['args']['r3']:08X} r4=0x{d['args']['r4']:08X} "
                           f"r5=0x{d['args']['r5']:08X} r6=0x{d['args']['r6']:08X}")
                lines.append(f"    Original: r3=0x{o['args']['r3']:08X} r4=0x{o['args']['r4']:08X} "
                           f"r5=0x{o['args']['r5']:08X} r6=0x{o['args']['r6']:08X}")

            # Show extra calls from the longer side
            longer_side = "decomp" if d_count > o_count else "orig"
            longer_log = decomp_result.call_log if d_count > o_count else orig_result.call_log
            shorter_count = min_count
            extra_count = abs(d_count - o_count)
            if extra_count > 0:
                show = min(extra_count, show_extra)
                lines.append(f"  Extra {longer_side} calls ({extra_count} total):")
                offset_map = orig_offset_map if longer_side == "orig" else build_offset_symbol_map(decomp_relocs)
                for i in range(shorter_count, shorter_count + show):
                    entry = longer_log[i]
                    sym = offset_map.get(entry.get("source_offset"), f"<call#{i}>")
                    lines.append(f"    #{i} {sym}  "
                               f"r3=0x{entry['args']['r3']:08X} "
                               f"r4=0x{entry['args']['r4']:08X}")
                if extra_count > show:
                    lines.append(f"    ... ({extra_count - show} more)")

        elif reason == "call_arg_mismatch":
            idx = result.details["call_index"]
            reg = result.details["register"]
            orig_offset_map = build_offset_symbol_map(orig_relocs)
            o_entry = orig_result.call_log[idx]
            sym = orig_offset_map.get(o_entry.get("source_offset"), f"<call#{idx}>")
            src_off = o_entry.get("source_offset", 0)
            lines.append(f"  First mismatch: call #{idx} ({sym} @ offset 0x{src_off:X})")
            # Always show all 4 regs for context (improvement 3b)
            d_args = result.details["decomp_args"]
            o_args = result.details["orig_args"]
            d_line = (f"    Decomp:   r3=0x{d_args['r3']:08X} r4=0x{d_args['r4']:08X} "
                     f"r5=0x{d_args['r5']:08X} r6=0x{d_args['r6']:08X}")
            o_line = (f"    Original: r3=0x{o_args['r3']:08X} r4=0x{o_args['r4']:08X} "
                     f"r5=0x{o_args['r5']:08X} r6=0x{o_args['r6']:08X}")
            lines.append(d_line)
            lines.append(o_line)
            # Show which registers differ
            diff_regs = [r for r in ("r3", "r4", "r5", "r6") if d_args[r] != o_args[r]]
            lines.append(f"    Differs: {', '.join(diff_regs)}")

            # Show call logs up to divergence
            if verbose:
                lines.append("  Call logs up to divergence:")
                for i in range(idx + 1):
                    d = decomp_result.call_log[i]
                    o = orig_result.call_log[i]
                    call_sym = orig_offset_map.get(o.get("source_offset"), f"<call#{i}>")
                    if i < idx:
                        lines.append(f"    #{i} {call_sym}  "
                                   f"r3=0x{d['args']['r3']:08X} "
                                   f"r4=0x{d['args']['r4']:08X} "
                                   f"r5=0x{d['args']['r5']:08X} "
                                   f"r6=0x{d['args']['r6']:08X}  (match)")
                    else:
                        lines.append(f"    #{i} {call_sym}  MISMATCH")

        elif reason == "return_value_mismatch":
            lines.append(f"  Return value mismatch:")
            lines.append(f"    Decomp: r3 = 0x{result.details['decomp_r3']:08X}")
            lines.append(f"    Original: r3 = 0x{result.details['orig_r3']:08X}")

        elif reason == "fpr_return_mismatch":
            lines.append(f"  Float return value mismatch (f1):")
            lines.append(f"    Decomp: f1 = 0x{result.details['decomp_f1']:016X}")
            lines.append(f"    Original: f1 = 0x{result.details['orig_f1']:016X}")

        elif reason == "memory_mismatch":
            obj_diffs = result.details.get("object_diffs", [])
            glob_diffs = result.details.get("globals_diffs", [])
            lines.append(f"  Memory mismatch: "
                       f"{len(obj_diffs)} diffs in object region, "
                       f"{len(glob_diffs)} diffs in globals")
            for addr, dv, ov in obj_diffs[:5]:
                lines.append(f"    0x{addr:08X}: decomp=0x{dv:08X} orig=0x{ov:08X}")
            for addr, dv, ov in glob_diffs[:5]:
                lines.append(f"    0x{addr:08X}: decomp=0x{dv:08X} orig=0x{ov:08X}")

        elif reason == "error_mismatch":
            lines.append(f"  Different errors on each side:")
            lines.append(f"    Decomp: {result.details['decomp_error']}")
            lines.append(f"    Original: {result.details['orig_error']}")

        elif reason in ("decomp_error", "orig_error"):
            side = "Decomp" if reason == "decomp_error" else "Original"
            lines.append(f"  {side} execution error: {result.details['error']}")

    return "\n".join(lines)


def format_json_result(result, decomp_result, orig_result, orig_relocs, metadata):
    """Format a ComparisonResult as a JSON string.

    Args:
        result: ComparisonResult from compare()
        decomp_result: ExecutionResult from decomp side
        orig_result: ExecutionResult from original side
        orig_relocs: list of original relocations (for symbol resolution)
        metadata: dict with keys: symbol, decomp_size, orig_size,
                  coloaded_callees, combined_code_size

    Returns:
        JSON string
    """
    orig_offset_map = build_offset_symbol_map(orig_relocs)
    json_data = result.to_dict()
    json_data["symbol"] = metadata["symbol"]
    json_data["decomp_size"] = metadata["decomp_size"]
    json_data["orig_size"] = metadata["orig_size"]
    json_data["coloaded_callees"] = metadata["coloaded_callees"]
    json_data["combined_code_size"] = metadata["combined_code_size"]
    json_data["decomp_call_count"] = len(decomp_result.call_log)
    json_data["orig_call_count"] = len(orig_result.call_log)
    json_data["r3"] = {"decomp": decomp_result.r3, "orig": orig_result.r3}
    json_data["f1"] = {"decomp": decomp_result.f1, "orig": orig_result.f1}
    # Resolve call log symbols
    resolved_calls = []
    for entry in decomp_result.call_log[:50]:  # cap for size
        sym = orig_offset_map.get(entry.get("source_offset"), None)
        resolved_calls.append({
            "index": entry["call_index"],
            "symbol": sym,
            "source_offset": entry.get("source_offset"),
            "args": entry["args"],
        })
    json_data["decomp_calls"] = resolved_calls
    return json.dumps(json_data)
