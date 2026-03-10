"""Tests for IL bundle metadata helpers."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
_TOOLS_DIR = _PROJECT_ROOT / "msvc-src" / "tools"
if str(_TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(_TOOLS_DIR))

from il_parser import (
    ILFile,
    IL_SUFFIXES,
    _bundle_manifest_path,
    build_bundle_manifest,
    cmd_export_json,
    read_bundle_manifest,
    resolve_bundle_base,
    write_bundle_manifest,
)

_REAL_FIXTURE_DIR = _PROJECT_ROOT / "msvc-src" / "analysis" / "il-fixtures" / "il_type_control_cast_vs_and"


class TestIlBundleHelpers(unittest.TestCase):
    def test_build_manifest_reports_existing_files(self):
        with tempfile.TemporaryDirectory() as td:
            td_path = Path(td)
            base = td_path / "_CL_deadbeef"
            for suffix in ("ex", "gl"):
                Path(str(base) + suffix).write_bytes(b"abc")

            manifest = build_bundle_manifest(
                base,
                source_path="/tmp/example.cpp",
                bundle_name="example_bundle",
                il_base="_CL_deadbeef",
                command=["cl.exe", "/Bd"],
                run_cwd=td,
            )

            self.assertEqual(manifest["bundle_name"], "example_bundle")
            self.assertTrue(manifest["files"]["ex"]["exists"])
            self.assertEqual(manifest["files"]["ex"]["size"], 3)
            self.assertFalse(manifest["files"]["sy"]["exists"])

    def test_write_and_read_manifest_for_flat_bundle(self):
        with tempfile.TemporaryDirectory() as td:
            td_path = Path(td)
            base = td_path / "_CL_flat"
            manifest = build_bundle_manifest(base, bundle_name="flat")
            manifest_path = write_bundle_manifest(base, manifest)

            self.assertEqual(manifest_path, Path(str(base) + ".manifest.json"))
            loaded = read_bundle_manifest(base)
            self.assertEqual(loaded["bundle_name"], "flat")

    def test_write_and_read_manifest_for_named_bundle_dir(self):
        with tempfile.TemporaryDirectory() as td:
            bundle_dir = Path(td) / "cast_vs_and"
            bundle_dir.mkdir()
            base = bundle_dir / "_CL_cafe"
            Path(str(base) + "ex").write_bytes(b"body")

            manifest = build_bundle_manifest(base, bundle_name="cast_vs_and")
            manifest_path = write_bundle_manifest(base, manifest, bundle_dir=bundle_dir)

            self.assertEqual(manifest_path, bundle_dir / "manifest.json")
            loaded = read_bundle_manifest(bundle_dir)
            self.assertEqual(loaded["bundle_name"], "cast_vs_and")

    def test_resolve_bundle_base_from_file_variants(self):
        with tempfile.TemporaryDirectory() as td:
            bundle_dir = Path(td) / "switch_shape"
            bundle_dir.mkdir()
            base = bundle_dir / "_CL_feed"
            for suffix in IL_SUFFIXES:
                Path(str(base) + suffix).write_bytes(b"x")
            manifest = build_bundle_manifest(base, bundle_name="switch_shape")
            write_bundle_manifest(base, manifest, bundle_dir=bundle_dir)

            self.assertEqual(resolve_bundle_base(bundle_dir), str(base))
            self.assertEqual(resolve_bundle_base(bundle_dir / "manifest.json"), str(base))
            self.assertEqual(resolve_bundle_base(str(base) + "ex"), str(base))
            self.assertEqual(resolve_bundle_base(base), str(base))

    def test_bundle_manifest_path_uses_bundle_dir_when_named(self):
        bundle_base = Path("/tmp/byte_shift/byte_shift")
        self.assertEqual(_bundle_manifest_path(bundle_base), Path("/tmp/byte_shift/byte_shift.manifest.json"))

    def test_ilfile_to_dict_handles_missing_files(self):
        with tempfile.TemporaryDirectory() as td:
            base = str(Path(td) / "_CL_missing")
            il = ILFile(base)
            data = il.to_dict()
            self.assertEqual(data["base"], base)
            self.assertEqual(data["functions"], [])
            self.assertIn("ex", data["files"])
            self.assertFalse(data["files"]["ex"]["present"])

    def test_export_json_uses_bundle_manifest_when_present(self):
        with tempfile.TemporaryDirectory() as td:
            bundle_dir = Path(td) / "cast_vs_and"
            bundle_dir.mkdir()
            base = bundle_dir / "_CL_face"
            manifest = build_bundle_manifest(base, bundle_name="cast_vs_and")
            write_bundle_manifest(base, manifest, bundle_dir=bundle_dir)
            output = bundle_dir / "bundle.json"

            cmd_export_json(SimpleNamespace(path=bundle_dir, output=str(output)))
            self.assertTrue(output.exists())
            text = output.read_text(encoding="utf-8")
            self.assertIn('"manifest"', text)
            self.assertIn('"bundle_name": "cast_vs_and"', text)

    def test_real_fixture_links_symbols_into_function_json(self):
        if not _REAL_FIXTURE_DIR.exists():
            self.skipTest("real IL fixture not present")
        il = ILFile(resolve_bundle_base(_REAL_FIXTURE_DIR))
        data = il.to_dict()
        names = [func["name"] for func in data["functions"]]
        self.assertIn("?cast_shift@@YAII@Z", names)
        cast_shift = next(func for func in data["functions"] if func["name"] == "?cast_shift@@YAII@Z")
        self.assertIn("w", cast_shift["param_names"])
        operand_names = {
            operand.get("name")
            for op in cast_shift["operations"]
            for operand in op.get("operands", [])
            if "name" in operand
        }
        self.assertIn("byte", operand_names)
        self.assertIn("hi", operand_names)

    def test_real_fixture_imports_and_debug_are_exposed(self):
        if not _REAL_FIXTURE_DIR.exists():
            self.skipTest("real IL fixture not present")
        il = ILFile(resolve_bundle_base(_REAL_FIXTURE_DIR))
        data = il.to_dict()
        self.assertIsNotNone(data["imports"])
        self.assertIn("known_types", data["imports"])
        self.assertIsNotNone(data["debug"])
        self.assertIn("line_candidates", data["debug"])


if __name__ == "__main__":
    unittest.main()
