"""Score cross-unit/header variants against all affected functions."""

from __future__ import annotations

import json
import subprocess
from dataclasses import dataclass
from pathlib import Path

from .cross_unit import AffectedFunction, lookup_functions_for_header_impact
from .file_util import apply_file_updates, restore_tracked_files
from .header_impact import HeaderImpact
from .repo_paths import get_decomp_db_path
from .score_cache import md5_file
from .types import Variant, variant_file_updates

from .project import get_project_config as _get_project_config

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
_project = _get_project_config()
OBJDIFF_CLI = _project.repo_root / _project.objdiff_cli


@dataclass(frozen=True)
class FunctionImpact:
    """Before/after score for one affected function."""

    function: AffectedFunction
    baseline_percent: float
    variant_percent: float

    @property
    def delta(self) -> float:
        return self.variant_percent - self.baseline_percent


@dataclass(frozen=True)
class HeaderVariantScore:
    """Aggregate impact report for a header or other cross-unit variant."""

    variant: Variant
    functions: tuple[FunctionImpact, ...]
    changed_objects: tuple[Path, ...]
    build_targets: tuple[Path, ...]
    build_success: bool
    build_error: str | None = None

    @property
    def total_delta(self) -> float:
        return sum(item.delta for item in self.functions)

    @property
    def improved_count(self) -> int:
        return sum(1 for item in self.functions if item.delta > 0.0)

    @property
    def regressed_count(self) -> int:
        return sum(1 for item in self.functions if item.delta < 0.0)

    @property
    def unchanged_count(self) -> int:
        return sum(1 for item in self.functions if item.delta == 0.0)

    @property
    def perfect_gained(self) -> int:
        return sum(
            1
            for item in self.functions
            if item.baseline_percent < 100.0 <= item.variant_percent
        )

    @property
    def perfect_lost(self) -> int:
        return sum(
            1
            for item in self.functions
            if item.baseline_percent >= 100.0 > item.variant_percent
        )

    @property
    def accepted(self) -> bool:
        return self.build_success and self.perfect_lost == 0 and self.total_delta > 0.0


class HeaderVariantScorer:
    """Evaluate a multi-file variant across all functions affected by a header edit."""

    def __init__(
        self,
        project_root: Path,
        db_path: Path | None = None,
    ) -> None:
        self.project_root = project_root.resolve()
        self.db_path = (db_path or get_decomp_db_path()).resolve()
        # Resolve a per-instance project config rooted at this scorer's
        # project_root rather than the module-global `_project` (which is
        # computed once at import time against the real repo root). In
        # production project_root IS the real repo root, so behaviour is
        # unchanged; this only lets callers (and tests) inject a root.
        self._project = _get_project_config(self.project_root)

    def evaluate_variant(
        self,
        primary_source_path: Path,
        impact: HeaderImpact,
        variant: Variant,
        baseline_scores: dict[str, float] | None = None,
        exclude_complete: bool = True,
        refresh_baseline: bool = False,
    ) -> HeaderVariantScore:
        """Score a variant against every function in the affected translation units."""
        functions = lookup_functions_for_header_impact(
            self.db_path,
            impact,
            project_root=self.project_root,
            exclude_complete=exclude_complete,
        )
        if not functions:
            return HeaderVariantScore(
                variant=variant,
                functions=(),
                changed_objects=(),
                build_targets=(),
                build_success=True,
            )

        build_targets = self._build_targets_for(functions)
        baseline_hashes = self._snapshot_object_hashes(build_targets)
        effective_baseline = self._baseline_scores(
            functions,
            baseline_scores=baseline_scores,
            refresh=refresh_baseline,
        )

        originals: dict[Path, bytes | None] = {}
        try:
            apply_file_updates(
                variant_file_updates(primary_source_path, variant),
                originals,
            )
            build_ok, build_error = self._run_ninja(build_targets)
            if not build_ok:
                return HeaderVariantScore(
                    variant=variant,
                    functions=tuple(
                        FunctionImpact(
                            function=func,
                            baseline_percent=effective_baseline[func.symbol],
                            variant_percent=effective_baseline[func.symbol],
                        )
                        for func in functions
                    ),
                    changed_objects=(),
                    build_targets=build_targets,
                    build_success=False,
                    build_error=build_error,
                )

            current_hashes = self._snapshot_object_hashes(build_targets)
            changed_objects = tuple(
                target
                for target in build_targets
                if current_hashes.get(target) != baseline_hashes.get(target)
            )
            changed_symbols = [
                func.symbol
                for func in functions
                if self._obj_target_for_source(func.source_path) in changed_objects
            ]
            rescored = self._run_objdiff_batch(changed_symbols) if changed_symbols else {}
            impacts = tuple(
                FunctionImpact(
                    function=func,
                    baseline_percent=effective_baseline[func.symbol],
                    variant_percent=rescored.get(func.symbol, effective_baseline[func.symbol]),
                )
                for func in functions
            )
            return HeaderVariantScore(
                variant=variant,
                functions=impacts,
                changed_objects=changed_objects,
                build_targets=build_targets,
                build_success=True,
            )
        finally:
            restore_tracked_files(originals)

    def _baseline_scores(
        self,
        functions: tuple[AffectedFunction, ...],
        baseline_scores: dict[str, float] | None,
        refresh: bool,
    ) -> dict[str, float]:
        """Compute baseline scores for all affected functions."""
        resolved = dict(baseline_scores or {})
        if refresh:
            missing = [func.symbol for func in functions]
        else:
            missing = [
                func.symbol
                for func in functions
                if func.symbol not in resolved and func.current_percent is None
            ]
            for func in functions:
                if func.symbol not in resolved and func.current_percent is not None:
                    resolved[func.symbol] = float(func.current_percent)

        if missing:
            resolved.update(self._run_objdiff_batch(missing))

        missing_after = [func.symbol for func in functions if func.symbol not in resolved]
        if missing_after:
            raise RuntimeError(
                f"Missing baseline scores for affected symbols: {', '.join(sorted(missing_after))}"
            )
        return resolved

    def _build_targets_for(self, functions: tuple[AffectedFunction, ...]) -> tuple[Path, ...]:
        """Return unique .obj targets for the affected functions' source files."""
        targets = {self._obj_target_for_source(func.source_path) for func in functions}
        return tuple(sorted(targets))

    def _obj_target_for_source(self, source_path: Path) -> Path:
        """Map a source file to its build output object file."""
        obj_target = self._project.obj_target_for_source(source_path)
        return self.project_root / obj_target

    def _snapshot_object_hashes(self, targets: tuple[Path, ...]) -> dict[Path, str | None]:
        """Hash existing object files so unchanged units can be skipped."""
        snapshot: dict[Path, str | None] = {}
        for target in targets:
            snapshot[target] = md5_file(target) if target.exists() else None
        return snapshot

    def _run_ninja(self, targets: tuple[Path, ...]) -> tuple[bool, str | None]:
        """Build the affected object targets with ninja."""
        if not targets:
            return True, None
        rel_targets: list[str] = []
        for target in targets:
            try:
                rel_targets.append(str(target.relative_to(self.project_root)))
            except ValueError:
                rel_targets.append(str(target))
        result = subprocess.run(
            ["ninja", *rel_targets],
            cwd=self.project_root,
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            combined = (result.stdout or "") + (result.stderr or "")
            return False, combined or "ninja failed"
        return True, None

    def _run_objdiff_batch(self, symbols: list[str]) -> dict[str, float]:
        """Score a set of symbols via objdiff batch mode."""
        if not symbols:
            return {}

        proc = subprocess.run(
            [
                str(OBJDIFF_CLI),
                "diff",
                "-p",
                str(self.project_root),
                "-c",
                "functionRelocDiffs=none",
                "--batch",
            ],
            input="\n".join(symbols) + "\n",
            capture_output=True,
            text=True,
        )
        if proc.returncode != 0:
            raise RuntimeError(proc.stderr.strip() or proc.stdout.strip() or "objdiff batch failed")

        scores: dict[str, float] = {}
        for line in proc.stdout.splitlines():
            line = line.strip()
            if not line or not line.startswith("{"):
                continue
            data = json.loads(line)
            symbol = data.get("symbol")
            score = data.get("fuzzy_match_percent")
            if symbol and score is not None:
                scores[symbol] = float(score)

        missing = [symbol for symbol in symbols if symbol not in scores]
        if missing:
            raise RuntimeError(
                f"objdiff batch did not return scores for: {', '.join(sorted(missing))}"
            )
        return scores
