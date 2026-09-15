# Print match_percent_normalized + size + unit for named symbols.
#
#   python3 scripts/w8g_pct.py '?Foo@@YAXXZ' ...
#   ... | python3 scripts/w8g_pct.py
#
# Reads the report of the tree this script lives in.  Override with
# DC3_REPORT=/path/to/report.json (e.g. to read another worktree's).
import json, os, sys
from pathlib import Path

repo = Path(__file__).resolve().parent.parent
report = Path(os.environ.get("DC3_REPORT", repo / "build" / "373307D9" / "report.json"))
if not report.is_file():
    sys.exit(f"no report at {report} -- run a full `ninja` first, or set DC3_REPORT")
rep = json.load(open(report))

want = sys.argv[1:] or [l.strip() for l in sys.stdin if l.strip()]
idx = {}
for u in rep["units"]:
    for f in u.get("functions", []):
        idx.setdefault(f["name"], []).append((u["name"], f.get("match_percent_normalized"), f.get("size")))
for w in want:
    if w in idx:
        for un, p, s in idx[w]:
            print(f"{p!s:>12}  {s:>6}  {un}  {w}")
    else:
        print(f"{'MISSING':>12}  {'':>6}  ?  {w}")
