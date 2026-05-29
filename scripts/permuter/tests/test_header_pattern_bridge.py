"""Tests for generic header-inline pattern bridging."""

from __future__ import annotations

from pathlib import Path

import pytest

from scripts.permuter.header_pattern_bridge import (
    discover_header_pattern_variants,
    supported_header_patterns,
)


@pytest.fixture(autouse=True)
def _force_msvc_dialect(monkeypatch):
    """Pin the compiler dialect to msvc for these bridge tests.

    ``discover_header_pattern_variants`` -> ``extract_function`` stamps each
    FunctionContext with the project's compiler dialect, resolved from the
    INVOKING checkout's config (``extractor._project_compiler_dialect`` ->
    ``project_config.get_compiler()``). ``scripts/permuter`` is shared (via
    symlink) between a DC3 (msvc) and an RB3 (mwcc) checkout, so running this
    shared test from the RB3 checkout would resolve mwcc and emit ``int`` /
    no extraction — failing ``variable_extraction``'s ``auto _tmp0`` /
    ``return _tmp0 > 0;`` assertions (``auto`` is msvc/C++11-only). These tests
    assert the msvc form, so force it deterministically regardless of cwd. The
    dialect-agnostic bridge tests in this file are unaffected. Clear the parse
    cache so the dialect change is reflected, not a stale tree.
    """
    from scripts.permuter import extractor

    monkeypatch.setattr(extractor, "_project_compiler_dialect", lambda: "msvc")
    extractor.ast_cache_clear()
    yield
    extractor.ast_cache_clear()


def test_header_return_call_merge_bridge(tmp_path: Path):
    header_path = tmp_path / "shared.h"
    source_path = tmp_path / "caller.cpp"

    header_path.write_text(
        "inline int Helper(int cond) {\n"
        "    if (cond) {\n"
        "        return Foo(1);\n"
        "    } else {\n"
        "        return Foo(2);\n"
        "    }\n"
        "}\n",
        encoding="utf-8",
    )
    source_path.write_text(
        '#include "shared.h"\n'
        "int Caller(int cond) {\n"
        "    return Helper(cond);\n"
        "}\n",
        encoding="utf-8",
    )

    variants = discover_header_pattern_variants(
        source_path,
        "Caller",
        "return_call_merge",
    )

    assert variants
    variant = variants[0].variant
    assert variant.pattern_name == "header_return_call_merge"
    header_text = variant.auxiliary_files[0].content.decode("utf-8")
    assert "return Foo(_merged);" in header_text


def test_header_switch_if_convert_bridge(tmp_path: Path):
    header_path = tmp_path / "shared.h"
    source_path = tmp_path / "caller.cpp"

    header_path.write_text(
        "inline int Helper(int mode) {\n"
        "    if (mode == 0) {\n"
        "        return 1;\n"
        "    } else if (mode == 1) {\n"
        "        return 2;\n"
        "    } else {\n"
        "        return 3;\n"
        "    }\n"
        "}\n",
        encoding="utf-8",
    )
    source_path.write_text(
        '#include "shared.h"\n'
        "int Caller(int mode) {\n"
        "    return Helper(mode);\n"
        "}\n",
        encoding="utf-8",
    )

    variants = discover_header_pattern_variants(
        source_path,
        "Caller",
        "switch_if_convert",
    )

    assert variants
    variant = variants[0].variant
    assert variant.pattern_name == "header_switch_if_convert"
    header_text = variant.auxiliary_files[0].content.decode("utf-8")
    assert "switch (mode)" in header_text


def test_header_pattern_bridge_skips_high_risk_headers(tmp_path: Path):
    header_path = tmp_path / "shared.h"
    source_path = tmp_path / "caller.cpp"

    header_path.write_text(
        "inline int Helper(int cond) {\n"
        "    if (cond) {\n"
        "        return Foo(1);\n"
        "    } else {\n"
        "        return Foo(2);\n"
        "    }\n"
        "}\n",
        encoding="utf-8",
    )
    source_path.write_text(
        '#include "shared.h"\n'
        "int Caller(int cond) {\n"
        "    return Helper(cond);\n"
        "}\n",
        encoding="utf-8",
    )

    for idx in range(8):
        (tmp_path / f"user_{idx}.cpp").write_text(
            '#include "shared.h"\n'
            f"int User{idx}(int cond) {{ return Helper(cond); }}\n",
            encoding="utf-8",
        )

    variants = discover_header_pattern_variants(
        source_path,
        "Caller",
        "return_call_merge",
    )

    assert variants == []


def test_supported_header_patterns_include_new_metadata_driven_patterns():
    supported = supported_header_patterns()

    assert "single_return" in supported
    assert "guard_to_nested" in supported
    assert "statement_reorder" in supported
    assert "variable_extraction" in supported


def test_header_single_return_bridge(tmp_path: Path):
    header_path = tmp_path / "shared.h"
    source_path = tmp_path / "caller.cpp"

    header_path.write_text(
        "inline int Helper(bool cond) {\n"
        "    if (cond) {\n"
        "        return 1;\n"
        "    }\n"
        "    return 0;\n"
        "}\n",
        encoding="utf-8",
    )
    source_path.write_text(
        '#include "shared.h"\n'
        "int Caller(bool cond) {\n"
        "    return Helper(cond);\n"
        "}\n",
        encoding="utf-8",
    )

    variants = discover_header_pattern_variants(
        source_path,
        "Caller",
        "single_return",
    )

    assert variants
    variant = variants[0].variant
    assert variant.pattern_name == "header_single_return"
    header_text = variant.auxiliary_files[0].content.decode("utf-8")
    assert "_result" in header_text
    assert "return _result;" in header_text


def test_header_variable_extraction_bridge(tmp_path: Path):
    header_path = tmp_path / "shared.h"
    source_path = tmp_path / "caller.cpp"

    header_path.write_text(
        "inline bool Helper() {\n"
        "    return Value() > 0;\n"
        "}\n",
        encoding="utf-8",
    )
    source_path.write_text(
        '#include "shared.h"\n'
        "bool Caller() {\n"
        "    return Helper();\n"
        "}\n",
        encoding="utf-8",
    )

    variants = discover_header_pattern_variants(
        source_path,
        "Caller",
        "variable_extraction",
    )

    assert variants
    variant = variants[0].variant
    assert variant.pattern_name == "header_variable_extraction"
    header_text = variant.auxiliary_files[0].content.decode("utf-8")
    assert "auto _tmp0 = Value();" in header_text
    assert "return _tmp0 > 0;" in header_text
