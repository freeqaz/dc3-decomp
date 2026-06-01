#!/usr/bin/env python3
"""
DEPRECATED SHIM — pcode_inspect.py is misnamed and does NOT export P-code.

The original "pcode_inspect.py" never touched real P-code: it only analyzed Ghidra's
decompiled C and hand-decoded raw PPC bytes for switch/cast patterns. It has been
renamed to switch_cast_inspect.py. For genuine Ghidra P-code, use pcode_export.py.

This shim is kept (not deleted) because skills/docs may still reference the old name.
It prints a deprecation note to stderr, then delegates to switch_cast_inspect so existing
invocations keep working unchanged.

  - real P-code (HIGH/RAW):      tools/ghidra/pcode_export.py
  - switch/cast analysis:        tools/ghidra/switch_cast_inspect.py
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

# Re-export the switch/cast inspector's public API so `from pcode_inspect import *`
# (and any direct symbol imports) keep resolving.
from switch_cast_inspect import *  # noqa: F401,F403
from switch_cast_inspect import main as _switch_cast_main


def main():
    print(
        "[deprecation] pcode_inspect.py is the switch/cast inspector and does NOT "
        "export P-code.\n"
        "              Use pcode_export.py for real P-code (HIGH/RAW), or "
        "switch_cast_inspect.py for switch/cast analysis.",
        file=sys.stderr,
    )
    _switch_cast_main()


if __name__ == "__main__":
    main()
