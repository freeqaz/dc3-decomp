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
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
from typing import Optional

from .file_util import (
    apply_file_updates,
    atomic_write_bytes,
    restore_tracked_files,
    SourceFileLock,
)
from .preprocess_cache import (
    PreprocessCache,
    derive_preprocess_command,
    fast_path_enabled,
)
from .profiling import get_profiler
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
        # Preprocessed-splice fast path (env-gated, RB3/mwcceppc only).
        self._pp_cache: Optional[PreprocessCache] = None
        self._pp_cache_inited = False
        self._pp_cache_lock = threading.Lock()
        self._pp_qualified_name: Optional[str] = None

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
        # Check for stale backup from a previous crash/kill. Only auto-restore
        # if the backup is younger than the source — otherwise the backup
        # predates committed work and restoring it would silently destroy
        # newer changes (see e.g. GemTrack.cpp.permuter_bak stuck from May 14).
        if self._backup_path.exists():
            try:
                bak_mtime = self._backup_path.stat().st_mtime
                src_mtime = self.source_path.stat().st_mtime
            except OSError:
                bak_mtime = src_mtime = 0.0
            if bak_mtime >= src_mtime:
                print(
                    f"  WARNING: Found stale backup {self._backup_path.name} — "
                    f"restoring from previous interrupted run.",
                    file=sys.stderr,
                )
                shutil.copy2(self._backup_path, self.source_path)
                self._backup_path.unlink()
            else:
                # Backup is OLDER than the source: the source has been committed
                # or modified since the backup was made, so the backup is an
                # orphan from an old killed run. Restoring it would clobber the
                # newer committed work — so DON'T restore. Just discard the stale
                # orphan and proceed with the current (newer) source as baseline.
                # Erroring out here used to wedge ~27 functions per sweep.
                print(
                    f"  WARNING: Discarding stale backup {self._backup_path.name} "
                    f"(older than source — predates committed work); proceeding "
                    f"with current source.",
                    file=sys.stderr,
                )
                try:
                    self._backup_path.unlink()
                except OSError:
                    pass

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
        # Restore the main source from the original bytes. The to_disk=True
        # path in IL capture writes variants directly to self.source_path; if
        # the loop completed (or threw) before restoring, the last variant is
        # still on disk. Restoring unconditionally is cheap and safe — by
        # contract, scoring never legitimately mutates the real source.
        if getattr(self, "_original_source", None) is not None:
            try:
                if self.source_path.read_bytes() != self._original_source:
                    atomic_write_bytes(self.source_path, self._original_source)
            except OSError:
                pass
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

        _t0 = time.perf_counter()
        result = subprocess.run(
            cmd,
            shell=True,
            cwd=self._compile_cwd or str(self._project.repo_root),
            capture_output=True,
            text=True,
        )
        get_profiler().record_subprocess("compile", time.perf_counter() - _t0)
        if result.returncode != 0:
            combined = result.stdout + result.stderr
            if "error" in combined.lower():
                return False, combined
        return True, None

    def _init_preprocess_cache(self) -> Optional[PreprocessCache]:
        """Lazily build the preprocessed-splice cache (once per session).

        Returns the cache if the fast path is usable for this scorer, else
        None. Thread-safe: holds a lock so parallel workers don't double-build.
        Any failure disables the cache permanently (returns None thereafter).
        """
        if self._pp_cache_inited:
            return self._pp_cache
        with self._pp_cache_lock:
            if self._pp_cache_inited:
                return self._pp_cache
            self._pp_cache_inited = True
            try:
                self._pp_cache = self._build_preprocess_cache()
            except Exception as exc:  # noqa: BLE001 — never crash a sweep
                print(f"  preprocess-cache: init failed ({exc})", file=sys.stderr)
                self._pp_cache = None
            return self._pp_cache

    def _build_preprocess_cache(self) -> Optional[PreprocessCache]:
        """Run the one-time ``-E`` preprocess + macro-liveness probe."""
        # The fast path only applies to the mwcceppc (RB3) toolchain. MSVC's
        # /E + splice has not been validated and the splice-region byte
        # identity is toolchain-specific.
        if self._project.project_type != ProjectType.RB3:
            return None
        if self._original_source is None:
            return None
        if self._compile_shell_cmd is None:
            self._extract_compile_cmd()

        # Resolve and cache the qualified name used by both the baseline range
        # lookup and per-variant range location.
        self._pp_qualified_name = self._lookup_qualified_name()
        if not self._pp_qualified_name:
            return None

        # We need the baseline function byte range to locate + gate.
        func_range = self._baseline_func_byte_range()
        if func_range is None:
            return None

        cache = PreprocessCache(self._project.repo_root, self.source_path)
        # Write a probe-augmented copy of the source. CRITICAL: the input file's
        # BASENAME must equal the real source's basename, because mwcceppc bakes
        # `__FILE__` (= the basename) into MILO_ASSERT/MakeString strings. A
        # different basename shifts the whole string pool and every downstream
        # offset, so the .o would not be byte-identical. We place a same-named
        # file in a private temp subdir (includes are repo-root-relative, so
        # the directory doesn't matter — only the basename does).
        probe_src = cache.probe_source(self._original_source)
        pp_dir = self._samename_work_dir("pp")
        pp_work = pp_dir / self.source_path.name
        pp_out = self._working_dir / f".permuter_pp_out_{self.source_path.name}"
        try:
            atomic_write_bytes(pp_work, probe_src)
            cmd = self._redirect_source_path(self._compile_shell_cmd, pp_work)
            if cmd is None:
                return None
            cmd = derive_preprocess_command(cmd, self._compile_fo_path, pp_out)
            if cmd is None:
                return None
            result = subprocess.run(
                cmd, shell=True,
                cwd=self._compile_cwd or str(self._project.repo_root),
                capture_output=True, text=False,
            )
            if result.returncode != 0 or not pp_out.exists():
                return None
            pp_text = pp_out.read_text(errors="surrogateescape")
        finally:
            shutil.rmtree(pp_dir, ignore_errors=True)
            pp_out.unlink(missing_ok=True)

        ok = cache.prepare_from_text(pp_text, self._original_source, func_range)
        if not ok or cache.disabled:
            return None

        # Self-validation: compile the baseline through the fast path and the
        # normal path and require byte-identical .o. This proves the splice is
        # sound for THIS function before any variant trusts it.
        if not self._validate_preprocess_cache(cache, func_range):
            return None
        return cache

    def _baseline_func_byte_range(self) -> Optional[tuple[int, int]]:
        """Best-effort byte range of the target function in the original source.

        Uses the extractor (tree-sitter) to find the function definition. The
        function is identified by demangling the symbol via the cache DB's
        stored demangled name when available, else parsed from the source.
        Returns None if it can't be determined (fast path then disabled).
        """
        if self._original_source is None:
            return None
        try:
            from .extractor import extract_function
        except Exception:
            return None
        qual = self._pp_qualified_name
        if not qual:
            return None
        try:
            ctx = extract_function(self.source_path, qual)
        except Exception:
            return None
        rng = ctx.func_byte_range
        if not rng:
            return None
        # extract_function parsed the on-disk source; ensure it matches our
        # cached original bytes (it should — restored by the context manager).
        if rng[0] < 0 or rng[1] > len(self._original_source) or rng[1] <= rng[0]:
            return None
        return rng

    def _lookup_qualified_name(self) -> Optional[str]:
        """Resolve the target symbol's qualified C++ name (Class::Method)."""
        try:
            from .repo_paths import get_decomp_db_path
            import sqlite3
            db = get_decomp_db_path()
            if not db.exists():
                return None
            conn = sqlite3.connect(str(db))
            try:
                row = conn.execute(
                    "SELECT demangled FROM functions WHERE symbol = ? LIMIT 1",
                    (self.symbol,),
                ).fetchone()
            finally:
                conn.close()
            if not row or not row[0]:
                return None
            from .types import extract_qualified_name
            return extract_qualified_name(row[0])
        except Exception:
            return None

    def _validate_preprocess_cache(
        self, cache: PreprocessCache, func_range: tuple[int, int]
    ) -> bool:
        """Compile the baseline both ways; require byte-identical .o."""
        if self._original_source is None:
            return False
        spliced = cache.splice(self._original_source, func_range)
        # Reset the just-incremented stat — this is validation, not a real hit.
        if spliced is not None:
            cache.fast_hits -= 1
        if spliced is None:
            # Baseline body references a live macro — can't validate, but the
            # gate is sound (it'll fall back on every such variant anyway).
            # Accept the cache; macro-bodies just never take the fast path.
            return True
        import tempfile as _tf
        tmp = Path(_tf.mkdtemp(prefix="permuter_ppval_"))
        try:
            normal_o = tmp / f"normal{self._project.obj_extension}"
            fast_o = tmp / f"fast{self._project.obj_extension}"
            # Normal path: compile the canonical source (real basename) so the
            # reference .o matches the target's __FILE__.
            ok_n, _ = self._compile_canonical(self._original_source, normal_o)
            ok_f, _ = self._compile_spliced(spliced, fast_o, tag="ppval_f")
            if not (ok_n and ok_f and normal_o.exists() and fast_o.exists()):
                return False

            # Strict check: byte-identical .o (holds for line-preserving cases).
            if md5_file(normal_o) == md5_file(fast_o):
                return True

            # In strict mode (PERMUTER_PREPROCESS_CACHE_STRICT=1) require exact
            # byte identity — disable the cache for this function otherwise.
            if os.environ.get("PERMUTER_PREPROCESS_CACHE_STRICT", "").strip() in (
                "1", "true", "yes", "on"
            ):
                return False

            # Default: accept score-equivalence. The objdiff fuzzy-match score
            # is the permuter's only judgement of a variant, and it ignores the
            # debug/symtab metadata bytes that can differ when the spliced `.i`
            # places the function at a different line offset than the raw .cpp.
            # We verify the spliced .o and canonical .o yield the SAME objdiff
            # score against the target before trusting the fast path.
            import shutil as _sh
            _sh.copy2(fast_o, self._obj_path)
            fast_pct, _ = self._run_objdiff()
            _sh.copy2(normal_o, self._obj_path)
            canon_pct, _ = self._run_objdiff()
            return abs(fast_pct - canon_pct) < 1e-6
        except Exception:
            return False
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def _samename_work_dir(self, tag: str) -> Path:
        """Create a private temp subdir for a same-basename work file.

        mwcceppc bakes ``__FILE__`` (the source BASENAME) into assert strings,
        so a fast-path work file must share the real source's basename to be
        byte-identical. We place it in a unique subdir so the basename can be
        reused without colliding with other workers / the real source.
        """
        d = self._working_dir / f".permuter_{tag}_{os.getpid()}_{threading.get_ident()}"
        d.mkdir(parents=True, exist_ok=True)
        return d

    def _redirect_source_path(self, cmd: str, new_src: Path) -> Optional[str]:
        """Replace the ``-c <source>`` argument in a compile command.

        Unlike ``redirect_source_in_cmd`` (which swaps only the basename and
        keeps the directory), this replaces the FULL source path token so the
        compiler reads from an arbitrary location while keeping the basename.
        Also injects ``-i <real-source-dir>`` so that relative quote-includes
        (``#include "Sibling.h"``) still resolve when the work file lives in a
        private temp subdir rather than the source's own directory. Verified
        byte-neutral: adding the source dir to the include search path does not
        change the ``.o`` for a normally-resolvable TU.
        """
        try:
            rel = str(self.source_path.resolve().relative_to(
                self._project.repo_root.resolve()))
        except ValueError:
            rel = None
        candidates = []
        if rel:
            candidates.append(rel)
        candidates.append(str(self.source_path))
        candidates.append(self.source_path.name)
        try:
            src_dir = str(self.source_path.resolve().parent.relative_to(
                self._project.repo_root.resolve()))
        except ValueError:
            src_dir = str(self.source_path.resolve().parent)
        for token in candidates:
            needle = f"-c {token}"
            if needle in cmd:
                redirected = cmd.replace(needle, f"-c {new_src}", 1)
                # Prepend the source dir to the include path so sibling
                # quote-includes resolve from the temp work dir.
                return redirected.replace(
                    "mwcceppc.exe ", f"mwcceppc.exe -i {src_dir} ", 1
                )
        return None

    def _compile_canonical(
        self, source_bytes: bytes, obj_output: Path,
    ) -> tuple[bool, str | None]:
        """Compile full (non-spliced) source via a same-basename work file.

        Produces a ``.o`` byte-identical to the real-source build (matching
        ``__FILE__``). Used only as the validation reference — the normal
        scoring path keeps using its own work files.
        """
        work_dir = self._samename_work_dir("canon")
        work_src = work_dir / self.source_path.name
        try:
            cmd = self._project.redirect_output_for_parallel(
                self._compile_shell_cmd,
                self._compile_fo_path,
                self._obj_path,
                obj_output,
            )
            redirected = self._redirect_source_path(cmd, work_src)
            if redirected is None:
                return False, "could not redirect source path"
            atomic_write_bytes(work_src, source_bytes)
            result = subprocess.run(
                redirected, shell=True,
                cwd=self._compile_cwd or str(self._project.repo_root),
                capture_output=True, text=True,
            )
            if result.returncode != 0:
                combined = result.stdout + result.stderr
                if "error" in combined.lower():
                    return False, combined
            return True, None
        finally:
            shutil.rmtree(work_dir, ignore_errors=True)

    def _compile_spliced(
        self, spliced_source: bytes, obj_output: Path, tag: str,
    ) -> tuple[bool, str | None]:
        """Compile already-spliced (preprocessed) source to ``obj_output``.

        Writes the spliced text to a same-basename file in a private temp dir
        (see :meth:`_samename_work_dir`) so the embedded ``__FILE__`` matches
        the canonical build and the resulting ``.o`` is byte-identical.
        """
        work_dir = self._samename_work_dir(tag)
        work_src = work_dir / self.source_path.name
        try:
            cmd = self._project.redirect_output_for_parallel(
                self._compile_shell_cmd,
                self._compile_fo_path,
                self._obj_path,
                obj_output,
            )
            redirected = self._redirect_source_path(cmd, work_src)
            if redirected is None:
                return False, "could not redirect source path for spliced compile"
            cmd = redirected
            atomic_write_bytes(work_src, spliced_source)
            _t0 = time.perf_counter()
            result = subprocess.run(
                cmd, shell=True,
                cwd=self._compile_cwd or str(self._project.repo_root),
                capture_output=True, text=True,
            )
            get_profiler().record_subprocess("compile", time.perf_counter() - _t0)
            if result.returncode != 0:
                combined = result.stdout + result.stderr
                if "error" in combined.lower():
                    return False, combined
            return True, None
        finally:
            shutil.rmtree(work_dir, ignore_errors=True)

    def _build_to_path(
        self, source_bytes: bytes, obj_output: Path,
        work_src: Path | None = None,
        allow_fast_path: bool = True,
    ) -> tuple[bool, str | None]:
        """Compile variant source to a specific object path (for parallel builds).

        Args:
            source_bytes: variant source.
            obj_output: where the .o lands.
            work_src: per-call source file path. When None, uses the scorer's
                shared `_working_source` (single-threaded fallback). For
                concurrent calls, pass a unique path per worker so threads
                don't clobber each other's writes.
            allow_fast_path: when True (default) and the env flag is set, try
                the preprocessed-splice fast path before a full compile.
        """
        if self._compile_shell_cmd is None:
            self._extract_compile_cmd()
        if work_src is None:
            work_src = self._working_source

        # ── Preprocessed-splice fast path ─────────────────────────────────
        # Splice the variant's function body into the cached preprocessed `.i`
        # and compile that — skipping the ~0.4s header re-parse. Falls through
        # to the normal compile on any miss (macro in body, no func range, …).
        if allow_fast_path and fast_path_enabled():
            cache = self._init_preprocess_cache()
            if cache is not None and not cache.disabled:
                func_range = self._variant_func_range(source_bytes)
                spliced = cache.splice(source_bytes, func_range)
                if spliced is not None:
                    ok, err = self._compile_spliced(
                        spliced, obj_output, tag="fast",
                    )
                    # A build error on the spliced path is unexpected; rather
                    # than trust a (possibly divergent) failure, fall through to
                    # the full compile so the variant is judged the canonical way.
                    if ok:
                        return ok, err

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

        _t0 = time.perf_counter()
        result = subprocess.run(
            cmd,
            shell=True,
            cwd=self._compile_cwd or str(self._project.repo_root),
            capture_output=True,
            text=True,
        )
        get_profiler().record_subprocess("compile", time.perf_counter() - _t0)
        if result.returncode != 0:
            combined = result.stdout + result.stderr
            if "error" in combined.lower():
                return False, combined
        return True, None

    def _variant_func_range(self, source_bytes: bytes) -> Optional[tuple[int, int]]:
        """Find the target function's byte range in a variant's source.

        Uses the self-contained regex/brace-match locator (cheap, no
        tree-sitter reparse). Returns None when it can't be determined (the
        fast path then declines this variant and a full compile runs).
        """
        qual = self._pp_qualified_name
        if not qual:
            return None
        from .preprocess_cache import _find_func_region
        try:
            text = source_bytes.decode("utf-8", errors="strict")
        except UnicodeDecodeError:
            return None
        region = _find_func_region(text, qual)
        if region is None:
            return None
        # _find_func_region returns char offsets; for ASCII source these equal
        # byte offsets. Re-encode the prefix to be exact under multibyte input.
        start_b = len(text[: region[0]].encode("utf-8"))
        end_b = len(text[: region[1]].encode("utf-8"))
        if 0 <= start_b < end_b <= len(source_bytes):
            return (start_b, end_b)
        return None

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
            # Restore the real source before returning. to_disk=True wrote
            # the last variant to self.source_path; leaving it there poisons
            # subsequent ninja builds (and is how parallel-sweep agents have
            # picked up "WIP" looking code that was never theirs).
            if self._original_source is not None:
                try:
                    if self.source_path.read_bytes() != self._original_source:
                        atomic_write_bytes(self.source_path, self._original_source)
                except OSError:
                    pass
            shutil.rmtree(tmp_dir, ignore_errors=True)
        return hashes

    def _run_objdiff(self, include_instructions: bool = False) -> tuple[float, dict | None]:
        """Run objdiff-cli and return (fuzzy_match_percent, full_json_dict).

        When include_instructions is True, passes --include-instructions for
        diagnosis. The JSON dict is only returned when include_instructions=True.

        Passes `-u UNIT` when self.unit is set — required when a symbol is
        shared across multiple translation units (e.g. inline stlport methods,
        LinkerMerged symbols). Without -u, objdiff-cli errors with "No such
        file" because it can't disambiguate.
        """
        objdiff = self._project.objdiff_cli
        cmd = [objdiff, "diff", "-p", "."]
        if self.unit:
            cmd.extend(["-u", self.unit])
        cmd.extend([self.symbol, "-c", "functionRelocDiffs=none", "-f", "json"])
        if include_instructions:
            cmd.append("--include-instructions")
        profiler = get_profiler()
        profiler.set_objdiff_binary(objdiff)
        _t0 = time.perf_counter()
        result = subprocess.run(
            cmd, capture_output=True, text=True,
            cwd=str(self._project.repo_root),
        )
        profiler.record_subprocess("objdiff", time.perf_counter() - _t0)
        try:
            data = json.loads(result.stdout)
            match_pct = data.get("fuzzy_match_percent", 0.0)
            return match_pct, data if include_instructions else None
        except (json.JSONDecodeError, KeyError):
            return 0.0, None

    def _run_objdiff_on_obj(self, obj_path: Path) -> float:
        """Run objdiff on a specific .obj file against the original.

        Uses -1 <target> -2 <base> to diff the variant obj directly against
        the original without touching self._obj_path, making this safe to call
        from multiple threads simultaneously.
        """
        target_obj = self._project.target_obj_for_base_obj(self._obj_path)
        objdiff = self._project.objdiff_cli
        cmd = [
            objdiff, "diff",
            "-1", str(target_obj),
            "-2", str(obj_path),
            self.symbol,
            "-c", "functionRelocDiffs=none",
            "-f", "json",
        ]
        result = subprocess.run(
            cmd, capture_output=True, text=True,
            cwd=str(self._project.repo_root),
        )
        try:
            data = json.loads(result.stdout)
            return data.get("fuzzy_match_percent", 0.0)
        except (json.JSONDecodeError, KeyError):
            return 0.0

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
        """Score multiple variants with parallel compilation and scoring.

        Workflow:
        1. Run dedup layers on all variants (instant, serial)
        2. Compile remaining variants in parallel (ThreadPoolExecutor)
        3. Score compiled .obj files in parallel via objdiff
        4. Return results in the same order as input variants

        Args:
            variants: List of variants to score.
            workers: Number of parallel compile/objdiff workers (default: 6).

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

            # Phase 3: Score compiled objects in parallel.
            # _run_objdiff_on_obj uses -1/-2 flags so each call diffs its own
            # obj file directly — no shared path is touched, making it reentrant.
            # ScoreCache._lock serializes SQLite writes and _obj_scores updates.
            # results[orig_idx] writes are safe because each idx is distinct.
            def _score_worker(
                orig_idx: int, variant: Variant, source_md5: str, obj_out: Path,
            ) -> tuple[int, ScoreResult]:
                if not obj_out.exists():
                    return orig_idx, ScoreResult(
                        variant=variant,
                        match_percent=0.0,
                        build_success=False,
                        error="obj file missing after compile",
                    )

                obj_md5 = md5_file(obj_out)
                if self._cache:
                    obj_cached = self._cache.lookup_obj(obj_md5)
                    if obj_cached is not None:
                        self._cache.store(
                            source_md5, obj_md5, obj_cached, True,
                            dep_hash=batch_dep_hash,
                        )
                        return orig_idx, ScoreResult(
                            variant=variant,
                            match_percent=obj_cached,
                            build_success=True,
                            error="obj_dedup",
                        )

                match_percent = self._run_objdiff_on_obj(obj_out)

                if self._cache:
                    self._cache.store(
                        source_md5, obj_md5, match_percent, True,
                        dep_hash=batch_dep_hash,
                    )

                return orig_idx, ScoreResult(
                    variant=variant,
                    match_percent=match_percent,
                    build_success=True,
                )

            with ThreadPoolExecutor(max_workers=workers) as score_pool:
                score_futures = [
                    score_pool.submit(_score_worker, orig_idx, variant, source_md5, obj_out)
                    for orig_idx, variant, source_md5, obj_out in compiled
                ]
                for fut in as_completed(score_futures):
                    idx, score_result = fut.result()
                    results[idx] = score_result
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

        # Eagerly build the preprocessed-splice cache here, on the
        # single-threaded baseline path, so its one-time `-E` + self-validation
        # (which temporarily writes self._obj_path) never races with parallel
        # score_batch workers. Cheap no-op when the fast path is disabled.
        if fast_path_enabled():
            try:
                self._init_preprocess_cache()
            except Exception:
                pass
            # Restore the baseline .o that validation may have overwritten, so
            # the obj-hash cache seed above stays consistent.
            try:
                if self._original_source is not None and self._obj_path.exists():
                    pass  # validation leaves the canonical baseline .o in place
            except OSError:
                pass

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
