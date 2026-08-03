"""Scheduler emission-schedule dump + diff (Stage V1b, task C5).

The MSVC x360 backend (c2.dll) runs a Xenon list-scheduler (`fcn.10b71d8f`,
PASS group G5P10) that chooses the per-instruction emission order. On the
82 genuinely-stuck near-miss functions the fuzzy gradient is flat exactly
where the *emission order* differs from the target (a `Box::Volume`-class
near-miss: the scheduler picked `dz - ...` before `dy - ...`, or vice versa).
Wave-1a attacked those blind. Stage V1b makes the schedule *observable*.

Rung verdict (C3, `docs/plans/il-witness/g2_push/V1_SCHED_DUMP_NOTES.md`):
**Rung 0 = GO.** c2 already *exports* its own emission schedule (issue cycle +
pipe slot + dependency edges + stall summary) into the `/FA*` code listing
when a single `.data` gate (`DAT_10c2e988`, RVA ``0x12e988``) is flipped to 1
at runtime. There is no CLI token for that gate and no code writes it, so we
flip it with a GDB `.data` write after the loader has finished mapping c2.dll
(NEVER editing c2.dll on disk). This module is therefore a **parser** of the
listing c2 itself emits — byte-exact by construction (the compiler IS the
judge); Rung 1 (reversing the ready-list container) was ruled NO-GO / not
needed.

Two commands:

    python -m tools.compiler_trace.sched_trace sched-trace <src> --function F
    python -m tools.compiler_trace.sched_trace sched-diff <srcA> <srcB> --function F

`sched-diff` aligns the two variants' annotated instruction streams and reports
the **first divergent pick** — the `Box::Volume` "at which pick did the order
diverge" question in one command.

CORRECTNESS BOUNDARY (THREAD_C_PLAN §7, V1_SCHED_DUMP_NOTES §5): this dump
reflects what c2 scheduled for *the source we fed it*. We do NOT have the
target's annotated listing (only the target `.obj` bytes), so `sched-diff`
compares **our own two variants (A vs B)** to *name why their emission orders
differ* and steer a source edit — it is an aiming instrument, never a witness.
The sole G2 judge stays `score_frontier_target.py` (recompiled base COMDAT
`raw_eq && reloc_eq` vs the TARGET reference obj). No dump output is ever
adopted as truth.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

from .invoker import (
    CompilerInvoker,
    PROJECT_ROOT,
    C2_IMAGE_BASE,
    _make_cl_path,
)
# Reuse the proven 32-bit-wibo + loader-bp scaffold from the BSF tracer verbatim.
from .bsf_trace import WIBO_32, WIBO_LOADER_BP, _MILOHAX_DIR

# ---------------------------------------------------------------------------
# c2.dll constants (from V1_SCHED_DUMP_NOTES.md §1)
# ---------------------------------------------------------------------------

# QXSTALLS schedule-annotation gate. `.data` default 0 (disabled); no code path
# writes it; there is no /QXSTALLS CLI token in this build. Runtime-only enable:
# GDB writes 1 here AFTER the loader `finish` (a pre-finish write lands in a
# shadow byte that gets memcpy'd over — same ordering rule as bsf_trace).
QXSTALLS_GATE_RVA = 0x12E988  # VA 0x10c2e988
# Listing flag that also arms the outer "/FA* listing active" gate (ctx+0xCD8
# bit0). /FAcs => code+source listing written to <srcstem>.cod in the /Fa dir.
LISTING_FLAG = "/FAcs"
LISTING_EXT = ".cod"


# ---------------------------------------------------------------------------
# Listing-format regexes (V1_SCHED_DUMP_NOTES.md §3)
# ---------------------------------------------------------------------------

_PROC_RE = re.compile(r"^(\S+)\s+PROC\s+NEAR")
_ENDP_RE = re.compile(r"^(\S+)\s+ENDP")

# Instruction line:  "  00000\t7d441850\t subf         r10,r4,r3"
#   group1 = COMDAT byte-offset (hex, emission ordinal)
#   group2 = 8-hex PPC encoding
#   group3 = mnemonic (lowercase — distinguishes from DW/DB data directives)
#   group4 = operands (may be empty, e.g. `blr`)
_INSN_RE = re.compile(
    r"^\s*([0-9a-f]+)\s+([0-9a-f]{8})\s+([a-z][a-z0-9._]*)\s*(.*?)\s*$"
)

# Annotation line: "; [I     2B] P10 S1 D1(from I 0A)"  or  "; [I     0A]"
#   group1 = issue cycle (decimal), group2 = slot (A|B), group3 = rest
_ANNO_RE = re.compile(r"^;\s*\[I\s+(\d+)([AB])\]\s*(.*?)\s*$")

# Producer dependency edge inside an annotation rest: "(from I 15B)"
_DEP_RE = re.compile(r"\(from\s+I\s+(\d+)([AB])\)")
# Stall code + count: "P10", "S1", "D1", and vector variants VQF/VQS/VQD.
# Anchored to a letter-run immediately followed by digits so it never matches
# the "I" of "(from I 0A)" (space-separated) or the "0A" cycle-slot token.
_STALL_RE = re.compile(r"\b([A-Z]{1,3})(\d+)\b")

# Stall-summary trailer row:  ";         P      2       20  Stalled for ..."
_STALL_SUMMARY_RE = re.compile(r"^;\s+([A-Z]{1,3})\s+(\d+)\s+(\d+)\s+(.+?)\s*$")


# ---------------------------------------------------------------------------
# Model
# ---------------------------------------------------------------------------


@dataclass
class SchedDep:
    cycle: int
    slot: str

    def __str__(self) -> str:
        return f"I{self.cycle}{self.slot}"


@dataclass
class SchedStall:
    code: str  # P (non-pipelined), S (structural), D (dependency), VQ* (vector)
    count: int

    def __str__(self) -> str:
        return f"{self.code}{self.count}"


@dataclass
class SchedPick:
    """One scheduled/emitted instruction (a scheduler 'pick').

    Node identity (V1_SCHED_DUMP_NOTES §4): the COMDAT byte-`off` is the stable,
    monotone-in-emission-order primary id; `(cycle, slot)` is the scheduler's
    chosen issue time; the payload `(mnem, ops, enc)` is the selected
    instruction; `deps` are the producer edges (the schedule DAG).
    """

    idx: int  # 0-based emission ordinal within the function
    off: int  # COMDAT byte-offset
    enc: str  # 8-hex PPC word
    mnem: str
    ops: str
    cycle: Optional[int] = None  # scheduler issue cycle (None if unannotated)
    slot: Optional[str] = None  # A | B
    stalls: list[SchedStall] = field(default_factory=list)
    deps: list[SchedDep] = field(default_factory=list)

    @property
    def payload(self) -> tuple[str, str]:
        """Instruction-selection identity, order-independent of schedule time."""
        return (self.mnem, self.ops)

    @property
    def annotated(self) -> bool:
        return self.cycle is not None

    def text(self) -> str:
        s = f"{self.off:#07x} {self.mnem} {self.ops}".rstrip()
        if self.annotated:
            s += f"  [I {self.cycle}{self.slot}]"
            if self.stalls:
                s += " " + " ".join(str(x) for x in self.stalls)
            if self.deps:
                s += " " + " ".join(f"(from {d})" for d in self.deps)
        return s


@dataclass
class SchedTrace:
    """Complete emission schedule for one function."""

    function: str
    source: Optional[Path] = None
    picks: list[SchedPick] = field(default_factory=list)
    stall_summary: list[tuple[str, int, int, str]] = field(default_factory=list)

    @property
    def n_picks(self) -> int:
        return len(self.picks)

    @property
    def n_annotated(self) -> int:
        return sum(1 for p in self.picks if p.annotated)


# ---------------------------------------------------------------------------
# Parser
# ---------------------------------------------------------------------------


def _parse_annotation_rest(rest: str) -> tuple[list[SchedStall], list[SchedDep]]:
    deps = [SchedDep(int(c), s) for c, s in _DEP_RE.findall(rest)]
    # Strip the (from I ..) spans before scanning stalls so no dep-internal
    # token can be mistaken for a stall code.
    stall_src = _DEP_RE.sub("", rest)
    stalls = [SchedStall(code, int(n)) for code, n in _STALL_RE.findall(stall_src)]
    return stalls, deps


def parse_listing(text: str, source: Optional[Path] = None) -> dict[str, SchedTrace]:
    """Parse a c2 `/FAcs` listing (QXSTALLS gate ON) into per-function traces.

    Instruction lines and their immediately-following `; [I ...]` annotation
    lines are paired (each emitted instruction has exactly one annotation).
    Robust to unannotated instructions (gate off / non-scheduled) — those keep
    cycle=None. Function regions are delimited by `PROC NEAR` .. `ENDP`; the
    `*** Stall summary ***` block that follows an `ENDP` is attached to that
    function.
    """
    traces: dict[str, SchedTrace] = {}
    cur: Optional[SchedTrace] = None
    in_stall_summary = False
    idx = 0

    for raw in text.splitlines():
        line = raw.rstrip("\n")

        m = _PROC_RE.match(line.strip())
        if m:
            cur = SchedTrace(function=m.group(1), source=source)
            traces[m.group(1)] = cur
            in_stall_summary = False
            idx = 0
            continue

        m = _ENDP_RE.match(line.strip())
        if m:
            # Stay 'cur' so the following stall-summary block attaches here,
            # but stop accepting instructions (they'd be the next function's
            # only after a new PROC anyway).
            in_stall_summary = True
            continue

        if cur is None:
            continue

        if in_stall_summary:
            sm = _STALL_SUMMARY_RE.match(line)
            if sm:
                cur.stall_summary.append(
                    (sm.group(1), int(sm.group(2)), int(sm.group(3)), sm.group(4))
                )
            continue

        # Annotation line — attach to the most recent still-unannotated pick.
        am = _ANNO_RE.match(line.strip())
        if am and cur.picks and not cur.picks[-1].annotated:
            p = cur.picks[-1]
            p.cycle = int(am.group(1))
            p.slot = am.group(2)
            p.stalls, p.deps = _parse_annotation_rest(am.group(3))
            continue

        im = _INSN_RE.match(line)
        if im:
            cur.picks.append(
                SchedPick(
                    idx=idx,
                    off=int(im.group(1), 16),
                    enc=im.group(2),
                    mnem=im.group(3),
                    ops=im.group(4).strip(),
                )
            )
            idx += 1

    return traces


def select_function(
    traces: dict[str, SchedTrace], function: Optional[str]
) -> SchedTrace:
    """Pick the requested function trace (exact or substring match)."""
    if not traces:
        raise ValueError("no functions found in listing (gate off / empty .cod?)")
    if function is None:
        if len(traces) == 1:
            return next(iter(traces.values()))
        names = ", ".join(traces)
        raise ValueError(f"--function required; listing has {len(traces)}: {names}")
    if function in traces:
        return traces[function]
    hits = [t for name, t in traces.items() if function in name]
    if len(hits) == 1:
        return hits[0]
    if not hits:
        raise ValueError(
            f"function {function!r} not in listing; have: {', '.join(traces)}"
        )
    raise ValueError(
        f"function {function!r} ambiguous; matches: "
        f"{', '.join(t.function for t in hits)}"
    )


# ---------------------------------------------------------------------------
# GDB-driven compile harness (runtime .data gate flip; no on-disk c2 edit)
# ---------------------------------------------------------------------------


def _generate_gdb_script(cl_args: str) -> str:
    """GDB batch script: map c2.dll, flip the QXSTALLS gate, run the compile.

    Cloned from the C3-verified probe recipe (V1_SCHED_DUMP_NOTES §1). The
    loader-bp / finish ordering is identical to bsf_trace: the `.data` write
    MUST happen after `finish` (sections copied + relocated) or it is lost.
    """
    # `set args` tokenizes backslash escapes; double every backslash so wibo
    # Z:\ paths survive verbatim (same fix as bsf_trace._generate_gdb_script).
    cl_args = cl_args.replace("\\", "\\\\")
    lines = [
        "set confirm off",
        "set pagination off",
        "set startup-with-shell off",
        "set debuginfod enabled off",
        'set libthread-db-search-path ""',
        "set print elements 0",
        f"file {WIBO_32}",
        f"set args {cl_args}",
        f"break {WIBO_LOADER_BP} if header32.imageBase == 0x{C2_IMAGE_BASE:08x}",
        "run",
        f"if header32.imageBase == 0x{C2_IMAGE_BASE:08x}",
        "  set $c2base = (unsigned int)allocatedBase",
        '  printf "### c2 mapped: base=0x%08x\\n", $c2base',
        "else",
        '  printf "### ERROR: loader bp on wrong module (preferred=0x%08x)\\n", header32.imageBase',
        "  quit 1",
        "end",
        "delete 1",
        # finish => c2 image bytes final (relocated). A pre-finish .data write
        # would be memcpy'd over. Same ordering rule as the BSF tracer.
        "finish",
        f"set $gate = $c2base + 0x{QXSTALLS_GATE_RVA:x}",
        '  printf "### gate before = %d\\n", *(int*)$gate',
        "set *(int*)$gate = 1",
        '  printf "### gate patched -> %d\\n", *(int*)$gate',
        "continue",
        "quit",
    ]
    return "\n".join(lines)


def run_sched_trace(
    source: Path,
    function: Optional[str] = None,
    extra_flags: Optional[list[str]] = None,
    timeout: int = 300,
    verbose: bool = False,
    keep_listing: bool = False,
) -> SchedTrace:
    """Compile `source` under GDB with the QXSTALLS gate on; parse the schedule.

    Returns the SchedTrace for `function` (or the sole function). The compile is
    a single small TU; nothing is written into the dc3 tree (obj + .cod go to a
    tmp dir). Raises RuntimeError on toolchain / gate failure.
    """
    if not WIBO_32.exists():
        raise FileNotFoundError(
            f"32-bit wibo not found at {WIBO_32} (needed for the GDB gate write). "
            f"Build: cd {_MILOHAX_DIR / 'wibo'} && mkdir -p build/debug && "
            "cd build/debug && cmake -DCMAKE_BUILD_TYPE=Debug ../.. && make"
        )
    source = source.resolve()

    workdir = Path(tempfile.mkdtemp(prefix="sched_", dir="/tmp/claude"))
    obj_path = workdir / (source.stem + ".obj")
    listing_path = workdir / (source.stem + LISTING_EXT)

    invoker = CompilerInvoker()
    fa_dir = _make_cl_path(workdir) + "\\"
    asm_flags = [LISTING_FLAG, f"/Fa{fa_dir}"] + (extra_flags or [])
    cmd = invoker.base_command(source, obj_path, extra_flags=asm_flags)
    script = _generate_gdb_script(" ".join(cmd[1:]))  # cmd[0] is wibo; drop it

    gdb_path = workdir / "sched_trace.gdb"
    gdb_path.write_text(script)

    env = os.environ.copy()
    env["WIBO_PATH_MAP"] = (
        f"e:/lazer_build_gmc1/system/src/={PROJECT_ROOT}/src/system;"
        f"e:/lazer_build_gmc1/lazer/src/={PROJECT_ROOT}/src/lazer"
    )
    try:
        result = subprocess.run(
            ["gdb", "-batch", "-x", str(gdb_path)],
            cwd=PROJECT_ROOT,
            capture_output=True,
            text=True,
            timeout=timeout,
            env=env,
        )
        out = result.stdout + result.stderr
        if verbose:
            print(out, file=sys.stderr)

        if not listing_path.exists():
            raise RuntimeError(
                "compile produced no .cod listing "
                f"({listing_path.name}); gdb rc={result.returncode}\n"
                f"last 800 chars:\n{out[-800:]}"
            )
        if "### gate patched -> 1" not in out:
            raise RuntimeError(
                "QXSTALLS gate was not confirmed patched to 1 — schedule "
                f"annotations would be absent. gdb rc={result.returncode}\n"
                f"last 800 chars:\n{out[-800:]}"
            )

        text = listing_path.read_text(errors="replace")
        traces = parse_listing(text, source=source)
        trace = select_function(traces, function)

        if trace.n_annotated == 0:
            raise RuntimeError(
                f"function {trace.function!r} parsed {trace.n_picks} instrs but "
                "0 schedule annotations — gate/listing-flag mismatch or the "
                "listing format drifted (see V1_SCHED_DUMP_NOTES §3)."
            )
        if keep_listing:
            print(f"# listing kept: {listing_path}", file=sys.stderr)
        return trace
    finally:
        if not keep_listing:
            for p in (gdb_path, obj_path, listing_path):
                p.unlink(missing_ok=True)
            try:
                workdir.rmdir()
            except OSError:
                pass


# ---------------------------------------------------------------------------
# sched-diff (first-divergent-pick report; V1_SCHED_DUMP_NOTES §4)
# ---------------------------------------------------------------------------


@dataclass
class SchedDivergence:
    """Result of aligning two variants' emission schedules."""

    function: str
    n_a: int
    n_b: int
    diverged: bool
    index: Optional[int] = None  # emission ordinal of the first divergent pick
    pick_a: Optional[SchedPick] = None
    pick_b: Optional[SchedPick] = None
    kind: str = ""  # payload | schedule-time | length | none

    def report(self) -> str:
        lines = [
            f"function: {self.function}",
            f"variant A picks: {self.n_a}   variant B picks: {self.n_b}",
        ]
        if not self.diverged:
            lines.append(
                "NO DIVERGENCE: both variants emit an identical instruction "
                "schedule (same picks, same issue order)."
            )
            return "\n".join(lines)
        lines.append(f"FIRST DIVERGENT PICK: emission index #{self.index} ({self.kind})")
        a = self.pick_a.text() if self.pick_a else "<none (A shorter)>"
        b = self.pick_b.text() if self.pick_b else "<none (B shorter)>"
        lines.append(f"  A: {a}")
        lines.append(f"  B: {b}")
        return "\n".join(lines)


def sched_diff(a: SchedTrace, b: SchedTrace) -> SchedDivergence:
    """Align two variants and report the first divergent pick.

    Positional alignment over the emission (byte-offset) order — the primary
    schedule ordinal per §4. The first index at which the selected instruction
    payload `(mnem, ops)` differs, or (payloads equal) the scheduler's chosen
    `(cycle, slot)` issue time differs, is the "first divergent pick": the
    exact place where the two source variants' emission orders part. For the
    `Box::Volume` fixture, swapping the two independent `dy = ..` / `dz = ..`
    statements flips which subtract is scheduled first, and this names it.

    This compares OUR OWN two variants (A vs B) — never the target (we don't
    have the target's annotated listing). It is an aiming instrument; the sole
    G2 judge stays `score_frontier_target.py`.
    """
    n = min(len(a.picks), len(b.picks))
    for i in range(n):
        pa, pb = a.picks[i], b.picks[i]
        if pa.payload != pb.payload:
            return SchedDivergence(
                function=a.function, n_a=len(a.picks), n_b=len(b.picks),
                diverged=True, index=i, pick_a=pa, pick_b=pb, kind="payload",
            )
        if (pa.cycle, pa.slot) != (pb.cycle, pb.slot):
            return SchedDivergence(
                function=a.function, n_a=len(a.picks), n_b=len(b.picks),
                diverged=True, index=i, pick_a=pa, pick_b=pb, kind="schedule-time",
            )
    if len(a.picks) != len(b.picks):
        i = n
        return SchedDivergence(
            function=a.function, n_a=len(a.picks), n_b=len(b.picks),
            diverged=True, index=i,
            pick_a=a.picks[i] if i < len(a.picks) else None,
            pick_b=b.picks[i] if i < len(b.picks) else None,
            kind="length",
        )
    return SchedDivergence(
        function=a.function, n_a=len(a.picks), n_b=len(b.picks),
        diverged=False, kind="none",
    )


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def cmd_sched_trace(args) -> int:
    trace = run_sched_trace(
        Path(args.source),
        function=args.function,
        extra_flags=args.extra_flags.split() if args.extra_flags else None,
        verbose=args.verbose,
        keep_listing=args.keep_listing,
    )
    print(f"# {trace.function}  ({trace.n_picks} picks, {trace.n_annotated} annotated)")
    for p in trace.picks:
        print(p.text())
    if trace.stall_summary:
        print("# stall summary:")
        for code, cnt, cyc, desc in trace.stall_summary:
            print(f"#   {code}  count={cnt}  cycles={cyc}  {desc}")
    return 0


def cmd_sched_diff(args) -> int:
    fn = args.function
    ta = run_sched_trace(
        Path(args.source_a), function=fn,
        extra_flags=args.extra_flags.split() if args.extra_flags else None,
        verbose=args.verbose,
    )
    tb = run_sched_trace(
        Path(args.source_b), function=fn,
        extra_flags=args.extra_flags.split() if args.extra_flags else None,
        verbose=args.verbose,
    )
    div = sched_diff(ta, tb)
    print(div.report())
    return 1 if div.diverged else 0


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="sched_trace",
        description="Xenon scheduler emission-schedule dump + diff (Stage V1b).",
    )
    sub = p.add_subparsers(dest="command", required=True)

    pt = sub.add_parser("sched-trace", help="dump one function's emission schedule")
    pt.add_argument("source")
    pt.add_argument("-f", "--function", help="function (mangled name or substring)")
    pt.add_argument("--extra-flags", default="", help="extra cl.exe flags")
    pt.add_argument("--keep-listing", action="store_true", help="keep the .cod tmp file")
    pt.add_argument("-v", "--verbose", action="store_true")
    pt.set_defaults(func=cmd_sched_trace)

    pd = sub.add_parser(
        "sched-diff", help="first-divergent-pick report between two variants"
    )
    pd.add_argument("source_a")
    pd.add_argument("source_b")
    pd.add_argument("-f", "--function", help="function (mangled name or substring)")
    pd.add_argument("--extra-flags", default="", help="extra cl.exe flags")
    pd.add_argument("-v", "--verbose", action="store_true")
    pd.set_defaults(func=cmd_sched_diff)
    return p


def main(argv: Optional[list[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
