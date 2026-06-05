#!/bin/bash
# Backup the decomp databases with xz compression, to an out-of-repo archive.
#
# TWO databases matter and they are NOT equivalent:
#   - decomp.db          enrichment/flags/Ghidra cache + orchestrator locks.
#                        RE-DERIVABLE (rebuild + scripts/sync_objdiff.py).
#   - permuter_cache.db  pattern_runs / climb_history / climb_variant — the
#                        permuter's accumulated run history (the data we mine).
#                        NOT re-derivable; if lost it is gone for good.
# Both live at the repo root, are gitignored, and are written live (WAL mode),
# so this archive is their only durable copy. Default archive dir is OUTSIDE the
# repo so a reclone / worktree teardown can't take the backups with it.
#
# Usage:
#   scripts/backup-db.sh                          # back up BOTH default DBs -> ~/code/db-backups/
#   scripts/backup-db.sh <db_path> [archive_dir]  # back up a single DB
#   DB_BACKUP_DIR=/some/dir scripts/backup-db.sh   # override the archive dir

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEFAULT_ARCHIVE="${DB_BACKUP_DIR:-$HOME/code/db-backups}"

backup_one() {
    local db_path="$1"
    local archive_dir="$2"
    local date base backup_file tmp

    if [[ ! -f "$db_path" ]]; then
        echo "Error: database not found: $db_path" >&2
        return 1
    fi

    date="$(date +%Y-%m-%d)"
    base="$(basename "$db_path")"                 # name the archive by the DB's
    backup_file="$archive_dir/$base.$date.xz"     # OWN name (not hardcoded)

    mkdir -p "$archive_dir"
    if [[ -f "$backup_file" ]]; then
        echo "Overwriting today's existing backup: $backup_file"
    fi

    # Take a CONSISTENT snapshot via sqlite3 .backup first. A raw `xz < live.db`
    # of a WAL-mode DB that a concurrent permuter is writing can capture a torn
    # or stale image (recent commits sit in the -wal file); .backup uses the
    # online-backup API + a busy timeout to produce one coherent .db file.
    tmp="$(mktemp "${TMPDIR:-/tmp}/${base}.XXXXXX")"
    echo "Snapshotting  $db_path"
    sqlite3 "$db_path" ".timeout 60000" ".backup '$tmp'"
    echo "Compressing -> $backup_file"
    xz -T0 -c "$tmp" > "$backup_file"
    rm -f "$tmp"
    echo "Done: $base -> $(ls -lh "$backup_file" | awk '{print $5}')"
}

if [[ $# -ge 1 ]]; then
    # Single-DB mode (back-compat with the old positional interface).
    backup_one "$1" "${2:-$DEFAULT_ARCHIVE}"
else
    # Default: snapshot every DB that matters, to the out-of-repo archive.
    for db in decomp.db permuter_cache.db; do
        backup_one "$REPO_ROOT/$db" "$DEFAULT_ARCHIVE"
    done
fi
