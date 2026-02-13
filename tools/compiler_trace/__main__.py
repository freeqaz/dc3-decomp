"""CLI entry point for compiler trace tooling."""

import argparse
import sys


def main():
    parser = argparse.ArgumentParser(
        prog="compiler_trace",
        description="Compiler instrumentation tooling for analyzing c2.dll",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    # diff-asm
    p_diff = sub.add_parser("diff-asm", help="Compile two variants and diff assembly")
    p_diff.add_argument("source_a", help="First source file")
    p_diff.add_argument("source_b", help="Second source file")
    p_diff.add_argument("--output-dir", help="Output directory for listings")
    p_diff.add_argument(
        "--listing-type",
        default="/FAcs",
        choices=["/FA", "/FAs", "/FAcs"],
        help="Assembly listing type (default: /FAcs for code+source)",
    )
    p_diff.add_argument(
        "-f",
        "--function",
        help="Extract and diff only this function (mangled name or substring)",
    )

    # capture-il
    p_il = sub.add_parser("capture-il", help="Capture IL temp files via strace")
    p_il.add_argument("source", help="Source file to compile")
    p_il.add_argument("--output-dir", required=True, help="Directory for captured IL files")
    p_il.add_argument("--diff", dest="source_b", help="Second source for IL diff mode")

    # callgrind-diff
    p_cg = sub.add_parser(
        "callgrind-diff", help="Instruction-level profiling diff of c2.dll"
    )
    p_cg.add_argument("source_a", help="First source file")
    p_cg.add_argument("source_b", help="Second source file")
    p_cg.add_argument("--output-dir", help="Output directory for callgrind files")
    p_cg.add_argument(
        "--funcmap",
        help="Path to c2 funcmap JSON (default: tools/c2_funcmap.json)",
    )
    p_cg.add_argument(
        "--perf",
        action="store_true",
        help="Use perf sampling instead of callgrind (noisier but no valgrind needed)",
    )

    # annotate
    p_ann = sub.add_parser(
        "annotate", help="Disassemble c2.dll at funcmap-identified addresses"
    )
    p_ann.add_argument(
        "--address",
        help="Specific RVA to disassemble (hex, e.g. 0x1234)",
    )
    p_ann.add_argument(
        "--top",
        type=int,
        default=10,
        help="Show top N funcmap addresses by evidence count (default: 10)",
    )
    p_ann.add_argument(
        "--callgrind",
        help="Path to callgrind output file for count overlay",
    )
    p_ann.add_argument(
        "--funcmap",
        help="Path to c2 funcmap JSON (default: tools/c2_funcmap.json)",
    )
    p_ann.add_argument(
        "--context",
        type=int,
        default=64,
        help="Bytes of context before/after each address (default: 64)",
    )

    # rr-record
    p_rr = sub.add_parser("rr-record", help="Record compilation for rr replay")
    p_rr.add_argument("source", help="Source file to compile")
    p_rr.add_argument(
        "--trace-dir", required=True, help="Output directory for rr trace"
    )
    p_rr.add_argument("--both", dest="source_b", help="Second source for dual recording")

    # gdb-attach
    p_gdb = sub.add_parser("gdb-attach", help="Generate GDB scripts for c2.dll debugging")
    p_gdb.add_argument("source", nargs="?", help="Source file (for live debugging)")
    p_gdb.add_argument("--rr-trace", help="rr trace directory (for replay debugging)")
    p_gdb.add_argument("--print-only", action="store_true", help="Print script, don't launch GDB")
    p_gdb.add_argument(
        "--funcmap",
        help="Path to c2 funcmap JSON (default: tools/c2_funcmap.json)",
    )
    p_gdb.add_argument(
        "--min-evidence",
        type=int,
        default=2,
        help="Minimum evidence count for breakpoints (default: 2)",
    )

    args = parser.parse_args()

    if args.command == "diff-asm":
        from .asm_diff import cmd_diff_asm

        cmd_diff_asm(args)
    elif args.command == "capture-il":
        from .il_capture import cmd_capture_il

        cmd_capture_il(args)
    elif args.command == "callgrind-diff":
        from .callgrind_diff import cmd_callgrind_diff

        cmd_callgrind_diff(args)
    elif args.command == "annotate":
        from .annotate import cmd_annotate

        cmd_annotate(args)
    elif args.command == "rr-record":
        from .rr_record import cmd_rr_record

        cmd_rr_record(args)
    elif args.command == "gdb-attach":
        from .gdb_script import cmd_gdb_attach

        cmd_gdb_attach(args)


if __name__ == "__main__":
    main()
