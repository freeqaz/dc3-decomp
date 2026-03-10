"""Tests for noinline_stub, including header-based variants."""

from __future__ import annotations

from pathlib import Path

import scripts.permuter.patterns  # noqa: F401
from scripts.permuter.extractor import extract_function
from scripts.permuter.patterns.base import get_pattern


def test_noinline_stub_generates_header_auxiliary_variant(tmp_path: Path):
    header_path = tmp_path / "shared.h"
    source_path = tmp_path / "caller.cpp"

    header_path.write_text(
        "inline int InlineFoo() { return 1; }\n",
        encoding="utf-8",
    )
    source_path.write_text(
        '#include "shared.h"\n'
        "int Caller() { return InlineFoo(); }\n",
        encoding="utf-8",
    )

    ctx = extract_function(source_path, "Caller")
    pattern = get_pattern("noinline_stub")
    variants = list(pattern.generate(ctx))

    assert variants
    header_variants = [v for v in variants if v.auxiliary_files]
    assert header_variants
    variant = header_variants[0]
    assert variant.source == source_path.read_bytes()
    assert len(variant.auxiliary_files) == 1
    assert variant.auxiliary_files[0].path == header_path.resolve()
    assert b"__declspec(noinline) inline int InlineFoo()" in variant.auxiliary_files[0].content


def test_noinline_stub_skips_high_risk_header_variants(tmp_path: Path):
    header_path = tmp_path / "shared.h"
    source_path = tmp_path / "caller.cpp"

    header_path.write_text(
        "inline int InlineFoo() { return 1; }\n",
        encoding="utf-8",
    )
    source_path.write_text(
        '#include "shared.h"\n'
        "int Caller() { return InlineFoo(); }\n",
        encoding="utf-8",
    )

    for idx in range(8):
        (tmp_path / f"user_{idx}.cpp").write_text(
            '#include "shared.h"\n'
            f"int User{idx}() {{ return InlineFoo(); }}\n",
            encoding="utf-8",
        )

    ctx = extract_function(source_path, "Caller")
    pattern = get_pattern("noinline_stub")
    variants = list(pattern.generate(ctx))

    assert not [v for v in variants if v.auxiliary_files]


def test_noinline_stub_still_handles_same_file_callees(tmp_path: Path):
    source_path = tmp_path / "caller.cpp"
    source_path.write_text(
        "int InlineFoo() { return 1; }\n"
        "int Caller() { return InlineFoo(); }\n",
        encoding="utf-8",
    )

    ctx = extract_function(source_path, "Caller")
    pattern = get_pattern("noinline_stub")
    variants = list(pattern.generate(ctx))

    assert variants
    assert any(b"__declspec(noinline) int InlineFoo()" in v.source for v in variants)
