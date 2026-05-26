"""Build and score variants using ninja + objdiff-cli.

Integrates three deduplication layers:
1. Source dedup: skip variants identical to baseline (no build needed)
2. Obj hash dedup: after build, hash .obj — skip objdiff if seen this session
3. Persistent cache: SQLite lookup by (symbol, source_md5) across sessions

Supports parallel scoring via score_batch() for ~4-8x throughput improvement.
"""

from __future__ import annotations

import io
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
from typing import Optional

from .file_util import (
    apply_file_updates,
    atomic_write_bytes,
    restore_tracked_files,
    SourceFileLock,
)
from .project import get_project_config, get_project_for_path, ProjectConfig, ProjectType
from .score_cache import ScoreCache, compute_dep_hash, md5_bytes, md5_file
from .types import (
    Diagnosis,
    ScoreResult,
    Variant,
    variant_file_updates,
    variant_identity_bytes,
)

REPO_ROOT = Path(__file__).resolve().parent.parent.parent  # Fallback; actual root from project config


class Scorer:
    """Scores variants by writing source, building, and running objdiff.

    Use as a context manager to guarantee source file restoration:

        with Scorer(source_path, symbol) as scorer:
            baseline = scorer.get_baseline()
            result = scorer.score(variant)

            # Or parallel:
            results = scorer.score_batch(variants, workers=8)
    """

    def __init__(self, source_path: Path, symbol: str, unit: Optional[str] = None,
                 project: Optional[ProjectConfig] = None):
        self.source_path = source_path
        self.symbol = symbol
        self._backup_path: Optional[Path] = None
        self._original_source: Optional[bytes] = None
        self._original_source_md5: Optional[str] = None
        self._decomp_path: Optional[str] = None
        self._orig_path: Optional[str] = None
        self._baseline_equivalent: Optional[bool] = None
        self.diagnosis: Optional[Diagnosis] = None
        self._cache: Optional[ScoreCache] = None
        # Ghidra-guided fields (populated by get_baseline when ghidra=True)
        self.ghidra_code: Optional[str] = None
        self.ghidra_ast: object = None  # GhidraAST or None
        self.m2c_code: Optional[str] = None
        self.unit = unit

        # Detect project configuration (DC3 vs RB3)
        self._project = project or get_project_for_path(source_path)

        # Derive targeted object path from source path using project config.
        # DC3: src/system/rndobj/Foo.cpp -> build/373307D9/src/system/rndobj/Foo.obj
        # RB3: src/system/rndobj/Foo.cpp -> build/SZBE69_B8/src/system/rndobj/Foo.o
        self._obj_target = self._project.obj_target_for_source(source_path)
        self._obj_path = Path(self._obj_target)
        self._compile_cwd: Optional[str] = None
        self._compile_shell_cmd: Optional[str] = None
        self._compile_fo_path: Optional[str] = None
        self._il_tools_loaded = False
        self._il_capture = None
        self._ILFile = None
        self._il_function_hash = None

        if unit:
            try:
                from scripts.unicorn_runner.run import resolve_unit
                self._decomp_path, self._orig_path = resolve_unit(unit)
            except Exception:
                pass  # Unicorn runner not available or unit not found

    def __enter__(self):
        self._backup_path = self.source_path.with_suffix(
            self.source_path.suffix + ".permuter_bak"
        )
        # Check for stale backup from a previous crash/kill
        if self._backup_path.exists():
            print(
                f"  WARNING: Found stale backup {self._backup_path.name} — "
                f"restoring from previous interrupted run.",
                file=sys.stderr,
            )
            shutil.copy2(self._backup_path, self.source_path)
            self._backup_path.unlink()

        self._original_source = self.source_path.read_bytes()
        self._tracked_file_originals: dict[Path, bytes | None] = {}
        self._applied_paths: set[Path] = set()
        self._original_source_md5 = self._variant_source_md5(
            Variant(
                name="baseline",
                pattern_name="baseline",
                description="baseline",
                source=self._original_source,
            )
        )
        shutil.copy2(self.source_path, self._backup_path)
        # Create working copy next to real source — variants are written here
        # instead of to the real source file, so concurrent ninja builds are
        # never broken by permuter runs.  The file lives in the same directory
        # so that relative include paths and wibo path mapping still work
        # (cl.exe treats /tmp/... as a compiler switch).
        self._working_dir = self.source_path.parent
        self._working_source = self._working_dir / f".permuter_work_{self.source_path.name}"
        # Acquire per-file lock (prevents concurrent permuter access)
        self._file_lock = SourceFileLock(self.source_path)
        self._file_lock.__enter__()
        # Open cache for this symbol
        self._cache = ScoreCache(self.symbol)
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        # Print cache stats
        if self._cache:
            stats = self._cache.stats_summary()
            if self._cache.hits_source + self._cache.hits_obj + self._cache.hits_persistent > 0:
                print(f"  {stats}", file=sys.stderr)
            self._cache.close()
            self._cache = None
        # Restore any auxiliary files that were modified (headers, etc.)
        if hasattr(self, "_tracked_file_originals"):
            restore_tracked_files(self._tracked_file_originals)
        if self._backup_path and self._backup_path.exists():
            self._backup_path.unlink()
        # Clean up working source file
        if hasattr(self, '_working_source') and self._working_source and self._working_source.exists():
            self._working_source.unlink(missing_ok=True)
        # Release per-file lock
        if hasattr(self, '_file_lock') and self._file_lock is not None:
            self._file_lock.__exit__(exc_type, exc_val, exc_tb)
            self._file_lock = None
        return False

    def _variant_file_updates(self, variant: Variant) -> dict[Path, bytes]:
        """Return the exact file writes needed for a variant."""
        return variant_file_updates(self.source_path, variant)

    def _variant_source_md5(self, variant: Variant) -> str:
        """Hash all file updates so cache keys distinguish header edits."""
        updates = self._variant_file_updates(variant)
        del updates  # normalized by variant_identity_bytes
        return md5_bytes(variant_identity_bytes(self.source_path, variant))

    def _apply_variant_files(self, variant: Variant, *, to_disk: bool = False) -> None:
        """Apply a variant's file edits.

        By default, the main source file is written to the working copy
        (self._working_source) so the real source is never modified during
        scoring.  Auxiliary files (headers) are always written in-place.

        When to_disk=True, ALL files including the main source are written
        to their real paths (needed for IL capture which extracts its own
        compile commands from ninja).
        """
        updates = self._variant_file_updates(variant)

        if not to_disk:
            # Redirect main source to working copy
            resolved_source = self.source_path.resolve()
            if resolved_source in updates:
                atomic_write_bytes(self._working_source, updates.pop(resolved_source))
            elif self.source_path in updates:
                atomic_write_bytes(self._working_source, updates.pop(self.source_path))

        # Apply remaining (auxiliary) files — or all files if to_disk=True
        if updates:
            self._applied_paths = apply_file_updates(
                updates,
                self._tracked_file_originals,
                current_paths=self._applied_paths,
            )

    def _extract_compile_cmd(self) -> None:
        """Extract the compile command from ninja for direct invocation.

        Handles both DC3 (MSVC cl.exe, "cd dir && ... /Fo...") and
        RB3 (MetroWerks mwcceppc, "wibo mwcceppc ... -c src -o dir").
        """
        result = subprocess.run(
            ["ninja", "-t", "commands", self._obj_target],
            capture_output=True, text=True,
            cwd=str(self._project.repo_root),
        )

        if not result.stdout.strip():
            err = result.stderr.strip()
            details = f"; stderr: {err}" if err else ""
            raise RuntimeError(
                f"Could not derive compile command for target '{self._obj_target}'"
                f" from 'ninja -t commands'{details}"
            )

        # Use project-specific command parsing
        self._compile_cwd, self._compile_shell_cmd = self._project.parse_ninja_command(
            result.stdout
        )

        if not self._compile_shell_cmd:
            raise RuntimeError(
                f"Could not derive compile command for target '{self._obj_target}'"
                f" from 'ninja -t commands'"
            )

        # Extract the output path from the command for reliable replacement
        self._compile_fo_path = self._project.extract_compile_output_path(
            self._compile_shell_cmd
        )

    def _build(self) -> tuple[bool, str | None]:
        """Compile directly, redirecting source to the working copy."""
        if self._compile_shell_cmd is None:
            self._extract_compile_cmd()

        # Swap the source filename to point at the working copy
        # (same directory, different name — avoids touching the real source).
        src_name = self.source_path.name
        work_name = self._working_source.name
        cmd = self._project.redirect_source_in_cmd(
            self._compile_shell_cmd, src_name, work_name
        )

        result = subprocess.run(
            cmd,
            shell=True,
            cwd=self._compile_cwd or str(self._project.repo_root),
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            combined = result.stdout + result.stderr
            if "error" in combined.lower():
                return False, combined
        return True, None

    def _build_to_path(
        self, source_bytes: bytes, obj_output: Path,
        work_src: Path | None = None,
    ) -> tuple[bool, str | None]:
        """Compile variant source to a specific object path (for parallel builds).

        Args:
            source_bytes: variant source.
            obj_output: where the .o lands.
            work_src: per-call source file path. When None, uses the scorer's
                shared `_working_source` (single-threaded fallback). For
                concurrent calls, pass a unique path per worker so threads
                don't clobber each other's writes.
        """
        if self._compile_shell_cmd is None:
            self._extract_compile_cmd()
        if work_src is None:
            work_src = self._working_source

        # Redirect output to the temp object path
        cmd = self._project.redirect_output_for_parallel(
            self._compile_shell_cmd,
            self._compile_fo_path,
            self._obj_path,
            obj_output,
        )

        # Redirect source filename to the working copy
        src_name = self.source_path.name
        work_name = work_src.name
        cmd = self._project.redirect_source_in_cmd(cmd, src_name, work_name)

        # Write source to the working copy (not the real source path)
        atomic_write_bytes(work_src, source_bytes)

        result = subprocess.run(
            cmd,
            shell=True,
            cwd=self._compile_cwd or str(self._project.repo_root),
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            combined = result.stdout + result.stderr
            if "error" in combined.lower():
                return False, combined
        return True, None

    def _load_il_tools(self) -> bool:
        """Load IL capture + canonical hash helpers on demand (DC3 only)."""
        if self._il_tools_loaded:
            return (
                self._il_capture is not None
                and self._ILFile is not None
                and self._il_function_hash is not None
            )

        self._il_tools_loaded = True
        # IL tools are DC3-specific (MSVC compiler trace)
        if not self._project.has_il_tools:
            return False
        tools_dir = self._project.repo_root / "msvc-src" / "tools"
        if str(tools_dir) not in sys.path:
            sys.path.insert(0, str(tools_dir))
        try:
            from il_parser import ILFile, capture_il
            from il_permuter import function_hash
        except Exception:
            return False

        self._il_capture = capture_il
        self._ILFile = ILFile
        self._il_function_hash = function_hash
        return True

    def capture_variant_il_hashes(
        self,
        variants: list[Variant],
        *,
        limit: int = 8,
    ) -> dict[int, str]:
        """Capture canonical IL hashes for a limited set of variants.

        This is for ranking/reporting only. Every candidate still goes through
        the normal build + objdiff path.
        """
        if limit <= 0 or not variants or not self._load_il_tools():
            return {}

        hashes: dict[int, str] = {}
        tmp_dir = Path(tempfile.mkdtemp(prefix="permuter_il_analysis_"))
        try:
            for idx, variant in enumerate(variants[:limit]):
                # IL capture extracts its own compile commands from ninja,
                # so it needs the source at the real path.
                self._apply_variant_files(variant, to_disk=True)
                try:
                    il_base = self._il_capture(
                        str(self.source_path),
                        output_dir=str(tmp_dir / f"variant_{idx}"),
                    )
                except Exception:
                    continue
                if not il_base:
                    continue
                try:
                    bundle = self._ILFile(il_base).to_dict()
                except Exception:
                    continue
                for function in bundle.get("functions", []):
                    if function.get("name") == self.symbol:
                        hashes[id(variant)] = self._il_function_hash(function)
                        break
        finally:
            shutil.rmtree(tmp_dir, ignore_errors=True)
        return hashes

    def _run_objdiff(self, include_instructions: bool = False) -> tuple[float, dict | None]:
        """Run objdiff-cli and return (fuzzy_match_percent, full_json_dict).

        When include_instructions is True, passes --include-instructions for
        diagnosis. The JSON dict is only returned when include_instructions=True.
        """
        objdiff = self._project.objdiff_cli
        cmd = [objdiff, "diff", "-p", ".", self.symbol,
               "-c", "functionRelocDiffs=none", "-f", "json"]
        if include_instructions:
            cmd.append("--include-instructions")
        result = subprocess.run(
            cmd, capture_output=True, text=True,
            cwd=str(self._project.repo_root),
        )
        try:
            data = json.loads(result.stdout)
            match_pct = data.get("fuzzy_match_percent", 0.0)
            return match_pct, data if include_instructions else None
        except (json.JSONDecodeError, KeyError):
            return 0.0, None

    def _run_objdiff_on_obj(self, obj_path: Path) -> float:
        """Run objdiff on a specific .obj file against the original.

        Temporarily swaps the built .obj, runs objdiff in project mode,
        and returns the match percentage.
        """
        # Copy the variant obj over the standard obj path so objdiff finds it
        shutil.copy2(obj_path, self._obj_path)
        match_pct, _ = self._run_objdiff(include_instructions=False)
        return match_pct

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

    def _dep_file_path(self) -> Path:
        """Path to the compiler-generated .d file alongside the .o target."""
        return self._obj_path.with_suffix(".d")

    def _current_dep_hash(self) -> Optional[str]:
        """Hash the current state of every header listed in the last build's
        .d file. Returns None if the .d file is missing (e.g., fresh build dir);
        cache lookups then bypass the dep-hash check and behave like the legacy
        cache. After the first build, the .d file is populated and dep-hash
        verification kicks in.
        """
        return compute_dep_hash(self._dep_file_path())

    def _check_dedup(self, variant: Variant) -> Optional[ScoreResult]:
        """Check dedup layers (source dedup + persistent cache).

        Returns a ScoreResult if the variant can be skipped, None otherwise.
        Thread-safe: only reads from cache, no builds.
        """
        source_md5 = self._variant_source_md5(variant)

        # Layer 1: Source dedup — identical to baseline
        if source_md5 == self._original_source_md5:
            if self._cache:
                self._cache.hits_source += 1
            return ScoreResult(
                variant=variant,
                match_percent=self._baseline_pct if hasattr(self, '_baseline_pct') else 0.0,
                build_success=True,
                error="source_dedup",
            )

        # Layer 2: Persistent cache lookup — but only honor a hit if the
        # current header state matches what was cached. Without this check
        # the cache reports stale 100% results when a batched edit touched
        # any transitively-included header.
        if self._cache:
            current_dep_hash = self._current_dep_hash()
            cached = self._cache.lookup_source(source_md5, current_dep_hash)
            if cached is not None:
                match_pct, build_ok = cached
                return ScoreResult(
                    variant=variant,
                    match_percent=match_pct,
                    build_success=build_ok,
                    error="cache_hit" if build_ok else "cached_build_fail",
                )

        return None

    def score(self, variant: Variant) -> ScoreResult:
        """Write variant source, build, score, and return result.

        Applies three dedup layers before doing expensive work:
        1. Source dedup: identical source to baseline → return baseline score
        2. Persistent cache: (symbol, source_md5) already scored → return cached
        3. Obj hash dedup: same .obj as a previous variant → reuse score
        """
        # Layers 1 & 2
        dedup_result = self._check_dedup(variant)
        if dedup_result is not None:
            return dedup_result

        source_md5 = self._variant_source_md5(variant)

        # No cache hit — must build
        self._apply_variant_files(variant)

        build_ok, build_error = self._build()
        if not build_ok:
            if self._cache:
                # A build failure has no reliable dep_hash — write a fresh
                # entry but stamp dep_hash=None so it's invalidated next session.
                self._cache.store(source_md5, None, 0.0, False, dep_hash=None)
            return ScoreResult(
                variant=variant,
                match_percent=0.0,
                build_success=False,
                error=build_error,
            )

        # After a successful build the .d file reflects this build's deps;
        # capture the dep_hash so future lookups can detect stale header state.
        post_build_dep_hash = self._current_dep_hash()

        # Layer 3: Obj hash dedup — same .obj means same score
        obj_md5 = md5_file(self._obj_path) if self._obj_path.exists() else None

        if obj_md5 and self._cache:
            obj_cached = self._cache.lookup_obj(obj_md5)
            if obj_cached is not None:
                # Store in persistent cache too
                self._cache.store(
                    source_md5, obj_md5, obj_cached, True,
                    dep_hash=post_build_dep_hash,
                )
                return ScoreResult(
                    variant=variant,
                    match_percent=obj_cached,
                    build_success=True,
                    error="obj_dedup",
                )

        # Full scoring path — run objdiff
        match_percent, _ = self._run_objdiff()

        # Store in cache
        if self._cache:
            self._cache.store(
                source_md5, obj_md5, match_percent, True,
                dep_hash=post_build_dep_hash,
            )

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

    def score_batch(
        self,
        variants: list[Variant],
        workers: int = 6,
    ) -> list[ScoreResult]:
        """Score multiple variants with parallel compilation.

        Workflow:
        1. Run dedup layers on all variants (instant, serial)
        2. Compile remaining variants in parallel (ThreadPoolExecutor)
        3. Score compiled .obj files sequentially via objdiff
        4. Return results in the same order as input variants

        Args:
            variants: List of variants to score.
            workers: Number of parallel compile workers (default: 6).

        Returns:
            List of ScoreResults in same order as input.
        """
        results: list[Optional[ScoreResult]] = [None] * len(variants)
        # Track which variants need building: (index, variant, source_md5)
        to_build: list[tuple[int, Variant, str]] = []

        # Phase 1: Dedup all variants (instant)
        for i, variant in enumerate(variants):
            dedup_result = self._check_dedup(variant)
            if dedup_result is not None:
                results[i] = dedup_result
            else:
                source_md5 = self._variant_source_md5(variant)
                to_build.append((i, variant, source_md5))

        if not to_build:
            return results  # type: ignore[return-value]

        # Phase 2: Parallel compilation
        # Create temp dir for variant .obj files
        tmp_dir = Path(tempfile.mkdtemp(prefix="permuter_"))
        try:
            # Build function for thread pool. Each worker writes to its own
            # `work_src` file so they can compile concurrently — without this,
            # the previous source_lock serialized the whole pool to ~1
            # variant at a time despite max_workers=N.
            #
            # `_apply_variant_files` still mutates the scorer's auxiliary
            # files (headers etc.), so its call lives outside the per-variant
            # work-src write. For variants that have auxiliary file updates
            # this is unavoidably serialized; for body-only variants (the
            # common case) parallelism is now real.
            apply_lock = threading.Lock()

            # Per-worker source files must live in the SAME directory as the
            # original source — the compile cmd has the source's directory
            # baked in via the -c flag, and redirect_source_in_cmd only swaps
            # the filename, not the path. We use the scorer's _working_dir
            # (same as the single-threaded _working_source) plus an idx
            # suffix to keep names unique across workers in the same batch.
            worker_src_paths: list[Path] = []

            def _compile_worker(
                idx: int, variant: Variant, source_md5: str, obj_out: Path,
            ) -> tuple[int, bool, str | None, str, Path]:
                """Compile one variant. Returns (idx, build_ok, error, source_md5, obj_out)."""
                # Only serialize the aux-file application; the heavy compile
                # itself runs in parallel via a per-worker source path.
                with apply_lock:
                    self._apply_variant_files(variant)
                work_src = (
                    self._working_dir
                    / f".permuter_work_{idx}_{self.source_path.name}"
                )
                worker_src_paths.append(work_src)
                build_ok, build_error = self._build_to_path(
                    variant.source, obj_out, work_src=work_src,
                )
                return (idx, build_ok, build_error, source_md5, obj_out)

            # Capture header-set hash once for the whole batch — every variant
            # in this batch shares the same source tree state, so a single
            # snapshot suffices for invalidation tracking.
            batch_dep_hash = self._current_dep_hash()

            futures = {}
            with ThreadPoolExecutor(max_workers=workers) as pool:
                for idx, variant, source_md5 in to_build:
                    obj_out = tmp_dir / f"variant_{idx}{self._project.obj_extension}"
                    future = pool.submit(
                        _compile_worker, idx, variant, source_md5, obj_out,
                    )
                    futures[future] = (idx, variant, source_md5)

                # Collect compile results
                compiled: list[tuple[int, Variant, str, Path]] = []
                for future in as_completed(futures):
                    idx, build_ok, build_error, source_md5, obj_out = future.result()
                    orig_idx, variant, _ = futures[future]

                    if not build_ok:
                        if self._cache:
                            self._cache.store(
                                source_md5, None, 0.0, False,
                                dep_hash=None,
                            )
                        results[orig_idx] = ScoreResult(
                            variant=variant,
                            match_percent=0.0,
                            build_success=False,
                            error=build_error,
                        )
                    else:
                        compiled.append((orig_idx, variant, source_md5, obj_out))

            # Phase 3: Score compiled objects sequentially (objdiff is fast)
            for orig_idx, variant, source_md5, obj_out in compiled:
                if not obj_out.exists():
                    results[orig_idx] = ScoreResult(
                        variant=variant,
                        match_percent=0.0,
                        build_success=False,
                        error="obj file missing after compile",
                    )
                    continue

                # Obj hash dedup
                obj_md5 = md5_file(obj_out)
                if self._cache:
                    obj_cached = self._cache.lookup_obj(obj_md5)
                    if obj_cached is not None:
                        self._cache.store(
                            source_md5, obj_md5, obj_cached, True,
                            dep_hash=batch_dep_hash,
                        )
                        results[orig_idx] = ScoreResult(
                            variant=variant,
                            match_percent=obj_cached,
                            build_success=True,
                            error="obj_dedup",
                        )
                        continue

                # Run objdiff on this .obj
                match_percent = self._run_objdiff_on_obj(obj_out)

                if self._cache:
                    self._cache.store(
                        source_md5, obj_md5, match_percent, True,
                        dep_hash=batch_dep_hash,
                    )

                results[orig_idx] = ScoreResult(
                    variant=variant,
                    match_percent=match_percent,
                    build_success=True,
                )
        finally:
            # Clean up temp dir + per-worker source files
            shutil.rmtree(tmp_dir, ignore_errors=True)
            for p in worker_src_paths:
                try:
                    p.unlink(missing_ok=True)
                    # Best-effort cleanup of the .d file MWCC emits alongside.
                    dep_file = p.with_suffix(p.suffix + ".d")
                    dep_file.unlink(missing_ok=True)
                except OSError:
                    pass

        return results  # type: ignore[return-value]

    def get_baseline(
        self,
        guided: bool = True,
        ghidra: bool = False,
        m2c: bool = False,
    ) -> float:
        """Score the unmodified source. Must be called within context manager.

        When guided=True, runs objdiff with --include-instructions and
        produces a Diagnosis for pattern filtering.
        When ghidra=True, also looks up cached Ghidra decompilation.
        When m2c=True, also tries to load machine-shaped m2c decompilation.
        """
        if self._original_source is None:
            raise RuntimeError("get_baseline() must be called within context manager")
        # Write original source to working copy for compilation
        atomic_write_bytes(self._working_source, self._original_source)
        build_ok, _ = self._build()
        if not build_ok:
            self._baseline_pct = 0.0
            return 0.0
        baseline, objdiff_data = self._run_objdiff(include_instructions=guided)

        if guided and objdiff_data and objdiff_data.get("instructions"):
            from .diagnosis import diagnose_baseline
            self.diagnosis = diagnose_baseline(objdiff_data)

        # Ghidra cache lookup
        if ghidra:
            try:
                from .ghidra_cache import get_or_cache_decompilation, GhidraCircuitOpen
                from .ghidra_ast import parse_ghidra, extract_savegpr_count
                code = get_or_cache_decompilation(self.symbol)
                if code:
                    self.ghidra_code = code
                    self.ghidra_ast = parse_ghidra(code)
                    gpr_saves = extract_savegpr_count(code)
                    if gpr_saves is not None:
                        print(f"  Ghidra: loaded ({len(code)} bytes, "
                              f"GPR saves={gpr_saves})", file=sys.stderr)
                    else:
                        print(f"  Ghidra: loaded ({len(code)} bytes)",
                              file=sys.stderr)
            except GhidraCircuitOpen:
                raise  # Let circuit breaker propagate to hill_climb
            except Exception as e:
                print(f"  Ghidra: unavailable ({e})", file=sys.stderr)

        if m2c:
            try:
                from .m2c import get_or_run_m2c
                code = get_or_run_m2c(self.symbol, self.unit)
                if code:
                    self.m2c_code = code
                    print(f"  m2c: loaded ({len(code)} bytes)", file=sys.stderr)
            except Exception as e:
                print(f"  m2c: unavailable ({e})", file=sys.stderr)

        # Check for ASM listing (for Ghidra+ASM crossref)
        # The /FAs listing would be at the same path as .obj but with .asm/.cod extension
        self.asm_listing_path = None
        if ghidra:
            for ext in (".asm", ".cod"):
                asm_path = self._obj_path.with_suffix(ext)
                if asm_path.exists():
                    self.asm_listing_path = asm_path
                    break

        self._baseline_equivalent = self._check_equivalence()
        self._baseline_pct = baseline
        # Stash objdiff data for optional attribution
        self._baseline_objdiff_data = objdiff_data if guided else None

        # Seed the obj hash cache with baseline obj
        if self._cache and self._obj_path.exists():
            obj_md5 = md5_file(self._obj_path)
            self._cache._obj_scores[obj_md5] = baseline
            # Also store baseline in persistent cache. The .d file alongside
            # the .o was just regenerated by the baseline build, so its dep
            # hash represents the canonical "this baseline build's headers"
            # state — exactly what we want to invalidate on next session if
            # any of those headers change.
            if self._original_source_md5:
                self._cache.store(
                    self._original_source_md5, obj_md5, baseline, True,
                    dep_hash=self._current_dep_hash(),
                )

        return baseline

    def get_attribution(self) -> list:
        """Compute source-attributed mismatch regions for the baseline.

        Compiles with /FAs, parses the listing, and joins with the objdiff
        instruction data from get_baseline(guided=True).

        Returns a list of MismatchRegion objects, or empty list on failure.
        Must be called after get_baseline(guided=True).
        """
        objdiff_data = getattr(self, "_baseline_objdiff_data", None)
        if not objdiff_data or not objdiff_data.get("instructions"):
            return []

        try:
            from tools.compiler_trace.invoker import CompilerInvoker
            from .attribution import attribute_function
        except ImportError:
            return []

        # Compile with /FAs to get listing
        invoker = CompilerInvoker()
        output_dir = Path("/tmp/claude") / "attribution" / self.source_path.stem
        output_dir.mkdir(parents=True, exist_ok=True)

        result = invoker.compile_with_asm(
            self.source_path, output_dir, listing_type="/FAs",
        )
        if result.returncode != 0:
            return []

        # Find listing file
        listing_text = None
        for ext in (".asm", ".cod"):
            p = output_dir / (self.source_path.stem + ext)
            if p.exists():
                listing_text = p.read_text(errors="replace")
                break
        if not listing_text:
            for p in output_dir.iterdir():
                if p.suffix in (".asm", ".cod"):
                    listing_text = p.read_text(errors="replace")
                    break
        if not listing_text:
            return []

        # Convert objdiff instructions to attribution format
        diff_instrs = []
        for ins in objdiff_data["instructions"]:
            mt = ins.get("match_type", "equal")
            if mt == "equal":
                kind = "match"
            elif mt in ("diff_op", "replace"):
                kind = "replace"
            elif mt == "insert":
                kind = "insert"
            elif mt == "delete":
                kind = "delete"
            elif mt == "diff_arg":
                kind = "replace"
            else:
                kind = "match"
            target = ins.get("target") or {}
            base = ins.get("base") or {}
            diff_instrs.append({
                "index": ins.get("index", -1),
                "diff_kind": kind,
                "target_opcode": target.get("opcode", ""),
                "base_opcode": base.get("opcode", ""),
            })

        # Run attribution
        _, _, regions = attribute_function(listing_text, self.symbol, diff_instrs)
        return regions

    def get_shape_facts(self) -> list[dict]:
        """Compute derived PPC shape facts for the baseline via /FAcs listing."""
        cached = getattr(self, "_baseline_shape_facts", None)
        if cached is not None:
            return cached

        try:
            from tools.compiler_trace.invoker import CompilerInvoker
            from .ppc_shape_facts import extract_shape_facts_from_listing
        except ImportError:
            return []

        invoker = CompilerInvoker()
        output_dir = Path("/tmp/claude") / "shape_facts" / self.source_path.stem
        output_dir.mkdir(parents=True, exist_ok=True)

        result = invoker.compile_with_asm(
            self.source_path, output_dir, listing_type="/FAcs",
        )
        if result.returncode != 0:
            self._baseline_shape_facts = []
            return []

        listing_text = None
        for ext in (".cod", ".asm"):
            p = output_dir / (self.source_path.stem + ext)
            if p.exists():
                listing_text = p.read_text(errors="replace")
                break
        if not listing_text:
            for p in output_dir.iterdir():
                if p.suffix in (".cod", ".asm"):
                    listing_text = p.read_text(errors="replace")
                    break
        if not listing_text:
            self._baseline_shape_facts = []
            return []

        facts = extract_shape_facts_from_listing(listing_text, self.symbol)
        self._baseline_shape_facts = facts
        return facts
