#!/usr/bin/env python3
"""access_specifier_scan.py — find members we declared with the WRONG ACCESS.

WHY THIS EXISTS
===============
MSVC encodes member access in the mangled name.  A method we declare `public`
that the image declares `protected` is a DIFFERENT SYMBOL, not a differently-
annotated one:

    target (ham_xbox_r.map)        ours
    ??_GJsonObject@@MAAPAXI@Z      ??_GJsonObject@@UAAPAXI@Z      M=protected vs U=public
    ??_ERndVelocityBuffer@@EAA...  ??_ERndVelocityBuffer@@UAA...  E=private   vs U=public

That makes the class structurally invisible to every ruler this project owns:

  * objdiff pairs symbols BY NAME, so the two never pair at all -- the row is
    `unresolved-target`, not a mismatch, and contributes no instruction diff.
  * `run_symbol_sweep(kind="vtable_slots")` filters a slot as a benign ICF fold
    when both sides resolve to the SAME ADDRESS.  These do: the bodies are
    identical, only the NAME differs.  So the sweep drops them by design.
    `docs/analysis/dispatch-data-rescan-20260818.md` called the JsonObject /
    RndVelocityBuffer pair "Cosmetic ICF naming" for a day on exactly that
    reasoning, then corrected itself -- it is a real decomp bug.

The access specifier is not cosmetic.  It is a fact about the original source
that we got wrong, it changes what compiles against the class, and for a
virtual it can change the vtable (a method whose access we mis-declare can
still bind, but a signature we get wrong alongside it will not).

WHAT IT DOES
------------
Reads every defined symbol from our built COFF objects and every symbol named
in the linker map, reduces each to an ACCESS-BLIND KEY (the mangled name with
the access/storage character blanked), and reports keys where our set of
access characters and the target's are DISJOINT.

Disjoint, not merely different, is deliberate: a symbol legitimately appears
under more than one spelling across objects, and a key where the two sides
share ANY spelling is not evidence of anything.

MSVC member access/type codes (the character right after the final `@@`):
    A-H private   I-P protected   Q-X public

WHAT IT CANNOT SEE  (read this before calling a class exhausted)
---------------------------------------------------------------
* **Only 990 of 2,223 target objects currently have a built counterpart.**  A
  wrong access specifier in a TU that does not build yet is invisible here.
  The coverage block states the object count on every run; it is NOT a
  whole-binary census and must not be quoted as one.
* A member the image never emitted as a standalone symbol (inlined away, or an
  unreferenced template instantiation) has no target spelling to disagree with.
  ~22k of our keys are in that bucket -- overwhelmingly STL/inline
  (`?Str@Symbol@@QBAPBDXZ`, `?end@?$vector@...`), counted as
  `absent-from-target` rather than silently skipped.
* `static` vs non-static and near/far live in the same character, so a
  disagreement is reported as an access disagreement even when the real defect
  is storage class.  The rendered row prints both raw characters so the reader
  can tell which it is.

USAGE
-----
    python3 scripts/analysis/access_specifier_scan.py --selftest   # validate first
    python3 scripts/analysis/access_specifier_scan.py
    python3 scripts/analysis/access_specifier_scan.py --json out.json --fail-on 1

`--selftest` asserts the comparator fires on a known-divergent pair AND is
silent on an agreeing one, then -- when the real corpus is present -- asserts
all four documented live instances are still found.  A sweep whose instrument
has not been shown to fire on a known instance is not evidence of anything.
"""
from __future__ import annotations

import argparse
import glob
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from coverage import CoverageReport, add_coverage_args, EXIT_NO_INPUT  # noqa: E402
from coffx import read_coff  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_MAP = os.path.join(REPO, "orig", "373307D9", "ham_xbox_r.map")
DEFAULT_OBJ_ROOT = os.path.join(REPO, "build", "373307D9", "src")
DEFAULT_TARGET_OBJ_ROOT = os.path.join(REPO, "build", "373307D9", "obj")

PRIVATE = set("ABCDEFGH")
PROTECTED = set("IJKLMNOP")
PUBLIC = set("QRSTUVWX")
ACCESS_CHARS = PRIVATE | PROTECTED | PUBLIC

SYMBOL_RE = re.compile(r"\?[A-Za-z0-9_?$@]+")

# The four instances documented as live at the time this scanner was written.
# --selftest requires every one of them to still be found, so that a future
# change which quietly stops detecting the class fails loudly instead of
# printing a smaller, cleaner-looking number.
KNOWN_LIVE = [
    "??_GJsonObject@@",
    "??_EJsonObject@@",
    "??_GRndVelocityBuffer@@",
    "??_ERndVelocityBuffer@@",
]


def access_class(ch: str) -> str:
    if ch in PRIVATE:
        return "private"
    if ch in PROTECTED:
        return "protected"
    if ch in PUBLIC:
        return "public"
    return "?"


def access_blind_key(name: str):
    """(key, access_char) for a mangled member symbol, else None.

    The access character is the one immediately after the FINAL `@@`, which is
    the separator between the qualified name and the function's type encoding.
    Templates embed `@@` inside the name, so rfind is correct and find is not.
    """
    i = name.rfind("@@")
    if i < 0 or i + 3 > len(name):
        return None
    ch = name[i + 2]
    if ch not in ACCESS_CHARS:
        return None
    return name[: i + 2] + "\x00" + name[i + 3 :], ch


def load_target(map_path: str):
    """access-blind key -> set of access chars seen in the linker map."""
    tgt = {}
    with open(map_path, errors="replace") as f:
        for line in f:
            for tok in SYMBOL_RE.findall(line):
                k = access_blind_key(tok)
                if k:
                    tgt.setdefault(k[0], set()).add(k[1])
    return tgt


def load_ours(obj_root: str, cov: CoverageReport):
    """access-blind key -> set of access chars defined by our built objects."""
    ours = {}
    n_obj = 0
    n_unreadable = 0
    for path in sorted(glob.glob(os.path.join(obj_root, "**", "*.obj"), recursive=True)):
        n_obj += 1
        try:
            with open(path, "rb") as f:
                _secs, syms = read_coff(f.read())
        except OSError:
            n_unreadable += 1
            continue
        if not syms:
            n_unreadable += 1
            continue
        for s in syms:
            name = getattr(s, "name", "") or ""
            if not name.startswith("?"):
                continue
            k = access_blind_key(name)
            if k:
                ours.setdefault(k[0], set()).add(k[1])
    return ours, n_obj, n_unreadable


def compare(ours: dict, tgt: dict, cov: CoverageReport):
    """Returns (findings, n_agree, n_partial). Routes every discard through cov."""
    findings = []
    n_agree = 0
    n_partial = 0
    for key in sorted(ours):
        our_acc = ours[key]
        tgt_acc = tgt.get(key)
        if not tgt_acc:
            cov.drop("absent-from-target",
                     note="inlined away or never emitted standalone by the image")
            continue
        cov.examine()
        if our_acc == tgt_acc:
            n_agree += 1
            continue
        if our_acc & tgt_acc:
            # Shares a spelling -- not evidence. Counted, not reported.
            n_partial += 1
            continue
        findings.append({
            "symbol": key.replace("\x00", "?"),
            "ours": sorted(our_acc),
            "target": sorted(tgt_acc),
            "ours_access": access_class(sorted(our_acc)[0]),
            "target_access": access_class(sorted(tgt_acc)[0]),
        })
    return findings, n_agree, n_partial


def selftest() -> int:
    """Fire on a known divergence; stay silent on agreement. Then the real corpus."""
    ok = True

    def check(label, cond):
        nonlocal ok
        print(f"  {'PASS' if cond else 'FAIL'}  {label}")
        if not cond:
            ok = False

    # -- comparator, on synthetic input -------------------------------------
    cov = CoverageReport("selftest", allow_truncation=True)
    cov.universe(4, "synthetic keys")
    ours = {
        "?A@C@@\x00AAXXZ": {"U"},      # public,    target protected -> FINDING
        "?B@C@@\x00AAXXZ": {"Q"},      # agrees
        "?C@C@@\x00AAXXZ": {"U", "M"}, # partial overlap -> NOT a finding
        "?D@C@@\x00AAXXZ": {"U"},      # absent from target
    }
    tgt = {
        "?A@C@@\x00AAXXZ": {"M"},
        "?B@C@@\x00AAXXZ": {"Q"},
        "?C@C@@\x00AAXXZ": {"M"},
    }
    found, n_agree, n_partial = compare(ours, tgt, cov)
    syms = [f["symbol"] for f in found]
    check("fires on a disjoint access pair", syms == ["?A@C@@?AAXXZ"])
    check("silent on an exact agreement", n_agree == 1)
    check("partial overlap counted, not reported", n_partial == 1)
    check("absent-from-target is dropped, not examined", cov.as_dict()["dropped"].get("absent-from-target") == 1)
    check("denominator balances", cov.unaccounted == 0)

    # Negative control: correct our side, the finding MUST disappear.
    cov2 = CoverageReport("selftest-neg", allow_truncation=True)
    cov2.universe(1, "synthetic keys")
    fixed, _, _ = compare({"?A@C@@\x00AAXXZ": {"M"}}, {"?A@C@@\x00AAXXZ": {"M"}}, cov2)
    check("negative control: fixing our access clears the finding", fixed == [])

    # -- key parsing --------------------------------------------------------
    check("rfind picks the LAST @@ (template names embed one)",
          access_blind_key("??1?$vector@PAVJsonObject@@V?$X@@stlpmtx_std@@QAA@XZ")[1] == "Q")
    check("non-access char (data symbol) is ignored",
          access_blind_key("?sFoo@@3MA") is None)

    # -- the real corpus, when present --------------------------------------
    if os.path.exists(DEFAULT_MAP) and os.path.isdir(DEFAULT_OBJ_ROOT):
        cov3 = CoverageReport("selftest-live", allow_truncation=True)
        tgt_live = load_target(DEFAULT_MAP)
        ours_live, n_obj, _ = load_ours(DEFAULT_OBJ_ROOT, cov3)
        cov3.universe(len(ours_live), "our access-bearing keys")
        live, _, _ = compare(ours_live, tgt_live, cov3)
        got = {f["symbol"] for f in live}
        for known in KNOWN_LIVE:
            check(f"still detects documented live instance {known}",
                  any(s.startswith(known) for s in got))
        check("live corpus was non-empty", n_obj > 0 and len(tgt_live) > 0)
    else:
        print("  SKIP  live-corpus checks (map or built objects absent)")
        print("        This is NOT a pass: the comparator was exercised, the")
        print("        extractor was not. Build the tree and re-run.")

    print("\nselftest:", "OK" if ok else "FAILED")
    return 0 if ok else 1


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--map", default=DEFAULT_MAP, help="MSVC linker map (target truth)")
    ap.add_argument("--obj-root", default=DEFAULT_OBJ_ROOT, help="our built COFF objects")
    ap.add_argument("--target-obj-root", default=DEFAULT_TARGET_OBJ_ROOT,
                    help="target objects, counted only to state build coverage")
    ap.add_argument("--json", default=None, help="write findings to this path")
    ap.add_argument("--limit", type=int, default=0,
                    help="shorten the PRINTOUT only; the counts above it are complete "
                         "(prints 'showing N of M')")
    ap.add_argument("--fail-on", type=int, default=0,
                    help="exit 1 when findings >= N (0 disables)")
    ap.add_argument("--selftest", action="store_true")
    add_coverage_args(ap)
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()

    # Rule 4: a missing input is never a clean verdict.
    if not os.path.exists(args.map):
        print(f"INCONCLUSIVE: linker map not found: {args.map}")
        print("The target side of this comparison is unavailable; this run checked nothing.")
        return EXIT_NO_INPUT
    if not os.path.isdir(args.obj_root):
        print(f"INCONCLUSIVE: built objects not found: {args.obj_root}")
        print("Run `ninja` first; an unbuilt tree cannot be compared against the map.")
        return EXIT_NO_INPUT

    cov = CoverageReport("access_specifier_scan", args=args)
    cov.require_examined("no symbol was comparable against the map")

    tgt = load_target(args.map)
    ours, n_obj, n_unreadable = load_ours(args.obj_root, cov)
    cov.universe(len(ours), "access-bearing mangled symbols defined by our objects")

    n_target_obj = len(glob.glob(os.path.join(args.target_obj_root, "**", "*.obj"),
                                 recursive=True)) if os.path.isdir(args.target_obj_root) else 0
    if n_target_obj:
        cov.note(f"our build covers {n_obj} of {n_target_obj} target objects "
                 f"({100.0 * n_obj / n_target_obj:.1f}%) -- a wrong access specifier in a TU "
                 f"that does not build yet is NOT visible to this scan")
    if n_unreadable:
        cov.note(f"{n_unreadable} object(s) unreadable or symbol-less")
    cov.note("target truth is the linker map; access char = first char after the final '@@'")
    cov.extra("our_objects", n_obj)
    cov.extra("target_objects", n_target_obj)
    cov.extra("target_keys", len(tgt))

    findings, n_agree, n_partial = compare(ours, tgt, cov)
    cov.extra("agree_exactly", n_agree)
    cov.extra("partial_overlap", n_partial)
    cov.extra("findings", len(findings))

    print(f"ACCESS-SPECIFIER DIVERGENCES: {len(findings)}")
    print(f"  compared against map : {n_agree + n_partial + len(findings)}")
    print(f"  agree exactly        : {n_agree}")
    print(f"  partial overlap      : {n_partial}  (share a spelling -- not evidence)")
    print()
    shown = findings if args.limit <= 0 else findings[: args.limit]
    if args.limit > 0 and len(shown) < len(findings):
        print(f"  showing {len(shown)} of {len(findings)}")
    for f in shown:
        print(f"  ours={''.join(f['ours'])}({f['ours_access']:9s}) "
              f"target={''.join(f['target'])}({f['target_access']:9s})  {f['symbol']}")

    # Distinct declarations: ??_E and ??_G are two thunks of ONE declaration.
    decls = sorted({re.sub(r"^\?\?_[EG]", "??_*", f["symbol"]) for f in findings})
    print(f"\n  distinct declarations behind those rows: {len(decls)}")

    if args.json:
        with open(args.json, "w") as fh:
            json.dump({"findings": findings, "_coverage": cov.as_dict()}, fh, indent=2)

    rc = cov.emit()
    if rc:
        return rc
    if args.fail_on and len(findings) >= args.fail_on:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
