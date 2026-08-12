#!/usr/bin/env python3
"""Score a candidate OBJ_SET_TYPE body by scope ordinal AND `none`-ruler fuzzy%.

MSVC parks a function-local static as `?<var>@?<ord>??<enclosing fn>`, and
<ord> counts the lexical scopes opened before it.  When our object and retail's
disagree on <ord> while the CODE is byte-identical, the ordinal is the only
witness we have to a source-structure difference -- so the way to read it is to
perturb the source, recompile, and watch the number.

One candidate per iteration, a one-TU ninja build (~1s behind the PCH), the
ordinal read straight out of the COFF, and the `none` ruler as the control that
says no instruction moved.

    probe_scope_ordinal.py <repo>                  # score every candidate
    probe_scope_ordinal.py <repo> --apply <name>   # splice one in and leave it

`LANDED` is what is in the tree.  Measured 2026-08-12:

    LANDED (null case first)      ordinal=?5  none-fuzzy=100.0%   <- retail
    pre_fix_not_null_first        ordinal=?4  none-fuzzy=100.0%
    landed_no_def                 ordinal=?5  none-fuzzy=100.0%
    landed_rb3_inner_spelling     ordinal=?5  none-fuzzy=100.0%

Nothing here distinguishes the last two from LANDED -- they are recorded so the
next reader knows they were tried and are unwitnessed, not untried.
"""
import re
import subprocess
import sys
from pathlib import Path

from coffsyms import symbols

HEADER = "src/system/obj/Object.h"
OBJ = "build/373307D9/src/system/ui/UIList.obj"
UNIT = "default/system/ui/UIList"
SYM = "?SetType@UIList@@UAAXVSymbol@@@Z"
ORD = re.compile(r"^\?types@\?([0-9A-P@]+?)\?\?SetType@UIList@")

# rb3 (`src/system/obj/ObjMacros.h`, the matched `#else` arm) reconstructs the
# same Milo macro independently for mwcc and writes the null case first, with
# everything else in the `else`.  Calibration says an unbraced `if` costs 2 and
# a braced `else` after it costs 2 more -- 1 + 2 + 2 = 5, which is retail.
LANDED = """\
#define OBJ_SET_TYPE(classname)                                                           \\
    virtual void SetType(Symbol classname) {                                              \\
        DataArray *def;                                                                   \\
        if (classname.Null())                                                             \\
            SetTypeDef(nullptr);                                                          \\
        else {                                                                            \\
            static DataArray *types =                                                     \\
                SystemConfig("objects", StaticClassName(), "types");                      \\
            DataArray *found = types->FindArray(classname, false);                        \\
            if (found) {                                                                  \\
                SetTypeDef(found);                                                        \\
            } else {                                                                      \\
                MILO_NOTIFY(                                                              \\
                    "%s:%s couldn't find type %s", ClassName(), PathName(this), classname \\
                );                                                                        \\
                SetTypeDef(nullptr);                                                      \\
            }                                                                             \\
        }                                                                                 \\
    }
"""

VARIANTS = {
    # What the tree said before 2026-08-12: the non-null case first, braced,
    # with the static inside it.  1 + 3 = 4, one short of retail.
    "pre_fix_not_null_first": """\
#define OBJ_SET_TYPE(classname)                                                           \\
    virtual void SetType(Symbol classname) {                                              \\
        DataArray *def;                                                                   \\
        if (!classname.Null()) {                                                          \\
            static DataArray *types =                                                     \\
                SystemConfig("objects", StaticClassName(), "types");                      \\
            DataArray *found = types->FindArray(classname, false);                        \\
            if (found) {                                                                  \\
                SetTypeDef(found);                                                        \\
            } else {                                                                      \\
                MILO_NOTIFY(                                                              \\
                    "%s:%s couldn't find type %s", ClassName(), PathName(this), classname \\
                );                                                                        \\
                SetTypeDef(nullptr);                                                      \\
            }                                                                             \\
        } else                                                                            \\
            SetTypeDef(nullptr);                                                          \\
    }
""",
    # `DataArray *def;` is dead here and rb3's macro has no such declaration.
    # Ordinal-neutral and byte-neutral, so there is no witness either way.
    "landed_no_def": LANDED.replace(
        "        DataArray *def;                                                                   \\\n", ""
    ),
    # rb3 spells the inner test `found != 0` with an unbraced then-arm.  Also
    # ordinal-neutral: the scopes it changes are all AFTER the static.
    "landed_rb3_inner_spelling": LANDED.replace(
        "            if (found) {                                                                  \\\n"
        "                SetTypeDef(found);                                                        \\\n"
        "            } else {                                                                      \\\n",
        "            if (found != 0)                                                               \\\n"
        "                SetTypeDef(found);                                                        \\\n"
        "            else {                                                                        \\\n",
    ),
}


def splice(repo, body):
    p = Path(repo) / HEADER
    txt = p.read_text()
    start = txt.index("#define OBJ_SET_TYPE(classname)")
    end = txt.index("// END SET TYPE MACRO", start)
    p.write_text(txt[:start] + body + "\n" + txt[end:])


def measure(repo):
    r = subprocess.run(["ninja", OBJ], cwd=repo, capture_output=True, text=True)
    if r.returncode:
        return None, (r.stdout + r.stderr)[-600:]
    ords = [m.group(1) for s in symbols(Path(repo) / OBJ) if (m := ORD.match(s))]
    pct = subprocess.run(
        ["objdiff-cli", "diff", "-p", repo, "-u", UNIT, "-c",
         "functionRelocDiffs=none", "-f", "json", "-o", "-", SYM],
        cwd=repo, capture_output=True, text=True,
    ).stdout
    m = re.search(r'"fuzzy_match_percent":\s*([0-9.]+)', pct)
    return (ords[0] if ords else None, m.group(1) if m else "?"), None


def main():
    repo = sys.argv[1]
    if len(sys.argv) > 3 and sys.argv[2] == "--apply":
        splice(repo, LANDED if sys.argv[3] == "LANDED" else VARIANTS[sys.argv[3]])
        got, err = measure(repo)
        print(err or f"applied {sys.argv[3]}: ordinal=?{got[0]} none-fuzzy={got[1]}%")
        return
    orig = (Path(repo) / HEADER).read_text()
    try:
        for name, body in [("LANDED", LANDED)] + list(VARIANTS.items()):
            splice(repo, body)
            got, err = measure(repo)
            if err:
                print(f"{name:26s} BUILD FAILED  {err.splitlines()[-1][:100]}")
            else:
                print(f"{name:26s} ordinal=?{got[0]}  none-fuzzy={got[1]}%")
    finally:
        (Path(repo) / HEADER).write_text(orig)
        subprocess.run(["ninja", OBJ], cwd=repo, capture_output=True)
    print("\nretail wants ordinal ?5 at 100% fuzzy under `none`.")


if __name__ == "__main__":
    main()
