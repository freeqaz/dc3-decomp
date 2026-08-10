#!/usr/bin/env python3
"""Render the synthetic MSVC map declaring dc3's ADMITTED ICF symbol folds.

Why this exists
---------------
Retail DC3 was linked with MSVC ``/OPT:ICF``, which folds byte-identical
COMDATs: several source-level spellings survive at ONE address in the shipped
image. The target objects can therefore only ever name the survivor, while our
compiled objects emit the spelling their own translation unit references. Any
by-name relocation comparison -- objdiff's ``reloc_eq`` included -- flags a
``[sym]`` mismatch on a call site that is the same bytes to the same code.

objdiff already has the seam for this: ``objdiff.json``'s ``map_file`` ->
``objdiff-core/src/obj/map_file.rs`` ``parse_msvc_map``, which groups every
symbol name sharing an 8-hex address and treats names in a group as
reloc-name-equal. We DO have a real MSVC ``.map`` -- ``orig/373307D9/ham_xbox_r.map``
is the shipped linker map for this exact build (same-build check: of the 67,510
symbol names it shares with ``scripts/target_symbol_map.json``, 67,483 sit at
identical addresses; all 27 disagreements are non-unique ``__unwind$N`` labels),
and roughly twenty tools in this repo already read it. What we hand objdiff is
nevertheless still a *synthetic* map, because the real one is ~118,000 publics
over ~107,649 addresses, i.e. the linker's whole symbol table rather than an
admitted equivalence set. The real map is therefore one of the SOURCES the alias
set is derived from (see the retail-linker-map class below), not the file
objdiff consumes. The rendered file carries only the admitted classes: one line
per group member, all carrying the survivor's address, so the parser buckets
them together. No objdiff source change is required.

The evidence, and what is NOT admitted
--------------------------------------
Groups in ``scripts/symbol_aliases.json`` are no longer all body-test witnesses.
Each group carries its OWN ``evidence`` string, and three classes coexist
(1,950 groups / 8,277 names as of 2026-08-10):

* **Body-test witness** (373 groups) -- the original class, from decomp-synth's
  ``tools/revcomp/probes/probe_icf_foldtest.py``: retail bytes at the survivor
  address are relocation-invariantly identical (immediates masked,
  ``60 00 00 00`` padding trimmed, one-level thunk chase) to our compiled body
  for each folded spelling.
* **COFF weak external** (962 groups; ``evidence`` reads ``COFF weak external,
  IMAGE_WEAK_EXTERN_SEARCH_ALIAS``) -- MSVC emits the vector deleting destructor
  ``??_E<Class>`` as an UNDEFINED WEAK EXTERNAL whose auxiliary record names
  ``??_G<Class>`` as its default resolution. The alias is DECLARED by the
  compiler in our own object file; it is not inferred from a fold.
* **Retail linker map** (615 groups; ``evidence`` starts
  ``orig/373307D9/ham_xbox_r.map:``) -- ``/OPT:ICF`` folds byte-identical
  COMDATs, so symbols sharing an address in that map ARE the fold set, stated by
  the linker that made the image. Admitted narrowly: the address must carry two
  or more names our own objects reference, ``__unwind$``/``__catch$``/``$L``/
  ``??_C@`` names are excluded as annotations rather than aliases, and the
  survivor must be the single member the target objects define.

**Name shapes are arguments, not witnesses** -- an earlier triage declared every
``merged_``-prefixed target benign from its name and the body test then found
9 of 33 such sites genuinely different functions. The two newer classes do not
weaken that rule: each still rests on something the toolchain STATED about these
particular symbols (an auxiliary record, a linker-assigned address), never on
what a name looks like.

That file holds the VALIDATOR-ACCEPTED classes only. This renderer does not
validate and objdiff never does, so "the map contains no class the validator
refused" has to be true of the input file itself. Regenerate the input with
decomp-synth's ``tools/il_witness/build_icf_alias_inputs.py`` (build, then
``--validate ... --emit-map``); do not hand-add a group here.

Note that the CONSUMER-side gate is independent and stricter: decomp-synth's
``tools/il_witness/symbol_equivalences.py`` re-derives the verdict from the tree
it is grading on every load, and will drop a class this map still contains once
the tree stops supporting it. That direction is safe (the grader is then
stricter than the judge for that one class); the reverse would not be.

Usage
-----
    python3 scripts/gen_icf_alias_map.py             # write build/373307D9/icf_aliases.map
    python3 scripts/gen_icf_alias_map.py --check     # exit 1 if the map is stale
    python3 scripts/gen_icf_alias_map.py --out PATH  # custom output path

``tools/project.py`` runs this at configure time and wires ``map_file`` only if
the output exists, so a fresh tree without the generated map still builds -- no
map means no equivalences, which is the pre-ICF behaviour.
"""

import argparse
import json
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
BUILD_ID = "373307D9"
DEFAULT_ALIASES = PROJECT_ROOT / "scripts" / "symbol_aliases.json"
DEFAULT_OUT = PROJECT_ROOT / "build" / BUILD_ID / "icf_aliases.map"

# parse_msvc_map's regex (objdiff-core/src/obj/map_file.rs) is
#   ^\s*\d{4}:[0-9a-fA-F]+\s+(\S+)\s+([0-9a-fA-F]{8})\s+
# i.e.  SSSS:OOOOOOOO   <symbol>   AAAAAAAA   <rest>
# Group 1 is the symbol, group 2 the address used to bucket the fold.
MAP_LINE = " 0001:00000000       {sym:<60} {addr}  f i icf_aliases.synthetic\n"

HEADER = (
    "; SYNTHETIC ICF-alias map -- generated by scripts/gen_icf_alias_map.py\n"
    "; DO NOT EDIT BY HAND. Source of truth: scripts/symbol_aliases.json\n"
    "; Consumed by objdiff via objdiff.json 'map_file' -> parse_msvc_map ->\n"
    "; symbol_equivalences (objdiff-core reloc_eq). Each group below carries its\n"
    "; own evidence in the source JSON, in one of three classes: a BODY-TEST\n"
    "; WITNESS (retail bytes at the survivor address are relocation-invariantly\n"
    "; identical to our compiled body for each folded spelling), a COFF WEAK\n"
    "; EXTERNAL whose auxiliary record declares the alias\n"
    "; (IMAGE_WEAK_EXTERN_SEARCH_ALIAS), or ADDRESS-SHARING in the retail linker\n"
    "; map orig/373307D9/ham_xbox_r.map, where /OPT:ICF survivors share an\n"
    "; address. No group is admitted from a name shape.\n"
    ";\n"
    "; Address                        Publics by Value\n"
)


def load_groups(aliases_path: Path) -> list:
    return json.loads(aliases_path.read_text()).get("groups", [])


def normalize_addr(addr: str) -> str:
    """The 8-uppercase-hex form parse_msvc_map expects."""
    return f"{int(str(addr).lower().removeprefix('0x'), 16):08X}"


def render_map(groups: list) -> str:
    """A group with no address is SKIPPED, not emitted at address 0.

    Colliding unrelated groups on a fake address would hand objdiff an
    equivalence class nothing witnessed.
    """
    out = [HEADER]
    for g in groups:
        if not g.get("address"):
            continue
        addr = normalize_addr(g["address"])
        out.append(f"; --- ICF group {g.get('name', addr)} @ {addr} ---\n")
        for sym in [g["survivor"], *g.get("folded", [])]:
            out.append(MAP_LINE.format(sym=sym, addr=addr))
    return "".join(out)


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--aliases", default=str(DEFAULT_ALIASES),
                    help="alias group JSON (default: %(default)s)")
    ap.add_argument("--out", default=str(DEFAULT_OUT),
                    help="output .map path (default: %(default)s)")
    ap.add_argument("--check", action="store_true",
                    help="verify the output is up to date; exit 1 if stale")
    args = ap.parse_args()

    aliases_path = Path(args.aliases)
    if not aliases_path.is_file():
        print(f"ERROR: no alias file at {aliases_path}", file=sys.stderr)
        return 1
    groups = load_groups(aliases_path)
    content = render_map(groups)

    out_path = Path(args.out)
    if args.check:
        if not out_path.is_file():
            print(f"STALE: {out_path} does not exist", file=sys.stderr)
            return 1
        if out_path.read_text() != content:
            print(f"STALE: {out_path} differs from generated content", file=sys.stderr)
            return 1
        print(f"OK: {out_path} up to date ({len(groups)} ICF groups)")
        return 0

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(content)
    # Count what was EMITTED, not what was offered. render_map skips any group
    # with no address, so summarising the input reported 1334 groups / 4060
    # symbol lines for a file that carried 372 -- and the only symptom was every
    # addressless class failing gate (f) later as "absent from map_file".
    emitted = [g for g in groups if g.get("address")]
    n_syms = sum(1 + len(g.get("folded", [])) for g in emitted)
    print(f"wrote {out_path}: {len(emitted)} ICF groups, {n_syms} symbol lines")
    if len(emitted) != len(groups):
        print(f"  SKIPPED {len(groups) - len(emitted)} group(s) with no address "
              f"-- these cannot pass gate (f) and will be rejected at load")
    return 0


if __name__ == "__main__":
    sys.exit(main())
