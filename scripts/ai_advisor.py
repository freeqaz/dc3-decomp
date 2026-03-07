"""AI-guided edit advisor for decomp functions.

Gathers structured context (source, Ghidra decomp, objdiff mismatch summary,
proven patterns) and asks an LLM for targeted source edit suggestions.
Each suggestion is compiled and scored via objdiff. Improvements are
optionally applied.

Usage:
    python3 scripts/ai_advisor.py --symbol 'Class::Method'
    python3 scripts/ai_advisor.py --symbol '?Method@Class@@UAAXXZ' --apply
    python3 scripts/ai_advisor.py --symbol 'Class::Method' --dry-run
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sqlite3
import subprocess
import sys
import textwrap
import time
from dataclasses import asdict, dataclass, field
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DB_PATH = REPO_ROOT / "decomp.db"
PATTERNS_DIR = REPO_ROOT / "docs" / "decomp" / "patterns"
M2C_PATH = Path.home() / "code" / "milohax" / "m2c" / "m2c.py"
OBJDIFF_TO_M2C_PATH = REPO_ROOT / "tools" / "objdiff_to_m2c.py"

# Ensure repo root is on path for imports
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

# ── Data types ──────────────────────────────────────────────────────────


@dataclass
class EditSuggestion:
    """A structured edit suggestion from the AI advisor."""

    edit_type: str  # e.g. "split_conjunction", "reorder_block", "change_cast"
    description: str  # human-readable explanation
    search: str  # source text to find (for search-and-replace)
    replace: str  # replacement text
    confidence: str = "medium"  # low/medium/high
    reasoning: str = ""  # why this should help


@dataclass
class AdvisorResult:
    """Result of an AI advisor session."""

    symbol: str
    function_name: str
    source_path: str
    initial_percent: float
    suggestions: list[EditSuggestion]
    tested: list[dict] = field(default_factory=list)  # [{suggestion, percent, improved}]
    best_percent: float = 0.0
    best_suggestion_idx: int = -1
    applied: bool = False
    error: str | None = None
    elapsed_seconds: float = 0.0


# ── Database helpers ────────────────────────────────────────────────────


def unit_to_source_path(unit: str) -> str:
    """Convert unit name to source path. e.g. 'default/system/rndobj/Text' -> 'src/system/rndobj/Text.cpp'."""
    if unit.startswith("default/"):
        unit = unit[len("default/"):]
    return f"src/{unit}.cpp"


def unit_to_obj_target(unit: str) -> str:
    """Convert unit name to ninja build target. e.g. 'default/system/rndobj/Text' -> 'build/373307D9/obj/system/rndobj/Text.obj'."""
    if unit.startswith("default/"):
        unit = unit[len("default/"):]
    return f"build/373307D9/obj/{unit}.obj"


def resolve_symbol(name: str) -> dict | None:
    """Resolve a function name/symbol to its DB record."""
    if not DB_PATH.exists():
        return None
    conn = sqlite3.connect(str(DB_PATH), timeout=5)
    conn.row_factory = sqlite3.Row
    try:
        # Try exact symbol match first
        row = conn.execute(
            "SELECT symbol, demangled, unit, current_percent, verdict "
            "FROM functions WHERE symbol = ? LIMIT 1",
            (name,),
        ).fetchone()
        if row:
            d = dict(row)
            d["source_path"] = unit_to_source_path(d["unit"])
            return d

        # Try demangled name match
        row = conn.execute(
            "SELECT symbol, demangled, unit, current_percent, verdict "
            "FROM functions WHERE demangled LIKE ? ORDER BY current_percent DESC LIMIT 1",
            (f"%{name}%",),
        ).fetchone()
        if row:
            d = dict(row)
            d["source_path"] = unit_to_source_path(d["unit"])
            return d
    finally:
        conn.close()
    return None


def get_ghidra_decomp(symbol: str) -> str | None:
    """Get cached Ghidra decompilation from DB."""
    if not DB_PATH.exists():
        return None
    conn = sqlite3.connect(str(DB_PATH), timeout=5)
    try:
        cur = conn.execute(
            "SELECT code FROM decompilations WHERE symbol = ? AND error IS NULL",
            (symbol,),
        )
        row = cur.fetchone()
        return row[0] if row else None
    finally:
        conn.close()


# ── m2c decompilation ──────────────────────────────────────────────────


def get_m2c_decomp(symbol: str, unit: str) -> str | None:
    """Run m2c decompiler on the TARGET binary's assembly for this function.

    Pipeline: objdiff JSON (target side) -> objdiff_to_m2c.py -> m2c
    This gives a machine-shaped C view that preserves temp reuse, stack layout,
    and call gating closer to what the compiler actually emitted.
    """
    if not M2C_PATH.exists() or not OBJDIFF_TO_M2C_PATH.exists():
        return None

    try:
        # Get objdiff JSON with instructions
        objdiff_result = subprocess.run(
            [
                "bin/objdiff-cli", "diff", "-p", ".", "-f", "json",
                "--include-instructions", symbol,
            ],
            capture_output=True, text=True, timeout=60, cwd=str(REPO_ROOT),
        )
        if objdiff_result.returncode != 0 or not objdiff_result.stdout.strip():
            return None

        # Find the JSON line (objdiff may emit build messages before it)
        json_line = None
        for line in objdiff_result.stdout.split("\n"):
            line = line.strip()
            if line.startswith("{") and ("instructions" in line or "symbol" in line):
                json_line = line
                break
        if not json_line:
            return None

        # Resolve the .obj path for jump table support
        unit_clean = unit.replace("default/", "")
        obj_path = REPO_ROOT / "build" / "373307D9" / "obj" / f"{unit_clean}.obj"

        # Convert to m2c assembly
        convert_cmd = ["python3", str(OBJDIFF_TO_M2C_PATH)]
        if obj_path.exists():
            convert_cmd.extend(["--obj", str(obj_path)])
        convert_cmd.extend(["--project-dir", str(REPO_ROOT)])

        convert_result = subprocess.run(
            convert_cmd, input=json_line,
            capture_output=True, text=True, timeout=30,
        )
        if convert_result.returncode != 0 or not convert_result.stdout.strip():
            return None

        # Run m2c
        m2c_cmd = ["python3", str(M2C_PATH), "-t", "ppc", "--valid-syntax", "-"]
        m2c_result = subprocess.run(
            m2c_cmd, input=convert_result.stdout,
            capture_output=True, text=True, timeout=60,
        )
        if m2c_result.returncode != 0:
            return None

        output = m2c_result.stdout.strip()
        if not output or "Decompilation failure" in output:
            return None

        return output

    except Exception as e:
        print(f"  m2c error: {e}", file=sys.stderr)
        return None


# ── Context gathering ───────────────────────────────────────────────────


def get_objdiff_markdown(symbol: str) -> str | None:
    """Run objdiff and get LLM-optimized markdown output."""
    try:
        result = subprocess.run(
            ["bin/objdiff-cli", "diff", "-p", ".", "-f", "markdown", symbol],
            capture_output=True,
            text=True,
            timeout=60,
            cwd=str(REPO_ROOT),
        )
        if result.returncode == 0 and result.stdout.strip():
            return result.stdout.strip()
    except Exception as e:
        print(f"  objdiff markdown error: {e}", file=sys.stderr)
    return None


def get_objdiff_percent(symbol: str) -> float:
    """Run objdiff and parse match% from markdown output."""
    try:
        result = subprocess.run(
            ["bin/objdiff-cli", "diff", "-p", ".", "-f", "markdown", symbol],
            capture_output=True,
            text=True,
            timeout=60,
            cwd=str(REPO_ROOT),
        )
        if result.returncode == 0:
            m = re.search(r"\*\*Match\*\*:\s*([\d.]+)%", result.stdout)
            if m:
                return float(m.group(1))
    except Exception as e:
        print(f"  objdiff score error: {e}", file=sys.stderr)
    return 0.0


def extract_function_source(source_path: Path, function_name: str) -> str | None:
    """Extract function source using tree-sitter."""
    try:
        result = subprocess.run(
            [
                "venv/bin/python", "-c",
                f"""
import sys
sys.path.insert(0, '.')
from scripts.permuter.extractor import extract_function
from pathlib import Path
ctx = extract_function(Path({json.dumps(str(source_path))}), {json.dumps(function_name)})
start, end = ctx.func_byte_range
print(ctx.file_source[start:end].decode('utf-8'))
"""
            ],
            capture_output=True,
            text=True,
            timeout=30,
            cwd=str(REPO_ROOT),
        )
        if result.returncode == 0 and result.stdout.strip():
            return result.stdout.strip()
    except Exception:
        pass

    # Fallback: read whole file
    try:
        return source_path.read_text()
    except Exception:
        return None


def load_pattern_summary() -> str:
    """Load a condensed summary of proven decomp patterns."""
    summary_parts = []

    # Load from INDEX.md (the table is the most useful part)
    index_path = PATTERNS_DIR / "INDEX.md"
    if index_path.exists():
        text = index_path.read_text()
        # Extract the fixable patterns table
        in_table = False
        for line in text.splitlines():
            if "| Pattern |" in line:
                in_table = True
            if in_table:
                summary_parts.append(line)
                if line.strip() == "" and in_table:
                    in_table = False

    # Also load key patterns from specific files (condensed)
    key_files = [
        ("fixable-casting.md", "Casting patterns"),
        ("fixable-comparison.md", "Comparison patterns"),
        ("fixable-control-flow.md", "Control flow patterns"),
        ("fixable-declarations.md", "Declaration patterns"),
    ]
    for fname, label in key_files:
        fpath = PATTERNS_DIR / fname
        if fpath.exists():
            text = fpath.read_text()
            # Extract just the h2/h3 headers and first line of each
            for line in text.splitlines():
                if line.startswith("## ") or line.startswith("### "):
                    summary_parts.append(f"- {label}: {line.lstrip('#').strip()}")

    return "\n".join(summary_parts[:80])  # Cap at 80 lines


# ── Prompt construction ─────────────────────────────────────────────────


def build_prompt(
    source: str,
    ghidra_code: str | None,
    m2c_code: str | None,
    objdiff_markdown: str | None,
    pattern_summary: str,
    function_name: str,
    current_percent: float,
) -> str:
    """Build the structured prompt for the AI advisor."""

    sections = []

    sections.append(textwrap.dedent(f"""\
        You are analyzing a PowerPC (Xbox 360, MSVC compiler) decompilation mismatch.
        The goal is to make the C++ source compile to identical assembly as the original binary.

        Function: {function_name}
        Current match: {current_percent:.1f}%

        IMPORTANT CONSTRAINTS:
        - This is MSVC for PowerPC (Xbox 360). NOT x86, NOT GCC.
        - The target binary is a DEBUG build (no LTCG, no aggressive optimization).
        - Small source changes can have large effects on register allocation and instruction scheduling.
        - Do NOT rewrite the whole function. Propose minimal, targeted edits.
        - Each edit should target ONE specific mismatch pattern.
    """))

    sections.append("## Current Source\n```cpp\n" + source + "\n```\n")

    if ghidra_code:
        sections.append(
            "## Target (Ghidra Decompilation)\n"
            "This shows what the original binary's code looks like when decompiled.\n"
            "Use it to understand the TARGET structure, control flow, and variable usage.\n"
            "Ghidra is best for: semantic anchors, function/field names, assert strings, "
            "higher-level intent.\n"
            "```c\n" + ghidra_code + "\n```\n"
        )

    if m2c_code:
        sections.append(
            "## Target (m2c Decompilation)\n"
            "This is a lower-level machine-shaped decompilation of the same target binary.\n"
            "m2c preserves more compiler-shaped structure than Ghidra:\n"
            "- Explicit temporaries and stack-slot groupings\n"
            "- Raw call gating and branch shapes closer to emitted code\n"
            "- Temp reuse and lifetime windows\n"
            "- Loop entry/exit forms matching the actual assembly\n"
            "Use m2c to understand HOW the compiler shaped the code (scheduling, temps, "
            "call shells). Use Ghidra to understand WHAT the code does.\n"
            "When Ghidra and m2c agree on structure, that's a strong signal.\n"
            "When they disagree, m2c is usually more reliable for call-site shape and "
            "loop forms.\n"
            "```c\n" + m2c_code + "\n```\n"
        )

    if objdiff_markdown:
        sections.append(
            "## Objdiff Analysis\n"
            "This is the instruction-level diff between your compiled output and the target.\n"
            "Use it to understand exactly where and how the assembly diverges.\n\n"
            + objdiff_markdown + "\n"
        )

    if pattern_summary:
        sections.append(
            "## Known Patterns That Work\n"
            "These are proven patterns for this compiler. Use them to guide your suggestions.\n"
            + pattern_summary + "\n"
        )

    sections.append(textwrap.dedent("""\
        ## Your Task

        Propose 1-5 specific source edits that could improve the match percentage.

        For each edit, respond with a JSON object in this exact format:
        ```json
        [
            {
                "edit_type": "descriptive_name",
                "description": "What this edit does and why",
                "search": "exact source text to find",
                "replace": "replacement text",
                "confidence": "low|medium|high",
                "reasoning": "Why this specific change should help based on the mismatch data"
            }
        ]
        ```

        RULES:
        - "search" must be an EXACT substring of the current source (copy-paste precision)
        - "replace" must be valid C++ that compiles
        - Each edit should be independent (don't assume previous edits were applied)
        - Focus on structural changes, not cosmetic ones
        - Prefer changes that affect control flow, variable lifetime, or type casting
        - Do NOT add comments, do NOT add whitespace-only changes
        - If you see a pattern from the "Known Patterns" section that applies, use it

        Common high-value edits for this compiler:
        1. Splitting `a && b` into nested `if` statements (or vice versa)
        2. Changing `(unsigned)x` to `(int)x` or vice versa
        3. Extracting subexpressions into local variables
        4. Changing loop form (for/while/do-while)
        5. Reordering independent statements
        6. Adding/removing explicit casts (float vs double, signed vs unsigned)
        7. Changing `!= 0` to `> 0` for unsigned comparisons
        8. Splitting compound boolean expressions
    """))

    return "\n".join(sections)


# ── API call ────────────────────────────────────────────────────────────


def call_advisor(prompt: str, model: str = "claude-sonnet-4-6") -> list[EditSuggestion]:
    """Call the Claude API with the structured prompt.

    Supports both direct Anthropic API (ANTHROPIC_API_KEY) and
    OpenRouter (OPENROUTER_API_KEY) as a fallback.
    """
    anthropic_key = os.environ.get("ANTHROPIC_API_KEY")
    openrouter_key = os.environ.get("OPENROUTER_API_KEY")

    if anthropic_key:
        return _call_anthropic(prompt, model, anthropic_key)
    elif openrouter_key:
        return _call_openrouter(prompt, model, openrouter_key)
    else:
        print(
            "Error: No API key found. Set ANTHROPIC_API_KEY or OPENROUTER_API_KEY.",
            file=sys.stderr,
        )
        return []


def _call_anthropic(prompt: str, model: str, api_key: str) -> list[EditSuggestion]:
    """Call via Anthropic API."""
    import anthropic

    client = anthropic.Anthropic(api_key=api_key)
    print(f"  Calling {model} (Anthropic)...", file=sys.stderr)
    response = client.messages.create(
        model=model,
        max_tokens=4096,
        messages=[{"role": "user", "content": prompt}],
    )
    text = response.content[0].text
    suggestions = parse_suggestions(text)
    print(
        f"  Got {len(suggestions)} suggestions "
        f"(input={response.usage.input_tokens}, output={response.usage.output_tokens})",
        file=sys.stderr,
    )
    return suggestions


def _call_openrouter(prompt: str, model: str, api_key: str) -> list[EditSuggestion]:
    """Call via OpenRouter API."""
    import httpx

    # Map model IDs to OpenRouter format
    MODEL_MAP = {
        "claude-sonnet-4-6": "anthropic/claude-sonnet-4.6",
        "claude-opus-4-6": "anthropic/claude-opus-4.6",
        "claude-haiku-4-5": "anthropic/claude-haiku-4.5",
    }
    or_model = MODEL_MAP.get(model, model)
    if "/" not in or_model:
        or_model = f"anthropic/{or_model}"

    print(f"  Calling {or_model} (OpenRouter)...", file=sys.stderr)
    response = httpx.post(
        "https://openrouter.ai/api/v1/chat/completions",
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json",
        },
        json={
            "model": or_model,
            "messages": [{"role": "user", "content": prompt}],
            "max_tokens": 4096,
        },
        timeout=120,
    )
    if response.status_code != 200:
        print(f"  OpenRouter error {response.status_code}: {response.text[:500]}", file=sys.stderr)
        response.raise_for_status()
    data = response.json()
    text = data["choices"][0]["message"]["content"]
    suggestions = parse_suggestions(text)

    usage = data.get("usage", {})
    print(
        f"  Got {len(suggestions)} suggestions "
        f"(input={usage.get('prompt_tokens', '?')}, "
        f"output={usage.get('completion_tokens', '?')})",
        file=sys.stderr,
    )
    return suggestions


def parse_suggestions(text: str) -> list[EditSuggestion]:
    """Parse edit suggestions from LLM response text."""
    suggestions = []

    # Strategy: try fenced code block first (most reliable), then bare array
    json_str = None

    # 1. Try fenced JSON block
    fence_match = re.search(r"```(?:json)?\s*(\[[\s\S]*?\])\s*```", text)
    if fence_match:
        json_str = fence_match.group(1)

    # 2. Try bare JSON array — find balanced brackets
    if json_str is None:
        start = text.find("[")
        if start >= 0:
            depth = 0
            in_string = False
            escape = False
            for i in range(start, len(text)):
                c = text[i]
                if escape:
                    escape = False
                    continue
                if c == "\\":
                    escape = True
                    continue
                if c == '"':
                    in_string = not in_string
                    continue
                if in_string:
                    continue
                if c == "[":
                    depth += 1
                elif c == "]":
                    depth -= 1
                    if depth == 0:
                        json_str = text[start : i + 1]
                        break

    if json_str is None:
        print("  Warning: could not find JSON array in response", file=sys.stderr)
        return []

    try:
        items = json.loads(json_str)
    except json.JSONDecodeError:
        # Fix common LLM issue: literal newlines inside string values
        try:
            fixed = json_str.replace("\n", "\\n")
            # But don't escape newlines that are structural (between entries)
            # Re-parse by replacing \\n back outside of strings
            items = json.loads(fixed)
        except json.JSONDecodeError as e:
            print(f"  Warning: JSON parse error: {e}", file=sys.stderr)
            print(f"  First 200 chars: {json_str[:200]}", file=sys.stderr)
            return []

    if not isinstance(items, list):
        items = [items]

    for item in items:
        if not isinstance(item, dict):
            continue
        if "search" not in item or "replace" not in item:
            continue
        suggestions.append(
            EditSuggestion(
                edit_type=item.get("edit_type", "unknown"),
                description=item.get("description", ""),
                search=item["search"],
                replace=item["replace"],
                confidence=item.get("confidence", "medium"),
                reasoning=item.get("reasoning", ""),
            )
        )

    return suggestions


# ── Testing suggestions ─────────────────────────────────────────────────


def test_suggestion(
    source_path: Path,
    symbol: str,
    suggestion: EditSuggestion,
    original_source: bytes,
    obj_target: str,
) -> dict:
    """Apply a suggestion, build, and score. Returns result dict."""
    source_text = original_source.decode("utf-8")

    # Find and replace
    if suggestion.search not in source_text:
        return {
            "suggestion": suggestion.edit_type,
            "description": suggestion.description,
            "percent": 0.0,
            "error": "search text not found in source",
            "improved": False,
        }

    modified = source_text.replace(suggestion.search, suggestion.replace, 1)
    modified_bytes = modified.encode("utf-8")

    # Write modified source
    source_path.write_bytes(modified_bytes)

    try:
        # Build
        build_result = subprocess.run(
            ["ninja", obj_target],
            capture_output=True,
            text=True,
            timeout=60,
            cwd=str(REPO_ROOT),
        )
        if build_result.returncode != 0:
            return {
                "suggestion": suggestion.edit_type,
                "description": suggestion.description,
                "percent": 0.0,
                "error": "build failed",
                "improved": False,
            }

        # Score with objdiff
        percent = get_objdiff_percent(symbol)
        return {
            "suggestion": suggestion.edit_type,
            "description": suggestion.description,
            "percent": percent,
            "improved": False,  # Caller sets this
        }
    except Exception as e:
        return {
            "suggestion": suggestion.edit_type,
            "description": suggestion.description,
            "percent": 0.0,
            "error": str(e),
            "improved": False,
        }
    finally:
        # Always restore original
        source_path.write_bytes(original_source)


# ── Main flow ───────────────────────────────────────────────────────────


def run_advisor(
    symbol_query: str,
    apply: bool = False,
    dry_run: bool = False,
    model: str = "claude-sonnet-4-6",
    max_suggestions: int = 5,
    iterations: int = 1,
) -> AdvisorResult:
    """Run the AI advisor on a function."""
    start_time = time.time()

    # 1. Resolve function
    print(f"Resolving '{symbol_query}'...", file=sys.stderr)
    func_info = resolve_symbol(symbol_query)
    if not func_info:
        return AdvisorResult(
            symbol=symbol_query,
            function_name=symbol_query,
            source_path="",
            initial_percent=0.0,
            suggestions=[],
            error=f"Could not resolve '{symbol_query}' in decomp.db",
        )

    symbol = func_info["symbol"]
    demangled = func_info["demangled"]
    unit = func_info["unit"]
    source_path = Path(func_info["source_path"])
    obj_target = unit_to_obj_target(unit)
    current_percent = func_info["current_percent"]

    # Extract qualified name from demangled
    from scripts.permuter.types import extract_qualified_name
    function_name = extract_qualified_name(demangled) or demangled

    print(
        f"  {function_name} ({current_percent:.1f}%) in {source_path}",
        file=sys.stderr,
    )

    # 2. Gather context
    print("Gathering context...", file=sys.stderr)

    # Source
    func_source = extract_function_source(source_path, function_name)
    if not func_source:
        return AdvisorResult(
            symbol=symbol,
            function_name=function_name,
            source_path=str(source_path),
            initial_percent=current_percent,
            suggestions=[],
            error="Could not extract function source",
        )

    # Ghidra
    ghidra_code = get_ghidra_decomp(symbol)
    if ghidra_code:
        print(f"  Ghidra: {len(ghidra_code)} bytes", file=sys.stderr)
    else:
        print("  Ghidra: no cached decompilation", file=sys.stderr)

    # Objdiff (markdown for LLM, percent for scoring)
    objdiff_markdown = None
    if not dry_run:
        # Build first so objdiff has fresh .obj
        build_result = subprocess.run(
            ["ninja", obj_target],
            capture_output=True, text=True, timeout=60, cwd=str(REPO_ROOT),
        )
        if build_result.returncode == 0:
            objdiff_markdown = get_objdiff_markdown(symbol)
            current_percent = get_objdiff_percent(symbol)
            print(f"  Objdiff: {current_percent:.1f}%", file=sys.stderr)
        else:
            print("  Objdiff: build failed", file=sys.stderr)

    # m2c decompilation (structural view of target)
    m2c_code = None
    if not dry_run:
        m2c_code = get_m2c_decomp(symbol, unit)
        if m2c_code:
            print(f"  m2c: {len(m2c_code)} bytes", file=sys.stderr)
        else:
            print("  m2c: decompilation failed or unavailable", file=sys.stderr)

    # Patterns
    pattern_summary = load_pattern_summary()

    # 3. Build prompt and call API
    prompt = build_prompt(
        source=func_source,
        ghidra_code=ghidra_code,
        m2c_code=m2c_code,
        objdiff_markdown=objdiff_markdown,
        pattern_summary=pattern_summary,
        function_name=function_name,
        current_percent=current_percent,
    )

    print(f"Prompt: {len(prompt)} chars", file=sys.stderr)

    if dry_run:
        print("\n--- DRY RUN: Prompt ---", file=sys.stderr)
        print(prompt, file=sys.stderr)
        return AdvisorResult(
            symbol=symbol,
            function_name=function_name,
            source_path=str(source_path),
            initial_percent=current_percent,
            suggestions=[],
            elapsed_seconds=time.time() - start_time,
        )

    all_suggestions = []
    all_tested = []
    overall_best_percent = current_percent
    overall_best_idx = -1
    applied = False
    initial_source = source_path.read_bytes()
    round_percent = current_percent

    for iteration in range(iterations):
        if iteration > 0:
            # Re-gather context with updated source for subsequent rounds
            print(f"\n--- Iteration {iteration + 1}/{iterations} (current: {round_percent:.1f}%) ---", file=sys.stderr)
            func_source = extract_function_source(source_path, function_name)
            if not func_source:
                break

            # Rebuild and get fresh objdiff
            build_result = subprocess.run(
                ["ninja", obj_target],
                capture_output=True, text=True, timeout=60, cwd=str(REPO_ROOT),
            )
            if build_result.returncode == 0:
                objdiff_markdown = get_objdiff_markdown(symbol)
                round_percent = get_objdiff_percent(symbol)
            else:
                break

            prompt = build_prompt(
                source=func_source,
                ghidra_code=ghidra_code,
                m2c_code=m2c_code,
                objdiff_markdown=objdiff_markdown,
                pattern_summary=pattern_summary,
                function_name=function_name,
                current_percent=round_percent,
            )

        suggestions = call_advisor(prompt, model=model)
        if not suggestions:
            if iteration == 0:
                return AdvisorResult(
                    symbol=symbol,
                    function_name=function_name,
                    source_path=str(source_path),
                    initial_percent=current_percent,
                    suggestions=[],
                    error="No suggestions returned",
                    elapsed_seconds=time.time() - start_time,
                )
            break

        # Test each suggestion
        print(f"\nTesting {len(suggestions)} suggestions...", file=sys.stderr)
        round_source = source_path.read_bytes()
        round_best_percent = round_percent
        round_best_idx = -1

        for i, suggestion in enumerate(suggestions):
            print(
                f"  [{i + 1}/{len(suggestions)}] {suggestion.edit_type}: "
                f"{suggestion.description[:60]}...",
                file=sys.stderr,
            )
            result = test_suggestion(source_path, symbol, suggestion, round_source, obj_target)
            result["improved"] = result["percent"] > round_percent
            result["iteration"] = iteration

            if result.get("error"):
                print(f"    ERROR: {result['error']}", file=sys.stderr)
            else:
                marker = ""
                if result["percent"] > round_best_percent:
                    round_best_percent = result["percent"]
                    round_best_idx = i
                    marker = " << BEST"
                elif result["improved"]:
                    marker = " (improved)"
                print(
                    f"    {result['percent']:.2f}%{marker}",
                    file=sys.stderr,
                )

            all_tested.append(result)

        all_suggestions.extend(suggestions)

        # Track overall best
        if round_best_percent > overall_best_percent:
            overall_best_percent = round_best_percent
            overall_best_idx = len(all_suggestions) - len(suggestions) + round_best_idx

        # For multi-iteration: apply best of this round and continue
        if iterations > 1 and round_best_idx >= 0 and round_best_percent > round_percent:
            best_suggestion = suggestions[round_best_idx]
            source_text = round_source.decode("utf-8")
            modified = source_text.replace(best_suggestion.search, best_suggestion.replace, 1)
            source_path.write_bytes(modified.encode("utf-8"))
            round_percent = round_best_percent
            applied = True
            print(
                f"\n  Applied for next round: {best_suggestion.edit_type} "
                f"({round_percent:.1f}%)",
                file=sys.stderr,
            )
        elif iterations > 1:
            print(f"\n  No improvement in round {iteration + 1}, stopping.", file=sys.stderr)
            break

    # Final apply/restore logic
    if iterations == 1:
        # Single iteration: apply only if --apply
        if apply and overall_best_idx >= 0 and overall_best_percent > current_percent:
            best_suggestion = all_suggestions[overall_best_idx]
            source_text = initial_source.decode("utf-8")
            modified = source_text.replace(best_suggestion.search, best_suggestion.replace, 1)
            source_path.write_bytes(modified.encode("utf-8"))
            applied = True
            print(
                f"\nApplied: {best_suggestion.edit_type} "
                f"({current_percent:.1f}% -> {overall_best_percent:.1f}%)",
                file=sys.stderr,
            )
        else:
            # Restore original
            source_path.write_bytes(initial_source)
            if overall_best_idx >= 0 and overall_best_percent > current_percent:
                print(
                    f"\nBest: {all_suggestions[overall_best_idx].edit_type} "
                    f"({current_percent:.1f}% -> {overall_best_percent:.1f}%) "
                    f"[NOT APPLIED - use --apply]",
                    file=sys.stderr,
                )
    elif not apply:
        # Multi-iteration without --apply: restore original
        source_path.write_bytes(initial_source)
        applied = False
        if overall_best_percent > current_percent:
            print(
                f"\nTotal improvement: {current_percent:.1f}% -> {overall_best_percent:.1f}% "
                f"[NOT APPLIED - use --apply]",
                file=sys.stderr,
            )
    else:
        # Multi-iteration with --apply: already applied incrementally
        if overall_best_percent > current_percent:
            print(
                f"\nTotal improvement: {current_percent:.1f}% -> {overall_best_percent:.1f}%",
                file=sys.stderr,
            )

    elapsed = time.time() - start_time
    return AdvisorResult(
        symbol=symbol,
        function_name=function_name,
        source_path=str(source_path),
        initial_percent=current_percent,
        suggestions=all_suggestions,
        tested=all_tested,
        best_percent=overall_best_percent,
        best_suggestion_idx=overall_best_idx,
        applied=applied,
        elapsed_seconds=round(elapsed, 2),
    )


def main():
    parser = argparse.ArgumentParser(
        description="AI-guided edit advisor for decomp functions",
    )
    parser.add_argument(
        "--symbol",
        required=True,
        help="Function name or mangled symbol to advise on",
    )
    parser.add_argument(
        "--apply",
        action="store_true",
        help="Apply the best improvement to source",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Show the prompt without calling the API",
    )
    parser.add_argument(
        "--model",
        default="claude-sonnet-4-6",
        help="Model to use (default: claude-sonnet-4-6). Options: claude-sonnet-4-6, claude-opus-4-6, claude-haiku-4-5",
    )
    parser.add_argument(
        "--iterations",
        type=int,
        default=1,
        help="Number of advisor rounds (each builds on the best result). Default: 1",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        dest="json_output",
        help="Output results as JSON",
    )
    args = parser.parse_args()

    result = run_advisor(
        symbol_query=args.symbol,
        apply=args.apply,
        dry_run=args.dry_run,
        model=args.model,
        iterations=args.iterations,
    )

    if args.json_output:
        print(json.dumps(asdict(result), indent=2))
    else:
        print(f"\n{'=' * 60}")
        print("AI ADVISOR RESULT")
        print(f"{'=' * 60}")
        print(f"  Function:  {result.function_name}")
        print(f"  Initial:   {result.initial_percent:.1f}%")
        print(f"  Best:      {result.best_percent:.1f}%")
        if result.best_suggestion_idx >= 0:
            best = result.suggestions[result.best_suggestion_idx]
            print(f"  Winner:    {best.edit_type} — {best.description}")
        print(f"  Applied:   {result.applied}")
        print(f"  Elapsed:   {result.elapsed_seconds:.1f}s")

        if result.suggestions:
            print(f"\n  Suggestions:")
            for i, s in enumerate(result.suggestions):
                tested = result.tested[i] if i < len(result.tested) else None
                if tested:
                    status = tested.get("error", f"{tested['percent']:.1f}%")
                    if tested["improved"]:
                        status = f"{tested['percent']:.1f}% IMPROVED"
                    marker = f" -> {status}"
                else:
                    marker = ""

                winner = " << BEST" if i == result.best_suggestion_idx else ""
                print(f"\n    [{i + 1}] {s.edit_type} ({s.confidence}){marker}{winner}")
                print(f"        {s.description}")
                if s.reasoning:
                    print(f"        Why: {s.reasoning}")
                print(f"        Search:  {s.search[:80]}{'...' if len(s.search) > 80 else ''}")
                print(f"        Replace: {s.replace[:80]}{'...' if len(s.replace) > 80 else ''}")

        if result.error:
            print(f"\n  Error: {result.error}")


if __name__ == "__main__":
    main()
