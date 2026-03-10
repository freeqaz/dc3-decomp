"""Atomic file writes and per-file locking for the permuter.

Prevents two failure modes:
1. Blank/truncated files from interrupted writes (SIGKILL, OOM)
2. Concurrent permuter processes clobbering each other's source files
"""

from __future__ import annotations

import fcntl
import os
import tempfile
from pathlib import Path

# Minimum source size to accept — catches degenerate empty variants
MIN_SOURCE_BYTES = 10


def atomic_write_bytes(path: Path, data: bytes) -> None:
    """Write bytes to a file atomically via temp+rename.

    Writes to a temp file in the same directory, then atomically replaces
    the target. If interrupted between steps, the temp file is orphaned
    but the target file remains intact.
    """
    if len(data) < MIN_SOURCE_BYTES:
        raise ValueError(
            f"Refusing to write near-empty source to {path} ({len(data)} bytes)"
        )
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
