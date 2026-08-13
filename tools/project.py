###
# decomp-toolkit project generator
# Generates build.ninja and objdiff.json.
#
# This generator is intentionally project-agnostic
# and shared between multiple projects. Any configuration
# specific to a project should be added to `configure.py`.
#
# If changes are made, please submit a PR to
# https://github.com/encounter/dtk-template
###

import io
import json
import math
import os
import platform
import subprocess
import sys
from pathlib import Path
from typing import (
    Any,
    Callable,
    cast,
    Dict,
    IO,
    Iterable,
    List,
    Optional,
    Set,
    Tuple,
    TypedDict,
    Union,
)

from . import ninja_syntax
from .ninja_syntax import serialize_path

if sys.platform == "cygwin":
    sys.exit(
        f"Cygwin/MSYS2 is not supported."
        f"\nPlease use native Windows Python instead."
        f"\n(Current path: {sys.executable})"
    )

Library = Dict[str, Any]


class Object:
    def __init__(self, completed: bool, name: str, **options: Any) -> None:
        self.name = name
        self.completed = completed
        self.options: Dict[str, Any] = {
            "add_to_all": None,
            "asflags": None,
            "asm_dir": None,
            "cflags": None,
            "extab_padding": None,
            "extra_asflags": [],
            "extra_cflags": [],
            "extra_clang_flags": [],
            "lib": None,
            "mw_version": None,
            "progress_category": None,
            "scratch_preset_id": None,
            "shift_jis": None,
            "source": name,
            "src_dir": None,
        }
        self.options.update(options)

        # Internal
        self.src_path: Optional[Path] = None
        self.asm_path: Optional[Path] = None
        self.src_obj_path: Optional[Path] = None
        self.asm_obj_path: Optional[Path] = None
        self.ctx_path: Optional[Path] = None

    def resolve(self, config: "ProjectConfig", lib: Library) -> "Object":
        # Use object options, then library options
        obj = Object(self.completed, self.name, **lib)
        for key, value in self.options.items():
            if value is not None or key not in obj.options:
                obj.options[key] = value

        # Use default options from config
        def set_default(key: str, value: Any) -> None:
            if obj.options[key] is None:
                obj.options[key] = value

        set_default("add_to_all", True)
        set_default("asflags", config.asflags)
        set_default("asm_dir", config.asm_dir)
        set_default("extab_padding", None)
        set_default("mw_version", config.linker_version)
        set_default("scratch_preset_id", config.scratch_preset_id)
        set_default("shift_jis", config.shift_jis)
        set_default("src_dir", config.src_dir)

        # Validate progress categories
        def check_category(category: str):
            if not any(category == c.id for c in config.progress_categories):
                sys.exit(
                    f"Progress category '{category}' missing from config.progress_categories"
                )

        progress_category = obj.options["progress_category"]
        if isinstance(progress_category, list):
            for category in progress_category:
                check_category(category)
        elif progress_category is not None:
            check_category(progress_category)

        # Resolve paths
        build_dir = config.out_path()
        obj.src_path = Path(obj.options["src_dir"]) / obj.options["source"]
        if obj.options["asm_dir"] is not None:
            obj.asm_path = (
                Path(obj.options["asm_dir"]) / obj.options["source"]
            ).with_suffix(".s")
        base_name = Path(self.name).with_suffix("")
        obj.src_obj_path = build_dir / "src" / f"{base_name}.obj"
        obj.asm_obj_path = build_dir / "mod" / f"{base_name}.obj"
        obj.ctx_path = build_dir / "src" / f"{base_name}.ctx"
        return obj


class ProgressCategory:
    def __init__(self, id: str, name: str) -> None:
        self.id = id
        self.name = name


class ProjectConfig:
    def __init__(self) -> None:
        # Paths
        self.build_dir: Path = Path("build")  # Output build files
        self.src_dir: Path = Path("src")  # C/C++/asm source files
        self.tools_dir: Path = Path("tools")  # Python scripts
        self.asm_dir: Optional[Path] = Path(
            "asm"
        )  # Override incomplete objects (for modding)

        # Tooling
        self.binutils_tag: Optional[str] = None  # Git tag
        self.binutils_path: Optional[Path] = None  # If None, download
        self.dtk_tag: Optional[str] = None  # Git tag
        self.dtk_path: Optional[Path] = None  # If None, download
        self.compilers_tag: Optional[str] = None  # 1
        self.compilers_path: Optional[Path] = None  # If None, download
        self.wibo_tag: Optional[str] = None  # Git tag
        self.wrapper: Optional[Path] = None  # If None, download wibo on Linux
        self.sjiswrap_tag: Optional[str] = None  # Git tag
        self.sjiswrap_path: Optional[Path] = None  # If None, download
        self.ninja_path: Optional[Path] = None  # If None, use system PATH
        self.objdiff_tag: Optional[str] = None  # Git tag
        self.objdiff_path: Optional[Path] = None  # If None, download

        # Project config
        self.non_matching: bool = False
        self.build_rels: bool = True  # Build REL files
        self.check_sha_path: Optional[Path] = None  # Path to version.sha1
        self.config_path: Optional[Path] = None  # Path to config.yml
        self.generate_map: bool = False  # Generate map file(s)
        self.asflags: Optional[List[str]] = None  # Assembler flags
        self.ldflags: Optional[List[str]] = None  # Linker flags
        self.libs: Optional[List[Library]] = None  # List of libraries
        self.linker_version: Optional[str] = None  # mwld version
        self.version: Optional[str] = None  # Version name
        self.warn_missing_config: bool = False  # Warn on missing unit configuration
        self.warn_missing_source: bool = False  # Warn on missing source file
        self.rel_strip_partial: bool = True  # Generate PLFs with -strip_partial
        self.rel_empty_file: Optional[str] = (
            None  # Object name for generating empty RELs
        )
        self.shift_jis = (
            True  # Convert source files from UTF-8 to Shift JIS automatically
        )
        self.reconfig_deps: Optional[List[Path]] = (
            None  # Additional re-configuration dependency files
        )
        self.custom_build_rules: Optional[List[Dict[str, Any]]] = (
            None  # Custom ninja build rules
        )
        self.custom_build_steps: Optional[Dict[str, List[Dict[str, Any]]]] = (
            None  # Custom build steps, types are ["pre-compile", "post-compile", "post-link", "post-build"]
        )
        self.generate_compile_commands: bool = (
            True  # Generate compile_commands.json for clangd
        )
        self.extra_clang_flags: List[str] = []  # Extra flags for clangd

        # Precompiled header (PCH) support
        self.pch_header: Optional[str] = None  # PCH boundary header name (e.g. "decomp_pch.h")
        self.pch_source: Optional[Path] = None  # PCH source file (e.g. Path("src/system/decomp_pch.cpp"))
        self.pch_eligible_dirs: Optional[Set[str]] = None  # Directory basenames eligible for PCH
        self.scratch_preset_id: Optional[int] = (
            None  # Default decomp.me preset ID for scratches
        )
        self.link_order_callback: Optional[Callable[[int, List[str]], List[str]]] = (
            None  # Callback to add/remove/reorder units within a module
        )

        # Progress output and report.json config
        self.progress = True  # Enable report.json generation and CLI progress output
        self.progress_modules: bool = True  # Include combined "modules" category
        self.progress_each_module: bool = (
            False  # Include individual modules, disable for large numbers of modules
        )
        self.progress_categories: List[ProgressCategory] = []  # Additional categories
        self.print_progress_categories: Union[bool, List[str]] = (
            True  # Print additional progress categories in the CLI progress output
        )
        self.progress_report_args: Optional[List[str]] = (
            None  # Flags to `objdiff-cli report generate`
        )

        # Progress fancy printing
        self.progress_use_fancy: bool = False
        self.progress_code_fancy_frac: int = 0
        self.progress_code_fancy_item: str = ""
        self.progress_data_fancy_frac: int = 0
        self.progress_data_fancy_item: str = ""

    def validate(self) -> None:
        required_attrs = [
            "build_dir",
            "src_dir",
            "tools_dir",
            "check_sha_path",
            "config_path",
            "ldflags",
            "linker_version",
            "libs",
            "version",
        ]
        for attr in required_attrs:
            if getattr(self, attr) is None:
                sys.exit(f"ProjectConfig.{attr} missing")

    # Creates a map of object names to Object instances
    # Options are fully resolved from the library and object
    def objects(self) -> Dict[str, Object]:
        out = {}
        for lib in self.libs or {}:
            objects: List[Object] = lib["objects"]
            for obj in objects:
                if obj.name in out:
                    sys.exit(f"Duplicate object name {obj.name}")
                out[obj.name] = obj.resolve(self, lib)
        return out

    # Gets the output path for build-related files.
    def out_path(self) -> Path:
        return self.build_dir / str(self.version)

    # Gets the path to the compilers directory.
    # Exits the program if neither `compilers_path` nor `compilers_tag` is provided.
    def compilers(self) -> Path:
        if self.compilers_path:
            return self.compilers_path
        elif self.compilers_tag:
            return self.build_dir / "compilers"
        else:
            sys.exit("ProjectConfig.compilers_tag missing")

    # Gets the wrapper to use for compiler commands, if set.
    def compiler_wrapper(self) -> Optional[Path]:
        wrapper = self.wrapper

        if self.use_wibo():
            wrapper = self.build_dir / "tools" / "wibo"
        if not is_windows() and wrapper is None:
            wrapper = Path("wine")

        return wrapper

    # Determines whether or not to use wibo as the compiler wrapper.
    def use_wibo(self) -> bool:
        return (
            self.wibo_tag is not None
            and sys.platform == "linux"
            and platform.machine() in ("i386", "x86_64")
            and self.wrapper is None
        )


def is_windows() -> bool:
    return os.name == "nt"


# On Windows, we need this to use && in commands
CHAIN = "cmd /c " if is_windows() else ""
# Native executable extension
EXE = ".exe" if is_windows() else ""


def file_is_asm(path: Path) -> bool:
    return path.suffix.lower() == ".s"


def file_is_c(path: Path) -> bool:
    return path.suffix.lower() == ".c"


def file_is_cpp(path: Path) -> bool:
    return path.suffix.lower() in (".cc", ".cp", ".cpp", ".cxx")


def file_is_c_cpp(path: Path) -> bool:
    return file_is_c(path) or file_is_cpp(path)


_listdir_cache = {}


def check_path_case(path: Path):
    parts = path.parts
    if path.is_absolute():
        curr = Path(parts[0])
        start = 1
    else:
        curr = Path(".")
        start = 0

    for part in parts[start:]:
        if curr in _listdir_cache:
            entries = _listdir_cache[curr]
        else:
            try:
                entries = os.listdir(curr)
            except (FileNotFoundError, PermissionError):
                sys.exit(f"Cannot access: {curr}")
            _listdir_cache[curr] = entries

        for entry in entries:
            if entry.lower() == part.lower():
                curr = curr / entry
                break
        else:
            sys.exit(f"Cannot resolve: {path}")

    if path != curr:
        print(f"⚠️  Case mismatch: expected={path} actual={curr}")


def shell_quote_flag(flag: str) -> str:
    """Keep a backslash in a flag alive through /bin/sh.

    The msvc rules interpolate `$cflags` straight into a shell command line, so
    an unquoted `/I e:\\lazer_build_gmc1\\system\\src` reaches cl.exe as
    `e:lazer_build_gmc1systemsrc` -- sh eats every backslash. That separator is
    load-bearing: MSVC hashes a string literal's CONTENTS into its COMDAT name,
    so `__FILE__` spelled with `/` where the retail build spelled `\\` gives
    byte-identical code a different relocation target.

    Quoting happens HERE, once, at the seam where a flag list becomes a shell
    string. Every Python consumer of the flag lists (compile_commands, the
    decompctx include set, tools/compiler_trace) keeps seeing the plain
    spelling, which is what they want to reason about.
    """
    if "\\" not in flag:
        return flag
    head, sep, tail = flag.partition(" ")
    if not sep:
        return f"'{flag}'"
    return f"{head} '{tail}'"


def make_flags_str(flags: Optional[List[str]]) -> str:
    if flags is None:
        return ""
    return " ".join(shell_quote_flag(f) for f in flags)


# Unit configuration
class BuildConfigUnit(TypedDict):
    object: Optional[str]
    name: str
    autogenerated: bool


# Module configuration
class BuildConfigModule(TypedDict):
    name: str
    module_id: int
    ldscript: str
    entry: str
    units: List[BuildConfigUnit]


# Module link configuration
class BuildConfigLink(TypedDict):
    modules: List[str]


# Build configuration generated by decomp-toolkit
class BuildConfig(BuildConfigModule):
    version: str
    modules: List[BuildConfigModule]
    links: List[BuildConfigLink]


# Load decomp-toolkit generated config.json
def load_build_config(
    config: ProjectConfig, build_config_path: Path
) -> Optional[BuildConfig]:
    if not build_config_path.is_file():
        return None

    def versiontuple(v: str) -> Tuple[int, ...]:
        return tuple(map(int, (v.split("."))))

    f = open(build_config_path, "r", encoding="utf-8")
    build_config: BuildConfig = json.load(f)
    config_version = build_config.get("version")
    if config_version is None:
        print("Invalid config.json, regenerating...")
        f.close()
        os.remove(build_config_path)
        return None

    dtk_version = str(config.dtk_tag)[1:]  # Strip v
    if versiontuple(config_version) < versiontuple(dtk_version):
        print("Outdated config.json, regenerating...")
        f.close()
        os.remove(build_config_path)
        return None

    f.close()

    # Apply link order callback
    if config.link_order_callback:
        modules: List[BuildConfigModule] = [build_config, *build_config["modules"]]
        for module in modules:
            unit_names = list(map(lambda u: u["name"], module["units"]))
            unit_names = config.link_order_callback(module["module_id"], unit_names)
            units: List[BuildConfigUnit] = []
            for unit_name in unit_names:
                units.append(
                    # Find existing unit or create a new one
                    next(
                        (u for u in module["units"] if u["name"] == unit_name),
                        {"object": None, "name": unit_name, "autogenerated": False},
                    )
                )
            module["units"] = units

    return build_config


# Generate build.ninja, objdiff.json and compile_commands.json
def generate_build(config: ProjectConfig) -> None:
    config.validate()
    objects = config.objects()
    build_config = load_build_config(config, config.out_path() / "config.json")
    generate_build_ninja(config, objects, build_config)
    generate_objdiff_config(config, objects, build_config)
    generate_compile_commands(config, objects, build_config)


# Generate build.ninja
def generate_build_ninja(
    config: ProjectConfig,
    objects: Dict[str, Object],
    build_config: Optional[BuildConfig],
) -> None:
    out = io.StringIO()
    n = ninja_syntax.Writer(out)
    n.variable("ninja_required_version", "1.3")
    n.newline()

    configure_script = Path(os.path.relpath(os.path.abspath(sys.argv[0])))
    python_lib = Path(os.path.relpath(__file__))
    python_lib_dir = python_lib.parent
    n.comment("The arguments passed to configure.py, for rerunning it.")
    n.variable("configure_args", [f'"\"{arg}\""' if ' ' in arg else arg for arg in sys.argv[1:]])
    # for arg in sys.argv[1:] if arg.contains(' ') wrap in quotes else arg
    n.variable("python", f'"{sys.executable}"')
    n.newline()

    ###
    # Variables
    ###
    n.comment("Variables")
    # n.variable("ldflags", make_flags_str(config.ldflags))
    # if config.linker_version is None:
    #     sys.exit("ProjectConfig.linker_version missing")
    n.variable("mw_version", Path(config.linker_version))
    n.variable("objdiff_report_args", make_flags_str(config.progress_report_args))
    if config.wibo_path_map:
        n.variable("wibo_path_map", config.wibo_path_map)
    n.newline()

    ###
    # Tooling
    ###
    n.comment("Tooling")

    build_path = config.out_path()
    report_path = build_path / "report.json"
    raw_report_path = build_path / "report_raw.json"
    # Stamp for the build-safe report.json -> orchestrator DB metadata sync.
    db_sync_stamp = build_path / "report_db_synced.stamp"
    # The synthetic ICF-alias map objdiff.json's `map_file` points at, its
    # generator, and its source of truth. Rendered both here at configure time
    # (so a fresh tree's first objdiff.json can reference it) and by the
    # `icf_alias_map` ninja edge; see the design comment on that edge.
    icf_gen_script = Path("scripts") / "gen_icf_alias_map.py"
    icf_aliases_json = Path("scripts") / "symbol_aliases.json"
    icf_map_path = build_path / "icf_aliases.map"
    icf_map_checked = build_path / "icf_aliases_checked.stamp"
    icf_map_purged = build_path / "icf_aliases_cache_purged.stamp"
    build_tools_path = config.build_dir / "tools"
    download_tool = config.tools_dir / "download_tool.py"
    n.rule(
        name="download_tool",
        command=f"$python {download_tool} $tool $out --tag $tag",
        description="TOOL $out",
        restat=True,
    )

    decompctx = config.tools_dir / "decompctx.py"
    n.rule(
        name="decompctx",
        command=f"$python {decompctx} $in -o $out -d $out.d $includes",
        description="CTX $in",
        depfile="$out.d",
        deps="gcc",
    )

    cargo_rule_written = False

    def write_cargo_rule():
        nonlocal cargo_rule_written
        if not cargo_rule_written:
            n.pool("cargo", 1)
            # NO depfile on purpose. Cargo emits a depfile whose TARGET line
            # is an absolute path (e.g. "/home/.../build/tools/release/dtk:")
            # while ninja's build edge declares the output with a relative
            # path, so ninja rejects the depfile and treats the tool as
            # perpetually dirty -- CARGO re-fires on every ninja pass and can
            # cascade into a full re-SPLIT + reconfigure. (Bug found live in
            # rb3-xenon, 2026-06-30; fixed identically there.)
            # Ninja therefore tracks the tool only via its explicit input
            # (Cargo.toml) + implicit input (Cargo.lock). TRADE-OFF: edits to
            # the tool's *Rust sources* are NOT auto-detected -- after
            # changing them, force the rebuild with e.g.
            # `touch ../jeff/Cargo.toml && ninja` (dtk) or
            # `touch ../objdiff/Cargo.toml && ninja` (objdiff-cli).
            n.rule(
                name="cargo",
                command="cargo build --release --manifest-path $in --bin $bin --target-dir $target",
                description="CARGO $bin",
                pool="cargo",
                # No deps="gcc" either -- ninja's binary deps cache is unsafe
                # under concurrent ninja invocations.
                restat=True,
            )
            cargo_rule_written = True

    if config.dtk_path is not None and config.dtk_path.is_file():
        dtk = config.dtk_path
    elif config.dtk_path is not None:
        dtk = build_tools_path / "release" / f"dtk{EXE}"
        write_cargo_rule()
        n.build(
            outputs=dtk,
            rule="cargo",
            inputs=config.dtk_path / "Cargo.toml",
            implicit=config.dtk_path / "Cargo.lock",
            variables={
                "bin": "dtk",
                "target": build_tools_path,
            },
        )
    elif config.dtk_tag:
        dtk = build_tools_path / f"dtk{EXE}"
        n.build(
            outputs=dtk,
            rule="download_tool",
            implicit=download_tool,
            variables={
                "tool": "dtk",
                "tag": config.dtk_tag,
            },
        )
    else:
        sys.exit("ProjectConfig.dtk_tag missing")

    if config.objdiff_path is not None and config.objdiff_path.is_file():
        objdiff = config.objdiff_path
    elif config.objdiff_path is not None:
        objdiff = build_tools_path / "release" / f"objdiff-cli{EXE}"
        write_cargo_rule()
        n.build(
            outputs=objdiff,
            rule="cargo",
            inputs=config.objdiff_path / "Cargo.toml",
            implicit=config.objdiff_path / "Cargo.lock",
            variables={
                "bin": "objdiff-cli",
                "target": build_tools_path,
            },
        )
    elif config.objdiff_tag:
        objdiff = build_tools_path / f"objdiff-cli{EXE}"
        n.build(
            outputs=objdiff,
            rule="download_tool",
            implicit=download_tool,
            variables={
                "tool": "objdiff-cli",
                "tag": config.objdiff_tag,
            },
        )
    else:
        sys.exit("ProjectConfig.objdiff_tag missing")

    if config.sjiswrap_path:
        sjiswrap = config.sjiswrap_path
    elif config.sjiswrap_tag:
        sjiswrap = build_tools_path / "sjiswrap.exe"
        n.build(
            outputs=sjiswrap,
            rule="download_tool",
            implicit=download_tool,
            variables={
                "tool": "sjiswrap",
                "tag": config.sjiswrap_tag,
            },
        )
    else:
        sys.exit("ProjectConfig.sjiswrap_tag missing")

    wrapper = config.compiler_wrapper()
    # Only add an implicit dependency on wibo if we download it
    wrapper_implicit: Optional[Path] = None
    if wrapper is not None and config.use_wibo():
        wrapper_implicit = wrapper
        n.build(
            outputs=wrapper,
            rule="download_tool",
            implicit=download_tool,
            variables={
                "tool": "wibo",
                "tag": config.wibo_tag,
            },
        )
    wrapper_cmd = f"{wrapper} " if wrapper else ""

    compilers = config.compilers()
    compilers_implicit: Optional[Path] = None
    if config.compilers_path is None and config.compilers_tag is not None:
        compilers_implicit = compilers
        n.build(
            outputs=compilers,
            rule="download_tool",
            implicit=download_tool,
            variables={
                "tool": "compilers",
                "tag": config.compilers_tag,
            },
        )

    binutils_implicit = None
    if config.binutils_path:
        binutils = config.binutils_path
    elif config.binutils_tag:
        binutils = config.build_dir / "binutils"
        binutils_implicit = binutils
        n.build(
            outputs=binutils,
            rule="download_tool",
            implicit=download_tool,
            variables={
                "tool": "binutils",
                "tag": config.binutils_tag,
            },
        )
    else:
        sys.exit("ProjectConfig.binutils_tag missing")

    n.newline()

    ###
    # Helper rule for downloading all tools
    ###
    n.comment("Download all tools")
    n.build(
        outputs="tools",
        rule="phony",
        inputs=[dtk, sjiswrap, wrapper, compilers, binutils, objdiff],
    )
    n.newline()

    ###
    # Build rules
    ###
    compiler_path = compilers / "$mw_version"

    transform_dep: Optional[Path] = None

    # MWCC
    mwcc = compiler_path / "cl.exe"
    mwcc_cmd = f"{wrapper_cmd}{mwcc} $cflags"
    mwcc_implicit: List[Optional[Path]] = [compilers_implicit or mwcc, wrapper_implicit]

    # MWCC with UTF-8 to Shift JIS wrapper
    mwcc_sjis_cmd = f"{wrapper_cmd}{sjiswrap} {mwcc} $cflags -MMD -c $in -o $basedir"
    mwcc_sjis_implicit: List[Optional[Path]] = [*mwcc_implicit, sjiswrap]

    # MWCC with extab post-processing
    mwcc_extab_cmd = f"{CHAIN}{mwcc_cmd} && {dtk} extab clean --padding \"$extab_padding\" $out $out"
    mwcc_extab_implicit: List[Optional[Path]] = [*mwcc_implicit, dtk]
    mwcc_sjis_extab_cmd = f"{CHAIN}{mwcc_sjis_cmd} && {dtk} extab clean --padding \"$extab_padding\" $out $out"
    mwcc_sjis_extab_implicit: List[Optional[Path]] = [*mwcc_sjis_implicit, dtk]

    # MWLD
    mwld = compiler_path / "mwldeppc.exe"
    mwld_cmd = f"{wrapper_cmd}{mwld} $ldflags -o $out @$out.rsp"
    mwld_implicit: List[Optional[Path]] = [compilers_implicit or mwld, wrapper_implicit]

    # GNU as
    gnu_as = binutils / f"powerpc-eabi-as{EXE}"
    gnu_as_cmd = (
        f"{CHAIN}{gnu_as} $asflags -o $out $in" + f" && {dtk} elf fixup $out $out"
    )
    gnu_as_implicit = [binutils_implicit or gnu_as, dtk]
    # As a workaround for https://github.com/encounter/dtk-template/issues/51
    # include macros.inc directly as an implicit dependency
    gnu_as_implicit.append(build_path / "include" / "macros.inc")

    if os.name != "nt":
        transform_dep = config.tools_dir / "transform_dep.py"
        mwcc_implicit.append(transform_dep)
        mwcc_sjis_implicit.append(transform_dep)
        mwcc_extab_implicit.append(transform_dep)
        mwcc_sjis_extab_implicit.append(transform_dep)


    # n.comment("Link ELF file")
    # n.rule(
    #     name="link",
    #     command=mwld_cmd,
    #     description="LINK $out",
    #     rspfile="$out.rsp",
    #     rspfile_content="$in_newline",
    # )
    # n.newline()

    # n.comment("Generate DOL")
    # n.rule(
    #     name="elf2dol",
    #     command=f"{dtk} elf2dol $in $out",
    #     description="DOL $out",
    # )
    # n.newline()

    # X360 MSVC Link
    msvc_link = compiler_path / "link.exe"
    msvc_link_cmd = f"{wrapper_cmd}{msvc_link} /NOLOGO @$out.rsp"
    msvc_link_implicit: List[Optional[Path]] = [compilers_implicit or msvc_link]

    n.comment("X360 MSVC Link")
    n.rule(
        name="msvc_link",
        command=msvc_link_cmd,
        description="LINK $out",
        rspfile="$out.rsp",
        rspfile_content="$in_newline $ldflags",
    )
    n.newline()

    # MSVC - use absolute paths since command cds into source dir for __FILE__
    msvc = compiler_path / "cl.exe"
    msvc_abs = compilers.resolve() / "$mw_version" / "cl.exe"
    # Prefer config.wrapper (custom wibo with env var support) over
    # compiler_wrapper() which may return the stock downloaded wibo
    wrapper_for_msvc = config.wrapper if config.wrapper else wrapper
    wrapper_abs = str(wrapper_for_msvc.resolve()) if wrapper_for_msvc else ""
    wrapper_cmd_msvc = f"{wrapper_abs} " if wrapper_abs else ""
    wibo_env = "WIBO_COMPUTER_NAME='9QVZU3' WIBO_FS_CACHE='1' "
    msvc_cmd = f"cd $in_dir && {wrapper_cmd_msvc}{wibo_env}{msvc_abs} $cflags /Fo$abs_out $in_win"
    if config.wibo_path_map:
        msvc_cmd = f"cd $in_dir && {wrapper_cmd_msvc}{wibo_env}WIBO_PATH_MAP='$wibo_path_map' {msvc_abs} $cflags /Fo$abs_out $in_win"

    # Add /showIncludes + WIBO_REWRITE_SHOWINCLUDES for header dependency tracking.
    # Wibo rewrites "Note: including file:" paths using WIBO_PATH_MAP so ninja's
    # deps=msvc can track host filesystem paths. Zero extra process spawns.
    msvc_deps = None
    if config.wibo_path_map:
        msvc_cmd_with_deps = msvc_cmd.replace(
            wibo_env,
            wibo_env + "WIBO_REWRITE_SHOWINCLUDES='1' ",
        ).replace("$cflags", "/showIncludes $cflags")
        msvc_deps = "msvc"
    else:
        msvc_cmd_with_deps = msvc_cmd

    n.comment("MSVC build")
    n.rule(
        name="msvc",
        command=msvc_cmd_with_deps,
        description="MSVC $out",
        deps=msvc_deps,
    )
    n.newline()

    # MSVC PCH create rule: compiles the PCH source and produces the .pch file
    msvc_pch_create_cmd = msvc_cmd_with_deps.replace(
        "$cflags /Fo$abs_out $in_win",
        '/Yc"decomp_pch.h" /Fp$pch_out $cflags /Fo$abs_out $in_win',
    )
    n.comment("MSVC PCH create")
    n.rule(
        name="msvc_pch_create",
        command=msvc_pch_create_cmd,
        description="PCH $pch_out",
        deps=msvc_deps,
    )
    n.newline()

    # MSVC PCH use rule: compiles with precompiled header
    msvc_pch_cmd = msvc_cmd_with_deps.replace(
        "$cflags /Fo$abs_out $in_win",
        '/Yu"decomp_pch.h" /FI"decomp_pch.h" /Fp$pch_file $cflags /Fo$abs_out $in_win',
    )
    n.comment("MSVC build with PCH")
    n.rule(
        name="msvc_pch",
        command=msvc_pch_cmd,
        description="MSVC $out",
        deps=msvc_deps,
    )
    n.newline()

    # n.comment("MWCC build (with UTF-8 to Shift JIS wrapper)")
    # n.rule(
    #     name="mwcc_sjis",
    #     command=mwcc_sjis_cmd,
    #     description="MWCC $out",
    #     depfile="$basefile.d",
    #     deps="gcc",
    # )
    # n.newline()

    # n.comment("MWCC build (with extab post-processing)")
    # n.rule(
    #     name="mwcc_extab",
    #     command=mwcc_extab_cmd,
    #     description="MWCC $out",
    #     depfile="$basefile.d",
    #     deps="gcc",
    # )
    # n.newline()

    # n.comment("MWCC build (with UTF-8 to Shift JIS wrapper and extab post-processing)")
    # n.rule(
    #     name="mwcc_sjis_extab",
    #     command=mwcc_sjis_extab_cmd,
    #     description="MWCC $out",
    #     depfile="$basefile.d",
    #     deps="gcc",
    # )

    # n.comment("Assemble asm")
    # n.rule(
    #     name="as",
    #     command=gnu_as_cmd,
    #     description="AS $out",
    #     # See https://github.com/encounter/dtk-template/issues/51
    #     # depfile="$out.d",
    #     # deps="gcc",
    # )
    # n.newline()

    if len(config.custom_build_rules or {}) > 0:
        n.comment("Custom project build rules (pre/post-processing)")
    for rule in config.custom_build_rules or {}:
        n.rule(
            name=cast(str, rule.get("name")),
            command=cast(str, rule.get("command")),
            description=rule.get("description", None),
            depfile=rule.get("depfile", None),
            generator=rule.get("generator", False),
            pool=rule.get("pool", None),
            restat=rule.get("restat", False),
            rspfile=rule.get("rspfile", None),
            rspfile_content=rule.get("rspfile_content", None),
            deps=rule.get("deps", None),
        )
        n.newline()

    def write_custom_step(step: str, prev_step: Optional[str] = None) -> None:
        implicit: List[str | Path] = []
        if config.custom_build_steps and step in config.custom_build_steps:
            n.comment(f"Custom build steps ({step})")
            for custom_step in config.custom_build_steps[step]:
                outputs = cast(List[str | Path], custom_step.get("outputs"))

                if isinstance(outputs, list):
                    implicit.extend(outputs)
                else:
                    implicit.append(outputs)

                n.build(
                    outputs=outputs,
                    rule=cast(str, custom_step.get("rule")),
                    inputs=custom_step.get("inputs", None),
                    implicit=custom_step.get("implicit", None),
                    order_only=custom_step.get("order_only", None),
                    variables=custom_step.get("variables", None),
                    implicit_outputs=custom_step.get("implicit_outputs", None),
                    pool=custom_step.get("pool", None),
                    dyndep=custom_step.get("dyndep", None),
                )
                n.newline()
        n.build(
            outputs=step,
            rule="phony",
            inputs=implicit,
            order_only=prev_step,
        )

    # Add all build steps needed before we compile (e.g. processing assets)
    write_custom_step("pre-compile")

    ###
    # PCH build edge
    ###
    pch_path: Optional[Path] = None
    if config.pch_source and config.pch_header:
        pch_dir = build_path / "pch"
        pch_path = pch_dir / "system.pch"
        pch_obj = pch_dir / "decomp_pch.obj"
        pch_src = config.pch_source
        pch_src_abs = pch_src.resolve()

        # Get engine cflags for the PCH compilation
        # Use the first lib's cflags (engine) since PCH covers engine code
        pch_cflags_str = ""
        if config.libs:
            for lib_cfg in config.libs:
                if lib_cfg["lib"] == "system":
                    pch_cflags_str = make_flags_str(lib_cfg["cflags"])
                    break
            if not pch_cflags_str and config.libs:
                pch_cflags_str = make_flags_str(config.libs[0]["cflags"])

        # Absolutize relative /I paths
        project_root = str(Path.cwd())
        def absolutize_pch_include(flag):
            if flag.startswith("/I "):
                inc = flag[3:]
                if ":" not in inc and not inc.startswith("/"):
                    return f"/I {project_root}/{inc}"
            elif flag.startswith("/I"):
                inc = flag[2:]
                if ":" not in inc and not inc.startswith("/"):
                    return f"/I{project_root}/{inc}"
            return flag

        if config.libs:
            for lib_cfg in config.libs:
                if lib_cfg["lib"] == "system":
                    pch_cflags_list = [absolutize_pch_include(f) for f in lib_cfg["cflags"]]
                    # Add /TP for C++ mode
                    pch_cflags_list.insert(0, "/TP")
                    pch_cflags_str = make_flags_str(pch_cflags_list)
                    break

        n.comment("Precompiled header")
        pch_implicit = [compilers_implicit or msvc, wrapper_implicit]
        if transform_dep is not None:
            pch_implicit.append(transform_dep)
        n.build(
            outputs=[pch_obj],
            rule="msvc_pch_create",
            inputs=pch_src,
            implicit=pch_implicit,
            implicit_outputs=[pch_path],
            variables={
                "mw_version": Path(config.linker_version),
                "cflags": pch_cflags_str,
                "in_win": pch_src.name,
                "in_dir": str(pch_src_abs.parent),
                "abs_out": str(pch_obj.resolve()),
                "pch_out": str(pch_path.resolve()),
            },
            order_only="pre-compile",
        )
        n.newline()

    ###
    # Source files
    ###
    n.comment("Source files")

    def map_path(path: Path) -> Path:
        return path.parent / (path.name + ".MAP")

    class LinkStep:
        def __init__(self, config: BuildConfigModule) -> None:
            self.name = config["name"]
            self.module_id = config["module_id"]
            self.ldscript: Optional[Path] = Path(config["ldscript"])
            self.entry = config["entry"]
            self.inputs: List[str] = []

        def add(self, obj: Path) -> None:
            self.inputs.append(serialize_path(obj))

        def output(self) -> Path:
            if self.module_id == 0:
                return build_path / f"{self.name}.dol"
            else:
                return build_path / self.name / f"{self.name}.rel"

        def partial_output(self) -> Path:
            if self.module_id == 0:
                return build_path / f"{self.name}.elf"
            else:
                return build_path / self.name / f"{self.name}.plf"

        def write(self, n: ninja_syntax.Writer) -> None:
            n.comment(f"Link {self.name}")
            if self.module_id == 0:
                elf_path = build_path / f"{self.name}.elf"
                elf_ldflags = f"$ldflags -lcf {serialize_path(self.ldscript)}"
                if config.generate_map:
                    elf_map = map_path(elf_path)
                    elf_ldflags += f" -map {serialize_path(elf_map)}"
                else:
                    elf_map = None
                n.build(
                    outputs=elf_path,
                    rule="link",
                    inputs=self.inputs,
                    implicit=[
                        self.ldscript,
                        *mwld_implicit,
                    ],
                    implicit_outputs=elf_map,
                    variables={"ldflags": elf_ldflags},
                    order_only="post-compile",
                )
            else:
                preplf_path = build_path / self.name / f"{self.name}.preplf"
                plf_path = build_path / self.name / f"{self.name}.plf"
                preplf_ldflags = "$ldflags -sdata 0 -sdata2 0 -r"
                plf_ldflags = f"$ldflags -sdata 0 -sdata2 0 -r1 -lcf {serialize_path(self.ldscript)}"
                if self.entry:
                    plf_ldflags += f" -m {self.entry}"
                    # -strip_partial is only valid with -m
                    if config.rel_strip_partial:
                        plf_ldflags += " -strip_partial"
                if config.generate_map:
                    preplf_map = map_path(preplf_path)
                    preplf_ldflags += f" -map {serialize_path(preplf_map)}"
                    plf_map = map_path(plf_path)
                    plf_ldflags += f" -map {serialize_path(plf_map)}"
                else:
                    preplf_map = None
                    plf_map = None
                n.build(
                    outputs=preplf_path,
                    rule="link",
                    inputs=self.inputs,
                    implicit=mwld_implicit,
                    implicit_outputs=preplf_map,
                    variables={"ldflags": preplf_ldflags},
                    order_only="post-compile",
                )
                n.build(
                    outputs=plf_path,
                    rule="link",
                    inputs=self.inputs,
                    implicit=[self.ldscript, preplf_path, *mwld_implicit],
                    implicit_outputs=plf_map,
                    variables={"ldflags": plf_ldflags},
                    order_only="post-compile",
                )
            n.newline()

    class X360LinkStep:
        def __init__(self, build_cfg: BuildConfigModule) -> None:
            self.name = build_cfg["name"]
            self.entry = build_cfg["entry"]
            self.inputs: List[str] = []
            self.extra_implicit: List[Path] = []

        def add(self, obj: Path) -> None:
            self.inputs.append(serialize_path(obj))

        def output(self) -> Path:
            return build_path / f"{self.name}.exe"

        def write(self, n: ninja_syntax.Writer) -> None:
            n.comment(f"Link {self.name}")
            exe_path = self.output()
            ldflags_str = make_flags_str(config.ldflags)
            ldflags_str += f" /ENTRY:{self.entry}"
            ldflags_str += f" /OUT:{serialize_path(exe_path)}"
            if config.generate_map:
                ldflags_str += f" /MAP:{serialize_path(map_path(exe_path))}"
            n.build(
                outputs=exe_path,
                rule="msvc_link",
                inputs=self.inputs,
                implicit=[*msvc_link_implicit, *self.extra_implicit],
                variables={"ldflags": ldflags_str},
                order_only="post-compile",
            )
            n.newline()

    link_outputs: List[Path] = []
    if build_config:
        link_steps: List[LinkStep] = []
        used_compiler_versions: Set[str] = set()
        source_inputs: List[Path] = []
        source_added: Set[Path] = set()

        def c_build(obj: Object, src_path: Path) -> Optional[Path]:
            # Avoid creating duplicate build rules
            if obj.src_obj_path is None or obj.src_obj_path in source_added:
                return obj.src_obj_path
            source_added.add(obj.src_obj_path)

            cflags = obj.options["cflags"]
            extra_cflags = obj.options["extra_cflags"]

            # Add appropriate language flag if it doesn't exist already
            # Added directly to the source so it flows to other generation tasks
            def is_lang_flag(flag):
                return flag.startswith("-lang") or flag in ("/TP", "/TC", "/Tp", "/Tc")

            if not any(is_lang_flag(flag) for flag in cflags) and not any(
                is_lang_flag(flag) for flag in extra_cflags
            ):
                # Ensure extra_cflags is a unique instance,
                # and insert into there to avoid modifying shared sets of flags
                extra_cflags = obj.options["extra_cflags"] = list(extra_cflags)
                if file_is_cpp(src_path):
                    extra_cflags.insert(0, "/TP")
                else:
                    extra_cflags.insert(0, "/TC")

            all_cflags = cflags + extra_cflags

            # Absolutize relative /I paths so they resolve correctly
            # after cd $in_dir (needed for __FILE__ basename fix)
            project_root = str(Path.cwd())
            def absolutize_include(flag):
                if flag.startswith("/I "):
                    inc = flag[3:]
                    if ":" not in inc and not inc.startswith("/"):
                        return f"/I {project_root}/{inc}"
                elif flag.startswith("/I"):
                    inc = flag[2:]
                    if ":" not in inc and not inc.startswith("/"):
                        return f"/I{project_root}/{inc}"
                return flag
            all_cflags = [absolutize_include(f) for f in all_cflags]

            cflags_str = make_flags_str(all_cflags)
            used_compiler_versions.add(obj.options["mw_version"])

            def get_win_path(path: Path) -> str:
                if not config.wibo_path_map:
                    return str(path)
                abs_path = str(path.absolute())
                for mapping in config.wibo_path_map.split(";"):
                    if "=" not in mapping:
                        continue
                    win_part, host_part = mapping.split("=", 1)
                    host_abs = str(Path(host_part).absolute())
                    if abs_path.startswith(host_abs):
                        rel = abs_path[len(host_abs) :].lstrip("/")
                        return (win_part.rstrip("\\/") + "/" + rel).replace("\\", "/")
                return str(path)

            # Add MSVC build rule
            lib_name = obj.options["lib"]
            build_rule = "msvc"
            build_implcit = mwcc_implicit
            src_abs = src_path.resolve()
            variables = {
                "mw_version": Path(obj.options["mw_version"]),
                "cflags": cflags_str,
                "basedir": os.path.dirname(obj.src_obj_path),
                "basefile": obj.src_obj_path.with_suffix(""),
                "in_win": src_path.name,
                "in_dir": str(src_abs.parent),
                "abs_out": str(Path(obj.src_obj_path).resolve()),
            }

            if obj.options["shift_jis"] and obj.options["extab_padding"] is not None:
                build_rule = "mwcc_sjis_extab"
                build_implcit = mwcc_sjis_extab_implicit
                variables["extab_padding"] = "".join(f"{i:02x}" for i in obj.options["extab_padding"])
            elif obj.options["shift_jis"]:
                build_rule = "mwcc_sjis"
                build_implcit = mwcc_sjis_implicit
            elif obj.options["extab_padding"] is not None:
                build_rule = "mwcc_extab"
                build_implcit = mwcc_extab_implicit
                variables["extab_padding"] = "".join(f"{i:02x}" for i in obj.options["extab_padding"])

            # Use PCH for eligible files (must be on plain msvc rule, C++ mode, in eligible dir)
            pch_implicit: List[Optional[Path]] = []
            if (
                pch_path is not None
                and build_rule == "msvc"
                and file_is_cpp(src_path)
                and "/TC" not in all_cflags
                and config.pch_eligible_dirs
            ):
                src_dir_name = src_path.parent.name
                if src_dir_name in config.pch_eligible_dirs:
                    build_rule = "msvc_pch"
                    variables["pch_file"] = str(pch_path.resolve())
                    # msvc_pch builds need the .pch binary as an input
                    pch_implicit = [pch_path]

            n.comment(f"{obj.name}: {lib_name} (linked {obj.completed})")
            n.build(
                outputs=obj.src_obj_path,
                rule=build_rule,
                inputs=src_path,
                variables=variables,
                implicit=[*build_implcit, *pch_implicit],
                order_only="pre-compile",
            )

            # Add ctx build rule
            if obj.ctx_path is not None:
                include_dirs = []
                for flag in all_cflags:
                    if (
                        flag.startswith("-i ")
                        or flag.startswith("-I ")
                        or flag.startswith("-I+")
                    ):
                        include_dirs.append(flag[3:])
                    elif flag.startswith("/I"):
                        include_dirs.append(flag[2:].lstrip())
                # Also a shell command line, so the same backslash rule applies.
                includes = " ".join(shell_quote_flag(f"-I {d}")
                                    for d in include_dirs)
                n.build(
                    outputs=obj.ctx_path,
                    rule="decompctx",
                    inputs=src_path,
                    implicit=decompctx,
                    variables={"includes": includes},
                )
            n.newline()

            if obj.options["add_to_all"]:
                source_inputs.append(obj.src_obj_path)

            return obj.src_obj_path

        def asm_build(
            obj: Object, src_path: Path, obj_path: Optional[Path]
        ) -> Optional[Path]:
            if obj.options["asflags"] is None:
                sys.exit("ProjectConfig.asflags missing")
            asflags_str = make_flags_str(obj.options["asflags"])
            if len(obj.options["extra_asflags"]) > 0:
                extra_asflags_str = make_flags_str(obj.options["extra_asflags"])
                asflags_str += " " + extra_asflags_str

            # Avoid creating duplicate build rules
            if obj_path is None or obj_path in source_added:
                return obj_path
            source_added.add(obj_path)

            # Add assembler build rule
            lib_name = obj.options["lib"]
            n.comment(f"{obj.name}: {lib_name} (linked {obj.completed})")

            def get_win_path(path: Path) -> str:
                if not config.wibo_path_map:
                    return str(path)
                abs_path = str(path.absolute())
                for mapping in config.wibo_path_map.split(";"):
                    if "=" not in mapping:
                        continue
                    win_part, host_part = mapping.split("=", 1)
                    host_abs = str(Path(host_part).absolute())
                    if abs_path.startswith(host_abs):
                        rel = abs_path[len(host_abs) :].lstrip("/")
                        return (win_part.rstrip("\\/") + "/" + rel).replace("\\", "/")
                return str(path)

            n.build(
                outputs=obj_path,
                rule="as",
                inputs=src_path,
                variables={"asflags": asflags_str, "in_win": get_win_path(src_path)},
                implicit=gnu_as_implicit,
                order_only="pre-compile",
            )
            n.newline()

            if obj.options["add_to_all"]:
                source_inputs.append(obj_path)

            return obj_path

        def add_unit(build_obj: BuildConfigUnit, link_step: LinkStep):
            obj_path, obj_name = build_obj["object"], build_obj["name"]
            obj = objects.get(obj_name)
            if obj is None:
                if config.warn_missing_config and not build_obj["autogenerated"]:
                    print(f"Missing configuration for {obj_name}")
                if obj_path is not None:
                    link_step.add(Path(obj_path))
                return

            link_built_obj = obj.completed
            built_obj_path: Optional[Path] = None
            if obj.src_path is not None and obj.src_path.exists():
                check_path_case(obj.src_path)
                if file_is_c_cpp(obj.src_path):
                    # Add C/C++ build rule
                    built_obj_path = c_build(obj, obj.src_path)
                elif file_is_asm(obj.src_path):
                    # Add assembler build rule
                    built_obj_path = asm_build(obj, obj.src_path, obj.src_obj_path)
                else:
                    sys.exit(f"Unknown source file type {obj.src_path}")
            else:
                if config.warn_missing_source or obj.completed:
                    print(f"Missing source file {obj.src_path}")
                link_built_obj = False

            # Assembly overrides
            if (
                not link_built_obj
                and obj.asm_path is not None
                and obj.asm_path.exists()
            ):
                check_path_case(obj.asm_path)
                link_built_obj = True
                built_obj_path = asm_build(obj, obj.asm_path, obj.asm_obj_path)

            if link_built_obj and built_obj_path is not None:
                # Use the source-built object only — do not also link the
                # split object.  Linking both causes /FORCE:MULTIPLE overlap
                # where split function bodies occupy the same address range as
                # decomp functions, corrupting guest-memory patches at runtime.
                link_step.add(built_obj_path)
                # Also link the data-stub .obj if it exists.  This provides
                # lbl_* data symbol exports that other split .objs reference.
                # Data stubs contain only data sections (no code) from the
                # split .obj, so they don't conflict with decomp code.
                if obj_path is not None:
                    data_stub = Path(str(obj_path).replace("/obj/", "/data/", 1))
                    if data_stub.exists():
                        link_step.add(data_stub)
            elif obj_path is not None:
                # Use the original (extracted) object
                link_step.add(Path(obj_path))

        # Add link steps
        link_step = LinkStep(build_config)
        x360_link_step = X360LinkStep(build_config)
        for unit in build_config["units"]:
            add_unit(unit, link_step)
            # Also add to X360 link step (uses same hybrid selection)
            x360_link_step.inputs = link_step.inputs.copy()
        link_steps.append(link_step)

        if config.build_rels:
            # Add REL link steps
            for module in build_config["modules"]:
                module_link_step = LinkStep(module)
                for unit in module["units"]:
                    add_unit(unit, module_link_step)
                # Add empty object to empty RELs
                if len(module_link_step.inputs) == 0:
                    if config.rel_empty_file is None:
                        sys.exit("ProjectConfig.rel_empty_file missing")
                    add_unit(
                        {
                            "object": None,
                            "name": config.rel_empty_file,
                            "autogenerated": True,
                        },
                        module_link_step,
                    )
                link_steps.append(module_link_step)
        n.newline()

        # Check if all compiler versions exist
        for mw_version in used_compiler_versions:
            msvc_path = compilers / mw_version / "cl.exe"
            if config.compilers_path and not os.path.exists(msvc_path):
                sys.exit(f"Compiler {msvc_path} does not exist")

        # Check if linker exists
        msvc_path = compilers / str(config.linker_version) / "link.exe"
        if config.compilers_path and not os.path.exists(msvc_path):
            sys.exit(f"Linker {msvc_path} does not exist")

        # Add all build steps needed before we link and after compiling objects
        write_custom_step("post-compile", "pre-compile")

        ###
        # Link (X360)
        ###
        x360_link_step.write(n)
        link_outputs.append(x360_link_step.output())

        # Add all build steps needed after linking and before GC/Wii native format generation
        write_custom_step("post-link", "post-compile")

        ###
        # Generate DOL
        ###
        # n.build(
        #     outputs=link_steps[0].output(),
        #     rule="elf2dol",
        #     inputs=link_steps[0].partial_output(),
        #     implicit=dtk,
        #     order_only="post-link",
        # )

        # ###
        # # Generate RELs
        # ###
        # n.comment("Generate REL(s)")
        # flags = "-w"
        # if len(build_config["links"]) > 1:
        #     flags += " -q"
        # n.rule(
        #     name="makerel",
        #     command=f"{dtk} rel make {flags} -c $config $names @$rspfile",
        #     description="REL",
        #     rspfile="$rspfile",
        #     rspfile_content="$in_newline",
        # )
        # generated_rels: List[str] = []
        # for idx, link in enumerate(build_config["links"]):
        #     # Map module names to link steps
        #     link_steps_local = list(
        #         filter(
        #             lambda step: step.name in link["modules"],
        #             link_steps,
        #         )
        #     )
        #     link_steps_local.sort(key=lambda step: step.module_id)
        #     # RELs can be the output of multiple link steps,
        #     # so we need to filter out duplicates
        #     rels_to_generate = list(
        #         filter(
        #             lambda step: step.module_id != 0
        #             and step.name not in generated_rels,
        #             link_steps_local,
        #         )
        #     )
        #     if len(rels_to_generate) == 0:
        #         continue
        #     generated_rels.extend(map(lambda step: step.name, rels_to_generate))
        #     rel_outputs = list(
        #         map(
        #             lambda step: step.output(),
        #             rels_to_generate,
        #         )
        #     )
        #     rel_names = list(
        #         map(
        #             lambda step: step.name,
        #             link_steps_local,
        #         )
        #     )
        #     rel_names_arg = " ".join(map(lambda name: f"-n {name}", rel_names))
        #     n.build(
        #         outputs=rel_outputs,
        #         rule="makerel",
        #         inputs=list(map(lambda step: step.partial_output(), link_steps_local)),
        #         implicit=[dtk, config.config_path],
        #         variables={
        #             "config": config.config_path,
        #             "rspfile": config.out_path() / f"rel{idx}.rsp",
        #             "names": rel_names_arg,
        #         },
        #         order_only="post-link",
        #     )
        #     n.newline()

        # Add all build steps needed post-build (re-building archives and such)
        write_custom_step("post-build", "post-link")

        ###
        # Helper rule for building all source files
        ###
        n.comment("Build all source files")
        n.build(
            outputs="all_source",
            rule="phony",
            inputs=source_inputs,
        )
        n.newline()

        ###
        # Link target
        ###
        if link_outputs:
            n.comment("Link target (build linked PE)")
            n.build(
                outputs="link",
                rule="phony",
                inputs=link_outputs,
            )
            n.newline()

        ###
        # Check hash
        ###
        # n.comment("Check hash")
        # ok_path = build_path / "ok"
        # quiet = "-q " if len(link_steps) > 3 else ""
        # n.rule(
        #     name="check",
        #     command=f"{dtk} shasum {quiet} -c $in -o $out",
        #     description="CHECK $in",
        # )
        # n.build(
        #     outputs=ok_path,
        #     rule="check",
        #     inputs=config.check_sha_path,
        #     implicit=[dtk, *link_outputs],
        #     order_only="post-build",
        # )
        # n.newline()

        ###
        # Calculate progress
        ###
        n.comment("Calculate progress")
        n.rule(
            name="progress",
            command=f"$python {configure_script} $configure_args progress",
            description="PROGRESS",
        )
        n.build(
            outputs="progress",
            rule="progress",
            implicit=[
                configure_script,
                python_lib,
                report_path,
                raw_report_path,
                str(db_sync_stamp),
                str(icf_map_checked),
            ],
            order_only="post-build",
        )

        ###
        # *** THE RENDERED ICF-ALIAS MAP IS AN INPUT OF THE REPORT. ***
        # objdiff.json names `map_file` -> build/<version>/icf_aliases.map, and
        # that map -- not scripts/symbol_aliases.json -- is the file objdiff
        # reads when it decides whether two relocation names denote one address.
        # It is a RENDERED artifact, and until 2026-08-12 it was rendered only at
        # CONFIGURE time (below, where objdiff.json is written). symbol_aliases.json
        # is not an input of the configure edge, so editing it and running `ninja`
        # re-rendered nothing: the tree kept the old map and the change measured
        # as zero. Measured that day -- a +198-complete-function fold tier landed
        # on main, `ninja` reported success, and the tree measured +0 of it; main's
        # map was still the one from 01:01 after a 05:57 merge. A landed change
        # that measures as nothing is indistinguishable from a lane that overstated
        # its result, which is the expensive way to find this. Same shape and same
        # week as the patcher-source defect in configure.py's post-compile block.
        # rb3-xenon has had this edge since bd6cefa1; this is dc3 catching up, and
        # the two are now deliberately identical in shape.
        #
        # TWO edges, because one of them cannot see everything:
        #
        #   icf_alias_map          renders the map when the JSON or the generator
        #                          is newer. This is the edge that makes "edit the
        #                          JSON, run ninja" sufficient.
        #   icf_alias_map_checked  re-derives the map content and FAILS THE BUILD
        #                          if the file on disk disagrees. It runs on every
        #                          build (`always`) because the three ways this map
        #                          goes wrong here are all mtime-INVISIBLE to the
        #                          edge above: a hand-edited map (the file's own
        #                          header says DO NOT EDIT BY HAND, which is said
        #                          because people do), a map rendered from a
        #                          different --aliases by a lane measuring a
        #                          variant, and a JSON restored with an OLDER mtime
        #                          than the map (`cp -a`, `tar -x`, `rsync -a` --
        #                          reproduced on rb3-xenon 2026-08-12: a byte-exact
        #                          restore of the JSON left a content-stale map and
        #                          `ninja` said "no work to do"). In each the map is
        #                          NEWER than its input, so no mtime rule can fire.
        # The check is a read-only assertion costing one interpreter start, placed
        # beside PROGRESS, which is already an always-dirty step -- it does not
        # break convergence, because "converged" here means no render and no
        # report, and neither runs twice.
        #
        # The check deliberately does NOT self-heal. Silently re-rendering over a
        # hand-edited map would erase somebody's deliberate experiment and hide
        # that it ever existed; the failure names the one command that fixes it.
        #
        # What this still does NOT protect: `objdiff-cli report generate -p <repo>`
        # invoked directly, which is how most measurement here actually happens and
        # which never touches ninja. Nothing in the build can reach that path. The
        # cheap assertion for it is the same one this edge runs --
        # `python3 scripts/gen_icf_alias_map.py --check` (exit 1 = stale, ~0.2s) --
        # and a measuring script should call it before it believes a number.
        ###
        n.comment("Render the synthetic ICF-alias map objdiff.json's map_file names")
        n.rule(
            name="icf_alias_map",
            command=f"$python {icf_gen_script} --out $out",
            description="GEN ICF-ALIAS MAP",
            # The generator writes only when the rendered content changes, so an
            # untouched map keeps its mtime; restat lets ninja mark the edge clean
            # instead of re-running it forever and dragging the report behind it.
            restat=True,
        )
        n.build(
            outputs=str(icf_map_path),
            rule="icf_alias_map",
            implicit=[icf_gen_script, icf_aliases_json],
        )
        n.comment("Assert the rendered map still agrees with the alias JSON")
        n.rule(
            name="icf_alias_map_check",
            command=f"$python {icf_gen_script} --check --out {icf_map_path} && touch $out",
            description="CHECK ICF-ALIAS MAP",
        )
        n.build(
            outputs=str(icf_map_checked),
            rule="icf_alias_map_check",
            implicit=[icf_gen_script, icf_aliases_json, str(icf_map_path), "always"],
        )

        ###
        # BELT AND BRACES: purge the report-cache sidecars when the alias map
        # moves. As of 2026-08-13 this edge is REDUNDANT, and it stays anyway.
        #
        # The history it was built for: `report generate -o X.json` writes a
        # sidecar `X.cache` and seeds the next run from it, and its key used to
        # be `ReportCache::hash_unit` (objdiff-cli/src/cmd/report.rs) over the
        # target obj bytes, the base obj bytes, the `-c` args, and the
        # project/unit `options` blocks -- with `map_file`, and the CONTENT of
        # the map it names, in none of them. So making the map an input of the
        # report edge was necessary and NOT sufficient: measured here
        # 2026-08-12, with that input wired, editing the alias JSON re-rendered
        # the map, re-ran REPORT, and report.json still read 4,849,144 matched
        # bytes when a cache-purged run of the same binary on the same tree read
        # 4,707,252. The edge fired and served the pre-change answer out of
        # cache -- the original defect one layer down.
        #
        # THE UPSTREAM FIX LANDED. The objdiff fork now folds the map file's
        # content hash -- and the resolved diff config, and the objdiff-cli
        # binary's own xxh3 -- into the cache key, and every generated report
        # carries a `provenance` block naming all three plus `cache_hits`. A
        # stale entry can no longer be served under a changed map. So the purge
        # below can no longer be the thing that saves a measurement.
        #
        # Kept regardless, for two reasons: it costs one `rm -f` on a rebuild
        # that only fires when the RENDERED map bytes actually changed (the
        # generator writes only on change, with `restat` above), so a
        # touched-but-identical JSON still costs nobody a re-diff; and it keeps
        # the build correct against an older objdiff-cli, which this repo does
        # not pin. Measurement code stays -- a redundant guard is cheap, and
        # removing it is only safe once the binary is pinned.
        ###
        n.comment("Purge report caches on an alias-map change "
                  "(redundant since the upstream map-keyed cache landed)")
        icf_purge_targets = " ".join(
            str(p.with_suffix(".cache"))
            # `baseline.json` by literal name: it is defined further down, and
            # it gets the same treatment as the other two.
            for p in (report_path, raw_report_path, build_path / "baseline.json")
        )
        n.rule(
            name="icf_alias_map_purge",
            command=f"rm -f {icf_purge_targets} && touch $out",
            description="PURGE REPORT CACHE (alias map changed)",
        )
        n.build(
            outputs=str(icf_map_purged),
            rule="icf_alias_map_purge",
            implicit=[str(icf_map_path)],
        )
        n.newline()

        ###
        # Generate progress report
        ###
        n.comment("Generate progress report")
        n.rule(
            name="report",
            command=f"{objdiff} report generate $objdiff_report_args -o $out",
            description="REPORT",
        )
        n.rule(
            name="report_raw",
            command=f"{objdiff} report generate $objdiff_report_args -c functionRelocDiffs=name_address -o $out",
            description="REPORT RAW",
        )
        # The reports are generated from the PATCHED objects, so they must
        # DEPEND ON the post-compile patch stamps, not merely be ordered after
        # them: `order_only="post-build"` never marks these edges dirty, and
        # now that every patcher restores the object's mtime (required for the
        # patch edges to converge -- see configure.py's post-compile block)
        # `all_source` does not change either when only the patchers ran.
        # Without this, a build in which only the patch passes fired (editing
        # a patcher script, or recovering a bypassed tree) leaves report.json
        # stale and the change measures as inert.  Same fix as rb3-xenon
        # bd6cefa1 part 3.
        # ... and on the rendered ICF-alias map, for the same reason one level up:
        # objdiff reads `map_file` at report time, so a re-rendered map that the
        # report edge does not list is a change ninja will not propagate.
        report_implicit: List[Union[str, Path]] = [
            objdiff, "objdiff.json", "all_source",
            str(icf_map_path), str(icf_map_purged),
        ]
        if config.custom_build_steps and "post-compile" in config.custom_build_steps:
            report_implicit.append("post-compile")
        n.build(
            outputs=report_path,
            rule="report",
            implicit=report_implicit,
            order_only="post-build",
        )
        n.build(
            outputs=raw_report_path,
            rule="report_raw",
            implicit=report_implicit,
            order_only="post-build",
        )

        ###
        # Sync report.json into the orchestrator DB (metadata + new symbols).
        # Build-safe: best-effort, never fails the build, skips if the DB is
        # locked by the live fleet. Verdicts are NOT set here — those are owned
        # by sync_objdiff.py (real diffs), per the orchestrator's design.
        ###
        n.comment("Sync report.json metadata into the orchestrator DB (build-safe)")
        n.rule(
            name="sync_db",
            command=(
                f"$python scripts/ingest_report.py {report_path} "
                f"--db decomp.db --build-safe && echo > $out"
            ),
            description="SYNC DB",
        )
        n.build(
            outputs=str(db_sync_stamp),
            rule="sync_db",
            inputs=[str(report_path)],
            implicit=["scripts/ingest_report.py"],
            order_only="post-build",
        )

        n.comment("Phony edge that will always be considered dirty by ninja.")
        n.comment(
            "This can be used as an implicit to a target that should always be rerun, ignoring file modified times."
        )
        n.build(
            outputs="always",
            rule="phony",
        )
        n.newline()

        ###
        # Regression test progress reports
        ###
        report_baseline_path = build_path / "baseline.json"
        report_changes_path = build_path / "report_changes.json"
        changes_fmt = config.tools_dir / "changes_fmt.py"
        regressions_md = build_path / "regressions.md"
        n.comment(
            "Create a baseline progress report for later match regression testing"
        )
        n.build(
            outputs=report_baseline_path,
            rule="report",
            implicit=[objdiff, "all_source", "always"],
            order_only="post-build",
        )
        n.build(
            outputs="baseline",
            rule="phony",
            inputs=report_baseline_path,
        )
        n.comment("Check for any match regressions against the baseline")
        n.comment("Will fail if no baseline has been created")
        n.rule(
            name="report_changes",
            command=f"{objdiff} report changes --format json-pretty {report_baseline_path} $in -o $out",
            description="CHANGES",
        )
        n.build(
            outputs=report_changes_path,
            rule="report_changes",
            inputs=report_path,
            implicit=[objdiff, "always"],
        )
        n.rule(
            name="changes_fmt",
            command=f"$python {changes_fmt} $args $in",
            description="CHANGESFMT",
        )
        n.build(
            outputs="changes",
            rule="changes_fmt",
            inputs=report_changes_path,
            implicit=changes_fmt,
        )
        n.build(
            outputs="changes_all",
            rule="changes_fmt",
            inputs=report_changes_path,
            implicit=changes_fmt,
            variables={"args": "--all"},
        )
        n.rule(
            name="changes_md",
            command=f"$python {changes_fmt} $in -o $out",
            description="CHANGESFMT $out",
        )
        n.build(
            outputs=regressions_md,
            rule="changes_md",
            inputs=report_changes_path,
            implicit=changes_fmt,
        )
        n.newline()

        n.comment("Unicorn behavioral equivalence test")
        n.rule(
            name="test_unicorn",
            command=f"$python -m scripts.unicorn_runner.run --batch-all --dual-fixture -j{os.cpu_count() or 8}",
            description="TEST-UNICORN",
            pool="console",
        )
        n.build(
            outputs="test-unicorn",
            rule="test_unicorn",
            implicit=["all_source"],
        )
        n.newline()

        ###
        # Helper tools
        ###
        # TODO: make these rules work for RELs too
        # dol_link_step = link_steps[0]
        # dol_elf_path = dol_link_step.partial_output()
        # n.comment("Check for mismatching symbols")
        # n.rule(
        #     name="dol_diff",
        #     command=f"{dtk} -L error dol diff $in",
        #     description=f"DIFF {dol_elf_path}",
        # )
        # n.build(
        #     inputs=[config.config_path, dol_elf_path],
        #     outputs="dol_diff",
        #     rule="dol_diff",
        # )
        # n.build(
        #     outputs="diff",
        #     rule="phony",
        #     inputs="dol_diff",
        # )
        # n.newline()

        # n.comment("Apply symbols from linked ELF")
        # n.rule(
        #     name="dol_apply",
        #     command=f"{dtk} dol apply $in",
        #     description=f"APPLY {dol_elf_path}",
        # )
        # n.build(
        #     inputs=[config.config_path, dol_elf_path],
        #     outputs="dol_apply",
        #     rule="dol_apply",
        #     implicit=[ok_path],
        # )
        # n.build(
        #     outputs="apply",
        #     rule="phony",
        #     inputs="dol_apply",
        # )
        # n.newline()

    ###
    # Split XEX
    ###
    build_config_path = build_path / "config.json"
    n.comment("Split XEX into relocatable objects")
    n.comment("write_if_changed: only update config.json mtime when content changes,")
    n.comment("preventing unnecessary generator re-runs that invalidate ninja deps.")
    n.rule(
        name="split",
        # prune_split_outputs: dtk rewrites the whole live unit set every run
        # but never REMOVES a unit whose splits.txt heading was re-pathed,
        # renamed or deleted, so the previous generation is orphaned on disk
        # forever (measured here: 8 stale .s + 9 stale .obj, oldest 2026-03-08).
        # Runs after the split succeeds; it never touches config.json, so the
        # cmp/touch mtime-preservation below is unaffected.
        command=f"cp $out_dir/config.json $out_dir/config.json.prev 2>/dev/null; "
                f"{dtk} xex split $in $out_dir && "
                f"$python tools/prune_split_outputs.py $out_dir && "
                f"if cmp -s $out_dir/config.json $out_dir/config.json.prev; then "
                f"touch -r $out_dir/config.json.prev $out_dir/config.json; fi; "
                f"rm -f $out_dir/config.json.prev",
        description="SPLIT $in",
        depfile="$out_dir/dep",
        deps="gcc",
    )
    n.build(
        inputs=config.config_path,
        outputs=build_config_path,
        rule="split",
        implicit=dtk,
        variables={"out_dir": build_path},
    )
    n.newline()

    ###
    # Regenerate on change
    ###
    n.comment("Reconfigure on change")
    n.rule(
        name="configure",
        command=f"$python {configure_script} $configure_args",
        generator=True,
        description=f"RUN {configure_script}",
    )
    n.build(
        outputs=["build.ninja", "objdiff.json"],
        rule="configure",
        implicit=[
            build_config_path,
            configure_script,
            python_lib,
            python_lib_dir / "ninja_syntax.py",
            *(config.reconfig_deps or []),
        ],
    )
    n.newline()

    ###
    # Default rule
    ###
    n.comment("Default rule")
    if build_config:
        if config.non_matching:
            n.default(link_outputs)
        elif config.progress:
            n.default("progress")
        else:
            n.default(ok_path)
    else:
        n.default(build_config_path)

    # Write build.ninja
    with open("build.ninja", "w", encoding="utf-8") as f:
        f.write(out.getvalue())
    out.close()


# Generate objdiff.json
def generate_objdiff_config(
    config: ProjectConfig,
    objects: Dict[str, Object],
    build_config: Optional[BuildConfig],
) -> None:
    if build_config is None:
        return

    # Load existing objdiff.json
    existing_units = {}
    if Path("objdiff.json").is_file():
        try:
            with open("objdiff.json", "r", encoding="utf-8") as r:
                existing_config = json.load(r)
                existing_units = {unit["name"]: unit for unit in existing_config["units"]}
        except json.JSONDecodeError:
            pass

    if config.ninja_path:
        ninja = str(config.ninja_path.absolute())
    else:
        ninja = "ninja"

    objdiff_config: Dict[str, Any] = {
        "min_version": "2.0.0-beta.5",
        "custom_make": ninja,
        "build_target": False,
        "watch_patterns": [
            "*.c",
            "*.cp",
            "*.cpp",
            "*.h",
            "*.hpp",
            "*.inc",
            "*.py",
            "*.yml",
            "*.txt",
            "*.json",
        ],
        "units": [],
        "progress_categories": [],
        # Relocation ruler. The objdiff default, `none`, compares a relocation's
        # POSITION and TYPE but not the NAME of the symbol it points at, so a `bl`
        # to the wrong callee, a load of the wrong global, or a reference to the
        # wrong float constant all score a COMPLETE match. That is not theoretical
        # here: an audit of the strict census found 32 functions whose .text was
        # byte-identical to retail and which called the wrong thing -- among them
        # EaseInExp raising t to 3.03 where retail raises it to 3.76, and
        # MsgSinks::RemoveSink reporting at the wrong severity.
        #
        # `name_check` is `name_only` plus the tolerances a SPLIT target object
        # requires: a missing left-side relocation and a placeholder-named left
        # target (`fn_8xxxxxxx`, `lbl_*`, `$L…`) are unverifiable rather than
        # wrong, COFF weak-external aliases (`??_E` defaulting to `??_G`) are one
        # body, template array-size instantiations are the same code, and two
        # non-code sections are the same logical section. It honours the ICF
        # equivalence classes in `map_file` by design.
        #
        # This DOES lower every recorded progress number, and the drop is the
        # point: it is the part of the corpus that was being credited without
        # being checked. Measured at the flip, dc3 goes 43.7276% -> 40.3618%
        # matched_code, exposing 1,166 of 29,182 complete functions.
        "options": {
            "functionRelocDiffs": "name_check",
        },
    }

    # ICF-fold symbol alias map. Retail's /OPT:ICF link folds byte-identical
    # COMDATs, so several source spellings survive at ONE address: the target
    # objects can only name the survivor, while our objects emit their own TU's
    # spelling, and objdiff's by-name reloc comparison flags a [sym] mismatch on
    # a call site that is the same bytes to the same code. objdiff already
    # consumes ICF equivalences from an MSVC `map_file` (parse_msvc_map groups
    # symbols sharing an address); scripts/gen_icf_alias_map.py renders the
    # admitted fold classes (scripts/symbol_aliases.json -- body-test witnesses,
    # COFF weak-external aliases, and address-sharing in the retail linker map
    # orig/373307D9/ham_xbox_r.map) into a synthetic one. The retail map is a
    # SOURCE for those classes, not the file objdiff consumes: it is the whole
    # linker symbol table, while the synthetic map carries only what was
    # admitted. Generate it here so objdiff.json can reference it even on the
    # FIRST configure of a fresh tree.
    #
    # The `map_file` key is written ONLY if the map exists, and that pairing is
    # load-bearing in BOTH directions: naming a map the tree does not have makes
    # decomp-synth's symbol_equivalences fail closed to no equivalences at all,
    # while shipping the alias JSON with no map_file makes its gate (f) -- "the
    # class is one objdiff itself consumes" -- silently skip, leaving the grader
    # applying classes the sole judge does not. Absent both, behaviour is
    # exactly the pre-ICF behaviour.
    #
    # This render is the BOOTSTRAP only -- it exists so `map_file` can be written
    # on the FIRST configure of a fresh tree, before any ninja edge has run. It is
    # NOT what keeps the map fresh: configure runs only when configure.py, this
    # file, or a config/ input changes, and scripts/symbol_aliases.json is none of
    # those. That gap is what made a landed alias tier measure as +0 on 2026-08-12.
    # The `icf_alias_map` / `icf_alias_map_checked` edges above own freshness; the
    # design comment there is the one place per repo this rule is written down.
    icf_gen = Path("scripts") / "gen_icf_alias_map.py"
    icf_map = config.out_path() / "icf_aliases.map"
    if icf_gen.is_file() and Path("scripts", "symbol_aliases.json").is_file():
        try:
            subprocess.run(
                [sys.executable, str(icf_gen), "--out", str(icf_map)],
                check=True, stdout=subprocess.DEVNULL,
            )
        except Exception as e:
            print(f"(icf alias map generation skipped: {e})")
    if icf_map.is_file():
        objdiff_config["map_file"] = str(icf_map).replace(os.sep, "/")

    # decomp.me compiler name mapping
    COMPILER_MAP = {
        "GC/1.0": "mwcc_233_144",
        "GC/1.1": "mwcc_233_159",
        "GC/1.1p1": "mwcc_233_159p1",
        "GC/1.2.5": "mwcc_233_163",
        "GC/1.2.5e": "mwcc_233_163e",
        "GC/1.2.5n": "mwcc_233_163n",
        "GC/1.3": "mwcc_242_53",
        "GC/1.3.2": "mwcc_242_81",
        "GC/1.3.2r": "mwcc_242_81r",
        "GC/2.0": "mwcc_247_92",
        "GC/2.0p1": "mwcc_247_92p1",
        "GC/2.5": "mwcc_247_105",
        "GC/2.6": "mwcc_247_107",
        "GC/2.7": "mwcc_247_108",
        "GC/3.0a3": "mwcc_41_51213",
        "GC/3.0a3.2": "mwcc_41_60126",
        "GC/3.0a3.3": "mwcc_41_60209",
        "GC/3.0a3.4": "mwcc_42_60308",
        "GC/3.0a5": "mwcc_42_60422",
        "GC/3.0a5.2": "mwcc_41_60831",
        "GC/3.0": "mwcc_41_60831",
        "Wii/1.0RC1": "mwcc_42_140",
        "Wii/0x4201_127": "mwcc_42_142",
        "Wii/1.0a": "mwcc_42_142",
        "Wii/1.0": "mwcc_43_145",
        "Wii/1.1": "mwcc_43_151",
        "Wii/1.3": "mwcc_43_172",
        "Wii/1.5": "mwcc_43_188",
        "Wii/1.6": "mwcc_43_202",
        "Wii/1.7": "mwcc_43_213",
        "X360/14.00.2110": "msvc_ppc_14.00.2110",
        "X360/16.00.11886.00": "msvc_ppc_16.00.11886.00",
    }

    # decomp.me platform mapping (by version prefix)
    PLATFORM_MAP = {
        "GC": "gc_wii",
        "Wii": "gc_wii",
        "X360": "xbox360",
    }

    def add_unit(
        build_obj: BuildConfigUnit, module_name: str, progress_categories: List[str]
    ) -> None:
        obj_path, obj_name = build_obj["object"], build_obj["name"]
        base_object = Path(obj_name).with_suffix("")
        name = str(Path(module_name) / base_object).replace(os.sep, "/")
        unit_config: Dict[str, Any] = {
            "name": name,
            "target_path": obj_path,
            "base_path": None,
            "scratch": None,
            "metadata": {
                "complete": None,
                "reverse_fn_order": None,
                "source_path": None,
                "progress_categories": progress_categories,
                "auto_generated": build_obj["autogenerated"],
            },
            "symbol_mappings": None,
        }

        # Preserve existing symbol mappings
        existing_unit = existing_units.get(name)
        if existing_unit is not None:
            unit_config["symbol_mappings"] = existing_unit.get("symbol_mappings")

        obj = objects.get(obj_name)
        if obj is None:
            objdiff_config["units"].append(unit_config)
            return

        src_exists = obj.src_path is not None and obj.src_path.exists()
        if src_exists:
            unit_config["base_path"] = obj.src_obj_path
            unit_config["metadata"]["source_path"] = obj.src_path

        # Filter out include directories
        def keep_flag(flag):
            return (
                not flag.startswith("-i ")
                and not flag.startswith("-i-")
                and not flag.startswith("-I ")
                and not flag.startswith("-I+")
                and not flag.startswith("-I-")
                and not flag.startswith("/I")
            )

        all_cflags = list(
            filter(keep_flag, obj.options["cflags"] + obj.options["extra_cflags"])
        )
        reverse_fn_order = False
        for flag in all_cflags:
            if not flag.startswith("-inline "):
                continue
            for value in flag.split(" ")[1].split(","):
                if value == "deferred":
                    reverse_fn_order = True
                elif value == "nodeferred":
                    reverse_fn_order = False

        compiler_version = COMPILER_MAP.get(obj.options["mw_version"])
        if compiler_version is None:
            print(f"Missing scratch compiler mapping for {obj.options['mw_version']}")
        else:
            platform_prefix = obj.options["mw_version"].split("/")[0]
            platform = PLATFORM_MAP.get(platform_prefix, "gc_wii")
            cflags_str = make_flags_str(all_cflags)
            unit_config["scratch"] = {
                "platform": platform,
                "compiler": compiler_version,
                "c_flags": cflags_str,
                "preset_id": obj.options["scratch_preset_id"],
            }
            if src_exists:
                unit_config["scratch"].update(
                    {
                        "ctx_path": obj.ctx_path,
                        "build_ctx": True,
                    }
                )
        category_opt: List[str] | str = obj.options["progress_category"]
        if isinstance(category_opt, list):
            progress_categories.extend(category_opt)
        elif category_opt is not None:
            progress_categories.append(category_opt)
        unit_config["metadata"].update(
            {
                "complete": obj.completed if src_exists else None,
                "reverse_fn_order": reverse_fn_order,
                "progress_categories": progress_categories,
            }
        )
        objdiff_config["units"].append(unit_config)

    # Add DOL units
    for unit in build_config["units"]:
        progress_categories = []
        # Only include a "dol" category if there are any modules
        # Otherwise it's redundant with the global report measures
        if len(build_config["modules"]) > 0:
            progress_categories.append("dol")
        add_unit(unit, build_config["name"], progress_categories)

    # Add REL units
    for module in build_config["modules"]:
        for unit in module["units"]:
            progress_categories = []
            if config.progress_modules:
                progress_categories.append("modules")
            if config.progress_each_module:
                progress_categories.append(module["name"])
            add_unit(unit, module["name"], progress_categories)

    # Add progress categories
    def add_category(id: str, name: str):
        objdiff_config["progress_categories"].append(
            {
                "id": id,
                "name": name,
            }
        )

    if len(build_config["modules"]) > 0:
        add_category("dol", "DOL")
        if config.progress_modules:
            add_category("modules", "Modules")
        if config.progress_each_module:
            for module in build_config["modules"]:
                add_category(module["name"], module["name"])
    for category in config.progress_categories:
        add_category(category.id, category.name)

    def cleandict(d):
        if isinstance(d, dict):
            return {k: cleandict(v) for k, v in d.items() if v is not None}
        elif isinstance(d, list):
            return [cleandict(v) for v in d]
        else:
            return d

    # Write objdiff.json
    with open("objdiff.json", "w", encoding="utf-8") as w:

        def unix_path(input: Any) -> str:
            return str(input).replace(os.sep, "/") if input else ""

        json.dump(cleandict(objdiff_config), w, indent=2, default=unix_path)


def generate_compile_commands(
    config: ProjectConfig,
    objects: Dict[str, Object],
    build_config: Optional[BuildConfig],
) -> None:
    if build_config is None or not config.generate_compile_commands:
        return

    # The following code attempts to convert mwcc flags to clang flags
    # for use with clangd.

    # Flags to ignore explicitly
    CFLAG_IGNORE: Set[str] = {
        # Search order modifier
        # Has a different meaning to Clang, and would otherwise
        # be picked up by the include passthrough prefix
        "-I-",
        "-i-",
    }
    CFLAG_IGNORE_PREFIX: Tuple[str, ...] = (
        # Recursive includes are not supported by modern compilers
        "-ir ",
    )

    # Flags to replace
    CFLAG_REPLACE: Dict[str, str] = {}
    CFLAG_REPLACE_PREFIX: Tuple[Tuple[str, str], ...] = (
        # Includes
        ("-i ", "-I"),
        ("-I ", "-I"),
        ("-I+", "-I"),
        # Defines
        ("-d ", "-D"),
        ("-D ", "-D"),
        ("-D+", "-D"),
    )

    # Flags with a finite set of options
    CFLAG_REPLACE_OPTIONS: Tuple[Tuple[str, Dict[str, Tuple[str, ...]]], ...] = (
        # Exceptions
        (
            "-Cpp_exceptions",
            {
                "off": ("-fno-cxx-exceptions",),
                "on": ("-fcxx-exceptions",),
            },
        ),
        # RTTI
        (
            "-RTTI",
            {
                "off": ("-fno-rtti",),
                "on": ("-frtti",),
            },
        ),
        # Language configuration
        (
            "-lang",
            {
                "c": ("--language=c", "--std=c99"),
                "c99": ("--language=c", "--std=c99"),
                "c++": ("--language=c++", "--std=c++98"),
                "cplus": ("--language=c++", "--std=c++98"),
            },
        ),
        # Enum size
        (
            "-enum",
            {
                "min": ("-fshort-enums",),
                "int": ("-fno-short-enums",),
            },
        ),
        # Common BSS
        (
            "-common",
            {
                "off": ("-fno-common",),
                "on": ("-fcommon",),
            },
        ),
    )

    # Flags to pass through
    CFLAG_PASSTHROUGH: Set[str] = set()
    CFLAG_PASSTHROUGH_PREFIX: Tuple[str, ...] = (
        "-I",  # includes
        "-D",  # defines
    )

    clangd_config = []

    def add_unit(build_obj: BuildConfigUnit) -> None:
        obj = objects.get(build_obj["name"])
        if obj is None:
            return

        # Skip unresolved objects
        if (
            obj.src_path is None
            or obj.src_obj_path is None
            or not file_is_c_cpp(obj.src_path)
        ):
            return

        # Gather cflags for source file
        cflags: list[str] = []

        def append_cflags(flags: Iterable[str]) -> None:
            # Match a flag against either a set of concrete flags, or a set of prefixes.
            def flag_match(
                flag: str, concrete: Set[str], prefixes: Tuple[str, ...]
            ) -> bool:
                if flag in concrete:
                    return True

                for prefix in prefixes:
                    if flag.startswith(prefix):
                        return True

                return False

            # Determine whether a flag should be ignored.
            def should_ignore(flag: str) -> bool:
                return flag_match(flag, CFLAG_IGNORE, CFLAG_IGNORE_PREFIX)

            # Determine whether a flag should be passed through.
            def should_passthrough(flag: str) -> bool:
                return flag_match(flag, CFLAG_PASSTHROUGH, CFLAG_PASSTHROUGH_PREFIX)

            # Attempts replacement for the given flag.
            def try_replace(flag: str) -> bool:
                replacement = CFLAG_REPLACE.get(flag)
                if replacement is not None:
                    cflags.append(replacement)
                    return True

                for prefix, replacement in CFLAG_REPLACE_PREFIX:
                    if flag.startswith(prefix):
                        cflags.append(flag.replace(prefix, replacement, 1))
                        return True

                for prefix, options in CFLAG_REPLACE_OPTIONS:
                    if not flag.startswith(prefix):
                        continue

                    # "-lang c99" and "-lang=c99" are both generally valid option forms
                    option = flag.removeprefix(prefix).removeprefix("=").lstrip()
                    replacements = options.get(option)
                    if replacements is not None:
                        cflags.extend(replacements)

                    return True

                return False

            for flag in flags:
                if flag.startswith("/I "):
                    cflags.extend(flag.split(' '))
                else:
                    cflags.append(flag)

                # # Ignore flags first
                # if should_ignore(flag):
                #     continue

                # # Then find replacements
                # if try_replace(flag):
                #     continue

                # # Pass flags through last
                # if should_passthrough(flag):
                #     cflags.append(flag)
                #     continue

        append_cflags(obj.options["cflags"])
        append_cflags(obj.options["extra_cflags"])
        cflags.extend(config.extra_clang_flags)
        cflags.extend(obj.options["extra_clang_flags"])

        unit_config = {
            "directory": Path.cwd(),
            "file": obj.src_path,
            "output": obj.src_obj_path,
            "arguments": [
                "clang-cl.exe",
                "--target=powerpc-eabi",
                *cflags,
                obj.src_path,
                "/Fo",
                obj.src_obj_path,
            ],
        }
        clangd_config.append(unit_config)

    # Add DOL units
    for unit in build_config["units"]:
        add_unit(unit)

    # Add REL units
    for module in build_config["modules"]:
        for unit in module["units"]:
            add_unit(unit)

    # Write compile_commands.json
    with open("compile_commands.json", "w", encoding="utf-8") as w:

        def default_format(o):
            if isinstance(o, Path):
                return o.resolve().as_posix()
            return str(o)

        json.dump(clangd_config, w, indent=2, default=default_format)


# Print progress information from objdiff report
def calculate_progress(config: ProjectConfig) -> None:
    config.validate()
    out_path = config.out_path()
    report_path = out_path / "report.json"
    raw_report_path = out_path / "report_raw.json"
    if not report_path.is_file():
        sys.exit(f"Report file {report_path} does not exist")
    if not raw_report_path.is_file():
        sys.exit(f"Raw report file {raw_report_path} does not exist")

    report_data: Dict[str, Any] = {}
    with open(report_path, "r", encoding="utf-8") as f:
        report_data = json.load(f)
    raw_report_data: Dict[str, Any] = {}
    with open(raw_report_path, "r", encoding="utf-8") as f:
        raw_report_data = json.load(f)

    # Convert string numbers (u64) to int
    def convert_numbers(data: Dict[str, Any]) -> None:
        for key, value in data.items():
            if isinstance(value, str) and value.isdigit():
                data[key] = int(value)

    convert_numbers(report_data["measures"])
    convert_numbers(raw_report_data["measures"])
    for category in report_data.get("categories", []):
        convert_numbers(category["measures"])
    for category in raw_report_data.get("categories", []):
        convert_numbers(category["measures"])

    raw_category_by_id = {c["id"]: c for c in raw_report_data.get("categories", [])}

    # Output to GitHub Actions job summary, if available
    summary_path = os.getenv("GITHUB_STEP_SUMMARY")
    summary_file: Optional[IO[str]] = None
    if summary_path:
        summary_file = open(summary_path, "a", encoding="utf-8")
        summary_file.write("```\n")

    def progress_print(s: str) -> None:
        print(s)
        if summary_file:
            summary_file.write(s + "\n")

    # Print human-readable progress
    progress_print("Progress:")

    def print_category(name: str, measures: Dict[str, Any], raw_measures: Dict[str, Any]) -> None:
        total_code = measures.get("total_code", 0)
        matched_code = measures.get("matched_code", 0)
        matched_code_percent = measures.get("matched_code_percent", 0)
        total_data = measures.get("total_data", 0)
        matched_data = measures.get("matched_data", 0)
        matched_data_percent = measures.get("matched_data_percent", 0)
        total_functions = measures.get("total_functions", 0)
        matched_functions = measures.get("matched_functions", 0)
        complete_code_percent = measures.get("complete_code_percent", 0)
        total_units = measures.get("total_units", 0)
        complete_units = measures.get("complete_units", 0)
        fuzzy_match_percent = measures.get("fuzzy_match_percent", 0)
        raw_fuzzy_match_percent = raw_measures.get("fuzzy_match_percent", 0)

        progress_print(
            f"  {name}: {matched_code_percent:.2f}% matched, {complete_code_percent:.2f}% linked ({complete_units} / {total_units} files)"
        )
        progress_print(
            f"    Fuzzy: {fuzzy_match_percent:.2f}% normalized (default), {raw_fuzzy_match_percent:.2f}% raw"
        )
        progress_print(
            f"    Code: {matched_code} / {total_code} bytes ({matched_functions} / {total_functions} functions)"
        )
        progress_print(
            f"    Data: {matched_data} / {total_data} bytes ({matched_data_percent:.2f}%)"
        )

    print_category("All", report_data["measures"], raw_report_data["measures"])
    for category in report_data.get("categories", []):
        if config.print_progress_categories is True or (
            isinstance(config.print_progress_categories, list)
            and category["id"] in config.print_progress_categories
        ):
            raw_category = raw_category_by_id.get(category["id"], {})
            print_category(category["name"], category["measures"], raw_category.get("measures", {}))

    if config.progress_use_fancy:
        measures = report_data["measures"]
        total_code = measures.get("total_code", 0)
        total_data = measures.get("total_data", 0)
        if total_code == 0 or total_data == 0:
            return
        code_frac = measures.get("complete_code", 0) / total_code
        data_frac = measures.get("complete_data", 0) / total_data

        progress_print(
            "\nYou have {} out of {} {} and {} out of {} {}.".format(
                math.floor(code_frac * config.progress_code_fancy_frac),
                config.progress_code_fancy_frac,
                config.progress_code_fancy_item,
                math.floor(data_frac * config.progress_data_fancy_frac),
                config.progress_data_fancy_frac,
                config.progress_data_fancy_item,
            )
        )

    # Finalize GitHub Actions job summary
    if summary_file:
        summary_file.write("```\n")
        summary_file.close()
