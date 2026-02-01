"""Build and score variants using ninja + objdiff-cli."""

from __future__ import annotations

import json
import subprocess
import shutil
from pathlib import Path
from typing import Optional

from .types import Variant, ScoreResult


class Scorer:
    """Scores variants by writing source, building, and running objdiff.

    Use as a context manager to guarantee source file restoration:

        with Scorer(source_path, symbol) as scorer:
            baseline = scorer.get_baseline()
            result = scorer.score(variant)
    """

    def __init__(self, source_path: Path, symbol: str):
        self.source_path = source_path
        self.symbol = symbol
        self._backup_path: Optional[Path] = None
        self._original_source: Optional[bytes] = None

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
        """Run ninja build. Returns (success, error_message)."""
        result = subprocess.run(
            ["ninja"],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            combined = result.stdout + result.stderr
            if "error" in combined.lower():
                return False, combined
        return True, None

    def _run_objdiff(self) -> float:
        """Run objdiff-cli and return fuzzy match percentage."""
        result = subprocess.run(
            ["./bin/objdiff-cli", "diff", "-p", ".", self.symbol, "-f", "json"],
            capture_output=True,
            text=True,
        )
        try:
            data = json.loads(result.stdout)
            return data.get("fuzzy_match_percent", 0.0)
        except (json.JSONDecodeError, KeyError):
            return 0.0

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

        match_percent = self._run_objdiff()
        return ScoreResult(
            variant=variant,
            match_percent=match_percent,
            build_success=True,
        )

    def get_baseline(self) -> float:
        """Score the unmodified source. Must be called within context manager."""
        if self._original_source is None:
            raise RuntimeError("get_baseline() must be called within context manager")
        # Ensure original source is written
        self.source_path.write_bytes(self._original_source)
        build_ok, _ = self._build()
        if not build_ok:
            return 0.0
        return self._run_objdiff()
