#!/usr/bin/env python3
"""Install the retail-map DATA-COMDAT fold tier into scripts/symbol_aliases.json.

Why a dedicated installer
-------------------------
`scripts/symbol_aliases.json` is written by SEVERAL producers and no single one
of them can regenerate it.  decomp-synth's `build_icf_alias_inputs.py` mints the
body-test class only (373 groups); the COFF weak-external class (962) and the
retail-linker-map class (615) were each installed in place by their own script.
Re-running any one generator would drop the other tiers, which is why the file's
own `_comment` says not to.  So this adds a FOURTH tier in place, touching no
existing group.

The tier: `retailmap-data:` -- ICF fold classes over DATA COMDATs, produced by
`scripts/retail_map_fold_candidates.py`, which re-runs the retail-map
installer's four map-residency gates with the symbol read widened from function
COMDATs to the whole COFF external-definition table, and then applies a fifth
gate the four could not: byte identity of the COMDAT against the target
survivor, which is the condition `/OPT:ICF` actually tests.  Almost all of it is
`??_8` vbtables.

    python3 scripts/install_data_fold_aliases.py                 # install
    python3 scripts/install_data_fold_aliases.py --identity strict
    python3 scripts/install_data_fold_aliases.py --dry-run
    python3 scripts/install_data_fold_aliases.py --check         # 1 if stale
    python3 scripts/install_data_fold_aliases.py --uninstall

Idempotent by construction: every run first removes every group whose name
starts with `retailmap-data:` and then re-adds the current candidate set, so
running it twice, or after the tree is rebuilt, converges rather than
accumulating.

What the decomp-synth validator says, and why it is installed anyway
--------------------------------------------------------------------
decomp-synth's `symbol_equivalences.validate_groups` rejects 102 of these 106
groups, all on gate (a) -- "survivor is a value in
`scripts/target_symbol_map.json`" -- and on nothing else; the other four pass
outright.  Gate (a) is not disagreeing with the evidence, it is unable to see
it: `target_symbol_map.json` holds 69,132 names and **0** of them is a `??_8`,
because it was built from the same function-only symbol read that made the
whole class invisible to the installer.  The check it stands for is already
made, twice and more directly, by gate 4 (the target OBJECTS define exactly one
member, read from the objects rather than from a name list) and gate 5 (that
member's COMDAT bytes are ours).

Consequence, stated rather than papered over: `load_validated` will drop these
102 classes on every load, so decomp-synth's grader stays STRICTER than the
rendered map -- the safe direction, and the one
`scripts/gen_icf_alias_map.py`'s header already describes for a class the tree
stops supporting.  objdiff itself reads `build/373307D9/icf_aliases.map` and
honours them.  Widening `target_symbol_map.json` would make gate (a) runnable
instead of waived, and is the real repair; it is a 69,132-entry file that
decomp-synth grades against and it is not this lane's to rewrite.

UPDATE 2026-08-12, and read this before requoting the paragraph above.

`load_validated` still drops all 102 -- the admission predicate has not moved
one name -- but it no longer calls it a REJECTION.  decomp-synth
`laneU-mapgate` splits gate (a)'s outcome: a survivor the target objects type
as DATA, checked against a map measured to hold 0 non-function names, is
reported UNRESOLVED ("cannot verify, not refuted") in its own bucket, distinct
from the 2 groups gate (b') genuinely refuses.  So "102 rejected" is retired
wording; the number to quote is 102 unresolved.  The `decomp_synth_validator`
string this script writes into `symbol_aliases.json` predates that split and
still says "rejects"; it is deliberately NOT regenerated here, because
regenerating the alias file would re-render `build/373307D9/icf_aliases.map`
and move a measurement to fix a comment.

The widening itself is now derived and PRICED but still not installed:
`scripts/derive_target_symbol_map.py` reproduces this map from
`config/373307D9/symbols.txt` (`--verify`), emits any of three widenings
(`--tier`), and its docstring carries the consumer audit that makes this an
owner call -- five consumers read absence-from-map as a MEANING, and one of
them pins the grader by hashing two .py files and not this one.
"""
import argparse
import collections
import json
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
ALIAS = REPO / "scripts" / "symbol_aliases.json"
GENERATOR = REPO / "scripts" / "retail_map_fold_candidates.py"
CANDIDATE_DIR = REPO / "build" / "373307D9" / "data-fold-candidates"
TIER = "retailmap-data:"
PROV_KEY = "retail_map_data_comdat_classes"

# `_comment` is copied forward by every installer, so the fourth tier has to be
# described there too or the file stops documenting itself.
COMMENT_ADDITION = [
    "  - retail-map DATA COMDAT fold (scripts/install_data_fold_aliases.py,",
    "    group names 'retailmap-data:'): the same linker-map address-sharing",
    "    evidence, over data COMDATs the function-only symbol read could not",
    "    see, AND a byte-identity gate -- our COMDAT contents equal the target",
    "    survivor's, relocations masked and reloc targets equal by name.",
    "    decomp-synth validate_groups gate (a) cannot run on these: 0 of",
    "    target_symbol_map.json's 69,132 names is a data symbol, so",
    "    load_validated drops them and grades stricter than this file.",
]

# The file asserted a property this tier is the first exception to, and leaving
# that sentence standing would make the file lie about itself. Swapped while
# the tier is installed and swapped back by --uninstall, so `_comment` states
# what is true of the file as it actually is.
COMMENT_SWAP = [
    (["This file holds the VALIDATOR-ACCEPTED classes only, so the synthetic",
      "map rendered from it (scripts/gen_icf_alias_map.py ->",
      "build/373307D9/icf_aliases.map) never hands objdiff a class the",
      "validator refused."],
     ["Every tier here except retailmap-data is VALIDATOR-ACCEPTED, so the",
      "synthetic map rendered from it (scripts/gen_icf_alias_map.py ->",
      "build/373307D9/icf_aliases.map) hands objdiff no class the validator",
      "refused ON EVIDENCE. retailmap-data is the one exception and it is a",
      "blind spot rather than a disagreement: validate_groups can only fault",
      "it on gate (a), which no data symbol can satisfy. load_validated drops",
      "it, so decomp-synth grades STRICTER than this file, never looser."]),
    (["Do not hand-edit. NOTE: build_icf_alias_inputs.py mints the body-test",
      "class ONLY -- regenerating from it alone would drop the other two."],
     ["Do not hand-edit. NOTE: build_icf_alias_inputs.py mints the body-test",
      "class ONLY -- regenerating from it alone would drop the other three."]),
]


def swap_comment(lines, installed):
    """Apply (or undo) the sentences the fourth tier makes true or false."""
    for was, now in COMMENT_SWAP:
        src, dst = (was, now) if installed else (now, was)
        for i in range(len(lines) - len(src) + 1):
            if lines[i:i + len(src)] == src:
                lines = lines[:i] + list(dst) + lines[i + len(src):]
                break
    return lines


def candidates(identity, out_dir, restrict_to=None):
    # Without --ignore-installed-prefix the generator's collision gate sees the
    # tier THIS script installed on the previous run and refuses every group in
    # it, so a second run reports the class as having evaporated.
    cmd = [sys.executable, str(GENERATOR), "--out", str(out_dir),
           "--identity", identity, "--ignore-installed-prefix", TIER]
    if restrict_to:
        cmd += ["--restrict-to", restrict_to]
    r = subprocess.run(cmd, cwd=REPO, capture_output=True, text=True)
    if r.returncode:
        sys.exit(f"generator failed:\n{r.stdout}\n{r.stderr}")
    print(r.stdout.rstrip())
    return (json.loads((out_dir / "candidate_groups.json").read_text()),
            json.loads((out_dir / "refused_by_byte_identity.json").read_text()))


def rebuild(doc, groups, identity, refused):
    """The alias document with the tier replaced by `groups`, nothing else."""
    kept = [g for g in doc["groups"] if not g.get("name", "").startswith(TIER)]
    spoken = {n for g in kept
              for n in (g["survivor"], *g.get("folded", []))}
    fresh, collided = [], []
    for g in sorted(groups, key=lambda g: g["address"]):
        clash = [n for n in (g["survivor"], *g["folded"]) if n in spoken]
        if clash:
            # Gate (e) drops a name in two classes from BOTH, so a collision
            # would cost an installed group as well as this one.
            collided.append({"name": g["name"], "members": clash})
            continue
        fresh.append(g)
        spoken |= {g["survivor"], *g["folded"]}

    comment = swap_comment(
        [c for c in doc["_comment"] if c not in COMMENT_ADDITION], False)
    if fresh:
        anchor = next((i for i, c in enumerate(comment)
                       if c.startswith("This file holds")), len(comment))
        comment = swap_comment(
            comment[:anchor] + COMMENT_ADDITION + comment[anchor:], True)

    prov = {k: v for k, v in doc["_provenance"].items() if k != PROV_KEY}
    if fresh:
        why = collections.Counter(r["reasons"][0]["why"] for r in refused)
        prov[PROV_KEY] = {
            "installed_by": "scripts/install_data_fold_aliases.py",
            "generator": "scripts/retail_map_fold_candidates.py",
            "identity_mode": identity,
            "n_groups": len(fresh),
            "n_names": sum(1 + len(g["folded"]) for g in fresh),
            "n_refused_by_byte_identity": len(refused),
            "refusal_reasons": dict(why),
            "n_dropped_on_collision_with_an_installed_group": len(collided),
            "dropped_on_collision": collided,
            "what": (
                "/OPT:ICF folds byte-identical COMDATs. The installed "
                "retail_map_classes tier evaluates its member and survivor "
                "gates with function COMDATs only, so every DATA COMDAT the "
                "linker folded was invisible to it -- not refused on "
                "evidence, not seen. These groups re-run those four gates "
                "over the whole COFF external-definition table and add a "
                "fifth: for every member our objects DEFINE, the COMDAT "
                "contents must equal the target survivor's with "
                "relocation-patched fields masked and relocation targets "
                "equal by name, which is the condition ICF actually tests. "
                "A group with a mismatching member, or with no member our "
                "objects define, is refused fail-closed."),
            "padding_normalisation": (
                "identity_mode 'align' additionally admits a target COMDAT "
                "that is our bytes followed by fewer than 8 zero bytes "
                "landing the end on an 8-byte boundary. Of the 254 ??_8 "
                "COMDATs defined on both sides, 176 are byte-identical, 78 "
                "are exactly ours + 4 trailing zeros and 0 differ in "
                "content; those 4 bytes are inter-symbol alignment padding "
                "dtk's splitter attributed to the vbtable, not a retail "
                "emission difference. Discriminator (retail_map_fold_"
                "candidates.py --padding-evidence, no exceptions in 254): "
                "47 flush with a 4-byte-aligned successor, 129 flush already "
                "on the boundary, 78 padded with an 8-byte-aligned successor "
                "and an off-boundary end. The 47 are the control an emitted "
                "terminator entry fails. 'strict' refuses the padding and "
                "admits 176 of the 254 instead."),
            "decomp_synth_validator": (
                "validate_groups rejects 102 of these on gate (a) alone -- "
                "survivor absent from scripts/target_symbol_map.json, which "
                "holds 0 data symbols in 69,132 names and has the same "
                "function-only blindness. Gates 4 and 5 make that check "
                "directly against the objects. load_validated therefore "
                "drops them and decomp-synth grades stricter than this file; "
                "objdiff reads the rendered map and honours them."),
        }

    return {"_comment": comment, "_provenance": prov,
            "_rejected_at_install": doc.get("_rejected_at_install", []),
            "groups": kept + fresh}, fresh, collided


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--identity", choices=("align", "strict"), default="align")
    ap.add_argument("--restrict-to",
                    help="pass through to the generator: keep only the "
                         "addresses this pair table charges")
    ap.add_argument("--candidates",
                    help="use this candidate_groups.json instead of running "
                         "the generator")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--check", action="store_true",
                    help="exit 1 if installing would change the file")
    ap.add_argument("--uninstall", action="store_true")
    args = ap.parse_args()

    before = ALIAS.read_text()
    doc = json.loads(before)

    if args.uninstall:
        new, fresh, collided = rebuild(doc, [], args.identity, [])
    else:
        if args.candidates:
            groups = json.loads(Path(args.candidates).read_text())
            refused = []
        else:
            CANDIDATE_DIR.mkdir(parents=True, exist_ok=True)
            groups, refused = candidates(args.identity, CANDIDATE_DIR,
                                         args.restrict_to)
        new, fresh, collided = rebuild(doc, groups, args.identity, refused)

    text = json.dumps(new, indent=1) + "\n"
    was = sum(1 for g in doc["groups"] if g.get("name", "").startswith(TIER))
    print(f"{TIER} groups: {was} installed -> {len(fresh)}"
          f"  (file {len(doc['groups'])} -> {len(new['groups'])} groups, "
          f"{sum(1 + len(g.get('folded', [])) for g in new['groups'])} names)")
    if collided:
        print(f"  dropped {len(collided)} group(s) colliding with an "
              f"installed class")

    if args.check:
        if text == before:
            print("OK: up to date")
            return 0
        print("STALE: installing would change scripts/symbol_aliases.json",
              file=sys.stderr)
        return 1
    if args.dry_run:
        print("dry run -- nothing written")
        return 0
    if text == before:
        print("no change")
    else:
        ALIAS.write_text(text)
        print(f"wrote {ALIAS}")

    r = subprocess.run([sys.executable, str(REPO / "scripts/gen_icf_alias_map.py")],
                       cwd=REPO, capture_output=True, text=True)
    print(r.stdout.strip() or r.stderr.strip())
    return r.returncode


if __name__ == "__main__":
    raise SystemExit(main())
