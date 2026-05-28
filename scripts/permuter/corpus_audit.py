"""Corpus-scale pattern stress-test / safety auditor.

Runs each selected pattern's ``generate()`` across every ``.cpp`` in the source
tree (no build, pure AST) and reports, per pattern:

  - fire rate          — functions that produced >=1 variant
  - variants emitted
  - NEW parse errors    — variants that introduce tree-sitter ERROR nodes the
                          ORIGINAL file did not have. This is the universal
                          safety signal: a pattern that emits malformed C++.
                          (Counted relative to the original because tree-sitter
                          already errors on plenty of MWCC macros/templates; a
                          naive has_error(variant) check is almost all false
                          positives.)

Two patterns get extra, pattern-specific deep analyses (the checks that caught
real defects when these rules were first stress-tested):

  - pointer_iter_unroll — incomplete-rewrite detection: the original loop
    pointer must NOT survive inside the transformed loop body (leaving it there
    is a behaviour change / infinite loop).
  - pod_ctor_toggle     — gate-leak: a remove-variant emitted even though the
    struct is still constructed with args / has an out-of-line def (would break
    the build); plus per-TU blast radius (how many functions reference the
    mutated struct).

This is a fast, build-free way to validate a new or changed pattern before
turning it loose in sweeps. Run as::

    python3 -m scripts.permuter.corpus_audit                 # all default patterns
    python3 -m scripts.permuter.corpus_audit --patterns pod_ctor_toggle
    python3 -m scripts.permuter.corpus_audit --patterns all --json
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable, Optional

from tree_sitter import Node

from .extractor import _PARSER, _find_all_function_defs, _get_function_name
from .patterns.base import get_pattern, list_patterns
from .types import Cluster, Diagnosis, DiffOp, FunctionContext
import scripts.permuter.patterns  # noqa: F401  (registers all patterns)


# ---------------------------------------------------------------------------
# Repo-root / source discovery (self-contained — independent of project.py so
# the auditor keeps working while that module is being refactored).
# ---------------------------------------------------------------------------


def detect_repo_root(start: Optional[Path] = None) -> Path:
    cur = (start or Path.cwd()).resolve()
    for cand in [cur, *cur.parents]:
        if (cand / "objdiff.json").is_file() or (cand / "src").is_dir():
            return cand
    return cur


def collect_cpp_files(roots: Iterable[Path]) -> list[Path]:
    out: list[Path] = []
    for root in roots:
        if root.is_file() and root.suffix == ".cpp":
            out.append(root)
        elif root.is_dir():
            out.extend(root.rglob("*.cpp"))
    return sorted(set(out))


# ---------------------------------------------------------------------------
# Diagnosis that makes generators emit maximally (so the audit sees the full
# variant set a pattern *could* produce). generate() does not gate on
# relevant(); patterns that read ctx.diagnosis (e.g. polarity hints) get a
# both-directions signal here.
# ---------------------------------------------------------------------------


def permissive_diag() -> Diagnosis:
    return Diagnosis(
        total_instructions=100,
        match_counts={"match": 90, "mismatch": 10},
        reg_swap_pairs={},
        offset_deltas={},
        diff_ops=[
            DiffOp(index=10, target_opcode="stfs", base_opcode="stw"),
            DiffOp(index=11, target_opcode="stw", base_opcode="sth"),
            DiffOp(index=12, target_opcode="bl", base_opcode="cmplw", target_arg="strcmp"),
        ],
        clusters=[Cluster(start_idx=5, end_idx=12, size=7, inserts=1, deletes=6)],
        noise_explained=0,
        noise_total=0,
    )


# ---------------------------------------------------------------------------
# tree-sitter helpers
# ---------------------------------------------------------------------------


def _count_error_nodes(node: Node) -> int:
    n = 1 if (node.type == "ERROR" or node.is_missing) else 0
    for c in node.children:
        n += _count_error_nodes(c)
    return n


def count_errors(source: bytes) -> int:
    return _count_error_nodes(_PARSER.parse(source).root_node)


def _walk(node: Node):
    yield node
    for c in node.children:
        yield from _walk(c)


def _ident_in(node: Node, name: bytes) -> bool:
    for n in _walk(node):
        if n.type == "identifier" and n.text == name:
            return True
    return False


def _for_loops(node: Node):
    for n in _walk(node):
        if n.type == "for_statement":
            yield n


# ---------------------------------------------------------------------------
# Result accumulation
# ---------------------------------------------------------------------------


@dataclass
class PatternAudit:
    name: str
    funcs_scanned: int = 0
    fired_funcs: int = 0
    variants: int = 0
    new_parse_errors: int = 0
    # pattern-specific
    incomplete_rewrites: int = 0          # pointer_iter_unroll
    gate_leaks: int = 0                   # pod_ctor_toggle
    blast_radius: list[int] = field(default_factory=list)  # pod_ctor_toggle
    samples: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)

    def to_dict(self) -> dict:
        d = {
            "name": self.name,
            "funcs_scanned": self.funcs_scanned,
            "fired_funcs": self.fired_funcs,
            "variants": self.variants,
            "new_parse_errors": self.new_parse_errors,
        }
        if self.incomplete_rewrites or self.name == "pointer_iter_unroll":
            d["incomplete_rewrites"] = self.incomplete_rewrites
        if self.blast_radius or self.name == "pod_ctor_toggle":
            d["gate_leaks"] = self.gate_leaks
            d["blast_radius_max"] = max(self.blast_radius) if self.blast_radius else 0
            d["blast_radius_count"] = len(self.blast_radius)
            d["blast_radius_le1"] = sum(1 for r in self.blast_radius if r <= 1)
        return d


_PTRITER_DESC = re.compile(r"fresh iterator '(\w+)' for '(\w+)'")


def _audit_pointer_iter_variant(audit: PatternAudit, variant_source: bytes, description: str) -> None:
    """Flag a variant that leaves the original loop pointer in the loop body."""
    m = _PTRITER_DESC.search(description)
    if not m:
        return
    fresh_b, ptr_b = m.group(1).encode(), m.group(2).encode()
    vtree = _PARSER.parse(variant_source)
    for loop in _for_loops(vtree.root_node):
        body = loop.child_by_field_name("body")
        if body is None:
            continue
        if _ident_in(loop, fresh_b) and _ident_in(body, ptr_b):
            audit.incomplete_rewrites += 1
            return


def _struct_from_pod_desc(description: str) -> Optional[str]:
    m = re.search(r"from (\w+) to make it POD", description)
    if not m:
        m = re.search(r"Add empty ctor (\w+)\(\)", description)
    return m.group(1) if m else None


def _pod_remove_is_unsafe(source: bytes, struct: str) -> bool:
    """Independent re-audit: would removing struct's ctor break the build?"""
    name_b = re.escape(struct.encode())
    tree = _PARSER.parse(source)
    body_ranges = [
        (n.start_byte, n.end_byte)
        for n in _walk(tree.root_node)
        if n.type in ("struct_specifier", "class_specifier")
        and (nn := n.child_by_field_name("name")) is not None
        and source[nn.start_byte:nn.end_byte] == struct.encode()
    ]

    def outside(pos: int) -> bool:
        return not any(lo <= pos < hi for lo, hi in body_ranges)

    patterns = (
        re.compile(rb"\b" + name_b + rb"\s*\(\s*(?!\))"),                       # Name(arg
        re.compile(rb"\b" + name_b + rb"\s+[A-Za-z_]\w*\s*\(\s*(?!\))"),        # Name ident(arg
        re.compile(rb"\b" + name_b + rb"\s*::\s*~?" + name_b + rb"\s*\("),      # out-of-line def
    )
    return any(outside(m.start()) for p in patterns for m in p.finditer(source))


def _count_struct_refs(source: bytes, struct: str) -> int:
    """Functions referencing the struct by name — as a value identifier OR a
    type (struct type names parse as ``type_identifier``, not ``identifier``)."""
    name_b = struct.encode()
    tree = _PARSER.parse(source)
    count = 0
    for fn in _find_all_function_defs(tree.root_node):
        if any(
            n.type in ("identifier", "type_identifier") and n.text == name_b
            for n in _walk(fn)
        ):
            count += 1
    return count


# ---------------------------------------------------------------------------
# Core audit
# ---------------------------------------------------------------------------


def audit_patterns(
    files: list[Path], pattern_names: list[str], max_samples: int = 12
) -> dict[str, PatternAudit]:
    diag = permissive_diag()
    audits = {name: PatternAudit(name=name) for name in pattern_names}
    patterns = {name: get_pattern(name) for name in pattern_names}

    for fp in files:
        try:
            src = fp.read_bytes()
        except OSError:
            continue
        tree = _PARSER.parse(src)
        orig_errs = _count_error_nodes(tree.root_node)
        fn_defs = _find_all_function_defs(tree.root_node)

        # pod_ctor_toggle walks the whole file from any function's root, so it
        # only needs ONE anchor function per file (dedup the per-file work).
        pod_done = False

        for fn in fn_defs:
            body = fn.child_by_field_name("body")
            if body is None:
                continue
            ctx = FunctionContext(
                file_path=fp,
                file_source=src,
                func_node=fn,
                body_node=body,
                statements=list(body.named_children),
                func_byte_range=(fn.start_byte, fn.end_byte),
                diagnosis=diag,
            )
            for name, pattern in patterns.items():
                audit = audits[name]
                file_scoped = name == "pod_ctor_toggle"
                if file_scoped and pod_done:
                    continue
                if not file_scoped:
                    audit.funcs_scanned += 1
                fired = False
                try:
                    variants = list(pattern.generate(ctx))
                except Exception as exc:  # noqa: BLE001
                    audit.warnings.append(f"{fp.name}::{_get_function_name(fn)}: {exc}")
                    continue
                for v in variants:
                    fired = True
                    audit.variants += 1
                    if count_errors(v.source) > orig_errs:
                        audit.new_parse_errors += 1
                        if len(audit.samples) < max_samples:
                            audit.samples.append(
                                f"!! PARSE_ERROR {fp.name}::{_get_function_name(fn)}: {v.description}"
                            )
                        continue
                    if name == "pointer_iter_unroll":
                        before = audit.incomplete_rewrites
                        _audit_pointer_iter_variant(audit, v.source, v.description)
                        if audit.incomplete_rewrites > before and len(audit.samples) < max_samples:
                            audit.samples.append(
                                f"!! INCOMPLETE {fp.name}::{_get_function_name(fn)}: {v.description}"
                            )
                    elif name == "pod_ctor_toggle":
                        struct = _struct_from_pod_desc(v.description)
                        if struct:
                            if "pod_ctor_remove" in v.name and _pod_remove_is_unsafe(src, struct):
                                audit.gate_leaks += 1
                                audit.samples.append(
                                    f"!! GATE_LEAK {fp.name}: remove {struct} unsafe"
                                )
                            audit.blast_radius.append(_count_struct_refs(src, struct))
                    if fired and len(audit.samples) < max_samples:
                        s = f"   fire {fp.name}::{_get_function_name(fn)}"
                        if s not in audit.samples:
                            audit.samples.append(s)
                if file_scoped:
                    pod_done = True
                    if fired:
                        audit.fired_funcs += 1
                elif fired:
                    audit.fired_funcs += 1
    return audits


def _print_report(audits: dict[str, PatternAudit], total_files: int) -> None:
    print(f"Corpus: {total_files} .cpp files\n")
    for name, a in sorted(audits.items()):
        print("=" * 70)
        print(name)
        print("=" * 70)
        if name != "pod_ctor_toggle":
            print(f"  functions scanned : {a.funcs_scanned}")
        print(f"  functions fired   : {a.fired_funcs}")
        print(f"  variants emitted  : {a.variants}")
        print(f"  NEW parse errors  : {a.new_parse_errors}  (must be 0)")
        if name == "pointer_iter_unroll":
            print(f"  INCOMPLETE rewrite: {a.incomplete_rewrites}  (must be 0)")
        if name == "pod_ctor_toggle":
            print(f"  GATE LEAKS        : {a.gate_leaks}  (must be 0)")
            if a.blast_radius:
                r = sorted(a.blast_radius)
                n = len(r)
                print(f"  blast radius      : n={n} max={r[-1]} "
                      f"median={r[n // 2]} radius<=1={sum(1 for x in r if x <= 1)}")
        if a.warnings:
            print(f"  generate() errors : {len(a.warnings)} (first: {a.warnings[0]})")
        for s in a.samples[:12]:
            print(f"    {s}")
        print()


def main(argv: Optional[list[str]] = None) -> int:
    ap = argparse.ArgumentParser(
        prog="python -m scripts.permuter.corpus_audit",
        description="Build-free corpus stress-test / safety audit for permuter patterns.",
    )
    ap.add_argument(
        "--patterns", default="default",
        help="Comma-separated pattern names, 'default' (non-opt-in), or 'all'.",
    )
    ap.add_argument(
        "--roots", default="",
        help="Comma-separated dirs/files to scan (default: <repo>/src).",
    )
    ap.add_argument("--limit", type=int, default=0, help="Max files (0 = all).")
    ap.add_argument("--max-samples", type=int, default=12)
    ap.add_argument("--json", action="store_true", help="Emit JSON instead of text.")
    args = ap.parse_args(argv)

    if args.patterns == "all":
        names = list_patterns(include_opt_in=True)
    elif args.patterns == "default":
        names = list_patterns(include_opt_in=False)
    else:
        names = [p.strip() for p in args.patterns.split(",") if p.strip()]
        # Validate up front so a typo fails fast.
        for n in names:
            get_pattern(n)

    repo_root = detect_repo_root()
    if args.roots:
        roots = [Path(r).expanduser().resolve() for r in args.roots.split(",")]
    else:
        roots = [repo_root / "src"]
    files = collect_cpp_files(roots)
    if args.limit:
        files = files[: args.limit]

    audits = audit_patterns(files, names, max_samples=args.max_samples)

    if args.json:
        print(json.dumps(
            {"files": len(files), "patterns": {n: a.to_dict() for n, a in audits.items()}},
            indent=2,
        ))
    else:
        _print_report(audits, len(files))
    return 0


if __name__ == "__main__":
    sys.exit(main())
