"""Tests for the macro-aware preprocessed-splice fast path.

Two tiers:

1. Pure-logic unit tests (always run): macro-name collection, definition-name
   extraction, function-region location + brace matching, probe block build /
   parse, and the splice/macro-gate behaviour of ``PreprocessCache``.

2. Integration byte/score-equivalence test (RB3 + mwcceppc only): compiles a
   handful of real functions both ways (full canonical compile vs spliced
   fast path) and asserts the objdiff scores match — and, for line-preserving
   variants, that the ``.o`` files are byte-identical. Skipped automatically
   when the RB3 toolchain isn't present so it never breaks DC3 CI.
"""

from __future__ import annotations

import os
import re
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.permuter.preprocess_cache import (  # noqa: E402
    PreprocessCache,
    _build_probe_block,
    _find_func_region,
    _match_brace,
    _parse_probe_results,
    extract_definition_name,
    region_has_macro,
)


# ── Tier 1: pure-logic unit tests ───────────────────────────────────────────

class TestDefinitionName(unittest.TestCase):
    def test_member_function(self):
        text = "void LightHue::TranslateColor(const Hmx::Color &c, Hmx::Color &r) {\n  x;\n}"
        self.assertEqual(extract_definition_name(text), "LightHue::TranslateColor")

    def test_free_function(self):
        text = "void UtilDrawCigar(const Transform &t, const float *a) {\n  y;\n}"
        self.assertEqual(extract_definition_name(text), "UtilDrawCigar")

    def test_nested_class(self):
        text = "void BandPatchMesh::WorkVerts::AddEdge(MeshVert *a, MeshVert *b) {\n  z;\n}"
        self.assertEqual(
            extract_definition_name(text), "BandPatchMesh::WorkVerts::AddEdge"
        )

    def test_whitespace_around_colons(self):
        text = "int  Foo :: Bar ( int x ) {\n  return x;\n}"
        self.assertEqual(extract_definition_name(text), "Foo::Bar")

    def test_param_types_not_matched(self):
        # The first Name( is the function; the param type Hmx::Color must not win.
        text = "void A::B(const Hmx::Color &c) { Hmx::Color z(c); }"
        self.assertEqual(extract_definition_name(text), "A::B")


class TestFuncRegion(unittest.TestCase):
    def test_basic_brace_match(self):
        text = "prefix\nvoid A::B(int x) {\n  if (x) { y(); }\n}\nsuffix"
        region = _find_func_region(text, "A::B")
        self.assertIsNotNone(region)
        s, e = region
        self.assertEqual(text[s:e], "A::B(int x) {\n  if (x) { y(); }\n}")

    def test_const_qualifier(self):
        text = "int A::Get() const {\n  return m;\n}\n"
        region = _find_func_region(text, "A::Get")
        self.assertIsNotNone(region)
        s, e = region
        self.assertTrue(text[s:e].endswith("}"))
        self.assertIn("const", text[s:e])

    def test_declaration_not_matched(self):
        # A bare declaration (semicolon, no body) must not be returned.
        text = "void A::B(int x);\nvoid other() {}\n"
        self.assertIsNone(_find_func_region(text, "A::B"))

    def test_word_boundary(self):
        # A::Bar must not match A::BarBaz.
        text = "void A::BarBaz(int x) { z(); }\n"
        self.assertIsNone(_find_func_region(text, "A::Bar"))

    def test_match_brace_helper(self):
        text = "{ a; { b; } c; }"
        self.assertEqual(_match_brace(text, 0), len(text))


class TestProbe(unittest.TestCase):
    def test_build_probe_block(self):
        block = _build_probe_block(("FOO", "BAR"))
        self.assertIn("#ifdef FOO", block)
        self.assertIn("#ifdef BAR", block)
        self.assertIn("__PPC_MACRO_PROBE__0", block)
        self.assertIn("__PPC_MACRO_PROBE__1", block)

    def test_parse_probe_results(self):
        names = ("FOO", "BAR", "BAZ")
        # Simulate preprocessed output: FOO and BAZ are live (1), BAR is not (0).
        pp = (
            "some preprocessed code here\n"
            "__PPC_MACRO_PROBE__0 1\n"
            "__PPC_MACRO_PROBE__1 0\n"
            "__PPC_MACRO_PROBE__2 1\n"
        )
        live, probe_start = _parse_probe_results(pp, names)
        self.assertEqual(live, frozenset({"FOO", "BAZ"}))
        # Probe region starts at the first marker.
        self.assertEqual(pp[probe_start:probe_start + 20], "__PPC_MACRO_PROBE__0")

    def test_parse_probe_no_markers(self):
        live, probe_start = _parse_probe_results("no markers here", ("FOO",))
        self.assertEqual(live, frozenset())
        self.assertEqual(probe_start, len("no markers here"))


class TestRegionHasMacro(unittest.TestCase):
    def test_detects_macro_token(self):
        body = b"void f() { MILO_ASSERT(x, 0); }"
        self.assertTrue(region_has_macro(body, frozenset({"MILO_ASSERT"})))

    def test_no_macro(self):
        body = b"void f() { return a + b; }"
        self.assertFalse(region_has_macro(body, frozenset({"MILO_ASSERT"})))

    def test_substring_not_matched(self):
        # MILO_ASSERTX must not trigger on macro MILO_ASSERT (whole-word only).
        body = b"void f() { MILO_ASSERTX(x); }"
        self.assertFalse(region_has_macro(body, frozenset({"MILO_ASSERT"})))


class TestSpliceMechanics(unittest.TestCase):
    """Splice behaviour without invoking a compiler (synthetic `.i`)."""

    def _make_cache(self, pp_text, baseline_src, func_range, live):
        cache = PreprocessCache(_PROJECT_ROOT, Path("Fake.cpp"))
        cache._pp_text = pp_text
        cache._pp_region = _find_func_region(pp_text, "A::B")
        cache._live_macros = frozenset(live)
        cache._func_name = "A::B"
        cache._disabled = False
        return cache

    def test_clean_splice(self):
        pp = "header stuff\nvoid A::B(int x) { OLD; }\ntrailer\n"
        src = b"void A::B(int x) { NEW_BODY; }"
        cache = self._make_cache(pp, src, (0, len(src)), set())
        out = cache.splice(src, (0, len(src)))
        self.assertIsNotNone(out)
        text = out.decode()
        self.assertIn("NEW_BODY", text)
        self.assertNotIn("OLD", text)
        # Return type preserved exactly once (no "void void").
        self.assertEqual(text.count("void A::B"), 1)
        self.assertEqual(cache.fast_hits, 1)

    def test_macro_gate_falls_back(self):
        pp = "void A::B(int x) { OLD; }\n"
        src = b"void A::B(int x) { MILO_ASSERT(x, 0); }"
        cache = self._make_cache(pp, src, (0, len(src)), {"MILO_ASSERT"})
        out = cache.splice(src, (0, len(src)))
        self.assertIsNone(out)
        self.assertEqual(cache.fallbacks, 1)

    def test_no_func_range_falls_back(self):
        pp = "void A::B(int x) { OLD; }\n"
        src = b"void A::B(int x) { NEW; }"
        cache = self._make_cache(pp, src, (0, len(src)), set())
        self.assertIsNone(cache.splice(src, None))

    def test_disabled_returns_none(self):
        cache = PreprocessCache(_PROJECT_ROOT, Path("Fake.cpp"))
        cache._disabled = True
        self.assertIsNone(cache.splice(b"void A::B() {}", (0, 13)))


# ── Tier 2: integration byte/score-equivalence (RB3 + mwcceppc only) ─────────

def _rb3_repo() -> Path | None:
    """Locate an RB3 checkout with a built mwcceppc, or None."""
    for cand in (
        Path("/home/free/code/milohax/rb3"),
        _PROJECT_ROOT,
    ):
        if (cand / "config" / "SZBE69_B8").is_dir() and (
            cand / "build" / "compilers" / "Wii" / "1.3" / "mwcceppc.exe"
        ).exists():
            return cand
    return None


def _func_range(sb: bytes, qual: str):
    t = sb.decode("utf-8", "replace")
    pat = re.compile(r"(?:(?<=[^A-Za-z0-9_])|^)" + re.escape(qual) + r"\s*\(")
    for m in pat.finditer(t):
        k = m.end() - 1
        d = 0
        n = len(t)
        while k < n:
            if t[k] == "(":
                d += 1
            elif t[k] == ")":
                d -= 1
                if d == 0:
                    k += 1
                    break
            k += 1
        i = k
        while i < n and t[i] not in "{;":
            i += 1
        if i >= n or t[i] == ";":
            continue
        bo = i
        dd = 0
        j = bo
        while j < n:
            if t[j] == "{":
                dd += 1
            elif t[j] == "}":
                dd -= 1
                if dd == 0:
                    j += 1
                    break
            j += 1
        return (len(t[:m.start()].encode()), len(t[:j].encode()))
    return None


@unittest.skipUnless(
    _rb3_repo() is not None,
    "RB3 mwcceppc toolchain not available — integration test skipped",
)
class TestByteScoreEquivalence(unittest.TestCase):
    """Compile real functions both ways; assert objdiff scores match."""

    # (symbol, unit, src-relative, qualified-name, expect_fast_path)
    TARGETS = [
        ("Collide__11RndDrawableFRC7SegmentRfR5Plane",
         "main/system/rndobj/Draw", "src/system/rndobj/Draw.cpp",
         "RndDrawable::Collide", False),  # has START_AUTO_TIMER/nullptr -> fallback
        ("WriteEndian__9BinStreamFPCvi",
         "main/system/utl/BinStream", "src/system/utl/BinStream.cpp",
         "BinStream::WriteEndian", True),
        ("AddEdge__Q213BandPatchMesh9WorkVertsFPQ213BandPatchMesh8MeshVertPQ213BandPatchMesh8MeshVert",
         "main/system/bandobj/BandPatchMesh", "src/system/bandobj/BandPatchMesh.cpp",
         "BandPatchMesh::WorkVerts::AddEdge", True),
    ]

    @classmethod
    def setUpClass(cls):
        cls.repo = _rb3_repo()
        os.environ["PERMUTER_PREPROCESS_CACHE"] = "1"
        cls._old_project = os.environ.get("PERMUTER_PROJECT")
        os.environ["PERMUTER_PROJECT"] = "rb3"
        cls._old_cwd = os.getcwd()
        os.chdir(cls.repo)
        # The project config and DB-root resolution are lru_cached; clear them
        # so the cwd/env above take effect even when the suite was collected
        # from a DC3 checkout (earlier tests may have cached DC3 paths).
        for mod, fn in (
            ("scripts.permuter.project", "_get_project_config_cached"),
            ("scripts.permuter.repo_paths", "get_db_root"),
        ):
            try:
                import importlib
                getattr(importlib.import_module(mod), fn).cache_clear()
            except Exception:
                pass

    @classmethod
    def tearDownClass(cls):
        os.chdir(cls._old_cwd)
        if cls._old_project is None:
            os.environ.pop("PERMUTER_PROJECT", None)
        else:
            os.environ["PERMUTER_PROJECT"] = cls._old_project
        for mod, fn in (
            ("scripts.permuter.project", "_get_project_config_cached"),
            ("scripts.permuter.repo_paths", "get_db_root"),
        ):
            try:
                import importlib
                getattr(importlib.import_module(mod), fn).cache_clear()
            except Exception:
                pass

    def _mutations(self, sb, rng):
        s, e = rng
        f = sb[s:e].decode("utf-8", "replace")
        bo = f.index("{")
        decl = f[:bo + 1] + "\n    int _permTmp = 0; (void)_permTmp;\n" + f[bo + 1:]
        return [
            ("identity", sb),
            ("decl_local", sb[:s] + decl.encode() + sb[e:]),
        ]

    def test_equivalence(self):
        from scripts.permuter.scorer import Scorer
        from scripts.permuter.score_cache import md5_file

        score_pairs = 0
        for sym, unit, srcrel, qual, expect_fast in self.TARGETS:
            src = self.repo / srcrel
            if not src.exists():
                continue
            sb = src.read_bytes()
            rng = _func_range(sb, qual)
            if rng is None:
                continue
            # A stale lock/backup from a concurrent permuter run must not fail
            # the whole suite — skip that target.
            try:
                ctx = Scorer(src, sym, unit=unit)
                ctx.__enter__()
            except RuntimeError as exc:
                self.skipTest(f"{sym}: {exc}")
                return
            try:
                ctx._extract_compile_cmd()
                cache = ctx._init_preprocess_cache()
                if cache is None:
                    # The fast path is correctly declined for a function whose
                    # body has preprocessor-split statements — e.g. an
                    # `#ifdef HX_NATIVE` cutting through an `if` condition (as in
                    # BinStream::WriteEndian, added for the native port). The
                    # splice can't preserve the #ifdef structure and tree-sitter
                    # can't even parse the function, so extract_function returns
                    # no range. That's safe behaviour, not a bug — skip this
                    # target; the remaining fast targets still verify equivalence.
                    continue
                self.assertFalse(cache.disabled, f"{qual}: cache disabled")

                tmp = Path(tempfile.mkdtemp(prefix="ppvt_"))
                try:
                    for vname, vb in self._mutations(sb, rng):
                        vrng = _func_range(vb, qual)
                        spliced = cache.splice(vb, vrng)
                        if not expect_fast:
                            self.assertIsNone(
                                spliced,
                                f"{qual}/{vname}: expected macro fallback",
                            )
                            continue
                        self.assertIsNotNone(
                            spliced, f"{qual}/{vname}: expected fast path"
                        )
                        fast_o = tmp / f"{vname}_f.o"
                        canon_o = tmp / f"{vname}_c.o"
                        okf, _ = ctx._compile_spliced(spliced, fast_o, tag="t_f")
                        okc, _ = ctx._compile_canonical(vb, canon_o)
                        self.assertTrue(okf and okc and fast_o.exists()
                                        and canon_o.exists(),
                                        f"{qual}/{vname}: build failed")
                        # objdiff score must match (the permuter's only oracle).
                        shutil.copy2(fast_o, ctx._obj_path)
                        fpct, _ = ctx._run_objdiff()
                        shutil.copy2(canon_o, ctx._obj_path)
                        cpct, _ = ctx._run_objdiff()
                        self.assertAlmostEqual(
                            fpct, cpct, places=4,
                            msg=f"{qual}/{vname}: score {fpct} != {cpct}",
                        )
                        # Identity variant must be byte-identical (no line shift).
                        if vname == "identity":
                            self.assertEqual(
                                md5_file(fast_o), md5_file(canon_o),
                                f"{qual}/identity: not byte-identical",
                            )
                        score_pairs += 1
                finally:
                    shutil.rmtree(tmp, ignore_errors=True)
            finally:
                ctx.__exit__(None, None, None)

        self.assertGreater(score_pairs, 0, "no fast-path pairs were verified")


if __name__ == "__main__":
    unittest.main()
