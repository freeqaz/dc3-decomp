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
