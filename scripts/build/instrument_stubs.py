#!/usr/bin/env python3
"""Instrument native engine stubs with HX_STUB_TRACE (roadmap N.2).

Walks native/src/engine_stubs_generated.cpp and inserts a
  HX_STUB_TRACE("<symbol>");
as the first statement of every single-line stub function body, so a runtime
opt-in counter (DC3_STUB_TRACE=1) records which silent stubs actually fire.

Two stub shapes are handled:
  1. C-symbol stubs:  RET name(args) { return X; }      -> name from the def
  2. asm-label stubs: extern "C" RET _stub_fn_N() {...}  -> demangled name from
     the `// RealName(args)` comment immediately above.

Idempotent: lines already containing HX_STUB_TRACE are left untouched.
Run from the repo root; writes the file in place.
"""
import re
import sys
from pathlib import Path

STUB_FILE = Path("native/src/engine_stubs_generated.cpp")

# RET name(args) { return X; }   or   RET name(args) {}
# RET may include leading attributes like __attribute__((weak)) and 'extern "C"'.
FUNC_RE = re.compile(
    r'^(?P<indent>\s*)'
    r'(?P<pre>(?:extern\s+"C"\s+)?(?:__attribute__\(\([^)]*\)\)\s+)*)'
    r'(?P<ret>(?:unsigned\s+|signed\s+|const\s+)*[A-Za-z_][\w:]*\s*\*?\s*)'
    r'(?P<name>[A-Za-z_]\w*)\s*'
    r'\((?P<args>[^;{]*)\)\s*'
    r'\{\s*(?P<body>return\s+[^;]*;|)\s*\}\s*$'
)

# A demangled-name comment: `// SomeName(args...)` or `// SomeName` (no parens).
COMMENT_NAME_RE = re.compile(r'^\s*//\s*([A-Za-z_][\w:]*)\b')


def trace_name(m, prev_comment):
    name = m.group("name")
    if name.startswith("_stub_") and prev_comment:
        cm = COMMENT_NAME_RE.match(prev_comment)
        if cm:
            return cm.group(1)
    return name


def instrument(text):
    lines = text.splitlines(keepends=True)
    out = []
    prev_comment = None
    count = 0
    for line in lines:
        stripped = line.rstrip("\n")
        m = FUNC_RE.match(stripped)
        if m and "HX_STUB_TRACE" not in stripped:
            name = trace_name(m, prev_comment)
            indent = m.group("indent")
            pre = m.group("pre")
            ret = m.group("ret")
            args = m.group("args")
            body = m.group("body")
            nl = "\n" if line.endswith("\n") else ""
            # Re-emit as a multi-line body so the trace runs first.
            new = (
                f'{indent}{pre}{ret}{name_sig(m)}({args}) {{ '
                f'HX_STUB_TRACE("{name}");'
            )
            if body:
                new += f" {body} }}{nl}"
            else:
                new += f" }}{nl}"
            out.append(new)
            count += 1
        else:
            out.append(line)
        # Track the immediately-preceding comment line for asm-label naming.
        # The `extern "C" ... _stub_*() __asm__(...)` forward declaration sits
        # between the name comment and the definition, so it must NOT reset the
        # remembered comment.
        ls = stripped.lstrip()
        if ls.startswith("//"):
            prev_comment = stripped
        elif stripped.strip() == "":
            pass  # blank lines don't reset the comment
        elif "_stub_" in stripped and "__asm__" in stripped:
            pass  # forward decl for an asm-label stub; keep the name comment
        else:
            prev_comment = None
    return "".join(out), count


def name_sig(m):
    # The matched function name as written in source (keeps _stub_fn_N etc.).
    return m.group("name")


def main():
    if not STUB_FILE.exists():
        print(f"error: {STUB_FILE} not found (run from repo root)", file=sys.stderr)
        sys.exit(1)
    text = STUB_FILE.read_text()
    if '#include "StubTrace.h"' not in text:
        # Insert the include right after the first block of system includes.
        text = text.replace(
            "#include <cstring>\n",
            '#include <cstring>\n\n#include "StubTrace.h"\n',
            1,
        )
    new_text, count = instrument(text)
    STUB_FILE.write_text(new_text)
    print(f"instrumented {count} stub functions in {STUB_FILE}")


if __name__ == "__main__":
    main()
