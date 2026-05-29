"""Project detection and configuration for multi-project source synthesis.

Detects which decomp project we're running inside and provides project-specific
paths, build configuration, and compile-command handling.  **All target-specific
facts come from a ``decomp-synth.json`` file at the repo root** (legacy name:
``permuter.json``, still read as a fallback).  The on-disk marker detection below
only supplies defaults for fields the config omits.

Known in-house targets (each ships a decomp-synth.json):
    - dc3       (Dance Central 3, Xbox 360):  MSVC PPC,  .obj, build/373307D9
    - rb3       (Rock Band 3, Wii):           MetroWerks mwcceppc, .o, build/SZBE69_B8
    - rb3-xenon (Rock Band 3, Xbox 360):      MSVC PPC,  .obj, build/45410914, flat obj layout

The two axes that used to be fused into a single ``ProjectType`` enum are now
independent and read from config:
    * toolchain  -- "msvc" | "mwcc" (drives compile-command parsing + obj naming)
    * build_id   -- the per-target build directory / title id
Plus a third axis, ``obj_layout``, for how the target .obj is located:
    * "mirror"      -- target mirrors the src subtree (dc3, rb3)
    * "objdiff_map" -- consult objdiff.json's base_path -> target_path (rb3-xenon)

Config resolution order for each field: decomp-synth.json > legacy permuter.json
> value derived from the detected toolchain > on-disk marker default.
"""

from __future__ import annotations

import json
import os
import re
from dataclasses import dataclass
from enum import Enum, auto
from functools import lru_cache
from pathlib import Path

# New config filename first, legacy name second (read as a fallback during the
# permuter -> decomp-synth transition).
_CONFIG_FILENAMES = ("decomp-synth.json", "permuter.json")


class ProjectType(Enum):
    """Legacy coarse project label.  Retained for backward compatibility and
    logging only — no behaviour branches on it anymore (use ``toolchain``)."""

    DC3 = auto()
    RB3 = auto()


@dataclass(frozen=True)
class ProjectConfig:
    """Immutable project-specific configuration, sourced from decomp-synth.json."""

    project_type: ProjectType  # legacy label; derived from toolchain
    name: str                  # config "name", e.g. "dc3" / "rb3" / "rb3-xenon"
    repo_root: Path

    # Toolchain axis — the real driver of compile-command + obj behaviour.
    toolchain: str             # "msvc" | "mwcc"

    # Build paths
    build_id: str              # "373307D9" / "SZBE69_B8" / "45410914"
    obj_extension: str         # ".obj" (msvc) or ".o" (mwcc)

    # Compile command format
    uses_cd_prefix: bool       # whether ninja emits "cd dir && ..." (dc3 only)
    output_flag: str           # "/Fo" (msvc) or "-o" (mwcc)

    # How the target (original) .obj is located from the compiled (base) .obj.
    obj_layout: str            # "mirror" | "objdiff_map"

    # objdiff
    objdiff_cli: str           # repo-relative path to objdiff-cli

    # m2c
    m2c_target: str            # "ppc" for current targets, kept for extensibility

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

    def target_obj_for_base_obj(self, base_obj: Path) -> Path:
        """Derive the original (target) .obj path from the compiled (base) .obj.

        Two layouts, selected by ``obj_layout``:

        * "mirror" (dc3, rb3): base lives under build/<id>/src/... and the target
          mirrors that subtree under build/<id>/obj/...
            build/373307D9/src/system/rndobj/Foo.obj
              -> build/373307D9/obj/system/rndobj/Foo.obj

        * "objdiff_map" (rb3-xenon): the dtk split emits a *flat* obj/ keyed by
          basename, so consult objdiff.json's authoritative base_path ->
          target_path map, falling back to flat obj/<basename>.
            build/45410914/src/system/beatmatch/MasterAudio.obj
              -> build/45410914/obj/MasterAudio.obj

        Works for both relative and absolute base_obj paths.
        """
        abs_base = base_obj if base_obj.is_absolute() else self.repo_root / base_obj

        if self.obj_layout == "objdiff_map":
            mapped = self._objdiff_target_for_base(abs_base)
            if mapped is not None:
                return mapped
            # Fallback: flat obj/ layout keyed by basename.
            return self.repo_root / "build" / self.build_id / "obj" / abs_base.name

        # "mirror": target mirrors the src subtree.
        src_prefix = self.repo_root / "build" / self.build_id / "src"
        obj_prefix = self.repo_root / "build" / self.build_id / "obj"
        try:
            rel = abs_base.relative_to(src_prefix)
            return obj_prefix / rel
        except ValueError:
            # Path doesn't follow the expected layout; fall back to the base obj
            # so callers at least get something (objdiff will report a mismatch).
            return abs_base

    def _objdiff_target_for_base(self, abs_base: Path) -> Path | None:
        """Look up the target obj for a base obj via objdiff.json's unit map."""
        mapping = dict(_objdiff_base_to_target(self.repo_root))
        if not mapping:
            return None
        try:
            rel_base = str(abs_base.relative_to(self.repo_root))
        except ValueError:
            rel_base = str(abs_base)
        target_rel = mapping.get(rel_base) or mapping.get(str(abs_base))
        if target_rel:
            return self.repo_root / target_rel
        return None

    def obj_path_for_unit(self, unit: str) -> Path:
        """Convert a unit name to its built object path.

        DC3: system/rndobj/Foo -> build/373307D9/obj/system/rndobj/Foo.obj
        RB3: system/rndobj/Foo -> build/SZBE69_B8/obj/system/rndobj/Foo.o
        """
        normalized = unit[len("default/"):] if unit.startswith("default/") else unit
        return self.repo_root / "build" / self.build_id / "obj" / f"{normalized}{self.obj_extension}"

    def baselines_dir(self) -> Path:
        """Directory for baseline storage."""
        return self.repo_root / "build" / self.build_id / "baselines"

    def extract_compile_output_path(self, compile_cmd: str) -> str | None:
        """Extract the output object path from a compile command.

        MSVC: looks for /FoPath
        mwcceppc: looks for -o dir (output directory, not full path)
        """
        if self.toolchain == "msvc":
            m = re.search(r'/Fo(\S+)', compile_cmd)
            return m.group(1) if m else None
        else:
            # mwcceppc uses "-o dir" where dir is the output directory
            # The actual output file is dir/SourceName.o
            m = re.search(r'-o\s+(\S+)', compile_cmd)
            return m.group(1) if m else None

    def replace_output_path(self, compile_cmd: str, old_path: str, new_path: str) -> str:
        """Replace the output path in a compile command.

        MSVC: /FoOldPath -> /FoNewPath
        mwcceppc: -o old_dir -> -o new_dir
        """
        if self.toolchain == "msvc":
            return compile_cmd.replace(f"/Fo{old_path}", f"/Fo{new_path}")
        else:
            return compile_cmd.replace(f"-o {old_path}", f"-o {new_path}")

    def parse_ninja_command(self, ninja_output: str) -> tuple[str | None, str]:
        """Parse ninja -t commands output into (cwd, shell_cmd).

        MSVC (dc3 / rb3-xenon): may have a "cd dir && cmd" line (dc3) or a plain
            command run from the repo root (rb3-xenon).  Both are handled: we take
            the last "cd " line if present, else the last command line with no cwd.
        mwcceppc (rb3): single line with no cd prefix; may have an && chain with a
            dep transform that we strip.
        """
        if self.toolchain == "msvc":
            # Look for the last "cd " line (dc3); fall back to the last line.
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
            # mwcceppc: the compile command is typically the last line,
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

            # mwcceppc commands run from repo root (no cd prefix)
            return None, cmd_line

    def redirect_source_in_cmd(self, cmd: str, src_name: str, work_name: str) -> str:
        """Redirect the source file in a compile command to a working copy.

        MSVC: source is the last token on the command line.
        mwcceppc: source follows the -c flag.
        """
        if self.toolchain == "msvc":
            if cmd.endswith(src_name):
                return cmd[:-len(src_name)] + work_name
            return cmd.replace(src_name, work_name)
        else:
            # mwcceppc: replace "source.cpp" wherever it appears, but be careful
            # to only replace the source path (after -c flag typically)
            return cmd.replace(src_name, work_name)

    def redirect_output_for_parallel(
        self, cmd: str, compile_fo_path: str | None, obj_path: Path, obj_output: Path
    ) -> str:
        """Redirect the output in a compile command to a different .o/.obj path.

        For parallel builds, each variant needs its own output path.
        """
        if self.toolchain == "msvc":
            if compile_fo_path:
                return cmd.replace(f"/Fo{compile_fo_path}", f"/Fo{obj_output}")
            return cmd.replace(str(obj_path), str(obj_output))
        else:
            # mwcceppc accepts "-o file_path" directly (not just dir).
            # Replace "-o dir" with "-o /path/to/variant_N.o".
            if compile_fo_path:
                return cmd.replace(f"-o {compile_fo_path}", f"-o {obj_output}")
            return cmd.replace(str(obj_path), str(obj_output))

    @property
    def has_il_tools(self) -> bool:
        """Whether this project has MSVC IL capture tools (msvc + tools present)."""
        return self.toolchain == "msvc" and (self.repo_root / "msvc-src" / "tools").is_dir()

    @property
    def has_unicorn_runner(self) -> bool:
        """Whether a unicorn execution-comparison runner is importable."""
        try:
            import importlib
            importlib.import_module("scripts.unicorn_runner.run")
            return True
        except ImportError:
            return False


# ── Config loading + detection ────────────────────────────────────────────────

def _load_project_json(repo_root: Path) -> dict:
    """Read decomp-synth.json (or legacy permuter.json) from the repo root.

    Returns an empty dict when no config file is present or it can't be parsed.
    """
    for fname in _CONFIG_FILENAMES:
        path = repo_root / fname
        if path.is_file():
            try:
                data = json.loads(path.read_text())
                if isinstance(data, dict):
                    return data
            except (OSError, ValueError):
                pass
    return {}


def _detect_defaults(repo_root: Path) -> tuple[str | None, str]:
    """Infer (build_id, toolchain) defaults from env + on-disk markers.

    Only used to fill fields the config omits.  Mirrors the historical detection
    order (env override, then build/config dir markers), defaulting to the
    DC3/msvc profile for backward compatibility when nothing else matches.
    """
    env = os.environ.get("DECOMP_SYNTH_PROJECT") or os.environ.get("PERMUTER_PROJECT") or ""
    env = env.lower().strip()
    if env == "rb3":
        return "SZBE69_B8", "mwcc"
    if env == "dc3":
        return "373307D9", "msvc"

    if (repo_root / "config" / "SZBE69_B8").is_dir():
        return "SZBE69_B8", "mwcc"
    if (repo_root / "build" / "373307D9").is_dir():
        return "373307D9", "msvc"

    # Last-resort directory-name heuristic, kept only as a default for build_id;
    # an explicit decomp-synth.json always wins over this (and is the reason the
    # old "rb3 in path -> mwcc" misdetection no longer bites rb3-xenon).
    name = repo_root.name.lower()
    if "dc3" in name:
        return "373307D9", "msvc"
    if "rb3" in name:
        return "SZBE69_B8", "mwcc"

    # Default to the DC3/msvc profile for backward compatibility.
    return "373307D9", "msvc"


def _legacy_project_type(toolchain: str) -> ProjectType:
    """Map a toolchain to the coarse legacy label (informational only)."""
    return ProjectType.RB3 if toolchain == "mwcc" else ProjectType.DC3


_KNOWN_NAMES = {
    ("msvc", "373307D9"): "dc3",
    ("mwcc", "SZBE69_B8"): "rb3",
    ("msvc", "45410914"): "rb3-xenon",
}


def _make_config(repo_root: Path) -> ProjectConfig:
    """Build a ProjectConfig from decomp-synth.json, with detected defaults."""
    cfg = _load_project_json(repo_root)
    det_build_id, det_toolchain = _detect_defaults(repo_root)

    # toolchain: config "compiler"/"toolchain" > detected default.
    toolchain = str(cfg.get("compiler") or cfg.get("toolchain") or det_toolchain).lower()
    if toolchain not in ("msvc", "mwcc"):
        toolchain = det_toolchain

    is_msvc = toolchain == "msvc"

    build_id = str(cfg.get("build_id") or det_build_id or "373307D9")
    obj_extension = str(cfg.get("obj_extension") or (".obj" if is_msvc else ".o"))
    output_flag = str(cfg.get("output_flag") or ("/Fo" if is_msvc else "-o"))
    uses_cd_prefix = cfg.get("uses_cd_prefix")
    if uses_cd_prefix is None:
        uses_cd_prefix = is_msvc  # dc3 emits "cd dir && ..."; rb3-xenon sets false
    objdiff_cli = str(cfg.get("objdiff_cli") or "bin/objdiff-cli")
    m2c_target = str(cfg.get("m2c_target") or "ppc")
    obj_layout = str(cfg.get("obj_layout") or "mirror")
    name = str(cfg.get("name") or _KNOWN_NAMES.get((toolchain, build_id)) or f"{toolchain}-{build_id}")

    return ProjectConfig(
        project_type=_legacy_project_type(toolchain),
        name=name,
        repo_root=repo_root,
        toolchain=toolchain,
        build_id=build_id,
        obj_extension=obj_extension,
        uses_cd_prefix=bool(uses_cd_prefix),
        output_flag=output_flag,
        obj_layout=obj_layout,
        objdiff_cli=objdiff_cli,
        m2c_target=m2c_target,
    )


def _resolve_repo_root() -> Path:
    """Detect repo root from cwd, falling back to __file__ location.

    When the permuter is symlinked from another project (e.g. RB3 -> DC3),
    this ensures the correct project is detected based on where the user
    is running from, not where the code physically lives.
    """
    cwd = Path.cwd().resolve()
    for candidate in [cwd] + list(cwd.parents):
        if any((candidate / f).is_file() for f in _CONFIG_FILENAMES):
            return candidate
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
    return _make_config(repo_root)


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
        if any((parent / f).is_file() for f in _CONFIG_FILENAMES):
            return get_project_config(parent)
        if (parent / "config" / "SZBE69_B8").is_dir():
            return get_project_config(parent)
        if (parent / "build" / "373307D9").is_dir():
            return get_project_config(parent)
        if (parent / "objdiff.json").is_file():
            return get_project_config(parent)
    # Fallback
    return get_project_config()


@lru_cache(maxsize=4)
def _objdiff_base_to_target(repo_root: Path) -> tuple[tuple[str, str], ...]:
    """Cache objdiff.json's base_path -> target_path map for objdiff_map layout."""
    try:
        data = json.loads((repo_root / "objdiff.json").read_text())
    except (OSError, ValueError):
        return ()
    mapping: dict[str, str] = {}
    for unit in data.get("units", []):
        base = unit.get("base_path")
        target = unit.get("target_path")
        if base and target:
            mapping[base] = target
    return tuple(mapping.items())


@lru_cache(maxsize=4)
def _load_objdiff_unit_names(objdiff_path: Path) -> tuple[str, ...]:
    """Load and cache the set of unit names from a project's objdiff.json."""
    try:
        with open(objdiff_path) as fp:
            data = json.load(fp)
    except (OSError, ValueError):
        return ()
    units = data.get("units") or []
    return tuple(u.get("name", "") for u in units if u.get("name"))


def validate_unit_name(unit: str, repo_root: Path | None = None) -> tuple[bool, list[str]]:
    """Verify a unit name exists in the project's objdiff.json.

    Returns (ok, suggestions). When ok is False, suggestions is a small list
    of unit names with the same basename as the input, intended as a hint
    ("did you mean main/band3/game/VocalPlayer?").

    Returns (True, []) without checking when no objdiff.json is present —
    we don't want to break workflows that bypass objdiff (e.g. tests).
    """
    if repo_root is None:
        repo_root = _resolve_repo_root()
    objdiff_path = repo_root / "objdiff.json"
    if not objdiff_path.is_file():
        return True, []
    units = _load_objdiff_unit_names(objdiff_path)
    if not units:
        return True, []
    if unit in units:
        return True, []
    # Build suggestions: same trailing basename ("game/VocalPlayer" matches
    # any unit ending in "/VocalPlayer" or exactly "VocalPlayer").
    basename = unit.rsplit("/", 1)[-1]
    suggestions: list[str] = []
    for name in units:
        tail = name.rsplit("/", 1)[-1]
        if tail == basename:
            suggestions.append(name)
    # Also include any unit containing the input as a substring, in case the
    # user provided a partial prefix.  Cap total suggestions to keep output
    # readable.
    if not suggestions:
        for name in units:
            if unit and unit in name:
                suggestions.append(name)
    return False, suggestions[:8]
