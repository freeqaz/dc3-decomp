#!/usr/bin/env python3
"""
Generate decomp-facing hints from raw XEX/PE metadata.

This script builds on analyze_xex_pe_raw.py and focuses on outputs that are
useful for reverse engineering work right now:

- embedded source/header paths mapped onto repo files
- raw import slots/thunks resolved to API names
- `.pdata` exception-handler samples resolved back to map symbols
"""

from __future__ import annotations

import argparse
import importlib.util
from bisect import bisect_right
from pathlib import Path

from analyze_xex_pe_raw import analyze


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_XEX = ROOT / "orig" / "373307D9" / "default.xex"
DEFAULT_PE = ROOT / "orig" / "373307D9" / "ham_xbox_r.exe"
DEFAULT_MAP = ROOT / "orig" / "373307D9" / "ham_xbox_r.map"
X360_IMPORTS = ROOT.parent / "xbox-reversing" / "x360_imports.py"


def load_imports_module(path: Path):
    spec = importlib.util.spec_from_file_location("x360_imports", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def parse_map_symbols(map_path: Path) -> list[dict]:
    symbols = []
    with map_path.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            parts = line.split()
            if len(parts) < 3 or ":" not in parts[0]:
                continue
            sec_off = parts[0]
            name = parts[1]
            va_token = parts[2]
            if len(sec_off) != 13 or not all(c in "0123456789ABCDEF:" for c in sec_off.upper()):
                continue
            if len(va_token) != 8 or not all(c in "0123456789ABCDEF" for c in va_token.upper()):
                continue
            section = int(sec_off.split(":", 1)[0], 16)
            offset = int(sec_off.split(":", 1)[1], 16)
            va = int(va_token, 16)
            rest = " ".join(parts[3:])
            symbols.append(
                {
                    "section": section,
                    "offset": offset,
                    "name": name,
                    "va": va,
                    "rest": rest,
                }
            )
    symbols.sort(key=lambda sym: sym["va"])
    return symbols


def find_symbol_at_or_before(symbols: list[dict], va: int) -> dict | None:
    vas = [sym["va"] for sym in symbols]
    idx = bisect_right(vas, va) - 1
    if idx < 0:
        return None
    return symbols[idx]


def find_symbol_exact(symbols: list[dict], va: int) -> dict | None:
    vas = [sym["va"] for sym in symbols]
    idx = bisect_right(vas, va) - 1
    while idx >= 0 and symbols[idx]["va"] == va:
        idx -= 1
    idx += 1
    if idx < len(symbols) and symbols[idx]["va"] == va:
        return symbols[idx]
    return None


def normalize_embedded_path(path: str) -> str | None:
    lower = path.replace("\\", "/").lower()
    if "/lazer/src/" in lower:
        suffix = lower.split("/lazer/src/", 1)[1]
        return f"src/lazer/{suffix}"
    if "/system/src/" in lower:
        suffix = lower.split("/system/src/", 1)[1]
        return f"src/system/{suffix}"
    return None


ALIASES = {
    "src/system/utl/localechunksort.h": "src/system/utl/Locale.h",
    "src/system/utl/songinfo.h": "src/system/utl/SongInfoCopy.h",
    "src/system/os/blockmgr_p.h": "src/system/os/Block.h",
    "src/system/rndobj/spline.inl": "src/system/rndobj/Spline.h",
    "src/system/synth360/envelopegenerator.h": "src/system/synth_xbox/EnvelopeGenerator.h",
    "src/system/synth360/sampleinst.h": "src/system/synth_xbox/SampleInst360.h",
    "src/system/synth360/voice.h": "src/system/synth_xbox/Voice.h",
    "src/system/synth360/soundtouch/fifosamplepipe.h": "src/system/synth_xbox/soundtouch/include/FIFOSamplePipe.h",
}


def collect_repo_paths(root: Path) -> dict[str, str]:
    repo = {}
    for path in root.joinpath("src").rglob("*"):
        if path.is_file():
            repo[str(path.relative_to(root)).replace("\\", "/").lower()] = str(path.relative_to(root))
    return repo


def repo_path_hints(report: dict, repo_paths: dict[str, str]) -> tuple[list[dict], list[str]]:
    hints = []
    unmatched = []
    seen = set()
    for embedded in report["source_paths"].get("all", []):
        normalized = normalize_embedded_path(embedded)
        if not normalized:
            continue
        if normalized in seen:
            continue
        seen.add(normalized)
        repo_match = repo_paths.get(normalized)
        alias = None
        if not repo_match and normalized in ALIASES:
            alias = ALIASES[normalized]
            repo_match = alias
        if repo_match:
            hints.append({"embedded": embedded, "repo": repo_match, "alias": alias})
        else:
            unmatched.append(embedded)
    return hints, unmatched


def resolve_import_name(module, lib_name: str, ordinal: int) -> str:
    return module.DoNameGen(lib_name, 0, ordinal)


def import_hints(report: dict, module) -> list[dict]:
    libs = {lib["module_index"]: lib["name"] for lib in report["import_libraries"]["libraries"]}
    hints = []
    for entry in report["iat"]["samples"]:
        lib_name = libs.get(entry["library_index"], f"lib{entry['library_index']}")
        hints.append(
            {
                "slot_va": 0x82000000 + entry["slot_rva"],
                "library": lib_name,
                "ordinal": entry["ordinal"],
                "name": resolve_import_name(module, lib_name, entry["ordinal"]),
            }
        )
    return hints


def exception_hints(report: dict, symbols: list[dict]) -> list[dict]:
    out = []
    for sample in report["pdata"]["exception_samples"][:12]:
        func_sym = find_symbol_exact(symbols, sample["function_va"]) or find_symbol_at_or_before(symbols, sample["function_va"])
        handler_sym = find_symbol_exact(symbols, sample["handler"]) or find_symbol_at_or_before(symbols, sample["handler"])
        out.append(
            {
                "function_va": sample["function_va"],
                "function": func_sym["name"] if func_sym else "<unknown>",
                "handler_va": sample["handler"],
                "handler": handler_sym["name"] if handler_sym else "<unknown>",
                "record_va": sample["record"],
            }
        )
    return out


def emit_text(path_hints: list[dict], unmatched: list[str], imports: list[dict], exceptions: list[dict], report: dict) -> str:
    lines = []
    lines.append("== Repo Path Hints ==")
    for hint in path_hints[:20]:
        if hint.get("alias"):
            lines.append(f"{hint['embedded']} -> {hint['repo']} [alias]")
        else:
            lines.append(f"{hint['embedded']} -> {hint['repo']}")
    if unmatched:
        lines.append(f"unmatched_embedded_paths={len(unmatched)}")
    lines.append("")

    lines.append("== Raw Import Hints ==")
    for hint in imports[:16]:
        lines.append(
            f"0x{hint['slot_va']:08X}: {hint['library']} ordinal 0x{hint['ordinal']:04X} -> {hint['name']}"
        )
    lines.append("")

    lines.append("== Exception Handler Hints ==")
    for hint in exceptions:
        lines.append(
            f"0x{hint['function_va']:08X}: {hint['function']} -> {hint['handler']} "
            f"(handler 0x{hint['handler_va']:08X}, record 0x{hint['record_va']:08X})"
        )
    lines.append("")

    lines.append("== Immediate Takeaways ==")
    lines.append(f"repo_path_matches={len(path_hints)}")
    lines.append(f"source_paths_found={report['source_paths']['count']}")
    lines.append(f"iat_entries={report['iat']['entry_count']} thunk_entries={report['architecture_thunks']['entry_count']}")
    lines.append(f"xdbf_metadata_ids={report['xdbf']['metadata_ids'] if report['xdbf'] else []}")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="Extract decompilation hints from raw XEX/PE metadata")
    parser.add_argument("--xex", type=Path, default=DEFAULT_XEX)
    parser.add_argument("--pe", type=Path, default=DEFAULT_PE)
    parser.add_argument("--map", dest="map_path", type=Path, default=DEFAULT_MAP)
    args = parser.parse_args()

    report = analyze(args.xex, args.pe)
    repo_paths = collect_repo_paths(ROOT)
    path_hints, unmatched = repo_path_hints(report, repo_paths)
    symbols = parse_map_symbols(args.map_path)
    imports_module = load_imports_module(X360_IMPORTS)
    imports = import_hints(report, imports_module)
    exceptions = exception_hints(report, symbols)
    print(emit_text(path_hints, unmatched, imports, exceptions, report))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
