import json, subprocess, os

REPO = "/home/free/code/milohax/dc3-decomp"
m = json.loads(open(os.path.join(REPO, "scratch/patches/manifest.json")).read())

ready = [e for e in m if e["category"] == "ready"
         and e.get("status") not in ("applied", "skipped")
         and e.get("delta", 0) > 0]
ready.sort(key=lambda e: e.get("delta", 0), reverse=True)

# Check which patches apply cleanly without conflicts
results = []
for e in ready:
    patch_path = os.path.join(REPO, "scratch/patches/ready", e["filename"])
    if not os.path.exists(patch_path):
        continue

    # Check if patch applies cleanly (dry-run)
    r = subprocess.run(
        ["git", "apply", "--check", str(patch_path)],
        cwd=REPO, capture_output=True, text=True
    )
    applies = r.returncode == 0

    # Read patch to count hunks and lines changed
    with open(patch_path) as f:
        content = f.read()
    hunks = content.count("@@")
    plus_lines = len([l for l in content.split("\n") if l.startswith("+") and not l.startswith("+++")])
    minus_lines = len([l for l in content.split("\n") if l.startswith("-") and not l.startswith("---")])

    files = e.get("target_files", [])
    unit = e.get("unit", "?")
    symbol = e.get("symbol", "")
    demangled = e.get("demangled", symbol)

    results.append({
        "filename": e["filename"],
        "applies": applies,
        "delta": e.get("delta", 0),
        "target_pct": e.get("patch_percent", 0),
        "current_pct": e.get("current_percent", 0),
        "hunks": hunks,
        "plus": plus_lines,
        "minus": minus_lines,
        "files": files,
        "unit": unit,
        "symbol": symbol,
        "demangled": demangled,
    })

# Show patches that apply cleanly, sorted by delta
clean = [r for r in results if r["applies"]]
print(f"=== {len(clean)} patches apply cleanly (of {len(results)} checked) ===\n")
print(f"{'Delta':>7}  {'Target':>6}  {'Cur':>5}  {'H':>2}  {'+':>3}  {'-':>3}  Unit / Symbol")
print("-" * 120)

seen_units = set()
for r in clean:
    unit_key = r["unit"]
    dup = " (DUP)" if unit_key in seen_units else ""
    seen_units.add(unit_key)
    name = r["demangled"] if len(r["demangled"]) < 60 else r["demangled"][:57] + "..."
    print(f"{r['delta']:+6.1f}%  {r['target_pct']:5.1f}%  {r['current_pct']:5.1f}  {r['hunks']:2d}  {r['plus']:3d}  {r['minus']:3d}  {r['unit']}")
    print(f"         {name}{dup}")
    for f in r["files"]:
        print(f"         -> {f}")
    print()
