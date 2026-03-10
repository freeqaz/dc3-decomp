"""Tests for header-inline tail-call discovery and rewriting."""

from __future__ import annotations

from pathlib import Path

from scripts.permuter.header_tail_call import discover_header_tail_call_variants


def test_discovers_header_inline_tail_call_swap(tmp_path: Path):
    header_path = tmp_path / "shared.h"
    source_path = tmp_path / "caller.cpp"

    header_path.write_text(
        "inline void Helper() {\n"
        "    First();\n"
        "    Second();\n"
        "}\n",
        encoding="utf-8",
    )
    source_path.write_text(
        '#include "shared.h"\n'
        "void Caller() {\n"
        "    Helper();\n"
        "}\n",
        encoding="utf-8",
    )

    variants = discover_header_tail_call_variants(source_path, "Caller")

    assert variants
    header_variant = variants[0].variant
    assert header_variant.pattern_name == "header_tail_call"
    assert header_variant.auxiliary_files
    header_text = header_variant.auxiliary_files[0].content.decode("utf-8")
    assert "    Second();\n    First();\n" in header_text


def test_discovers_before_return_tail_call_swap_in_header(tmp_path: Path):
    header_path = tmp_path / "shared.h"
    source_path = tmp_path / "caller.cpp"

    header_path.write_text(
        "inline void Helper() {\n"
        "    First();\n"
        "    Second();\n"
        "    return;\n"
        "}\n",
        encoding="utf-8",
    )
    source_path.write_text(
        '#include "shared.h"\n'
        "void Caller() {\n"
        "    Helper();\n"
        "}\n",
        encoding="utf-8",
    )

    variants = discover_header_tail_call_variants(source_path, "Caller")

    assert variants
    header_text = variants[0].variant.auxiliary_files[0].content.decode("utf-8")
    assert "    Second();\n    First();\n    return;\n" in header_text


def test_skips_unsafe_header_tail_call_pairs(tmp_path: Path):
    header_path = tmp_path / "shared.h"
    source_path = tmp_path / "caller.cpp"

    header_path.write_text(
        "inline void Helper() {\n"
        "    SetState();\n"
        "    Finish();\n"
        "}\n",
        encoding="utf-8",
    )
    source_path.write_text(
        '#include "shared.h"\n'
        "void Caller() {\n"
        "    Helper();\n"
        "}\n",
        encoding="utf-8",
    )

    variants = discover_header_tail_call_variants(source_path, "Caller")

    assert variants == []
