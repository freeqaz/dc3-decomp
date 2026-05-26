"""Env-gated per-variant profiling for the permuter inner loop.

Activated by ``PERMUTER_PROFILE=1``. When off, every entry point here is a
near-zero-cost no-op so it can stay wired into the hot path permanently.

The inner loop is *compile + objdiff per variant*, both subprocesses. This
module attributes wall-clock across the buckets the performance roadmap
(``docs/plans/permuter/PERFORMANCE_ROADMAP.md`` item A0) cares about::

    generate         variant source generation (outside the scorer)
    compile-spawn    fork/exec/DLL-load floor for the compiler subprocess
    compile-run      compiler compute (full subprocess wall minus the spawn floor)
    objdiff-spawn    fork/exec floor for the objdiff subprocess
    objdiff-run      objdiff compute (full subprocess wall minus the spawn floor)
    python-overhead  everything else on the wall clock (dedup, hashing, I/O glue)

``compile-spawn`` / ``objdiff-spawn`` cannot be read directly off a single
``subprocess.run`` call (spawn and compute are one blocking syscall). We split
them with a *calibration floor*: at first use we time a trivial invocation of
each binary (the shell running ``true``; objdiff running ``--version``) and
treat that as the per-call spawn cost. Each measured subprocess call is then
charged ``spawn = min(floor, total)`` to its ``-spawn`` bucket and the
remainder to its ``-run`` bucket. The floor is therefore a *measured* number,
not an assumption — which is the whole point of A0.
"""

from __future__ import annotations

import json
import os
import subprocess
import threading
import time
from contextlib import contextmanager
from dataclasses import dataclass, field


def profiling_enabled() -> bool:
    return os.environ.get("PERMUTER_PROFILE", "") not in ("", "0", "false", "False")


@dataclass
class _Bucket:
    seconds: float = 0.0
    count: int = 0


@dataclass
class Profiler:
    """Thread-safe accumulator of timing buckets.

    Safe to share across the compile ThreadPoolExecutor: every mutation is
    guarded by a single lock and the per-call work outside the lock is just a
    pair of ``perf_counter`` reads.
    """

    enabled: bool = False
    _lock: threading.Lock = field(default_factory=threading.Lock, repr=False)
    buckets: dict[str, _Bucket] = field(default_factory=dict)
    # Measured per-call spawn floors (seconds). Populated lazily.
    _compile_spawn_floor: float | None = field(default=None, repr=False)
    _objdiff_spawn_floor: float | None = field(default=None, repr=False)
    wall_start: float = 0.0
    wall_end: float = 0.0

    def add(self, bucket: str, seconds: float, count: int = 1) -> None:
        if not self.enabled:
            return
        with self._lock:
            slot = self.buckets.setdefault(bucket, _Bucket())
            slot.seconds += seconds
            slot.count += count

    @contextmanager
    def timed(self, bucket: str):
        """Charge the wrapped block's wall time to ``bucket``."""
        if not self.enabled:
            yield
            return
        start = time.perf_counter()
        try:
            yield
        finally:
            self.add(bucket, time.perf_counter() - start)

    # ── Subprocess timing with spawn/run split ─────────────────────────────

    def record_subprocess(
        self, kind: str, total_seconds: float,
    ) -> None:
        """Split one subprocess call's wall time into ``<kind>-spawn`` /
        ``<kind>-run`` using the calibrated spawn floor.

        ``kind`` is ``"compile"`` or ``"objdiff"``.
        """
        if not self.enabled:
            return
        floor = self._spawn_floor(kind)
        spawn = min(floor, total_seconds)
        run = max(0.0, total_seconds - spawn)
        self.add(f"{kind}-spawn", spawn)
        self.add(f"{kind}-run", run)

    def _spawn_floor(self, kind: str) -> float:
        if kind == "compile":
            if self._compile_spawn_floor is None:
                self._compile_spawn_floor = _measure_shell_spawn_floor()
            return self._compile_spawn_floor
        if self._objdiff_spawn_floor is None:
            self._objdiff_spawn_floor = _measure_objdiff_spawn_floor()
        return self._objdiff_spawn_floor

    def set_objdiff_binary(self, path: str) -> None:
        """Tell the calibrator which objdiff binary to probe for the floor."""
        if not self.enabled:
            return
        global _OBJDIFF_BINARY
        _OBJDIFF_BINARY = path

    # ── Reporting ──────────────────────────────────────────────────────────

    def start_wall(self) -> None:
        if self.enabled:
            self.wall_start = time.perf_counter()

    def stop_wall(self) -> None:
        if self.enabled:
            self.wall_end = time.perf_counter()

    def summary(self) -> dict:
        """Return a JSON-serializable breakdown.

        ``python-overhead`` is the residual: total wall minus everything we
        explicitly timed. It absorbs dedup, hashing, thread scheduling, and
        any un-instrumented glue, so it is honest about what we did *not*
        attribute rather than silently dropping it.
        """
        wall = max(0.0, self.wall_end - self.wall_start)
        attributed = sum(b.seconds for b in self.buckets.values())
        out = {
            name: {"seconds": round(b.seconds, 4), "count": b.count}
            for name, b in sorted(self.buckets.items())
        }
        residual = max(0.0, wall - attributed)
        out["python-overhead"] = {"seconds": round(residual, 4), "count": 0}
        out["_wall_seconds"] = round(wall, 4)
        out["_spawn_floors"] = {
            "compile": round(self._compile_spawn_floor or 0.0, 5),
            "objdiff": round(self._objdiff_spawn_floor or 0.0, 5),
        }
        # Percentages over the wall clock (the only denominator that sums to 100).
        if wall > 0:
            pct = {}
            for name, b in self.buckets.items():
                pct[name] = round(100.0 * b.seconds / wall, 2)
            pct["python-overhead"] = round(100.0 * residual / wall, 2)
            out["_percent_of_wall"] = pct
        return out

    def dump(self, path: str | None = None) -> str:
        text = json.dumps(self.summary(), indent=2)
        if path:
            with open(path, "w") as fh:
                fh.write(text)
        return text


# ── Process-wide singleton ────────────────────────────────────────────────

_OBJDIFF_BINARY = "objdiff-cli"
_GLOBAL: Profiler | None = None


def get_profiler() -> Profiler:
    """Return the process-wide profiler (created on first call)."""
    global _GLOBAL
    if _GLOBAL is None:
        _GLOBAL = Profiler(enabled=profiling_enabled())
    return _GLOBAL


def reset_profiler() -> Profiler:
    """Drop and recreate the singleton — used between bench functions so each
    function's breakdown is isolated."""
    global _GLOBAL
    _GLOBAL = Profiler(enabled=profiling_enabled())
    return _GLOBAL


# ── Spawn-floor calibration probes ─────────────────────────────────────────

_CALIBRATION_ROUNDS = 5


def _measure_shell_spawn_floor() -> float:
    """Median wall time of ``sh -c true`` — the irreducible cost of launching
    a shell, which is exactly what the compile path pays before mwcceppc/cl
    even start (the compile command runs via ``shell=True``)."""
    times = []
    for _ in range(_CALIBRATION_ROUNDS):
        start = time.perf_counter()
        subprocess.run("true", shell=True, capture_output=True)
        times.append(time.perf_counter() - start)
    times.sort()
    return times[len(times) // 2]


def _measure_objdiff_spawn_floor() -> float:
    """Median wall time of ``objdiff-cli --version`` — process launch + binary
    load + arg parse, with no diff work. This is the *measured* replacement for
    the roadmap's unverified '~80 ms objdiff spawn' assumption."""
    times = []
    for _ in range(_CALIBRATION_ROUNDS):
        start = time.perf_counter()
        try:
            subprocess.run([_OBJDIFF_BINARY, "--version"], capture_output=True)
        except OSError:
            return 0.0
        times.append(time.perf_counter() - start)
    times.sort()
    return times[len(times) // 2]
