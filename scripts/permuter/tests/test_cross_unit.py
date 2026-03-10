"""Tests for cross-unit source/header to symbol resolution helpers."""

from __future__ import annotations

import sqlite3
from pathlib import Path

from scripts.permuter.cross_unit import (
    lookup_functions_for_header_impact,
    lookup_functions_for_sources,
)
from scripts.permuter.header_impact import estimate_header_impact


def _make_db(path: Path) -> None:
    conn = sqlite3.connect(path)
    try:
        conn.execute(
            "CREATE TABLE functions ("
            "symbol TEXT NOT NULL UNIQUE, "
            "demangled TEXT, "
            "unit TEXT, "
            "current_percent REAL, "
            "verdict TEXT)"
        )
        conn.executemany(
            "INSERT INTO functions(symbol, demangled, unit, current_percent, verdict) "
            "VALUES (?, ?, ?, ?, ?)",
            [
                ("?Foo@@YAHXZ", "int __cdecl Foo(void)", "src/a.cpp", 91.0, None),
                ("?Bar@@YAHXZ", "int __cdecl Bar(void)", "default/src/a.cpp", 92.0, "COMPLETE"),
                ("?Baz@@YAHXZ", "int __cdecl Baz(void)", "src/b.cpp", 75.5, None),
            ],
        )
        conn.commit()
    finally:
        conn.close()


def test_lookup_functions_for_sources_matches_units(tmp_path: Path):
    db_path = tmp_path / "decomp.db"
    _make_db(db_path)

    src_a = tmp_path / "src" / "a.cpp"
    src_b = tmp_path / "src" / "b.cpp"
    src_a.parent.mkdir()
    src_a.write_text("int Foo() { return 1; }\n", encoding="utf-8")
    src_b.write_text("int Baz() { return 2; }\n", encoding="utf-8")

    results = lookup_functions_for_sources(
        db_path,
        (src_a, src_b),
        project_root=tmp_path,
    )

    assert [item.function_name for item in results] == ["Bar", "Foo", "Baz"]
    assert results[0].source_path == src_a
    assert results[2].source_path == src_b


def test_lookup_functions_for_header_impact_filters_complete(tmp_path: Path):
    db_path = tmp_path / "decomp.db"
    _make_db(db_path)

    include_dir = tmp_path / "include"
    src_dir = tmp_path / "src"
    include_dir.mkdir()
    src_dir.mkdir()

    header = include_dir / "shared.h"
    wrapper = include_dir / "wrapper.h"
    src_a = src_dir / "a.cpp"
    src_b = src_dir / "b.cpp"

    header.write_text("// shared\n", encoding="utf-8")
    wrapper.write_text('#include "shared.h"\n', encoding="utf-8")
    src_a.write_text('#include "../include/shared.h"\n', encoding="utf-8")
    src_b.write_text('#include "../include/wrapper.h"\n', encoding="utf-8")

    impact = estimate_header_impact(tmp_path, header)
    results = lookup_functions_for_header_impact(
        db_path,
        impact,
        project_root=tmp_path,
        exclude_complete=True,
    )

    assert [item.function_name for item in results] == ["Foo", "Baz"]
