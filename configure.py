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
# Always use local tool builds (relative paths for stable ninja regeneration)
config.dtk_path = args.dtk or Path("..") / "jeff" / "target" / "release" / "dtk"
config.objdiff_path = args.objdiff or Path("..") / "objdiff" / "target" / "release" / "objdiff-cli"
config.binutils_path = args.binutils
config.compilers_path = args.compilers
config.generate_map = args.map
config.non_matching = args.non_matching
config.sjiswrap_path = args.sjiswrap
config.ninja_path = args.ninja
config.progress = args.progress
if not is_windows():
    config.wrapper = args.wrapper if hasattr(args, 'wrapper') and args.wrapper else Path("..") / "wibo" / "build" / "release" / "wibo"
# Don't build asm unless we're --non-matching
if not config.non_matching:
    config.asm_dir = None

# Tool versions
config.binutils_tag = "2.42-1"
config.compilers_tag = "20250812"
# dtk_tag does NOT select the binary -- config.dtk_path above wins, and it points
# at our jeff fork's build (../jeff/target/release/dtk), so whatever is compiled
# there is what splits the XEX. The tag still has one live job: tools/project.py's
# load_build_config() deletes build/<version>/config.json when the "version" it
# recorded is older than dtk_tag, forcing a re-split. Leaving the tag stale
# therefore silently disables that staleness gate. Keep it in step with the jeff
# binary actually deployed.
#
# jeff cdfe173 == v1.10.0 (previously 1.9.5). The 1.9.5 -> 1.10.0 delta is a
# single change: xpdb.rs::try_parse_pdb now harvests S_GPROC32/S_LPROC32 sizes
# from a PDB's per-module DBI streams instead of only the globals stream (plus an
# env-gated JEFF_DUMP_RELOCS stderr dump in xex.rs that mutates nothing).
# try_parse_pdb is reached from load_analyze_xex only under
# `if let Some(pdb_path) = &config.base.pdb`, and config/373307D9/config.yml has
# no `pdb:` key -- so the whole changed path is unreachable for this project.
#
# Verified empirically on 2026-08-08 rather than assumed: 1.9.5 and 1.10.0 were
# each run over config/373307D9/config.yml into separate out_dirs and the outputs
# compared by sha256. All 2223 split .obj and all 2223 .s files are byte-identical
# between the two versions AND identical to what is already in build/373307D9/;
# config.json matches too once the recorded "version" string is ignored.
# Regenerating the objdiff report over the 2224 units moved nothing: 43.67707%
# matched code, 29384/48383 functions, 968 complete units, and zero measure deltas
# in any unit or category (game 87.21148%, engine 76.40033%, sdk 0.021002889%).
#
# 1.10.0 -> 1.11.0: the XDK CRT register save/restore sleds are now named from their
# bodies (__savegprlr_25) instead of split as lbl_<addr>. dtk had the names all along
# and discarded them. Not PDB-gated, so unlike 1.10.0 this one DOES reach DC3.
# build/373307D9/config.json already records 1.11.0 -- the shared jeff binary was
# rebuilt and this tree re-split against it -- so this pin bump is documentation
# catching up to a split that already happened, and it cannot itself force a
# re-split: load_build_config() only drops config.json when the RECORDED version is
# older than the pin, and 1.11.0 < 1.11.0 is false. Bumping it re-arms that gate for
# the next release, which is the whole reason the v1.9.2 pin was worth fixing.
#
# 1.11.0 -> 1.12.0 (jeff c0cc506, deployed 2026-08-13): write_coff no longer emits a
# PpcRel14 for a branch whose destination never leaves the emitted section. The
# tracker walks past function_end (tracker.rs:503) and was minting relocation
# records for intra-function branches; the writer now requires the destination to
# leave BOTH the ObjInfo section and the COMDAT region. REL14 records go 8 -> 0 on
# this project (rb3-xenon 650 -> 17, cea 3 -> 0), and the 17 survivors are correct
# keeps guarded by two discriminating negative controls.
#
# This one DOES reach DC3 and it changes objects, so unlike 1.10.0 the pin bump is
# not documentation catching up -- it is the gate that forces the re-split. Parity
# measured before deploying (jeff docs/sessions/2026-08-12-splitter-reloc-addend/
# INTEGRATION.md): staging self-check fired on 8,983 objects across three projects;
# the ONLY object-level difference in all 195 changed objects is removed REL14
# records -- 0 added, 0 layout/data/symbol changes; per-symbol report movement is
# 1 up / 21 up / 0 down with zero unexplained; and the `normalized == 100`
# population moves by +0 on both games. Not cosmetic: the spurious records made the
# MSVC linker rewrite 7 DC3 branch instructions away from their retail encoding,
# and the 1.12.0 image carries the retail words.
#
# 1.12.0 -> 1.13.0: two splitter correctness fixes, co-measured before deploy
# (decomp-bench archive/runs/2026-08-13-jeff-combined-deploy-gate/).
#   * DS-form decode, both sides: the analysis side read a DS-form load/store
#     displacement as the full low halfword, when bits [1:0] are an opcode
#     extension -- so it anchored two bytes inside the real datum -- and the
#     writer zeroed those same two bits, rewriting the opcode. Both fixed
#     together; fixing either alone regresses.
#   * The relocation tracker walked past function_end into the NEXT function and
#     judged its branches against the PREVIOUS function's bounds, minting
#     spurious PpcRel14 records. Fixed at the cause, plus an
#     instruction-derived COMDAT keep-back and a COMDAT-NESTING fix in two
#     containment lookups (nested regions were resolved by a nearest-region
#     query, which both held functions back spuriously and dropped needed
#     relocations).
# Measured object movement, exact -- the split is deterministic, six control
# runs 3084/3084 identical: dc3 3 of 2223, rb3-xenon 64 of 3084, cea 2 of 3675.
# Zero interaction between the two fixes on all three projects.
config.dtk_tag = "v1.13.0"
config.objdiff_tag = "v4.2.2"  # freeqaz/objdiff fork release (linux-x86_64 asset)
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

config.wibo_path_map = (
    f"e:/lazer_build_gmc1/system/src/={Path('src/system').absolute()};"
    f"e:/lazer_build_gmc1/lazer/src/={Path('src/lazer').absolute()}"
)

config.shift_jis = False
config.progress_all = False

# Precompiled header: covers ~370 files (42% of codebase) that include obj/Object.h
config.pch_header = "decomp_pch.h"
config.pch_source = Path("src/system/decomp_pch.cpp")
config.pch_eligible_dirs = {
    "rndobj", "hamobj", "char", "synth", "dsp", "ui", "flow", "gesture",
    "world", "meta", "obj", "os", "utl", "movie",
}

# *** ONE PASS RUNS INSIDE THE COMPILE EDGE, NOT ONLY AFTER IT. ***
# Everything in `post-compile` below hangs off a phony keyed on `all_source`,
# which `ninja <one>.obj` never reaches.  That is a documented gap for the five
# name/storage patchers (a per-target object is raw, and
# verify_objs_patched.py --verify-manifest is what detects a tree left that
# way).  For the build-metadata pass it was not merely a gap: MSVC stamps the
# wall clock into every object it writes, so a per-target rebuild of UNCHANGED
# source produced a DIFFERENT hash every time -- measured on TypeProps.obj as
# dfeda314... then e14ac6e8... .  Every control of the shape "I edited
# something, rebuilt, the object hash moved, therefore the mechanism works"
# passed whether or not the mechanism worked.
#
# So the metadata pass is appended to all three MSVC compile rules (see
# `obj_postprocess_cmd` in tools/project.py).  It is chained with `&&`: a
# failure fails the compile edge.  The batch pass below STAYS -- it is
# idempotent once the compile edge has run, and it is the only thing that
# covers objects which never went through a compile edge (the .obj files
# create_data_stubs.py mints).  Neither location is redundant.
#
# Absolute path because the compile command `cd`s into the source directory
# first (for __FILE__), and `{obj}` is substituted with the object's own
# absolute path.
#
# The script is deliberately NOT an implicit input of the 956 compile edges --
# that would recompile the whole tree on every edit to it.  Editing it does
# re-fire the `--batch` stamp below (configure.py derives that dependency from
# the step's own `cmd`), and `verify_objs_patched.py --check` then asserts the
# whole tree is a fixed point of the new logic, so a changed pass cannot leave
# a stale object behind unnoticed; only the per-target path lags until the next
# full `ninja`.
config.obj_postprocess_cmd = (
    f"$python {Path('scripts/obj_build_metadata_patcher.py').resolve()} "
    f"--obj {{obj}}"
)

# Post-compile patchers: run after all .obj files are compiled, before linking.
# These patch decomp .obj files to match original binary patterns.
#
# The five obj patchers each read-modify-write the SAME build/**/*.obj set, so
# they MUST be serialized: with only `order_only: all_source` ninja runs them
# concurrently, and a reader can catch a file another patcher is rewriting
# (transiently truncated -> string-table read past EOF crashed a fresh build
# 2026-07-11), or two patchers can lose each other's writes (read/read/write/
# write -> silently dropped symbol patches, nondeterministic match output).
# Each stamp is an implicit input of the next, fixing the order: data stubs
# are generated first (so patchers see a stable file set), then the patchers
# run one at a time.
#
# *** `all_source` IS AN IMPLICIT INPUT, NOT AN ORDER-ONLY ONE. ***
# Order-only constrains ORDER but NEVER marks an edge dirty, so until 2026-08-09
# nothing re-ran a patcher behind a recompiled object.  Two ways in:
#   * an incremental build -- one edited .cpp recompiles one .obj, which arrives
#     UNPATCHED while every stamp is still "current";
#   * ANY SECOND `ninja`, on any tree -- an in-place rewrite makes the object
#     newer than the mtime ninja stored beside its `deps = msvc` record, so the
#     next build prints `stored deps info out of date` and RECOMPILES it.
# The second one made "patches wiped" the STEADY STATE of this build: measured
# 2026-08-09 at dc3 21f7f331, build-then-`ninja` reverted 277 of 980 objects,
# no patcher re-fired, the third build said "no work to do", and the
# ADDR_IDENTITY witness derived from that tree fell 60 -> 53 pairings with
# nothing failing anywhere.  As an implicit input, `all_source` propagates the
# objects' mtimes through the phony, so an object newer than a stamp re-triggers
# that patcher.  (Same defect and same fix as rb3-xenon bd6cefa1, 2026-08-02.)
#
# ! THIS ONLY CONVERGES BECAUSE EVERY PATCHER RESTORES THE OBJECT'S mtime
# (scripts/obj_patch_io.py).  A patcher that bumped the mtime would make ninja
# recompile the object it just patched, which now re-triggers the patcher --
# an oscillation that never reaches "no work to do".  Do not remove it.
#
# The last step re-runs all five in dry-run and FAILS THE BUILD unless the tree
# is a fixed point of them, then records a content manifest so a single-TU
# compile that bypasses this graph entirely is detectable from outside.
stamp_dir = config.build_dir / config.version
config.custom_build_rules = [
    {
        "name": "run_script",
        "command": "$cmd && touch $out",
        "description": "$desc",
    },
]
config.custom_build_steps = {
    "post-compile": [
        {
            "outputs": str(stamp_dir / "data_stubs.stamp"),
            "rule": "run_script",
            # The whole post-compile chain reads the TARGET objects in
            # build/<v>/obj/**, whose COFF symbol names dtk writes from
            # config/<v>/symbols.txt. Those objects are an UNDECLARED build
            # output, and the split deliberately PRESERVES config.json's mtime
            # when its content is unchanged -- which is exactly the case for a
            # symbols.txt edit. So without this edge a symbols.txt change
            # rewrote every target object while every patcher stamp stayed
            # green, and the patchers kept writing the PREVIOUS names into our
            # objects. Measured 2026-08-31: correcting three anonymous-namespace
            # hashes in symbols.txt left our HolmesClient.obj still spelling
            # `?A0xbd0b8fef` against a target that now said `?A0x49b544a7`,
            # manufacturing a 9-function / -1,640-byte "regression" that was
            # purely a stale patch. `split_current_checked.stamp` is the right
            # dependency and not config.json: it is an `always` edge with
            # `restat`, so its output moves EXACTLY when a split moved and a
            # no-op ninja does not re-patch. The report edges already depend on
            # it; the patchers that feed those reports now do too.
            "implicit": [str(stamp_dir / "split_current_checked.stamp")],
            "variables": {
                "cmd": "python3 scripts/create_data_stubs.py",
                "desc": "GEN data-stub .obj files for lbl_* resolution",
            },
        },
        {
            "outputs": str(stamp_dir / "anon_ns_patched.stamp"),
            "rule": "run_script",
            "implicit": [str(stamp_dir / "data_stubs.stamp"), "all_source"],
            "variables": {
                "cmd": "python3 scripts/obj_anon_ns_patcher.py --batch --apply",
                "desc": "PATCH anonymous namespace hashes",
            },
        },
        {
            "outputs": str(stamp_dir / "dynamic_init_patched.stamp"),
            "rule": "run_script",
            "implicit": [str(stamp_dir / "anon_ns_patched.stamp"), "all_source"],
            "variables": {
                "cmd": "python3 scripts/obj_dynamic_init_patcher.py --batch --apply",
                "desc": "PATCH ??__E dynamic initializers STATIC->EXTERNAL",
            },
        },
        {
            "outputs": str(stamp_dir / "guard_patched.stamp"),
            "rule": "run_script",
            "implicit": [str(stamp_dir / "dynamic_init_patched.stamp"), "all_source"],
            "variables": {
                "cmd": "python3 scripts/obj_guard_patcher.py --batch --apply",
                "desc": "PATCH $S guard variables to match ??_B naming",
            },
        },
        {
            "outputs": str(stamp_dir / "bool_mangle_patched.stamp"),
            "rule": "run_script",
            "implicit": [str(stamp_dir / "guard_patched.stamp"), "all_source"],
            "variables": {
                "cmd": "python3 scripts/obj_bool_mangle_patcher.py --batch --apply",
                "desc": "PATCH bool parameter back-reference mangling",
            },
        },
        {
            "outputs": str(stamp_dir / "atexit_scope_patched.stamp"),
            "rule": "run_script",
            "implicit": [str(stamp_dir / "bool_mangle_patched.stamp"), "all_source"],
            "variables": {
                "cmd": "python3 scripts/obj_atexit_scope_patcher.py --batch --apply",
                "desc": "PATCH ??__F atexit scope counters (fuzzy match)",
            },
        },
        {
            # LAST of the rewrite passes, deliberately: it zeroes MSVC's
            # clock-derived COFF TimeDateStamp and CodeView S_OBJNAME
            # signature, and the earlier passes must not be able to reintroduce
            # them.  Without this, two rebuilds of identical source in one tree
            # produced 980 differing objects of 989 (issue #150) -- so every
            # byte-identity control this project runs over compiled objects was
            # comparing the wall clock.  Score-neutral: neither field is in a
            # section objdiff diffs.
            "outputs": str(stamp_dir / "build_metadata_normalized.stamp"),
            "rule": "run_script",
            "implicit": [str(stamp_dir / "atexit_scope_patched.stamp"), "all_source"],
            "variables": {
                "cmd": "python3 scripts/obj_build_metadata_patcher.py --batch --apply",
                "desc": "NORMALIZE clock-derived .obj build metadata",
            },
        },
        {
            # The patch passes are only half of the fix: a build that omits
            # one has to SAY SO.  This re-runs every patcher in dry-run and
            # fails the build unless the object tree is a fixed point of all
            # six, then records a content manifest so a consumer can detect a
            # single-TU compile that bypassed the graph entirely.
            #
            # `--check-compile-edge` runs FIRST and covers the one thing the
            # other two cannot see: the per-object metadata pass lives in the
            # MSVC compile rules, and a full build would still end up
            # normalised if that wiring were dropped (the batch pass follows
            # it), so `--check` would stay green while the per-target path went
            # back to measuring the wall clock.
            "outputs": str(stamp_dir / "objs_patched_verified.stamp"),
            "rule": "run_script",
            "implicit": [str(stamp_dir / "build_metadata_normalized.stamp"), "all_source"],
            "variables": {
                "cmd": "python3 scripts/verify_objs_patched.py "
                       "--check-compile-edge --check --emit",
                "desc": "VERIFY every .obj carries the post-compile patches",
            },
        },
        {
            # Catch "declared, called, defined nowhere" -- the defect class that
            # `-Wl,--no-undefined` on the native link is STRUCTURALLY unable to
            # see, because clang at -O2 can delete the only call site before the
            # linker runs.  JoypadSendKeepAlive lived here undetected for the
            # whole life of the file.  Reads the MSVC COFF symbol tables, which
            # keep the reference regardless of what any optimizer concluded;
            # ~1.3 s over 990 objects, no link, no extra build.
            #
            # Runs LAST, after every patcher, because the patchers rewrite
            # symbol NAMES (anonymous-namespace hashes, ??__E/??__F scope
            # counters) and this compares names.  Running it earlier would
            # compare a pre-patch spelling against a post-patch inventory.
            #
            # Deliberately advisory-on-improvement (exit 3, non-fatal here): a
            # newly IMPLEMENTED body shrinks the inventory, and implementing
            # bodies is the entire job of this repo -- failing every other
            # lane's build for it would get the gate disabled within a day.  A
            # NEW undefined symbol is exit 1 and does fail.
            "outputs": str(stamp_dir / "undefined_symbols_checked.stamp"),
            "rule": "run_script",
            "implicit": [str(stamp_dir / "objs_patched_verified.stamp"), "all_source"],
            "variables": {
                "cmd": "python3 scripts/check_undefined_decomp_symbols.py --check "
                       "|| test $$? -eq 3",
                "desc": "VERIFY no symbol is called but defined nowhere",
            },
        },
    ],
}

# *** A PATCHER'S OWN SOURCE IS AN INPUT TO ITS OWN EDGE. ***
# Every step above lists the previous stamp and `all_source`, so it re-fires when
# an OBJECT changes underneath it (the 2026-08-09 fix).  Nothing listed the
# SCRIPT, so changing what a patcher does re-fired nothing: its stamp was still
# newer than every object, ninja said "no work to do", and the tree kept the old
# patches.  Measured 2026-08-12 -- obj_anon_ns_patcher.py was rewritten to assign
# hashes per SYMBOL (+132 functions in its own worktree, where it had been run by
# hand), the branch merged, `ninja` reported success, and dc3 measured +0 of it:
# stamp 03:57, script 06:02.  A landed change that measures as nothing looks
# exactly like a lane that overstated itself, which is the expensive way to find
# this.
# Derived from each step's own `cmd` rather than hand-listed, so a new patcher
# cannot be added without its dependency.
for _step in config.custom_build_steps["post-compile"]:
    _cmd = _step["variables"]["cmd"]
    for _tok in _cmd.split():
        if _tok.endswith(".py") and (config.build_dir.parent / _tok).exists():
            _step.setdefault("implicit", []).append(_tok)

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
