"""Optional m2c decompilation support for permuter guidance."""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
M2C_PATH = Path.home() / "code" / "milohax" / "m2c" / "m2c.py"
OBJDIFF_TO_M2C_PATH = REPO_ROOT / "tools" / "objdiff_to_m2c.py"
_CALL_RE = re.compile(r"\b([A-Za-z_]\w*)\s*\(")
_NOT_CALLS = frozenset({
    "if", "while", "for", "switch", "return", "sizeof", "typeof",
    "int", "long", "short", "char", "void", "float", "double",
    "uint", "ulong", "ushort", "uchar", "bool", "struct",
})

_M2C_CACHE: dict[tuple[str, str | None], str | None] = {}


def get_or_run_m2c(symbol: str, unit: str | None = None) -> str | None:
    """Return cached m2c decompilation text for a symbol, if available."""
    key = (symbol, unit)
    if key in _M2C_CACHE:
        return _M2C_CACHE[key]

    code = _run_m2c(symbol, unit)
    _M2C_CACHE[key] = code
    return code


def _run_m2c(symbol: str, unit: str | None) -> str | None:
    """Run objdiff JSON -> objdiff_to_m2c -> m2c pipeline."""
    if not M2C_PATH.exists() or not OBJDIFF_TO_M2C_PATH.exists():
        return None

    try:
        objdiff_result = subprocess.run(
            [
                "bin/objdiff-cli",
                "diff",
                "-p",
                str(REPO_ROOT),
                "-f",
                "json",
                "--include-instructions",
                symbol,
            ],
            capture_output=True,
            text=True,
            timeout=60,
            cwd=str(REPO_ROOT),
        )
        if objdiff_result.returncode != 0 or not objdiff_result.stdout.strip():
            return None

        json_line = None
        for line in objdiff_result.stdout.splitlines():
            candidate = line.strip()
            if candidate.startswith("{") and (
                '"instructions"' in candidate or '"symbol"' in candidate
            ):
                json_line = candidate
                break
        if not json_line:
            return None

        convert_cmd = ["python3", str(OBJDIFF_TO_M2C_PATH), "--project-dir", str(REPO_ROOT)]
        obj_path = _obj_path_for_unit(unit)
        if obj_path is not None and obj_path.exists():
            convert_cmd.extend(["--obj", str(obj_path)])

        convert_result = subprocess.run(
            convert_cmd,
            input=json_line,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=str(REPO_ROOT),
        )
        if convert_result.returncode != 0 or not convert_result.stdout.strip():
            return None

        m2c_result = subprocess.run(
            ["python3", str(M2C_PATH), "-t", "ppc", "--valid-syntax", "-"],
            input=convert_result.stdout,
            capture_output=True,
            text=True,
            timeout=60,
            cwd=str(REPO_ROOT),
        )
        if m2c_result.returncode != 0:
            return None

        output = m2c_result.stdout.strip()
        if not output or "Decompilation failure" in output:
            return None
        return output
    except Exception as exc:
        print(f"  m2c: unavailable ({exc})", file=sys.stderr)
        return None


def _obj_path_for_unit(unit: str | None) -> Path | None:
    """Best-effort map from DB unit to built object path."""
    if not unit:
        return None
    normalized = unit[len("default/"):] if unit.startswith("default/") else unit
    return REPO_ROOT / "build" / "373307D9" / "obj" / f"{normalized}.obj"


def extract_last_call_name(code: str) -> str | None:
    """Extract the last call-like identifier from m2c output."""
    body = code.split("{", 1)[1] if "{" in code else code
    calls: list[str] = []
    for match in _CALL_RE.finditer(body):
        name = match.group(1)
        if name in _NOT_CALLS:
            continue
        calls.append(name)
    if calls:
        return calls[-1]
    return None
