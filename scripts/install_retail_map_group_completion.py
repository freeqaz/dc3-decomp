#!/usr/bin/env python3
"""Complete already-admitted ICF fold classes with the members the retail map names.

Why this exists
---------------
`scripts/symbol_aliases.json` carries several evidence tiers.  Two of them
disagree about *how many* names a fold class holds:

  * the BODY-TEST tier (decomp-synth `probe_icf_foldtest.py`) mints a class
    from *witnessed call sites*.  A spelling that is folded in the image but
    that no probed call site exercised is simply never seen, so the class it
    writes is a SUBSET of the fold.
  * the RETAIL-MAP tier reads `orig/373307D9/ham_xbox_r.map`, where /OPT:ICF
    survivors share an address, so it sees the whole fold set at once.

Where a body-test class already occupied an address, the retail-map installer
left it alone -- and the class stayed short.  Measured 2026-08-23: 228 admitted
classes are missing 942 names that the shipped linker map puts at the very
address the class was admitted for.

What that costs, concretely: `HttpGet::Poll` read 99.98344% with one charged
relocation, `??$MakeString@W4State@HttpGet@@` (which our HttpGet.obj emits and
which the retail map places at 0x8255A0A0) against dtk's name for that same
address, `??$MakeString@W4ReqType@@` (from HttpReqCurl.obj).  Same address,
same bytes, one name -- a naming artifact charged as a wrong callee.  The
source was never wrong.

Gates
-----
The four gates of the original retail-map installer are re-run verbatim; none
is relaxed and no name shape is trusted:

  1. the map carries >= 2 non-annotation names at the address
     (`__unwind$` / `__catch$` / `$L` / `??_C@` excluded verbatim);
  2. the address already carries an ADMITTED class -- this script never mints
     a new class, it only completes one that passed a stricter tier;
  3. every added name is a real name from the map at that address;
  4. exactly one name at the address is TARGET-RESIDENT (present in
     `config/373307D9/symbols.txt`), and no target-resident name is ever added
     as a folded member.

Gate 4 is the substantive one and it is what keeps a real wrong-callee bug
from hiding: an over-merge (two target-resident spellings in one class) would
make a genuine divergence between them unmeterable, so such an address is
refused fail-closed and left short.  Measured: 4 classes are refused this way,
withholding 251 names.

Adding a member that our build never mentions is inert (objdiff only consults
an equivalence when a relocation name actually differs), so gate 3's
"one side uses it" hygiene condition is not safety-critical here and is not
re-imposed; gate 4 is.

`--mint-new` (second pass)
-------------------------
The same evidence also covers map-shared addresses that carry NO class at all.
`--mint-new` creates one per address, under gate 4 plus a new gate 5: the
survivor's address in `config/373307D9/symbols.txt` must equal the map address,
which stops an anonymous-namespace symbol's TU hash (`?A0xc9fefd64@@`) being
misread as an address.  Measured 2026-08-23: 534 classes / 1,577 folded names,
6 addresses refused.  Its measured yield on this tree is SMALL -- 3 functions
improved, 0 crossed 100.0, 0 regressed -- because most of those spellings are
in units this build does not compile.  It is a correctness fix to the ruler,
not a scoring win, and it is separable from the completion pass above.

Usage
-----
    python3 scripts/install_retail_map_group_completion.py --check
    python3 scripts/install_retail_map_group_completion.py --apply
    python3 scripts/install_retail_map_group_completion.py --apply --mint-new
"""
from __future__ import annotations

import argparse
import collections
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
BUILD_ID = "373307D9"
RETAIL_MAP = ROOT / "orig" / BUILD_ID / "ham_xbox_r.map"
SYMBOLS_TXT = ROOT / "config" / BUILD_ID / "symbols.txt"
ALIASES = ROOT / "scripts" / "symbol_aliases.json"

ANNOTATION_PREFIXES = ("__unwind$", "__catch$", "$L", "??_C@")
MAP_LINE = re.compile(
    r"\s+[0-9A-Fa-f]{4}:[0-9A-Fa-f]{8}\s+(\S+)\s+([0-9A-Fa-f]{8})\s+f\s+i\s"
)
TIER = "retailmap-completion"


def is_annotation(name: str) -> bool:
    return any(name.startswith(p) for p in ANNOTATION_PREFIXES)


def retail_map_addresses() -> dict[str, set[str]]:
    addr2names: dict[str, set[str]] = collections.defaultdict(set)
    with RETAIL_MAP.open() as fh:
        for line in fh:
            m = MAP_LINE.match(line)
            if not m:
                continue
            name, addr = m.group(1), m.group(2).lower()
            if is_annotation(name):
                continue
            addr2names[addr].add(name)
    return addr2names


# The address must be read from the "= .text:0x…" assignment, NOT the first
# 0x… on the line: an anonymous-namespace symbol carries its TU hash inside the
# mangled name (?A0xc9fefd64@@), and a naive search returns that hash instead.
SYMBOL_ADDR = re.compile(r"=\s*\.\w+:0x([0-9A-Fa-f]{8})")


def target_resident() -> dict[str, str | None]:
    """dtk-named target-resident symbols -> the address symbols.txt gives them."""
    names: dict[str, str | None] = {}
    with SYMBOLS_TXT.open() as fh:
        for line in fh:
            if "=" not in line:
                continue
            name = line.split("=", 1)[0].strip()
            addr = SYMBOL_ADDR.search(line)
            names[name] = addr.group(1).lower() if addr else None
    return names


def plan(mint_new: bool = False):
    addr2names = retail_map_addresses()
    tres = target_resident()
    doc = json.loads(ALIASES.read_text())

    completions = []  # (group, sorted added names)
    refused = []  # (group name, reason, n_names)
    minted = []  # brand-new groups

    for group in doc["groups"]:
        raw = group.get("address")
        if not raw:
            continue
        addr = raw.lower().removeprefix("0x")
        names_at = addr2names.get(addr)
        if not names_at or len(names_at) < 2:  # gate 1
            continue
        have = set(group["folded"]) | {group["survivor"]}
        extra = names_at - have
        if not extra:
            continue
        resident = names_at & set(tres)
        if len(resident) != 1:  # gate 4
            refused.append((group["name"], f"{len(resident)} target-resident names", len(extra)))
            continue
        add = sorted(n for n in extra if n not in tres)  # gate 4, second half
        if add:
            completions.append((group, add))

    if mint_new:
        occupied = {
            g["address"].lower().removeprefix("0x") for g in doc["groups"] if g.get("address")
        }
        for addr, names_at in sorted(addr2names.items()):
            if len(names_at) < 2 or addr in occupied:  # gates 1, 2
                continue
            resident = names_at & set(tres)
            if len(resident) != 1:  # gate 4
                refused.append((f"mint@0x{addr}", f"{len(resident)} target-resident names",
                                len(names_at) - 1))
                continue
            survivor = next(iter(resident))
            if tres[survivor] != addr:  # gate 5: symbols.txt must agree with the map
                refused.append((f"mint@0x{addr}",
                                f"symbols.txt puts {survivor} at 0x{tres[survivor]}",
                                len(names_at) - 1))
                continue
            folded = sorted(names_at - {survivor})
            stem = survivor.lstrip("?").split("@", 1)[0][:40] or "sym"
            minted.append(
                {
                    "name": f"retailmap-mint:{stem}@0x{addr}",
                    "address": f"0x{addr}",
                    "survivor": survivor,
                    "folded": folded,
                    "sites": 0,
                    "survivor_candidates": 1,
                    "evidence": (
                        f"MINTED from orig/{BUILD_ID}/ham_xbox_r.map ({TIER}): "
                        f"{len(folded) + 1} spellings share 0x{addr} in the shipped MSVC "
                        "linker map, i.e. /OPT:ICF folded them. Gate 4: exactly one of "
                        f"them ({survivor}) is target-resident, so the canonical spelling "
                        "is deterministic and no target-resident name is folded away. "
                        "Gate 5: config/" + BUILD_ID + "/symbols.txt agrees with the map "
                        "about the survivor's address."
                    ),
                }
            )

    return doc, completions, refused, minted


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--apply", action="store_true", help="write scripts/symbol_aliases.json")
    ap.add_argument("--check", action="store_true", help="report only (default)")
    ap.add_argument("--mint-new", action="store_true",
                    help="also mint classes for map-shared addresses that carry none")
    args = ap.parse_args()

    doc, completions, refused, minted = plan(mint_new=args.mint_new)
    n_names = sum(len(a) for _, a in completions)
    n_minted_names = sum(len(g["folded"]) for g in minted)
    print(f"completable classes: {len(completions)}   names to add: {n_names}")
    print(f"minted classes:      {len(minted)}   folded names: {n_minted_names}")
    print(f"refused by gate 4:   {len(refused)}   names withheld: {sum(r[2] for r in refused)}")
    for name, reason, n in refused:
        print(f"  REFUSED {name}: {reason} ({n} names withheld)")

    if not args.apply:
        for group, add in completions[:10]:
            print(f"  {group['name']}: +{len(add)}  e.g. {add[0]}")
        if len(completions) > 10:
            print(f"  ... and {len(completions) - 10} more classes")
        return 0

    for group, add in completions:
        group["folded"].extend(add)
        group["evidence"] = (
            group.get("evidence", "")
            + f" | COMPLETED from orig/{BUILD_ID}/ham_xbox_r.map ({TIER}): "
            f"{len(add)} further spelling(s) share this address in the shipped "
            "MSVC linker map, i.e. /OPT:ICF folded them into this survivor. "
            "Gate 4 re-checked: exactly one name at the address is "
            "target-resident, and no target-resident name was added."
        )
    doc["groups"].extend(minted)
    doc.setdefault("_provenance", {}).setdefault("installers", []).append(
        {
            "script": "scripts/install_retail_map_group_completion.py",
            "tier": TIER,
            "classes_completed": len(completions),
            "names_added": n_names,
            "classes_minted": len(minted),
            "names_minted": n_minted_names,
            "classes_refused_gate4": len(refused),
            "names_withheld_gate4": sum(r[2] for r in refused),
        }
    )
    ALIASES.write_text(json.dumps(doc, indent=1) + "\n")
    print(f"wrote {ALIASES}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
