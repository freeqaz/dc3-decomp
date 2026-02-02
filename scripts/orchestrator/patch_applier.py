"""Auto-apply patches from agent worktrees to main repository.

When an agent works on a function, any non-error patches are applied to the
main repo — even without match improvement, style/cleanup changes are valuable.
"""

import logging
import subprocess
import tempfile
from datetime import datetime
from pathlib import Path
from typing import Optional


logger = logging.getLogger("decomp_orchestrator")


def clean_patch(patch: str) -> str:
    """Remove spurious changes from patch (e.g., .gitkeep deletions, orig/ files).

    Args:
        patch: Raw git diff output

    Returns:
        Cleaned patch with only relevant changes
    """
    lines = patch.split('\n')
    cleaned = []
    skip_until_next_diff = False

    # Only allow patches to files in src/ and include/ directories
    allowed_prefixes = ('diff --git a/src/', 'diff --git a/include/')

    for line in lines:
        if line.startswith('diff --git'):
            # Only keep changes to source/header files
            if not any(line.startswith(p) for p in allowed_prefixes):
                skip_until_next_diff = True
                continue
            # Also filter known spurious files within allowed dirs
            if '.gitkeep' in line:
                skip_until_next_diff = True
                continue
            skip_until_next_diff = False

        if not skip_until_next_diff:
            cleaned.append(line)

    return '\n'.join(cleaned)


def apply_patch_to_main(
    patch: str,
    main_repo: Path,
    dry_run: bool = False,
    allow_partial: bool = True,
) -> dict:
    """Apply a patch to the main repository.

    Args:
        patch: Git diff patch content
        main_repo: Path to main repository
        dry_run: If True, only check if patch would apply
        allow_partial: If True, apply hunks that succeed even if some fail

    Returns:
        Dict with:
            - success: bool - whether patch applied (fully or partially)
            - applied: bool - whether any changes were applied
            - message: str - description of result
            - failed_files: list[str] - files that failed to apply
    """
    if not patch or not patch.strip():
        return {
            "success": True,
            "applied": False,
            "message": "No patch to apply",
            "failed_files": [],
        }

    # Clean the patch first
    cleaned_patch = clean_patch(patch)
    if not cleaned_patch.strip():
        return {
            "success": True,
            "applied": False,
            "message": "Patch contained only spurious changes",
            "failed_files": [],
        }

    # Write patch to temp file
    with tempfile.NamedTemporaryFile(mode='w', suffix='.patch', delete=False) as f:
        f.write(cleaned_patch)
        patch_file = Path(f.name)

    try:
        # First, try to apply cleanly
        cmd = ['git', 'apply']
        if dry_run:
            cmd.append('--check')
        cmd.append(str(patch_file))

        result = subprocess.run(
            cmd,
            cwd=main_repo,
            capture_output=True,
            text=True,
        )

        if result.returncode == 0:
            action = "would apply" if dry_run else "applied"
            return {
                "success": True,
                "applied": not dry_run,
                "message": f"Patch {action} cleanly",
                "failed_files": [],
            }

        # If clean apply failed and we allow partial, try --reject
        if allow_partial and not dry_run:
            reject_cmd = ['git', 'apply', '--reject', str(patch_file)]
            reject_result = subprocess.run(
                reject_cmd,
                cwd=main_repo,
                capture_output=True,
                text=True,
            )

            # Collect .rej files (failed hunks)
            rej_files = list(main_repo.rglob('*.rej'))
            failed_files = [str(f.relative_to(main_repo)) for f in rej_files]

            # Clean up .rej files
            for rej in rej_files:
                rej.unlink()

            if reject_result.returncode == 0 or not failed_files:
                return {
                    "success": True,
                    "applied": True,
                    "message": "Patch applied cleanly",
                    "failed_files": [],
                }
            else:
                # Some parts applied, some failed
                return {
                    "success": True,  # Partial success is still success
                    "applied": True,
                    "message": f"Patch partially applied ({len(failed_files)} file(s) had conflicts)",
                    "failed_files": failed_files,
                }

        # Clean apply failed and we don't allow partial
        return {
            "success": False,
            "applied": False,
            "message": f"Patch failed to apply: {result.stderr.strip()}",
            "failed_files": [],
        }

    finally:
        patch_file.unlink(missing_ok=True)


class PatchApplier:
    """Manages automatic patch application from agent results.

    All non-error patches are applied (style/cleanup changes are valuable).
    Only patches from errored agents are skipped.

    Configuration options:
        - enabled: Whether auto-apply is on
        - allow_partial: Apply partial patches on conflict (default: True)
    """

    def __init__(
        self,
        main_repo: Path,
        enabled: bool = True,
        min_progress: float = 0.0,
        require_improvement: bool = True,
        allow_partial: bool = True,
    ):
        self.main_repo = Path(main_repo).resolve()
        self.patches_dir = self.main_repo / "generated-patches"
        self.enabled = enabled
        self.allow_partial = allow_partial

        # Stats
        self.applied_count = 0
        self.skipped_count = 0
        self.failed_count = 0

    def write_patch_file(self, patch: str, symbol: str, percent: float) -> Path:
        """Write patch to generated-patches/ directory for review/tracking.

        Args:
            patch: Git diff patch content
            symbol: Function symbol (mangled name)
            percent: Final match percentage

        Returns:
            Path to the written patch file
        """
        self.patches_dir.mkdir(exist_ok=True)

        # Create filename from symbol (sanitized)
        safe_name = symbol.replace("?", "").replace("@", "_")[:50]
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"{safe_name}_{percent:.0f}pct_{timestamp}.patch"

        patch_path = self.patches_dir / filename
        patch_path.write_text(patch)
        logger.debug(f"Wrote patch file: {patch_path}")
        return patch_path

    def maybe_apply(
        self,
        patch: Optional[str],
        start_percent: float,
        end_percent: float,
        exit_status: str,
        symbol: str = "",
    ) -> dict:
        """Conditionally apply a patch based on configuration and results.

        Args:
            patch: Git diff content
            start_percent: Match % before agent
            end_percent: Match % after agent
            exit_status: Agent exit status
            symbol: Function symbol (for logging)

        Returns:
            Result dict with applied status and message
        """
        if not self.enabled:
            return {
                "success": True,
                "applied": False,
                "message": "Auto-apply disabled",
            }

        if not patch or not patch.strip():
            return {
                "success": True,
                "applied": False,
                "message": "No patch to apply",
            }

        # Don't apply patches from errored agents — code may be broken
        if exit_status == "error":
            self.skipped_count += 1
            logger.debug(f"Skipping patch for {symbol}: agent errored")
            return {
                "success": True,
                "applied": False,
                "message": "Skipped: Agent errored",
            }

        # Determine reason for logging (improvement vs style-only)
        improvement = end_percent - start_percent
        if improvement > 0:
            reason = f"Progress: {start_percent:.1f}% -> {end_percent:.1f}% (+{improvement:.1f}%)"
        else:
            reason = f"Style/cleanup changes ({start_percent:.1f}% -> {end_percent:.1f}%)"

        # Write patch file for review/tracking
        patch_path = self.write_patch_file(patch, symbol, end_percent)

        # Always apply non-error patches — even without match improvement,
        # refactor/style changes are valuable
        logger.info(f"Auto-applying patch for {symbol}: {reason}")
        result = apply_patch_to_main(
            patch=patch,
            main_repo=self.main_repo,
            allow_partial=self.allow_partial,
        )

        if result["applied"]:
            self.applied_count += 1
            logger.info(f"Applied patch for {symbol}: {result['message']}")
            if result.get("failed_files"):
                logger.warning(f"  Conflicts in: {', '.join(result['failed_files'])}")
        elif not result["success"]:
            self.failed_count += 1
            logger.warning(f"Failed to apply patch for {symbol}: {result['message']}")

        return result

    def stats(self) -> dict:
        """Get application statistics."""
        return {
            "applied": self.applied_count,
            "skipped": self.skipped_count,
            "failed": self.failed_count,
        }
