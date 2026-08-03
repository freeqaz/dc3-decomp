"""COLOR register-map dump — capture post-COLOR register assignment events.

The MSVC x86-to-PPC cross-compiler (c2.dll) commits every physical register
assignment made by its linear-scan COLOR allocator through a single choke
point: ``color_set_reg_state`` (VA ``0x10bc4ba1``, RVA ``0x0c4ba1``). This
module traces that setter under GDB so a register-allocation divergence
becomes a *named fact* — "variable X got r30, target has r31" — instead of a
flat, bijection-tolerant fuzzy score.

This is task **C4** of THREAD_C (``docs/plans/il-witness/g2_push/THREAD_C_PLAN.md``
§3/§4); the breakpoint spec, operand read locations, and probe evidence this
module implements are recorded in
``docs/plans/il-witness/g2_push/V1_COLOR_DUMP_NOTES.md`` (task C2). Read that
doc before touching the addresses/offsets below.

Correctness boundary (binds this whole module): this is an **observation**
tool. It reads the real c2.dll's own state writes and reports named
divergence facts for aiming — it never licenses a source edit and is never
itself the judge. Every proposal it conditions is still graded byte-exact
against the TARGET object by ``objdiff`` (``score_frontier_target.py``
discipline), never against a heuristic or a base-repro comparison.

Usage:
    from tools.compiler_trace.color_map import trace_color_map
    trace = trace_color_map(Path("src/system/obj/Foo.cpp"))
    for ev in trace.events:
        print(f"COLORSET #{ev.index}: idx={ev.phys_idx} reg={ev.reg_name()}")
"""

from __future__ import annotations

import os
import re
import subprocess
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

from .invoker import CompilerInvoker, PROJECT_ROOT, C2_IMAGE_BASE
from .bsf_trace import WIBO_32, WIBO_LOADER_BP, _parse_function_info

# ---------------------------------------------------------------------------
# Addresses (V1_COLOR_DUMP_NOTES.md §1) — c2.dll VAs at preferred base
# 0x10B00000; RVA = VA - base. Verified against the in-tree
# build/compilers/X360/16.00.11886.00/c2.dll (MSVC 16.00.11886, build 278379).
# ---------------------------------------------------------------------------

# color_set_reg_state ENTRY — the single choke point every COLOR physical
# register commit flows through. `ecx` = phys index (authoritative), `edx` =
# vreg/live-range node ptr, BOTH already in registers at entry (no field
# dereference needed to read the index — this is why this site was chosen
# over color_select_reg / color_assign_regs; see notes §1 "Why not...").
COLOR_SETTER_RVA = 0x0C4BA1
COLOR_SETTER_VA = C2_IMAGE_BASE + COLOR_SETTER_RVA

# Caller RVA of the `color_select_reg`-driven primary allocation path (notes
# §1 table). Other caller RVAs reaching the same setter are spill / recolor /
# pre-color commits — a different phase, NOT folded into "primary alloc"
# (notes §5 caveat 3: mixing phases inflates the distinct-assignment count on
# functions that spill).
PRIMARY_ALLOC_CALLER_RVA = 0x0C6031

# Register-class word (`(cw >> 12) & 0xf` at node +0x10, notes §2) — GPR is
# probe-confirmed (idx -> r(idx-1)); FPR/VMX bases are carried from
# COLOR_RE.md's static RE only and are UNCONFIRMED by any probe (notes §5
# caveat 2). `reg_name()` therefore only names GPRs; FPR/VMX callers get the
# raw index + class tag, never a fabricated f*/vr* name.
CLASS_GPR = 1
CLASS_FPR = 5
CLASS_VMX = 0xC
_CLASS_NAMES = {CLASS_GPR: "gpr", CLASS_FPR: "fpr", CLASS_VMX: "vmx"}

# Parses the printf format emitted by `_generate_gdb_script` below (matches
# the C2 probe transcript in V1_COLOR_DUMP_NOTES.md §4 exactly):
#   COLORSET #1: caller=0x0c6031 idx=32 node=0x6c28f5b8 lrreg=78 hint=0x10c2f208 cls=1
_COLORSET_RE = re.compile(
    r"COLORSET #(\d+): caller=0x([0-9a-f]+) idx=(-?\d+) node=0x([0-9a-f]+) "
    r"lrreg=(-?\d+) hint=0x([0-9a-f]+) cls=(\d+)"
)


@dataclass
class ColorEvent:
    """A single COLOR register-assignment commit (one `color_set_reg_state` call)."""

    index: int  # sequential event number (1-based)
    caller_rva: int  # return address RVA in c2.dll (phase tag)
    phys_idx: int  # AUTHORITATIVE physical register index (from $ecx)
    node_ptr: int  # vreg/live-range node pointer (from $edx)
    lr_id: int  # live-range/symbol id, node+0x1c (NOT the phys index — footgun,
    #             see notes §5 caveat 1: this field is easy to mistake for a
    #             register but it is a per-LR identifier, stable but not a reg)
    hint: int  # coalescing hint: ptr to a register descriptor, or 0
    cls: int  # register class word, node+0x10 bits 15:12

    @property
    def class_name(self) -> str:
        return _CLASS_NAMES.get(self.cls, f"cls{self.cls:#x}")

    @property
    def is_primary_alloc(self) -> bool:
        """True if this commit came from the `color_select_reg`-driven path."""
        return self.caller_rva == PRIMARY_ALLOC_CALLER_RVA

    def reg_name(self) -> Optional[str]:
        """Physical register name, or None if this class is unconfirmed.

        GPR (`cls == 1`): `idx -> r(idx-1)`, probe-confirmed (notes §1/§4:
        idx 32->r31 first, distinct {28..32} == {r27..r31} == the
        `__savegprlr_27` save set). FPR/VMX bases are documented in
        `COLOR_RE.md` but NOT probe-confirmed here — per notes caveat 2,
        emit raw index + class instead of a possibly-wrong name.
        """
        if self.cls == CLASS_GPR:
            n = self.phys_idx - 1
            if 0 <= n <= 31:
                return f"r{n}"
        return None


@dataclass
class ColorMapTrace:
    """Complete COLOR assignment-event trace from a single compilation."""

    source: Path
    events: list[ColorEvent] = field(default_factory=list)

    @property
    def total_events(self) -> int:
        return len(self.events)

    @property
    def primary_events(self) -> list[ColorEvent]:
        """Events from the `color_select_reg`-driven primary allocation path."""
        return [e for e in self.events if e.is_primary_alloc]

    def events_by_caller(self) -> dict[int, list[ColorEvent]]:
        """Group events by caller RVA (allocation phase)."""
        groups: dict[int, list[ColorEvent]] = {}
        for ev in self.events:
            groups.setdefault(ev.caller_rva, []).append(ev)
        return groups

    def partition_by_function(
        self, asm_lines: list[str]
    ) -> dict[str, "ColorMapTrace"]:
        """Partition the primary-alloc events by function using an /FAs listing.

        Mirrors `BSFTrace.partition_by_function` (bsf_trace.py): parses
        PROC NEAR/ENDP markers + callee-saved counts in source order, then
        walks the primary-alloc event stream consuming events until N
        *distinct* physical-register indices have been seen per function
        (dynamic reuse of an already-assigned index, e.g. a dead temp's slot
        being overwritten, does not count as a new distinct assignment — see
        notes §3, "log assignments only").

        Falls back to {'__all__': self} if partitioning fails (no function
        info, or no primary-alloc events at all — e.g. an all-spill fixture).
        """
        func_info = _parse_function_info(asm_lines)
        if not func_info:
            return {"__all__": self}

        primary = self.primary_events
        if not primary:
            return {"__all__": self}

        result: dict[str, ColorMapTrace] = {}
        ev_idx = 0

        for func_name, n_callee_saved in func_info:
            if n_callee_saved == 0:
                result[func_name] = ColorMapTrace(source=self.source, events=[])
                continue

            func_events: list[ColorEvent] = []
            seen_idx: set[int] = set()
            distinct_count = 0

            while ev_idx < len(primary) and distinct_count < n_callee_saved:
                ev = primary[ev_idx]
                func_events.append(ev)
                if ev.phys_idx not in seen_idx:
                    seen_idx.add(ev.phys_idx)
                    distinct_count += 1
                ev_idx += 1

            result[func_name] = ColorMapTrace(source=self.source, events=func_events)

        if ev_idx < len(primary):
            result["__remainder__"] = ColorMapTrace(
                source=self.source, events=primary[ev_idx:]
            )

        return result

    def var_map(self, source: Path, function_name: str) -> dict[str, str]:
        """Correlate this (single-function) trace's distinct primary-alloc
        assignments with source declaration order.

        Mirrors `regmap_solver.solve_register_order`'s colorings<->decl_names
        zip (same declaration-order assumption — the i-th distinct primary
        assignment corresponds to the i-th declared local). Only assignments
        with a confirmed `reg_name()` (GPR) are emitted; unconfirmed classes
        are silently omitted rather than guessed (notes caveat 2).

        Returns {} if declaration names could not be extracted (e.g. the
        `decomp_synth` extractor is not importable in this environment — see
        `regmap_solver._extract_declaration_names`'s fallback).
        """
        from .regmap_solver import _extract_declaration_names

        decl_names = _extract_declaration_names(source, function_name)
        if not decl_names:
            return {}

        distinct: list[ColorEvent] = []
        seen_idx: set[int] = set()
        for ev in self.primary_events:
            if ev.phys_idx not in seen_idx:
                seen_idx.add(ev.phys_idx)
                distinct.append(ev)

        mapping: dict[str, str] = {}
        for name, ev in zip(decl_names, distinct):
            reg = ev.reg_name()
            if reg:
                mapping[name] = reg
        return mapping

    def diff_target(
        self, source: Path, function_name: str, objdiff_json: dict
    ) -> list[dict]:
        """Per-variable {var, ours, target, divergent} rows vs an objdiff target.

        `ours` comes from this trace's `var_map()`. `target` is derived from
        `regmap_solver.extract_target_register_map` (target_reg -> base_reg
        for every diff-breakdown register operand where they differ):
        inverted to base_reg -> target_reg, then looked up by `ours`'s
        register name. This is a register-NAME correlation (aiming data
        only) — it is not itself a witness; every proposal it conditions
        must still be graded via splice->ninja->objdiff against the TARGET
        object (correctness boundary, module docstring).
        """
        from .regmap_solver import extract_target_register_map

        ours = self.var_map(source, function_name)
        reg_map = extract_target_register_map(objdiff_json)  # target_reg -> base_reg

        base_to_target: dict[str, str] = {}
        for target_reg, base_reg in reg_map.items():
            base_to_target.setdefault(base_reg, target_reg)

        rows: list[dict] = []
        for var, our_reg in sorted(ours.items()):
            target_reg = base_to_target.get(our_reg)
            rows.append(
                {
                    "var": var,
                    "ours": our_reg,
                    "target": target_reg,
                    "divergent": target_reg is not None and target_reg != our_reg,
                }
            )
        return rows


def select_function_key(
    traces_by_func: dict[str, ColorMapTrace], function_name: str
) -> Optional[str]:
    """Resolve a (possibly-substring) function name to a partition key.

    Mirrors `asm_diff.extract_function`'s matching rule (exact PROC name, or
    substring, case-sensitive first then case-insensitive fallback).
    """
    if function_name in traces_by_func:
        return function_name
    for key in traces_by_func:
        if function_name in key:
            return key
    lowered = function_name.lower()
    for key in traces_by_func:
        if lowered in key.lower():
            return key
    return None


def _generate_gdb_script(
    source: Path,
    obj_output: Path,
    extra_flags: list[str] | None = None,
) -> str:
    """Generate a GDB batch script tracing every `color_set_reg_state` call.

    Clone of `bsf_trace._generate_gdb_script`'s proven harness (32-bit wibo,
    loader-bp on c2.dll's preferred base, `finish` before planting the
    breakpoint so relocations are final) — see that function's comments for
    the full rationale. Only the traced site and the printf payload differ.
    """
    invoker = CompilerInvoker()
    cmd = invoker.base_command(source, obj_output, extra_flags)
    cl_args = " ".join(cmd[1:]).replace("\\", "\\\\")

    lines = [
        "# Auto-generated COLOR assignment-event trace script",
        "set confirm off",
        "set pagination off",
        "set startup-with-shell off",
        "set debuginfod enabled off",
        'set libthread-db-search-path ""',
        "set print elements 0",
        "",
        f"file {WIBO_32}",
        f"set args {cl_args}",
        "",
        f"break {WIBO_LOADER_BP} if header32.imageBase == 0x{C2_IMAGE_BASE:08x}",
        "run",
        "",
        f"if header32.imageBase == 0x{C2_IMAGE_BASE:08x}",
        "  set $c2base = (unsigned int)allocatedBase",
        '  printf "### c2 mapped: base=0x%08x preferred=0x%08x\\n", $c2base, header32.imageBase',
        "else",
        '  printf "### ERROR: loader bp stopped on wrong module (preferred=0x%08x)\\n", header32.imageBase',
        "  quit 1",
        "end",
        "delete 1",
        "",
        "# See bsf_trace._generate_gdb_script: `finish` before planting the sw bp",
        "# so the image bytes are final (relocations applied, no memcpy-over risk).",
        "finish",
        "",
        f"set $setter = $c2base + 0x{COLOR_SETTER_RVA:x}",
        'printf "### setter byte at 0x%08x = 0x%02x\\n", $setter, *(unsigned char*)$setter',
        "break *$setter",
        "",
        "set $n = 0",
        "set $done = 0",
        "while $done == 0",
        "  continue",
        "  if $_isvoid($eip)",
        "    set $done = 1",
        "  else",
        "    if $eip == $setter",
        "      set $n = $n + 1",
        "      set $idx = $ecx",
        "      set $node = $edx",
        "      set $crva = (*(unsigned int*)$esp) - $c2base",
        "      set $lrreg = *(int*)($node + 0x1c)",
        "      set $hint = *(unsigned int*)($node + 0x30)",
        "      set $cw = *(unsigned short*)($node + 0x10)",
        "      set $cls = ($cw >> 12) & 0xf",
        '      printf "COLORSET #%d: caller=0x%06x idx=%d node=0x%08x lrreg=%d hint=0x%08x cls=%d\\n", $n, $crva, $idx, $node, $lrreg, $hint, $cls',
        "    else",
        "      set $done = 1",
        "    end",
        "  end",
        "end",
        "",
        'printf "### Total COLORSET events: %d\\n", $n',
        "quit",
    ]
    return "\n".join(lines)


def _parse_color_map_output(output: str, source: Path) -> ColorMapTrace:
    """Parse GDB output into a ColorMapTrace."""
    trace = ColorMapTrace(source=source)
    for match in _COLORSET_RE.finditer(output):
        trace.events.append(
            ColorEvent(
                index=int(match.group(1)),
                caller_rva=int(match.group(2), 16),
                phys_idx=int(match.group(3)),
                node_ptr=int(match.group(4), 16),
                lr_id=int(match.group(5)),
                hint=int(match.group(6), 16),
                cls=int(match.group(7)),
            )
        )
    return trace


def trace_color_map(
    source: Path,
    extra_flags: list[str] | None = None,
    timeout: int = 300,
    verbose: bool = False,
) -> ColorMapTrace:
    """Compile source under GDB and capture all COLOR assignment events.

    Args:
        source: Path to C++ source file.
        extra_flags: Additional cl.exe flags.
        timeout: GDB timeout in seconds (default: 5 minutes).
        verbose: Print GDB output to stderr.

    Returns:
        ColorMapTrace with all captured assignment events.
    """
    if not WIBO_32.exists():
        raise FileNotFoundError(
            f"32-bit wibo not found at {WIBO_32}. "
            f"Build with: cd {WIBO_32.parents[2]} && mkdir -p build/debug && "
            "cd build/debug && cmake -DCMAKE_BUILD_TYPE=Debug ../.. && make"
        )

    with tempfile.NamedTemporaryFile(
        suffix=".gdb", prefix="color_map_", dir="/tmp/claude", delete=False, mode="w"
    ) as gdb_f:
        obj_path = Path(
            tempfile.mktemp(suffix=".obj", prefix="color_map_", dir="/tmp/claude")
        )
        script = _generate_gdb_script(source, obj_path, extra_flags)
        gdb_f.write(script)
        gdb_path = Path(gdb_f.name)

    wibo_path_map = (
        f"e:/lazer_build_gmc1/system/src/={PROJECT_ROOT}/src/system;"
        f"e:/lazer_build_gmc1/lazer/src/={PROJECT_ROOT}/src/lazer"
    )
    env = os.environ.copy()
    env["WIBO_PATH_MAP"] = wibo_path_map

    try:
        result = subprocess.run(
            ["gdb", "-batch", "-x", str(gdb_path)],
            cwd=PROJECT_ROOT,
            capture_output=True,
            text=True,
            timeout=timeout,
            env=env,
        )

        output = result.stdout + result.stderr
        if verbose:
            import sys

            print(output, file=sys.stderr)

        trace = _parse_color_map_output(output, source)

        if trace.total_events == 0:
            if "not in executable format" in output:
                raise RuntimeError(f"Wrong wibo binary format: {output[:500]}")
            if "### c2 mapped:" not in output:
                hint = ""
                if (
                    "No source file named" in output
                    or "No symbol" in output
                    or "### ERROR" in output
                ):
                    hint = (
                        " — debug-info/anchor drift: rebuild wibo debug with -g "
                        "(cmake -DCMAKE_BUILD_TYPE=Debug) or update WIBO_LOADER_BP "
                        "to the `executable.imageBase = allocatedBase;` line"
                    )
                raise RuntimeError(
                    f"c2.dll never hit wibo PE-mapping breakpoint ({WIBO_LOADER_BP})"
                    f"{hint}. GDB return code: {result.returncode}\n"
                    f"Last 500 chars of output: {output[-500:]}"
                )
            raise RuntimeError(
                "c2.dll mapped but 0 COLORSET events captured — compile likely "
                "failed (bad flags/source) or COLOR_SETTER_RVA 0x0c4ba1 is stale "
                f"for this c2.dll. GDB return code: {result.returncode}\n"
                f"Last 500 chars of output: {output[-500:]}"
            )

        return trace

    finally:
        gdb_path.unlink(missing_ok=True)
        obj_path.unlink(missing_ok=True)


def _compile_asm_listing(
    source: Path, extra_flags: list[str] | None = None
) -> list[str]:
    """Compile `source` with /FAs and return the listing lines.

    Used by the CLI's `--function` mode to get callee-saved counts (for
    partitioning) and declaration-order correlation input.
    """
    inv = CompilerInvoker()
    with tempfile.TemporaryDirectory(dir="/tmp/claude", prefix="color_map_asm_") as td:
        out_dir = Path(td)
        inv.compile_with_asm(source, out_dir, extra_flags=extra_flags, listing_type="/FAs")
        asm_path = out_dir / (source.stem + ".asm")
        if not asm_path.exists():
            return []
        return asm_path.read_text(errors="replace").splitlines()


def _run_objdiff(symbol: str) -> dict:
    """Run objdiff-cli for `symbol` and return the parsed JSON.

    Same `--build --incremental -f json -o <file>` pattern as
    `regmap_solver.cmd_bsf_solve` — routes objdiff's JSON to an explicit
    output file so ninja's stdout build chatter never lands on the same
    stream as the JSON (the `bsf-solve` oracle bug fixed in `f9a301ac`).
    """
    import json
    import sys

    with tempfile.NamedTemporaryFile(
        mode="r", suffix=".json", prefix="color_map_objdiff_", delete=True
    ) as jf:
        result = subprocess.run(
            [
                str(PROJECT_ROOT / "bin" / "objdiff-cli"),
                "diff",
                symbol,
                "--include-instructions",
                "--build",
                "--incremental",
                "-f",
                "json",
                "-o",
                jf.name,
            ],
            cwd=PROJECT_ROOT,
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            print(f"objdiff failed: {result.stderr}", file=sys.stderr)
            raise RuntimeError(f"objdiff-cli exited {result.returncode}")
        jf.seek(0)
        out = jf.read()
    if not out.strip():
        raise RuntimeError("objdiff produced no JSON output")
    return json.loads(out)


def cmd_color_map(args) -> None:
    """Entry point for the `color-map` subcommand."""
    import json as json_mod
    import sys

    source = Path(args.source).resolve()
    extra_flags = (
        args.extra_flags.split()
        if getattr(args, "extra_flags", None)
        else None
    )
    verbose = getattr(args, "verbose", False)
    function_name = getattr(args, "function", None)
    diff_target_symbol = getattr(args, "diff_target", None)
    json_output = getattr(args, "json_output", False)

    print(f"Tracing COLOR assignment events for {source.name}...", file=sys.stderr)
    trace = trace_color_map(source, extra_flags=extra_flags, verbose=verbose)
    n_primary = len(trace.primary_events)
    print(
        f"Captured {trace.total_events} COLORSET events ({n_primary} primary-alloc)",
        file=sys.stderr,
    )

    if not function_name:
        for ev in trace.events:
            print(
                f"COLORSET #{ev.index}: caller=0x{ev.caller_rva:06x} "
                f"idx={ev.phys_idx} reg={ev.reg_name()} lr_id={ev.lr_id} "
                f"hint=0x{ev.hint:x} cls={ev.class_name}"
            )
        return

    asm_lines = _compile_asm_listing(source, extra_flags=extra_flags)
    traces_by_func = trace.partition_by_function(asm_lines)
    key = select_function_key(traces_by_func, function_name)
    if key is None or key in ("__all__", "__remainder__"):
        print(
            f"Function {function_name!r} not found in partitioned trace "
            f"(known: {sorted(traces_by_func)})",
            file=sys.stderr,
        )
        sys.exit(1)

    per_function = traces_by_func[key]
    var_map = per_function.var_map(source, function_name)
    print(f"\nVariable -> register map for {key}:")
    for var, reg in var_map.items():
        print(f"  {var} -> {reg}")

    if not diff_target_symbol:
        if json_output:
            print(json_mod.dumps({"function": key, "var_map": var_map}))
        return

    objdiff_json = _run_objdiff(diff_target_symbol)
    rows = per_function.diff_target(source, function_name, objdiff_json)
    print(f"\nDiff vs TARGET ({diff_target_symbol}):")
    for row in rows:
        marker = " <-- DIVERGENT" if row["divergent"] else ""
        print(f"  {row['var']}: ours={row['ours']} target={row['target']}{marker}")

    if json_output:
        print(json_mod.dumps({"function": key, "var_map": var_map, "diff_target": rows}))


def _build_arg_parser():
    """Standalone argparse for this module.

    NOT wired into `__main__.py`'s central dispatcher — task C4's hygiene
    budget is "new untracked files only; zero edits to tracked files"
    (THREAD_C_PLAN.md §5), and `__main__.py` is tracked. Invoke directly:
    `python -m tools.compiler_trace.color_map <source> [--function F] ...`.
    Wiring a `color-map` subcommand into `__main__.py` alongside `bsf-trace`
    et al. is a one-block deferred integration step for whoever next commits
    changes to that file (the argparse block + dispatch arm are the same
    shape as `bsf-trace`'s; see the `cmd_color_map(args)` entry point above,
    which already expects the args namespace this parser produces).
    """
    import argparse

    p = argparse.ArgumentParser(
        prog="color_map",
        description="Trace COLOR register-assignment events during compilation",
    )
    p.add_argument("source", help="Source file to compile")
    p.add_argument(
        "-f", "--function", help="Isolate one function (mangled name or substring)"
    )
    p.add_argument(
        "--diff-target",
        help="Mangled symbol to diff against via objdiff (requires --function)",
    )
    p.add_argument("--extra-flags", help="Additional cl.exe flags (space-separated)")
    p.add_argument("--verbose", action="store_true", help="Print raw GDB output")
    p.add_argument("--json", action="store_true", dest="json_output", help="Output JSON")
    return p


if __name__ == "__main__":
    cmd_color_map(_build_arg_parser().parse_args())
