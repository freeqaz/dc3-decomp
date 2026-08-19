#!/usr/bin/env python3
"""
reloc_strict_classify.py — Strict-reloc re-certification classifier (roadmap 0.5/0.6).

Quantifies the ONLY uncounted DC3 measurement risk: wrong-call-target / wrong-data-symbol
false-100%s that the lenient reloc mode (functionRelocDiffs=None) feeding report.json hides.

Inputs (default, all relative to a primed dc3 worktree build dir):
  - build/373307D9/report.json         (lenient, None mode — the canonical metric)
  - build/373307D9/report_strict.json  (NameOnly mode — name+section match, addend ignored)

A function in the CANDIDATE set is 100% in report.json but <100% in report_strict.json.
For each candidate we re-diff under NameOnly (which forgives benign build-address/addend
differences but NOT a wrong symbol NAME), walk the per-instruction symbol-argument diffs,
and classify every reloc-target mismatch:

  genuine_wrong_target : target & base point at different *code/data* symbols of a kind that
                         changes behavior (different callee, different vtable/global/object).
                         THIS is the real false-100% population.
  benign_string_path   : both symbols are string-constant pools (??_C@...) that differ only
                         by an embedded build path (e.g. '/' vs '\\' separators, __FILE__),
                         a cosmetic compiler artifact, not a behavioral diff.
  missing_reloc        : one side has a reloc/symbol, the other a plain immediate (None
                         relaxation forgave it). Not a wrong target.
  non_reloc_codegen    : the strict <100 has no symbol-arg diff at all (register/opcode/
                         branch differences); not a reloc question.

The headline deliverable is THE genuine_wrong_target count (authorable only).

Usage:
  scripts/analysis/reloc_strict_classify.py \
      --lenient build/373307D9/report.json \
      --strict  build/373307D9/report_strict.json \
      --objdiff /home/free/code/milohax/wt-objdiff-strict/target/release/objdiff-cli \
      --project . --out docs/.../14-strict-recert-results.data.json [--jobs 24] [--limit N]

Read-only: never writes to decomp.db; only writes the --out JSON (and prints a summary table).
"""
import argparse
import collections
import json
import os
import subprocess
import sys
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

_REPO = str(Path(__file__).resolve().parent.parent.parent)
if _REPO not in sys.path:
    sys.path.insert(0, _REPO)
from scripts.analysis.coverage import CoverageReport, add_coverage_args  # noqa: E402

# Units that are not source-authorable (vendor/SDK). Kept in sync with
# scripts/authorable.py / SDK_UNIT_PREFIXES; duplicated minimally here so the
# classifier is standalone. Authorable filtering is applied to the HEADLINE only.
SDK_UNIT_PREFIXES = ("default/xdk/", "default/lib/")


def is_authorable(unit_name):
    return not any(unit_name.startswith(p) for p in SDK_UNIT_PREFIXES)


def load_report_fn_percents(path):
    """(unit, fn) -> fuzzy_match_percent for every function in a report."""
    with open(path) as fh:
        doc = json.load(fh)
    out = {}
    sizes = {}
    for unit in doc.get("units", []):
        un = unit["name"]
        for fn in unit.get("functions", []):
            key = (un, fn["name"])
            out[key] = fn.get("fuzzy_match_percent", 0.0)
            sizes[key] = int(fn.get("size", "0"))
    return out, sizes


def find_candidates(lenient, strict, cov=None):
    """Functions 100% lenient (None) but <100% strict (NameOnly).

    `sp is not None` is a SILENT DROP: a key scored in the lenient report but
    absent from (or unscored in) the strict one vanishes here with no count, and
    it is exactly the interesting population -- a row that the strict pass never
    scored is not a row the strict pass cleared.  Counting it does not change
    which candidates come back; it changes whether the denominator is knowable.
    """
    cands = []
    for key, lp in lenient.items():
        sp = strict.get(key)
        if lp < 100.0:
            if cov is not None:
                cov.drop("not-100-on-lenient",
                         note="only lenient-100 rows can be false-100s")
            continue
        if sp is None:
            if cov is not None:
                cov.drop("no-strict-score",
                         note="scored lenient-100 but the strict report has no "
                              "percent for it -- NOT the same as strict-clean")
            continue
        if sp >= 100.0:
            if cov is not None:
                cov.drop("strict-also-100", note="agrees under both rulers")
            continue
        if cov is not None:
            cov.examine()
        cands.append(key)
    return cands


import re

# A '?A0x<hex>@' anonymous-namespace disambiguator: the hash is per-translation-unit
# build noise (same anon-namespace symbol, different hash on target vs base). Benign.
_ANON_NS = re.compile(r"\?A0x[0-9a-fA-F]+@")
# MSVC local-scope disambiguators inside a function: '?BG@'/'?DA@'/'?DC@' (nested-scope
# index, letter ?A.. ?Z..) and '?$Sn@' (static-local counter). Different counters = same
# logical local, build noise.
_LOCAL_SCOPE = re.compile(r"\?[A-Z][0-9A-P]+@")
_STATIC_LOCAL = re.compile(r"\?\$S[0-9A-P]+@")
# Const-qualifier mangling tail on a data symbol address: '...@3PBDB' (const) vs '@3PADA'
# (non-const) — same global, different cv-qualifier encoding. Collapse the trailing
# data-type/cv code so anon-ns globals that differ only in cv compare equal.
_DATA_CV_TAIL = re.compile(r"@3[A-Z]+[A-Z]$")
# Target-side unresolved address label / funclet / ICF-merged stub that jeff's split could
# not give the real source name: 'lbl_<hex>', 'fn_<hex>' (MSVC EH funclet, doc 02 F5),
# 'merged_<...>' (Identical COMDAT Folding). Not an authorable wrong-target.
_ADDR_LABEL = re.compile(
    r"^(lbl_|loc_|sub_|data_|off_|byte_|word_|dword_|unk_|fn_)[0-9a-fA-F]+$")
# Switch jump-table: target-split names it 'jumptable_<hex>', the base/recompile names it
# '$T<n>' (MSVC switch table). Same construct, different naming convention. Benign.
_JUMPTABLE_T = re.compile(r"^jumptable_[0-9a-fA-F]+$")
_JUMPTABLE_DOLLAR = re.compile(r"^\$T[0-9]+$")
# MSVC EH state/unwind record the splitter labels 'except_record_<hex>' (and similar EH
# scaffolding). A name diff against any non-EH symbol is a target-split EH artifact.
_EH_RECORD = re.compile(r"^(except_record_|__ehhandler|__unwind|\$unwind\$|\$chain\$)")


def is_string_const(sym):
    """MSVC string-literal pool symbol, e.g. ??_C@_0CO@HAOEJAIE@e?3?2lazer...@"""
    return sym.startswith("??_C@")


def is_icf_stub(sym):
    """An ICF-fold placeholder name the splitter emits for trivially-identical functions
    (e.g. lots of one-instruction 'blr' bodies fold to one address). The source-true name
    is on the other side; a name diff here is an ICF artifact, not a wrong target."""
    return (sym.startswith("merged_")
            or sym.startswith("OnlyReturn")  # OnlyReturns / OnlyReturnsN
            or sym.startswith("Returns")     # ReturnsTrue/ReturnsFalse/Returns0 folds
            or sym.startswith("Return0") or sym.startswith("Return1"))


def is_addr_label(sym):
    # 'merged_<name>' = Identical COMDAT Folding stub (the linker merged identical machine
    # code; the source-true name is on the other side). Also the hex labels / funclets.
    return bool(_ADDR_LABEL.match(sym)) or is_icf_stub(sym)


def is_runtime_helper(sym):
    """Compiler runtime save/restore helpers; ICF picks a variant by #regs, build noise."""
    base = sym.split("_")[0] if sym.startswith("__") else sym
    return sym.startswith(("__savegprlr", "__restgprlr", "__savefpr", "__restfpr",
                           "__savevmx", "__restvmx", "__save", "__rest")) or base in (
        "__savegprlr", "__restgprlr")


def strip_string_hash(sym):
    """Drop the two hash segments of a ??_C@ symbol, keeping the readable string body.

    ??_C@_<len>@<hash>@<body>@  ->  <body> with path separators normalized.
    Different __FILE__ build paths (/ vs \\, encoded ?2 vs ?1) hash differently but the
    readable body is identical after separator normalization.
    """
    if not is_string_const(sym):
        return sym
    parts = sym.split("@")
    # parts: ['??_C', '_<len>', '<hash>', '<body...>', ''] (body may contain '@' rarely)
    body = "@".join(parts[3:]) if len(parts) > 3 else sym
    # ?2 == '/', ?1 == '\\' in MSVC name mangling; normalize both to '/'
    return body.replace("?1", "?2")


def normalize_build_noise(sym):
    """Collapse per-build-noise hashes so two logically-identical symbols compare equal."""
    s = _ANON_NS.sub("?A0xNN@", sym)
    s = _STATIC_LOCAL.sub("?$Sn@", s)
    s = _LOCAL_SCOPE.sub("?Sc@", s)
    # Normalize a trailing data cv-qualifier code (e.g. '@3PBDB' const vs '@3PADA').
    s = _DATA_CV_TAIL.sub("@3CV", s)
    # '_E' (scalar deleting dtor) vs '_G' (vector deleting dtor) thunk pair on the same
    # class are an ICF/thunk artifact, not a different target.
    if s.startswith("??_E") or s.startswith("??_G"):
        s = "??_D" + s[4:]
    return s


# objdiff symbol-arg "value" kinds we treat as code/data targets when the names differ.
def classify_pair(t_sym, b_sym):
    """Classify a single (target_symbol, base_symbol) reloc-target name mismatch.

    Returns a class string, or None if not actually a mismatch.
    """
    # Coerce non-string operand values (numbers, nested dicts) to a stable str or None so
    # one side carrying a symbol and the other a literal counts as a missing/changed reloc.
    if t_sym is not None and not isinstance(t_sym, str):
        t_sym = str(t_sym)
    if b_sym is not None and not isinstance(b_sym, str):
        b_sym = str(b_sym)
    if t_sym == b_sym:
        return None
    if t_sym is None or b_sym is None:
        return "missing_reloc"

    # Target-side unresolved address label vs a real base symbol: jeff split artifact, not
    # an authorable wrong-target. (Also the reverse, defensively.)
    if is_addr_label(t_sym) or is_addr_label(b_sym):
        return "target_split_label"

    # Switch jump-table named differently on each side (jumptable_<hex> vs $T<n>): benign.
    if (_JUMPTABLE_T.match(t_sym) or _JUMPTABLE_DOLLAR.match(t_sym)) and \
       (_JUMPTABLE_T.match(b_sym) or _JUMPTABLE_DOLLAR.match(b_sym)):
        return "benign_build_artifact"

    # EH unwind/state record on either side: target-split exception-scaffolding artifact.
    if _EH_RECORD.match(t_sym) or _EH_RECORD.match(b_sym):
        return "target_split_label"

    # Compiler runtime save/restore helper variants (ICF chooses by #regs): build noise.
    if is_runtime_helper(t_sym) and is_runtime_helper(b_sym):
        return "benign_build_artifact"

    # String constants: equal-after-path-normalization => benign __FILE__ build path diff.
    if is_string_const(t_sym) and is_string_const(b_sym):
        if strip_string_hash(t_sym) == strip_string_hash(b_sym):
            return "benign_string_path"
        return "genuine_wrong_target"

    # Anon-namespace / local-scope / static-local hash noise: equal after collapsing.
    if normalize_build_noise(t_sym) == normalize_build_noise(b_sym):
        return "benign_build_artifact"

    # Template array-size equivalence (the fork already neutralizes some of these in the
    # scorer): MakeString-style instantiations differing only in encoded array sizes.
    if _array_size_equivalent(t_sym, b_sym):
        return "benign_build_artifact"

    # Same method/operator name but different template TYPE parameters, e.g.
    # ?SetObjConcrete@?$ObjRefConcrete@VRndDrawable@@... vs ...@VRndMesh@@...
    # The machine code is usually identical (and ICF-folded to one address); this is a
    # template-instantiation variant, NOT a behavioral wrong-target. Reported separately
    # so the alarming "different function entirely" set stays clean.
    if _same_method_token(t_sym, b_sym):
        return "template_instantiation_variant"

    # Different method/function NAME entirely: the genuine wrong-call-target / wrong-data
    # -symbol population the lenient (None) mode hides. THE headline number.
    return "genuine_wrong_target"


_METHOD_TOKEN = re.compile(r"\?\??[\$0-9A-Za-z_]+")


def _same_method_token(a, b):
    """True if a and b share the same leading mangled method/operator token (so they are
    the same member differing only in template/type parameters or signature)."""
    ma = _METHOD_TOKEN.match(a)
    mb = _METHOD_TOKEN.match(b)
    if not ma or not mb:
        return False
    ta, tb = ma.group(0), mb.group(0)
    if ta != tb:
        return False
    # Guard: require the token to be a real method/template head (not a bare operator like
    # '??1' which every destructor shares — those ARE different functions). A shared bare
    # special token <= 3 chars is too weak; require additional class-name agreement.
    if len(ta) <= 3:
        # e.g. '??1' (dtor), '??0' (ctor): also require the first class name to match.
        ca = a[len(ta):].split("@@")[0].split("@")[0]
        cb = b[len(tb):].split("@@")[0].split("@")[0]
        return ca == cb and ca != ""
    return True


def _array_size_equivalent(a, b):
    """Heuristic mirror of objdiff's normalize_mangled_array_sizes: MakeString/template
    instantiations that differ only in $$BY0<size>@ array-extent codes produce identical
    code (arrays decay to pointers). Collapse the $$BY0..@ extents and compare."""
    if "$$BY0" not in a or "$$BY0" not in b:
        return False
    na = re.sub(r"\$\$BY0[0-9A-P]*@", "$$BY0X@", a)
    nb = re.sub(r"\$\$BY0[0-9A-P]*@", "$$BY0X@", b)
    return na == nb


def diff_one(objdiff, project, unit, symbol):
    """Run a single-symbol NameOnly diff with instructions; return (worst_class, detail)."""
    cmd = [
        objdiff, "diff", "--project", project, "-u", unit, symbol,
        "-c", "functionRelocDiffs=name_only",
        "--include-instructions", "-f", "json", "-o", "-",
    ]
    try:
        res = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        return (unit, symbol, "error", "timeout", [])
    if res.returncode != 0 or not res.stdout.strip():
        return (unit, symbol, "error", (res.stderr or "no-output")[:200], [])
    try:
        d = json.loads(res.stdout)
    except json.JSONDecodeError:
        return (unit, symbol, "error", "json-decode", [])

    classes = collections.Counter()
    examples = []
    for ins in d.get("instructions", []):
        mt = ins.get("match_type")
        if not mt or mt == "equal":
            continue
        had_symbol_arg = False
        for arg in ins.get("diff_breakdown", {}).get("arguments", []):
            if arg.get("arg_type") != "symbol":
                continue
            had_symbol_arg = True
            t = (arg.get("target") or {}).get("value")
            b = (arg.get("base") or {}).get("value")
            cls = classify_pair(t, b)
            if cls is None:
                continue
            classes[cls] += 1
            if cls in ("genuine_wrong_target", "template_instantiation_variant") \
                    and len(examples) < 4:
                examples.append({"class": cls, "target": t, "base": b})
        # A reloc present on one side only shows as a symbol on one side and a value/none
        # on the other -> the arg_type may be 'symbol' on one side only or the instruction
        # is match_type replace/insert/delete. Treat replace with no symbol-arg as codegen.
        if not had_symbol_arg and mt in ("diff_op", "replace", "insert", "delete"):
            classes["non_reloc_codegen"] += 1

    if not classes:
        return (unit, symbol, "non_reloc_codegen", "", examples)
    # Worst (most-severe) class wins for the function-level label.
    order = ["genuine_wrong_target", "template_instantiation_variant", "missing_reloc",
             "target_split_label", "benign_string_path", "benign_build_artifact",
             "non_reloc_codegen"]
    for c in order:
        if classes.get(c):
            return (unit, symbol, c, dict(classes), examples)
    return (unit, symbol, "non_reloc_codegen", dict(classes), examples)


def _worker(args):
    return diff_one(*args)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--lenient", default="build/373307D9/report.json")
    ap.add_argument("--strict", default="build/373307D9/report_strict.json")
    ap.add_argument("--objdiff",
                    default="/home/free/code/milohax/wt-objdiff-strict/target/release/objdiff-cli")
    ap.add_argument("--project", default=".")
    ap.add_argument("--out", default="reloc_strict_classify.json")
    ap.add_argument("--jobs", type=int, default=min(32, (os.cpu_count() or 8)))
    ap.add_argument("--limit", type=int, default=0, help="cap candidates (debug)")
    ap.add_argument("--authorable-only", action="store_true",
                    help="restrict the candidate diffing to authorable units")
    add_coverage_args(ap)
    args = ap.parse_args()

    lenient, lsizes = load_report_fn_percents(args.lenient)
    strict, _ = load_report_fn_percents(args.strict)

    cov = CoverageReport("reloc_strict_classify", args=args)
    cov.universe(len(lenient), "function rows in the LENIENT report")
    cands = find_candidates(lenient, strict, cov=cov)
    if args.authorable_only:
        before_auth = len(cands)
        cands = [c for c in cands if is_authorable(c[0])]
        cov.drop("not-authorable", before_auth - len(cands),
                 note="--authorable-only")
    cands.sort()
    n_cands = len(cands)
    cov.extra("candidates_before_limit", n_cands)
    if args.limit and n_cands > args.limit:
        cands = cands[: args.limit]
        cov.cap("--limit", args.limit, before=n_cands, after=len(cands),
                note="never diffed; --limit is a debug flag")
        # The candidate count below is this tool's headline. Print the cut BEFORE
        # it, or a debug run's sample gets quoted as the population size.
        print(f"[reloc-strict] !! TRUNCATED by --limit={args.limit}: "
              f"{n_cands - args.limit} of {n_cands} candidates were NEVER diffed. "
              f"This run is a SAMPLE, not a census.", file=sys.stderr)

    print(f"[reloc-strict] candidates (lenient-100 & strict-NameOnly-<100): "
          f"{len(cands)} of {n_cands}", file=sys.stderr)
    print(f"[reloc-strict] diffing with {args.jobs} workers via {args.objdiff}",
          file=sys.stderr)

    work = [(args.objdiff, args.project, u, s) for (u, s) in cands]
    results = []
    done = 0
    with ProcessPoolExecutor(max_workers=args.jobs) as ex:
        futs = [ex.submit(_worker, w) for w in work]
        for fut in as_completed(futs):
            results.append(fut.result())
            done += 1
            if done % 500 == 0:
                print(f"[reloc-strict] {done}/{len(work)} diffed", file=sys.stderr)

    # Aggregate.
    by_class = collections.Counter()
    by_class_auth = collections.Counter()
    bytes_by_class = collections.Counter()
    genuine = []
    template_variants = []
    by_unit_genuine = collections.Counter()
    errors = []
    for (unit, sym, label, detail, examples) in results:
        sz = lsizes.get((unit, sym), 0)
        by_class[label] += 1
        bytes_by_class[label] += sz
        auth = is_authorable(unit)
        if auth:
            by_class_auth[label] += 1
        if label == "error":
            errors.append({"unit": unit, "symbol": sym, "detail": detail})
        rec = {"unit": unit, "symbol": sym, "size": sz, "authorable": auth,
               "strict_percent": strict.get((unit, sym)),
               "detail": detail, "examples": examples}
        if label == "genuine_wrong_target":
            genuine.append(rec)
            if auth:
                by_unit_genuine[unit] += 1
        elif label == "template_instantiation_variant":
            template_variants.append(rec)

    genuine.sort(key=lambda r: (not r["authorable"], -r["size"]))
    template_variants.sort(key=lambda r: (not r["authorable"], -r["size"]))
    genuine_auth = [g for g in genuine if g["authorable"]]

    out = {
        "summary": {
            # PRE-truncation.  This used to be len(cands), i.e. the count
            # AFTER --limit, so a debug sample serialised as the population
            # size.  The exculpatory number has to travel with the verdict, not
            # only on stderr where a redirect drops it.
            "candidates_total": n_cands,
            "candidates_diffed": len(cands),
            "truncated": bool(args.limit and n_cands > args.limit),
            "by_class": dict(by_class),
            "by_class_authorable": dict(by_class_auth),
            "bytes_by_class": dict(bytes_by_class),
            "genuine_wrong_target_total": by_class["genuine_wrong_target"],
            "genuine_wrong_target_authorable": by_class_auth["genuine_wrong_target"],
            "template_instantiation_variant_total": by_class["template_instantiation_variant"],
            "errors": len(errors),
        },
        "top_units_genuine_authorable": by_unit_genuine.most_common(20),
        "genuine_wrong_target_authorable": genuine_auth,
        "template_instantiation_variant_sample": template_variants[:60],
        "errors_sample": errors[:20],
    }
    with open(args.out, "w") as fh:
        json.dump(out, fh, indent=1)

    # Summary table to stdout.
    print("\n=== STRICT-RELOC RE-CERTIFICATION (NameOnly) ===")
    if len(cands) != n_cands:
        print(f"!! TRUNCATED by --limit={args.limit}: this table describes "
              f"{len(cands)} of {n_cands} candidates. It is a SAMPLE.")
    print(f"candidates (lenient-100 & strict-<100): {len(cands)} of {n_cands}")
    print(f"{'class':<32} {'all':>8} {'authorable':>12} {'bytes':>12}")
    order = ["genuine_wrong_target", "template_instantiation_variant", "missing_reloc",
             "target_split_label", "benign_string_path", "benign_build_artifact",
             "non_reloc_codegen", "error"]
    for c in order:
        print(f"{c:<32} {by_class.get(c,0):>8} {by_class_auth.get(c,0):>12} "
              f"{bytes_by_class.get(c,0):>12}")
    print(f"\nGENUINE wrong-target (false-100%) total      : {by_class['genuine_wrong_target']}")
    print(f"GENUINE wrong-target (false-100%) authorable : {by_class_auth['genuine_wrong_target']}")
    print(f"\nTop authorable units by genuine wrong-target:")
    for un, c in by_unit_genuine.most_common(12):
        print(f"  {c:4d}  {un}")
    print(f"\nWrote {args.out} ({len(genuine_auth)} authorable genuine records)")
    out["summary"]["_coverage"] = cov.as_dict()
    with open(args.out, "w") as fh:
        json.dump(out, fh, indent=1)
    sys.exit(cov.emit())


if __name__ == "__main__":
    main()
