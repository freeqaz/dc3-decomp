#!/usr/bin/env python3

###
# Generates build files for the project.
# This file also includes the project configuration,
# such as compiler flags and the object matching status.
#
# Usage:
#   python3 configure.py
#   ninja
#
# Append --help to see available options.
###

import argparse
import json
import sys
from pathlib import Path
from typing import Any, Dict, List, Union

from tools.project import (
    Object,
    ProgressCategory,
    ProjectConfig,
    calculate_progress,
    generate_build,
    generate_build_ninja,
    generate_objdiff_config,
    generate_compile_commands,
    load_build_config,
    is_windows,
)

from tools.defines_common import (
    cflags_includes,
    DEFAULT_VERSION,
    VERSIONS
)

# for future reference, ninja code to build one cpp -> obj
# # Define variables
# x360_cl = win32/cl.exe
# cflags = /nologo /c /Zi /O1 /TP /Isrc/system
# objdir = build/373307D9/src

# # Rule to compile C/C++ source to .obj
# rule cpp
#   command = $x360_cl $cflags /Fo$out $in
#   description = Compiling $in to $out

# # Build output .obj file from input .cpp
# build $objdir//system/math/Rand2.obj: cpp src/system/math/Rand2.cpp

parser = argparse.ArgumentParser()
parser.add_argument(
    "mode",
    choices=["configure", "progress"],
    default="configure",
    help="script mode (default: configure)",
    nargs="?",
)
parser.add_argument(
    "-v",
    "--version",
    choices=VERSIONS,
    type=str.upper,
    default=VERSIONS[DEFAULT_VERSION],
    help="version to build",
)
parser.add_argument(
    "--build-dir",
    metavar="DIR",
    type=Path,
    default=Path("build"),
    help="base build directory (default: build)",
)
parser.add_argument(
    "--binutils",
    metavar="BINARY",
    type=Path,
    help="path to binutils (optional)",
)
parser.add_argument(
    "--compilers",
    metavar="DIR",
    type=Path,
    help="path to compilers (optional)",
)
parser.add_argument(
    "--map",
    action="store_true",
    help="generate map file(s)",
)
parser.add_argument(
    "--debug",
    action="store_true",
    help="build with debug info (non-matching)",
)
if not is_windows():
    parser.add_argument(
        "--wrapper",
        "--wibo",
        metavar="BINARY",
        type=Path,
        help="path to wibo or wine (optional)",
    )
parser.add_argument(
    "--dtk",
    metavar="BINARY | DIR",
    type=Path,
    help="path to decomp-toolkit binary or source (optional)",
)
parser.add_argument(
    "--objdiff",
    metavar="BINARY | DIR",
    type=Path,
    help="path to objdiff-cli binary or source (optional)",
)
parser.add_argument(
    "--sjiswrap",
    metavar="EXE",
    type=Path,
    help="path to sjiswrap.exe (optional)",
)
parser.add_argument(
    "--ninja",
    metavar="BINARY",
    type=Path,
    help="path to ninja binary (optional)"
)
parser.add_argument(
    "--verbose",
    action="store_true",
    help="print verbose output",
)
parser.add_argument(
    "--non-matching",
    dest="non_matching",
    action="store_true",
    help="builds equivalent (but non-matching) or modded objects",
)
parser.add_argument(
    "--warn",
    dest="warn",
    type=str,
    choices=["all", "off", "error"],
    help="how to handle warnings",
)
parser.add_argument(
    "--no-progress",
    dest="progress",
    action="store_false",
    help="disable progress calculation",
)
args = parser.parse_args()

config = ProjectConfig()
config.version = str(args.version)
version_num = VERSIONS.index(config.version)

# Apply arguments
config.build_dir = args.build_dir
config.dtk_path = args.dtk
config.objdiff_path = args.objdiff
config.binutils_path = args.binutils
config.compilers_path = args.compilers
config.generate_map = args.map
config.non_matching = args.non_matching
config.sjiswrap_path = args.sjiswrap
config.ninja_path = args.ninja
config.progress = args.progress
if not is_windows():
    config.wrapper = args.wrapper
# Don't build asm unless we're --non-matching
if not config.non_matching:
    config.asm_dir = None

# Tool versions
config.binutils_tag = "2.42-1"
config.compilers_tag = "20250812"
config.dtk_tag = "v1.9.2"
config.objdiff_tag = "v4.0.0"
config.sjiswrap_tag = "v1.2.1"
config.wibo_tag = "1.0.0"

# Project
config_dir = Path("config") / config.version
config_json_path = config_dir / "config.json"
objects_path = config_dir / "objects.json"
config.config_path = config_dir / "config.yml"
config.check_sha_path = config_dir / "build.sha1"
# Use for any additional files that should cause a re-configure when modified
config.reconfig_deps = [
    config_json_path,
    objects_path,
]

# if args.debug:
#     config.ldflags.append("-g")  # Or -gdwarf-2 for Wii linkers
# if args.map:
#     config.ldflags.append("-mapunused")
    # config.ldflags.append("-listclosure") # For Wii linkers

# Optional numeric ID for decomp.me preset
# Can be overridden in libraries or objects
config.scratch_preset_id = None

# Build flags
flags = json.load(open(config_json_path, "r", encoding="utf-8"))
progress_categories: dict[str, str] = flags["progress_categories"]
asflags: list[str] = flags["asflags"]
ldflags: list[str] = flags["ldflags"]
cflags: dict[str, dict] = flags["cflags"]

def get_cflags(name: str) -> list[str]:
    return cflags[name]["flags"]
def add_cflags(name: str, flags: list[str]):
    cflags[name]["flags"] = [*flags, *cflags[name]["flags"]]

def get_cflags_base(name: str) -> str:
    return cflags[name].get("base", None)

def are_cflags_inherited(name: str) -> bool:
    return "inherited" in cflags[name]
def set_cflags_inherited(name: str):
    cflags[name]["inherited"] = True

def apply_base_cflags(key: str):
    if are_cflags_inherited(key):
        return

    base = get_cflags_base(key)
    if base is None:
        add_cflags(key, cflags_includes)
    else:
        apply_base_cflags(base)
        add_cflags(key, get_cflags(base))

    set_cflags_inherited(key)

# Set up base flags
base_cflags = get_cflags("base")
# base_cflags.append(f"-d BUILD_VERSION={version_num}")
# base_cflags.append(f"-d VERSION_{config.version}")

# Set conditionally-added flags
# cflags
# if args.debug:
#     base_cflags.append("-sym dwarf-2,full")
#     # Causes code generation memes, use only in desperation
#     # base_cflags.append("-pragma \"debuginline on\"")
# else:
#     base_cflags.append("-DNDEBUG=1")

# ldflags
# if args.debug:
#     ldflags.append("-gdwarf-2")
# if config.generate_map:
#     ldflags.extend(["-mapunused", "-listclosure"])

# Apply cflag inheritance
for key in cflags.keys():
    apply_base_cflags(key)

config.asflags = [
    *asflags,
    # f"--defsym BUILD_VERSION={version_num}",
    # f"--defsym VERSION_{config.version}",
]
config.ldflags = ldflags

config.linker_version = "X360/16.00.11886.00"

config.shift_jis = False
config.progress_all = False

# Object files
Matching = True
Equivalent = config.non_matching
NonMatching = False

config.warn_missing_config = True
config.warn_missing_source = False

def get_object_completed(status: str) -> bool:
    if status == "MISSING":
        return NonMatching
    elif status == "Matching":
        return Matching
    elif status == "NonMatching":
        return NonMatching
    elif status == "Equivalent":
        return Equivalent
    elif status == "LinkIssues":
        return NonMatching

    assert False, f"Invalid object status {status}"

libs: list[dict] = []
objects: dict[str, dict] = json.load(open(objects_path, "r", encoding="utf-8"))
for (lib, lib_config) in objects.items():
    # config_cflags: str | list[str]
    config_cflags: list[str] = lib_config.pop("cflags")
    lib_cflags = get_cflags(config_cflags) if isinstance(config_cflags, str) else config_cflags

    lib_objects: list[Object] = []
    # config_objects: dict[str, str | dict]
    config_objects: dict[str, Union[str, dict[str, Union[str, Any]]]] = lib_config.pop("objects")
    if len(config_objects) < 1:
        continue

    for (path, obj_config) in config_objects.items():
        if isinstance(obj_config, str):
            completed = get_object_completed(obj_config)
            lib_objects.append(Object(completed, path))
        else:
            completed = get_object_completed(obj_config["status"])

            if "cflags" in obj_config:
                object_cflags = obj_config["cflags"]
                if isinstance(object_cflags, str):
                    obj_config["cflags"] = get_cflags(object_cflags)

            lib_objects.append(Object(completed, path, **obj_config))

    libs.append({
        "lib": lib,
        "cflags": lib_cflags,
        "host": False,
        "objects": lib_objects,
        **lib_config
    })

config.libs = libs

# Link order: reorder objects to match original linker layout
# Generated by: python3 scripts/build/generate_link_order.py
link_order_file = config_dir / "link_order.txt"
if link_order_file.exists():
    link_order_lines = link_order_file.read_text().strip().splitlines()
    link_order_map = {name: i for i, name in enumerate(link_order_lines)}

    def link_order_callback(module_id: int, objects: List[str]) -> List[str]:
        if module_id != 0:
            return objects

        # Split into ordered (in link_order.txt) and unordered (not in it)
        ordered = []
        unordered = []
        for obj in objects:
            if obj in link_order_map:
                ordered.append(obj)
            else:
                unordered.append(obj)

        # Sort the ordered objects by their position in link_order.txt
        ordered.sort(key=lambda x: link_order_map[x])

        # Append unordered objects at the end (preserving original relative order)
        return ordered + unordered

    config.link_order_callback = link_order_callback
    config.reconfig_deps.append(link_order_file)

# Progress tracking categories
config.progress_categories = [ProgressCategory(name, desc) for (name, desc) in progress_categories.items()]
config.progress_each_module = args.verbose

if args.mode == "configure":
    # Write build.ninja and objdiff.json
    # Inline generate_build() to inject extra units not in dtk config.json
    config.validate()
    objects = config.objects()
    build_config = load_build_config(config, config.out_path() / "config.json")
    # Inject decomp-only units (not produced by dtk split)
    if build_config is not None:
        extra_units = [
            {"object": None, "name": "link_glue.cpp", "autogenerated": False, "code_size": 0, "data_size": 0},
        ]
        build_config["units"].extend(extra_units)
    generate_build_ninja(config, objects, build_config)
    generate_objdiff_config(config, objects, build_config)
    generate_compile_commands(config, objects, build_config)
elif args.mode == "progress":
    # Print progress and write progress.json
    calculate_progress(config)
else:
    sys.exit("Unknown mode: " + args.mode)
