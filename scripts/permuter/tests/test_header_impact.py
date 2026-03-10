"""Tests for shared-header blast-radius estimation helpers."""

from __future__ import annotations

from pathlib import Path

from scripts.permuter.header_impact import estimate_header_impact


def test_estimate_header_impact_finds_sources_and_headers(tmp_path: Path):
    include_dir = tmp_path / "include"
    src_dir = tmp_path / "src"
    include_dir.mkdir()
    src_dir.mkdir()

    target = include_dir / "shared.h"
    target.write_text("// shared\n", encoding="utf-8")
    (include_dir / "wrapper.h").write_text('#include "shared.h"\n', encoding="utf-8")
    (src_dir / "main.cpp").write_text('#include "../include/shared.h"\n', encoding="utf-8")
    (src_dir / "other.cpp").write_text('#include "../include/wrapper.h"\n', encoding="utf-8")

    impact = estimate_header_impact(tmp_path, target)

    assert impact.header == target.resolve()
    assert impact.including_sources == (src_dir / "main.cpp",)
    assert impact.including_headers == (include_dir / "wrapper.h",)
    assert impact.total_includers == 2
    assert impact.affected_sources == (
        src_dir / "main.cpp",
        src_dir / "other.cpp",
    )
    assert impact.affected_headers == (include_dir / "wrapper.h",)
    assert impact.max_include_depth == 2
    assert impact.risk_tier == "medium"


def test_estimate_header_impact_ignores_unrelated_includes(tmp_path: Path):
    include_dir = tmp_path / "include"
    src_dir = tmp_path / "src"
    include_dir.mkdir()
    src_dir.mkdir()

    target = include_dir / "shared.h"
    target.write_text("// shared\n", encoding="utf-8")
    (src_dir / "main.cpp").write_text('#include "other.h"\n', encoding="utf-8")
    (include_dir / "other.h").write_text("// other\n", encoding="utf-8")

    impact = estimate_header_impact(tmp_path, target)

    assert impact.including_sources == ()
    assert impact.including_headers == ()
    assert impact.affected_sources == ()
    assert impact.affected_headers == ()
    assert impact.total_affected_files == 0
    assert impact.risk_tier == "low"
