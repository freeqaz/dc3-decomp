#!/usr/bin/env python3
"""Triage and categorize patches from generated-patches/ and decomp.db.

Reads both sources, deduplicates by function (keeping best match%),
and categorizes each patch by applicability. Produces a scratch workspace
with a manifest for safe integration.

Usage:
    python scripts/patch_triage.py                    # Full triage run
    python scripts/patch_triage.py --stats            # Just print category counts
    python scripts/patch_triage.py --refresh          # Re-triage (preserves status)
"""

import argparse
import json
import os
import re
import shutil
import sqlite3
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Optional

# Add parent dir so we can import from orchestrator
sys.path.insert(0, str(Path(__file__).parent))
from orchestrator.patch_applier import clean_patch

REPO_ROOT = Path(__file__).resolve().parent.parent
PATCHES_DIR = REPO_ROOT / "generated-patches"
DB_PATH = REPO_ROOT / "decomp.db"
SCRATCH_DIR = REPO_ROOT / "scratch" / "patches"
MANIFEST_PATH = SCRATCH_DIR / "manifest.json"

CATEGORIES = ["ready", "needs-merge", "needs-file", "stale", "broken"]


@dataclass
class PatchInfo:
    """Metadata about a single patch."""
    filename: str
    category: str = ""
    symbol: str = ""
    demangled: str = ""
    unit: str = ""
    patch_percent: float = 0.0
    current_percent: float = 0.0
    delta: float = 0.0
    target_files: list = field(default_factory=list)
    source: str = ""  # "generated-patches" or "decomp.db"
    patch_bytes: int = 0
    timestamp: str = ""
    status: str = "triaged"  # triaged | applied | regressed | skipped | refreshed
    note: str = ""


def parse_patch_filename(filename: str) -> dict:
    """Extract metadata from a generated-patches filename.

    Format: {symbol_sanitized}_{percent}pct_{timestamp}.patch
    or:     {timestamp}_{symbol_sanitized}_{percent}pct.patch
    """
    stem = Path(filename).stem

    # Extract percentage - look for NNpct pattern
    pct_match = re.search(r'_(\d+(?:\.\d+)?)pct', stem)
    percent = float(pct_match.group(1)) if pct_match else 0.0

    # Extract timestamp - 8+6 digit pattern
    ts_match = re.search(r'(\d{8}_\d{6})', stem)
    timestamp = ts_match.group(1) if ts_match else ""

    # Symbol is everything else (rough - we'll refine via DB lookup)
    symbol_part = stem
    if pct_match:
        symbol_part = symbol_part[:pct_match.start()]
    if ts_match:
        symbol_part = symbol_part.replace(ts_match.group(0), "").strip("_")

    return {
        "percent": percent,
        "timestamp": timestamp,
        "symbol_hint": symbol_part,
    }


def extract_target_files(patch_content: str) -> list[str]:
    """Extract target file paths from patch diff headers."""
    files = []
    for line in patch_content.split('\n'):
        if line.startswith('diff --git a/'):
            # "diff --git a/src/foo.cpp b/src/foo.cpp"
            parts = line.split(' b/')
            if len(parts) >= 2:
                path = parts[-1].strip()
                if path not in files:
                    files.append(path)
    return files


def extract_unit_from_targets(target_files: list[str]) -> str:
    """Derive the unit path from target files (first .cpp file)."""
    for f in target_files:
        if f.endswith('.cpp'):
            return f
    return target_files[0] if target_files else ""


def load_db_patches() -> list[tuple[dict, str]]:
    """Load best-attempt patches from decomp.db where improvement exists.

    Returns list of (metadata_dict, patch_content) tuples.
    """
    if not DB_PATH.exists():
        print(f"  Warning: {DB_PATH} not found, skipping DB patches")
        return []

    conn = sqlite3.connect(str(DB_PATH))
    conn.execute("PRAGMA journal_mode = WAL")
    conn.row_factory = sqlite3.Row
    try:
        rows = conn.execute("""
            SELECT f.symbol, f.demangled, f.unit, f.current_percent,
                   a.patch, a.end_percent, a.finished_at
            FROM functions f
            JOIN attempts a ON a.function_id = f.id
            WHERE a.end_percent > f.current_percent
              AND a.patch IS NOT NULL AND length(a.patch) > 200
              AND a.end_percent = (
                SELECT MAX(a2.end_percent) FROM attempts a2
                WHERE a2.function_id = f.id
                  AND a2.patch IS NOT NULL AND length(a2.patch) > 200
              )
        """).fetchall()
    finally:
        conn.close()

    results = []
    for row in rows:
        meta = {
            "symbol": row["symbol"],
            "demangled": row["demangled"] or "",
            "unit": row["unit"] or "",
            "current_percent": row["current_percent"] or 0.0,
            "patch_percent": row["end_percent"],
            "timestamp": row["finished_at"] or "",
            "source": "decomp.db",
        }
        results.append((meta, row["patch"]))
    return results


def load_file_patches() -> list[tuple[dict, str]]:
    """Load patches from generated-patches/ directory.

    Returns list of (metadata_dict, patch_content) tuples.
    """
    if not PATCHES_DIR.exists():
        print(f"  Warning: {PATCHES_DIR} not found")
        return []

    results = []
    for path in sorted(PATCHES_DIR.glob("*.patch")):
        content = path.read_text(errors="replace")
        parsed = parse_patch_filename(path.name)

        meta = {
            "symbol": "",  # Will be resolved later if possible
            "demangled": "",
            "unit": "",
            "current_percent": 0.0,
            "patch_percent": parsed["percent"],
            "timestamp": parsed["timestamp"],
            "source": "generated-patches",
            "original_filename": path.name,
            "symbol_hint": parsed["symbol_hint"],
        }
        results.append((meta, content))
    return results


def resolve_symbols_from_db(patches: list[tuple[dict, str]]) -> None:
    """Try to match file-sourced patches to DB functions for richer metadata."""
    if not DB_PATH.exists():
        return

    conn = sqlite3.connect(str(DB_PATH))
    conn.execute("PRAGMA journal_mode = WAL")
    conn.row_factory = sqlite3.Row
    try:
        all_funcs = conn.execute(
            "SELECT symbol, demangled, unit, current_percent FROM functions"
        ).fetchall()
    finally:
        conn.close()

    # Build lookup by sanitized symbol prefix
    func_lookup = {}
    for f in all_funcs:
        safe = f["symbol"].replace("?", "").replace("@", "_")[:50]
        func_lookup[safe] = f

    for meta, _ in patches:
        if meta["source"] != "generated-patches":
            continue
        hint = meta.get("symbol_hint", "")
        if hint in func_lookup:
            f = func_lookup[hint]
            meta["symbol"] = f["symbol"]
            meta["demangled"] = f["demangled"] or ""
            meta["unit"] = f["unit"] or ""
            meta["current_percent"] = f["current_percent"] or 0.0


def deduplicate(patches: list[tuple[dict, str]]) -> list[tuple[dict, str]]:
    """Keep only the best patch per function.

    Best = highest patch_percent. Ties broken by most recent timestamp.
    For patches without a resolved symbol, use target files as key.
    """
    by_key: dict[str, tuple[dict, str]] = {}

    for meta, content in patches:
        # Determine dedup key
        key = meta.get("symbol") or ""
        if not key:
            # Fall back to target files
            cleaned = clean_patch(content)
            targets = extract_target_files(cleaned)
            key = "|".join(sorted(targets)) if targets else meta.get("original_filename", "")
        if not key:
            continue

        if key in by_key:
            existing_meta = by_key[key][0]
            # Prefer higher percent
            if meta["patch_percent"] > existing_meta["patch_percent"]:
                by_key[key] = (meta, content)
            elif (meta["patch_percent"] == existing_meta["patch_percent"]
                  and meta["timestamp"] > existing_meta["timestamp"]):
                by_key[key] = (meta, content)
        else:
            by_key[key] = (meta, content)

    return list(by_key.values())


def categorize_patch(meta: dict, cleaned_content: str) -> tuple[str, list[str]]:
    """Determine the category for a patch.

    Returns (category, target_files).
    """
    target_files = extract_target_files(cleaned_content)

    if not target_files:
        return "", []  # Empty after cleaning - filter out

    # Check if target files exist
    missing = [f for f in target_files if not (REPO_ROOT / f).exists()]
    if missing:
        return "needs-file", target_files

    # Check if function is already at or above patch percent
    if meta.get("current_percent", 0) >= meta.get("patch_percent", 0) and meta["patch_percent"] > 0:
        return "stale", target_files

    # Try clean apply
    with tempfile.NamedTemporaryFile(mode='w', suffix='.patch', delete=False) as f:
        f.write(cleaned_content)
        patch_file = f.name

    try:
        result = subprocess.run(
            ['git', 'apply', '--check', patch_file],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
        )
        if result.returncode == 0:
            return "ready", target_files

        # Try 3-way merge check
        result = subprocess.run(
            ['git', 'apply', '--3way', '--check', patch_file],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
        )
        if result.returncode == 0:
            return "needs-merge", target_files

        return "broken", target_files
    finally:
        os.unlink(patch_file)


def make_safe_filename(meta: dict) -> str:
    """Generate a safe filename for scratch storage."""
    symbol = meta.get("symbol", "")
    demangled = meta.get("demangled", "")
    percent = meta.get("patch_percent", 0)

    if demangled:
        # Use class::method format
        name = re.sub(r'[^A-Za-z0-9_:]+', '_', demangled)[:60]
    elif symbol:
        name = symbol.replace("?", "").replace("@", "_")[:60]
    else:
        name = meta.get("original_filename", "unknown")[:60]

    return f"{name}_{percent:.0f}pct.patch"


def load_existing_manifest() -> dict[str, dict]:
    """Load existing manifest for status preservation during refresh."""
    if not MANIFEST_PATH.exists():
        return {}
    try:
        data = json.loads(MANIFEST_PATH.read_text())
        # Key by symbol or filename for lookup
        by_key = {}
        for entry in data:
            key = entry.get("symbol") or entry.get("filename", "")
            if key:
                by_key[key] = entry
        return by_key
    except (json.JSONDecodeError, KeyError):
        return {}


def run_triage(refresh: bool = False):
    """Main triage pipeline."""
    print("=== Patch Triage ===\n")

    # Load existing manifest for status preservation
    existing = load_existing_manifest() if refresh else {}
    if refresh and existing:
        print(f"  Refresh mode: preserving status from {len(existing)} existing entries\n")

    # Step 1: Load patches from both sources
    print("Loading patches...")
    file_patches = load_file_patches()
    print(f"  generated-patches/: {len(file_patches)} files")

    db_patches = load_db_patches()
    print(f"  decomp.db: {len(db_patches)} improvement patches")

    # Step 2: Resolve symbols for file-sourced patches
    print("\nResolving symbols...")
    resolve_symbols_from_db(file_patches)
    resolved = sum(1 for m, _ in file_patches if m.get("symbol"))
    print(f"  Matched {resolved}/{len(file_patches)} file patches to DB functions")

    # Step 3: Combine and deduplicate
    all_patches = file_patches + db_patches
    print(f"\nDeduplicating {len(all_patches)} total patches...")
    deduped = deduplicate(all_patches)
    print(f"  {len(deduped)} unique patches after dedup")

    # Step 4: Clean and categorize
    print("\nCategorizing patches...")
    SCRATCH_DIR.mkdir(parents=True, exist_ok=True)
    for cat in CATEGORIES:
        (SCRATCH_DIR / cat).mkdir(exist_ok=True)

    manifest = []
    counts = {cat: 0 for cat in CATEGORIES}
    counts["filtered"] = 0

    for i, (meta, raw_content) in enumerate(deduped):
        if (i + 1) % 100 == 0:
            print(f"  Processing {i+1}/{len(deduped)}...")

        cleaned = clean_patch(raw_content)
        if not cleaned.strip():
            counts["filtered"] += 1
            continue

        category, target_files = categorize_patch(meta, cleaned)
        if not category:
            counts["filtered"] += 1
            continue

        counts[category] += 1

        # Generate filename and write to category dir
        filename = make_safe_filename(meta)
        # Avoid collisions
        dest = SCRATCH_DIR / category / filename
        counter = 1
        while dest.exists():
            stem = Path(filename).stem
            dest = SCRATCH_DIR / category / f"{stem}_{counter}.patch"
            counter += 1

        dest.write_text(cleaned)

        # Preserve status from previous manifest
        status = "triaged"
        lookup_key = meta.get("symbol") or filename
        if lookup_key in existing:
            prev = existing[lookup_key]
            if prev.get("status") in ("applied", "skipped", "refreshed"):
                status = prev["status"]

        unit = meta.get("unit", "") or extract_unit_from_targets(target_files)

        entry = PatchInfo(
            filename=dest.name,
            category=category,
            symbol=meta.get("symbol", ""),
            demangled=meta.get("demangled", ""),
            unit=unit,
            patch_percent=meta.get("patch_percent", 0),
            current_percent=meta.get("current_percent", 0),
            delta=meta.get("patch_percent", 0) - meta.get("current_percent", 0),
            target_files=target_files,
            source=meta.get("source", ""),
            patch_bytes=len(cleaned.encode()),
            timestamp=meta.get("timestamp", ""),
            status=status,
        )
        manifest.append(asdict(entry))

    # Sort manifest by delta descending (biggest improvements first)
    manifest.sort(key=lambda e: e["delta"], reverse=True)

    # Write manifest
    MANIFEST_PATH.write_text(json.dumps(manifest, indent=2))
    print(f"\nManifest written: {MANIFEST_PATH}")

    print_stats(counts, manifest)


def print_stats(counts: Optional[dict] = None, manifest: Optional[list] = None):
    """Print triage statistics."""
    if counts is None:
        # Load from manifest
        if not MANIFEST_PATH.exists():
            print("No manifest found. Run triage first.")
            return
        manifest = json.loads(MANIFEST_PATH.read_text())
        counts = {}
        for entry in manifest:
            cat = entry["category"]
            counts[cat] = counts.get(cat, 0) + 1

    print("\n=== Triage Results ===")
    for cat in CATEGORIES:
        print(f"  {cat:>12}: {counts.get(cat, 0)}")
    if "filtered" in counts:
        print(f"  {'filtered':>12}: {counts['filtered']}")
    print(f"  {'TOTAL':>12}: {sum(counts.values())}")

    if manifest:
        # Top improvements
        ready = [e for e in manifest if e["category"] == "ready" and e["delta"] > 0]
        if ready:
            print(f"\nTop ready patches by improvement:")
            for e in ready[:10]:
                name = e["demangled"] or e["symbol"] or e["filename"]
                if len(name) > 60:
                    name = name[:57] + "..."
                print(f"  +{e['delta']:5.1f}%  {name}")

        # Status summary
        statuses = {}
        for e in manifest:
            s = e.get("status", "triaged")
            statuses[s] = statuses.get(s, 0) + 1
        if any(s != "triaged" for s in statuses):
            print(f"\nStatus summary:")
            for s, c in sorted(statuses.items()):
                print(f"  {s:>12}: {c}")


def main():
    parser = argparse.ArgumentParser(description="Triage decomp patches")
    parser.add_argument("--stats", action="store_true",
                        help="Just print category counts from existing manifest")
    parser.add_argument("--refresh", action="store_true",
                        help="Re-triage, preserving applied/skipped status")
    args = parser.parse_args()

    if args.stats:
        print_stats()
    else:
        run_triage(refresh=args.refresh)


if __name__ == "__main__":
    main()
