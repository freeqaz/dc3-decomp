"""Tests for generic header-inline pattern bridging."""

from __future__ import annotations

from pathlib import Path

from scripts.permuter.header_pattern_bridge import discover_header_pattern_variants


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
