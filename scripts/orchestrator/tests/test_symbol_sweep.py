"""Regression tests for scripts/orchestrator/symbol_sweep.py.

Every test here pins a defect that a hand-rolled version of this sweep actually
had — measured, not imagined — while reproducing the published vtable count in
docs/analysis/dispatch-data-rescan-20260818.md:

  * the ICF alias map was not read, so the binary's most common benign fold
    (`OnlyReturns` @0x823e3b70) counted as a divergence and flow/ measured 184
    slots instead of 11;
  * undefined external COFF symbols were diffed and their "Symbol not found in
    target" answers counted as tool errors — 43% of the flow/ sweep;
  * objdiff's stderr banner was reported as the error text, so every failure
    looked identical;
  * a truncated run must not present a sample as a total.

They are hermetic: no objdiff-cli, no build, no database.
"""
import json
import sys
import types
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from orchestrator import symbol_sweep as S  # noqa: E402

#: Bound at import, BEFORE any fixture patches it -- the shape that makes a
#: restore mean something. Re-reading `S._load_coffx` after a patch would
#: compare the stub against itself.
_REAL_LOAD_COFFX = S._load_coffx


def tearDownModule():
    """No fixture in this file may leak its stub into the rest of the process.

    `TestEnumeration.setUp` replaced `S._load_coffx` with a lambda and
    for a while never put it back; `sys.modules["coffx"]` WAS restored, which
    made the teardown look symmetrical. Anything importing symbol_sweep later
    in the same pytest process then read the fake loader and would have kept
    passing had the real one broken.

    SABOTAGE: delete the `S._load_coffx = self._saved_loader` line in that
    tearDown; this goes red. Verified 2026-09-01 -- it does.
    """
    assert S._load_coffx is _REAL_LOAD_COFFX, (
        "a fixture in this file left symbol_sweep._load_coffx monkeypatched; "
        "every later test in this process is now reading a stub")


def _uncollected_testcases() -> set:
    """TestCase classes present in this FILE that the loader did not collect.

    The comparison is against the file's own SOURCE (via `ast`), not against
    `vars(module)`, and that is the whole point: a stray
    `if __name__ == "__main__": unittest.main()` placed mid-file stops
    execution before the later classes are ever *defined*, so `vars(module)`
    agrees with the loader and the check is vacuous exactly when it matters.
    The source text is there regardless of where execution stopped.

    Measured 2026-08-22: the guard sat at line 357 and `TestCoverageImplParity`
    at 361. `python3 test_symbol_sweep.py` printed "Ran 24 tests ... OK" and
    collected ZERO of that class's tests, while `python3 -m unittest` (which
    imports the module first) collected 2. The parity test written to prove the
    coverage shim and the shared CoverageReport are interchangeable had never
    executed -- and they were not interchangeable, in three separate methods.
    """
    import ast
    import sys as _sys
    src = Path(__file__).read_text()
    in_file = set()
    for node in ast.parse(src).body:
        if isinstance(node, ast.ClassDef) and any(
                (isinstance(b, ast.Attribute) and b.attr == "TestCase")
                or (isinstance(b, ast.Name) and b.id == "TestCase")
                for b in node.bases):
            in_file.add(node.name)
    collected = set()
    for suite in unittest.defaultTestLoader.loadTestsFromModule(
            _sys.modules[__name__]):
        for t in suite:
            collected.add(type(t).__name__)
    return in_file - collected


def _assert_collects_everything() -> None:
    """Hard-fail the RUN, not just one test, if classes went uncollected.

    Called from the `__main__` guard so it fires wherever that guard is: if
    somebody re-introduces the mid-file guard, the classes below it are not
    defined, `loadTestsFromModule` cannot see them, and this exits 6 instead of
    printing a green "Ran 24 tests ... OK". A check that lives only inside a
    test case cannot cover a guard placed above that test case.
    """
    missing = _uncollected_testcases()
    if missing:
        raise SystemExit(
            f"REFUSING to report a result: {len(missing)} TestCase class(es) "
            f"in this file were never collected -- {sorted(missing)}.\n"
            f"`unittest.main()` must be the LAST statement in the file; "
            f"anything defined after it never runs and the suite reports OK "
            f"over a smaller set than it appears to.")


MAP_TEXT = """\
 Address         Publics by Value              Rva+Base       Lib:Object

 0001:00000b58       ??_7FilePath@@6B@          82001158     App.obj
 0001:00001000       ?RefOwner@Object@Hmx@@UBAPAV12@XZ 823e3b70 f i char:Character.obj
 0001:00002000       ?Poll@RndPollable@@UAAXXZ  823e3b70 f i flow:Flow.obj
 0001:00003000       ?Enter@Flow@@UAAXXZ        82112233 f i flow:Flow.obj
 0001:00004000       ?Exit@Flow@@UAAXXZ         82445566 f i flow:Flow.obj
 0001:00000000 000005b4H .idata$5                DATA
"""

ICF_TEXT = """\
; SYNTHETIC ICF-alias map
; --- ICF group OnlyReturns@0x823e3b70 @ 823E3B70 ---
 0001:00000000       OnlyReturns                     823E3B70  f i icf_aliases.synthetic
; --- ICF group retailmap:merged_Thing @ 82331448 ---
 0001:00000000       merged_Thing                    82331448  f i icf_aliases.synthetic
"""


def _write_project(tmp: Path) -> Path:
    (tmp / "orig" / "373307D9").mkdir(parents=True)
    (tmp / "orig" / "373307D9" / "ham_xbox_r.map").write_text(MAP_TEXT)
    (tmp / "build" / "373307D9").mkdir(parents=True)
    (tmp / "build" / "373307D9" / "icf_aliases.map").write_text(ICF_TEXT)
    return tmp


class TestLinkerMapParsing(unittest.TestCase):
    def setUp(self):
        import tempfile
        self.tmp = Path(tempfile.mkdtemp())
        _write_project(self.tmp)

    def test_data_rows_without_flags_are_parsed(self):
        """`??_7...` rows carry no `f i` flag column.

        icf_pairing_bodytest.read_map()'s regex REQUIRES that column, which is
        why the vtable question needed its own reader: with that regex every
        vtable in the binary is invisible.
        """
        sym2addr, _ = S.parse_linker_map(self.tmp)
        self.assertEqual(sym2addr["??_7FilePath@@6B@"], "82001158")

    def test_icf_alias_names_resolve(self):
        """Without this merge, `OnlyReturns` is unresolvable and every fold
        against it reads as a wrong-target divergence."""
        sym2addr, stats = S.parse_linker_map(self.tmp)
        self.assertTrue(stats["icf_alias_map_present"])
        self.assertEqual(sym2addr["OnlyReturns"], "823e3b70")
        self.assertEqual(sym2addr["merged_Thing"], "82331448")

    def test_missing_icf_map_is_declared_not_assumed(self):
        (self.tmp / "build" / "373307D9" / "icf_aliases.map").unlink()
        sym2addr, stats = S.parse_linker_map(self.tmp)
        self.assertFalse(stats["icf_alias_map_present"])
        self.assertNotIn("OnlyReturns", sym2addr)

    def test_section_header_rows_are_counted_not_silently_skipped(self):
        _, stats = S.parse_linker_map(self.tmp)
        self.assertEqual(stats["rows_rowlike_unparsed"], 1)


class TestAdjudication(unittest.TestCase):
    def setUp(self):
        self.sym2addr = {
            "OnlyReturns": "823e3b70",
            "?RefOwner@Object@Hmx@@UBAPAV12@XZ": "823e3b70",
            "?Poll@RndPollable@@UAAXXZ": "823e3b70",
            "?Enter@Flow@@UAAXXZ": "82112233",
            "?Exit@Flow@@UAAXXZ": "82445566",
        }

    def _adj(self, relocs):
        return S.adjudicate_relocations({"relocations": relocs}, self.sym2addr)

    def test_equal_rows_are_never_kept(self):
        self.assertEqual(
            self._adj([{"offset": 0, "kind": "equal", "target_symbol": "?Enter@Flow@@UAAXXZ"}]),
            [],
        )

    def test_same_address_is_a_proven_icf_fold_and_benign(self):
        """The single most common row in the binary: the target names the ICF
        representative, we name our real method, both live at one address."""
        rows = self._adj([{
            "offset": 4, "kind": "replace",
            "target_symbol": "OnlyReturns",
            "base_target_symbol": "?RefOwner@Object@Hmx@@UBAPAV12@XZ",
        }])
        self.assertEqual(rows, [])

    def test_different_addresses_is_a_wrong_target(self):
        rows = self._adj([{
            "offset": 8, "kind": "replace",
            "target_symbol": "?Enter@Flow@@UAAXXZ",
            "base_target_symbol": "?Exit@Flow@@UAAXXZ",
        }])
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["class"], "wrong-target")

    def test_absent_base_target_symbol_means_same_symbol_both_sides(self):
        """objdiff emits base_target_symbol ONLY when it differs. Treating a
        blank as 'the base has nothing' invents divergences on every slot."""
        rows = self._adj([{
            "offset": 12, "kind": "replace",
            "target_symbol": "?Enter@Flow@@UAAXXZ",
        }])
        self.assertEqual(rows, [])

    def test_base_only_and_target_only_are_a_separate_tier(self):
        rows = self._adj([
            {"offset": 44, "kind": "insert", "target_symbol": "",
             "base_target_symbol": "?Exit@Flow@@UAAXXZ"},
            {"offset": 48, "kind": "delete", "target_symbol": "??_R4Flow@@6B@"},
        ])
        self.assertEqual({r["class"] for r in rows}, {"base-only", "target-only"})

    def test_unresolvable_side_is_labelled_not_asserted(self):
        rows = self._adj([{
            "offset": 16, "kind": "replace",
            "target_symbol": "merged_NeverSeen",
            "base_target_symbol": "?Enter@Flow@@UAAXXZ",
        }])
        self.assertEqual(rows[0]["class"], "unresolved-target")


class TestVtableClassName(unittest.TestCase):
    def test_plain(self):
        self.assertEqual(S.vtable_class_name("??_7Flow@@6BRndPollable@@@"), "Flow")

    def test_template(self):
        self.assertEqual(
            S.vtable_class_name("??_7?$ObjPtrVec@VFlowLabel@@VObjectDir@@@@6B@"),
            "?$ObjPtrVec@VFlowLabel@@VObjectDir@@",
        )


class _FakeSym:
    def __init__(self, name, sec):
        self.name = name
        self.sec = sec


class TestEnumeration(unittest.TestCase):
    """The universe must include, and separately count, undefined externals."""

    def setUp(self):
        import tempfile
        self.tmp = Path(tempfile.mkdtemp())
        objdir = self.tmp / "build" / "373307D9" / "obj"
        objdir.mkdir(parents=True)
        (objdir / "A.obj").write_bytes(b"x")
        (objdir / "B.obj").write_bytes(b"x")
        (self.tmp / "objdiff.json").write_text(json.dumps({"units": [
            {"name": "default/A", "target_path": "build/373307D9/obj/A.obj"},
            {"name": "default/B", "target_path": "build/373307D9/obj/B.obj"},
            {"name": "default/gone", "target_path": "build/373307D9/obj/gone.obj"},
        ]}))
        fake = types.ModuleType("coffx")
        table = {
            b"x": [
                _FakeSym("??_7Defined@@6B@", 7),
                _FakeSym("??_7Referenced@@6B@", 0),   # UNDEFINED external
                _FakeSym("?NotAVtable@@YAXXZ", 3),
            ]
        }
        fake.read_coff = lambda data: (None, table[data])  # noqa: E731
        # BIND BEFORE PATCHING, both of them. `_load_coffx` used to be replaced
        # here and never restored, so the fake loader leaked into every later
        # test in the same process -- a test that had nothing to do with coffx
        # would still be reading this stub, and would keep passing if the real
        # loader broke. Restoring only the `sys.modules` half looked complete
        # because the visible entry was symmetrical.
        self._saved_module = sys.modules.get("coffx")
        self._saved_loader = S._load_coffx
        sys.modules["coffx"] = fake
        S._load_coffx = lambda: fake

    def tearDown(self):
        S._load_coffx = self._saved_loader
        if self._saved_module is not None:
            sys.modules["coffx"] = self._saved_module
        else:
            sys.modules.pop("coffx", None)

    def test_undefined_externals_are_dropped_but_counted(self):
        pairs, stats = S.enumerate_target_symbols(self.tmp, "??_7*", "*")
        self.assertEqual([p[1] for p in pairs], ["??_7Defined@@6B@"] * 2)
        self.assertEqual(stats["matched"], 4)          # 2 units x 2 vtables
        self.assertEqual(stats["undefined_external"], 2)
        # universe == kept + dropped: the arithmetic that makes a denominator
        # checkable rather than decorative.
        self.assertEqual(stats["matched"], len(pairs) + stats["undefined_external"])

    def test_missing_target_object_is_counted(self):
        _, stats = S.enumerate_target_symbols(self.tmp, "??_7*", "*")
        self.assertEqual(stats["units_missing_object"], 1)
        self.assertEqual(stats["units_selected"], 3)

    def test_unit_glob_restricts(self):
        pairs, stats = S.enumerate_target_symbols(self.tmp, "??_7*", "default/A")
        self.assertEqual(len(pairs), 1)
        self.assertEqual(stats["units_selected"], 1)


class TestCoverageContract(unittest.TestCase):
    def test_clean_run_accounts_for_every_row(self):
        cov = S.make_coverage("t")
        cov.universe(10, "things")
        for _ in range(7):
            cov.examine()
        cov.drop("reason-a", 3)
        d = cov.as_dict()
        self.assertEqual(d["unaccounted"], 0)
        self.assertFalse(d["truncated"])
        self.assertTrue(d["complete"])

    def test_unaccounted_rows_are_surfaced(self):
        cov = S.make_coverage("t")
        cov.universe(10, "things")
        cov.examine()
        d = cov.as_dict()
        self.assertEqual(d["unaccounted"], 9)
        self.assertFalse(d["complete"])

    def test_truncation_is_never_presented_as_a_total(self):
        cov = S.make_coverage("t")
        cov.universe(18549, "symbols")
        cov.cap("--max-symbols", 4000, before=18549, after=4000)
        for _ in range(4000):
            cov.examine()
        d = cov.as_dict()
        self.assertTrue(d["truncated"])
        self.assertFalse(d["complete"])
        self.assertIn("TRUNCATED", cov.render())
        self.assertIn("18549", cov.render())

    def test_render_states_the_denominator(self):
        cov = S.make_coverage("t")
        cov.universe(100, "widgets")
        for _ in range(40):
            cov.examine()
        cov.drop("nope", 60)
        text = cov.render()
        self.assertIn("40/100", text)
        self.assertIn("nope", text)


class TestErrorExtraction(unittest.TestCase):
    def test_stderr_banner_is_not_reported_as_the_error(self):
        """objdiff prints 'Loaded N ICF equivalence entries' on EVERY run."""
        import subprocess
        from unittest import mock

        proc = subprocess.CompletedProcess(
            args=[], returncode=1,
            stdout="",
            stderr=("Loaded 8719 ICF equivalence entries from ./build/373307D9/icf_aliases.map\n"
                    "Failed: Symbol not found in target: ??_7ObjRef@@6B@\n"),
        )
        with mock.patch("subprocess.run", return_value=proc):
            with self.assertRaises(RuntimeError) as ctx:
                S.diff_symbol("/nowhere", "u", "??_7ObjRef@@6B@")
        self.assertIn("Symbol not found in target", str(ctx.exception))
        self.assertNotIn("ICF equivalence entries", str(ctx.exception))


class TestRenderMarkdown(unittest.TestCase):
    def test_coverage_block_leads_the_report(self):
        cov = S.make_coverage("x")
        cov.universe(3, "syms")
        cov.examine(3)
        result = {
            "kind": "vtable_slots",
            "divergent_slots": 1,
            "length_findings": 0,
            "divergent_rows_before_dedup": 1,
            "by_class": {"Flow": 1},
            "by_finding_class": {"wrong-target": 1},
            "slots": [{"class_name": "Flow", "offset": 8, "class": "wrong-target",
                       "target_symbol": "?A@@", "base_target_symbol": "?B@@"}],
            "length": [],
            "errors": [], "error_count": 0,
            "_coverage": cov.as_dict(), "_coverage_render": cov.render(),
        }
        md = S.render_markdown(result)
        self.assertTrue(md.startswith("="))
        self.assertIn("COVERAGE", md.splitlines()[1])
        self.assertIn("Divergent slots: 1", md)
        self.assertIn("0x8", md)


class TestBatchFunctionSweep(unittest.TestCase):
    """objdiff emits {"error":"not_found","symbol":...} as a normal JSONL row.

    Counting that as `examined` puts an unmeasurable symbol into the numerator
    of the coverage fraction -- the exact shape of the six instrument defects
    this project found in one week.
    """

    def _run(self, stdout):
        import subprocess
        from unittest import mock
        proc = subprocess.CompletedProcess(args=[], returncode=0, stdout=stdout, stderr="")
        with mock.patch("subprocess.run", return_value=proc):
            return S.sweep_functions("/nowhere", ["?A@@YAXXZ", "?B@@YAXXZ", "?C@@YAXXZ"])

    def test_error_rows_are_dropped_not_examined(self):
        out = self._run(
            '{"symbol":"?A@@YAXXZ","normalized_match_percent":90.0}\n'
            '{"error":"not_found","symbol":"?B@@YAXXZ"}\n'
            '{"symbol":"?C@@YAXXZ","normalized_match_percent":100.0}\n'
        )
        cov = out["_coverage"]
        self.assertEqual(cov["universe"], 3)
        self.assertEqual(cov["examined"], 2)
        self.assertEqual(cov["dropped"], {"objdiff-not_found": 1})
        self.assertEqual(cov["unaccounted"], 0)
        self.assertEqual(out["errored_count"], 1)
        self.assertEqual(len(out["rows"]), 2)

    def test_symbols_with_no_row_at_all_are_counted(self):
        out = self._run('{"symbol":"?A@@YAXXZ","normalized_match_percent":90.0}\n')
        self.assertEqual(out["missing_count"], 2)
        self.assertEqual(out["_coverage"]["unaccounted"], 0)

    def test_truncation_is_declared(self):
        import subprocess
        from unittest import mock
        proc = subprocess.CompletedProcess(args=[], returncode=0, stdout="", stderr="")
        with mock.patch("subprocess.run", return_value=proc):
            out = S.sweep_functions("/nowhere", ["a", "b", "c"], max_symbols=1)
        self.assertTrue(out["_coverage"]["truncated"])


class TestCoverageImplParity(unittest.TestCase):
    """The shim and the shared CoverageReport must be interchangeable.

    make_coverage() hands a caller whichever is importable, so a divergence in
    cap()'s signature or its drop bookkeeping is a crash or a double-count at a
    production call site, not a graceful fallback.

    ⚠ THIS CLASS DID NOT RUN. `if __name__ == "__main__": unittest.main()` was
    at line 357 and this class started at line 361 -- Python executes top to
    bottom, so running the file the documented way collected the nine classes
    above it and reported "Ran 24 tests ... OK" while contributing zero from
    here. Measured 2026-08-22: `python3 test_symbol_sweep.py -v | grep -c
    CoverageImplParity` -> 0; `python3 -m unittest ...` (discovery, which
    imports the whole module first) -> 2. A parity test that cannot run.

    What it would have caught, had it also covered `extra()`: the shim was
    `extra(self, **kw)` and the shared class `extra(self, key, value)`, so
    `run_symbol_sweep(kind='vtable_slots')` -- the whole-binary vtable
    adjudication CLAUDE.md advertises -- raised TypeError on every call.
    `test_every_public_method_has_a_matching_signature` below now grades the
    whole API rather than the one method someone thought to test.
    """

    def _both(self):
        impls = [S._LocalCoverage("t")]
        if S._SHARED_COVERAGE and S._SharedCoverageReport is not None:
            impls.append(S._SharedCoverageReport("t"))
        return impls

    def test_cap_agrees_across_implementations(self):
        seen = []
        for cov in self._both():
            cov.universe(18549, "symbols")
            cov.cap("--max-symbols", 4000, before=18549, after=4000)
            for _ in range(4000):
                cov.examine()
            d = cov.as_dict()
            seen.append((d["truncated"], d["complete"], cov.dropped_total))
            self.assertTrue(d["truncated"], type(cov).__name__)
        self.assertEqual(len(set(seen)), 1, f"implementations disagree: {seen}")

    def test_cap_counts_the_drop_exactly_once(self):
        for cov in self._both():
            cov.universe(18549, "symbols")
            cov.cap("--max-symbols", 4000, before=18549, after=4000)
            self.assertEqual(cov.dropped_total, 14549, type(cov).__name__)

    def test_every_public_method_has_a_matching_signature(self):
        """Grade the WHOLE API, not the one method someone remembered.

        Testing `cap()` by hand is how `extra()` drifted unnoticed. This
        enumerates `S.COVERAGE_API` so a method added to one side and not the
        other is RED without anybody writing a new test.
        """
        if not (S._SHARED_COVERAGE and S._SharedCoverageReport is not None):
            self.skipTest("shared CoverageReport not importable")
        shared, local = S._SharedCoverageReport("t"), S._LocalCoverage("t")
        mismatched = {
            m: (S._api_signature(shared, m), S._api_signature(local, m))
            for m in S.COVERAGE_API
            if S._api_signature(shared, m) != S._api_signature(local, m)
        }
        self.assertEqual(mismatched, {},
                         f"coverage API drift (shared, local): {mismatched}")

    def test_extra_accepts_the_production_call_form(self):
        """The exact call `sweep_data_symbols` makes, against both impls.

        Negative control for the test above: a signature comparison that was
        somehow vacuous would still let this one fail.
        """
        for cov in self._both():
            cov.extra("divergent_slots", 7)
            self.assertEqual(cov.as_dict()["divergent_slots"], 7,
                             type(cov).__name__)

    def test_make_coverage_returns_something_usable(self):
        """Whatever make_coverage hands back must survive the production calls.

        `make_coverage`'s fallback used to wrap only the CONSTRUCTOR, so drift
        in a METHOD was not caught and surfaced 500 lines later as a crashed
        sweep.
        """
        cov = S.make_coverage("t")
        cov.universe(3, "symbols")
        cov.note("ruler: name_check")
        cov.extra("divergent_slots", 1)
        cov.examine(1)
        cov.drop("no-object", 2)
        self.assertEqual(cov.as_dict()["divergent_slots"], 1)


class TestThisFileCollectsEveryTestCase(unittest.TestCase):
    """A test file cannot be trusted to report on classes it never loaded.

    The negative control for the `unittest.main()`-placement bug above: compare
    what the default loader collects against every TestCase subclass actually
    defined in this module. If someone re-introduces a class after the
    `__main__` guard -- or the guard drifts back up -- the counts diverge and
    this is RED, without anyone having to notice the line numbers.
    """

    def test_loader_sees_every_testcase_defined_here(self):
        self.assertEqual(_uncollected_testcases(), set(),
                         "TestCase classes defined but not collected -- is "
                         "`if __name__ == \"__main__\"` above them?")


if __name__ == "__main__":
    _assert_collects_everything()
    unittest.main()
