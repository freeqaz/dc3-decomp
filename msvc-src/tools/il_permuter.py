#!/usr/bin/env python3
"""Canonical IL hashing and bucketing prototype for the source permuter.

This tool operates on normalized bundle exports (`bundle.json`) or raw `_CL_*`
bundle bases and answers one immediate question:

    Can we use captured IL as a stable dedup/bucketing key before full scoring?

The canonical form preserves compiler-relevant structure (opcodes, operand
kind/type, literals, result types, switch values) and normalizes away capture
noise (token ids, labels, bundle paths, debug metadata).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
import tempfile
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from il_parser import ILFile, capture_il, read_bundle_manifest, resolve_bundle_base


TOKEN_FIELDS = ("target", "default_target", "result_var")
DROP_FIELDS = {
    "header_offset",
    "body_offset",
    "end_offset",
    "target_name",
    "default_target_name",
    "result_var_name",
    "param_names",
}


def _load_bundle_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def load_bundle(path: str | os.PathLike[str]) -> dict:
    """Load a normalized bundle from bundle.json, a bundle dir, or raw base path."""
    candidate = Path(path)
    if candidate.is_file() and candidate.suffix == ".json":
        return _load_bundle_json(candidate)
    if candidate.is_dir():
        bundle_json = candidate / "bundle.json"
        if bundle_json.exists():
            return _load_bundle_json(bundle_json)
        manifest = read_bundle_manifest(candidate)
        if manifest and manifest.get("bundle_base"):
            return ILFile(manifest["bundle_base"]).to_dict()
    base = resolve_bundle_base(str(candidate))
    bundle_json = Path(base).parent / "bundle.json"
    if bundle_json.exists():
        return _load_bundle_json(bundle_json)
    return ILFile(base).to_dict()


def _stable_hash(payload: object) -> str:
    data = json.dumps(payload, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(data).hexdigest()[:16]


def _alpha_map_get(mapping: dict[int, str], value: int, prefix: str) -> str:
    if value not in mapping:
        mapping[value] = f"{prefix}{len(mapping)}"
    return mapping[value]


def _normalize_name_hint(name: str | None) -> str | None:
    if not name:
        return None
    if name.startswith("tok_"):
        return None
    if name.isidentifier():
        return None
    return name


def canonicalize_operand(
    operand: dict,
    *,
    token_map: dict[int, str],
) -> dict:
    """Normalize an operand while preserving compiler-relevant semantics."""
    kind = operand.get("kind")
    value = operand.get("value")
    normalized = {"kind": kind}
    if "type" in operand:
        normalized["type"] = operand["type"]

    if kind in ("var", "ref", "val") and isinstance(value, int):
        normalized["value"] = _alpha_map_get(token_map, value, "t")
        symbol_name = _normalize_name_hint(operand.get("name"))
        if symbol_name:
            normalized["symbol"] = symbol_name
        return normalized

    normalized["value"] = value
    return normalized


def canonicalize_operation(
    operation: dict,
    *,
    token_map: dict[int, str],
    label_map: dict[int, str],
) -> dict:
    """Normalize a single IL operation."""
    normalized = {
        "type": operation.get("type"),
        "name": operation.get("name"),
    }

    if "operands" in operation:
        normalized["operands"] = [
            canonicalize_operand(operand, token_map=token_map)
            for operand in operation.get("operands", [])
        ]

    for field in ("result_type", "return_type", "flags", "value"):
        if field in operation:
            normalized[field] = operation[field]

    if "label" in operation:
        label = operation["label"]
        if isinstance(label, int):
            normalized["label"] = _alpha_map_get(label_map, label, "L")
        else:
            normalized["label"] = label

    for field in ("target", "default_target"):
        if field in operation:
            value = operation[field]
            if isinstance(value, int):
                normalized[field] = _alpha_map_get(token_map, value, "t")
            else:
                normalized[field] = value

    return normalized


def canonicalize_function(function: dict, *, include_name: bool = False) -> dict:
    """Return compiler-relevant canonical form for a function."""
    token_map: dict[int, str] = {}
    label_map: dict[int, str] = {}

    normalized = {
        "operation_count": function.get("operation_count"),
        "operations": [
            canonicalize_operation(op, token_map=token_map, label_map=label_map)
            for op in function.get("operations", [])
        ],
    }

    if include_name:
        normalized["name"] = function.get("name")

    params = function.get("params") or []
    if params:
        normalized["params"] = [
            _alpha_map_get(token_map, value, "t") if isinstance(value, int) else value
            for value in params
        ]

    if "result_var" in function and function["result_var"] is not None:
        value = function["result_var"]
        normalized["result_var"] = (
            _alpha_map_get(token_map, value, "t") if isinstance(value, int) else value
        )

    for field, value in function.items():
        if field in normalized or field in DROP_FIELDS:
            continue
        if field in TOKEN_FIELDS and isinstance(value, int):
            normalized[field] = _alpha_map_get(token_map, value, "t")
            continue
        if field in ("index", "name", "operations", "params"):
            continue
        normalized[field] = value

    return normalized


def function_hash(function: dict) -> str:
    return _stable_hash(canonicalize_function(function))


def canonicalize_bundle(bundle: dict, *, function_filter: str | None = None) -> dict:
    """Return canonical bundle view suitable for hashing or comparison."""
    functions = []
    for function in bundle.get("functions", []):
        name = function.get("name", "")
        if function_filter and function_filter not in name:
            continue
        functions.append(
            {
                "name": name,
                "hash": function_hash(function),
                "canonical": canonicalize_function(function, include_name=False),
            }
        )
    functions.sort(key=lambda item: item["name"])

    return {
        "token_width": bundle.get("token_width"),
        "functions": functions,
    }


def bundle_hash(bundle: dict, *, function_filter: str | None = None) -> str:
    """Return a stable bundle identity hash.

    This includes function names so whole-TU identity stays stable even if two
    different TUs happen to contain the same set of anonymous structures.
    """
    identity = {
        "token_width": bundle.get("token_width"),
        "functions": [
            {
                "name": item["name"],
                "hash": item["hash"],
            }
            for item in canonicalize_bundle(bundle, function_filter=function_filter)["functions"]
        ],
    }
    return _stable_hash(identity)


def compare_bundles(
    bundle_a: dict,
    bundle_b: dict,
    *,
    function_filter: str | None = None,
) -> dict:
    funcs_a = {
        item["name"]: item
        for item in canonicalize_bundle(bundle_a, function_filter=function_filter)["functions"]
    }
    funcs_b = {
        item["name"]: item
        for item in canonicalize_bundle(bundle_b, function_filter=function_filter)["functions"]
    }

    changed = []
    added = []
    removed = []

    for name in sorted(set(funcs_a) | set(funcs_b)):
        if name not in funcs_a:
            added.append(name)
            continue
        if name not in funcs_b:
            removed.append(name)
            continue
        if funcs_a[name]["hash"] != funcs_b[name]["hash"]:
            changed.append(
                {
                    "name": name,
                    "hash_a": funcs_a[name]["hash"],
                    "hash_b": funcs_b[name]["hash"],
                }
            )

    return {
        "bundle_hash_a": bundle_hash(bundle_a, function_filter=function_filter),
        "bundle_hash_b": bundle_hash(bundle_b, function_filter=function_filter),
        "changed": changed,
        "added": added,
        "removed": removed,
    }


def iter_bundle_paths(root: str | os.PathLike[str]) -> list[Path]:
    """Return bundle directories or bundle json paths under a root."""
    root_path = Path(root)
    results: list[Path] = []
    for path in sorted(root_path.rglob("bundle.json")):
        results.append(path)
    return results


def bucket_functions(root: str | os.PathLike[str], *, function_filter: str | None = None) -> dict[str, list[str]]:
    groups: dict[str, list[str]] = defaultdict(list)
    for bundle_json in iter_bundle_paths(root):
        bundle = load_bundle(bundle_json)
        bundle_name = bundle_json.parent.name
        for function in bundle.get("functions", []):
            name = function.get("name", "")
            if function_filter and function_filter not in name:
                continue
            groups[function_hash(function)].append(f"{bundle_name}:{name}")
    return dict(sorted(groups.items(), key=lambda item: (-len(item[1]), item[0])))


def _print_hash_bundle(bundle: dict, *, function_filter: str | None, as_json: bool) -> None:
    functions = []
    for function in bundle.get("functions", []):
        name = function.get("name", "")
        if function_filter and function_filter not in name:
            continue
        functions.append({"name": name, "hash": function_hash(function)})

    payload = {
        "bundle_hash": bundle_hash(bundle, function_filter=function_filter),
        "functions": sorted(functions, key=lambda item: item["name"]),
    }
    if as_json:
        print(json.dumps(payload, indent=2, sort_keys=True))
        return
    print(f"bundle_hash={payload['bundle_hash']}")
    for item in payload["functions"]:
        print(f"{item['hash']}  {item['name']}")


def _print_compare(result: dict, *, as_json: bool) -> None:
    if as_json:
        print(json.dumps(result, indent=2, sort_keys=True))
        return
    print(f"bundle_hash_a={result['bundle_hash_a']}")
    print(f"bundle_hash_b={result['bundle_hash_b']}")
    if result["added"]:
        print("added:")
        for name in result["added"]:
            print(f"  {name}")
    if result["removed"]:
        print("removed:")
        for name in result["removed"]:
            print(f"  {name}")
    if result["changed"]:
        print("changed:")
        for item in result["changed"]:
            print(f"  {item['name']}: {item['hash_a']} -> {item['hash_b']}")
    if not result["added"] and not result["removed"] and not result["changed"]:
        print("no canonical IL changes")


def _print_bucket_report(groups: dict[str, list[str]], *, min_count: int, as_json: bool) -> None:
    filtered = {
        hash_value: members
        for hash_value, members in groups.items()
        if len(members) >= min_count
    }
    if as_json:
        print(json.dumps(filtered, indent=2, sort_keys=True))
        return
    for hash_value, members in filtered.items():
        print(f"{hash_value} ({len(members)})")
        for member in members:
            print(f"  {member}")


def _capture_bundle(source_path: str, *, output_dir: str) -> dict:
    base = capture_il(source_path, output_dir=output_dir)
    if not base:
        raise RuntimeError(f"IL capture failed for {source_path}")
    return ILFile(base).to_dict()


def main() -> None:
    parser = argparse.ArgumentParser(description="Canonical IL hashing prototype")
    sub = parser.add_subparsers(dest="command", required=True)

    p_hash = sub.add_parser("hash-bundle", help="Hash a bundle or bundle.json")
    p_hash.add_argument("path")
    p_hash.add_argument("--function", help="Restrict to functions containing this substring")
    p_hash.add_argument("--json", action="store_true", help="Emit JSON")

    p_compare = sub.add_parser("compare-bundles", help="Compare two bundles canonically")
    p_compare.add_argument("path_a")
    p_compare.add_argument("path_b")
    p_compare.add_argument("--function", help="Restrict to functions containing this substring")
    p_compare.add_argument("--json", action="store_true", help="Emit JSON")

    p_source = sub.add_parser("compare-source", help="Capture IL for two sources and compare")
    p_source.add_argument("source_a")
    p_source.add_argument("source_b")
    p_source.add_argument("--function", help="Restrict to functions containing this substring")
    p_source.add_argument("--output-dir", help="Working directory for captured bundles")
    p_source.add_argument("--json", action="store_true", help="Emit JSON")

    p_bucket = sub.add_parser("bucket-dir", help="Bucket functions by canonical IL hash")
    p_bucket.add_argument("root")
    p_bucket.add_argument("--function", help="Restrict to functions containing this substring")
    p_bucket.add_argument("--min-count", type=int, default=1, help="Only print groups of at least this size")
    p_bucket.add_argument("--json", action="store_true", help="Emit JSON")

    args = parser.parse_args()

    if args.command == "hash-bundle":
        bundle = load_bundle(args.path)
        _print_hash_bundle(bundle, function_filter=args.function, as_json=args.json)
        return

    if args.command == "compare-bundles":
        bundle_a = load_bundle(args.path_a)
        bundle_b = load_bundle(args.path_b)
        result = compare_bundles(bundle_a, bundle_b, function_filter=args.function)
        _print_compare(result, as_json=args.json)
        return

    if args.command == "compare-source":
        output_dir = args.output_dir
        if not output_dir:
            with tempfile.TemporaryDirectory(prefix="il-permuter-") as temp_dir:
                bundle_a = _capture_bundle(args.source_a, output_dir=temp_dir)
                bundle_b = _capture_bundle(args.source_b, output_dir=temp_dir)
                result = compare_bundles(bundle_a, bundle_b, function_filter=args.function)
                _print_compare(result, as_json=args.json)
                return
        Path(output_dir).mkdir(parents=True, exist_ok=True)
        bundle_a = _capture_bundle(args.source_a, output_dir=output_dir)
        bundle_b = _capture_bundle(args.source_b, output_dir=output_dir)
        result = compare_bundles(bundle_a, bundle_b, function_filter=args.function)
        _print_compare(result, as_json=args.json)
        return

    if args.command == "bucket-dir":
        groups = bucket_functions(args.root, function_filter=args.function)
        _print_bucket_report(groups, min_count=args.min_count, as_json=args.json)
        return


if __name__ == "__main__":
    main()
