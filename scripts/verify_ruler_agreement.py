#!/usr/bin/env python3
"""Assert that `objdiff-cli diff` scores a function the same way `report generate` does.

Why this exists
---------------
`objdiff-cli report generate` and `objdiff-cli diff` carry DIFFERENT hardcoded
base configs, and neither is the schema default:

    report generate   objdiff-cli/src/cmd/report.rs:581
                      functionRelocDiffs=none, combineDataSections=true,
                      combineTextSections=true, ppc.calculatePoolRelocations=false

    diff              objdiff-cli/src/cmd/diff.rs:1070  (and the --batch path at
                      diff.rs:1807, identically)
                      functionRelocDiffs=data_value, and the other three at their
                      SCHEMA defaults: false / false / TRUE

Both then layer the project's `objdiff.json` "options" block on top.  For a long
time this repo's options block set only `functionRelocDiffs`, which fixed the
ruler both paths agreed to argue about and left them disagreeing on the other
three.

`ppc.calculatePoolRelocations` is the one that bites.  It SYNTHESIZES
`R_PPC_NONE` "fake" relocations for pooled data loads
(`objdiff-core/src/arch/ppc/mod.rs:819 make_fake_pool_reloc`; the schema calls
them "fake relocations" in as many words), reconstructed per object from that
object's own symbol table.  A dtk-carved target obj -- a whole linked data
section, anonymous `lbl_*` labels -- and our MSVC per-TU COMDAT obj do not
reconstruct the same set.  `reloc_eq`
(`objdiff-core/src/diff/code.rs:1338`) charges a relocation present on one side
and absent on the other under EVERY `functionRelocDiffs` mode except `none`,
`name_check` included.  So the per-function path charged rows whose two sides
were textually identical:

    [128] replace: `subi r28, r11, 0x8` vs `subi r28, r11, 0x8`

Measured over the whole binary on 2026-08-31 (one worktree, one objdiff-cli
4.2.8, report cache cold): **155 functions / 120,728 bytes** scored LOWER
through `diff` than through `report generate`, and **zero** scored higher.
49 of them (28,240 B) read exactly 100.0 in report.json and <100 through
`diff`/`run_objdiff` -- the class on which a lane refuses a promotion for a
reason that does not exist.

The fix is one line of project config, not a tool change: pin all four keys in
`objdiff.json`'s `options`, which BOTH paths honour.  Doing so changed no
recorded number (matched_functions 29,902 / matched_code 5,056,848 /
44.45998% before and after) and collapsed the 155 to zero.

What this checks
----------------
`--check` (fast, ~0.2 s): every key on which the two CLI base configs disagree
is pinned in `objdiff.json`'s options block AND agrees with the value
`report.json`'s own `provenance.diff_config` says the grading run used.
report.json is authoritative here by construction: it is not a description of
the config, it IS the config the score was taken under.

`--verify-scores`: the end-to-end assertion.  Batch-diffs symbols through
`objdiff-cli diff --batch` with NO `-c` flags -- i.e. exactly what a lane's
per-function tooling sees -- and compares each `canonical_match_percent`
against report.json's `match_percent_normalized`.  Rows the batch path scored
against ANOTHER unit's base object (its cross-unit COMDAT fallback, disclosed
as `base_unit` in the output) are reported separately: the report scores
per-unit only, so those two numbers are answers to different questions and
their disagreement is not this defect.

`--selftest`: the negative control.  Re-runs `--verify-scores` over the same
symbols with `-c ppc.calculatePoolRelocations=true`, restoring the `diff`
path's own default, and REQUIRES that to produce disagreements.  A check that
cannot be made to fail is not a check; if the flipped run comes back clean this
exits 5 saying the probe went vacuous rather than reporting success.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# The keys on which `report generate`'s base config and `diff`'s base config
# disagree.  Values here are `report generate`'s -- the grading semantics every
# recorded number in this project was taken under.  This table is a FALLBACK for
# the "no report.json yet" case; when a report exists its provenance wins.
DIVERGENT_KEYS: dict[str, str] = {
    "functionRelocDiffs": "name_check",  # set by this project; both paths honour it
    "combineDataSections": "true",
    "combineTextSections": "true",
    "ppc.calculatePoolRelocations": "false",
}

# The knob the negative control flips.  Restoring `diff`'s own default here must
# reintroduce disagreements, or the probe proved nothing.
SELFTEST_OVERRIDE = "ppc.calculatePoolRelocations=true"

# Units carrying a known witness -- a function whose score moves when the knob is
# flipped -- so `--selftest` costs seconds rather than a whole-binary sweep.
# These ROT as work lands.  When they do, the selftest says so and tells you to
# re-run with `--all`; it never silently reports success from an empty probe.
WITNESS_UNITS = (
    "default/system/rndobj/DOFProc_NG",
    "default/system/zlib/trees",
    "default/system/oggvorbis/psy",
    "default/system/world/Crowd",
    "default/system/os/File",
    "default/system/rndobj/Graph",
    "default/system/obj/DataFile",
)


def _norm(value) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    return str(value)


def find_report(repo: Path) -> Path | None:
    hits = sorted(repo.glob("build/*/report.json"))
    return hits[0] if hits else None


def grader_config(repo: Path) -> tuple[dict[str, str], str]:
    """The effective config the grading run used, and where that came from."""
    report = find_report(repo)
    if report is not None:
        with report.open() as fh:
            data = json.load(fh)
        entries = (data.get("provenance") or {}).get("diff_config") or []
        if entries:
            cfg = {}
            for entry in entries:
                key, _, value = entry.partition("=")
                cfg[key] = value
            return cfg, f"{report.relative_to(repo)} provenance.diff_config"
    return dict(DIVERGENT_KEYS), "verify_ruler_agreement.DIVERGENT_KEYS (FALLBACK: no report.json)"


def check_config(repo: Path) -> int:
    with (repo / "objdiff.json").open() as fh:
        options = json.load(fh).get("options") or {}
    pinned = {k: _norm(v) for k, v in options.items()}
    grader, source = grader_config(repo)

    print(f"grader config source: {source}")
    problems = []
    for key, fallback in DIVERGENT_KEYS.items():
        want = grader.get(key, fallback)
        have = pinned.get(key)
        if have is None:
            problems.append(
                f"  {key}: NOT PINNED in objdiff.json options -- "
                f"`report generate` uses {want!r}, `objdiff-cli diff` will use its own "
                f"base default instead"
            )
        elif have != want:
            problems.append(f"  {key}: objdiff.json says {have!r}, the grading run used {want!r}")
        else:
            print(f"  OK  {key} = {have}")

    if problems:
        print(
            "\nFAIL: `objdiff-cli diff` and `objdiff-cli report generate` will not agree.\n"
            "Every per-function measurement -- run_objdiff, run_diff_inspect, "
            "run_symbol_sweep, `diff --batch` -- reads a different ruler than "
            "report.json, and the per-function side reads LOW.\n"
        )
        print("\n".join(problems))
        print(
            "\nFix in tools/project.py's `options` block (NOT by passing -c at each "
            "call site), then re-run configure.py."
        )
        return 1
    print("\nOK: both objdiff-cli entry points resolve the same ruler.")
    return 0


def batch_scores(repo: Path, symbols: list[str], extra_config: list[str]) -> dict[str, dict]:
    cmd = [str(repo / "bin" / "objdiff-cli"), "diff", "--batch", "-p", str(repo), "-f", "json"]
    for item in extra_config:
        cmd += ["-c", item]
    proc = subprocess.run(
        cmd,
        input="\n".join(symbols) + "\n",
        capture_output=True,
        text=True,
        cwd=str(repo),
    )
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr[-4000:])
        raise SystemExit(f"objdiff-cli diff --batch failed (exit {proc.returncode})")
    out: dict[str, dict] = {}
    for line in proc.stdout.splitlines():
        if not line.strip():
            continue
        row = json.loads(line)
        if "error" in row:
            continue
        out[row["symbol"]] = row
    return out


def load_report_rows(repo: Path, units: tuple[str, ...] | None) -> dict[str, tuple[str, float]]:
    report = find_report(repo)
    if report is None:
        raise SystemExit("no build/*/report.json -- run ninja first")
    with report.open() as fh:
        data = json.load(fh)
    seen: dict[str, int] = {}
    rows: dict[str, tuple[str, float]] = {}
    for unit in data["units"]:
        if units is not None and unit["name"] not in units:
            continue
        for fn in unit.get("functions", []):
            seen[fn["name"]] = seen.get(fn["name"], 0) + 1
            rows[fn["name"]] = (unit["name"], fn["match_percent_normalized"])
    # A name defined in more than one unit cannot be attributed from a batch run
    # (the batch resolves a bare name to ONE unit), so drop it rather than guess.
    return {k: v for k, v in rows.items() if seen[k] == 1}


def verify_scores(repo: Path, units: tuple[str, ...] | None, extra_config: list[str]) -> dict:
    rows = load_report_rows(repo, units)
    scores = batch_scores(repo, sorted(rows), extra_config)
    result = {
        "examined": 0,
        "agree": 0,
        "disagree": [],
        "cross_unit_fallback": 0,
        "unpaired": 0,
        "unresolved": 0,
        "universe": len(rows),
    }
    for name, (unit, want) in rows.items():
        row = scores.get(name)
        if row is None:
            result["unresolved"] += 1
            continue
        if row.get("unit") != unit:
            result["unresolved"] += 1
            continue
        got = row.get("canonical_match_percent")
        if got is None:
            result["unpaired"] += 1
            continue
        result["examined"] += 1
        if abs(got - want) < 1e-4:
            result["agree"] += 1
        elif row.get("base_unit"):
            result["cross_unit_fallback"] += 1
        else:
            result["disagree"].append((name, unit, want, got))
    return result


def report_scores(label: str, res: dict) -> None:
    print(
        f"{label}: universe {res['universe']} | examined {res['examined']} | "
        f"agree {res['agree']} | disagree {len(res['disagree'])} | "
        f"cross-unit base_unit fallback {res['cross_unit_fallback']} | "
        f"unpaired (no base symbol) {res['unpaired']} | unresolved {res['unresolved']}"
    )
    for name, unit, want, got in sorted(res["disagree"], key=lambda r: r[2] - r[3], reverse=True)[
        :25
    ]:
        print(f"    report {want:9.5f}  diff {got:9.5f}   {unit}  {name}")
    if len(res["disagree"]) > 25:
        print(f"    ... and {len(res['disagree']) - 25} more")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--repo", default=str(REPO_ROOT), help="project dir (default: this checkout)")
    ap.add_argument("--check", action="store_true", help="config-pin assertion only (~0.2 s)")
    ap.add_argument("--verify-scores", action="store_true", help="end-to-end score comparison")
    ap.add_argument("--selftest", action="store_true", help="negative control: flip the knob back and require failure")
    ap.add_argument("--all", action="store_true", help="with --verify-scores/--selftest: every unit, not just the witness units")
    args = ap.parse_args()

    repo = Path(args.repo).resolve()
    if not (args.check or args.verify_scores or args.selftest):
        args.check = True

    rc = 0
    if args.check:
        rc |= check_config(repo)

    units = None if args.all else WITNESS_UNITS

    if args.verify_scores:
        res = verify_scores(repo, units, [])
        report_scores("as configured", res)
        if res["examined"] == 0:
            print("FAIL: examined 0 functions -- an empty comparison agrees by construction.")
            return 4
        if res["disagree"]:
            print("FAIL: the per-function path disagrees with report.json on the rows above.")
            rc |= 1

    if args.selftest:
        base = verify_scores(repo, units, [])
        flipped = verify_scores(repo, units, [SELFTEST_OVERRIDE])
        report_scores("as configured        ", base)
        report_scores(f"with {SELFTEST_OVERRIDE}", flipped)
        if base["examined"] == 0:
            print("FAIL: examined 0 functions.")
            return 4
        if not flipped["disagree"]:
            print(
                "\nVACUOUS (exit 5): restoring `diff`'s own "
                f"{SELFTEST_OVERRIDE} produced NO disagreement over "
                f"{flipped['examined']} functions, so this probe cannot distinguish a "
                "working check from a broken one.\n"
                "The witness units have rotted. Re-run with --all to search the whole "
                "binary, and refresh WITNESS_UNITS from what it finds. Do NOT read this "
                "as a pass."
            )
            return 5
        if base["disagree"]:
            print("\nFAIL: the as-configured run disagrees with report.json.")
            rc |= 1
        else:
            print(
                f"\nOK: as-configured agrees on all {base['examined']} functions, and the "
                f"control flip produces {len(flipped['disagree'])} disagreement(s) -- "
                "the check can fail."
            )

    return rc


if __name__ == "__main__":
    raise SystemExit(main())
