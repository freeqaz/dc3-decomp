"""Atomic file writes and per-file locking for the permuter.

Prevents three failure modes:
1. Blank/truncated files from interrupted writes (SIGKILL, OOM)
2. Concurrent permuter processes clobbering each other's source files
3. Variant write-backs that DELETE preprocessor conditionals — in particular
   the native-port ``#ifdef HX_NATIVE`` / ``#else`` / ``#endif`` forks that are
   interleaved with the matched (Wii/PPC) source. The permuter only ever
   optimizes the matched (``#else``) branch; it must never drop the native
   fork. See ``_assert_directives_preserved`` below.
"""

from __future__ import annotations

import fcntl
import os
import re
import sys
import tempfile
from pathlib import Path

# Minimum source size to accept — catches degenerate empty variants
MIN_SOURCE_BYTES = 10

# Preprocessor conditional directives whose count must never DROP across a
# write. Plain ``#define`` / ``#include`` are intentionally excluded — only the
# conditional skeleton (which is what carries an #else native fork) is guarded.
_PREPROC_DIRECTIVE_RE = re.compile(
    rb"^[ \t]*#[ \t]*(?:if|ifdef|ifndef|elif|else|endif)\b",
    re.MULTILINE,
)
# Native-port fork token. Losing one of these is the canonical corruption this
# guard exists to stop, so it is counted independently of the directive total
# (an edit could keep directive count level while swapping HX_NATIVE for a
# different macro).
_HX_NATIVE_RE = re.compile(rb"HX_NATIVE")


class DirectivePreservationError(RuntimeError):
    """Raised when a write would delete preprocessor conditionals / HX_NATIVE.

    Signals that a permuter variant tried to clobber an ``#ifdef`` skeleton
    (most importantly a ``#ifdef HX_NATIVE`` native-port fork). The write is
    refused so the native branch is never silently wiped.
    """


def _is_ephemeral_path(path: Path) -> bool:
    """True for permuter scratch files (working copies, probe/splice temps).

    The directive guard only protects REAL tracked source files — the place a
    winning variant actually lands. Throwaway compile inputs (``.permuter_work_*``,
    ``.permuter_pp_*`` and same-basename files inside ``permuter_*`` temp dirs)
    are validated by the compiler/objdiff and discarded, so guarding them would
    add per-compile overhead and risk false-positive crashes mid-sweep.
    """
    name = path.name
    if name.startswith(".permuter_") or name.endswith(".tmp"):
        return True
    # Same-basename work files live inside a private temp dir whose own name is
    # permuter-prefixed (see Scorer._samename_work_dir / score_batch tmp_dir).
    for part in path.parts:
        if part.startswith("permuter_") or part.startswith(".permuter_"):
            return True
    return False


def _count_directives(data: bytes) -> tuple[int, int]:
    """Return ``(preproc_conditional_count, hx_native_token_count)`` for *data*."""
    return (
        len(_PREPROC_DIRECTIVE_RE.findall(data)),
        len(_HX_NATIVE_RE.findall(data)),
    )


def directive_guard_enabled() -> bool:
    """Whether the directive-preservation guard is active (default ON).

    Set ``PERMUTER_ALLOW_DIRECTIVE_DROP=1`` to disable — only needed for the
    rare deliberate edit that removes an ``#ifdef`` (e.g. a human collapsing a
    fork). The permuter's own variant generators never legitimately drop one.
    """
    return os.environ.get(
        "PERMUTER_ALLOW_DIRECTIVE_DROP", "0"
    ).strip().lower() not in ("1", "true", "yes", "on")


def _assert_directives_preserved(path: Path, new_data: bytes) -> None:
    """Refuse *new_data* if it drops conditionals / HX_NATIVE vs the file on disk.

    Compares against the file's CURRENT on-disk content — which always holds the
    prior good state — so this catches a directive-dropping write no matter which
    apply path (hill_climber, beam_search, evolutionary, __main__, ...) produced
    it. Restores (writing the original back) and banner add/strip preserve the
    count and pass cleanly. A missing target file (first create) is allowed.
    """
    if not directive_guard_enabled():
        return
    if _is_ephemeral_path(path):
        return  # throwaway compile input — not a real-source apply
    try:
        old_data = path.read_bytes()
    except FileNotFoundError:
        return  # creating a new file — nothing to preserve

    old_directives, old_hx = _count_directives(old_data)
    if old_directives == 0 and old_hx == 0:
        return  # file has no guarded directives — fast path

    new_directives, new_hx = _count_directives(new_data)
    if new_directives >= old_directives and new_hx >= old_hx:
        return  # nothing dropped

    msg = (
        f"REFUSING WRITE to {path}: it would DELETE preprocessor conditionals.\n"
        f"  #if/#ifdef/#ifndef/#elif/#else/#endif: {old_directives} -> {new_directives}\n"
        f"  HX_NATIVE tokens:                       {old_hx} -> {new_hx}\n"
        f"This almost always means a permuter variant clobbered a native-port "
        f"(#ifdef HX_NATIVE / #else) fork. The permuter must only rewrite the "
        f"matched (#else) branch, never drop the native code. Write blocked.\n"
        f"(Set PERMUTER_ALLOW_DIRECTIVE_DROP=1 only if this removal is intentional.)"
    )
    print(f"\n*** {msg}", file=sys.stderr)
    raise DirectivePreservationError(msg)


def atomic_write_bytes(path: Path, data: bytes) -> None:
    """Write bytes to a file atomically via temp+rename.

    Writes to a temp file in the same directory, then atomically replaces
    the target. If interrupted between steps, the temp file is orphaned
    but the target file remains intact.

    Refuses any write that would reduce the file's preprocessor-conditional or
    ``HX_NATIVE`` count vs. the current on-disk content (see
    ``_assert_directives_preserved``) — this is the universal safety net that
    stops a permuter variant from wiping a native-port ``#ifdef HX_NATIVE`` fork.
    """
    if len(data) < MIN_SOURCE_BYTES:
        raise ValueError(
            f"Refusing to write near-empty source to {path} ({len(data)} bytes)"
        )
    _assert_directives_preserved(path, data)
    fd, tmp = tempfile.mkstemp(dir=path.parent, suffix=".tmp")
    try:
        os.write(fd, data)
        os.fsync(fd)
        os.close(fd)
        os.replace(tmp, path)
    except BaseException:
        try:
            os.close(fd)
        except OSError:
            pass
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise


def restore_file_bytes(path: Path, original: bytes | None) -> None:
    """Restore a file to its original bytes, or remove it if it was created."""
    if original is None:
        path.unlink(missing_ok=True)
        return
    atomic_write_bytes(path, original)


def apply_file_updates(
    updates: dict[Path, bytes],
    originals: dict[Path, bytes | None],
    current_paths: set[Path] | None = None,
) -> set[Path]:
    """Apply an exact file set, restoring previously touched files not in updates."""
    current = set(current_paths or ())
    desired = {path.resolve() for path in updates}
    normalized_updates = {path.resolve(): data for path, data in updates.items()}

    for path in current - desired:
        restore_file_bytes(path, originals[path])

    for path, data in normalized_updates.items():
        if path not in originals:
            originals[path] = path.read_bytes() if path.exists() else None
        atomic_write_bytes(path, data)

    return desired


def restore_tracked_files(originals: dict[Path, bytes | None]) -> None:
    """Restore all tracked files to their original bytes."""
    for path, original in sorted(originals.items(), key=lambda item: str(item[0])):
        restore_file_bytes(path, original)


class SourceFileLock:
    """Per-file exclusive lock to prevent concurrent permuter access.

    Uses fcntl.flock (POSIX advisory locking) — non-blocking so we fail
    fast if another process holds the lock.

    Usage:
        with SourceFileLock(source_path):
            # safe to read/write source_path
    """

    def __init__(self, source_path: Path):
        self.lock_path = source_path.with_suffix(source_path.suffix + ".permuter.lock")
        self._fd = None

    def __enter__(self):
        self._fd = open(self.lock_path, "w")
        try:
            fcntl.flock(self._fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            self._fd.close()
            self._fd = None
            raise RuntimeError(
                f"Source file locked by another permuter: {self.lock_path}"
            )
        return self

    def __exit__(self, *args):
        if self._fd:
            fcntl.flock(self._fd, fcntl.LOCK_UN)
            self._fd.close()
            self._fd = None
            try:
                self.lock_path.unlink()
            except OSError:
                pass
        return False
