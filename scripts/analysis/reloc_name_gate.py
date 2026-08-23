#!/usr/bin/env python3
"""List every function whose RELOCATION TARGET NAMES disagree with the target's.

WHY THIS EXISTS
===============
`match_percent_normalized` is defined as `diff_score - arg_diff_score`, and
objdiff folds relocation penalties into `arg_diff_score` by design
(`objdiff-core/src/diff/code.rs`).  **No `-c` flag makes a wrong callee cost a
normalized point.**  `scripts/orchestrator/mcp_server.py` and
`scripts/sync_objdiff.py` additionally hard-code `functionRelocDiffs=none`, so
`decomp.db.current_percent` is blind as well.

Demonstrated on this tree (2026-08-19): repointing all 13 `bl` sites of a
100 %-matched function at a nonexistent decoy -- changing ZERO instruction
bytes -- left `run_objdiff` printing 64 equal / **0 mismatches** and normalized
at exactly 100.0.  A function can therefore call entirely the wrong callees and
score a perfect, zero-mismatch 100 %.

Two confirmed bugs of that shape:
  * `createFilter`  -- `EQEffect.cpp` declared it `extern "C"`, so the object
    referenced an unmangled symbol while the target calls
    `?createFilter@@YAXW4FilterType@@...`.
  * `KinectShareConnection::Poll` -- calls `MakeString<char>` where the target
    calls `MakeString<unsigned char>` (`8268f6e8` vs `825f7ae0`, two distinct
    addresses in `orig/373307D9/ham_xbox_r.map`, not an ICF fold).

WHAT IT DOES -- AND WHAT IT DELIBERATELY DOES NOT
=================================================
It **lists**.  It does not classify a row away.  The one classifier this project
already had for this class (`split_reloc_residency.py`) would have buried
`createFilter` as a candidate ICF fold, so every row here is printed with the
evidence that would let you adjudicate it, and nothing is dropped on the floor.

The only judgement applied is a set of named, individually-countable
`--exempt` buckets, and **the count of every bucket is always printed** so the
denominator is never hidden.  `--no-exempt` prints the raw population.

DENOMINATOR
===========
Every run prints:  rows scanned / rows in the population / bytes, plus the
per-bucket counts.  A numerator without its denominator is not a measurement.

THE POPULATION
==============
A row qualifies when it scores 100 % under `functionRelocDiffs=none` but below
100 % under the graded ruler (`name_check`, read from `report.json`'s own
`provenance.diff_config` via `scripts/analysis/ruler.py`).  That delta isolates
the relocation-NAME class exactly: `none` and the graded ruler differ in one
key, so nothing else can move between the two legs.  Both legs load
`build/373307D9/icf_aliases.map`, so folds the project has already adjudicated
are forgiven before a row ever reaches this tool.

ADJUDICATION
============
Each charged pair (target_name, our_name) is resolved in the shipped MSVC linker
map `orig/373307D9/ham_xbox_r.map`:

    FOLD                both names occupy the same address  -> ICF, benign
    DIFFERENT_ADDRESS   both present, different addresses    -> REAL divergence
    BASE_NOT_IN_MAP     our name absent from the image       -> lead (weak: an
                        ICF fold loser can also be absent)
    TARGET_NOT_IN_MAP   dtk synthetic (`merged_*`/`OnlyReturns`) -> usually fold
    NEITHER_IN_MAP      static/local-scope symbols the map never lists

NEGATIVE CONTROL
================
`--selftest` re-applies the `createFilter` bug **in memory** (no build, no edit)
against a recorded fixture of the charge and asserts the tool reports it.  For
the end-to-end control, re-declare `createFilter` `extern "C"` in
`src/system/synth/EQEffect.cpp`, rebuild, and re-run: the row must appear with
the pair
    ?createFilter@@YAXW4FilterType@@MMMMPAUFilterCoeff@@@Z  vs  createFilter
See docs/decomp/patterns/relocation-names-are-unmetered.md.

USAGE
=====
    python3 scripts/analysis/reloc_name_gate.py --project . --map orig/373307D9/ham_xbox_r.map
    python3 scripts/analysis/reloc_name_gate.py --project . --json-out /tmp/rows.json
    python3 scripts/analysis/reloc_name_gate.py --selftest
"""

from __future__ import annotations

import argparse
import collections
import json
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))

from scripts.analysis import ruler as ruler_mod  # noqa: E402

MAP_LINE = re.compile(r"^\s*[0-9A-Fa-f]{4}:[0-9A-Fa-f]{8}\s+(\S+)\s+([0-9A-Fa-f]{8})\b")

# ── Exemption buckets ────────────────────────────────────────────────────────
# Each is a named, counted bucket.  Nothing is silently dropped: --no-exempt
# turns them all off and the per-bucket counts print either way.

#: dtk placeholder for an anonymous namespace whose hash it could not recover:
#: `?x@?A@@3MA` (target) vs `?x@?A0xf503845b@@3MA` (ours).  Same variable.
_ANON_NS = re.compile(r"@\?A(0x[0-9a-f]+)?@")

#: MSVC lexical-scope counter on a function-local static:
#: `?_s@?HP@??Foo@@...` vs `?_s@?JD@??Foo@@...`.  Same variable, different
#: number of preceding scopes -- an inlining-shape signal, not a wrong callee.
#: Two encodings appear: a bare digit (`?9??Foo`) for 0..9 and a letter run
#: terminated by `@` (`?M@??Foo`, `?HP@??Foo`) above that, so both must match or
#: the exemption silently misses half the class.
_SCOPE = re.compile(r"^(\?[^@?]*@)\?(?:[0-9]|[A-P]+@)(\?\?.*)$")

#: dtk synthetics for ICF fold winners it could not name.
_SYNTHETIC = re.compile(r"^(merged_|OnlyReturns$|Returns\d)")


def _strip_anon(name: str) -> str:
    return _ANON_NS.sub("@?A@", name)


def _strip_scope(name: str) -> str | None:
    m = _SCOPE.match(name)
    return (m.group(1) + m.group(2)) if m else None


def classify_exempt(target, base) -> str | None:
    """Named exemption bucket for a pair, or None if the pair stands.

    A `Symbol` typed_arg can carry a non-string value (or None) when one side has
    no relocation at all.  Such a pair is NOT exempt -- a missing relocation
    where the target has one is the loudest possible version of this bug -- so it
    falls through to the map adjudication rather than being dropped here.
    """
    if not isinstance(target, str) or not isinstance(base, str):
        return None
    if _strip_anon(target) == _strip_anon(base):
        return "anon_ns_placeholder"
    ts, bs = _strip_scope(target), _strip_scope(base)
    if ts is not None and bs is not None and ts == bs:
        return "scope_counter"
    if _SYNTHETIC.match(target):
        return "dtk_synthetic_fold_name"
    return None


def load_map_index(path: str) -> dict[str, list[str]]:
    idx: dict[str, set] = collections.defaultdict(set)
    with open(path, errors="replace") as fh:
        for line in fh:
            m = MAP_LINE.match(line)
            if m:
                idx[m.group(1)].add(m.group(2).lower())
    return {k: sorted(v) for k, v in idx.items()}


CFG_LINE = re.compile(r"^\s*(\S+)\s*=\s*[.\w]+:0x([0-9A-Fa-f]+)")


def load_config_symbols(path: str) -> dict[str, int]:
    """symbol -> address, from config/<title>/symbols.txt (dtk's split input)."""
    out: dict[str, int] = {}
    with open(path, errors="replace") as fh:
        for line in fh:
            m = CFG_LINE.match(line)
            if m:
                out.setdefault(m.group(1), int(m.group(2), 16))
    return out


def config_map_disagreements(cfg: dict[str, int], idx: dict[str, list[str]]):
    """Config symbols whose address contradicts the shipped linker map.

    A single one of these manufactures a *false* wrong-callee story: it stamps a
    real symbol's name onto whatever block it lands in, and every downstream
    reader -- objdiff, this gate, a lane triaging the row -- repeats it. On
    2026-08-19 exactly one existed in 107,552 shared names
    (`?sJointParents@BaseSkeleton@@...`, config 0x8202EE20 vs map 0x8202EEC0),
    and it was the row that read as "MirrorJoint indexes the joint-PARENT table",
    i.e. a shipped gameplay bug. It was not one.
    """
    shared = [k for k in cfg if k in idx]
    bad = [(k, cfg[k], idx[k]) for k in shared
           if f"{cfg[k]:08x}" not in idx[k]]
    return len(shared), sorted(bad)


_ASM_OBJ = re.compile(r'^\.obj\s+"([^"]+)",')
_ASM_VTBL = re.compile(r'^\t\.4byte\s+"(\?\?_7([A-Za-z0-9_@$?]+)@@6B@)"')
#: `?name@@3V<Class>@@A` -- a global object of class type.
_DECL_CLASS = re.compile(r"^\?[^@]+@@3V([A-Za-z0-9_@$?]+)@@A$")


def vtable_type_disagreements(asm_root: Path, idx: dict[str, list[str]] | None = None):
    """Global objects whose config NAME contradicts the vtable they hold.

    A global of class type starts with its own vtable pointer, so
    `?gShaderStandard@@3VRndShaderStandard@@A` holding `??_7RndShaderMultimesh@@6B@`
    is internally impossible: the config named the wrong slot.

    This catches a class the config-vs-map check cannot -- .data globals are not
    listed in the shipped linker map, so a whole run of them can be shifted by
    one slot with the map staying silent. That is exactly what had happened to
    all twelve RndShader globals (fixed 2026-08-19): every ??__FgShaderX thunk
    charged a relocation naming the NEXT shader, which reads as twelve source
    bugs and was one config edit.

    ⚠ A NAME disagreement is not an IDENTITY disagreement. Pass `idx` (the
    linker-map index this module already builds) and a pair whose two names
    resolve to the SAME address is forgiven as an ICF fold rather than reported:
    the two names denote one object, so nothing contradicts anything.

    That is not hypothetical -- it was this check's only standing finding.
    `?g_csOverrideRestore@@3VCCriticalSection@@A` holding `??_7RefCount@Nui@@6B@`
    looked like a type bug and is six names for one address in
    `orig/373307D9/ham_xbox_r.map`::

        0001:00164618  ??_7RefCount@Nui@@6B@             82164c18  nuiapi:truecolor.obj
        0001:00164618  ??_7JSONBufferManager@@6B@        82164c18  os:jsonbuffer.obj
        0001:00164618  ??_7ControlMethod@TrueColor@@6B@  82164c18  nuiapi:controlmethod.obj
        0001:00164618  ??_7CCriticalSection@@6B@         82164c18  xmp:xmp.obj
        ... (6 total)

    all one-slot vtables holding one folded scalar-deleting destructor.
    `config/373307D9/symbols.txt` records only ONE name for that address
    (`??_7RefCount@Nui@@6B@`), so dtk renders the relocation with the only name
    it has. `CCriticalSection` really does have a vptr at +0 -- the dynamic
    initializer passes `this+4` to `RtlInitializeCriticalSection`, skipping it --
    so the premise held and only the naming was aliased.

    The forgiveness is narrow ON PURPOSE. All sixteen `RndShader*` vtables sit at
    distinct map addresses, so the whole 2026-08-19 failure mode still reports;
    verified by sabotage, see `--selftest`.

    Returns (checked, [(file, symbol, vtable_symbol, verdict)], forgiven).
    """
    bad, checked, forgiven = [], 0, []
    for path in sorted(asm_root.rglob("*.s")):
        cur, first = None, False
        try:
            fh = open(path, errors="replace")
        except OSError:
            continue
        with fh:
            for line in fh:
                m = _ASM_OBJ.match(line)
                if m:
                    cur, first = m.group(1), True
                    continue
                if cur is None:
                    continue
                if first and line.startswith("\t.4byte"):
                    first = False
                    d, v = _DECL_CLASS.match(cur), _ASM_VTBL.match(line)
                    if d and v:
                        checked += 1
                        if v.group(2) != d.group(1):
                            declared = f"??_7{d.group(1)}@@6B@"
                            verdict = (map_verdict(idx, declared, v.group(1))
                                       if idx else "MAP_NOT_CONSULTED")
                            row = (str(path), cur, v.group(1), verdict)
                            (forgiven if verdict == "FOLD" else bad).append(row)
                elif line.startswith(".endobj"):
                    cur = None
    return checked, bad, forgiven


def map_verdict(idx, target: str, base: str) -> str:
    ta, ba = idx.get(target), idx.get(base)
    if not ta and not ba:
        return "NEITHER_IN_MAP"
    if not ta:
        return "TARGET_NOT_IN_MAP"
    if not ba:
        return "BASE_NOT_IN_MAP"
    return "FOLD" if set(ta) & set(ba) else "DIFFERENT_ADDRESS"


def gen_report(cli: str, project: Path, extra: list[str], out: Path) -> None:
    cmd = [cli, "report", "generate", "-p", str(project), "-o", str(out)] + extra
    subprocess.run(cmd, check=True, capture_output=True, text=True)


def index_report(path: Path) -> dict:
    rep = json.loads(Path(path).read_text())
    out = {}
    for u in rep["units"]:
        for f in (u.get("functions") or []):
            out[(u["name"], f["name"])] = (
                float(f.get("fuzzy_match_percent") or 0.0),
                int(f.get("size") or 0),
            )
    return out


def charged_pairs(cli, project, ruler, population, timeout):
    """(unit, sym) -> {'pairs': [...], 'other': n} for the population rows."""
    by_unit = collections.defaultdict(list)
    for u, s in population:
        by_unit[u].append(s)
    out = {}
    for unit, syms in sorted(by_unit.items()):
        cmd = [cli, "diff", "-p", str(project), "-u", unit, "--batch",
               "-f", "json", "-o", "-", "--include-instructions"] + ruler.args
        try:
            r = subprocess.run(cmd, capture_output=True, text=True,
                               input="\n".join(syms) + "\n", timeout=timeout)
        except subprocess.TimeoutExpired:
            print(f"  ! timeout on {unit}", file=sys.stderr)
            continue
        txt = r.stdout.strip()
        if not txt:
            print(f"  ! empty diff for {unit}", file=sys.stderr)
            continue
        try:
            j = json.loads(txt)
            recs = j if isinstance(j, list) else [j]
        except json.JSONDecodeError:
            recs = [json.loads(x) for x in txt.splitlines() if x.strip()]
        for rec in recs:
            sym = rec.get("symbol") or rec.get("name") or ""
            if sym not in syms:
                continue
            pairs, other = set(), 0
            for ins in rec.get("instructions", []) or []:
                mt = ins.get("match_type")
                if mt == "equal":
                    continue
                t, b = ins.get("target") or {}, ins.get("base") or {}
                kinds, sp = set(), None
                for x, y in zip(t.get("typed_args", []) or [],
                                b.get("typed_args", []) or []):
                    if x.get("value") != y.get("value"):
                        kinds.add(x.get("type"))
                        if x.get("type") == "Symbol":
                            sp = (x.get("value"), y.get("value"))
                if mt == "diff_arg" and kinds == {"Symbol"} and sp:
                    pairs.add(sp)
                else:
                    other += 1
            out[(unit, sym)] = {"pairs": sorted(pairs, key=lambda p: (str(p[0]), str(p[1]))),
                                "other": other}
    return out


# ── Negative control ─────────────────────────────────────────────────────────
#: The charge the `createFilter` bug produces, recorded verbatim from the tree
#: on 2026-08-19 while the bug was re-applied.  --selftest replays it through
#: the same adjudication path a live run uses; if a future edit makes the tool
#: classify this away, the selftest fails.
_CREATEFILTER_FIXTURE = (
    "?createFilter@@YAXW4FilterType@@MMMMPAUFilterCoeff@@@Z",
    "createFilter",
)


def _selftest() -> int:
    ok = True

    def check(label, cond, detail=""):
        nonlocal ok
        print(f"  [{'PASS' if cond else 'FAIL'}] {label}"
              f"{(' - ' + detail) if detail else ''}")
        ok = ok and cond

    t, b = _CREATEFILTER_FIXTURE
    check("createFilter pair is NOT exempted", classify_exempt(t, b) is None,
          f"bucket={classify_exempt(t, b)}")

    # Exemptions must fire on their own shapes, or they are decoration.
    check("anon-ns placeholder is exempted",
          classify_exempt("?gXboxDeadzone@?A@@3MA",
                          "?gXboxDeadzone@?A0xf503845b@@3MA")
          == "anon_ns_placeholder")
    # Both MSVC scope encodings, taken verbatim from this tree's charges.
    check("scope counter (digit vs letter-run) is exempted",
          classify_exempt(
              "?msg@?9??OnMsg@PreloadPanel@@AAA?AVDataNode@@"
              "ABVUITransitionCompleteMsg@@@Z@4VMessage@@A",
              "?msg@?M@??OnMsg@PreloadPanel@@AAA?AVDataNode@@"
              "ABVUITransitionCompleteMsg@@@Z@4VMessage@@A")
          == "scope_counter")
    check("scope counter (letter-run vs letter-run) is exempted",
          classify_exempt(
              "?_s@?HP@??SyncProperty@CamShot@@UAA_NAAVDataNode@@"
              "PAVDataArray@@HW4PropOp@@@Z@4VSymbol@@A",
              "?_s@?JD@??SyncProperty@CamShot@@UAA_NAAVDataNode@@"
              "PAVDataArray@@HW4PropOp@@@Z@4VSymbol@@A")
          == "scope_counter")
    # ...but it must NOT swallow a pair where the VARIABLE differs, only the
    # scope counter.  A scanner that ate this would hide ThreeDSound::Load.
    check("different local-static names are NOT exempted",
          classify_exempt(
              "?gRevs@?1??Load@ThreeDSound@@UAAXAAVBinStream@@@Z@4QBGB",
              "gAltRev") is None)
    check("same scope, different variable is NOT exempted",
          classify_exempt("?omg@?CM@??Foo@@QAA@Z@4V5@A",
                          "?wtf@?CM@??Foo@@QAA@Z@4V5@A") is None)
    check("dtk synthetic is exempted",
          classify_exempt("merged_SetObjConcrete", "?SetObj@@QAA@Z")
          == "dtk_synthetic_fold_name")

    # A real wrong-callee pair with two different spellings must NOT be exempt.
    check("MakeString<E> vs MakeString<D> is NOT exempted",
          classify_exempt("??$MakeString@E@@YAPBDPBDABE@Z",
                          "??$MakeString@D@@YAPBDPBDABD@Z") is None)
    check("Task vs Hmx::Object base ctor is NOT exempted",
          classify_exempt("??0Object@Hmx@@QAA@XZ", "??0Task@@QAA@XZ") is None)

    # ── global-vtable fold forgiveness, sabotaged before it is trusted ───────
    # `vtable_type_disagreements` now forgives a name pair whose two names sit
    # at ONE map address (ICF).  A forgiveness is only safe if it can be shown
    # NOT to fire on the bug it might swallow, so every row below is graded and
    # the real 2026-08-19 RndShader defect is included as the thing that must
    # still report.  Addresses are the live ones from `ham_xbox_r.map`.
    _vt_idx = {
        # six names, one address -- the live g_csOverrideRestore row
        "??_7CCriticalSection@@6B@":      ["82164c18"],
        "??_7RefCount@Nui@@6B@":          ["82164c18"],
        # distinct addresses -- the RndShader run the check was built to catch
        "??_7RndShaderStandard@@6B@":     ["8209ca88"],
        "??_7RndShaderMultimesh@@6B@":    ["8209ca74"],
        "??_7RndShaderFur@@6B@":          ["8209cb14"],
        "??_7RndShaderSyncTrack@@6B@":    ["8209cb28"],
    }
    check("ICF fold (two names, one address) is FOLD",
          map_verdict(_vt_idx, "??_7CCriticalSection@@6B@",
                      "??_7RefCount@Nui@@6B@") == "FOLD")
    check("the real RndShader off-by-one-slot bug still reports",
          map_verdict(_vt_idx, "??_7RndShaderStandard@@6B@",
                      "??_7RndShaderMultimesh@@6B@") == "DIFFERENT_ADDRESS")
    check("an adjacent-slot RndShader pair still reports",
          map_verdict(_vt_idx, "??_7RndShaderFur@@6B@",
                      "??_7RndShaderSyncTrack@@6B@") == "DIFFERENT_ADDRESS")
    check("a name absent from the map is NOT forgiven",
          map_verdict(_vt_idx, "??_7RndShaderFur@@6B@",
                      "??_7TotallyMadeUpClass@@6B@") == "BASE_NOT_IN_MAP")
    # Vacuity control: with no map, nothing may be forgiven silently.
    check("no map index => MAP_NOT_CONSULTED, never FOLD",
          map_verdict({}, "??_7A@@6B@", "??_7B@@6B@") == "NEITHER_IN_MAP")

    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--project", default=".")
    ap.add_argument("--map", default=None,
                    help="MSVC linker map (default: orig/<title>/ham_xbox_r.map)")
    ap.add_argument("--objdiff-cli", default=None)
    ap.add_argument("--config", default=None,
                    help="dtk symbols.txt (default: config/<title>/symbols.txt)")
    ap.add_argument("--json-out")
    ap.add_argument("--no-exempt", action="store_true",
                    help="print the raw population with no exemption buckets")
    ap.add_argument("--limit", type=int, default=40,
                    help="print at most N standing rows (counts are always "
                         "complete; --limit 0 for all)")
    ap.add_argument("--include-synthetic", action="store_true",
                    help="also list fn_*/lbl_* rows (EH funclets etc.)")
    ap.add_argument("--timeout", type=int, default=900)
    ap.add_argument("--fail-on", type=int, default=None,
                    help="exit 1 if the standing (non-exempt) row count exceeds N")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args(argv)

    if args.selftest:
        print("# reloc_name_gate selftest (negative control)")
        return _selftest()

    project = Path(args.project).resolve()
    cli = args.objdiff_cli or str(project / "bin" / "objdiff-cli")
    graded = ruler_mod.resolve_ruler(project, ruler_mod.RULER_GRADED)
    blind = ruler_mod.resolve_ruler(project, ruler_mod.RULER_NONE)
    print(graded.banner())
    print(f"blind leg: functionRelocDiffs={blind.reloc_mode}")

    mapfile = args.map
    if not mapfile:
        cand = sorted(project.glob("orig/*/ham_xbox_r.map"))
        if not cand:
            raise SystemExit("no orig/<title>/ham_xbox_r.map; pass --map")
        mapfile = str(cand[0])

    import tempfile
    with tempfile.TemporaryDirectory() as td:
        none_json = Path(td) / "report_none.json"
        gen_report(cli, project, blind.args, none_json)
        N = index_report(none_json)
    graded_report = ruler_mod._find_report_json(project)
    if graded_report is None:
        raise SystemExit("no build/<title>/report.json -- run ninja first")
    G = index_report(graded_report)

    scanned = len(N)
    # ── The population ────────────────────────────────────────────────────────
    # ANY row the graded ruler scores below the relocation-blind ruler carries a
    # relocation-name charge. Nothing else can move between the two legs -- they
    # differ in exactly one config key.
    #
    # ⚠ This used to read `N == 100 and G < 100`, i.e. only rows that were
    #   otherwise PERFECT. That is a defensible reporting slice and an indefensible
    #   population: it silently dropped every wrong-callee bug sitting on a row
    #   that also had instruction mismatches. The end-to-end negative control
    #   caught it -- re-applying the `createFilter` bug produced NO output,
    #   because EQEffect::SetParameter is 84.7% blind and the filter threw it
    #   away. 42 rows survived that filter; the honest population is 508.
    pop = [k for k in N if G.get(k, (0.0, 0))[0] < N[k][0] - 1e-9]
    synthetic = [k for k in pop if k[1].startswith(("fn_", "lbl_"))]
    if not args.include_synthetic:
        pop = [k for k in pop if k not in set(synthetic)]
    pop_bytes = sum(N[k][1] for k in pop)
    # Rows whose ONLY defect is the name: closing it crosses the row and pays
    # its full size, because matched_code is all-or-nothing per row.
    crossers = [k for k in pop if N[k][0] >= 100.0]

    print(f"\nrows scanned (denominator)          : {scanned}")
    print(f"rows graded < blind (ANY name charge): {len(pop)}  ({pop_bytes} B)")
    print(f"  of which otherwise PERFECT         : {len(crossers)}  "
          f"({sum(N[k][1] for k in crossers)} B)  <- closing the name crosses the row")
    if not args.include_synthetic:
        print(f"  fn_*/lbl_* funclet rows excluded    : {len(synthetic)}  "
              "(--include-synthetic to keep)")

    idx = load_map_index(mapfile)
    print(f"linker map                          : {mapfile} ({len(idx)} names)")

    # ── Instrument check, run before any row is adjudicated ───────────────────
    # A config symbol at the wrong address stamps a real name onto the wrong
    # block, and every row that references that block then reads as a
    # wrong-callee bug. Check the instrument before believing its output.
    cfgfile = args.config or next(
        (str(p) for p in sorted(project.glob("config/*/symbols.txt"))), None)
    if cfgfile:
        cfg = load_config_symbols(cfgfile)
        shared, bad = config_map_disagreements(cfg, idx)
        print(f"config symbols vs map               : {len(cfg)} config, "
              f"{shared} present in both, {len(bad)} DISAGREE")
        for name, caddr, maddrs in bad:
            print(f"  !! config 0x{caddr:08X}  map {maddrs}  {name}")
        if bad and args.fail_on is not None:
            print("  (a config/map disagreement is an INSTRUMENT defect: fix "
                  "the address before triaging any row that names this symbol)")
    else:
        print("config symbols vs map               : no config/*/symbols.txt "
              "found -- INSTRUMENT UNCHECKED")

    asm_root = next((p for p in sorted(project.glob("build/*/asm"))), None)
    if asm_root is not None:
        nchecked, vbad, vfold = vtable_type_disagreements(asm_root, idx)
        # Every bucket's count is printed, forgiven ones included: a silent
        # exemption is how a suppression turns into a blind spot.
        print(f"global vtable vs declared type      : {nchecked} checked, "
              f"{len(vbad)} DISAGREE, {len(vfold)} forgiven as ICF fold "
              f"(both names at one map address)")
        for path, sym, vt, verdict in vbad:
            print(f"  !! {sym}\n       holds {vt}  [{verdict}]  ({path})")
        for path, sym, vt, _v in vfold:
            print(f"  ~~ {sym}\n       holds {vt}  [FOLD -- same address, benign]  ({path})")
    else:
        print("global vtable vs declared type      : no build/*/asm -- "
              "INSTRUMENT UNCHECKED")

    detail = charged_pairs(cli, project, graded, pop, args.timeout)

    buckets = collections.Counter()
    rows = []
    for key in sorted(pop, key=lambda k: -N[k][1]):
        d = detail.get(key, {"pairs": [], "other": 0})
        kept, exempted = [], collections.Counter()
        for t, b in d["pairs"]:
            bucket = None if args.no_exempt else classify_exempt(t, b)
            if bucket:
                exempted[bucket] += 1
                buckets[bucket] += 1
                continue
            ts, bs = (t if isinstance(t, str) else repr(t),
                      b if isinstance(b, str) else repr(b))
            kept.append({"target": ts, "base": bs,
                         "verdict": map_verdict(idx, ts, bs),
                         "target_addrs": idx.get(ts, []),
                         "base_addrs": idx.get(bs, [])})
        if not d["pairs"]:
            buckets["no_symbol_pair_extracted"] += 1
        rows.append({"unit": key[0], "symbol": key[1], "size": N[key][1],
                     "graded_fuzzy": G[key][0], "blind_fuzzy": N[key][0],
                     "other_charges": d["other"], "crosses": N[key][0] >= 100.0,
                     "pairs": kept, "exempted": dict(exempted)})

    standing = [r for r in rows if r["pairs"]]
    fold = [r for r in standing
            if all(p["verdict"] == "FOLD" for p in r["pairs"])]
    print(f"\nexemption buckets (counted, never hidden):")
    for k, v in sorted(buckets.items()):
        print(f"  {k:<28} {v:>5} pairs")
    if not buckets:
        print("  (none)")
    print(f"\nrows still standing after exemptions : {len(standing)}"
          f"  ({sum(r['size'] for r in standing)} B)")
    print(f"  of which every pair is a proven FOLD: {len(fold)}")

    vcount = collections.Counter(p["verdict"] for r in standing for p in r["pairs"])
    print("\npair verdicts (standing rows):")
    for k, v in vcount.most_common():
        print(f"  {k:<20} {v:>5}")

    listed = [r for r in standing if r not in fold]
    shown = listed if not args.limit else listed[:args.limit]
    print("\n" + "=" * 78)
    print(f"listing {len(shown)} of {len(listed)} non-FOLD standing rows"
          + ("" if not args.limit else f" (--limit {args.limit}; "
             "the COUNT above is the finding, the listing is a convenience)"))
    for r in shown:
        print(f"\n{r['size']:>7} B  blind={r['blind_fuzzy']:.4f} "
              f"graded={r['graded_fuzzy']:.4f}  "
              f"other_charges={r['other_charges']}"
              f"{'  [CROSSES: name is the only defect]' if r['crosses'] else ''}")
        print(f"  {r['unit']} :: {r['symbol']}")
        for p in r["pairs"]:
            print(f"    [{p['verdict']}]")
            print(f"      TARGET {p['target']}  @{p['target_addrs'] or '-'}")
            print(f"      OURS   {p['base']}  @{p['base_addrs'] or '-'}")
        if r["exempted"]:
            print(f"    (also exempted: {r['exempted']})")

    if args.json_out:
        Path(args.json_out).write_text(json.dumps(rows, indent=1))
        print(f"\nwrote {args.json_out}")

    if args.fail_on is not None and len(standing) > args.fail_on:
        print(f"\nFAIL: {len(standing)} standing rows > --fail-on {args.fail_on}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
