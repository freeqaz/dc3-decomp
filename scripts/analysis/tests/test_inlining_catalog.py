"""Tests for inlining_catalog — TU-boundary inlining control catalog."""
from __future__ import annotations

import json
import os
import textwrap
from pathlib import Path

import pytest

from scripts.analysis.inlining_catalog import (
    HeaderAccessor,
    CatalogEntry,
    _count_statements,
    _is_accessor_body,
    _is_small_body,
    _scan_single_header,
    _normalize_header_key,
    _extract_class_context,
    scan_header_accessors,
    build_catalog,
    check_header,
    _build_parser,
    _KNOWN_OUTLINES,
    main,
)


# -----------------------------------------------------------------------
# Fixtures
# -----------------------------------------------------------------------

MOCK_HEADER_SIMPLE = textwrap.dedent("""\
    #pragma once

    class Foo : public Base {
    public:
        float GetValue() const { return mValue; }
        int Count() const { return mCount; }
        void SetValue(float v) { mValue = v; }
        Bar* GetBar() { return mBar; }
        const char* Name() const { return mName.c_str(); }

    protected:
        float mValue;
        int mCount;
        Bar* mBar;
        String mName;
    };
""")

MOCK_HEADER_MIXED = textwrap.dedent("""\
    #pragma once
    #include "obj/Object.h"

    class Widget : public Hmx::Object {
    public:
        virtual void Draw() {}
        virtual UIList* SubList(int) { return nullptr; }
        virtual void Poll() {}

        float DrawOrder() const { return mDrawOrder; }
        float Alpha() const { return mAlpha; }
        Widget* Parent() { return mParent; }
        int ComputeIndex(int x, int y) { int r = x * mStride + y; return r; }
        void BigMethod(int a, int b) { int c = a+b; int d = c*2; int e = d+1; mVal = e; DoStuff(e); Update(); }

    protected:
        float mDrawOrder;
        float mAlpha;
        Widget* mParent;
        int mStride;
        int mVal;
    };
""")

MOCK_HEADER_NO_CLASS = textwrap.dedent("""\
    #pragma once

    inline int Square(int x) { return x * x; }
    inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
""")

MOCK_HEADER_EMPTY_VIRTUALS = textwrap.dedent("""\
    #pragma once

    class Handler {
    public:
        virtual void OnEvent() {}
        virtual void OnUpdate() {}
        virtual int GetType() { return mType; }

    protected:
        int mType;
    };
""")


@pytest.fixture
def tmp_header_dir(tmp_path):
    """Create a temp directory with mock headers."""
    hdir = tmp_path / "src" / "system" / "ui"
    hdir.mkdir(parents=True)

    (hdir / "Foo.h").write_text(MOCK_HEADER_SIMPLE)
    (hdir / "Widget.h").write_text(MOCK_HEADER_MIXED)
    (hdir / "Handler.h").write_text(MOCK_HEADER_EMPTY_VIRTUALS)

    mathdir = tmp_path / "src" / "system" / "math"
    mathdir.mkdir(parents=True)
    (mathdir / "MathUtil.h").write_text(MOCK_HEADER_NO_CLASS)

    return tmp_path


# -----------------------------------------------------------------------
# Statement counting
# -----------------------------------------------------------------------

class TestCountStatements:
    def test_empty_body(self):
        assert _count_statements("") == 0

    def test_single_return(self):
        assert _count_statements("return mValue;") == 1

    def test_two_statements(self):
        assert _count_statements("int x = 1; return x;") == 2

    def test_setter(self):
        assert _count_statements("mValue = v;") == 1

    def test_multi_statement(self):
        body = "int c = a+b; int d = c*2; int e = d+1; mVal = e; DoStuff(e); Update();"
        assert _count_statements(body) == 6


# -----------------------------------------------------------------------
# Accessor body detection
# -----------------------------------------------------------------------

class TestIsAccessorBody:
    def test_return_member(self):
        assert _is_accessor_body("return mValue;")

    def test_return_member_method(self):
        assert _is_accessor_body("return mName.c_str();")

    def test_return_member_deref(self):
        assert _is_accessor_body("return mBar->GetValue();")

    def test_return_simple(self):
        assert _is_accessor_body("return value;")

    def test_return_nullptr(self):
        assert _is_accessor_body("return nullptr;")
        assert _is_accessor_body("return 0;")
        assert _is_accessor_body("return NULL;")

    def test_setter_not_accessor(self):
        assert not _is_accessor_body("mValue = v;")

    def test_complex_not_accessor(self):
        assert not _is_accessor_body("int r = x * mStride + y; return r;")

    def test_empty(self):
        assert not _is_accessor_body("")


# -----------------------------------------------------------------------
# Small body detection
# -----------------------------------------------------------------------

class TestIsSmallBody:
    def test_single_statement(self):
        assert _is_small_body("return mValue;")

    def test_five_statements(self):
        assert _is_small_body("a; b; c; d; e;", max_statements=5)

    def test_six_statements_exceeds(self):
        assert not _is_small_body("a; b; c; d; e; f;", max_statements=5)


# -----------------------------------------------------------------------
# Header accessor size classification
# -----------------------------------------------------------------------

class TestHeaderAccessorSizeClass:
    def test_trivial(self):
        a = HeaderAccessor("h.h", "Foo", "Get", 1, "return mX;")
        assert a.size_class == "trivial"

    def test_small(self):
        a = HeaderAccessor("h.h", "Foo", "Compute", 2, "int r = x; return r;")
        assert a.size_class == "small"

    def test_medium(self):
        a = HeaderAccessor("h.h", "Foo", "DoStuff", 4, "a; b; c; d;")
        assert a.size_class == "medium"


# -----------------------------------------------------------------------
# Single header scanning
# -----------------------------------------------------------------------

class TestScanSingleHeader:
    def test_simple_accessors(self):
        results = _scan_single_header(MOCK_HEADER_SIMPLE, "ui/Foo.h")
        method_names = [r.method_name for r in results]
        # Should find the simple return-member accessors
        assert "GetValue" in method_names
        assert "Count" in method_names
        assert "GetBar" in method_names

    def test_setter_included(self):
        results = _scan_single_header(MOCK_HEADER_SIMPLE, "ui/Foo.h")
        method_names = [r.method_name for r in results]
        assert "SetValue" in method_names

    def test_virtual_empty_excluded(self):
        results = _scan_single_header(MOCK_HEADER_MIXED, "ui/Widget.h")
        method_names = [r.method_name for r in results]
        # Virtual methods with empty bodies should be excluded
        assert "Draw" not in method_names
        assert "Poll" not in method_names
        # Virtual with nullptr return should also be excluded
        assert "SubList" not in method_names

    def test_real_accessor_in_mixed(self):
        results = _scan_single_header(MOCK_HEADER_MIXED, "ui/Widget.h")
        method_names = [r.method_name for r in results]
        assert "DrawOrder" in method_names
        assert "Alpha" in method_names
        assert "Parent" in method_names

    def test_medium_body_included(self):
        results = _scan_single_header(MOCK_HEADER_MIXED, "ui/Widget.h")
        method_names = [r.method_name for r in results]
        assert "ComputeIndex" in method_names

    def test_big_method_excluded(self):
        results = _scan_single_header(MOCK_HEADER_MIXED, "ui/Widget.h")
        method_names = [r.method_name for r in results]
        assert "BigMethod" not in method_names

    def test_class_context(self):
        results = _scan_single_header(MOCK_HEADER_SIMPLE, "ui/Foo.h")
        for r in results:
            assert r.class_name == "Foo"

    def test_const_detection(self):
        results = _scan_single_header(MOCK_HEADER_SIMPLE, "ui/Foo.h")
        by_name = {r.method_name: r for r in results}
        assert by_name["GetValue"].is_const
        assert by_name["Count"].is_const
        assert not by_name["SetValue"].is_const

    def test_handler_virtuals_excluded(self):
        results = _scan_single_header(MOCK_HEADER_EMPTY_VIRTUALS, "obj/Handler.h")
        method_names = [r.method_name for r in results]
        # Virtual empty bodies are excluded
        assert "OnEvent" not in method_names
        assert "OnUpdate" not in method_names
        # But GetType has a real accessor body, should be included
        assert "GetType" in method_names

    def test_free_functions(self):
        results = _scan_single_header(MOCK_HEADER_NO_CLASS, "math/MathUtil.h")
        method_names = [r.method_name for r in results]
        assert "Square" in method_names
        assert "Lerp" in method_names


# -----------------------------------------------------------------------
# Header path normalization
# -----------------------------------------------------------------------

class TestNormalizeHeaderKey:
    def test_src_system_prefix(self):
        assert _normalize_header_key("src/system/ui/UIListWidget.h") == "ui/UIListWidget.h"

    def test_already_relative(self):
        assert _normalize_header_key("ui/UIListWidget.h") == "ui/UIListWidget.h"

    def test_src_prefix(self):
        assert _normalize_header_key("src/obj/Object.h") == "obj/Object.h"

    def test_backslashes(self):
        assert _normalize_header_key("src\\system\\ui\\UIListWidget.h") == "ui/UIListWidget.h"


# -----------------------------------------------------------------------
# Class context extraction
# -----------------------------------------------------------------------

class TestExtractClassContext:
    def test_simple_class(self):
        text = "class Foo : public Base { ... };"
        assert _extract_class_context(text) == "Foo"

    def test_struct(self):
        text = "struct Bar { ... };"
        assert _extract_class_context(text) == "Bar"

    def test_no_class(self):
        text = "int x = 5;"
        assert _extract_class_context(text) == ""

    def test_multiple_classes(self):
        text = "class Foo { }; class Bar { };"
        assert _extract_class_context(text) == "Bar"


# -----------------------------------------------------------------------
# Directory scanning
# -----------------------------------------------------------------------

class TestScanHeaderAccessors:
    def test_scan_directory(self, tmp_header_dir):
        src_dir = tmp_header_dir / "src" / "system"
        results = scan_header_accessors(
            [str(src_dir)],
            project_root=str(tmp_header_dir),
        )
        assert len(results) > 0
        # Should find accessors from multiple headers
        headers = set(r.header for r in results)
        assert len(headers) >= 2

    def test_relative_paths(self, tmp_header_dir):
        src_dir = tmp_header_dir / "src" / "system"
        results = scan_header_accessors(
            [str(src_dir)],
            project_root=str(tmp_header_dir),
        )
        for r in results:
            assert r.header.startswith("src/system/")

    def test_nonexistent_dir(self):
        results = scan_header_accessors(["/nonexistent/path"])
        assert results == []


# -----------------------------------------------------------------------
# Catalog building
# -----------------------------------------------------------------------

class TestBuildCatalog:
    def test_basic_catalog(self, tmp_header_dir):
        src_dir = tmp_header_dir / "src" / "system"
        catalog = build_catalog(
            [str(src_dir)],
            known_outlines=[],
            project_root=str(tmp_header_dir),
        )
        assert "headers" in catalog
        assert "summary" in catalog
        assert catalog["summary"]["total_accessors"] > 0

    def test_with_known_outlines(self, tmp_header_dir):
        src_dir = tmp_header_dir / "src" / "system"
        known = [
            {
                "header": "ui/Foo.h",
                "class": "Foo",
                "method": "GetValue",
                "status": "outlined",
                "fixed_functions": ["Bar::Draw"],
                "notes": "Fixed Bar::Draw 90->100%",
            }
        ]
        catalog = build_catalog(
            [str(src_dir)],
            known_outlines=known,
            project_root=str(tmp_header_dir),
        )
        summary = catalog["summary"]
        assert summary["outlined"] >= 1

        # Find the outlined entry
        for header_data in catalog["headers"].values():
            for acc in header_data["accessors"]:
                if acc["method"] == "GetValue":
                    assert acc["status"] == "outlined"
                    assert "Bar::Draw" in acc["fixed_functions"]
                    break

    def test_catalog_json_serializable(self, tmp_header_dir):
        src_dir = tmp_header_dir / "src" / "system"
        catalog = build_catalog(
            [str(src_dir)],
            project_root=str(tmp_header_dir),
        )
        # Must be JSON-serializable
        serialized = json.dumps(catalog)
        assert isinstance(serialized, str)
        reparsed = json.loads(serialized)
        assert reparsed["summary"]["total_accessors"] == catalog["summary"]["total_accessors"]

    def test_default_known_outlines(self, tmp_header_dir):
        # Verify _KNOWN_OUTLINES has valid structure
        for ko in _KNOWN_OUTLINES:
            assert "header" in ko
            assert "class" in ko
            assert "method" in ko
            assert "status" in ko


# -----------------------------------------------------------------------
# Check header
# -----------------------------------------------------------------------

class TestCheckHeader:
    def test_check_existing(self, tmp_header_dir):
        header_path = tmp_header_dir / "src" / "system" / "ui" / "Foo.h"
        result = check_header(str(header_path))
        assert "accessors" in result
        assert len(result["accessors"]) > 0
        assert result["summary"]["total"] > 0

    def test_check_nonexistent(self):
        result = check_header("/nonexistent/header.h")
        assert "error" in result

    def test_check_with_known_outlines(self, tmp_header_dir):
        header_path = tmp_header_dir / "src" / "system" / "ui" / "Foo.h"
        known = [
            {
                "header": str(header_path),
                "class": "Foo",
                "method": "GetValue",
                "status": "outlined",
                "fixed_functions": ["Baz::Render"],
            }
        ]
        result = check_header(str(header_path), known_outlines=known)
        outlined = [a for a in result["accessors"] if a["status"] == "outlined"]
        assert len(outlined) >= 1


# -----------------------------------------------------------------------
# CatalogEntry
# -----------------------------------------------------------------------

class TestCatalogEntry:
    def test_to_dict(self):
        acc = HeaderAccessor("h.h", "Foo", "Get", 1, "return mX;", is_const=True)
        entry = CatalogEntry(
            accessor=acc,
            status="outlined",
            fixed_functions=["Bar::Draw"],
            notes="test note",
        )
        d = entry.to_dict()
        assert d["status"] == "outlined"
        assert d["fixed_functions"] == ["Bar::Draw"]
        assert d["notes"] == "test note"
        assert d["method"] == "Get"
        assert d["is_const"] is True

    def test_default_status(self):
        acc = HeaderAccessor("h.h", "Foo", "Get", 1, "return mX;")
        entry = CatalogEntry(accessor=acc)
        assert entry.status == "candidate"
        d = entry.to_dict()
        assert "fixed_functions" not in d
        assert "notes" not in d


# -----------------------------------------------------------------------
# CLI argument parsing
# -----------------------------------------------------------------------

class TestCLI:
    def test_scan_headers_args(self):
        parser = _build_parser()
        args = parser.parse_args(["scan-headers", "--max-statements", "3", "--json"])
        assert args.command == "scan-headers"
        assert args.max_statements == 3
        assert args.json is True

    def test_catalog_args(self):
        parser = _build_parser()
        args = parser.parse_args(["catalog", "--json"])
        assert args.command == "catalog"
        assert args.json is True

    def test_check_args(self):
        parser = _build_parser()
        args = parser.parse_args(["check", "path/to/header.h"])
        assert args.command == "check"
        assert args.header == "path/to/header.h"

    def test_no_command(self):
        parser = _build_parser()
        args = parser.parse_args([])
        assert args.command is None


class TestMainEntrypoint:
    def test_no_args_returns_1(self):
        assert main([]) == 1

    def test_scan_headers_on_fixture(self, tmp_header_dir, capsys):
        src_dir = str(tmp_header_dir / "src" / "system")
        ret = main(["scan-headers", "--src-dir", src_dir])
        assert ret == 0
        captured = capsys.readouterr()
        assert "inline accessors" in captured.out

    def test_scan_headers_json(self, tmp_header_dir, capsys):
        src_dir = str(tmp_header_dir / "src" / "system")
        ret = main(["scan-headers", "--src-dir", src_dir, "--json"])
        assert ret == 0
        captured = capsys.readouterr()
        data = json.loads(captured.out)
        assert isinstance(data, list)
        assert len(data) > 0

    def test_check_on_fixture(self, tmp_header_dir, capsys):
        header = str(tmp_header_dir / "src" / "system" / "ui" / "Foo.h")
        ret = main(["check", header])
        assert ret == 0
        captured = capsys.readouterr()
        assert "Header:" in captured.out

    def test_catalog_on_fixture(self, tmp_header_dir, capsys):
        src_dir = str(tmp_header_dir / "src" / "system")
        ret = main(["catalog", "--src-dir", src_dir])
        assert ret == 0
        captured = capsys.readouterr()
        assert "Inlining Control Catalog" in captured.out


# -----------------------------------------------------------------------
# HeaderAccessor.to_dict
# -----------------------------------------------------------------------

class TestHeaderAccessorToDict:
    def test_to_dict_fields(self):
        a = HeaderAccessor(
            header="ui/Foo.h",
            class_name="Foo",
            method_name="GetVal",
            statement_count=1,
            body="return mVal;",
            is_const=True,
            return_type="float",
        )
        d = a.to_dict()
        assert d["header"] == "ui/Foo.h"
        assert d["class"] == "Foo"
        assert d["method"] == "GetVal"
        assert d["size"] == 1
        assert d["size_class"] == "trivial"
        assert d["body"] == "return mVal;"
        assert d["is_const"] is True
        assert d["return_type"] == "float"
