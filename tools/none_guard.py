#!/usr/bin/env python3
"""Keep an automated source fix from buying relocation names with real bytes.

Every fixer in this lane repairs a symbol NAME, and the claim each of them
makes is that no instruction moves.  That claim is checkable: build, and read
`functionRelocDiffs=none` -- the ruler that ignores relocation names entirely.
If it moved, the edit changed codegen and is not the fix it looked like.

This is the harness for that, usable on its own or from a fixer:

    none_guard.py --baseline out.json                 # before editing
    none_guard.py --check out.json --revert           # after editing

`--revert` bisects by consequence rather than by halves: it reverts the source
file behind every unit that LOST a complete function, rebuilds, and repeats
until `none` is whole again.  That converges in a couple of builds because the
report already names the casualties, and it keeps the part of the wave that was
genuinely free instead of throwing the whole thing away.

It is a guard, not a proof of correctness: a `none` that does not move says the
bytes are unchanged, which is exactly the claim -- nothing more.
"""
import argparse
import json
import re
import struct
import subprocess
import sys
from pathlib import Path

RULER = ["-c", "functionRelocDiffs=none"]


def build(repo):
    r = subprocess.run(["ninja"], cwd=repo, capture_output=True, text=True)
    if r.returncode:
        sys.exit("BUILD FAILED:\n" + (r.stdout + r.stderr)[-3000:])


def refuse_if_not_a_full_recompute(d, label=""):
    """Exit unless the report `d` is a cold-cache measurement of this tree.

    `label` names which side of the comparison failed, since this now guards the
    stored baseline as well as the freshly generated report.

    Read the cache count off the REPORT, not off stderr. The old check scraped
    `Report cache: (\\d+) hits` out of stderr and refused only on a truthy match
    -- so ABSENCE of the line read as a PASS. Every way of losing that line (a
    binary that never printed it, a quieter build, a redirect) turned this guard
    into a green no-op, which is precisely the failure it exists to catch. The
    report's own `provenance` block cannot go missing for a formatting reason,
    and cannot go missing for a cache-configuration reason either: `report
    generate` writes it unconditionally. Either the tool wrote it or the tool is
    the wrong tool.

    `.get("cache_hits", 0)`, never `["cache_hits"]`: the block is serialized
    from proto3, which OMITS zero-valued scalars, so the clean run is the one
    with no `cache_hits` key at all. Subscripting raises exactly on success, and
    a bare `.get(k)` returns None, which is not a number.
    """
    prov = d.get("provenance")
    if prov is None:
        sys.exit(f"REFUSING to grade{label}: this report carries no "
                 f"`provenance` block, so nothing in it identifies the ruler it "
                 f"was measured with or says whether any unit came out of "
                 f"cache. There is exactly one cause: a pre-2026-08-13 "
                 f"objdiff-cli. `report generate` emits the block "
                 f"unconditionally -- --no-cache, --deduplicate and an "
                 f"unhashable binary all still write it -- so 'the cache was "
                 f"off' is NOT an innocent explanation. Absent is FAILED, not "
                 f"clean.\n"
                 f"  FIX: use the current fork build -- `objdiff-cli --version` "
                 f"must print a commit + xxh3, e.g. "
                 f"`objdiff-cli 4.2.3 (<commit>, xxh3 <hash>)`. A stored "
                 f"baseline recorded by an older binary is not repairable and "
                 f"is not a tool bug: re-run `none_guard.py --baseline "
                 f"<path>` with the current binary, on the pre-edit tree, to "
                 f"record a comparable one.")
    hits = prov.get("cache_hits", 0)
    if hits:
        sys.exit(f"REFUSING to grade{label}: objdiff-cli served {hits} unit(s) "
                 f"from cache, so this report is not a measurement of the "
                 f"current tree. Remove the stale *.cache sidecars and retry.")
    return prov


def report(repo, out):
    # `report generate -o X.json` writes a SIDECAR `X.cache` and seeds the next
    # run from it. Historically the key was hash_unit + hash_options and did NOT
    # include objdiff.json's `map_file`, so a second run into the same -o path
    # after only the ICF alias map changed came back 100% cache hits carrying
    # the PREVIOUS map's numbers. Measured 2026-08-12: 2224 hits and a report
    # byte-identical to the baseline for a map with 340 more names in it.
    #
    # UPDATED 2026-08-13: the objdiff fork landing keys the cache on the obj
    # bytes + the resolved diff config + the MAP FILE'S CONTENT HASH + the
    # objdiff-cli binary's own xxh3, so that specific silent staleness is fixed
    # upstream. The unlink below is kept anyway -- belt and braces, and it costs
    # one syscall. What is NOT optional is refusing to grade a cached run: a
    # guard that reads a cached report cannot see a regression and reports
    # "intact".
    path = Path(out) if Path(out).is_absolute() else Path(repo, out)
    path.with_suffix(".cache").unlink(missing_ok=True)
    subprocess.run(["objdiff-cli", "report", "generate", "-p", ".", *RULER,
                    "-o", str(out)], cwd=repo, check=True,
                   capture_output=True, text=True)
    d = json.loads(path.read_text())
    refuse_if_not_a_full_recompute(d, label=f" ({out})")
    complete = {(u["name"], f["name"]) for u in d["units"]
                for f in u.get("functions", [])
                if f.get("fuzzy_match_percent") == 100.0}
    return d["measures"], complete


# `none` watches the instruction stream, and a fixer that edits a STRING can
# leave it untouched while changing what the program says: renaming the local
# `static Message special_finished("special_finished")` to `msg` walked into the
# constructor argument and renamed the MESSAGE, and `matched_code` did not move
# by a byte.
def data_measure(m):
    return int(m.get("matched_data", 0)), m.get("matched_data_percent", 0.0)


# `matched_data` is not the answer either -- dc3 matches 0.08% of its data, so a
# changed literal moves it by nothing.  The answer is that an MSVC `??_C@` COMDAT
# is NAMED after a hash of its text, so the multiset of those names across our
# own objects is a fingerprint of every string literal the build emits.  If a
# fixer was supposed to rename a symbol and that fingerprint moved, it edited a
# literal, and no code-side ruler will ever say so.
STRLIT = re.compile(r"^\?\?_C@")


def string_fingerprint(repo, stats=None):
    """Multiset of `??_C@` COMDAT names across our own objects.

    `stats`, if given, receives the DENOMINATOR: how many units were in
    objdiff.json, how many had a base object on disk, and how many failed to
    parse.  Both drops used to be a bare `continue`, so a fingerprint of {}
    -- from a wrong root, an unbuilt tree, or a COFF layout the parser stopped
    understanding -- was indistinguishable from "this build emits no string
    literals", and `was ^ now` over two empty sets is the empty set, i.e. CLEAN.
    Measured 2026-08-22 on this tree: 2,224 units, 1,244 with no base object
    (target-only, legitimate), 980 parsed, 0 parse failures, 28,768 names.
    """
    names = set()
    seen = parsed = nofile = failed = 0
    for u in json.loads(Path(repo, "objdiff.json").read_text())["units"]:
        seen += 1
        p = Path(repo, u.get("base_path") or "")
        if not p.is_file():
            nofile += 1
            continue
        d = p.read_bytes()
        try:
            psym, nsym = struct.unpack_from("<II", d, 8)
            strt = psym + nsym * 18
            i = 0
            while i < nsym:
                rec = d[psym + i * 18: psym + i * 18 + 18]
                if rec[:4] == b"\0\0\0\0":
                    off, = struct.unpack_from("<I", rec, 4)
                    nm = d[strt + off:d.index(b"\0", strt + off)].decode("latin1")
                else:
                    nm = rec[:8].rstrip(b"\0").decode("latin1")
                if STRLIT.match(nm):
                    names.add((u["name"], nm))
                i += 1 + rec[17]
            parsed += 1
        except Exception:
            failed += 1
            continue
    if stats is not None:
        stats.update(units=seen, no_base_obj=nofile, parsed=parsed,
                     parse_failed=failed, names=len(names))
    return names


def source_of(repo, units):
    idx = {u["name"]: u.get("metadata", {}).get("source_path")
           for u in json.loads(Path(repo, "objdiff.json").read_text())["units"]}
    return {idx[u] for u in units if idx.get(u)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=".")
    ap.add_argument("--baseline", help="write the pre-edit `none` report here")
    ap.add_argument("--check", help="compare against this pre-edit report")
    ap.add_argument("--revert", action="store_true",
                    help="git checkout the sources behind any regressed unit "
                         "and repeat until `none` is restored")
    ap.add_argument("--max-rounds", type=int, default=4)
    args = ap.parse_args()
    repo = Path(args.repo).resolve()

    if args.baseline:
        build(repo)
        m, _ = report(repo, Path(args.baseline).resolve())
        st = {}
        fp = sorted("\t".join(x) for x in string_fingerprint(repo, st))
        if not fp:
            sys.exit(f"REFUSING to record a baseline: the string fingerprint is "
                     f"EMPTY ({st}). Every later --check compares `was ^ now`, "
                     f"and the empty set is symmetric-difference-clean against "
                     f"anything else that is also empty -- so this baseline "
                     f"would silently disable the literal check forever. Build "
                     f"the tree first.")
        # The denominator travels WITH the baseline. A --check whose tree parses
        # a different number of units is comparing two different populations,
        # and 1,244 of 2,224 units legitimately have no base object here, so
        # "some units dropped" is not by itself the signal -- a CHANGE in how
        # many dropped is.
        Path(args.baseline).with_suffix(".strings.json").write_text(
            json.dumps({"version": 2, "coverage": st, "names": fp}))
        print(f"baseline none: {m['matched_code_percent']:.6f}% "
              f"({m['matched_code']} bytes), {len(fp)} string COMDATs "
              f"from {st['parsed']}/{st['units']} units "
              f"({st['no_base_obj']} target-only, {st['parse_failed']} failed)")
        return 0

    # Guard the BASELINE too, not just the fresh side. A comparison is only as
    # sound as its weaker leg: a baseline written by a pre-landing binary, or
    # served warm out of a sidecar, is exactly as invalid a basis as a warm
    # fresh read -- and it is the leg nobody re-examines, because it was
    # recorded in an earlier session and has looked fine ever since. Cheap: one
    # dict lookup on a file this function already parses.
    base = json.loads(Path(args.check).read_text())
    refuse_if_not_a_full_recompute(base, label=f" (baseline {args.check})")
    before = {(u["name"], f["name"]) for u in base["units"]
              for f in u.get("functions", [])
              if f.get("fuzzy_match_percent") == 100.0}
    bm = base["measures"]

    tmp = Path(args.check).with_suffix(".now.json")
    for rnd in range(args.max_rounds):
        build(repo)
        m, now = report(repo, tmp)
        lost = before - now
        dbefore, dafter = data_measure(bm), data_measure(m)
        print(f"round {rnd}: none {bm['matched_code_percent']:.6f}% -> "
              f"{m['matched_code_percent']:.6f}%, {len(lost)} function(s) lost; "
              f"data {dbefore[0]} -> {dafter[0]} bytes")
        if dafter[0] < dbefore[0]:
            print("   DATA REGRESSED -- an edit changed a literal, not just a "
                  "name.  `none` alone would have called this clean.")
        # ⚠ ABSENT IS FAILED, NOT CLEAN -- the same rule this file already
        # applies to `provenance` in refuse_if_not_a_full_recompute(), and did
        # NOT apply here. Measured 2026-08-22 on this tree, same commit, same
        # objects, back to back:
        #     sidecar present, one entry mutated -> "STRING LITERALS CHANGED: 2
        #         COMDAT(s) differ", exit 1
        #     sidecar simply deleted             -> "none is intact -- the edits
        #         moved no instruction, no datum and no literal", exit 0
        # The second is a positive claim about a check that never ran, and the
        # string fingerprint is the ONLY instrument for that class (`none`
        # cannot see it, and dc3 matches 0.08% of its data so matched_data
        # cannot either).
        strfile = Path(args.check).with_suffix(".strings.json")
        strmoved = []
        if not strfile.exists():
            sys.exit(f"REFUSING to grade: {strfile} is absent, so the string-"
                     f"literal fingerprint cannot be compared. `none` is blind "
                     f"to a rename that walks into a string literal (the "
                     f"`static Message special_finished(\"special_finished\")` "
                     f"case in this file's own comment), and matched_data is "
                     f"too. Passing here would print 'no literal moved' about "
                     f"a check that did not run.\n"
                     f"  FIX: re-record with `none_guard.py --baseline "
                     f"{args.check}` on the PRE-EDIT tree. A baseline written "
                     f"before 2026-08-22 has no sidecar and is not repairable "
                     f"after the fact.")
        doc = json.loads(strfile.read_text())
        if isinstance(doc, list):          # v1 sidecar: bare list, no coverage
            was, was_cov = set(doc), None
        else:
            was, was_cov = set(doc["names"]), doc.get("coverage")
        now_cov = {}
        now_fp = {"\t".join(x) for x in string_fingerprint(repo, now_cov)}
        if now_cov.get("parse_failed"):
            sys.exit(f"REFUSING to grade: {now_cov['parse_failed']} object(s) "
                     f"failed to parse while fingerprinting ({now_cov}). Each "
                     f"one contributes zero names, which is arithmetically "
                     f"identical to 'this object has no literals'.")
        if was_cov and was_cov.get("parsed") != now_cov.get("parsed"):
            sys.exit(f"REFUSING to grade: the baseline fingerprinted "
                     f"{was_cov['parsed']} units, this tree fingerprinted "
                     f"{now_cov['parsed']}. `was ^ now` over two different "
                     f"populations is not a literal diff.\n"
                     f"  baseline: {was_cov}\n  now:      {now_cov}")
        strmoved = sorted(was ^ now_fp)
        if strmoved:
            print(f"   STRING LITERALS CHANGED: {len(strmoved)} COMDAT(s) "
                  f"differ from the baseline -- a rename walked into a "
                  f"literal.  `none` cannot see this.")
            for s in strmoved[:10]:
                print(f"      {s}")
        if not lost and dafter[0] >= dbefore[0] and not strmoved:
            # The claim now carries the denominator it is a claim about.
            print(f"none is intact -- the edits moved no instruction, no datum "
                  f"and no literal ({len(now_fp)} string COMDATs compared "
                  f"across {now_cov['parsed']}/{now_cov['units']} units)")
            return 0
        if not lost and strmoved:
            return 1
        if not args.revert:
            for u, f in sorted(lost)[:20]:
                print(f"   LOST  {u}  {f}")
            return 1
        srcs = source_of(repo, {u for u, _ in lost})
        if not srcs:
            sys.exit("regressed units have no source path -- cannot revert")
        print("   reverting: " + ", ".join(sorted(srcs)))
        subprocess.run(["git", "checkout", "--", *sorted(srcs)],
                       cwd=repo, check=True)
    sys.exit(f"still regressed after {args.max_rounds} rounds")


if __name__ == "__main__":
    sys.exit(main())
