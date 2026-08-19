#!/usr/bin/env python3
"""Safely apply triaged patches one at a time with build + objdiff verification.

Reads from scratch/patches/manifest.json, applies each patch, builds the
affected unit, and verifies with objdiff. Reverts on regression.

Usage:
    python scripts/patches/apply_safe.py                          # Apply all ready/ patches
    python scripts/patches/apply_safe.py --category needs-merge   # Apply needs-merge patches
    python scripts/patches/apply_safe.py --unit src/system/char/CharBones.cpp  # One file only
    python scripts/patches/apply_safe.py --dry-run                # Report only
    python scripts/patches/apply_safe.py --limit 10               # Stop after 10
    python scripts/patches/apply_safe.py --min-delta 5            # Only 5%+ improvement
"""

import argparse
import json
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
SCRATCH_DIR = REPO_ROOT / "scratch" / "patches"
MANIFEST_PATH = SCRATCH_DIR / "manifest.json"
OBJDIFF_CLI = REPO_ROOT / "bin" / "objdiff-cli"


def load_manifest() -> list[dict]:
    if not MANIFEST_PATH.exists():
        print(f"Error: {MANIFEST_PATH} not found. Run patch_triage.py first.")
        sys.exit(1)
    return json.loads(MANIFEST_PATH.read_text())


def save_manifest(manifest: list[dict]):
    MANIFEST_PATH.write_text(json.dumps(manifest, indent=2))


def get_patch_path(entry: dict) -> Path:
    return SCRATCH_DIR / entry["category"] / entry["filename"]


def apply_patch(patch_path: Path, threeway: bool = False) -> tuple[bool, str]:
    """Apply a patch file. Returns (success, message)."""
    cmd = ['git', 'apply']
    if threeway:
        cmd.append('--3way')
    cmd.append(str(patch_path))

    result = subprocess.run(cmd, cwd=REPO_ROOT, capture_output=True, text=True)
    if result.returncode == 0:
        return True, "Applied cleanly"
    return False, result.stderr.strip()


def build_unit(unit_path: str) -> tuple[bool, str]:
    """Build a single object file. Returns (success, message)."""
    # Convert unit path to build object path
    # e.g. src/system/char/Char.cpp -> build/373307D9/src/system/char/Char.cpp.obj
    # But unit might be in default/ format: default/system/char/Char
    cpp_path = unit_path
    if cpp_path.startswith("default/"):
        cpp_path = "src/" + cpp_path[len("default/"):] + ".cpp"
    if not cpp_path.endswith(('.cpp', '.c')):
        cpp_path += ".cpp"
    if not cpp_path.startswith("src/"):
        cpp_path = "src/" + cpp_path

    # Strip .cpp/.c extension before appending .obj
    # ninja expects e.g. build/373307D9/src/system/utl/MemMgr.obj (not .cpp.obj)
    stem = cpp_path
    for ext in ('.cpp', '.c'):
        if stem.endswith(ext):
            stem = stem[:-len(ext)]
            break
    obj_path = f"build/373307D9/{stem}.obj"

    result = subprocess.run(
        ['ninja', obj_path],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        timeout=120,
    )
    if result.returncode == 0:
        return True, "Build OK"
    return False, result.stderr.strip()[:500]


def check_match(symbol: str) -> dict:
    """Run objdiff to check match percentage.

    Returns a dict with:
        success: bool
        percent: float (equal_percent from instruction_summary)
        verdict_class: str (e.g. "COMPLETE", "AT_LIMIT", "LIKELY_FIXABLE")
        diff_op: int (opcode mismatches - 0 means address-only diffs)
        replace: int (replaced instructions)
        diff_arg: int (argument-only diffs, typically address relocations)
        instruction_summary: dict (full summary from objdiff)
        error: str (if success is False)
    """
    empty = {"success": False, "percent": 0.0, "verdict_class": "", "diff_op": 0,
             "replace": 0, "diff_arg": 0, "instruction_summary": {}, "error": ""}

    if not OBJDIFF_CLI.exists():
        return {**empty, "error": f"objdiff-cli not found at {OBJDIFF_CLI}"}

    result = subprocess.run(
        [str(OBJDIFF_CLI), 'diff', '-p', '.', symbol, '--build', '--verdict', '-f', 'json'],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        timeout=120,
    )
    if result.returncode != 0:
        return {**empty, "error": f"objdiff failed: {result.stderr.strip()[:200]}"}

    try:
        # objdiff --build may prefix stdout with ninja output; extract JSON
        stdout = result.stdout
        json_start = stdout.find('{')
        if json_start > 0:
            stdout = stdout[json_start:]
        data = json.loads(stdout)

        summary = data.get("instruction_summary", {})
        verdict_obj = data.get("verdict", {})

        return {
            "success": True,
            "percent": summary.get("equal_percent", 0.0),
            "verdict_class": verdict_obj.get("classification", "") if isinstance(verdict_obj, dict) else str(verdict_obj),
            "diff_op": summary.get("diff_op", 0),
            "replace": summary.get("replace", 0),
            "diff_arg": summary.get("diff_arg", 0),
            "instruction_summary": summary,
            "error": "",
        }
    except (json.JSONDecodeError, KeyError) as e:
        return {**empty, "error": f"Failed to parse objdiff output: {e}"}


def revert_files(target_files: list[str]):
    """Revert changed files."""
    if not target_files:
        return
    subprocess.run(
        ['git', 'checkout', '--'] + target_files,
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
    )


def apply_single(entry: dict, dry_run: bool = False) -> dict:
    """Apply and verify a single patch. Returns updated entry."""
    patch_path = get_patch_path(entry)
    symbol = entry.get("symbol", "")
    unit = entry.get("unit", "")
    target_files = entry.get("target_files", [])
    category = entry.get("category", "")
    expected_pct = entry.get("patch_percent", 0)

    name = entry.get("demangled") or symbol or entry["filename"]
    delta_str = f"+{entry['delta']:.1f}%" if entry.get("delta", 0) > 0 else f"{entry.get('delta', 0):.1f}%"

    if not patch_path.exists():
        print(f"  SKIP {name}: patch file missing")
        entry["status"] = "skipped"
        entry["note"] = "patch file missing"
        return entry

    if dry_run:
        print(f"  DRY  {delta_str:>7}  {name}")
        return entry

    # Step 1: Apply patch
    threeway = category == "needs-merge"
    ok, msg = apply_patch(patch_path, threeway=threeway)
    if not ok:
        print(f"  FAIL {name}: {msg[:80]}")
        entry["status"] = "regressed"
        entry["note"] = f"apply failed: {msg[:200]}"
        return entry

    # Step 2: Build (skip for header-only units that have no .obj)
    if unit and not unit.endswith(('.h', '.hpp')):
        ok, msg = build_unit(unit)
        if not ok:
            print(f"  FAIL {name}: build failed")
            revert_files(target_files)
            entry["status"] = "regressed"
            entry["note"] = f"build failed: {msg[:200]}"
            return entry

    # Step 3: Verify with objdiff (only if we have a symbol)
    if symbol:
        match_info = check_match(symbol)
        if match_info["success"]:
            actual_pct = match_info["percent"]
            verdict_class = match_info["verdict_class"]
            current_pct = entry.get("current_percent", 0.0)

            # Accept if ANY of these conditions hold:
            # 1. Meets expected percentage (with rounding tolerance)
            meets_expected = actual_pct >= expected_pct - 0.5
            # 2. Improved over current AND verdict says it's done (COMPLETE/AT_LIMIT)
            improved_and_done = (actual_pct > current_pct and
                                verdict_class in ("COMPLETE", "AT_LIMIT"))
            # 3. Improved over current AND only address-only diffs remain
            improved_addr_only = (actual_pct > current_pct and
                                 match_info["diff_op"] == 0 and
                                 match_info["replace"] == 0)

            if meets_expected or improved_and_done or improved_addr_only:
                reason = ""
                if not meets_expected:
                    if improved_and_done:
                        reason = f" [{verdict_class}]"
                    elif improved_addr_only:
                        reason = " [addr-only diffs]"
                print(f"  OK   {delta_str:>7}  {name}  ({actual_pct:.1f}%{reason})")
                entry["status"] = "applied"
                entry["note"] = f"verified at {actual_pct:.1f}% (verdict={verdict_class})"
            else:
                print(f"  REG  {name}: expected {expected_pct:.1f}%, got {actual_pct:.1f}% (verdict={verdict_class}, diff_op={match_info['diff_op']})")
                revert_files(target_files)
                entry["status"] = "regressed"
                entry["note"] = (f"regressed: expected {expected_pct:.1f}%, got {actual_pct:.1f}% "
                                 f"(verdict={verdict_class}, diff_op={match_info['diff_op']})")
        else:
            # objdiff failed but patch applied and built - keep it but note
            err = match_info["error"]
            print(f"  OK?  {delta_str:>7}  {name}  (objdiff check failed: {err[:60]})")
            entry["status"] = "applied"
            entry["note"] = f"applied but objdiff check failed: {err[:100]}"
    else:
        # No symbol to verify - trust the patch
        print(f"  OK   {delta_str:>7}  {name}  (no symbol to verify)")
        entry["status"] = "applied"
        entry["note"] = "applied without objdiff verification"

    return entry


def main():
    parser = argparse.ArgumentParser(description="Safely apply triaged patches")
    parser.add_argument("--category", default="ready",
                        help="Category to apply from (default: ready)")
    parser.add_argument("--unit", help="Only apply patches for this unit")
    parser.add_argument("--dry-run", action="store_true",
                        help="Report what would be applied without doing it")
    parser.add_argument("--limit", type=int, default=0,
                        help="Stop after N patches (0 = no limit)")
    parser.add_argument("--min-delta", type=float, default=0,
                        help="Only apply patches with at least this improvement")
    parser.add_argument("--include-applied", action="store_true",
                        help="Re-apply patches already marked as applied")
    args = parser.parse_args()

    manifest = load_manifest()

    # Filter entries
    candidates = []
    for entry in manifest:
        if entry["category"] != args.category:
            continue
        if entry.get("status") in ("applied", "skipped", "regressed") and not args.include_applied:
            continue
        if args.unit and entry.get("unit", "") != args.unit:
            # Also check target_files
            if args.unit not in entry.get("target_files", []):
                continue
        if args.min_delta > 0 and entry.get("delta", 0) < args.min_delta:
            continue
        candidates.append(entry)

    # Sort by delta descending
    candidates.sort(key=lambda e: e.get("delta", 0), reverse=True)

    n_before = len(candidates)
    if args.limit > 0 and n_before > args.limit:
        candidates = candidates[:args.limit]
        # Say so. A patch run that applies 10 of 300 and prints only "10" reads
        # identically to one where 10 was the whole queue.
        print(f"!! TRUNCATED by --limit={args.limit}: {n_before - args.limit} of "
              f"{n_before} eligible patches were NOT applied", file=sys.stderr)

    if not candidates:
        print(f"No patches to apply (category={args.category}, "
              f"min_delta={args.min_delta}, unit={args.unit or 'any'})")
        return

    mode = "DRY RUN" if args.dry_run else "APPLYING"
    print(f"=== {mode}: {len(candidates)} patches from {args.category}/ ===\n")

    applied = 0
    failed = 0

    for entry in candidates:
        result = apply_single(entry, dry_run=args.dry_run)

        # Update manifest entry in-place
        idx = next(i for i, e in enumerate(manifest)
                   if e["filename"] == entry["filename"]
                   and e["category"] == entry["category"])
        manifest[idx] = result

        if not args.dry_run:
            # Save after every patch for crash safety
            save_manifest(manifest)

            if result.get("status") == "applied":
                applied += 1
            elif result.get("status") == "regressed":
                failed += 1

    if not args.dry_run:
        print(f"\n=== Results: {applied} applied, {failed} failed/regressed ===")
        print(f"Changes are in working tree (not committed). Review with git diff.")


if __name__ == "__main__":
    main()
