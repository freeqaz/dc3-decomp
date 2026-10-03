"""Unit tests for the Xenia-golden comparator (no engine, no Xenia).

Fixtures are synthetic on purpose: the recorded goldens are game-derived and
live only in the gitignored archive/, so no value here is copied from one.

    python3 tools/state_diff/tests/test_golden.py
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from state_diff import golden as G  # noqa: E402
from state_diff import editor_schema as ES  # noqa: E402

M = "m.milo"


def cap(objs):
    return {"milos": {M: {"objects": objs}}}


class Classify(unittest.TestCase):
    def test_equal(self):
        self.assertIsNone(G.classify("1:0.5", "1:0.5"))

    def test_crt_exponent_is_tolerated_not_equal(self):
        self.assertEqual(G.classify("1:3.5e-007", "1:3.5e-07"), "crt_exponent")

    def test_crt_tie_round_same_float32(self):
        self.assertEqual(G.classify("1:1234.56787", "1:1234.56786"), "crt_round")

    def test_real_float_difference_fails(self):
        self.assertEqual(G.classify("1:-90", "1:0"), "float_value")

    def test_ulp_level_difference(self):
        self.assertEqual(G.classify("1:2.5", "1:2.50000025"), "fp_eval")

    def test_type_change(self):
        self.assertEqual(G.classify("0:1", "1:1"), "value_type")


class Compare(unittest.TestCase):
    golden = {M: {"a": {"_class": "CharClip", "size": "0:100", "x": "1:1"}}}

    def test_open_row_fails(self):
        res = G.compare(self.golden, cap({"a": {"_class": "CharClip", "size": "0:100",
                                                "x": "1:2"}}), [])
        self.assertEqual(res["open"], 1)

    def test_int_delta_waiver_is_narrow(self):
        adj = [{"id": "w", "verdict": "d", "class": "CharClip", "field": "size",
                "rule": "int_delta", "delta": 292}]
        ok = G.compare(self.golden, cap({"a": {"_class": "CharClip", "size": "0:392",
                                               "x": "1:1"}}), adj)
        self.assertEqual(ok["open"], 0)
        bad = G.compare(self.golden, cap({"a": {"_class": "CharClip", "size": "0:393",
                                                "x": "1:1"}}), adj)
        self.assertEqual(bad["open"], 1)

    def test_multiset_same_members_any_order(self):
        g = {M: {"d": {"_class": "RndDir", "draws/#": "2", "draws/0": "4:a", "draws/1": "4:b"}}}
        adj = [{"id": "o", "verdict": "d", "field": "draws/*", "rule": "multiset",
                "array": "draws"}]
        swapped = cap({"d": {"_class": "RndDir", "draws/#": "2", "draws/0": "4:b",
                             "draws/1": "4:a"}})
        self.assertEqual(G.compare(g, swapped, adj)["open"], 0)
        other = cap({"d": {"_class": "RndDir", "draws/#": "2", "draws/0": "4:b",
                           "draws/1": "4:c"}})
        self.assertGreater(G.compare(g, other, adj)["open"], 0)

    def test_extra_object_is_a_disagreement(self):
        res = G.compare(self.golden, cap({"a": dict(self.golden[M]["a"]),
                                          "z": {"_class": "Mesh"}}), [])
        self.assertEqual(res["open"], 1)

    def test_missing_field_fails(self):
        res = G.compare(self.golden, cap({"a": {"_class": "CharClip", "size": "0:100"}}), [])
        self.assertEqual(res["open"], 1)


class Refusal(unittest.TestCase):
    def test_perturbed_loader_subsystem_refused(self):
        with self.assertRaises(SystemExit):
            G.refuse_if_perturbed({"name": "x", "perturbed_subsystems": ["propsync"],
                                   "active_hacks": []})

    def test_unknown_hack_refused(self):
        with self.assertRaises(SystemExit):
            G.refuse_if_perturbed({"name": "x", "perturbed_subsystems": [],
                                   "active_hacks": ["io.mystery"]})

    def test_known_hacks_accepted(self):
        G.refuse_if_perturbed({"name": "x", "perturbed_subsystems": ["kinect_nui"],
                               "active_hacks": ["nui.NuiInitialize", "calib.nav_data"]})


class Schema(unittest.TestCase):
    def test_color_struct_reads_packed_leaf(self):
        out = []
        ES.leaves_for_type(["struct", ["r", "float"], ["g", "float"], ["b", "float"]],
                           ("color",), out)
        self.assertEqual([(l.path, l.kind) for l in out], [(("color",), "int")])

    def test_array_becomes_size_leaf(self):
        out = []
        ES.leaves_for_type(["array", "object"], ("draws",), out)
        self.assertEqual(out[0].kind, "size")


if __name__ == "__main__":
    unittest.main()
