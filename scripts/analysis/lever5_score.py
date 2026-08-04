#!/usr/bin/env python3
"""Score functions against the Lever-5 signature (unnamed const-ref aggregate temps).

Lever 5 (docs/decomp/patterns/fixable-liveness.md): an unnamed aggregate built
inside a call argument list is a temporary bound to a const reference, so it dies
at the end of its own full-expression.  N such temporaries in a row therefore
share ONE stack slot, while the target -- which had them as named locals --
allocated one slot each (minus whatever the frame packer coalesced).  Result: our
frame is short by k * sizeof(aggregate), and every register downstream of the
first call is permuted.

Two independent signals, both mechanical:

  BINARY   base frame < target frame, deficit a multiple of 16
           (from scripts/analysis/frame_deficit_census.py -- reads `stwu r1,-N`
            straight out of both COFF prologues, no objdiff run needed)

  SOURCE   >= 2 unnamed aggregate constructor temporaries appearing in argument
           position inside the function body, ideally in consecutive statements

Score = 0 unless BOTH fire.  The binary signal alone is weak: -16 is also what a
single missing scalar local looks like after 16-byte stack alignment, and it fires
on 128 functions.  The source signal alone is weak too: a lone temp whose callee
takes it by *value* costs nothing.  The conjunction is what is specific.

Usage:
    python3 scripts/analysis/frame_deficit_census.py --json /tmp/frame.json
    python3 scripts/analysis/lever5_score.py --frames /tmp/frame.json
"""

import argparse
import json
import os
import re
import sys

# Value types that MSVC materializes in a stack slot when built as a temporary
# and bound to a const reference.  Size is the *slot* size (16-byte aligned).
AGGREGATES = {
    "Vector3": 16, "Vector4": 16, "Vector2": 16,
    "Hmx::Color": 16, "Color": 16, "Color32": 16,
    "Transform": 64, "Matrix4": 64, "Hmx::Matrix3": 48, "Matrix3": 48,
    "Hmx::Quat": 16, "Quat": 16,
    "Box": 32, "Sphere": 16, "Plane": 16, "Segment": 32,
    "Symbol": 16, "String": 16, "FilePath": 16, "DataNode": 16,
    "UIComponent::State": 16,
}

# `Foo(` inside an argument list.  We reject:
#   - a declaration:            Vector3 v(a, b, c);   -> has an identifier before
#   - a cast/decl at stmt start
# by requiring the type name to be preceded by '(' or ',' (argument position).
ARG_TEMP_RE = re.compile(
    r"[(,]\s*(?P<ty>" + "|".join(re.escape(t) for t in sorted(AGGREGATES, key=len, reverse=True)) + r")\s*\("
)

# A named local of an aggregate type: `Vector3 v(...)` or `Vector3 v = ...` or `Vector3 v;`
NAMED_LOCAL_RE = re.compile(
    r"^\s*(?:const\s+)?(?P<ty>" + "|".join(re.escape(t) for t in sorted(AGGREGATES, key=len, reverse=True)) +
    r")\s+(?!\*)(?P<name>[A-Za-z_]\w*)\s*[(;=]"
)


def demangled_index(project_dir):
    """mangled symbol -> (demangled, unit, percent)."""
    path = os.path.join(project_dir, "build/373307D9/report.json")
    out = {}
    rep = json.load(open(path))
    for unit in rep.get("units", []):
        src = (unit.get("metadata") or {}).get("source_path", "")
        for fn in unit.get("functions", []) or []:
            d = (fn.get("metadata") or {}).get("demangled_name", "")
            out.setdefault(fn["name"], (d, src, fn.get("match_percent_normalized")))
    return out


def qualified_name(demangled):
    """'void __cdecl Foo::Bar(int)' -> 'Foo::Bar'.  Returns None if unparseable."""
    if not demangled:
        return None
    # strip everything from the first '(' that opens the parameter list
    depth = 0
    cut = len(demangled)
    for i, ch in enumerate(demangled):
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
        elif ch == "(" and depth == 0:
            cut = i
            break
    head = demangled[:cut].strip()
    m = re.search(r"([A-Za-z_~][\w:~]*)\s*$", head)
    return m.group(1) if m else None


def find_body(text, qname):
    """Locate `... Qual::Name(...) { ... }` and return (body, start_line).

    Falls back to the last component if the qualified form is absent (the
    definition may use a different namespace spelling than the mangled name)."""
    cands = [qname]
    if "::" in qname:
        cands.append(qname.split("::")[-1])
    for cand in cands:
        pat = re.compile(r"(?<![\w:])" + re.escape(cand) + r"\s*\(")
        for m in pat.finditer(text):
            # require the match to start a definition: walk forward past the
            # parameter list to a '{' with only qualifiers between
            i = m.end() - 1
            depth = 0
            while i < len(text):
                if text[i] == "(":
                    depth += 1
                elif text[i] == ")":
                    depth -= 1
                    if depth == 0:
                        break
                i += 1
            j = i + 1
            while j < len(text) and text[j] in " \t\r\n" or text[j:j + 5] == "const":
                j += 5 if text[j:j + 5] == "const" else 1
            if j >= len(text) or text[j] != "{":
                continue
            # brace-match the body
            depth = 0
            k = j
            while k < len(text):
                if text[k] == "{":
                    depth += 1
                elif text[k] == "}":
                    depth -= 1
                    if depth == 0:
                        break
                k += 1
            return text[j:k + 1], text.count("\n", 0, m.start()) + 1
    return None, None


def strip_noise(body):
    """Remove comments and string/char literals so the regexes cannot false-fire."""
    body = re.sub(r"/\*.*?\*/", " ", body, flags=re.S)
    body = re.sub(r"//[^\n]*", " ", body)
    body = re.sub(r'"(\\.|[^"\\])*"', '""', body)
    body = re.sub(r"'(\\.|[^'\\])*'", "''", body)
    return body


def score_body(body):
    body = strip_noise(body)
    temps = [m.group("ty") for m in ARG_TEMP_RE.finditer(body)]
    named = [m.group("name") for m in NAMED_LOCAL_RE.finditer(body, )
             ] if False else [m.group("name") for line in body.splitlines()
                              for m in [NAMED_LOCAL_RE.match(line)] if m]
    # consecutive-run detection: how many temps sit in adjacent statements
    stmts = [s for s in body.split(";")]
    runs, cur = [], 0
    for s in stmts:
        if ARG_TEMP_RE.search(s):
            cur += 1
        else:
            if cur:
                runs.append(cur)
            cur = 0
    if cur:
        runs.append(cur)
    return {
        "temps": temps,
        "n_temps": len(temps),
        "longest_run": max(runs) if runs else 0,
        "named_aggregates": named,
        "slot_bytes": sum(AGGREGATES.get(t, 16) for t in temps),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--project-dir", default=os.getcwd())
    ap.add_argument("--frames", required=True, help="JSON from frame_deficit_census.py")
    ap.add_argument("--min-run", type=int, default=2,
                    help="minimum consecutive-statement temp run to score (default 2)")
    ap.add_argument("--json", default=None)
    args = ap.parse_args()

    pd = os.path.abspath(args.project_dir)
    frames = json.load(open(args.frames))
    idx = demangled_index(pd)

    src_cache = {}
    rows = []
    for fr in frames:
        dem, src, pct = idx.get(fr["symbol"], ("", fr.get("source", ""), fr.get("percent")))
        src = src or fr.get("source", "")
        if not src or not src.endswith((".cpp", ".c", ".cc")):
            continue
        path = os.path.join(pd, src)
        if not os.path.exists(path):
            continue
        if path not in src_cache:
            src_cache[path] = open(path, errors="replace").read()
        qn = qualified_name(dem) or ""
        if not qn:
            continue
        body, line = find_body(src_cache[path], qn)
        if body is None:
            continue
        sc = score_body(body)
        if sc["longest_run"] < args.min_run:
            continue
        deficit = -fr["delta"]
        # does the source-side temp budget explain the deficit?
        explains = "YES" if sc["slot_bytes"] >= deficit else (
            "PARTIAL" if sc["slot_bytes"] >= 16 and deficit % 16 == 0 else "NO")
        rows.append({
            "symbol": fr["symbol"], "demangled": dem, "source": src, "line": line,
            "percent": fr.get("percent"), "target_frame": fr["target_frame"],
            "base_frame": fr["base_frame"], "deficit": deficit,
            "n_temps": sc["n_temps"], "longest_run": sc["longest_run"],
            "types": sorted(set(sc["temps"])), "slot_bytes": sc["slot_bytes"],
            "explains_deficit": explains,
            "named_aggregates": sc["named_aggregates"],
        })

    rows.sort(key=lambda r: (r["explains_deficit"] != "YES",
                             -(r["longest_run"]),
                             r["percent"] if r["percent"] is not None else 0))
    if args.json:
        json.dump(rows, open(args.json, "w"), indent=1)

    print(f"# {len(rows)} functions with BOTH a frame deficit and >={args.min_run} "
          f"consecutive-statement aggregate temporaries")
    print(f"# {'match%':>7} {'deficit':>8} {'run':>4} {'budget':>7} {'expl':>8}  symbol")
    for r in rows:
        p = f"{r['percent']:.1f}" if r["percent"] is not None else " n/a"
        print(f"{p:>9} {r['deficit']:>8} {r['longest_run']:>4} {r['slot_bytes']:>7} "
              f"{r['explains_deficit']:>8}  {r['demangled'] or r['symbol']}")
        print(f"{'':>9}   {r['source']}:{r['line']}  types={','.join(r['types'])}")


if __name__ == "__main__":
    main()
