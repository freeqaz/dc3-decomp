#!/usr/bin/env python3
"""
Real Ghidra P-code exporter (in-process pyghidra).

Unlike the old pcode_inspect.py (which only ever analyzed Ghidra's *decompiled C*
and hand-decoded raw PPC bytes — it never touched real P-code), this tool starts
the JVM in-process via pyghidra and emits genuine Ghidra P-code:

  --high  (default)  HIGH P-code from the decompiler's HighFunction (SSA/simplified,
                     register/varnode names resolved). Useful for "what does the
                     decompiler think the dataflow is".
  --raw              RAW (low) P-code straight from the SLEIGH disassembler, 1:1 with
                     the machine instructions in the function body. The more faithful
                     view for lowering questions (sign/zero extension, operand order,
                     dead-address materialization, etc.).

Symbol/address resolution reuses pyghidra-mcp's GhidraTools.find_function (multi-strategy
mangled / demangled / method-name / address / map-file O(1) lookup, with a
CreateFunctionCmd fallback). The DC3 linker map (orig/373307D9/ham_xbox_r.map) is wired
in for the O(1) address path.

LOCK / SANDBOX NOTES
--------------------
The running pyghidra-mcp HTTP service holds ghidra_projects/DC3/DC3/DC3.lock, so we do
NOT attach to that project. Instead — exactly like tools/ghidra/direct_client.py — we open
(or, on first run, import+analyze) a private throwaway project at:

    /tmp/claude/ghidra_projects/DirectGhidraClient

On the first invocation this re-runs Ghidra auto-analysis on the XEX (several minutes);
subsequent runs reuse the analyzed project instantly. This sidesteps the DC3.lock
contention entirely — no need to stop the service.

This script must run with the sandbox SKIPPED (dangerouslyDisableSandbox) so the JVM /
native ICD can load, like every other Ghidra script here. Run it via the
pcode-export.sh wrapper, which sets GHIDRA_INSTALL_DIR / GHIDRA_USER_HOME.

Usage:
    pcode_export.py "CharBones::PoseMeshes"            # HIGH p-code, human-readable
    pcode_export.py "CharBones::PoseMeshes" --raw      # RAW sleigh p-code
    pcode_export.py "0x82878b58" --raw --json          # RAW, machine-readable JSON
    pcode_export.py "?OnBeat@HollaBackMinigame@@QAAXXZ" --high --json
"""

import argparse
import json
import logging
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

# Mirror direct_client.py: project root + default DC3 map / binary.
_PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
DEFAULT_MAP_FILE = _PROJECT_ROOT / "orig" / "373307D9" / "ham_xbox_r.map"
DEFAULT_BINARY = _PROJECT_ROOT / "orig" / "373307D9" / "default.xex"

# A private throwaway project so we never touch the service-held DC3.lock.
DEFAULT_PROJECT_DIR = "/tmp/claude/ghidra_projects"
DEFAULT_PROJECT_NAME = "DirectGhidraClient"

logger = logging.getLogger(__name__)


class PcodeExportError(Exception):
    """Raised when JVM/Ghidra setup or P-code extraction fails."""


# --------------------------------------------------------------------------- #
# Varnode / PcodeOp serialization
# --------------------------------------------------------------------------- #


def varnode_to_dict(vn, language=None):
    """Serialize a ghidra.program.model.pcode.Varnode to a plain dict.

    Reports the disjoint address-space classification (register / constant / unique /
    ram-stack/etc.) plus offset+size so the consumer can reason about lowering without
    a live JVM. `language` (program.getLanguage()) lets Varnode.toString render register
    names; it is optional.
    """
    if vn is None:
        return None
    try:
        if language is not None:
            text = vn.toString(language)
        else:
            text = vn.toString()
    except Exception:
        text = str(vn)
    d = {
        "text": str(text),
        "size": int(vn.getSize()),
        "is_register": bool(vn.isRegister()),
        "is_constant": bool(vn.isConstant()),
        "is_unique": bool(vn.isUnique()),
        "is_address": bool(vn.isAddress()),
    }
    try:
        addr = vn.getAddress()
        if addr is not None:
            d["space"] = str(addr.getAddressSpace().getName())
            d["offset"] = int(vn.getOffset())
    except Exception:
        pass
    return d


def pcodeop_to_dict(op, language=None):
    """Serialize a ghidra.program.model.pcode.PcodeOp to a plain dict.

    Shape is identical for HIGH and RAW p-code so consumers can treat them uniformly:
        {seq, mnemonic, out, ins[]}
    where `seq` is "<target-address>:<order>" (order from the SequenceNumber, NOT from
    PcodeOp — PcodeOp has no getOrder() in this Ghidra build).
    """
    seqnum = op.getSeqnum()
    try:
        target = seqnum.getTarget().toString()
        order = seqnum.getOrder()
        seq = f"{target}:{order}"
    except Exception:
        seq = str(seqnum)
    return {
        "seq": seq,
        "mnemonic": str(op.getMnemonic()),
        "out": varnode_to_dict(op.getOutput(), language),
        "ins": [varnode_to_dict(v, language) for v in op.getInputs()],
    }


# --------------------------------------------------------------------------- #
# Ghidra context (throwaway project, no DC3.lock contention)
# --------------------------------------------------------------------------- #


def _start_jvm(verbose=False):
    """Start the JVM via pyghidra (once per process)."""
    try:
        import pyghidra
    except Exception as e:  # pragma: no cover - import-time/env failure
        raise PcodeExportError(
            f"pyghidra is not importable ({e}). Run via pcode-export.sh with the "
            f"sandbox skipped so GHIDRA_INSTALL_DIR/JVM are available."
        ) from e
    try:
        pyghidra.start(verbose=verbose)
    except Exception as e:
        raise PcodeExportError(f"Failed to start JVM for pyghidra: {e}") from e


def _open_context(binary_path, project_dir, project_name, map_file, verbose=False):
    """Open (or import+analyze) the throwaway project and return (program_info, tools).

    Models DirectGhidraClient._initialize_ghidra: reuse an already-analyzed throwaway
    project if present, otherwise import the XEX (PowerPC:BE:64:Xenon, auto-detect
    fallback) and run analysis.
    """
    from pyghidra_mcp.context import PyGhidraContext
    from pyghidra_mcp.tools import GhidraTools

    binary_path = Path(binary_path)
    if not binary_path.exists():
        raise PcodeExportError(f"Binary file not found: {binary_path}")

    ctx = PyGhidraContext(
        project_name=project_name,
        project_path=project_dir,
        force_analysis=False,
        verbose_analysis=False,
    )

    programs = ctx.programs
    if programs:
        if verbose:
            logger.info(f"Reusing loaded programs: {list(programs.keys())}")
        program_name = list(programs.keys())[0]
        program_info = programs[program_name]
    else:
        # COLD path: import + analyze synchronously. import_binary(analyze=True)
        # calls analyze_program() in-line (NOT via the async executor), so the XEX
        # is actually disassembled+analyzed before we return — unlike analyze_project(),
        # which (with the default threaded executor + wait_for_analysis=False) hands
        # back a Future that nobody awaits, leaving the program unanalyzed and
        # getInstructions() empty. import_binary's real signature is
        # (binary_path, analyze=False, relative_path=None, language=None) — no
        # 'compiler' param.
        if verbose:
            logger.info(f"Importing + analyzing binary: {binary_path}")
        if str(binary_path).endswith(".xex"):
            try:
                ctx.import_binary(
                    binary_path=binary_path,
                    language="PowerPC:BE:64:Xenon",
                    analyze=True,
                )
            except Exception as e:
                if verbose:
                    logger.info(f"PowerPC:BE:64:Xenon import failed ({e}); auto-detecting")
                ctx.import_binary(binary_path=binary_path, language=None, analyze=True)
        else:
            ctx.import_binary(binary_path=binary_path, language=None, analyze=True)

        programs = ctx.programs
        if not programs:
            raise PcodeExportError("No programs loaded after import")
        program_name = list(programs.keys())[0]
        program_info = programs[program_name]

    if not program_info:
        raise PcodeExportError("Failed to load program info")

    # Belt-and-suspenders: ensure the program is actually analyzed before we read
    # P-code. ProgramInfo.ghidra_analysis_complete is always re-initialized to False
    # when a project is (re)opened, so it is NOT a reliable cross-run signal — the
    # WARM path reuses an already-analyzed on-disk program but still reports
    # analysis_complete==False. Ask Ghidra itself via shouldAskToAnalyze(): True means
    # the on-disk program has not been analyzed yet, so analyze it now (synchronously,
    # via analyze_program(force_analysis=True) — never the async analyze_project()).
    program = program_info.program
    needs_analysis = True
    try:
        from ghidra.program.util import GhidraProgramUtilities

        needs_analysis = bool(GhidraProgramUtilities.shouldAskToAnalyze(program))
    except Exception as e:  # pragma: no cover - fall back to the dataclass flag
        if verbose:
            logger.info(f"shouldAskToAnalyze unavailable ({e}); using analysis_complete flag")
        needs_analysis = not program_info.analysis_complete

    if needs_analysis:
        if verbose:
            logger.info("Running auto-analysis (cold run; this can take minutes)...")
        ctx.analyze_program(program, force_analysis=True)

    tools = GhidraTools(program_info, cache_manager=None, map_file=map_file)
    return ctx, program_info, tools


# --------------------------------------------------------------------------- #
# P-code extraction
# --------------------------------------------------------------------------- #


def export_high_pcode(program_info, func, timeout=120):
    """HIGH (decompiler) P-code from the function's HighFunction.

    getPcodeOps() lives on PcodeSyntaxTree, which HighFunction extends, and yields
    PcodeOpAST in execution order.
    """
    from ghidra.util.task import ConsoleTaskMonitor

    decompiler = program_info.decompiler
    res = decompiler.decompileFunction(func, timeout, ConsoleTaskMonitor())
    err = res.getErrorMessage()
    if err:
        raise PcodeExportError(f"Decompiler error: {err}")
    hf = res.getHighFunction()
    if hf is None:
        raise PcodeExportError(
            "Decompiler returned no HighFunction (no high P-code available for this function)."
        )
    language = program_info.program.getLanguage()
    ops = []
    op_iter = hf.getPcodeOps()
    while op_iter.hasNext():
        ops.append(pcodeop_to_dict(op_iter.next(), language))
    return ops


def export_raw_pcode(program_info, func):
    """RAW (low) P-code straight from SLEIGH, 1:1 with the function body instructions.

    Instruction.getPcode() returns PcodeOp[] for each machine instruction.
    """
    program = program_info.program
    language = program.getLanguage()
    listing = program.getListing()
    ops = []
    insn_iter = listing.getInstructions(func.getBody(), True)
    for insn in insn_iter:
        for op in insn.getPcode():
            ops.append(pcodeop_to_dict(op, language))
    return ops


# --------------------------------------------------------------------------- #
# Output
# --------------------------------------------------------------------------- #


def print_human(ops, func_name, mode):
    addr = "?"
    sig = ""
    print(f"P-code ({mode}) for {func_name} {sig}".rstrip())
    print(f"  {len(ops)} ops")
    print("=" * 70)
    for op in ops:
        out = op["out"]
        out_txt = (out["text"] + " = ") if out else ""
        ins_txt = ", ".join(v["text"] if v else "<null>" for v in op["ins"])
        print(f"  {op['seq']:<24} {out_txt}{op['mnemonic']}({ins_txt})")


def main():
    parser = argparse.ArgumentParser(
        description="Export real Ghidra P-code (HIGH or RAW) for a function.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "symbol",
        help='Function symbol (mangled/demangled) or address, e.g. '
        '"CharBones::PoseMeshes" or "0x82878b58"',
    )
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument(
        "--high",
        dest="mode",
        action="store_const",
        const="high",
        help="HIGH (decompiler/SSA) P-code from the HighFunction (default)",
    )
    mode.add_argument(
        "--raw",
        dest="mode",
        action="store_const",
        const="raw",
        help="RAW (sleigh, 1:1 with instructions) P-code from the function body",
    )
    parser.add_argument(
        "--json", action="store_true", help="Emit a JSON array of P-code ops instead of text"
    )
    parser.add_argument(
        "--timeout", type=int, default=120, help="Decompiler timeout in seconds for --high (default 120)"
    )
    parser.add_argument(
        "--binary", default=str(DEFAULT_BINARY), help="Path to the XEX binary"
    )
    parser.add_argument(
        "--project-dir", default=DEFAULT_PROJECT_DIR, help="Throwaway Ghidra project directory"
    )
    parser.add_argument(
        "--project-name", default=DEFAULT_PROJECT_NAME, help="Throwaway Ghidra project name"
    )
    parser.add_argument(
        "--map-file", default=str(DEFAULT_MAP_FILE), help="Linker map file for O(1) address lookup"
    )
    parser.add_argument("-v", "--verbose", action="store_true", help="Verbose logging")
    args = parser.parse_args()

    mode = args.mode or "high"  # default --high
    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.WARNING)

    try:
        _start_jvm(verbose=args.verbose)
        ctx, program_info, tools = _open_context(
            binary_path=args.binary,
            project_dir=args.project_dir,
            project_name=args.project_name,
            map_file=Path(args.map_file),
            verbose=args.verbose,
        )
    except PcodeExportError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)

    func = tools.find_function(args.symbol)
    if func is None:
        print(f"ERROR: function not found: {args.symbol}", file=sys.stderr)
        sys.exit(1)

    func_name = str(func.getName())
    if args.verbose:
        print(f"Resolved {args.symbol} -> {func_name} @ {func.getEntryPoint()}", file=sys.stderr)

    try:
        if mode == "raw":
            ops = export_raw_pcode(program_info, func)
        else:
            ops = export_high_pcode(program_info, func, timeout=args.timeout)
    except PcodeExportError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)

    if args.json:
        print(json.dumps(ops, indent=2))
    else:
        print_human(ops, func_name, mode)


if __name__ == "__main__":
    main()
