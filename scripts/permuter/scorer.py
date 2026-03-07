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
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
from typing import Optional

from .score_cache import ScoreCache, md5_bytes, md5_file
from .types import Variant, ScoreResult, Diagnosis

REPO_ROOT = Path(__file__).resolve().parent.parent.parent


class Scorer:
    """Scores variants by writing source, building, and running objdiff.

    Use as a context manager to guarantee source file restoration:

        with Scorer(source_path, symbol) as scorer:
            baseline = scorer.get_baseline()
            result = scorer.score(variant)

            # Or parallel:
            results = scorer.score_batch(variants, workers=8)
    """

    def __init__(self, source_path: Path, symbol: str, unit: Optional[str] = None):
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

        # Derive targeted object path from source path.
        # Accept either relative or absolute source paths.
        # src/system/rndobj/Foo.cpp -> build/373307D9/src/system/rndobj/Foo.obj
        source_obj_path = source_path.with_suffix(".obj")
        try:
            if source_obj_path.is_absolute():
                source_obj_path = source_obj_path.relative_to(REPO_ROOT)
        except ValueError:
            # Leave path unchanged if it's outside repo root.
            pass

        self._obj_target = f"build/373307D9/{source_obj_path}"
        self._obj_path = Path(self._obj_target)
        self._compile_cwd: Optional[str] = None
        self._compile_shell_cmd: Optional[str] = None

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
        self._original_source_md5 = md5_bytes(self._original_source)
        shutil.copy2(self.source_path, self._backup_path)
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
        # Always restore original source
        if self._original_source is not None:
            self.source_path.write_bytes(self._original_source)
        if self._backup_path and self._backup_path.exists():
            self._backup_path.unlink()
        return False

    def _extract_compile_cmd(self) -> None:
        """Extract the cl.exe command from ninja for direct invocation."""
        result = subprocess.run(
            ["ninja", "-t", "commands", self._obj_target],
            capture_output=True, text=True,
        )
        # ninja -t commands outputs multiple lines (download_tool, PCH, actual compile).
        # We need the LAST "cd " line — that's the actual .obj compile command.
        cmd_line = None
        for line in result.stdout.strip().splitlines():
            if line.startswith("cd "):
                cmd_line = line  # keep overwriting — last one wins

        if cmd_line is None:
            lines = result.stdout.strip().splitlines()
            if not lines:
                err = result.stderr.strip()
                details = f"; stderr: {err}" if err else ""
                raise RuntimeError(
                    f"Could not derive compile command for target '{self._obj_target}'"
                    f" from 'ninja -t commands'{details}"
                )
            # Fallback: use last line
            cmd_line = lines[-1]

        if cmd_line.startswith("cd "):
            parts = cmd_line.split(" && ", 1)
            self._compile_cwd = parts[0][3:]  # strip "cd "
            self._compile_shell_cmd = parts[1]
        else:
            self._compile_cwd = None
            self._compile_shell_cmd = cmd_line

        # Extract the absolute /Fo path from the command for reliable replacement
        fo_match = re.search(r'/Fo(\S+)', self._compile_shell_cmd)
        if fo_match:
            self._compile_fo_path = fo_match.group(1)
        else:
            self._compile_fo_path = None

    def _build(self) -> tuple[bool, str | None]:
        """Compile directly via cl.exe, bypassing ninja dep-checking."""
        if self._compile_shell_cmd is None:
            self._extract_compile_cmd()

        result = subprocess.run(
            self._compile_shell_cmd,
            shell=True,
            cwd=self._compile_cwd,
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            combined = result.stdout + result.stderr
            if "error" in combined.lower():
                return False, combined
        return True, None

    def _build_to_path(
        self, source_bytes: bytes, obj_output: Path, source_tmp: Path | None = None,
    ) -> tuple[bool, str | None]:
        """Compile variant source to a specific .obj path (for parallel builds).

        When source_tmp is provided, writes variant source there and compiles
        from that path (enables true parallelism — no shared source file).
        """
        if self._compile_shell_cmd is None:
            self._extract_compile_cmd()

        # Replace /FoOriginal with /FoTemp in the compile command
        if self._compile_fo_path:
            cmd = self._compile_shell_cmd.replace(
                f"/Fo{self._compile_fo_path}", f"/Fo{obj_output}"
            )
        else:
            cmd = self._compile_shell_cmd.replace(
                str(self._obj_path), str(obj_output)
            )

        if source_tmp is not None:
            # Write to private temp file — no lock needed
            source_tmp.write_bytes(source_bytes)
            # Replace the trailing source filename with /Tp<absolute_temp_path>
            # to tell MSVC to compile from the temp file instead
            src_basename = self.source_path.name
            cmd = cmd.rsplit(src_basename, 1)
            cmd = f"/Tp{source_tmp}".join(cmd)
        else:
            # Legacy path: write to real source (caller must serialize)
            self.source_path.write_bytes(source_bytes)

        result = subprocess.run(
            cmd,
            shell=True,
            cwd=self._compile_cwd,
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
        cmd = ["bin/objdiff-cli", "diff", "-p", ".", self.symbol,
               "-c", "functionRelocDiffs=none", "-f", "json"]
        if include_instructions:
            cmd.append("--include-instructions")
        result = subprocess.run(cmd, capture_output=True, text=True)
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

    def _check_dedup(self, variant: Variant) -> Optional[ScoreResult]:
        """Check dedup layers (source dedup + persistent cache).

        Returns a ScoreResult if the variant can be skipped, None otherwise.
        Thread-safe: only reads from cache, no builds.
        """
        source_md5 = md5_bytes(variant.source)

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

        # Layer 2: Persistent cache lookup
        if self._cache:
            cached = self._cache.lookup_source(source_md5)
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

        source_md5 = md5_bytes(variant.source)

        # No cache hit — must build
        self.source_path.write_bytes(variant.source)

        build_ok, build_error = self._build()
        if not build_ok:
            if self._cache:
                self._cache.store(source_md5, None, 0.0, False)
            return ScoreResult(
                variant=variant,
                match_percent=0.0,
                build_success=False,
                error=build_error,
            )

        # Layer 3: Obj hash dedup — same .obj means same score
        obj_md5 = md5_file(self._obj_path) if self._obj_path.exists() else None

        if obj_md5 and self._cache:
            obj_cached = self._cache.lookup_obj(obj_md5)
            if obj_cached is not None:
                # Store in persistent cache too
                self._cache.store(source_md5, obj_md5, obj_cached, True)
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
            self._cache.store(source_md5, obj_md5, match_percent, True)

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
        2. Compile remaining variants in parallel — each thread writes to its
           own temp .cpp file (no shared source file, true parallelism)
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
                source_md5 = md5_bytes(variant.source)
                to_build.append((i, variant, source_md5))

        if not to_build:
            return results  # type: ignore[return-value]

        # Phase 2: Parallel compilation
        # Each thread gets its own temp .cpp and .obj — no shared source file
        n_dedup = sum(1 for r in results if r is not None)
        n_build = len(to_build)
        if n_dedup > 0:
            print(
                f"  Dedup: {n_dedup} cached, {n_build} to compile "
                f"({workers} workers)",
                file=sys.stderr,
            )

        tmp_dir = Path(tempfile.mkdtemp(prefix="permuter_"))
        try:
            def _compile_worker(
                idx: int, variant: Variant, source_md5: str,
                obj_out: Path, src_tmp: Path,
            ) -> tuple[int, bool, str | None, str, Path]:
                """Compile one variant to its own temp files."""
                build_ok, build_error = self._build_to_path(
                    variant.source, obj_out, source_tmp=src_tmp,
                )
                return (idx, build_ok, build_error, source_md5, obj_out)

            compiled: list[tuple[int, Variant, str, Path]] = []
            build_done = 0
            build_fail = 0

            # Use ThreadPoolExecutor but manage shutdown ourselves to
            # avoid the wait=True hang on KeyboardInterrupt.
            pool = ThreadPoolExecutor(max_workers=workers)
            try:
                futures = {}
                for idx, variant, source_md5 in to_build:
                    obj_out = tmp_dir / f"variant_{idx}.obj"
                    src_tmp = tmp_dir / f"variant_{idx}.cpp"
                    future = pool.submit(
                        _compile_worker, idx, variant, source_md5,
                        obj_out, src_tmp,
                    )
                    futures[future] = (idx, variant, source_md5)

                for future in as_completed(futures):
                    idx, build_ok, build_error, source_md5, obj_out = future.result()
                    orig_idx, variant, _ = futures[future]
                    build_done += 1

                    if not build_ok:
                        build_fail += 1
                        if self._cache:
                            self._cache.store(source_md5, None, 0.0, False)
                        results[orig_idx] = ScoreResult(
                            variant=variant,
                            match_percent=0.0,
                            build_success=False,
                            error=build_error,
                        )
                    else:
                        compiled.append((orig_idx, variant, source_md5, obj_out))

                    # Progress every 10 builds or on last
                    if build_done % 10 == 0 or build_done == n_build:
                        fail_str = f", {build_fail} failed" if build_fail else ""
                        print(
                            f"  Compiled {build_done}/{n_build}{fail_str}",
                            file=sys.stderr,
                        )
            except KeyboardInterrupt:
                for f in futures:
                    f.cancel()
                pool.shutdown(wait=False, cancel_futures=True)
                raise
            else:
                pool.shutdown(wait=True)

            # Phase 3: Score compiled objects sequentially (objdiff is fast)
            n_to_score = len(compiled)
            if n_to_score > 0:
                print(
                    f"  Scoring {n_to_score} compiled objects...",
                    file=sys.stderr,
                )
            score_done = 0
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
                        self._cache.store(source_md5, obj_md5, obj_cached, True)
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
                    self._cache.store(source_md5, obj_md5, match_percent, True)

                results[orig_idx] = ScoreResult(
                    variant=variant,
                    match_percent=match_percent,
                    build_success=True,
                )
        finally:
            # Clean up temp dir
            shutil.rmtree(tmp_dir, ignore_errors=True)

        return results  # type: ignore[return-value]

    def get_baseline(self, guided: bool = True, ghidra: bool = False) -> float:
        """Score the unmodified source. Must be called within context manager.

        When guided=True, runs objdiff with --include-instructions and
        produces a Diagnosis for pattern filtering.
        When ghidra=True, also looks up cached Ghidra decompilation.
        """
        if self._original_source is None:
            raise RuntimeError("get_baseline() must be called within context manager")
        # Ensure original source is written
        self.source_path.write_bytes(self._original_source)
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
                from .ghidra_cache import get_or_cache_decompilation
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
            except Exception as e:
                print(f"  Ghidra: unavailable ({e})", file=sys.stderr)

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

        # Seed the obj hash cache with baseline obj
        if self._cache and self._obj_path.exists():
            obj_md5 = md5_file(self._obj_path)
            self._cache._obj_scores[obj_md5] = baseline
            # Also store baseline in persistent cache
            if self._original_source_md5:
                self._cache.store(self._original_source_md5, obj_md5, baseline, True)

        return baseline
