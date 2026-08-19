#!/usr/bin/env python3
"""Count REAL `objdiff-cli` invocations across Claude Code session transcripts.

This is the evidence base behind the guidance in `CLAUDE.md` and
`docs/tools/REFERENCE.md`: the blanket rule "do not call `objdiff-cli` directly"
was unfollowable, because agents were reaching for capabilities the MCP wrappers
did not have.  Asserting that from memory is worth nothing, so the method lives
here and anyone can re-run it.

    python3 scripts/orchestrator/transcript_cli_sweep.py
    python3 scripts/orchestrator/transcript_cli_sweep.py --json --out /tmp/sweep.json

WHAT COUNTS AS "REAL"
---------------------
A transcript line mentioning `objdiff-cli` is only counted when it is an actual
`Bash` **tool_use** whose command *runs* the binary.  Three things are excluded,
because counting them is how you turn 296 into a much larger and meaningless
number:

  * assistant prose and tool *results* -- only `tool_use` inputs are read;
  * commands that merely mention the string -- `grep objdiff-cli`,
    `ls -la bin/objdiff-cli`, `echo`, `cat`, comment-only lines;
  * the `-p <dir>` / cwd of another decomp tree is not excluded but is
    *attributed*, so the DC3-scoped subtotal is separable.

Every exclusion is counted and printed, so the denominator is stated rather
than implied -- the same contract `symbol_sweep.py` follows.

SCOPE ATTRIBUTION
-----------------
The transcript's *project directory* is the session's cwd, NOT the tree the
command targeted -- a `decomp-synth` session routinely runs `objdiff-cli -p
/home/free/code/milohax/dc3-decomp`.  Scoping by slug therefore answers the
wrong question.  This script attributes by what the COMMAND names (`-p`, `cd`,
explicit paths), and reports `unscoped` separately rather than silently folding
it into either side.

CAVEAT ON REPRODUCIBILITY -- READ BEFORE QUOTING A NUMBER
---------------------------------------------------------
This is a *method*, not a frozen number, and the published figures behind the
`CLAUDE.md` guidance ("474 transcripts, 296 invocations, 259 DC3, led by
`--include-data` 88 and `--batch` 49") **were measured over an unrecorded
subset and do not reproduce as stated.**  Measured 2026-08-19 over the whole
corpus:

  * **16,661** transcript files, not 474.  The bulk are subagent transcripts
    nesting two and three levels below the project dir; a flat `*.jsonl` glob
    sees only **231** of them (1.4%).  Getting this wrong is easy and it is
    almost certainly what happened -- one plausible flat-glob subset gives 297
    real invocations, i.e. the published 296 to within one.
  * **2,722** real invocations across the full corpus.
  * DC3-scoped: **304** attributed by command content, plus **230** whose
    command names no tree but whose session was DC3 -- so ~534 on the most
    generous reading, against 583 explicitly naming another repo.  DC3 is a
    large minority, not the 259/296 = 88% the published split implies.
  * DC3-scoped flag histogram: `--include-data` **15** and `--batch` **18**,
    not 88 and 49.

**What survives, and it is the part the guidance actually rests on:** agents
really did reach for `--include-data` and `--batch` against DC3 -- capabilities
the MCP wrappers did not have until 2026-08-19 -- and direct invocation was
routine rather than exceptional (2,722 of them).  The blanket prohibition was
unfollowable.  That conclusion holds on any slice.  The precise counts do not;
quote this script's current output instead of the historical figures.
"""
from __future__ import annotations

import argparse
import collections
import json
import re
import sys
from pathlib import Path
from typing import Any, Dict, Iterable, List

TRANSCRIPT_ROOT = Path.home() / ".claude" / "projects"

# Scope attribution, by what the COMMAND names rather than the session's cwd.
# Deliberately conservative: a command naming neither is `unscoped`, and a
# command naming both is `both`, so neither side can absorb the ambiguity.
DC3_CMD = re.compile(r"dc3-decomp|dc3-relocverify|373307D9", re.IGNORECASE)
OTHER_CMD = re.compile(
    r"rb3-xenon|/rb3[/\b]|cea-decomp|godzilla-decomp|decomp-synth|"
    r"decomp-clones|decomp-cli",
    re.IGNORECASE,
)

# A command that RUNS the binary, vs one that merely names it.  Require the
# token to be in command position: start of line/pipeline, or after a
# shell operator, optionally prefixed by a path and/or `time`/`env`.
RUNS = re.compile(
    r"(?:^|[\n;&|]|\$\(|`|&&|\|\|)\s*"
    r"(?:time\s+|env\s+[\w=]+\s+|nohup\s+)*"
    r"(?:\./)?(?:[\w./-]*/)?objdiff-cli\b",
)

# Commands that merely mention the binary.  Checked BEFORE `RUNS` so that
# `grep objdiff-cli build.ninja` is excluded even though it contains the token.
MENTIONS_ONLY = re.compile(
    r"\b(?:grep|rg|ug|ls|cat|head|tail|echo|printf|find|stat|file|which|"
    r"readlink|wc|awk|sed|test|\[)\b[^\n;&|]*objdiff-cli",
)

FLAGS_OF_INTEREST = [
    "--include-data", "--batch", "--include-instructions", "--analyze",
    "--verdict", "--summary", "--full-listing", "--concise", "--map-file",
    "--build", "--incremental", "--full-build", "-1", "-2", "-p", "-u",
    "doc-links", "report",
]


def iter_tool_uses(path: Path) -> Iterable[Dict[str, Any]]:
    """Yield every Bash tool_use input dict in one .jsonl transcript."""
    try:
        with path.open("r", errors="replace") as fh:
            for line in fh:
                line = line.strip()
                if not line or "objdiff-cli" not in line:
                    continue
                try:
                    rec = json.loads(line)
                except json.JSONDecodeError:
                    continue
                msg = rec.get("message")
                if not isinstance(msg, dict):
                    continue
                content = msg.get("content")
                if not isinstance(content, list):
                    continue
                for block in content:
                    if not isinstance(block, dict):
                        continue
                    if block.get("type") != "tool_use":
                        continue
                    if block.get("name") not in ("Bash", "BashOutput"):
                        continue
                    inp = block.get("input") or {}
                    cmd = inp.get("command")
                    if isinstance(cmd, str) and "objdiff-cli" in cmd:
                        yield {"command": cmd, "session": path.stem}
    except OSError:
        return


def classify(cmd: str) -> str:
    if MENTIONS_ONLY.search(cmd) and not RUNS.search(
        MENTIONS_ONLY.sub("", cmd)
    ):
        return "mentions-only"
    if not RUNS.search(cmd):
        return "not-in-command-position"
    return "real"


def scope_of(cmd: str, slug: str) -> str:
    d, o = bool(DC3_CMD.search(cmd)), bool(OTHER_CMD.search(cmd))
    if d and not o:
        return "dc3"
    if o and not d:
        return "other-repo"
    if d and o:
        return "both-named"
    # Nothing in the command says which tree.  Fall back to the session cwd,
    # but keep it in its own bucket -- it is an inference, not an observation.
    return "unscoped-session-dc3" if "dc3-decomp" in slug else "unscoped-session-other"


def main(argv: List[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", type=Path, default=TRANSCRIPT_ROOT,
                    help=f"transcript root (default: {TRANSCRIPT_ROOT})")
    ap.add_argument("--json", action="store_true", help="emit JSON")
    ap.add_argument("--out", type=Path, help="write to this path instead of stdout")
    args = ap.parse_args(argv)

    if not args.root.is_dir():
        print(f"error: transcript root not found: {args.root}", file=sys.stderr)
        return 2

    sessions_scanned = 0
    sessions_with_hits = set()
    drops: collections.Counter = collections.Counter()
    real_by_scope: collections.Counter = collections.Counter()
    flag_counts: collections.Counter = collections.Counter()
    flag_counts_dc3: collections.Counter = collections.Counter()
    real_total = 0

    for proj in sorted(args.root.iterdir()):
        if not proj.is_dir():
            continue
        # rglob, not glob: subagent transcripts nest two and three levels
        # deep under the project dir. A flat `*.jsonl` sees 231 of the
        # 16,661 files on this box -- 1.4% of the corpus -- and every
        # subagent invocation, which is most of them, is invisible to it.
        for jf in sorted(proj.rglob("*.jsonl")):
            sessions_scanned += 1
            for use in iter_tool_uses(jf):
                cmd = use["command"]
                cls = classify(cmd)
                if cls != "real":
                    drops[cls] += 1
                    continue
                scope = scope_of(cmd, proj.name)
                real_total += 1
                real_by_scope[scope] += 1
                sessions_with_hits.add(f"{proj.name}/{jf.stem}")
                for flag in FLAGS_OF_INTEREST:
                    if re.search(rf"(?<![\w-]){re.escape(flag)}(?![\w-])", cmd):
                        flag_counts[flag] += 1
                        if scope == "dc3":
                            flag_counts_dc3[flag] += 1

    result = {
        "sessions_scanned": sessions_scanned,
        "sessions_containing_a_real_invocation": len(sessions_with_hits),
        "real_invocations": real_total,
        "real_by_scope": dict(real_by_scope),
        "excluded": dict(drops),
        "flags_all_scopes": dict(flag_counts.most_common()),
        "flags_dc3_only": dict(flag_counts_dc3.most_common()),
    }

    if args.json:
        text = json.dumps(result, indent=2)
    else:
        L = [
            "# objdiff-cli direct-invocation sweep",
            "",
            f"  transcript files scanned          : {sessions_scanned}",
            f"  files with >=1 real invocation    : {len(sessions_with_hits)}",
            f"  REAL invocations                  : {real_total}",
            "",
            "  scope, by what the COMMAND names (not the session cwd):",
        ]
        for k in ("dc3", "other-repo", "both-named",
                  "unscoped-session-dc3", "unscoped-session-other"):
            L.append(f"    {k:<28} {real_by_scope.get(k, 0)}")
        L += [
            "",
            "  !! the published figures (474 files / 296 / 259 DC3) were taken",
            "     over an unrecorded SUBSET and do not reproduce; this run read",
            "     %d files. Quote this output, not the historical numbers." % sessions_scanned,
            "",
            "  excluded (mentioned but not run):",
        ]
        for reason, n in drops.most_common():
            L.append(f"    {reason:<28} {n}")
        if not drops:
            L.append("    (none)")
        L += ["", "  flags on DC3-scoped real invocations:"]
        for flag, n in flag_counts_dc3.most_common(12):
            L.append(f"    {flag:<24} {n}")
        text = "\n".join(L)

    if args.out:
        args.out.write_text(text + "\n")
        print(f"wrote {args.out}")
    else:
        print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
