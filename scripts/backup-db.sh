#!/bin/bash
# Backup decomp.db with xz compression

set -e

DB_PATH="${1:-decomp.db}"
ARCHIVE_DIR="${2:-archive}"
DATE=$(date +%Y-%m-%d)
BACKUP_FILE="$ARCHIVE_DIR/decomp.db.$DATE.xz"

if [[ ! -f "$DB_PATH" ]]; then
    echo "Error: Database not found: $DB_PATH" >&2
    exit 1
fi

mkdir -p "$ARCHIVE_DIR"

if [[ -f "$BACKUP_FILE" ]]; then
    echo "Backup already exists: $BACKUP_FILE"
    echo "Overwrite? [y/N] "
    read -r response
    if [[ ! "$response" =~ ^[Yy]$ ]]; then
        exit 0
    fi
fi

echo "Backing up $DB_PATH -> $BACKUP_FILE"
xz -c "$DB_PATH" > "$BACKUP_FILE"
echo "Done: $(ls -lh "$BACKUP_FILE" | awk '{print $5}')"
