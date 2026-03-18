"""Tests for scorer support of multi-file variants."""

from __future__ import annotations

from pathlib import Path

from scripts.permuter.scorer import Scorer
from scripts.permuter.types import AuxiliaryFile, Variant


def test_scorer_applies_and_restores_auxiliary_files(tmp_path: Path):
    source_path = tmp_path / "main.cpp"
    header_path = tmp_path / "shared.h"
    source_path.write_bytes(b'int f() { return VALUE; }\n')
    header_path.write_bytes(b"#define VALUE 1\n")

    seen_sources: list[tuple[bytes, bytes]] = []

    def fake_build(self):
        # Source is written to working copy, not the real source path
        seen_sources.append((self._working_source.read_bytes(), header_path.read_bytes()))
        return True, None

    def fake_objdiff(self, include_instructions=False):
        return 91.0, None

    original_build = Scorer._build
    original_objdiff = Scorer._run_objdiff
    try:
        Scorer._build = fake_build
        Scorer._run_objdiff = fake_objdiff

        with Scorer(source_path, symbol=f"sym_{tmp_path.name}") as scorer:
            result = scorer.score(
                Variant(
                    name="with_header",
                    pattern_name="test",
                    description="update source and header",
                    source=b'int f() { return VALUE + 1; }\n',
                    auxiliary_files=(
                        AuxiliaryFile(
                            path=header_path,
                            content=b"#define VALUE 7\n",
                        ),
                    ),
                )
            )
            assert result.build_success
            assert result.match_percent == 91.0

        assert seen_sources == [
            (b'int f() { return VALUE + 1; }\n', b"#define VALUE 7\n")
        ]
        assert source_path.read_bytes() == b'int f() { return VALUE; }\n'
        assert header_path.read_bytes() == b"#define VALUE 1\n"
    finally:
        Scorer._build = original_build
        Scorer._run_objdiff = original_objdiff


def test_scorer_cache_key_distinguishes_auxiliary_file_changes(tmp_path: Path):
    source_path = tmp_path / "main.cpp"
    header_path = tmp_path / "shared.h"
    source_path.write_bytes(b'int f() { return VALUE; }\n')
    header_path.write_bytes(b"#define VALUE 1\n")

    build_count = 0

    def fake_build(self):
        nonlocal build_count
        build_count += 1
        return True, None

    def fake_objdiff(self, include_instructions=False):
        return 80.0 + build_count, None

    original_build = Scorer._build
    original_objdiff = Scorer._run_objdiff
    try:
        Scorer._build = fake_build
        Scorer._run_objdiff = fake_objdiff

        with Scorer(source_path, symbol=f"sym_cache_{tmp_path.name}") as scorer:
            variant_a = Variant(
                name="header_a",
                pattern_name="test",
                description="first header change",
                source=b'int f() { return VALUE; }\n',
                auxiliary_files=(
                    AuxiliaryFile(path=header_path, content=b"#define VALUE 2\n"),
                ),
            )
            variant_b = Variant(
                name="header_b",
                pattern_name="test",
                description="second header change",
                source=b'int f() { return VALUE; }\n',
                auxiliary_files=(
                    AuxiliaryFile(path=header_path, content=b"#define VALUE 3\n"),
                ),
            )

            result_a = scorer.score(variant_a)
            result_b = scorer.score(variant_b)

        assert result_a.error != "cache_hit"
        assert result_b.error != "cache_hit"
        assert build_count == 2
    finally:
        Scorer._build = original_build
        Scorer._run_objdiff = original_objdiff
