#!/usr/bin/env python3
"""
DC3 Decomp Orchestrator - Multi-agent decompilation pipeline.

Commands:
    init        Initialize worktree pool
    single      Run single agent on one function
    batch       Run batch of functions with parallel agents
    query       Query functions matching criteria
    status      Show orchestrator status
    info        Show backend and model information
    retry       Retry a specific function with escalated model
    cleanup     Clean up stale locks and worktrees
    release-locks Immediately clear all locks (quick recovery)
    sync        Sync database with current report.json

Build Strategies:
    --incremental-only   : Force all builds incremental (fast mode, ~15s per function)
    --full-build         : Force all builds full (safe mode, ~88s per function)
    --periodic-full N    : Run full build every Nth batch (default: 10)
    --validate-diffs     : Extra validation between incremental and full builds

Usage:
    python3 scripts/decomp_orchestrate.py init --pool-size 3
    python3 scripts/decomp_orchestrate.py single "?Poll@CharMirror@@UAEXXZ"
    python3 scripts/decomp_orchestrate.py batch "src/system/char/*" --max-agents 3
    python3 scripts/decomp_orchestrate.py query --pattern "src/system/char/*" --max-percent 30
    python3 scripts/decomp_orchestrate.py status

Examples:
    # Fast incremental analysis (2-4s per function with 3 agents)
    ./bin/orchestrate batch "src/system/char/*.cpp" --max-agents 3 --limit 30

    # Multiple unit patterns at once
    ./bin/orchestrate batch "default/system/char/*" "default/system/rndobj/*" --limit 50 -j 3

    # Safe mode with periodic full builds (verify every 10 batches)
    ./bin/orchestrate batch "src/system/char/*.cpp" --periodic-full 10 --limit 50

    # Pure incremental mode (no validation)
    ./bin/orchestrate batch "src/system/char/*.cpp" --incremental-only --limit 100

    # Conservative: force all full builds
    ./bin/orchestrate batch "src/system/char/*.cpp" --full-build --max-agents 2 --limit 20
"""

import argparse
import asyncio
import json
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Any, Optional

# Add scripts and project root to path (needed for tools/ imports)
_scripts_dir = Path(__file__).resolve().parent
_project_root = _scripts_dir.parent
sys.path.insert(0, str(_scripts_dir))
sys.path.insert(0, str(_project_root))

# Load environment variables from .env file (for GHIDRA_INSTALL_DIR etc.)
from dotenv import load_dotenv
load_dotenv(_project_root / ".env")

from orchestrator.database import (
    init_database,
    get_connection,
    query_functions,
    query_functions_by_priority,
    query_functions_for_unit_completion,
    get_priority_stats,
    get_function_by_symbol,
    get_stats,
    unlock_session,
    unlock_function,
    query_file_pairs,
    get_file_pairs_stats,
)
from orchestrator.rb3_pairing import (
    sync_file_pairs,
    DEFAULT_RB3_PATH,
    DEFAULT_DC3_REPORT,
)
from orchestrator.rb2_dwarf import RB2DwarfParser, RB2DwarfDB, DEFAULT_RB2_DUMP
from orchestrator.core import DecompOrchestrator, DEFAULT_POOL_DIR
from orchestrator.model_selection import (
    estimate_batch_cost,
    MODEL_MAPS,
    COST_TABLES,
    get_model_id,
    get_model_cost,
)
from orchestrator.reporting import (
    get_model_effectiveness,
    get_effectiveness_by_range,
    get_gain_distribution,
    format_model_analysis,
)
from orchestrator.config import (
    get_backend,
    get_token_budget,
    get_available_models,
    _get_openrouter_api_key,
    requires_openrouter,
    TOKEN_BUDGETS,
)


def validate_model_backend(model: Optional[str]) -> None:
    """Validate model is usable with current configuration.

    Exits with clear error if an OpenRouter-only model is specified
    but OPENROUTER_API_KEY is not configured.

    Args:
        model: Model tier (may be None if using auto-selection)
    """
    if not model:
        return
    if requires_openrouter(model) and not _get_openrouter_api_key():
        print(f"Error: Model '{model}' requires OpenRouter.")
        print("Set OPENROUTER_API_KEY in .env or environment.")
        sys.exit(1)


def cmd_init(args):
    """Initialize worktree pool."""
    orchestrator = DecompOrchestrator(
        db_path=args.db,
        pool_dir=Path(args.pool_dir),
        pool_size=args.pool_size,
        main_repo=_project_root,
    )
    orchestrator.initialize(force=args.force)
    print(f"\nPool initialized at {args.pool_dir}")
    print(orchestrator.status())


def _verbosity(args) -> int:
    """Convert --quiet / --verbose flags to int: 0=quiet, 1=normal, 2=verbose."""
    if getattr(args, 'quiet', False):
        return 0
    if getattr(args, 'verbose', False):
        return 2
    return 1


def cmd_single(args):
    """Run single agent on one function."""
    validate_model_backend(args.model)
    orchestrator = DecompOrchestrator(
        db_path=args.db,
        pool_dir=Path(args.pool_dir),
        main_repo=_project_root,
        auto_apply=not args.no_auto_apply,
    )

    # Force unlock if requested
    if args.force:
        func = get_function_by_symbol(args.symbol, db_path=args.db)
        if func and func.get("locked_by"):
            unlock_function(func["id"], db_path=args.db)
            if not args.quiet:
                print(f"Force-unlocked function (was locked by {func['locked_by']})")

    # Ensure pool exists
    if orchestrator.worktree_pool.status()["total"] == 0:
        print("Initializing worktree pool...")
        orchestrator.initialize()

    # Determine build strategy
    use_incremental = not args.full_build
    if args.incremental_only:
        use_incremental = True

    if not args.quiet:
        build_strategy = "incremental" if use_incremental else "full"
        print(f"Using {build_strategy} build strategy")

    result = orchestrator.run_single_sync(
        symbol=args.symbol,
        model=args.model,
        verbose=_verbosity(args),
        dry_run=args.dry_run,
        use_incremental=use_incremental,
        refactor=not args.no_refactor,
        custom_prompt=args.prompt,
    )

    if args.json:
        print(json.dumps(result, indent=2, default=str))
    else:
        print(f"\nResult: {result.get('status')}")
        if result.get("end_percent"):
            print(f"Match: {result.get('start_percent', 0)}% → {result['end_percent']}%")
        if result.get("actual_cost_usd") is not None:
            print(f"Actual cost: ${result['actual_cost_usd']:.4f}")


def _print_priority_summary(targets, min_priority, reachable_only):
    """Print a readable summary of priority-selected targets."""
    n = len(targets)
    pcts = [t.get('current_percent') or 0 for t in targets]
    avg_pct = sum(pcts) / n if n else 0
    near_complete = sum(1 for p in pcts if p >= 95)
    high_match = sum(1 for p in pcts if 80 <= p < 95)
    low_match = sum(1 for p in pcts if p < 80)
    pri_min = min((t.get('priority_score') or 0) for t in targets)
    pri_max = max((t.get('priority_score') or 0) for t in targets)

    reach_str = ", reachable-only" if reachable_only else ""
    pri_str = f", min_priority={min_priority}" if min_priority > 0 else ""

    print(f"\nStrategy: PRIORITY{pri_str}{reach_str}")
    print(f"Selected {n} targets ranked by ease x impact x confidence score.")
    print(f"Priority range: {pri_max:.0f} (best) to {pri_min:.0f} (worst), avg match: {avg_pct:.1f}%")
    print(f"Breakdown: {near_complete} near-complete (95%+), {high_match} high (80-95%), {low_match} below 80%")
    print()

    # Table header
    print(f"  {'Score':>5} | {'Match':>6} | {'Function'}")
    print(f"  {'-'*5}-+-{'-'*6}-+-{'-'*50}")
    for t in targets[:8]:
        pct = t.get('current_percent') or 0
        pri = t.get('priority_score') or 0
        name = t.get('demangled') or t['symbol'][:50]
        print(f"  {pri:5.0f} | {pct:5.1f}% | {name[:50]}")
    if n > 8:
        print(f"  {'...':>5}   {'...':>6}   ... and {n - 8} more")
    print()


def _print_unit_completion_summary(targets, reachable_only):
    """Print a readable summary of unit-completion-selected targets."""
    n = len(targets)
    reach_str = ", reachable-only" if reachable_only else ""

    # Group by unit
    by_unit = {}
    for t in targets:
        unit = t.get('unit', 'unknown')
        by_unit.setdefault(unit, []).append(t)

    print(f"\nStrategy: UNIT-COMPLETION{reach_str}")
    print(f"Selected {n} incomplete functions across {len(by_unit)} near-complete units.")
    print(f"Completing these will bring entire compilation units to 100%.")
    print()

    print(f"  {'Remaining':>9} | {'Unit'}")
    print(f"  {'-'*9}-+-{'-'*50}")
    for unit, funcs in list(by_unit.items())[:8]:
        print(f"  {len(funcs):>6} fn | {unit}")
    if len(by_unit) > 8:
        remaining_units = len(by_unit) - 8
        remaining_funcs = sum(len(f) for f in list(by_unit.values())[8:])
        print(f"  {remaining_funcs:>6} fn | ... and {remaining_units} more units")
    print()


def cmd_batch(args):
    """Run batch of functions with parallel agents."""
    validate_model_backend(args.model)

    # Select targets based on strategy
    strategy = getattr(args, 'strategy', 'pattern')
    reachable_only = getattr(args, 'reachable_only', False)
    min_priority = getattr(args, 'min_priority', 0)

    if strategy == "priority":
        # Use Phase 2 scoring infrastructure
        targets = query_functions_by_priority(
            min_priority=min_priority,
            min_percent=args.min_percent,
            max_percent=args.max_percent,
            reachable_only=reachable_only,
            limit=args.limit if args.limit > 0 else 1000,
            db_path=args.db,
        )
        if not targets:
            print("No functions found matching priority criteria.")
            print("Run: python3 docs/meta-strategy/scripts/compute_scores.py")
            return
        if not args.quiet:
            _print_priority_summary(targets, min_priority, reachable_only)

    elif strategy == "unit-completion":
        # Focus on near-complete units
        targets = query_functions_for_unit_completion(
            reachable_only=reachable_only,
            limit=args.limit if args.limit > 0 else 100,
            db_path=args.db,
        )
        if not targets:
            print("No functions found in near-complete units.")
            return
        if not args.quiet:
            _print_unit_completion_summary(targets, reachable_only)

    else:
        # Default pattern-based selection
        targets = None  # Will use orchestrator's built-in pattern matching

    orchestrator = DecompOrchestrator(
        db_path=args.db,
        pool_dir=Path(args.pool_dir),
        pool_size=max(args.max_agents, 3),  # Ensure enough worktrees
        main_repo=_project_root,
        auto_apply=not args.no_auto_apply,
    )

    # Ensure pool exists
    if orchestrator.worktree_pool.status()["total"] < args.max_agents:
        print(f"Initializing worktree pool with {args.max_agents} worktrees...")
        orchestrator.initialize(force=True)

    # Determine build strategy
    use_incremental = True
    if args.full_build:
        use_incremental = False
    if args.incremental_only:
        use_incremental = True

    # Print build strategy
    if not args.quiet:
        if args.incremental_only:
            print("Build strategy: INCREMENTAL ONLY (fast mode, no validation)")
        elif args.full_build:
            print("Build strategy: FULL BUILD (safe mode)")
        else:
            periodic_str = f"every {args.periodic_full} batches" if args.periodic_full else "disabled"
            print(f"Build strategy: INCREMENTAL + periodic full builds ({periodic_str})")
        print()

    # Run batch with either pre-selected targets or pattern-based selection
    if targets is not None:
        # Use pre-selected targets (priority or unit-completion strategy)
        summary = asyncio.run(
            orchestrator.run_batch_with_targets(
                targets=targets,
                max_agents=args.max_agents,
                model=args.model,
                verbose=_verbosity(args),
                use_incremental=use_incremental,
                periodic_full_interval=args.periodic_full if not args.incremental_only else 0,
                validate_diffs=args.validate_diffs,
                refactor=not args.no_refactor,
            )
        )
    else:
        # Use pattern-based selection (default)
        summary = asyncio.run(
            orchestrator.run_batch(
                pattern=args.pattern,
                min_percent=args.min_percent,
                max_percent=args.max_percent,
                max_agents=args.max_agents,
                model=args.model,
                limit=args.limit,
                verbose=_verbosity(args),
                use_incremental=use_incremental,
                periodic_full_interval=args.periodic_full if not args.incremental_only else 0,
                validate_diffs=args.validate_diffs,
                refactor=not args.no_refactor,
                exclude_at_limit=args.exclude_at_limit,
            )
        )

    if args.json:
        print(json.dumps(summary, indent=2, default=str))


def cmd_query(args):
    """Query functions matching criteria."""
    functions = query_functions(
        pattern=args.pattern,
        min_percent=args.min_percent,
        max_percent=args.max_percent,
        limit=args.limit,
        exclude_complete=not args.include_complete,
        exclude_at_limit=getattr(args, 'exclude_at_limit', False),
        db_path=args.db,
    )

    if not functions:
        print("No functions found matching criteria.")
        return

    if args.json:
        print(json.dumps(functions, indent=2, default=str))
        return

    # Pretty print
    print(f"\nFound {len(functions)} functions:\n")

    for func in functions:
        pct = func.get("current_percent")
        pct_str = f"{pct:.1f}%" if pct is not None else "unimplemented"
        attempts = func.get("attempt_count", 0)

        print(f"  {func.get('demangled') or func['symbol']}")
        print(f"    Symbol: {func['symbol']}")
        print(f"    Unit:   {func.get('unit', 'unknown')}")
        print(f"    Match:  {pct_str}  (attempts: {attempts})")
        print()

    # Cost estimate
    if args.estimate_cost:
        costs = estimate_batch_cost(functions, model=args.model)
        print(f"\nEstimated cost for batch:")
        print(f"  Haiku:  {costs['haiku']} functions")
        print(f"  Sonnet: {costs['sonnet']} functions")
        print(f"  Opus:   {costs['opus']} functions")
        print(f"  Total:  ${costs['total']:.2f}")


def cmd_status(args):
    """Show orchestrator status."""
    orchestrator = DecompOrchestrator(db_path=args.db, pool_dir=Path(args.pool_dir), main_repo=_project_root)
    status = orchestrator.status()

    if args.json:
        print(json.dumps(status, indent=2, default=str))
        return

    db = status["database"]
    pool = status["worktree_pool"]

    print(f"\n{'='*60}")
    print("Orchestrator Status")
    print(f"{'='*60}\n")

    print("Database:")
    print(f"  Total functions:   {db['total_functions']}")
    print(f"  With match %:      {db['with_percent']}")
    print(f"  Complete (100%):   {db['complete']}")
    print(f"  At limit:          {db['at_limit']}")
    print(f"  Locked:            {db['locked']}")
    print(f"  Total attempts:    {db['total_attempts']}")
    if db.get("avg_percent"):
        print(f"  Average match:     {db['avg_percent']:.1f}%")

    print(f"\nWorktree Pool:")
    print(f"  Total:      {pool['total']}")
    print(f"  Available:  {pool['available']}")
    print(f"  In use:     {pool['in_use']}")
    print(f"  Dirty:      {pool['dirty']}")

    if pool.get("active_sessions"):
        print(f"\nActive Sessions:")
        for session in pool["active_sessions"]:
            print(f"  - {session['session_id']}: {session['path']}")

    print()


def cmd_info(args):
    """Show orchestrator backend and model information."""
    backend = get_backend()

    if args.json:
        # JSON output
        models_info = []
        for model_name in sorted(TOKEN_BUDGETS[backend].keys()):
            try:
                model_id = get_model_id(model_name)
                thinking_tokens = get_token_budget(model_name)
                cost = get_model_cost(model_name)
                models_info.append({
                    "name": model_name,
                    "model_id": model_id,
                    "thinking_tokens": thinking_tokens,
                    "cost_per_function_usd": cost,
                })
            except KeyError:
                pass  # Skip models not available for this backend

        info = {
            "backend": backend,
            "openrouter_configured": bool(_get_openrouter_api_key()),
            "models": models_info,
        }
        print(json.dumps(info, indent=2))
        return

    # Pretty print
    print(f"\n{'='*70}")
    print("Orchestrator Backend Information")
    print(f"{'='*70}\n")

    print(f"Current Backend: {backend.upper()}")

    if backend == "openrouter":
        # Show masked API key
        api_key = _get_openrouter_api_key()
        if api_key:
            masked_key = api_key[:10] + "..." + api_key[-10:]
            print(f"OpenRouter API Key: {masked_key}")
        else:
            print("OpenRouter API Key: NOT CONFIGURED")

    print(f"\nAvailable Models ({len(TOKEN_BUDGETS[backend])} total):\n")

    # Display models grouped by thinking token budget
    models_by_tokens = {}
    for model_name in sorted(TOKEN_BUDGETS[backend].keys()):
        thinking_tokens = get_token_budget(model_name)
        if thinking_tokens not in models_by_tokens:
            models_by_tokens[thinking_tokens] = []
        models_by_tokens[thinking_tokens].append(model_name)

    # Display in descending order by thinking tokens
    for thinking_tokens in sorted(models_by_tokens.keys(), reverse=True):
        for model_name in sorted(models_by_tokens[thinking_tokens]):
            try:
                model_id = get_model_id(model_name)
                cost = get_model_cost(model_name)

                # Format model display
                model_display = f"{model_name:20s}"
                model_id_display = f"{model_id:35s}"
                thinking_display = f"{thinking_tokens:5d} tokens"
                cost_display = f"${cost:7.2f}/func"

                print(f"  {model_display} → {model_id_display}  ({thinking_display}, {cost_display})")
            except KeyError:
                pass  # Skip models not available

    print()


def cmd_retry(args):
    """Retry a specific function with escalated model."""
    validate_model_backend(args.model)
    func = get_function_by_symbol(args.symbol, db_path=args.db)
    if not func:
        print(f"Function not found: {args.symbol}")
        sys.exit(1)

    orchestrator = DecompOrchestrator(db_path=args.db, pool_dir=Path(args.pool_dir), main_repo=_project_root)

    if orchestrator.worktree_pool.status()["total"] == 0:
        orchestrator.initialize()

    # Force specific model
    model = args.model or "sonnet"  # Default to Sonnet for retry

    print(f"Retrying {func.get('demangled') or args.symbol} with {model}...")

    result = orchestrator.run_single_sync(
        symbol=args.symbol,
        model=model,
        verbose=_verbosity(args),
    )

    print(f"\nResult: {result.get('status')}")


def cmd_cleanup(args):
    """Clean up stale locks and worktrees."""
    orchestrator = DecompOrchestrator(db_path=args.db, pool_dir=Path(args.pool_dir), main_repo=_project_root)

    # Clean stale locks
    unlocked = orchestrator.cleanup_stale_locks(max_age_hours=args.max_age)
    print(f"Unlocked {unlocked} stale function locks")

    if args.reset_pool:
        print("Resetting worktree pool...")
        orchestrator.worktree_pool.cleanup()
        orchestrator.initialize(force=True)


def cmd_release_locks(args):
    """Immediately clear all locks on functions and worktrees."""
    conn = get_connection(args.db)

    # Clear function locks
    cursor = conn.execute(
        "UPDATE functions SET locked_by = NULL, locked_at = NULL WHERE locked_by IS NOT NULL"
    )
    functions_unlocked = cursor.rowcount

    # Clear worktree locks
    cursor = conn.execute(
        "UPDATE worktrees SET status = 'available', session_id = NULL WHERE status != 'available'"
    )
    worktrees_unlocked = cursor.rowcount

    conn.commit()

    print(f"Unlocked {functions_unlocked} functions")
    print(f"Released {worktrees_unlocked} worktrees")

    # Show current status
    available = conn.execute("SELECT COUNT(*) FROM worktrees WHERE status = 'available'").fetchone()[0]
    total_wt = conn.execute("SELECT COUNT(*) FROM worktrees").fetchone()[0]

    print(f"\nCurrent status:")
    print(f"  Worktrees: {available}/{total_wt} available")
    print(f"  Functions: 0 locked")


def cmd_targets(args):
    """Show top priority targets from Phase 2 scoring infrastructure."""
    # Check if priority scoring is populated
    stats = get_priority_stats(db_path=args.db)
    if not stats.get("populated"):
        print("Priority scoring not populated.")
        print("Run: python3 docs/meta-strategy/scripts/compute_scores.py")
        return

    if args.json:
        # JSON output
        if args.strategy == "priority":
            targets = query_functions_by_priority(
                min_priority=args.min_priority,
                min_percent=args.min_percent,
                max_percent=args.max_percent,
                reachable_only=args.reachable_only,
                limit=args.limit,
                db_path=args.db,
            )
        else:  # unit-completion
            targets = query_functions_for_unit_completion(
                reachable_only=args.reachable_only,
                limit=args.limit,
                db_path=args.db,
            )
        print(json.dumps(targets, indent=2, default=str))
        return

    # Pretty print
    print(f"\n{'='*70}")
    print("Priority Targets (Phase 2 Scoring Infrastructure)")
    print(f"{'='*70}\n")

    # Show stats
    print("Scoring Summary:")
    print(f"  Functions with scores: {stats['with_scores']}")
    print(f"  HIGH priority (50+):   {stats['high_priority']}")
    print(f"  MEDIUM (20-49):        {stats['medium_priority']}")
    print(f"  LOW (5-19):            {stats['low_priority']}")
    print(f"\n80%+ Functions:")
    print(f"  Can reach 100%:        {stats['reachable_100_80plus']}")
    print(f"  Unfixable patterns:    {stats['unreachable_80plus']}")
    print(f"    LINKER_MERGED:       {stats['linker_merged_count']}")
    print(f"    BOOL_MASK:           {stats['bool_mask_count']}")
    print()

    # Get targets based on strategy
    if args.strategy == "priority":
        print(f"Strategy: PRIORITY (min={args.min_priority}, reachable_only={args.reachable_only})")
        targets = query_functions_by_priority(
            min_priority=args.min_priority,
            min_percent=args.min_percent,
            max_percent=args.max_percent,
            reachable_only=args.reachable_only,
            limit=args.limit,
            db_path=args.db,
        )
    else:  # unit-completion
        print(f"Strategy: UNIT-COMPLETION (reachable_only={args.reachable_only})")
        targets = query_functions_for_unit_completion(
            reachable_only=args.reachable_only,
            limit=args.limit,
            db_path=args.db,
        )

    if not targets:
        print("\nNo targets found matching criteria.")
        return

    print(f"\nTop {len(targets)} Targets:\n")
    print(f"{'Pri':>6} | {'Match':>7} | {'R'} | {'Pattern':<15} | {'Function'}")
    print("-" * 82)

    for t in targets:
        pri = t.get('priority_score') or 0
        pct = t.get('current_percent') or 0
        reach = "✓" if t.get('reachable_100') else "✗"
        pattern = (t.get('primary_pattern') or 'none')[:15]
        name = (t.get('demangled') or t['symbol'])[:40]

        print(f"{pri:6.1f} | {pct:6.2f}% | {reach} | {pattern:<15} | {name}")

    print()

    # Show command to run batch
    if args.strategy == "priority":
        cmd = f"./bin/orchestrate batch --strategy priority --limit {args.limit}"
        if args.reachable_only:
            cmd += " --reachable-only"
        if args.min_priority > 0:
            cmd += f" --min-priority {args.min_priority}"
    else:
        cmd = f"./bin/orchestrate batch --strategy unit-completion --limit {args.limit}"
        if args.reachable_only:
            cmd += " --reachable-only"

    print(f"Run batch with: {cmd}")
    print()


def cmd_sync(args):
    """Sync database with current report.json."""
    report_path = _project_root / "build" / "373307D9" / "report.json"

    if not report_path.exists():
        print(f"Report not found: {report_path}")
        print("Run 'ninja' first to generate the report.")
        sys.exit(1)

    if args.build:
        print("Building project...")
        result = subprocess.run(["ninja"], cwd=_project_root, capture_output=not args.verbose)
        if result.returncode != 0:
            print("Build failed!")
            sys.exit(1)

    print(f"Syncing database from {report_path}...")

    # Load report
    with open(report_path) as f:
        report = json.load(f)

    # Connect to database
    import sqlite3
    conn = sqlite3.connect(args.db)
    cursor = conn.cursor()

    updated = 0
    inserted = 0

    for unit in report.get("units", []):
        unit_name = unit.get("name", "")
        for func in unit.get("functions", []):
            symbol = func.get("name", "")
            fuzzy = func.get("fuzzy_match_percent")
            demangled = func.get("demangled_name", "")

            if not symbol or fuzzy is None:
                continue

            # Try to update existing
            cursor.execute("""
                UPDATE functions
                SET current_percent = ?, demangled = COALESCE(?, demangled)
                WHERE symbol = ? AND (current_percent IS NULL OR ABS(current_percent - ?) > 0.001)
            """, (fuzzy, demangled or None, symbol, fuzzy))

            if cursor.rowcount > 0:
                updated += 1
            else:
                # Check if it exists at all
                cursor.execute("SELECT 1 FROM functions WHERE symbol = ?", (symbol,))
                if not cursor.fetchone():
                    # Insert new function
                    cursor.execute("""
                        INSERT INTO functions (symbol, demangled, unit, current_percent)
                        VALUES (?, ?, ?, ?)
                    """, (symbol, demangled or None, unit_name, fuzzy))
                    inserted += 1

    conn.commit()
    conn.close()

    print(f"Updated: {updated} functions")
    print(f"Inserted: {inserted} functions")

    if args.json:
        print(json.dumps({"updated": updated, "inserted": inserted}))


def cmd_analyze_models(args):
    """Analyze model effectiveness from attempt history."""
    # Get cost table for $/% gain calculations
    # Merge costs from all backends, but prefer current backend's prices
    backend = get_backend()
    cost_table = {}
    # First add all costs from other backends
    for be, backend_costs in COST_TABLES.items():
        if be != backend:
            cost_table.update(backend_costs)
    # Then override with current backend's prices (preferred)
    cost_table.update(COST_TABLES.get(backend, {}))

    # Query effectiveness data
    effectiveness = get_model_effectiveness(
        db_path=args.db,
        hours=args.hours,
        exclude_unknown=not args.include_unknown,
        model=args.model,
    )

    by_range = get_effectiveness_by_range(
        db_path=args.db,
        hours=args.hours,
        exclude_unknown=not args.include_unknown,
        model=args.model,
    )

    distribution = get_gain_distribution(
        db_path=args.db,
        model=args.model,
        hours=args.hours,
        exclude_unknown=not args.include_unknown,
    )

    if args.json:
        result = {
            "hours": args.hours,
            "model_filter": args.model,
            "include_unknown": args.include_unknown,
            "effectiveness": effectiveness,
            "by_range": by_range,
            "distribution": distribution,
        }
        print(json.dumps(result, indent=2, default=str))
        return

    # Format and print text output
    output = format_model_analysis(
        effectiveness=effectiveness,
        by_range=by_range,
        distribution=distribution,
        hours=args.hours,
        cost_table=cost_table,
    )
    print(output)


def cmd_rb3_sync(args):
    """Build/update RB3 file pairing database."""
    rb3_path = Path(args.rb3_path) if args.rb3_path else DEFAULT_RB3_PATH
    report_path = _project_root / "build" / "373307D9" / "report.json"

    if not rb3_path.exists():
        print(f"Error: RB3 path not found: {rb3_path}")
        sys.exit(1)

    if not report_path.exists():
        print(f"Error: DC3 report not found: {report_path}")
        print("Run 'ninja' first to build the project.")
        sys.exit(1)

    print(f"Syncing file pairs from RB3: {rb3_path}")
    print(f"DC3 report: {report_path}")
    print()

    results = sync_file_pairs(
        rb3_path=rb3_path,
        report_path=report_path,
        db_path=args.db,
        verbose=_verbosity(args),
    )

    if args.json:
        print(json.dumps(results, indent=2))
    else:
        print()
        print(f"{'='*60}")
        print("RB3 Sync Complete")
        print(f"{'='*60}")
        print(f"  Matched:   {results['matched']} units")
        print(f"  Unmatched: {results['unmatched']} units")
        print(f"  Total:     {results['total_units']} units")

        # Show stats
        stats = get_file_pairs_stats(db_path=args.db)
        print()
        print(f"Database Stats:")
        print(f"  Total pairs:        {stats['total_pairs']}")
        print(f"  With RB3 match:     {stats['with_rb3_match']}")
        print(f"  High compat (>80%): {stats['high_compatibility']}")
        if stats.get('avg_compatibility'):
            print(f"  Avg compatibility:  {stats['avg_compatibility']:.1%}")


def cmd_rb3_query(args):
    """Query RB3 file pairings."""
    pairs = query_file_pairs(
        min_compat=args.min_compat,
        pattern=args.pattern,
        limit=args.limit,
        db_path=args.db,
    )

    if not pairs:
        print("No file pairs found matching criteria.")
        return

    if args.json:
        print(json.dumps(pairs, indent=2, default=str))
        return

    print(f"\nFound {len(pairs)} file pairs:\n")

    for pair in pairs:
        compat = pair.get('compatibility_score')
        compat_str = f"{compat:.1%}" if compat is not None else "unknown"
        overlap = pair.get('function_overlap', 0)
        dc3_count = pair.get('dc3_function_count', 0)

        print(f"  {pair['dc3_unit']}")
        if pair.get('rb3_file'):
            print(f"    RB3:    {pair['rb3_file']}")
        print(f"    Compat: {compat_str} ({overlap}/{dc3_count} functions)")
        print()


def cmd_rb3_merge(args):
    """Run RB3-assisted decomp on file(s) with concurrent agents."""
    validate_model_backend(args.model)

    # Get file pairs to process
    if args.unit:
        # Single unit mode
        from orchestrator.database import get_file_pair
        pair = get_file_pair(args.unit, db_path=args.db)
        if not pair:
            # Try with prefix
            for prefix in ["default/system/", "default/"]:
                pair = get_file_pair(prefix + args.unit, db_path=args.db)
                if pair:
                    break

        if not pair:
            print(f"Error: No pairing found for unit: {args.unit}")
            print("Run './bin/orchestrate rb3-sync' first.")
            sys.exit(1)

        pairs = [pair]
    else:
        # Batch mode
        pairs = query_file_pairs(
            min_compat=args.min_compat,
            pattern=args.pattern,
            limit=args.limit,
            db_path=args.db,
        )

    if not pairs:
        print("No file pairs found matching criteria.")
        return

    if args.dry_run:
        print(f"\n{'='*60}")
        print("RB3-Merge Mode (DRY RUN)")
        print(f"{'='*60}")
        print(f"Files to process: {len(pairs)}")
        print("\n[DRY RUN] Would process:")
        for pair in pairs:
            compat = pair.get('compatibility_score')
            compat_str = f"{compat:.1%}" if compat is not None else "?"
            print(f"  - {pair['dc3_unit']} ({compat_str} compat)")
        return

    # Initialize orchestrator
    orchestrator = DecompOrchestrator(
        db_path=args.db,
        pool_dir=Path(args.pool_dir),
        pool_size=max(args.max_agents, 3),
        main_repo=_project_root,
        auto_apply=not args.no_auto_apply,
    )

    if orchestrator.worktree_pool.status()["total"] < args.max_agents:
        print(f"Initializing worktree pool with {args.max_agents} worktrees...")
        orchestrator.initialize(force=True)

    # Run batch with concurrent agents
    summary = asyncio.run(
        orchestrator.run_rb3_merge_batch(
            file_pairs=pairs,
            model=args.model,
            max_agents=args.max_agents,
            func_limit_per_unit=args.func_limit,
            min_percent=args.min_percent,
            max_percent=args.max_percent,
            verbose=_verbosity(args),
        )
    )

    if args.json:
        print(json.dumps(summary, indent=2, default=str))


def cmd_patch_refresh(args):
    """Refresh stale patches using agents in worktrees."""
    validate_model_backend(args.model)

    # Load manifest
    manifest_path = _project_root / "scratch" / "patches" / "manifest.json"
    if not manifest_path.exists():
        print(f"Error: No manifest found at {manifest_path}")
        print("Run: python scripts/patch_triage.py")
        sys.exit(1)

    manifest = json.loads(manifest_path.read_text())

    # Filter to needs-merge patches (or a specific file)
    if args.patch_file:
        # Single patch file mode
        patch_path = Path(args.patch_file)
        if not patch_path.exists():
            print(f"Error: Patch file not found: {patch_path}")
            sys.exit(1)
        patch_content = patch_path.read_text()

        # Try to find matching manifest entry
        entry = None
        for e in manifest:
            if e.get("filename") == patch_path.name:
                entry = e
                break

        if entry is None:
            # Build a minimal entry from the patch itself
            from orchestrator.patch_applier import clean_patch
            cleaned = clean_patch(patch_content)
            target_files = []
            for line in cleaned.split('\n'):
                if line.startswith('diff --git a/'):
                    parts = line.split(' b/')
                    if len(parts) >= 2:
                        path = parts[-1].strip()
                        if path not in target_files:
                            target_files.append(path)
            entry = {
                "filename": patch_path.name,
                "symbol": "",
                "demangled": "",
                "unit": "",
                "patch_percent": 0,
                "current_percent": 0,
                "target_files": target_files,
                "category": "needs-merge",
            }

        patches = [(entry, patch_content)]
    else:
        # Filter manifest for needs-merge patches
        category = args.category or "needs-merge"
        candidates = [
            e for e in manifest
            if e.get("category") == category
            and e.get("status") not in ("applied", "skipped", "refreshed")
            and e.get("delta", 0) > 0
        ]

        if args.min_delta:
            candidates = [e for e in candidates if e.get("delta", 0) >= args.min_delta]

        # Sort by delta descending (biggest improvements first)
        candidates.sort(key=lambda e: e.get("delta", 0), reverse=True)

        if args.limit and args.limit > 0:
            candidates = candidates[:args.limit]

        if not candidates:
            print(f"No {category} patches found matching criteria.")
            return

        # Load patch content for each candidate
        scratch_dir = _project_root / "scratch" / "patches"
        patches = []
        for entry in candidates:
            cat = entry.get("category", "needs-merge")
            patch_path = scratch_dir / cat / entry["filename"]
            if not patch_path.exists():
                print(f"  Warning: patch file missing: {patch_path}")
                continue
            patches.append((entry, patch_path.read_text()))

    if not patches:
        print("No patches to refresh.")
        return

    if not args.quiet:
        print(f"\nPatch Refresh: {len(patches)} patches")
        for entry, _ in patches:
            name = entry.get("demangled") or entry.get("symbol") or entry.get("filename")
            if len(name) > 60:
                name = name[:57] + "..."
            delta = entry.get("delta", 0)
            print(f"  +{delta:5.1f}%  {name}")
        print()

    # Initialize orchestrator
    orchestrator = DecompOrchestrator(
        db_path=args.db,
        pool_dir=Path(args.pool_dir),
        pool_size=max(args.max_agents, 3),
        main_repo=_project_root,
        auto_apply=False,  # Never auto-apply refreshed patches
    )

    if orchestrator.worktree_pool.status()["total"] < args.max_agents:
        print(f"Initializing worktree pool with {args.max_agents} worktrees...")
        orchestrator.initialize(force=True)

    summary = asyncio.run(
        orchestrator.run_patch_refresh_batch(
            patches=patches,
            max_agents=args.max_agents,
            model=args.model,
            verbose=_verbosity(args),
            dry_run=args.dry_run,
        )
    )

    # Update manifest with refreshed status
    if not args.dry_run:
        refreshed_symbols = set()
        for r in summary.get("results", []):
            if r.get("refreshed_patch"):
                refreshed_symbols.add(r.get("symbol") or r.get("filename"))

        if refreshed_symbols:
            updated = 0
            for entry in manifest:
                key = entry.get("symbol") or entry.get("filename")
                if key in refreshed_symbols:
                    entry["status"] = "refreshed"
                    updated += 1

            manifest_path.write_text(json.dumps(manifest, indent=2))
            print(f"Manifest updated: {updated} entries marked as 'refreshed'")

    if args.json:
        print(json.dumps(summary, indent=2, default=str))


def main():
    parser = argparse.ArgumentParser(
        description="DC3 Decomp Orchestrator - Multi-agent decompilation pipeline"
    )
    parser.add_argument("--db", default="decomp.db", help="Database path")
    parser.add_argument(
        "--pool-dir",
        default=str(DEFAULT_POOL_DIR),
        help="Worktree pool directory",
    )

    subparsers = parser.add_subparsers(dest="command", required=True)

    # init
    p_init = subparsers.add_parser(
        "init",
        help="Initialize worktree pool",
        description="Create a pool of git worktrees for parallel agent execution. Each worktree is isolated to prevent conflicts.",
        epilog="Example: ./bin/orchestrate init --pool-size 5"
    )
    p_init.add_argument("--pool-size", type=int, default=3, help="Number of worktrees to create (default: 3)")
    p_init.add_argument("--force", action="store_true", help="Force recreation of pool (removes existing worktrees)")

    # single
    p_single = subparsers.add_parser(
        "single",
        help="Run agent on a single function",
        description="Run Claude agent on one function with full analysis context and pre-computed decompilations.",
        epilog="Example: ./bin/orchestrate single '?Load@CharClip@@UAAXAAVBinStream@@@Z' --model sonnet"
    )
    p_single.add_argument("symbol", help="Mangled function symbol (copy from progress report)")
    # Model choices generated dynamically from registry (supports all backends)
    available_models = sorted(set(get_available_models("anthropic") + get_available_models("openrouter")))
    p_single.add_argument("--model", choices=available_models, help="Force specific model (default: auto-select)")
    p_single.add_argument("--quiet", "-q", action="store_true", help="Suppress all output")
    p_single.add_argument("--verbose", "-v", action="store_true", help="Full agent output (tool args, results, text)")
    p_single.add_argument("--json", action="store_true", help="Output results as JSON")
    p_single.add_argument("--dry-run", action="store_true", help="Show what would happen without running the agent")
    p_single.add_argument("--force", "-f", action="store_true", help="Force unlock if function is already locked by another session")
    build_group = p_single.add_mutually_exclusive_group()
    build_group.add_argument(
        "--incremental-only",
        action="store_true",
        help="Use incremental build only (fast, ~15s, less safe)",
    )
    build_group.add_argument(
        "--full-build",
        action="store_true",
        help="Use full build only (safe, ~88s, more reliable)",
    )
    # Auto-apply options (enabled by default)
    p_single.add_argument(
        "--no-auto-apply",
        action="store_true",
        help="Disable auto-applying patches to main repo",
    )
    p_single.add_argument(
        "--no-refactor",
        action="store_true",
        help="Skip the Haiku refactor-staff cleanup pass after the main agent",
    )
    p_single.add_argument(
        "--prompt", "-p",
        type=str,
        default=None,
        help="Custom instructions/guidance to append to the agent prompt",
    )

    # batch
    p_batch = subparsers.add_parser(
        "batch",
        help="Run batch processing with parallel agents",
        description="Process multiple functions in parallel with configurable build strategies. Agents work independently in isolated worktrees.",
        epilog="""Examples:
  # Process all char functions by pattern (default)
  ./bin/orchestrate batch 'src/system/char/*' --limit 50

  # Multiple unit patterns at once
  ./bin/orchestrate batch 'default/system/char/*' 'default/system/rndobj/*' --limit 50 -j 3

  # Use priority scoring to select best targets
  ./bin/orchestrate batch --strategy priority --limit 20

  # Focus on functions that can reach 100%
  ./bin/orchestrate batch --strategy priority --reachable-only --limit 30

  # Complete nearly-done units
  ./bin/orchestrate batch --strategy unit-completion --limit 20"""
    )
    p_batch.add_argument("pattern", nargs="*", default=["*"], help="Glob pattern(s) for units (e.g., 'src/system/char/*'). Multiple patterns can be specified. Ignored if --strategy is not 'pattern'")

    # Strategy selection (Phase 2 scoring infrastructure)
    p_batch.add_argument(
        "--strategy",
        choices=["pattern", "priority", "unit-completion"],
        default="pattern",
        help="Target selection strategy: 'pattern' (default, filter by unit glob), 'priority' (use Phase 2 scoring), 'unit-completion' (focus on nearly-done units)"
    )
    p_batch.add_argument(
        "--reachable-only",
        action="store_true",
        help="Only process functions that can reach 100%% (excludes LINKER_MERGED, BOOL_MASK patterns)"
    )
    p_batch.add_argument(
        "--min-priority",
        type=float,
        default=0,
        help="Minimum priority score for selection (only with --strategy priority, default: 0)"
    )
    p_batch.add_argument("-j", "--max-agents", type=int, default=3, help="Maximum parallel agents (default: 3). Increase for faster batches (uses more RAM)")
    p_batch.add_argument("--min-percent", type=float, default=0, help="Only process functions with match at least this many percent (default: 0)")
    p_batch.add_argument("--max-percent", type=float, default=100, help="Only process functions with match at most this many percent (default: 100)")
    # Model choices generated dynamically from registry (supports all backends)
    p_batch.add_argument("--model", choices=available_models, help="Force all agents to use this model")
    p_batch.add_argument("--limit", type=int, default=0, help="Max functions to process (0=unlimited)")
    p_batch.add_argument("--quiet", "-q", action="store_true", help="Suppress all output")
    p_batch.add_argument("--verbose", "-v", action="store_true", help="Full agent output (tool args, results, text)")
    p_batch.add_argument("--json", action="store_true", help="Output results as JSON")

    # Build strategy options
    build_batch_group = p_batch.add_mutually_exclusive_group()
    build_batch_group.add_argument(
        "--incremental-only",
        action="store_true",
        help="Force all builds incremental (fastest mode, ~15s per function, less comprehensive validation)",
    )
    build_batch_group.add_argument(
        "--full-build",
        action="store_true",
        help="Force all builds full (safest mode, ~88s per function, best validation)",
    )
    p_batch.add_argument(
        "--periodic-full",
        type=int,
        default=10,
        help="Run full build every Nth batch for validation (default: 10, 0=disabled). Balances speed and reliability",
    )
    p_batch.add_argument(
        "--validate-diffs",
        action="store_true",
        help="Compare incremental vs full build results for extra validation",
    )

    # Auto-apply options (enabled by default)
    p_batch.add_argument(
        "--no-auto-apply",
        action="store_true",
        help="Disable auto-applying patches to main repo",
    )
    p_batch.add_argument(
        "--no-refactor",
        action="store_true",
        help="Skip the Haiku refactor-staff cleanup pass after the main agent",
    )
    p_batch.add_argument(
        "--exclude-at-limit",
        action="store_true",
        help="Also exclude functions with AT_LIMIT verdict (by default only 100%% COMPLETE functions are excluded)",
    )

    # query
    p_query = subparsers.add_parser(
        "query",
        help="Query functions matching criteria",
        description="Search database for functions matching patterns and match percentage filters. Useful for batch planning and cost estimation.",
        epilog="Examples:\n  # Find incomplete char functions\n  ./bin/orchestrate query --pattern '*char*' --max-percent 99\n  # Find easy wins (high match but not complete)\n  ./bin/orchestrate query --min-percent 95 --max-percent 99\n  # Estimate cost to complete char system\n  ./bin/orchestrate query --pattern 'src/system/char/*' --estimate-cost --model sonnet"
    )
    p_query.add_argument("--pattern", default="*", help="Glob pattern for unit paths (default: all). Examples: 'src/system/char/*', '*CharClip*', 'src/system/world/*'")
    p_query.add_argument("--min-percent", type=float, default=0, help="Only show functions with match at least this many percent (default: 0)")
    p_query.add_argument("--max-percent", type=float, default=100, help="Only show functions with match at most this many percent (default: 100)")
    p_query.add_argument("--limit", type=int, default=20, help="Max results to show (default: 20)")
    p_query.add_argument("--include-complete", action="store_true", help="Include completely matched (100) functions in results")
    p_query.add_argument("--exclude-at-limit", action="store_true", help="Also exclude functions with AT_LIMIT verdict")
    p_query.add_argument("--estimate-cost", action="store_true", help="Show estimated token cost and runtime to complete batch")
    # Model choices generated dynamically from registry (supports all backends)
    p_query.add_argument("--model", choices=available_models, help="Model for cost estimate (if not specified, uses auto-selection)")
    p_query.add_argument("--json", action="store_true", help="Output results as JSON")

    # status
    p_status = subparsers.add_parser(
        "status",
        help="Show orchestrator status",
        description="Display current status: worktree pool state, active sessions, progress statistics.",
        epilog="Example: ./bin/orchestrate status"
    )
    p_status.add_argument("--json", action="store_true", help="Output as JSON")

    # info
    p_info = subparsers.add_parser(
        "info",
        help="Show backend and model information",
        description="Display configured backend (Anthropic/OpenRouter), available models, token budgets, and API status.",
        epilog="Example: ./bin/orchestrate info"
    )
    p_info.add_argument("--json", action="store_true", help="Output as JSON")

    # retry
    p_retry = subparsers.add_parser(
        "retry",
        help="Retry a function with escalated model",
        description="Retry a previously attempted function with a higher-tier model (e.g., retry failed haiku with sonnet).",
        epilog="Example: ./bin/orchestrate retry '?Load@CharClip@@UAAXAAVBinStream@@@Z' --model opus"
    )
    p_retry.add_argument("symbol", help="Mangled function symbol")
    # Model choices generated dynamically from registry (supports all backends)
    p_retry.add_argument("--model", choices=available_models, help="Escalated model to use")
    p_retry.add_argument("--quiet", "-q", action="store_true", help="Suppress all output")
    p_retry.add_argument("--verbose", "-v", action="store_true", help="Full agent output (tool args, results, text)")

    # cleanup
    p_cleanup = subparsers.add_parser(
        "cleanup",
        help="Clean up stale locks and worktrees",
        description="Remove orphaned worktrees and expired function locks from failed agent sessions.",
        epilog="Example: ./bin/orchestrate cleanup --max-age 4 --reset-pool"
    )
    p_cleanup.add_argument("--max-age", type=int, default=2, help="Consider locks older than this many hours as stale (default: 2)")
    p_cleanup.add_argument("--reset-pool", action="store_true", help="Recreate entire worktree pool (removes all worktrees and recreates fresh)")

    # release-locks
    p_release_locks = subparsers.add_parser(
        "release-locks",
        help="Immediately clear all locks",
        description="Clear all function locks and release all worktrees. Use this to recover from crashed/interrupted batch runs.",
        epilog="Example: ./bin/orchestrate release-locks"
    )

    # targets (Phase 2 scoring)
    p_targets = subparsers.add_parser(
        "targets",
        help="Show top priority targets from Phase 2 scoring",
        description="Display functions ranked by priority score (ease × impact × confidence). Use --strategy to switch between priority-based and unit-completion targeting.",
        epilog="""Examples:
  # Show top 20 priority targets
  ./bin/orchestrate targets

  # Show only functions that can reach 100%%
  ./bin/orchestrate targets --reachable-only

  # Focus on near-complete units
  ./bin/orchestrate targets --strategy unit-completion

  # Export as JSON for scripting
  ./bin/orchestrate targets --json --limit 100"""
    )
    p_targets.add_argument(
        "--strategy",
        choices=["priority", "unit-completion"],
        default="priority",
        help="Target selection strategy (default: priority)"
    )
    p_targets.add_argument(
        "--reachable-only",
        action="store_true",
        help="Only show functions that can reach 100%% (no LINKER_MERGED, BOOL_MASK)"
    )
    p_targets.add_argument(
        "--min-priority",
        type=float,
        default=0,
        help="Minimum priority score (default: 0)"
    )
    p_targets.add_argument(
        "--min-percent",
        type=float,
        default=0,
        help="Minimum match percentage (default: 0)"
    )
    p_targets.add_argument(
        "--max-percent",
        type=float,
        default=100,
        help="Maximum match percentage (default: 100)"
    )
    p_targets.add_argument(
        "--limit",
        type=int,
        default=20,
        help="Maximum targets to show (default: 20)"
    )
    p_targets.add_argument("--json", action="store_true", help="Output as JSON")

    # sync
    p_sync = subparsers.add_parser(
        "sync",
        help="Sync database with current report.json",
        description="Update the database with match percentages from the latest build report. Run this after manual code changes to keep the database accurate.",
        epilog="Examples:\n  ./bin/orchestrate sync\n  ./bin/orchestrate sync --build  # Build first, then sync"
    )
    p_sync.add_argument("--build", "-b", action="store_true", help="Run ninja build before syncing")
    p_sync.add_argument("--verbose", "-v", action="store_true", help="Show build output")
    p_sync.add_argument("--json", action="store_true", help="Output results as JSON")

    # analyze-models
    p_analyze = subparsers.add_parser(
        "analyze-models",
        help="Analyze model effectiveness from attempt history",
        description="Analyze and compare model effectiveness, showing improvement distributions by model and starting percentage ranges. Includes cost efficiency metrics.",
        epilog="Examples:\n  ./bin/orchestrate analyze-models\n  ./bin/orchestrate analyze-models --hours 24\n  ./bin/orchestrate analyze-models --model haiku\n  ./bin/orchestrate analyze-models --json"
    )
    p_analyze.add_argument(
        "--hours",
        type=int,
        default=0,
        help="Only analyze attempts from last N hours (0=all time, default: 0)"
    )
    p_analyze.add_argument(
        "--model",
        choices=available_models,
        help="Focus analysis on specific model"
    )
    p_analyze.add_argument(
        "--include-unknown",
        action="store_true",
        help="Include attempts with 'unknown' model (from MCP direct calls)"
    )
    p_analyze.add_argument(
        "--json",
        action="store_true",
        help="Output results as JSON"
    )

    # rb3-sync
    p_rb3_sync = subparsers.add_parser(
        "rb3-sync",
        help="Build/update RB3 file pairing database",
        description="Scan DC3 units and match them with RB3 source files. Calculates compatibility scores based on function name overlap.",
        epilog="Examples:\n  ./bin/orchestrate rb3-sync\n  ./bin/orchestrate rb3-sync --rb3-path ~/rb3/src"
    )
    p_rb3_sync.add_argument(
        "--rb3-path",
        help=f"Path to RB3 source (default: {DEFAULT_RB3_PATH})"
    )
    p_rb3_sync.add_argument("--quiet", "-q", action="store_true", help="Suppress all output")
    p_rb3_sync.add_argument("--verbose", "-v", action="store_true", help="Full agent output (tool args, results, text)")
    p_rb3_sync.add_argument("--json", action="store_true", help="Output as JSON")

    # rb3-query
    p_rb3_query = subparsers.add_parser(
        "rb3-query",
        help="Query RB3 file pairings",
        description="List DC3 units paired with RB3 files, filtered by compatibility score.",
        epilog="Examples:\n  ./bin/orchestrate rb3-query --min-compat 0.8\n  ./bin/orchestrate rb3-query --pattern '*char*' --limit 20"
    )
    p_rb3_query.add_argument("--min-compat", type=float, default=0.0, help="Minimum compatibility score 0.0-1.0 (default: 0)")
    p_rb3_query.add_argument("--pattern", default="*", help="Glob pattern for DC3 unit paths (default: all)")
    p_rb3_query.add_argument("--limit", type=int, default=50, help="Max results (default: 50)")
    p_rb3_query.add_argument("--json", action="store_true", help="Output as JSON")

    # rb3-merge
    p_rb3_merge = subparsers.add_parser(
        "rb3-merge",
        help="Run RB3-assisted decomp on file(s)",
        description="Process DC3 functions using RB3 reference implementations. Uses a specialized prompt that includes the paired RB3 source code.",
        epilog="Examples:\n  ./bin/orchestrate rb3-merge --unit 'system/char/CharBones'\n  ./bin/orchestrate rb3-merge --pattern '*char*' --min-compat 0.9 --limit 5"
    )
    p_rb3_merge.add_argument("--unit", help="Single DC3 unit to process")
    p_rb3_merge.add_argument("--pattern", default="*char*", help="Glob pattern for DC3 units (batch mode)")
    p_rb3_merge.add_argument("--min-compat", type=float, default=0.8, help="Minimum compatibility score (default: 0.8)")
    p_rb3_merge.add_argument("--min-percent", type=float, default=0, help="Minimum function match percent")
    p_rb3_merge.add_argument("--max-percent", type=float, default=99.9, help="Maximum function match percent")
    p_rb3_merge.add_argument("--limit", type=int, default=10, help="Max units to process (default: 10)")
    p_rb3_merge.add_argument("--func-limit", type=int, default=20, help="Max functions per unit (default: 20)")
    p_rb3_merge.add_argument("-j", "--max-agents", type=int, default=1, help="Max parallel agents (default: 1)")
    p_rb3_merge.add_argument("--model", choices=available_models, help="Force specific model")
    p_rb3_merge.add_argument("--quiet", "-q", action="store_true", help="Suppress all output")
    p_rb3_merge.add_argument("--verbose", "-v", action="store_true", help="Full agent output (tool args, results, text)")
    p_rb3_merge.add_argument("--dry-run", action="store_true", help="Show what would be processed")
    p_rb3_merge.add_argument("--no-auto-apply", action="store_true", help="Disable auto-apply patches")
    p_rb3_merge.add_argument("--json", action="store_true", help="Output as JSON")

    # patch-refresh
    p_patch_refresh = subparsers.add_parser(
        "patch-refresh",
        help="Refresh stale/needs-merge patches using agents",
        description="Take stale patches that no longer apply cleanly, run agents in worktrees to apply the intent and produce clean refreshed patches.",
        epilog="""Examples:
  # Refresh top 10 needs-merge patches by delta
  ./bin/orchestrate patch-refresh --limit 10

  # Refresh a specific patch file
  ./bin/orchestrate patch-refresh --patch-file scratch/patches/needs-merge/SomeFunc_85pct.patch

  # Refresh patches with at least 10%% improvement potential
  ./bin/orchestrate patch-refresh --min-delta 10 --limit 20 -j 3

  # Dry run to see what would be refreshed
  ./bin/orchestrate patch-refresh --limit 5 --dry-run"""
    )
    p_patch_refresh.add_argument(
        "--patch-file",
        help="Single patch file to refresh (instead of filtering manifest)"
    )
    p_patch_refresh.add_argument(
        "--category",
        default="needs-merge",
        help="Manifest category to filter (default: needs-merge)"
    )
    p_patch_refresh.add_argument(
        "--min-delta",
        type=float,
        default=0,
        help="Minimum improvement delta to include (default: 0)"
    )
    p_patch_refresh.add_argument(
        "--limit",
        type=int,
        default=0,
        help="Max patches to refresh (0=unlimited, default: 0)"
    )
    p_patch_refresh.add_argument(
        "-j", "--max-agents",
        type=int,
        default=3,
        help="Maximum parallel agents (default: 3)"
    )
    p_patch_refresh.add_argument(
        "--model",
        choices=available_models,
        help="Force specific model (default: sonnet)"
    )
    p_patch_refresh.add_argument("--quiet", "-q", action="store_true", help="Suppress output")
    p_patch_refresh.add_argument("--verbose", "-v", action="store_true", help="Full agent output")
    p_patch_refresh.add_argument("--dry-run", action="store_true", help="Show what would be refreshed")
    p_patch_refresh.add_argument("--json", action="store_true", help="Output as JSON")

    args = parser.parse_args()

    commands = {
        "init": cmd_init,
        "single": cmd_single,
        "batch": cmd_batch,
        "query": cmd_query,
        "status": cmd_status,
        "info": cmd_info,
        "retry": cmd_retry,
        "cleanup": cmd_cleanup,
        "release-locks": cmd_release_locks,
        "targets": cmd_targets,
        "sync": cmd_sync,
        "analyze-models": cmd_analyze_models,
        "rb3-sync": cmd_rb3_sync,
        "rb3-query": cmd_rb3_query,
        "rb3-merge": cmd_rb3_merge,
        "patch-refresh": cmd_patch_refresh,
    }

    try:
        commands[args.command](args)
    except KeyboardInterrupt:
        print("\nInterrupted")
        sys.exit(1)
    except Exception as e:
        print(f"Error: {e}")
        if "--debug" in sys.argv:
            raise
        sys.exit(1)


if __name__ == "__main__":
    main()
