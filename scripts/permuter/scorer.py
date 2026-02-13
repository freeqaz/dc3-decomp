"""Build and score variants using ninja + objdiff-cli."""

from __future__ import annotations

import io
import json
import subprocess
import shutil
import sys
from pathlib import Path
from typing import Optional

from .types import Variant, ScoreResult, Diagnosis


class Scorer:
    """Scores variants by writing source, building, and running objdiff.

    Use as a context manager to guarantee source file restoration:

        with Scorer(source_path, symbol) as scorer:
            baseline = scorer.get_baseline()
            result = scorer.score(variant)
    """

    def __init__(self, source_path: Path, symbol: str, unit: Optional[str] = None):
        self.source_path = source_path
        self.symbol = symbol
        self._backup_path: Optional[Path] = None
        self._original_source: Optional[bytes] = None
        self._decomp_path: Optional[str] = None
        self._orig_path: Optional[str] = None
        self._baseline_equivalent: Optional[bool] = None
        self.diagnosis: Optional[Diagnosis] = None

        # Derive targeted object path from source path
        # src/system/rndobj/Foo.cpp -> build/373307D9/src/system/rndobj/Foo.obj
        self._obj_target = f"build/373307D9/{source_path.with_suffix('.obj')}"

        if unit:
            try:
                from scripts.unicorn_runner.run import resolve_unit
                self._decomp_path, self._orig_path = resolve_unit(unit)
            except Exception:
                pass  # Unicorn runner not available or unit not found

    def __enter__(self):
        self._original_source = self.source_path.read_bytes()
        self._backup_path = self.source_path.with_suffix(
            self.source_path.suffix + ".permuter_bak"
        )
        shutil.copy2(self.source_path, self._backup_path)
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        # Always restore original source
        if self._original_source is not None:
            self.source_path.write_bytes(self._original_source)
        if self._backup_path and self._backup_path.exists():
            self._backup_path.unlink()
        return False

    def _build(self) -> tuple[bool, str | None]:
        """Run ninja build targeting only this source's object file."""
        result = subprocess.run(
            ["ninja", self._obj_target],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            combined = result.stdout + result.stderr
            if "error" in combined.lower():
                return False, combined
        return True, None

    def _run_objdiff(self, include_instructions: bool = False) -> tuple[float, dict | None]:
        """Run objdiff-cli and return (fuzzy_match_percent, full_json_dict).

        When include_instructions is True, passes --include-instructions for
        diagnosis. The JSON dict is only returned when include_instructions=True.
        """
        cmd = ["./bin/objdiff-cli", "diff", "-p", ".", self.symbol, "-f", "json"]
        if include_instructions:
            cmd.append("--include-instructions")
        result = subprocess.run(cmd, capture_output=True, text=True)
        try:
            data = json.loads(result.stdout)
            match_pct = data.get("fuzzy_match_percent", 0.0)
            return match_pct, data if include_instructions else None
        except (json.JSONDecodeError, KeyError):
            return 0.0, None

    def _check_equivalence(self) -> Optional[bool]:
        """Run unicorn comparison and return True if equivalent, False if divergent.

        Uses co-loading and dual-fixture for stronger regression detection.
        Returns None if unicorn runner is not configured or fails.
        """
        if self._decomp_path is None:
            return None

        try:
            from scripts.unicorn_runner.run import run_comparison, EXIT_EQUIVALENT

            old_stdout = sys.stdout
            old_stderr = sys.stderr
            sys.stdout = io.StringIO()
            sys.stderr = io.StringIO()
            try:
                code = run_comparison(
                    self.symbol, self._decomp_path, self._orig_path,
                    coload=True, dual_fixture=True)
            finally:
                sys.stdout = old_stdout
                sys.stderr = old_stderr
            return code == EXIT_EQUIVALENT
        except Exception:
            return None

    def score(self, variant: Variant) -> ScoreResult:
        """Write variant source, build, score, and return result."""
        self.source_path.write_bytes(variant.source)

        build_ok, build_error = self._build()
        if not build_ok:
            return ScoreResult(
                variant=variant,
                match_percent=0.0,
                build_success=False,
                error=build_error,
            )

        match_percent, _ = self._run_objdiff()

        # Guard rail: if baseline was equivalent, check variant equivalence
        execution_equivalent = None
        if self._baseline_equivalent and match_percent > 0:
            execution_equivalent = self._check_equivalence()
            if execution_equivalent is False:
                return ScoreResult(
                    variant=variant,
                    match_percent=0.0,
                    build_success=True,
                    error="Execution equivalence broken",
                    execution_equivalent=False,
                )

        return ScoreResult(
            variant=variant,
            match_percent=match_percent,
            build_success=True,
            execution_equivalent=execution_equivalent,
        )

    def get_baseline(self, guided: bool = True) -> float:
        """Score the unmodified source. Must be called within context manager.

        When guided=True, runs objdiff with --include-instructions and
        produces a Diagnosis for pattern filtering.
        """
        if self._original_source is None:
            raise RuntimeError("get_baseline() must be called within context manager")
        # Ensure original source is written
        self.source_path.write_bytes(self._original_source)
        build_ok, _ = self._build()
        if not build_ok:
            return 0.0
        baseline, objdiff_data = self._run_objdiff(include_instructions=guided)

        if guided and objdiff_data and objdiff_data.get("instructions"):
            from .diagnosis import diagnose_baseline
            self.diagnosis = diagnose_baseline(objdiff_data)

        self._baseline_equivalent = self._check_equivalence()
        return baseline
