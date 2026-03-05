"""Validate that Ghidra decompilation output is parseable and structurally useful.

Tests three categories:
1. Expression structure detection (flat vs parenthesized chains)
2. Control flow skeleton extraction
3. Variable first-use order extraction

Run: python -m scripts.permuter.experiments.validate_ghidra_parsing
"""

from __future__ import annotations

import sys
from pathlib import Path

from scripts.permuter.ghidra_cache import get_decompilation
from scripts.permuter.ghidra_ast import (
    parse_ghidra,
    extract_variable_first_use_order,
    extract_expression_structure,
    extract_control_flow_skeleton,
    extract_savegpr_count,
    extract_arithmetic_expressions,
)


def test_metapanel_isloaded():
    """MetaPanel::IsLoaded — verify control flow structure detection.

    Expected: Ghidra shows a conjunction (&&) vs our guard-return structure.
    """
    symbol = "?IsLoaded@MetaPanel@@UBA_NXZ"
    code = get_decompilation(symbol)
    if not code:
        print(f"SKIP: {symbol} not in cache")
        return False

    print(f"\n=== MetaPanel::IsLoaded ===")
    print(f"Ghidra code ({len(code)} bytes):")
    print(code[:500])

    ast = parse_ghidra(code)
    print(f"Parse errors: {ast.has_errors}")
    print(f"Function found: {ast.func_node is not None}")

    skeleton = extract_control_flow_skeleton(ast)
    print(f"Control flow skeleton: {skeleton}")

    vars_order = extract_variable_first_use_order(ast)
    print(f"Variable order: {[(v.name, v.type_prefix) for v in vars_order]}")

    # Check if we can detect conjunction vs guard pattern
    has_conjunction = "&&" in code or "||" in code
    has_guard = skeleton.count("if") > 0 and skeleton.count("return") > 1
    print(f"Has conjunction (&&/||): {has_conjunction}")
    print(f"Has guard pattern (if + multiple returns): {has_guard}")

    return True


def test_expression_parsing_synthetic():
    """Test expression structure extraction on synthetic Ghidra-like code."""
    print("\n=== Synthetic Expression Structure Tests ===")

    # Flat chain: a - b + c
    flat_code = """
void test(void) {
    int result;
    result = a - b + c;
}
"""
    flat_ast = parse_ghidra(flat_code)
    exprs = extract_arithmetic_expressions(flat_ast)
    print(f"\nFlat 'a - b + c':")
    for node, struct in exprs:
        print(f"  Structure: {struct}")

    # Parenthesized: a - (b - c)
    paren_code = """
void test(void) {
    int result;
    result = a - (b - c);
}
"""
    paren_ast = parse_ghidra(paren_code)
    exprs = extract_arithmetic_expressions(paren_ast)
    print(f"\nParen 'a - (b - c)':")
    for node, struct in exprs:
        print(f"  Structure: {struct}")

    # FMA-like: a + b * c
    fma_code = """
void test(void) {
    float result;
    result = a + b * c;
}
"""
    fma_ast = parse_ghidra(fma_code)
    exprs = extract_arithmetic_expressions(fma_ast)
    print(f"\nFMA 'a + b * c':")
    for node, struct in exprs:
        print(f"  Structure: {struct}")

    # Verify flat and paren produce different structures
    flat_structs = [s for _, s in extract_arithmetic_expressions(flat_ast)]
    paren_structs = [s for _, s in extract_arithmetic_expressions(paren_ast)]

    if flat_structs and paren_structs:
        if flat_structs[0] != paren_structs[0]:
            print(f"\nSTRUCTURAL DIFF DETECTED:")
            print(f"  Flat:  {flat_structs[0]}")
            print(f"  Paren: {paren_structs[0]}")
            return True
        else:
            print(f"\nWARNING: Same structure for flat and paren!")
            return False
    else:
        print(f"\nWARNING: Could not extract expressions")
        return False


def test_variable_order_from_cache():
    """Test variable ordering on a real Ghidra decompilation."""
    print("\n=== Variable Order from Real Decompilation ===")

    # Find a function with several local variables
    import sqlite3
    conn = sqlite3.connect(str(Path(__file__).resolve().parents[3] / "decomp.db"))
    cur = conn.execute(
        "SELECT symbol, code FROM decompilations "
        "WHERE error IS NULL AND LENGTH(code) BETWEEN 500 AND 2000 "
        "ORDER BY RANDOM() LIMIT 5"
    )
    rows = cur.fetchall()
    conn.close()

    for symbol, code in rows:
        ast = parse_ghidra(code)
        if not ast.func_node:
            continue

        vars_order = extract_variable_first_use_order(ast)
        if len(vars_order) < 3:
            continue

        print(f"\nSymbol: {symbol[:60]}")
        print(f"Variables in first-use order:")
        for v in vars_order:
            print(f"  {v.name:20s}  type_prefix={v.type_prefix!r:4s}  "
                  f"decl_type={v.decl_type!r:20s}  line={v.first_use_line}")

        gpr_count = extract_savegpr_count(code)
        if gpr_count is not None:
            print(f"GPR save count: {gpr_count}")

        return True

    print("No suitable functions found in cache")
    return False


def test_bulk_parseability():
    """Test that the vast majority of cached decompilations parse without errors."""
    print("\n=== Bulk Parseability Test ===")

    import sqlite3
    conn = sqlite3.connect(str(Path(__file__).resolve().parents[3] / "decomp.db"))
    cur = conn.execute(
        "SELECT code FROM decompilations WHERE error IS NULL ORDER BY RANDOM() LIMIT 200"
    )
    rows = cur.fetchall()
    conn.close()

    total = 0
    parsed_ok = 0
    has_func = 0
    has_vars = 0

    for (code,) in rows:
        total += 1
        ast = parse_ghidra(code)
        if not ast.has_errors:
            parsed_ok += 1
        if ast.func_node:
            has_func += 1
            vars_order = extract_variable_first_use_order(ast)
            if vars_order:
                has_vars += 1

    print(f"Total sampled: {total}")
    print(f"Parsed without errors: {parsed_ok} ({100*parsed_ok/total:.1f}%)")
    print(f"Function node found: {has_func} ({100*has_func/total:.1f}%)")
    print(f"Has extractable variables: {has_vars} ({100*has_vars/total:.1f}%)")

    return parsed_ok / total > 0.5  # Success if >50% parse clean


def main():
    results = {}

    results["synthetic_expressions"] = test_expression_parsing_synthetic()
    results["metapanel_isloaded"] = test_metapanel_isloaded()
    results["variable_order"] = test_variable_order_from_cache()
    results["bulk_parseability"] = test_bulk_parseability()

    print("\n" + "=" * 60)
    print("RESULTS:")
    all_pass = True
    for name, passed in results.items():
        status = "PASS" if passed else "FAIL"
        if not passed:
            all_pass = False
        print(f"  {name}: {status}")

    print(f"\nOverall: {'ALL PASS' if all_pass else 'SOME FAILURES'}")
    return 0 if all_pass else 1


if __name__ == "__main__":
    sys.exit(main())
