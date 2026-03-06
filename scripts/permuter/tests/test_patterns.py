"""Pattern benchmark tests — verify patterns can recover known transformations.

Pure AST/text-level tests. No builds, no objdiff. Each fixture seeds a known
flip into C++ source and verifies that the appropriate pattern produces at
least one variant that recovers the original.

Usage:
    python -m pytest scripts/permuter/tests/test_patterns.py -v
    python scripts/permuter/tests/test_patterns.py
    python scripts/permuter/tests/test_patterns.py --pattern ternary_swap -v
    python scripts/permuter/tests/test_patterns.py --list
    python scripts/permuter/tests/test_patterns.py --fixture emptysize_empty_to_size -v
"""

from __future__ import annotations

import argparse
import sys
import unittest
from pathlib import Path

# Ensure project root is on the path so imports work standalone
_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.permuter.tests.conftest import (
    PatternFixture,
    _similarity,
    make_context,
    match_variant,
    normalize,
)
from scripts.permuter.tests.test_pattern_fixtures import FIXTURES
from scripts.permuter.patterns.base import get_pattern


# ---------------------------------------------------------------------------
# Test class
# ---------------------------------------------------------------------------

class TestPatternFixtures(unittest.TestCase):
    """Parametric tests: one test per fixture, verifying pattern recovery."""
    pass  # Tests are added dynamically below


# ---------------------------------------------------------------------------
# Dynamic test generation from fixtures
# ---------------------------------------------------------------------------

def _make_fixture_test(fixture: PatternFixture):
    """Create a test method for a single fixture."""

    def test_method(self):
        pattern = get_pattern(fixture.pattern_name)

        # Build context from seeded source
        ctx = make_context(fixture.seeded_source, fixture.func_name, fixture.diagnosis)

        # Verify relevant() agrees this pattern applies
        self.assertTrue(
            pattern.relevant(fixture.diagnosis),
            f"Pattern '{fixture.pattern_name}' reports not relevant for fixture '{fixture.id}'",
        )

        # Generate variants
        variants = list(pattern.generate(ctx))
        self.assertGreater(
            len(variants), 0,
            f"Pattern '{fixture.pattern_name}' generated 0 variants for fixture '{fixture.id}'",
        )

        # Check if any variant matches expected
        matched = False
        best_match = ""
        for v in variants:
            if match_variant(v.source, fixture.expected_source, fixture.match_mode):
                matched = True
                break
            # Track closest for debug output
            norm_v = normalize(v.source)
            norm_e = normalize(fixture.expected_source)
            if not best_match or _similarity(norm_v, norm_e) > _similarity(best_match, norm_e):
                best_match = norm_v

        if not matched:
            norm_expected = normalize(fixture.expected_source)
            msg = (
                f"\nFixture '{fixture.id}': no variant matched expected output.\n"
                f"  Expected (normalized): {norm_expected}\n"
                f"  Closest  (normalized): {best_match}\n"
                f"  Total variants: {len(variants)}"
            )
            self.fail(msg)

    test_method.__doc__ = f"{fixture.id}: {fixture.description}"
    return test_method


# Attach a test method per fixture
for _fixture in FIXTURES:
    _test_name = f"test_{_fixture.id}"
    setattr(TestPatternFixtures, _test_name, _make_fixture_test(_fixture))


# ---------------------------------------------------------------------------
# CLI runner
# ---------------------------------------------------------------------------

def _run_cli():
    parser = argparse.ArgumentParser(description="Pattern benchmark tests")
    parser.add_argument("--list", action="store_true", help="List all fixtures")
    parser.add_argument("--pattern", type=str, help="Filter fixtures by pattern name")
    parser.add_argument("--fixture", type=str, help="Run a single fixture by ID")
    parser.add_argument("-v", "--verbose", action="store_true", help="Verbose output")
    args = parser.parse_args()

    if args.list:
        print(f"{'ID':<40s} {'Pattern':<25s} Description")
        print("-" * 100)
        for f in FIXTURES:
            print(f"{f.id:<40s} {f.pattern_name:<25s} {f.description}")
        print(f"\nTotal: {len(FIXTURES)} fixtures")
        return

    # Build filtered fixture list
    selected = FIXTURES
    if args.fixture:
        selected = [f for f in FIXTURES if f.id == args.fixture]
        if not selected:
            print(f"Unknown fixture '{args.fixture}'. Use --list to see available.")
            sys.exit(1)
    elif args.pattern:
        selected = [f for f in FIXTURES if f.pattern_name == args.pattern]
        if not selected:
            print(f"No fixtures for pattern '{args.pattern}'. Use --list to see available.")
            sys.exit(1)

    passed = 0
    failed = 0
    errors = []

    for fixture in selected:
        try:
            pattern = get_pattern(fixture.pattern_name)
            ctx = make_context(fixture.seeded_source, fixture.func_name, fixture.diagnosis)

            if not pattern.relevant(fixture.diagnosis):
                errors.append((fixture.id, "relevant() returned False"))
                failed += 1
                continue

            variants = list(pattern.generate(ctx))
            if not variants:
                errors.append((fixture.id, "0 variants generated"))
                failed += 1
                continue

            matched = any(
                match_variant(v.source, fixture.expected_source, fixture.match_mode)
                for v in variants
            )

            if matched:
                passed += 1
                if args.verbose:
                    print(f"  PASS  {fixture.id} ({len(variants)} variants)")
            else:
                failed += 1
                detail = f"{len(variants)} variants, none matched"
                errors.append((fixture.id, detail))
                if args.verbose:
                    print(f"  FAIL  {fixture.id}: {detail}")
                    norm_e = normalize(fixture.expected_source)
                    for i, v in enumerate(variants):
                        norm_v = normalize(v.source)
                        marker = "*" if _similarity(norm_v, norm_e) > 0.8 else " "
                        print(f"    {marker} variant {i}: {v.description}")
                        if args.verbose:
                            print(f"        {norm_v[:120]}...")

        except Exception as e:
            failed += 1
            errors.append((fixture.id, f"Exception: {e}"))
            if args.verbose:
                import traceback
                traceback.print_exc()

    # Summary
    total = passed + failed
    status = "PASS" if failed == 0 else "FAIL"
    print(f"\n{status}: {passed}/{total} fixtures passed")

    if errors:
        print("\nFailures:")
        for fid, detail in errors:
            print(f"  {fid}: {detail}")

    sys.exit(0 if failed == 0 else 1)


if __name__ == "__main__":
    # If run with pytest-style args (no --list/--pattern/--fixture), use unittest
    if len(sys.argv) == 1 or (len(sys.argv) == 2 and sys.argv[1] in ("-v", "--verbose")):
        _run_cli()
    else:
        _run_cli()
