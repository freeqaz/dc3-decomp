#!/usr/bin/env python3
"""Calibrate MSVC's local-static scope ordinal: what exactly does it count?

MSVC parks a function-local static as `?<var>@?<ord>??<enclosing fn>`, where
<ord> is a running count of the lexical scopes opened in the function up to and
including the one holding the static (one digit for 0-9, else base-16 digits
A-P terminated by `@`).  That number is a witness to SOURCE STRUCTURE that
survives into the object file even when every instruction matches -- so knowing
the price of each construct turns "retail says 5, we say 4" from a curiosity
into a statement about what retail wrote.

Appends probe functions to a real TU, builds it (one-TU ninja, ~1s behind the
PCH), and reads each probe's ordinal out of the COFF.  The TU is restored.

    calib_scope_ordinal.py <repo> [name-substring]

Measured on dc3 (MSVC PPC, `/EHsc`, `src/system/ui/UIList.cpp`) 2026-08-12.
`base` is the ordinal a static gets at function top level with nothing before
it, which is 1; the table is the DELTA each construct adds.

    construct                                              delta
    ------------------------------------------------------ -----
    (function body, nothing before the static)               +0
    a bare `{ }` before it                                   +1
    `if (c) S;`   before it   (unbraced substatement)        +2
    `if (c) ;`    before it                                  +2
    `if (c) { }`  before it                                  +3
    `if (c) S; else S;`             before it                +3
    `if (c) S; else { }`            before it                +4
    `if (c) { } else S;`            before it                +4
    `for`/`while`/`do`/`switch` with a braced body, before   +2
    `for (;;) break;` / `for (...) S;` / `switch (c) S;`     +1
    `switch (c) { case 0: S; }` before it                    +2
    `try { S } catch (...) { }` before it                    +2
    INSIDE `if (c) {`                                        +3
    INSIDE the `else {` of `if (c) { } else {`               +5
    INSIDE the `else {` of `if (c) S; else {`                +4   <- retail
    INSIDE the `else {` of `if (c) ; else {`                 +4
    INSIDE `else if (c) {` of `if (c) S; else if (c) {`      +6
    INSIDE `for (...) {` / `switch` `case` / `catch (...) {` +2
    INSIDE `try {`                                           +1
    one extra nesting level inside any of the above          +1
    MILO_ASSERT(c, line) before it   (`do { if (!c) {} } while (0)`) +5
    MILO_NOTIFY / MILO_WARN / MILO_LOG before it              +0
    a destructible local (`String t;`), a temporary           +0
    a `const Symbol &` bound to a temporary                   +0
    an inlined callee -- with a bare block, a loop, a `try`,
      its own static, or used as the static's initialiser     +0
    a ternary initialiser, a comma expression                 +0
    `if (a && b)` vs `if (a)`, redundant parens               +0
    `#pragma warning(push/pop)`                               +0
    a declaration-in-condition, a goto label                  +0

Two independent checks that the table reads real source and is not fitted:

  * `OBJ_CLASSNAME`'s `static Symbol name(#classname)` sits at the top of
    `StaticClassName()` -> predicted 1, and it has ZERO name_check charges
    across the tree, so retail is 1 too.
  * `MonthToken` (`src/system/os/DateTime.cpp`) has a MILO_ASSERT before its
    `static Symbol month_symbols[12]` -> predicted 1+5 = 6, and our object says
    6.  (Retail says 1 there, which is a separate open finding: retail's
    assert cannot have preceded that static.)
"""
import re
import subprocess
import sys
from pathlib import Path

from coffsyms import symbols

TU = "src/system/ui/UIList.cpp"
OBJ = "build/373307D9/src/system/ui/UIList.obj"

# A statically-initialised `int` needs no guard and is not parked in a scope, so
# every probe uses a DYNAMICALLY initialised static -- the same shape as the
# `static DataArray *types = SystemConfig(...)` this exists to calibrate.
S = "static int v = Rand(); (void)v;"

PROBES = {
    # --- the base table ---
    "p_plain": S,
    "p_one_block": "{ } " + S,
    "p_after_if_braced": "if (Rand()) { } " + S,
    "p_inside_if_braced": "if (Rand()) { " + S + " }",
    "p_if_else_inside_else": "if (Rand()) { } else { " + S + " }",
    # --- unbraced substatements: the cheapest scope units there are ---
    "p_after_if_bare": "if (Rand()) Sink(); " + S,
    "p_after_if_empty": "if (Rand()) ; " + S,
    "p_bare_if_else_in_else": "if (Rand()) Sink(); else { " + S + " }",
    "p_empty_if_else_in_else": "if (Rand()) ; else { " + S + " }",
    "p_bare_if_bare_else": "if (Rand()) Sink(); else Sink(); " + S,
    "p_braced_if_bare_else": "if (Rand()) { } else Sink(); " + S,
    "p_bare_if_braced_else": "if (Rand()) Sink(); else { } " + S,
    # --- loops / switch ---
    "p_for_ever_break": "for (;;) break; " + S,
    "p_for_bare": "for (int i = 0; i < 1; ++i) Sink(); " + S,
    "p_switch_one_case": "switch (Rand()) { case 0: Sink(); } " + S,
    "p_switch_bare": "switch (Rand()) Sink(); " + S,
    "p_inside_switch_case": "switch (Rand()) { case 0: " + S + " } ",
    "p_inside_for": "for (int i = 0; i < 1; ++i) { " + S + " }",
    # --- exception scopes (this TU is /EHsc) ---
    "p_try_catch": "try { Sink(); } catch (...) { } " + S,
    "p_inside_try": "try { " + S + " } catch (...) { }",
    "p_inside_catch": "try { Sink(); } catch (...) { " + S + " }",
    # --- the Milo diagnostic macros, placed BEFORE the static ---
    "p_milo_assert": "MILO_ASSERT(Rand(), 0x10); " + S,
    "p_milo_notify": 'MILO_NOTIFY("x %d", Rand()); ' + S,
    "p_milo_warn": 'MILO_WARN("x %d", Rand()); ' + S,
    "p_milo_log": 'MILO_LOG("x %d", Rand()); ' + S,
    "p_assert_in_if": "if (Rand()) { MILO_ASSERT(Rand(), 0x10); " + S + " }",
    "p_notify_in_if": 'if (Rand()) { MILO_NOTIFY("x %d", Rand()); ' + S + " }",
    # --- temporaries, references, ctor/dtor scopes ---
    "p_const_ref_temp": 'const Symbol &r = Symbol("x"); (void)r; ' + S,
    "p_symbol_temp_in_if": 'if (Rand()) { Symbol s("x"); ' + S + " }",
    "p_string_temp_in_if": 'if (Rand()) { String t("x"); ' + S + " }",
    "p_call_temp_in_if": 'if (Rand()) { Sink(String("x").length()); ' + S + " }",
    # --- inlined callees whose BODY opens a scope ---
    "p_inline_bare_block": "if (Rand()) { InlineWithBareBlock(); " + S + " }",
    "p_inline_loop": "if (Rand()) { InlineWithLoop(); " + S + " }",
    "p_inline_try": "if (Rand()) { InlineWithTry(); " + S + " }",
    "p_inline_in_init": "if (Rand()) { static int v = InlineWithBareBlock(); (void)v; }",
    # --- preprocessor / expression shapes ---
    "p_pragma_before": "\n#pragma warning(push)\n#pragma warning(pop)\n" + S,
    "p_comma_expr": "(void)(Rand(), Rand()); " + S,
    "p_cond_expr": "(void)(Rand() ? Rand() : Rand()); " + S,
    "p_short_circuit": "if (Rand() && Rand()) { " + S + " }",
    "p_nested_paren_block": "if ((Rand())) { " + S + " }",
    "p_else_if_chain": "if (Rand()) Sink(); else if (Rand()) { " + S + " }",
    "p_if_in_else_braced": "if (Rand()) Sink(); else { if (Rand()) { } " + S + " }",
}

PRELUDE = """
extern int Rand();
void Sink(int = 0);
inline int InlineWithBareBlock() { { int q = Rand(); return q; } }
inline int InlineWithLoop() { for (int i = 0; i < 2; ++i) Sink(i); return 0; }
inline int InlineWithTry() { try { Sink(); } catch (...) { } return 0; }
"""

HEX = "0123456789ABCDEFGHIJKLMNOP"


def decode(o):
    """`?<n>?` is one digit for 0-9, else base-16 digits A-P terminated by `@`."""
    if o.endswith("@"):
        n = 0
        for ch in o[:-1]:
            n = n * 16 + HEX.index(ch)
        return n
    return int(o)


def build(repo, probes):
    code = ["\n// ---- scope-ordinal calibration probes (temporary) ----\n", PRELUDE]
    for name, body in probes.items():
        code.append(f"void {name}() {{ {body} }}\n")
    p = Path(repo) / TU
    orig = p.read_text()
    p.write_text(orig + "".join(code))
    r = subprocess.run(["ninja", OBJ], cwd=repo, capture_output=True, text=True)
    syms = symbols(Path(repo) / OBJ) if r.returncode == 0 else []
    p.write_text(orig)
    subprocess.run(["ninja", OBJ], cwd=repo, capture_output=True)
    return r, syms


def main():
    repo = sys.argv[1]
    probes = PROBES
    if len(sys.argv) > 2:
        probes = {k: v for k, v in PROBES.items() if sys.argv[2] in k}
    r, syms = build(repo, probes)
    if r.returncode:
        print((r.stdout + r.stderr)[-3000:])
        return
    pat = re.compile(r"^\?v@\?([0-9A-P@]+?)\?\?(p_\w+)@@YAXXZ")
    got = {}
    for s in syms:
        m = pat.match(s)
        if m:
            got[m.group(2)] = decode(m.group(1))
    for name in probes:
        o = got.get(name)
        print(f"  {name:26s} ordinal={o if o is not None else '(not emitted)'}")


if __name__ == "__main__":
    main()
