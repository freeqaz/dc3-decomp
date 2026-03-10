"""Tests for multi-symbol header variant scoring."""

from __future__ import annotations

import sqlite3
from pathlib import Path

from scripts.permuter.header_impact import HeaderImpact
from scripts.permuter.header_variant_scorer import HeaderVariantScorer
from scripts.permuter.types import AuxiliaryFile, Variant


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
                ("?A@@YAHXZ", "int __cdecl A(void)", "src/a.cpp", 90.0, None),
                ("?B@@YAHXZ", "int __cdecl B(void)", "src/b.cpp", 80.0, None),
            ],
        )
        conn.commit()
    finally:
        conn.close()


def test_header_variant_scorer_only_rescores_changed_objects(tmp_path: Path):
    db_path = tmp_path / "decomp.db"
    _make_db(db_path)

    include_dir = tmp_path / "include"
    src_dir = tmp_path / "src"
    build_dir = tmp_path / "build" / "373307D9" / "src"
    include_dir.mkdir()
    src_dir.mkdir()
    build_dir.mkdir(parents=True)

    header = include_dir / "shared.h"
    primary = src_dir / "caller.cpp"
    src_a = src_dir / "a.cpp"
    src_b = src_dir / "b.cpp"
    obj_a = build_dir / "a.obj"
    obj_b = build_dir / "b.obj"

    header.write_text("#define VALUE 1\n", encoding="utf-8")
    primary.write_text('#include "../include/shared.h"\nint Caller() { return VALUE; }\n', encoding="utf-8")
    src_a.write_text("int A() { return 1; }\n", encoding="utf-8")
    src_b.write_text("int B() { return 2; }\n", encoding="utf-8")
    obj_a.write_bytes(b"base-a")
    obj_b.write_bytes(b"base-b")

    impact = HeaderImpact(
        header=header.resolve(),
        including_sources=(primary,),
        including_headers=(),
        affected_sources=(src_a, src_b),
        affected_headers=(),
        max_include_depth=1,
    )
    variant = Variant(
        name="header_edit",
        pattern_name="test",
        description="edit shared header",
        source=primary.read_bytes(),
        auxiliary_files=(AuxiliaryFile(path=header, content=b"#define VALUE 2\n"),),
    )

    scorer = HeaderVariantScorer(tmp_path, db_path=db_path)
    objdiff_calls: list[list[str]] = []

    def fake_run_ninja(targets):
        obj_a.write_bytes(b"variant-a")
        return True, None

    def fake_run_objdiff_batch(symbols):
        objdiff_calls.append(list(symbols))
        return {"?A@@YAHXZ": 95.0}

    scorer._run_ninja = fake_run_ninja  # type: ignore[method-assign]
    scorer._run_objdiff_batch = fake_run_objdiff_batch  # type: ignore[method-assign]

    score = scorer.evaluate_variant(primary, impact, variant)

    assert score.build_success
    assert score.changed_objects == (obj_a,)
    assert score.improved_count == 1
    assert score.unchanged_count == 1
    assert score.regressed_count == 0
    assert score.total_delta == 5.0
    assert score.accepted
    assert objdiff_calls == [["?A@@YAHXZ"]]
    assert header.read_text(encoding="utf-8") == "#define VALUE 1\n"


def test_header_variant_scorer_refreshes_baseline_when_requested(tmp_path: Path):
    db_path = tmp_path / "decomp.db"
    _make_db(db_path)

    include_dir = tmp_path / "include"
    src_dir = tmp_path / "src"
    build_dir = tmp_path / "build" / "373307D9" / "src"
    include_dir.mkdir()
    src_dir.mkdir()
    build_dir.mkdir(parents=True)

    header = include_dir / "shared.h"
    primary = src_dir / "caller.cpp"
    src_a = src_dir / "a.cpp"
    src_b = src_dir / "b.cpp"
    obj_a = build_dir / "a.obj"
    obj_b = build_dir / "b.obj"

    header.write_text("#define VALUE 1\n", encoding="utf-8")
    primary.write_text('#include "../include/shared.h"\nint Caller() { return VALUE; }\n', encoding="utf-8")
    src_a.write_text("int A() { return 1; }\n", encoding="utf-8")
    src_b.write_text("int B() { return 2; }\n", encoding="utf-8")
    obj_a.write_bytes(b"base-a")
    obj_b.write_bytes(b"base-b")

    impact = HeaderImpact(
        header=header.resolve(),
        including_sources=(primary,),
        including_headers=(),
        affected_sources=(src_a, src_b),
        affected_headers=(),
        max_include_depth=1,
    )
    variant = Variant(
        name="header_edit",
        pattern_name="test",
        description="edit shared header",
        source=primary.read_bytes(),
        auxiliary_files=(AuxiliaryFile(path=header, content=b"#define VALUE 3\n"),),
    )

    scorer = HeaderVariantScorer(tmp_path, db_path=db_path)
    objdiff_calls: list[list[str]] = []

    def fake_run_ninja(targets):
        obj_a.write_bytes(b"variant-a")
        obj_b.write_bytes(b"variant-b")
        return True, None

    def fake_run_objdiff_batch(symbols):
        objdiff_calls.append(list(symbols))
        if len(objdiff_calls) == 1:
            return {"?A@@YAHXZ": 90.0, "?B@@YAHXZ": 80.0}
        return {"?A@@YAHXZ": 92.0, "?B@@YAHXZ": 83.0}

    scorer._run_ninja = fake_run_ninja  # type: ignore[method-assign]
    scorer._run_objdiff_batch = fake_run_objdiff_batch  # type: ignore[method-assign]

    score = scorer.evaluate_variant(primary, impact, variant, refresh_baseline=True)

    assert score.build_success
    assert objdiff_calls == [["?A@@YAHXZ", "?B@@YAHXZ"], ["?A@@YAHXZ", "?B@@YAHXZ"]]
    assert score.total_delta == 5.0


def test_header_variant_scorer_restores_files_on_build_failure(tmp_path: Path):
    db_path = tmp_path / "decomp.db"
    _make_db(db_path)

    include_dir = tmp_path / "include"
    src_dir = tmp_path / "src"
    build_dir = tmp_path / "build" / "373307D9" / "src"
    include_dir.mkdir()
    src_dir.mkdir()
    build_dir.mkdir(parents=True)

    header = include_dir / "shared.h"
    primary = src_dir / "caller.cpp"
    src_a = src_dir / "a.cpp"
    src_b = src_dir / "b.cpp"
    (build_dir / "a.obj").write_bytes(b"base-a")
    (build_dir / "b.obj").write_bytes(b"base-b")

    header.write_text("#define VALUE 1\n", encoding="utf-8")
    primary.write_text('#include "../include/shared.h"\nint Caller() { return VALUE; }\n', encoding="utf-8")
    src_a.write_text("int A() { return 1; }\n", encoding="utf-8")
    src_b.write_text("int B() { return 2; }\n", encoding="utf-8")

    impact = HeaderImpact(
        header=header.resolve(),
        including_sources=(primary,),
        including_headers=(),
        affected_sources=(src_a, src_b),
        affected_headers=(),
        max_include_depth=1,
    )
    variant = Variant(
        name="header_edit",
        pattern_name="test",
        description="edit shared header",
        source=b'// modified caller\n#include "../include/shared.h"\nint Caller() { return VALUE; }\n',
        auxiliary_files=(AuxiliaryFile(path=header, content=b"#define VALUE 4\n"),),
    )

    scorer = HeaderVariantScorer(tmp_path, db_path=db_path)

    def fake_run_ninja(targets):
        return False, "compile failed"

    def fake_run_objdiff_batch(symbols):
        raise AssertionError("objdiff should not run after build failure")

    scorer._run_ninja = fake_run_ninja  # type: ignore[method-assign]
    scorer._run_objdiff_batch = fake_run_objdiff_batch  # type: ignore[method-assign]

    score = scorer.evaluate_variant(primary, impact, variant)

    assert not score.build_success
    assert score.build_error == "compile failed"
    assert primary.read_text(encoding="utf-8").startswith('#include "../include/shared.h"')
    assert header.read_text(encoding="utf-8") == "#define VALUE 1\n"
