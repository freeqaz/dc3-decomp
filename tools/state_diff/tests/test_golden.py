"""Unit tests for the Xenia-golden comparator (no engine, no Xenia).

Fixtures are synthetic on purpose: the recorded goldens are game-derived and
live only in the gitignored archive/, so no value here is copied from one.

    python3 tools/state_diff/tests/test_golden.py
"""
import os
import socket
import stat
import sys
import tempfile
import textwrap
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


#: A stand-in for dc3-native's HTTP side: binds DC3_HTTP_PORT exactly the way
#: HttpServer::Start does (0 -> kernel-assigned; explicit port taken -> the
#: same FATAL line and abort rc), prints the DC3_HTTP_PORT= stdout line, and
#: answers /api/dta/eval for the two probes NativeBoot.wait_ready sends.
#: FAKE_MODE=old reproduces a pre-fix binary: "0" is read as FAKE_OLD_PORT.
#: FAKE_MODE=crash prints a line to stderr and exits 1 before binding.
FAKE_DC3 = textwrap.dedent("""\
    #!/usr/bin/env python3
    import http.server, json, os, sys
    mode = os.environ.get("FAKE_MODE", "new")
    if mode == "crash":
        print("boot exploded: FAKE_CRASH_MARKER", file=sys.stderr, flush=True)
        sys.exit(1)
    spec = os.environ.get("DC3_HTTP_PORT", "9090")
    port = int(spec)
    if mode == "old" and port == 0:
        port = int(os.environ["FAKE_OLD_PORT"])
    class H(http.server.BaseHTTPRequestHandler):
        def log_message(self, *a): pass
        def do_POST(self):
            expr = self.rfile.read(int(self.headers["Content-Length"])).decode()
            data = {"type": "int", "value": 2} if expr == "{+ 1 1}" else \\
                   {"type": "string", "value": "title_screen"}
            body = json.dumps({"ok": True, "data": data}).encode()
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
    try:
        srv = http.server.HTTPServer(("127.0.0.1", port), H)
    except OSError:
        print(f"[HttpServer] FATAL: port {port} already in use", file=sys.stderr, flush=True)
        os.abort()
    print(f"DC3_HTTP_PORT={srv.server_address[1]}", flush=True)
    srv.serve_forever()
    """)


class NativeBootPort(unittest.TestCase):
    """The port race (fleet flake: 'port 49627 already in use', rc=134)."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        d = Path(self.tmp.name)
        self.bin = d / "fake-dc3-native"
        self.bin.write_text(FAKE_DC3)
        self.bin.chmod(self.bin.stat().st_mode | stat.S_IXUSR)
        self.log = d / "native.log"
        self.blocker = None

    def tearDown(self):
        if self.blocker:
            self.blocker.close()
        self.tmp.cleanup()

    def occupy(self) -> int:
        self.blocker = socket.socket()
        self.blocker.bind(("0.0.0.0", 0))
        self.blocker.listen(1)
        return self.blocker.getsockname()[1]

    def boot(self, **env):
        b = G.NativeBoot(self.bin, self.log, timeout_s=60, extra_env=env)
        self.addCleanup(b.stop)
        return b

    def test_port_zero_is_read_back_from_stdout(self):
        b = self.boot(FAKE_MODE="new")
        t = b.wait_ready(deadline_s=20)
        self.assertEqual(b.attempt, 1)
        self.assertEqual(b.url, f"http://127.0.0.1:{b.port}")
        self.assertIn(f"DC3_HTTP_PORT={b.port}".encode(), self.log.read_bytes())
        self.assertEqual(t.eval_dta("{+ 1 1}").value, 2)

    def test_harness_no_longer_hands_out_a_port(self):
        # The old harness picked the port itself (_free_port) and passed it in;
        # a socket that grabs that port first killed the boot. Sabotage the
        # picker to return an occupied port: attempt 1 must not consult it.
        taken = self.occupy()
        orig = G._free_port
        G._free_port = lambda: taken
        try:
            b = self.boot(FAKE_MODE="new")
            b.wait_ready(deadline_s=20)
        finally:
            G._free_port = orig
        self.assertEqual(b.attempt, 1)
        self.assertNotEqual(b.port, taken)

    def test_collision_on_a_pre_fix_binary_is_retried(self):
        taken = self.occupy()
        b = self.boot(FAKE_MODE="old", FAKE_OLD_PORT=str(taken))
        b.wait_ready(deadline_s=20)
        self.assertEqual(b.attempt, 2)
        self.assertNotEqual(b.port, taken)
        self.assertIn(b"already in use", self.log.read_bytes())

    def test_collision_retries_are_bounded_and_report_the_output(self):
        taken = self.occupy()
        orig = G._free_port
        G._free_port = lambda: taken
        try:
            b = self.boot(FAKE_MODE="old", FAKE_OLD_PORT=str(taken))
            with self.assertRaises(RuntimeError) as cm:
                b.wait_ready(deadline_s=20)
        finally:
            G._free_port = orig
        self.assertEqual(b.attempt, 3)
        msg = str(cm.exception)
        self.assertIn("before ready", msg)
        self.assertIn(f"port {taken} already in use", msg)

    def test_early_exit_message_carries_stderr_tail(self):
        b = self.boot(FAKE_MODE="crash")
        with self.assertRaises(RuntimeError) as cm:
            b.wait_ready(deadline_s=20)
        self.assertEqual(b.attempt, 1)
        self.assertIn("FAKE_CRASH_MARKER", str(cm.exception))


if __name__ == "__main__":
    unittest.main()
