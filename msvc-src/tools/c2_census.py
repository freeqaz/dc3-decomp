#!/usr/bin/env python3
"""c2.dll Ghidra decompilability census.

A MEASUREMENT experiment: run Ghidra's decompiler over every function in c2.dll
(the MSVC Xbox 360 PPC back-end DLL — a 32-bit x86 PE) and score how cleanly each
function recovers. The point is to quantify what fraction of the ~1430 functions
Ghidra turns into structured, portable C vs. how many come back as artifact-laden
goto/register-soup — i.e. to feasibility-check a clean-room native port.

This does NOT port anything and makes no source edits. It emits:
  - msvc-src/results/c2_census.json      per-function rows + aggregate summary
  - (readout doc written separately by the analyst)

Run:
    GHIDRA_INSTALL_DIR=~/code/milohax/ghidra/build/ghidra \
    GHIDRA_USER_HOME=/tmp/claude/ghidra_user \
    venv/bin/python msvc-src/tools/c2_census.py

Requires the sandbox skipped (JVM / native load), like every other Ghidra tool here.
"""
from __future__ import annotations

import json
import os
import re
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
C2_DLL = REPO_ROOT / "build" / "compilers" / "X360" / "16.00.11886.00" / "c2.dll"
OUT_JSON = REPO_ROOT / "msvc-src" / "results" / "c2_census.json"
PROJECT_DIR = "/tmp/claude/c2-census"
PROJECT_NAME = "c2_census"
DECOMP_TIMEOUT_S = 90

os.environ.setdefault(
    "GHIDRA_INSTALL_DIR",
    os.path.expanduser("~/code/milohax/ghidra/build/ghidra"),
)
os.environ.setdefault("GHIDRA_USER_HOME", "/tmp/claude/ghidra_user")
Path(os.environ["GHIDRA_USER_HOME"]).mkdir(parents=True, exist_ok=True)
Path(PROJECT_DIR).mkdir(parents=True, exist_ok=True)

import pyghidra  # noqa: E402


# --- artifact regexes over decompiled C -------------------------------------
# Each captures a class of "decompiler couldn't fully recover this" signal.
RE_GOTO = re.compile(r"\bgoto\s+\w+")
RE_LABEL = re.compile(r"^\s*\w+:\s*$", re.MULTILINE)
RE_WARNING = re.compile(r"/\* WARNING:")
# uninitialized-register / stack reads: c2.dll was optimized, so Ghidra's
# calling-convention + register recovery frequently invents these phantoms.
RE_PHANTOM = re.compile(
    r"\b(in_[A-Z][A-Z0-9]+|unaff_\w+|extraout_\w+|in_stack_\w+|"
    r"unique0x\w+|register0x\w+)\b"
)
# indirect / computed calls the decompiler could not bind to a symbol
RE_INDIRECT_CALL = re.compile(r"\(\*\(code \*\)")
# jump-table / switch that Ghidra could not fully reconstruct
RE_BAD_SWITCH = re.compile(r"switchD_\w+|unrecovered_jumptable|switchdataD_\w+")
# bad disassembly / data-in-code
RE_HALT = re.compile(r"halt_baddata|code_r0x\w+|__assert|\bBADSPACEBASE\b")
# bit-slicing pseudo-ops that don't map onto clean C
RE_EXTRACT_OP = re.compile(r"\b(CONCAT\d+|SUB\d+|ZEXT\d+|SEXT\d+)\b")
# "undefinedN" typed vars — incomplete type recovery
RE_UNDEFINED_TYPE = re.compile(r"\bundefined\d*\b")
# explicit numeric casts (proxy for type-recovery churn)
RE_CAST = re.compile(
    r"\((?:unsigned\s+)?(?:u?int|u?char|u?short|u?long|byte|code|"
    r"undefined\d*|void)\s*\**\)"
)
# structural complexity keywords
RE_IF = re.compile(r"\bif\s*\(")
RE_LOOP = re.compile(r"\b(?:while|for)\s*\(")
RE_CASE = re.compile(r"\bcase\b")
RE_BOOLOP = re.compile(r"&&|\|\|")
RE_TERNARY = re.compile(r"\?[^:]{0,60}:")


def count(rx: re.Pattern, s: str) -> int:
    return len(rx.findall(s))


def nonblank_lines(s: str) -> int:
    return sum(1 for ln in s.splitlines() if ln.strip())


def classify(row: dict) -> str:
    """Bucket a function by recovery cleanliness for the census.

    clean   : structured C, no serious recovery artifacts
    fair    : recovered but noticeable phantom regs / casts / gotos
    poor    : goto-soup, unresolved switch/indirect, bad disasm, or failed
    """
    if not row["decompiled"]:
        return "failed"
    if (
        row["bad_switch"]
        or row["halt"]
        or row["goto"] >= 8
        or row["phantom"] >= 6
        or row["indirect_call"] >= 4
    ):
        return "poor"
    if (
        row["goto"] >= 2
        or row["phantom"] >= 1
        or row["indirect_call"] >= 1
        or row["warning"] >= 1
        or row["extract_op"] >= 8
    ):
        return "fair"
    return "clean"


def main() -> int:
    t0 = time.time()
    print(f"[census] launching pyghidra; opening {C2_DLL.name} ...", file=sys.stderr)
    pyghidra.start()

    from ghidra.app.decompiler import DecompInterface, DecompileOptions
    from ghidra.util.task import ConsoleTaskMonitor

    rows = []
    with pyghidra.open_program(
        str(C2_DLL),
        project_location=PROJECT_DIR,
        project_name=PROJECT_NAME,
        analyze=True,
    ) as flat:
        program = flat.getCurrentProgram()
        print(
            f"[census] analysis ready in {time.time()-t0:.0f}s; enumerating funcs",
            file=sys.stderr,
        )

        fm = program.getFunctionManager()
        listing = program.getListing()

        decomp = DecompInterface()
        opts = DecompileOptions()
        decomp.setOptions(opts)
        decomp.openProgram(program)
        monitor = ConsoleTaskMonitor()

        funcs = list(fm.getFunctions(True))  # forward order
        total = len(funcs)
        print(f"[census] {total} functions to process", file=sys.stderr)

        for i, func in enumerate(funcs):
            entry = func.getEntryPoint()
            body = func.getBody()
            size_bytes = int(body.getNumAddresses())
            # instruction count
            insns = 0
            it = listing.getInstructions(body, True)
            while it.hasNext():
                it.next()
                insns += 1

            row = {
                "addr": "0x%x" % entry.getOffset(),
                "name": func.getName(),
                "size_bytes": size_bytes,
                "insns": insns,
                "is_thunk": bool(func.isThunk()),
                "params": func.getParameterCount(),
                "callees": func.getCalledFunctions(monitor).size(),
                "callers": func.getCallingFunctions(monitor).size(),
                "cconv": str(func.getCallingConventionName()),
                "decompiled": False,
                "decomp_error": "",
                "c_lines": 0,
                "c_chars": 0,
                "locals": 0,
                "goto": 0,
                "label": 0,
                "warning": 0,
                "phantom": 0,
                "indirect_call": 0,
                "bad_switch": 0,
                "halt": 0,
                "extract_op": 0,
                "undefined_type": 0,
                "cast": 0,
                "if_": 0,
                "loop": 0,
                "case_": 0,
                "boolop": 0,
                "ternary": 0,
            }

            try:
                res = decomp.decompileFunction(func, DECOMP_TIMEOUT_S, monitor)
                if res.decompileCompleted():
                    dfunc = res.getDecompiledFunction()
                    c = dfunc.getC() if dfunc else ""
                    hf = res.getHighFunction()
                    row["decompiled"] = True
                    row["c_chars"] = len(c)
                    row["c_lines"] = nonblank_lines(c)
                    if hf is not None:
                        try:
                            lsm = hf.getLocalSymbolMap()
                            row["locals"] = int(lsm.getNumLocalSymbols())
                        except Exception:
                            pass
                    row["goto"] = count(RE_GOTO, c)
                    row["label"] = count(RE_LABEL, c)
                    row["warning"] = count(RE_WARNING, c)
                    row["phantom"] = count(RE_PHANTOM, c)
                    row["indirect_call"] = count(RE_INDIRECT_CALL, c)
                    row["bad_switch"] = count(RE_BAD_SWITCH, c)
                    row["halt"] = count(RE_HALT, c)
                    row["extract_op"] = count(RE_EXTRACT_OP, c)
                    row["undefined_type"] = count(RE_UNDEFINED_TYPE, c)
                    row["cast"] = count(RE_CAST, c)
                    row["if_"] = count(RE_IF, c)
                    row["loop"] = count(RE_LOOP, c)
                    row["case_"] = count(RE_CASE, c)
                    row["boolop"] = count(RE_BOOLOP, c)
                    row["ternary"] = count(RE_TERNARY, c)
                else:
                    row["decomp_error"] = str(res.getErrorMessage() or "no-complete")
            except Exception as e:  # noqa: BLE001
                row["decomp_error"] = f"exc: {e}"

            row["bucket"] = classify(row)
            rows.append(row)

            if (i + 1) % 100 == 0 or i + 1 == total:
                print(
                    f"[census] {i+1}/{total}  ({time.time()-t0:.0f}s)",
                    file=sys.stderr,
                )

        decomp.dispose()

    summary = summarize(rows, program_meta={
        "binary": str(C2_DLL),
        "size_bytes": C2_DLL.stat().st_size,
        "ghidra": os.environ["GHIDRA_INSTALL_DIR"],
        "decomp_timeout_s": DECOMP_TIMEOUT_S,
        "elapsed_s": round(time.time() - t0, 1),
    })

    OUT_JSON.parent.mkdir(parents=True, exist_ok=True)
    with open(OUT_JSON, "w") as f:
        json.dump({"summary": summary, "functions": rows}, f, indent=1)
    print(f"[census] wrote {OUT_JSON}", file=sys.stderr)
    print(json.dumps(summary, indent=2))
    return 0


def summarize(rows: list[dict], program_meta: dict) -> dict:
    n = len(rows)
    dec = [r for r in rows if r["decompiled"]]
    failed = [r for r in rows if not r["decompiled"]]
    buckets = {}
    for r in rows:
        buckets[r["bucket"]] = buckets.get(r["bucket"], 0) + 1

    def frac_with(key, thresh=1):
        return sum(1 for r in dec if r[key] >= thresh)

    total_c_lines = sum(r["c_lines"] for r in dec)
    sizes = sorted(r["size_bytes"] for r in rows)

    def pct(p):
        if not sizes:
            return 0
        return sizes[min(len(sizes) - 1, int(p / 100 * len(sizes)))]

    # size bands
    bands = {"tiny(<=32b)": 0, "small(33-128)": 0, "med(129-512)": 0,
             "large(513-2048)": 0, "huge(>2048)": 0}
    for r in rows:
        b = r["size_bytes"]
        if b <= 32:
            bands["tiny(<=32b)"] += 1
        elif b <= 128:
            bands["small(33-128)"] += 1
        elif b <= 512:
            bands["med(129-512)"] += 1
        elif b <= 2048:
            bands["large(513-2048)"] += 1
        else:
            bands["huge(>2048)"] += 1

    thunks = sum(1 for r in rows if r["is_thunk"])
    named = sum(1 for r in rows if not re.match(r"^(FUN_|thunk_FUN_|LAB_)", r["name"]))

    return {
        "meta": program_meta,
        "n_functions": n,
        "n_decompiled": len(dec),
        "n_failed_decomp": len(failed),
        "n_thunks": thunks,
        "n_named_by_ghidra": named,  # imports/exports/analyzer-named, not FUN_
        "buckets": buckets,
        "bucket_pct": {k: round(100 * v / n, 1) for k, v in buckets.items()},
        "total_c_lines_decompiled": total_c_lines,
        "median_c_lines": (sorted(r["c_lines"] for r in dec)[len(dec) // 2]
                           if dec else 0),
        "size_bytes_p50": pct(50),
        "size_bytes_p90": pct(90),
        "size_bytes_max": sizes[-1] if sizes else 0,
        "size_bands": bands,
        "total_insns": sum(r["insns"] for r in rows),
        "artifact_prevalence_over_decompiled": {
            "any_goto": frac_with("goto"),
            "goto_ge8": frac_with("goto", 8),
            "any_phantom_reg": frac_with("phantom"),
            "any_indirect_call": frac_with("indirect_call"),
            "unrecovered_switch": frac_with("bad_switch"),
            "bad_disasm_halt": frac_with("halt"),
            "any_warning": frac_with("warning"),
            "any_undefined_type": frac_with("undefined_type"),
            "extract_op_ge8": frac_with("extract_op", 8),
        },
        "failed_addrs": [r["addr"] for r in failed][:50],
    }


if __name__ == "__main__":
    sys.exit(main())
