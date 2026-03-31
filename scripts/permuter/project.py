"""Project detection and configuration for multi-project permuter support.

Detects whether we're running inside the DC3 or RB3 decomp project and
provides project-specific paths, build configuration, and compile command
handling.

Supported projects:
    - DC3 (Dance Central 3): Xbox 360, MSVC PPC cl.exe, .obj files
    - RB3 (Rock Band 3): Wii, MetroWerks mwcceppc, .o files

Detection order:
    1. PERMUTER_PROJECT env var ("dc3" or "rb3")
    2. Repo root directory name heuristic
    3. Presence of config/SZBE69_B8/ (RB3) vs build/373307D9/ (DC3)
"""

from __future__ import annotations

import os
import re
from dataclasses import dataclass
from enum import Enum, auto
from functools import lru_cache
from pathlib import Path


class ProjectType(Enum):
    DC3 = auto()
    RB3 = auto()


@dataclass(frozen=True)
class ProjectConfig:
    """Immutable project-specific configuration."""

    project_type: ProjectType
    repo_root: Path

    # Build paths
    build_id: str            # "373307D9" (DC3) or "SZBE69_B8" (RB3)
    obj_extension: str       # ".obj" (DC3) or ".o" (RB3)

    # Compile command format
    uses_cd_prefix: bool     # DC3's ninja emits "cd dir && ..."
    output_flag: str         # "/Fo" (MSVC) or "-o" (mwcceppc)

    # objdiff
    objdiff_cli: str         # relative path to objdiff-cli

    # m2c
    m2c_target: str          # "ppc" for both, but kept for extensibility

    @property
    def build_prefix(self) -> str:
        """Build directory prefix, e.g. 'build/373307D9'."""
        return f"build/{self.build_id}"

    def obj_target_for_source(self, source_path: Path) -> str:
        """Convert a source .cpp path to its ninja build target.

        DC3: src/system/rndobj/Foo.cpp -> build/373307D9/src/system/rndobj/Foo.obj
        RB3: src/system/rndobj/Foo.cpp -> build/SZBE69_B8/src/system/rndobj/Foo.o
        """
        obj_path = source_path.with_suffix(self.obj_extension)
        try:
            if obj_path.is_absolute():
                obj_path = obj_path.relative_to(self.repo_root)
        except ValueError:
            pass
        return f"{self.build_prefix}/{obj_path}"

    def obj_path_for_unit(self, unit: str) -> Path:
        """Convert a unit name to its built object path.

        DC3: system/rndobj/Foo -> build/373307D9/obj/system/rndobj/Foo.obj
        RB3: system/rndobj/Foo -> build/SZBE69_B8/obj/system/rndobj/Foo.o
        """
        normalized = unit[len("default/"):] if unit.startswith("default/") else unit
        if self.project_type == ProjectType.DC3:
            return self.repo_root / "build" / self.build_id / "obj" / f"{normalized}{self.obj_extension}"
        else:
            # RB3 uses build/SZBE69_B8/obj/<unit>.o
            return self.repo_root / "build" / self.build_id / "obj" / f"{normalized}{self.obj_extension}"

    def baselines_dir(self) -> Path:
        """Directory for baseline storage."""
        return self.repo_root / "build" / self.build_id / "baselines"

    def extract_compile_output_path(self, compile_cmd: str) -> str | None:
        """Extract the output object path from a compile command.

        DC3 (MSVC): looks for /FoPath
        RB3 (mwcceppc): looks for -o dir (output directory, not full path)
        """
        if self.project_type == ProjectType.DC3:
            m = re.search(r'/Fo(\S+)', compile_cmd)
            return m.group(1) if m else None
        else:
            # mwcceppc uses "-o dir" where dir is the output directory
            # The actual output file is dir/SourceName.o
            m = re.search(r'-o\s+(\S+)', compile_cmd)
            return m.group(1) if m else None

    def replace_output_path(self, compile_cmd: str, old_path: str, new_path: str) -> str:
        """Replace the output path in a compile command.

        DC3: /FoOldPath -> /FoNewPath
        RB3: -o old_dir -> -o new_dir (or replace the full -o path)
        """
        if self.project_type == ProjectType.DC3:
            return compile_cmd.replace(f"/Fo{old_path}", f"/Fo{new_path}")
        else:
            return compile_cmd.replace(f"-o {old_path}", f"-o {new_path}")

    def parse_ninja_command(self, ninja_output: str) -> tuple[str | None, str]:
        """Parse ninja -t commands output into (cwd, shell_cmd).

        DC3: Multiple lines, last "cd dir && cmd" line is the compile.
        RB3: Single line with no cd prefix; may have && chain with dep transform.
        """
        if self.project_type == ProjectType.DC3:
            # DC3: look for last "cd " line
            cmd_line = None
            for line in ninja_output.strip().splitlines():
                if line.startswith("cd "):
                    cmd_line = line
            if cmd_line is None:
                lines = ninja_output.strip().splitlines()
                if not lines:
                    return None, ""
                cmd_line = lines[-1]

            if cmd_line.startswith("cd "):
                parts = cmd_line.split(" && ", 1)
                cwd = parts[0][3:]  # strip "cd "
                shell_cmd = parts[1]
                return cwd, shell_cmd
            return None, cmd_line

        else:
            # RB3: the compile command is typically the last line,
            # may be "wibo mwcceppc ... -c src.cpp -o dir && python transform_dep.py ..."
            lines = ninja_output.strip().splitlines()
            # Skip download_tool lines
            cmd_line = None
            for line in lines:
                if "mwcceppc" in line or (line.strip() and not line.strip().startswith('"') and "download_tool" not in line):
                    cmd_line = line
            if cmd_line is None and lines:
                cmd_line = lines[-1]
            if cmd_line is None:
                return None, ""

            # Strip the && transform_dep.py suffix if present
            if " && " in cmd_line:
                parts = cmd_line.split(" && ")
                # Keep only the compile part (contains mwcceppc or -c)
                for part in parts:
                    if "mwcceppc" in part or "-c " in part:
                        cmd_line = part.strip()
                        break
                else:
                    cmd_line = parts[0].strip()

            # RB3 commands run from repo root (no cd prefix)
            return None, cmd_line

    def redirect_source_in_cmd(self, cmd: str, src_name: str, work_name: str) -> str:
        """Redirect the source file in a compile command to a working copy.

        DC3 (MSVC): source is the last token on the command line.
        RB3 (mwcceppc): source follows the -c flag.
        """
        if self.project_type == ProjectType.DC3:
            if cmd.endswith(src_name):
                return cmd[:-len(src_name)] + work_name
            return cmd.replace(src_name, work_name)
        else:
            # RB3: replace "source.cpp" wherever it appears, but be careful
            # to only replace the source path (after -c flag typically)
            return cmd.replace(src_name, work_name)

    def redirect_output_for_parallel(
        self, cmd: str, compile_fo_path: str | None, obj_path: Path, obj_output: Path
    ) -> str:
        """Redirect the output in a compile command to a different .o/.obj path.

        For parallel builds, each variant needs its own output path.
        """
        if self.project_type == ProjectType.DC3:
            if compile_fo_path:
                return cmd.replace(f"/Fo{compile_fo_path}", f"/Fo{obj_output}")
            return cmd.replace(str(obj_path), str(obj_output))
        else:
            # RB3: mwcceppc accepts "-o file_path" directly (not just dir).
            # Replace "-o dir" with "-o /path/to/variant_N.o".
            if compile_fo_path:
                return cmd.replace(f"-o {compile_fo_path}", f"-o {obj_output}")
            return cmd.replace(str(obj_path), str(obj_output))

    @property
    def has_il_tools(self) -> bool:
        """Whether this project has IL capture tools (DC3 only currently)."""
        if self.project_type == ProjectType.DC3:
            return (self.repo_root / "msvc-src" / "tools").is_dir()
        return False

    @property
    def has_unicorn_runner(self) -> bool:
        """Whether this project has unicorn execution comparison."""
        if self.project_type == ProjectType.DC3:
            try:
                import importlib
                importlib.import_module("scripts.unicorn_runner.run")
                return True
            except ImportError:
                return False
        return False


# ── Detection ───────────────────────────────────────────────────────────────

def _detect_project_type(repo_root: Path) -> ProjectType:
    """Detect project type from repo root."""
    # 1. Environment variable override
    env = os.environ.get("PERMUTER_PROJECT", "").lower().strip()
    if env == "rb3":
        return ProjectType.RB3
    if env == "dc3":
        return ProjectType.DC3

    # 2. Check for project-specific directories
    if (repo_root / "config" / "SZBE69_B8").is_dir():
        return ProjectType.RB3
    if (repo_root / "build" / "373307D9").is_dir():
        return ProjectType.DC3

    # 3. Directory name heuristic
    name = repo_root.name.lower()
    if "rb3" in name:
        return ProjectType.RB3
    if "dc3" in name:
        return ProjectType.DC3

    # Default to DC3 for backward compatibility
    return ProjectType.DC3


def _make_config(project_type: ProjectType, repo_root: Path) -> ProjectConfig:
    """Create a ProjectConfig for the given project type."""
    if project_type == ProjectType.DC3:
        return ProjectConfig(
            project_type=ProjectType.DC3,
            repo_root=repo_root,
            build_id="373307D9",
            obj_extension=".obj",
            uses_cd_prefix=True,
            output_flag="/Fo",
            objdiff_cli="bin/objdiff-cli",
            m2c_target="ppc",
        )
    else:
        return ProjectConfig(
            project_type=ProjectType.RB3,
            repo_root=repo_root,
            build_id="SZBE69_B8",
            obj_extension=".o",
            uses_cd_prefix=False,
            output_flag="-o",
            objdiff_cli="bin/objdiff-cli",
            m2c_target="ppc",
        )


def _resolve_repo_root() -> Path:
    """Detect repo root from cwd, falling back to __file__ location.

    When the permuter is symlinked from another project (e.g. RB3 -> DC3),
    this ensures the correct project is detected based on where the user
    is running from, not where the code physically lives.
    """
    cwd = Path.cwd().resolve()
    for candidate in [cwd] + list(cwd.parents):
        if (candidate / "config" / "SZBE69_B8").is_dir():
            return candidate
        if (candidate / "build" / "373307D9").is_dir():
            return candidate
        if (candidate / "objdiff.json").is_file():
            return candidate
    # Fallback to __file__ location
    return Path(__file__).resolve().parent.parent.parent


@lru_cache(maxsize=4)
def _get_project_config_cached(repo_root: Path) -> ProjectConfig:
    """Cached project config creation."""
    project_type = _detect_project_type(repo_root)
    return _make_config(project_type, repo_root)


def get_project_config(repo_root: Path | None = None) -> ProjectConfig:
    """Get or create the project configuration.

    If repo_root is None, detects from the current working directory first,
    then falls back to __file__ resolution.

    Results are cached per resolved repo_root.
    """
    if repo_root is None:
        repo_root = _resolve_repo_root()
    return _get_project_config_cached(repo_root)


def get_project_for_path(source_path: Path) -> ProjectConfig:
    """Detect the project from a source file path.

    Walks up the directory tree looking for project markers.
    """
    path = source_path.resolve()
    for parent in [path] + list(path.parents):
        if (parent / "config" / "SZBE69_B8").is_dir():
            return get_project_config(parent)
        if (parent / "build" / "373307D9").is_dir():
            return get_project_config(parent)
        if (parent / "objdiff.json").is_file():
            return get_project_config(parent)
    # Fallback
    return get_project_config()
