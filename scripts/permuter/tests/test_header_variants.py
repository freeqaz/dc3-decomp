"""Tests for the header-backed variant workflow helpers."""

from __future__ import annotations

from pathlib import Path

import scripts.permuter.patterns  # noqa: F401

from scripts.permuter.header_impact import HeaderImpact
from scripts.permuter.header_variant_scorer import FunctionImpact, HeaderVariantScore
from scripts.permuter.header_variants import (
    DiscoveredHeaderVariant,
    apply_header_variant,
    discover_header_variants,
    score_discovered_variants,
    select_best_variant,
)
from scripts.permuter.types import AuxiliaryFile, Variant


def test_discover_header_variants_finds_auxiliary_header_edits(tmp_path: Path):
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

    discovered = discover_header_variants(source_path, "Caller")

    assert discovered
    assert discovered[0].variant.auxiliary_files
    assert discovered[0].impact.header == header_path.resolve()


def test_discover_header_variants_supports_header_tail_call(tmp_path: Path):
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

    discovered = discover_header_variants(
        source_path,
        "Caller",
        pattern_name="header_tail_call",
    )

    assert discovered
    assert discovered[0].variant.pattern_name == "header_tail_call"


def test_discover_header_variants_supports_generic_header_bridge(tmp_path: Path):
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

    discovered = discover_header_variants(
        source_path,
        "Caller",
        pattern_name="header_return_call_merge",
    )

    assert discovered
    assert discovered[0].variant.pattern_name == "header_return_call_merge"


def test_score_discovered_variants_orders_accepted_before_rejected(tmp_path: Path):
    primary = tmp_path / "caller.cpp"
    header = tmp_path / "shared.h"
    primary.write_text("int Caller() { return 0; }\n", encoding="utf-8")
    header.write_text("// header\n", encoding="utf-8")

    impact = HeaderImpact(
        header=header.resolve(),
        including_sources=(primary,),
        including_headers=(),
        affected_sources=(primary,),
        affected_headers=(),
        max_include_depth=1,
    )
    variant_a = Variant(
        name="good",
        pattern_name="test",
        description="good",
        source=primary.read_bytes(),
        auxiliary_files=(AuxiliaryFile(path=header, content=b"// good\n"),),
    )
    variant_b = Variant(
        name="bad",
        pattern_name="test",
        description="bad",
        source=primary.read_bytes(),
        auxiliary_files=(AuxiliaryFile(path=header, content=b"// bad\n"),),
    )
    discovered = [
        DiscoveredHeaderVariant(variant=variant_b, impact=impact),
        DiscoveredHeaderVariant(variant=variant_a, impact=impact),
    ]
    fake_function = type("Func", (), {
        "symbol": "sym",
        "function_name": "Func",
        "source_path": primary,
    })()

    class _FakeScorer:
        def evaluate_variant(self, source_path, impact, variant, exclude_complete=True, refresh_baseline=False):
            accepted = variant.name == "good"
            if accepted:
                return HeaderVariantScore(
                    variant=variant,
                    functions=(
                        FunctionImpact(
                            function=fake_function,
                            baseline_percent=90.0,
                            variant_percent=94.0,
                        ),
                    ),
                    changed_objects=(),
                    build_targets=(),
                    build_success=True,
                    build_error=None,
                )
            return HeaderVariantScore(
                variant=variant,
                functions=(
                    FunctionImpact(
                        function=fake_function,
                        baseline_percent=90.0,
                        variant_percent=89.0,
                    ),
                ),
                changed_objects=(),
                build_targets=(),
                build_success=True,
                build_error=None,
            )

    scores = score_discovered_variants(primary, discovered, _FakeScorer())
    best = select_best_variant(scores)

    assert scores[0].variant.name == "good"
    assert best is not None
    assert best.variant.name == "good"


def test_apply_header_variant_writes_auxiliary_files(tmp_path: Path):
    source_path = tmp_path / "caller.cpp"
    header_path = tmp_path / "shared.h"
    source_path.write_text("int Caller() { return 0; }\n", encoding="utf-8")
    header_path.write_text("// old header\n", encoding="utf-8")

    variant = Variant(
        name="apply_me",
        pattern_name="test",
        description="apply me",
        source=source_path.read_bytes(),
        auxiliary_files=(AuxiliaryFile(path=header_path, content=b"// new header\n"),),
    )

    apply_header_variant(source_path, variant)

    assert header_path.read_text(encoding="utf-8") == "// new header\n"
