#!/usr/bin/env python3
"""Sabotage tests for the whole-build object manifest and the compile-edge pass.

`scripts/verify_objs_patched.py` is this repo's ONLY whole-build hash -- the
thing that says "these 989 objects are the ones that were verified patched" --
and until this file existed **nothing anywhere exercised `emit()`,
`verify_manifest()` or `tree_sha256`**.  By this project's own standing rule
that made it a guard nobody had watched fail.  Its siblings
(`tests/test_split_currency.py`, `tests/test_complete_units.py`) are the shape
followed here.

The discipline, and why each clause is here:

* **Sabotage before you believe.**  Every case asserts GREEN on a healthy
  fixture, breaks exactly one thing, asserts RED **pinning the reason**, then
  restores and asserts GREEN again.  Pinning the reason matters because several
  distinct defects all go red; a guard that reddens for the wrong reason must
  fail these rather than pass.
* **The negative control lives inside the test.**  Not in a sibling case that
  can be deselected, and not in a comment.
* **A case that finds no specimen FAILS.**  `test_the_real_build_ninja_is_wired`
  needs a generated `build.ninja`; if there isn't one it fails and says to run
  `configure.py`.  It does not skip.  A silent skip is the same defect wearing
  a different hat.
* **A control that can become invisible is itself detected.**  The stub
  patchers in `RunCheckTest` write marker files and the sabotaged one exits
  with a distinctive **7**, so a stub that never executed (exit 127) or an
  argparse mishap (exit 2) cannot be read as the failure under test.  A sibling
  lane's own-ancestor control passed twice for exactly that class of reason.

Run:  python3 -m pytest tests/test_objs_patched.py -q
      python3 tests/test_objs_patched.py            (unittest fallback)
"""

from __future__ import annotations

import hashlib
import importlib.util
import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CHECKER = REPO_ROOT / "scripts" / "verify_objs_patched.py"
METADATA_PATCHER = REPO_ROOT / "scripts" / "obj_build_metadata_patcher.py"


def _load(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(mod)
    return mod


vop = _load(CHECKER, "_vop_under_test")
meta = _load(METADATA_PATCHER, "_meta_under_test")


# --------------------------------------------------------------------------
# A synthetic PowerPC COFF object carrying both clock-derived fields.
#
# Built rather than copied out of build/ so the tests run in a tree that has
# never been compiled, and so the S_OBJNAME record is at a KNOWN offset -- a
# fixture lifted from the build could have zero of them and every "normalised
# the signature" assertion would pass without normalising anything.
# --------------------------------------------------------------------------

S_COMPILE3 = 0x1116


def synthetic_obj(timestamp: int = 0x6A95A2E7,
                  objname_sig: int = 0xDEADBEEF,
                  name: bytes = b"fixture.obj\0",
                  text: bytes = b"\x60\x00\x00\x00") -> bytes:
    """A minimal, structurally valid object with one `.debug$S` and one `.text`.

    Two sections, so `_debug_s_sections` has to actually find the right one
    rather than assume section 0.
    """
    # -- .debug$S payload: C13 signature, then a DEBUG_S_SYMBOLS subsection
    #    holding a filler record and then the S_OBJNAME whose signature word is
    #    what the pass zeroes.
    filler = struct.pack("<HH", 10, S_COMPILE3) + b"\0" * 8      # 12 bytes
    objname_body = struct.pack("<I", objname_sig) + name
    pad = (-len(objname_body)) % 4
    objname_body += b"\0" * pad
    objname = struct.pack("<HH", 2 + len(objname_body), meta.S_OBJNAME) + objname_body
    syms = filler + objname
    debug_s = (struct.pack("<I", meta.CV_SIGNATURE_C13)
               + struct.pack("<II", meta.DEBUG_S_SYMBOLS, len(syms)) + syms)

    header_len = 20 + 40 * 2
    text_off = header_len
    debug_off = text_off + len(text)

    hdr = struct.pack("<HHIIIHH", meta.COFF_MACHINE_POWERPCBE, 2, timestamp,
                      0, 0, 0, 0)

    def section(nm: bytes, size: int, praw: int) -> bytes:
        return (nm.ljust(8, b"\0")
                + struct.pack("<IIIIIIHHI", 0, 0, size, praw, 0, 0, 0, 0, 0))

    return (hdr
            + section(b".text", len(text), text_off)
            + section(b".debug$S", len(debug_s), debug_off)
            + text + debug_s)


def coff_timestamp(data: bytes) -> int:
    return struct.unpack_from("<I", data, 4)[0]


def objname_sigs(data: bytes) -> list[int]:
    return [struct.unpack_from("<I", data, o)[0]
            for o in meta.objname_signature_offsets(data)]


class SyntheticObjectSanityTest(unittest.TestCase):
    """The fixture itself is load-bearing; assert it carries what it claims.

    Without this, every "the pass zeroed the S_OBJNAME signature" assertion
    below could be satisfied by a fixture that has no S_OBJNAME record at all.
    """

    def test_fixture_carries_both_field_kinds(self):
        data = synthetic_obj()
        self.assertEqual(coff_timestamp(data), 0x6A95A2E7)
        self.assertEqual(objname_sigs(data), [0xDEADBEEF],
                         "the fixture must contain exactly one parsed "
                         "S_OBJNAME signature, or the normalisation tests are "
                         "asserting over an empty set")
        self.assertEqual(sorted(meta.plan(data)),
                         sorted([4] + meta.objname_signature_offsets(data)))
        # ...and the negative control: a fixture already at zero has NO plan.
        clean = synthetic_obj(timestamp=0, objname_sig=0)
        self.assertEqual(meta.plan(clean), [],
                         "plan() must be empty on an already-normalised "
                         "object, or 'pending' is a constant")


# --------------------------------------------------------------------------
# Fixture repo
# --------------------------------------------------------------------------

class RepoFixture(unittest.TestCase):
    """A temp tree shaped like `build/<VERSION>/src/**.obj`."""

    OBJECTS = {
        "alpha.obj": synthetic_obj(timestamp=0, objname_sig=0, text=b"\x01\x02\x03\x04"),
        "sub/beta.obj": synthetic_obj(timestamp=0, objname_sig=0, text=b"\x05\x06\x07\x08"),
        "sub/gamma.obj": synthetic_obj(timestamp=0, objname_sig=0, text=b"\x09\x0a\x0b\x0c"),
    }

    def setUp(self):
        self._td = tempfile.TemporaryDirectory(prefix="objs-patched-")
        self.addCleanup(self._td.cleanup)
        self.root = Path(self._td.name)
        self.src = vop.src_dir(self.root)
        self.src.mkdir(parents=True)
        for rel, data in self.OBJECTS.items():
            p = self.src / rel
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(data)

    def obj(self, rel: str) -> Path:
        return self.src / rel

    def manifest(self) -> dict:
        return json.loads(
            (self.root / "build" / vop.VERSION / "patch_state.json").read_text())

    def emit(self) -> dict:
        self.assertEqual(vop.emit(self.root), 0)
        return self.manifest()

    def assert_manifest_green(self):
        rc = vop.verify_manifest(self.root, quiet=True)
        self.assertEqual(rc, 0, "expected the manifest to verify clean")

    def assert_manifest_red(self, reason: str, expect_rc: int = 1) -> str:
        buf = _CapturedStderr()
        with buf:
            rc = vop.verify_manifest(self.root, quiet=True)
        self.assertEqual(rc, expect_rc,
                         f"expected exit {expect_rc}, got {rc}; stderr was:\n"
                         f"{buf.text}")
        self.assertIn(reason, buf.text,
                      f"went red, but not for the pinned reason {reason!r}. "
                      f"stderr was:\n{buf.text}")
        return buf.text


class _CapturedStderr:
    """Capture `sys.stderr` writes; these checkers print rather than raise."""

    def __enter__(self):
        import io
        self._old = sys.stderr
        self._buf = io.StringIO()
        sys.stderr = self._buf
        return self

    def __exit__(self, *exc):
        sys.stderr = self._old
        self.text = self._buf.getvalue()
        return False


# --------------------------------------------------------------------------
# emit() and tree_sha256
# --------------------------------------------------------------------------

class EmitTest(RepoFixture):

    def test_emit_records_every_object_with_its_real_hash(self):
        doc = self.emit()
        self.assertEqual(doc["n_objects"], len(self.OBJECTS))
        self.assertEqual(set(doc["objects"]),
                         {str(self.obj(r).relative_to(self.root))
                          for r in self.OBJECTS})
        for rel in self.OBJECTS:
            p = self.obj(rel)
            key = str(p.relative_to(self.root))
            self.assertEqual(doc["objects"][key]["sha256"],
                             hashlib.sha256(p.read_bytes()).hexdigest())
            self.assertEqual(doc["objects"][key]["size"], p.stat().st_size)

        # SABOTAGE, in-test: a manifest that recorded a CONSTANT would satisfy
        # everything above on a second run.  Change one object and re-emit; the
        # recorded hash for THAT object must move and the others must not.
        victim = str(self.obj("sub/beta.obj").relative_to(self.root))
        before = {k: v["sha256"] for k, v in doc["objects"].items()}
        self.obj("sub/beta.obj").write_bytes(
            synthetic_obj(timestamp=0, objname_sig=0, text=b"\xff\xff\xff\xff"))
        after = {k: v["sha256"] for k, v in self.emit()["objects"].items()}
        self.assertNotEqual(before[victim], after[victim])
        for k in before:
            if k != victim:
                self.assertEqual(before[k], after[k],
                                 "re-emit moved an object nobody touched")

    def test_tree_sha256_is_content_keyed_and_mtime_blind(self):
        """The load-bearing property, both directions in one test.

        `obj_patch_io.write_patched_obj` deliberately restores each object's
        mtime, so the tree's patch state is invisible in timestamps.  That is
        only safe because this hash is over CONTENT.  An implementation that
        folded mtime in would fail the first half; one that ignored content
        would fail the second.
        """
        first = self.emit()["tree_sha256"]

        # (a) mtime moves wildly, content does not -> hash must NOT move.
        for rel in self.OBJECTS:
            os.utime(self.obj(rel), ns=(1_000_000_000 * 10**9,
                                        1_000_000_000 * 10**9))
        self.assertEqual(self.emit()["tree_sha256"], first,
                         "tree_sha256 moved when only mtimes changed -- it is "
                         "no longer content-keyed, and obj_patch_io's mtime "
                         "restore silently stops being safe")

        # (b) ONE byte of ONE object moves -> hash MUST move.
        p = self.obj("alpha.obj")
        data = bytearray(p.read_bytes())
        data[-1] ^= 0xFF
        p.write_bytes(bytes(data))
        self.assertNotEqual(self.emit()["tree_sha256"], first,
                            "tree_sha256 did not move for a one-byte content "
                            "change -- it is not a hash of this tree")

    def test_tree_sha256_binds_each_hash_to_its_path(self):
        """Swapping two objects' contents must move the tree hash.

        The multiset of per-object hashes is unchanged by a swap, so a
        `tree_sha256` built from the hashes alone -- sorted and concatenated
        without their names -- would be IDENTICAL.  Every other test in this
        file passes under that implementation.  This one does not.
        """
        first = self.emit()["tree_sha256"]
        a, b = self.obj("sub/beta.obj"), self.obj("sub/gamma.obj")
        da, db = a.read_bytes(), b.read_bytes()
        self.assertNotEqual(da, db, "the swap fixture needs two DIFFERENT "
                                    "objects or the swap is a no-op")
        a.write_bytes(db)
        b.write_bytes(da)
        swapped = self.emit()
        self.assertNotEqual(swapped["tree_sha256"], first,
                            "tree_sha256 is blind to WHICH path a hash was "
                            "recorded under")
        # ...and the control that the swap really was hash-preserving overall:
        self.assertEqual(
            sorted(v["sha256"] for v in swapped["objects"].values()),
            sorted(hashlib.sha256(d).hexdigest() for d in (da, db,
                   self.obj("alpha.obj").read_bytes())),
            "the swap changed more than the pairing, so the test above proved "
            "less than it claims")
        # Restore and confirm we land back on the original hash exactly.
        a.write_bytes(da)
        b.write_bytes(db)
        self.assertEqual(self.emit()["tree_sha256"], first)

    def test_emit_refuses_an_empty_object_tree(self):
        """`0 objects verified patched` is the number a perfect tree reports."""
        # Negative control FIRST: the populated tree emits fine.
        self.assertEqual(vop.emit(self.root), 0)
        for rel in self.OBJECTS:
            self.obj(rel).unlink()
        with self.assertRaises(vop.EmptyObjectTreeError) as cm:
            vop.emit(self.root)
        self.assertIn("REFUSING TO VOUCH FOR", str(cm.exception))
        # And through the CLI, where the distinct exit code is the contract.
        rc = _cli(self.root, "--emit")
        self.assertEqual(rc.returncode, vop.EXIT_EMPTY_UNIVERSE,
                         f"expected EXIT_EMPTY_UNIVERSE; stderr:\n{rc.stderr}")


# --------------------------------------------------------------------------
# verify_manifest()
# --------------------------------------------------------------------------

class VerifyManifestTest(RepoFixture):

    def test_content_drift_is_caught_and_restoring_clears_it(self):
        self.emit()
        self.assert_manifest_green()

        victim = self.obj("sub/beta.obj")
        saved = victim.read_bytes()
        mangled = bytearray(saved)
        mangled[-1] ^= 0x01
        victim.write_bytes(bytes(mangled))
        text = self.assert_manifest_red("content differs")
        self.assertIn("sub/beta.obj", text)
        self.assertNotIn("alpha.obj", text.split("content differs", 1)[1]
                         .split("\n\n", 1)[0])

        victim.write_bytes(saved)
        self.assert_manifest_green()

    def test_drift_is_caught_even_when_size_is_unchanged(self):
        """The cheap half of the check must not be the only half.

        `verify_manifest` short-circuits on size before hashing.  An
        implementation that compared ONLY size would pass every other case in
        this file: the objects here are all different sizes.  This one flips a
        single byte, leaving the size identical.
        """
        self.emit()
        victim = self.obj("alpha.obj")
        saved = victim.read_bytes()
        mangled = bytearray(saved)
        mangled[len(mangled) // 2] ^= 0xFF
        self.assertEqual(len(mangled), len(saved))
        victim.write_bytes(bytes(mangled))
        self.assert_manifest_red("content differs")
        victim.write_bytes(saved)
        self.assert_manifest_green()

    def test_mtime_alone_is_deliberately_not_drift(self):
        """The design decision, pinned so a later 'fix' cannot smuggle it out.

        `obj_patch_io` restores mtimes on purpose (ninja's `.ninja_deps` goes
        stale otherwise and every patched object gets recompiled). Re-adding an
        mtime rule here would make a correctly patched tree read as drifted on
        every build.
        """
        self.emit()
        for rel in self.OBJECTS:
            os.utime(self.obj(rel), ns=(5 * 10**17, 5 * 10**17))
        self.assert_manifest_green()
        # Negative control in the same test: content drift on the SAME tree,
        # with those same alien mtimes, still goes red.
        p = self.obj("alpha.obj")
        p.write_bytes(p.read_bytes() + b"\0")
        self.assert_manifest_red("content differs")

    def test_a_deleted_object_is_caught_and_named(self):
        self.emit()
        victim = self.obj("sub/gamma.obj")
        saved = victim.read_bytes()
        victim.unlink()
        text = self.assert_manifest_red("now missing")
        self.assertIn("sub/gamma.obj", text)
        victim.write_bytes(saved)
        self.assert_manifest_green()

    def test_an_object_the_manifest_never_saw_is_caught(self):
        """The single-TU-compile shape: something appeared outside the graph."""
        self.emit()
        self.assert_manifest_green()
        intruder = self.obj("sub/delta.obj")
        intruder.write_bytes(synthetic_obj(timestamp=0, objname_sig=0,
                                           text=b"\xde\xad\xbe\xef"))
        text = self.assert_manifest_red("not in the manifest")
        self.assertIn("sub/delta.obj", text)
        intruder.unlink()
        self.assert_manifest_green()

    def test_an_absent_manifest_is_a_refusal_not_a_pass(self):
        self.emit()
        self.assert_manifest_green()
        (self.root / "build" / vop.VERSION / "patch_state.json").unlink()
        self.assert_manifest_red("has never been verified patched", expect_rc=2)

    def test_a_manifest_vouching_for_zero_objects_is_refused(self):
        """`OK: 0 objects match` -- a manifest of nothing matches a tree of
        nothing, and that used to exit 0."""
        self.emit()
        self.assert_manifest_green()
        mpath = self.root / "build" / vop.VERSION / "patch_state.json"
        doc = json.loads(mpath.read_text())
        doc["objects"] = {}
        doc["n_objects"] = 0
        doc["tree_sha256"] = hashlib.sha256(b"").hexdigest()
        mpath.write_text(json.dumps(doc))
        self.assert_manifest_red("vouches for ZERO objects",
                                 expect_rc=vop.EXIT_EMPTY_UNIVERSE)

    def test_a_per_target_rebuild_of_a_raw_object_is_caught_end_to_end(self):
        """The scenario the manifest exists for, played out with real bytes.

        Emit over a normalised tree, then put back the RAW form of one object
        -- exactly what `ninja <one>.obj` produced before the compile edge ran
        the metadata pass.  The manifest must go red.  Normalising that same
        object with the real patcher must then restore byte-identity and turn
        it green, without re-emitting: if `--obj` produced anything other than
        the exact bytes `emit` vouched for, this stays red.
        """
        self.emit()
        self.assert_manifest_green()

        victim = self.obj("alpha.obj")
        normalized = victim.read_bytes()
        raw = synthetic_obj(timestamp=0x6A95A2E7, objname_sig=0xDEADBEEF,
                            text=b"\x01\x02\x03\x04")
        self.assertNotEqual(raw, normalized,
                            "the 'raw' fixture must actually differ, or this "
                            "test proves nothing")
        victim.write_bytes(raw)
        self.assert_manifest_red("content differs")

        rc = subprocess.run([sys.executable, str(METADATA_PATCHER),
                             "--obj", str(victim)],
                            capture_output=True, text=True)
        self.assertEqual(rc.returncode, 0, rc.stderr)
        self.assertEqual(victim.read_bytes(), normalized,
                         "--obj did not reproduce the bytes emit() vouched for")
        self.assert_manifest_green()


# --------------------------------------------------------------------------
# run_check() -- the chain dry-run
# --------------------------------------------------------------------------

STUB = """#!/usr/bin/env python3
import sys, pathlib
pathlib.Path(__file__).with_suffix(".ran").write_text("ran\\n")
sys.exit({code})
"""


class RunCheckTest(RepoFixture):
    """`run_check` shells out to each entry of `PATCHERS`.

    Driven with STUB patchers rather than the real six: the contract under test
    is "run every listed pass, go red naming any that reports pending", and a
    stub makes the sabotage exact.  `test_PATCHERS_names_real_files` below is
    what ties the list back to the real scripts.
    """

    def _install_stubs(self, failing: str | None = None, fail_code: int = 7):
        sdir = self.root / "scripts"
        sdir.mkdir(exist_ok=True)
        for name in vop.PATCHERS:
            code = fail_code if name == failing else 0
            (sdir / name).write_text(STUB.format(code=code))

    def _assert_all_stubs_ran(self):
        """The invisible-control probe.

        A stub that never executes leaves `run_check` reading exit 0 from
        nothing at all, and the GREEN assertion passes for the worst possible
        reason.  A sibling lane's control passed twice that way (`sh -c` elided
        the fork; a SyntaxError's exit 1 was the value being asserted).  So the
        stubs leave evidence and the evidence is checked.
        """
        sdir = self.root / "scripts"
        for name in vop.PATCHERS:
            marker = (sdir / name).with_suffix(".ran")
            self.assertTrue(marker.exists(),
                            f"{name} never executed -- this run's result says "
                            f"nothing about it")
            marker.unlink()

    def test_green_when_every_pass_reports_no_pending_work(self):
        self._install_stubs()
        self.assertEqual(vop.run_check(self.root), 0)
        self._assert_all_stubs_ran()

    def test_red_naming_the_one_pass_that_reports_pending(self):
        offender = vop.PATCHERS[-1]          # the build-metadata pass
        self._install_stubs(failing=offender)
        buf = _CapturedStderr()
        with buf:
            rc = vop.run_check(self.root)
        self.assertEqual(rc, 1)
        self._assert_all_stubs_ran()
        self.assertIn("NOT FULLY PATCHED", buf.text)
        self.assertIn(offender, buf.text)
        self.assertIn("exit 7", buf.text,
                      "the reported exit code must be the stub's 7, not a "
                      "127 (never launched) or a 2 (argparse) wearing its "
                      "costume; stderr was:\n" + buf.text)
        self.assertIn(f"1 of {len(vop.PATCHERS)}", buf.text)
        for innocent in vop.PATCHERS[:-1]:
            self.assertNotIn(f"  {innocent} (exit", buf.text)

        # Restore and confirm green -- the guard is not simply always red.
        self._install_stubs()
        self.assertEqual(vop.run_check(self.root), 0)
        self._assert_all_stubs_ran()

    def test_run_check_refuses_an_empty_object_tree(self):
        self._install_stubs()
        self.assertEqual(vop.run_check(self.root), 0)   # control
        self._assert_all_stubs_ran()
        for rel in self.OBJECTS:
            self.obj(rel).unlink()
        with self.assertRaises(vop.EmptyObjectTreeError):
            vop.run_check(self.root)

    def test_PATCHERS_names_real_files(self):
        """The list is the build's statement of what a patched tree is.

        A renamed or deleted patcher would leave `run_check` shelling out to a
        missing path -- `subprocess.run` raises `FileNotFoundError` there, which
        is loud, but only when someone runs it.
        """
        for name in vop.PATCHERS:
            self.assertTrue((REPO_ROOT / "scripts" / name).is_file(),
                            f"PATCHERS names {name}, which does not exist")
        # In-test negative control: the check above must be capable of failing.
        self.assertFalse((REPO_ROOT / "scripts" / "obj_nonexistent_patcher.py").is_file())
        self.assertIn("obj_build_metadata_patcher.py", vop.PATCHERS)
        self.assertEqual(vop.PATCHERS[-1], "obj_build_metadata_patcher.py",
                         "the metadata pass must stay LAST: it zeroes fields "
                         "the earlier passes must not be able to reintroduce")


# --------------------------------------------------------------------------
# check_compile_edge() -- the per-target build's only guard
# --------------------------------------------------------------------------

WIRED = """{prefix}rule msvc
  command = cd $in_dir && wibo cl.exe $cflags /Fo$abs_out $in_win{m}
  description = MSVC $out
  deps = msvc

rule msvc_pch_create
  command = cd $in_dir && wibo cl.exe /Yc $cflags /Fo$abs_out $in_win{pc}
  description = PCH $pch_out

rule msvc_pch
  command = cd $in_dir && wibo cl.exe /Yu $cflags $
      /Fo$abs_out $in_win{p}
  description = MSVC $out

rule run_script
  command = $cmd && touch $out
"""

PASS_SUFFIX = (" && $python $\n"
               "      /abs/scripts/obj_build_metadata_patcher.py --obj $abs_out")


def _ninja(msvc=True, pch_create=True, pch=True, prefix="") -> str:
    return WIRED.format(prefix=prefix,
                        m=PASS_SUFFIX if msvc else "",
                        pc=PASS_SUFFIX if pch_create else "",
                        p=PASS_SUFFIX if pch else "")


class CompileEdgeTest(unittest.TestCase):

    def setUp(self):
        self._td = tempfile.TemporaryDirectory(prefix="compile-edge-")
        self.addCleanup(self._td.cleanup)
        self.root = Path(self._td.name)

    def write(self, text: str):
        (self.root / "build.ninja").write_text(text)

    def assert_green(self):
        buf = _CapturedStderr()
        with buf:
            rc = vop.check_compile_edge(self.root, quiet=True)
        self.assertEqual(rc, 0, f"expected green; stderr:\n{buf.text}")

    def assert_red(self, reason: str, expect_rc: int = None) -> str:
        expect_rc = vop.EXIT_COMPILE_EDGE if expect_rc is None else expect_rc
        buf = _CapturedStderr()
        with buf:
            rc = vop.check_compile_edge(self.root, quiet=True)
        self.assertEqual(rc, expect_rc,
                         f"expected exit {expect_rc}, got {rc}; stderr:\n{buf.text}")
        self.assertIn(reason, buf.text,
                      f"red, but not for the pinned reason {reason!r}:\n{buf.text}")
        return buf.text

    def test_green_when_all_three_rules_carry_the_pass(self):
        self.write(_ninja())
        self.assert_green()

    def test_red_when_exactly_one_rule_loses_it(self):
        for dropped, kwargs in (("msvc", dict(msvc=False)),
                                ("msvc_pch", dict(pch=False)),
                                ("msvc_pch_create", dict(pch_create=False))):
            with self.subTest(rule=dropped):
                self.write(_ninja(**kwargs))
                text = self.assert_red("do not run obj_build_metadata_patcher.py")
                offenders = text.split("do not run obj_build_metadata_patcher.py")[1]
                offenders = offenders.split("\n")[0]
                self.assertIn(dropped, offenders)
                for innocent in set(vop.COMPILE_RULES) - {dropped}:
                    # `msvc` is a substring of the other two, so compare the
                    # parsed list rather than the raw text.
                    self.assertNotIn(innocent,
                                     [t.strip() for t in offenders.split(",")])
                self.write(_ninja())
                self.assert_green()

    def test_red_when_a_compile_rule_disappears_entirely(self):
        self.write(_ninja())
        self.assert_green()
        text = (self.root / "build.ninja").read_text()
        without = text.split("rule msvc_pch_create")[0] + \
            "rule msvc_pch\n" + text.split("rule msvc_pch\n", 1)[1]
        self.write(without)
        self.assert_red("rules absent from build.ninja entirely")
        self.write(_ninja())
        self.assert_green()

    def test_a_mention_elsewhere_in_the_file_does_not_count(self):
        """The trap a whole-file grep falls into, and the reason this parses.

        The REAL build.ninja names `obj_build_metadata_patcher.py` in the
        post-compile `run_script` step regardless of whether the compile edges
        carry it.  A checker written as
        `"obj_build_metadata_patcher.py" in build_ninja_text` would therefore
        be GREEN on a completely unwired tree -- a guard that cannot fail.
        """
        decoy = ("rule post_compile_metadata\n"
                 "  command = python3 scripts/obj_build_metadata_patcher.py "
                 "--batch --apply\n\n")
        self.write(_ninja(msvc=False, pch=False, pch_create=False, prefix=decoy))
        raw = (self.root / "build.ninja").read_text()
        self.assertIn("obj_build_metadata_patcher.py", raw,
                      "the decoy must really be present, or this test is "
                      "asserting against a file that has nothing to be fooled by")
        self.assert_red("do not run obj_build_metadata_patcher.py")
        # ...and with the decoy STILL present, wiring the rules turns it green.
        self.write(_ninja(prefix=decoy))
        self.assert_green()

    def test_absent_build_ninja_is_a_refusal_not_a_pass(self):
        self.write(_ninja())
        self.assert_green()
        (self.root / "build.ninja").unlink()
        self.assert_red("is absent")

    def test_dollar_continuations_are_joined_before_matching(self):
        """`msvc_pch` in the fixture wraps mid-command; the real one wraps
        eight times.  A line-at-a-time parser sees the pass on a line that is
        not the `command =` line and reports it missing."""
        cmds = vop.ninja_rule_commands(_ninja())
        self.assertEqual(set(vop.COMPILE_RULES) - set(cmds), set())
        self.assertIn("/Fo$abs_out $in_win", cmds["msvc_pch"],
                      "the wrapped command was not rejoined")
        self.assertIn("obj_build_metadata_patcher.py", cmds["msvc_pch"])
        self.assertNotIn("obj_build_metadata_patcher.py",
                         vop.ninja_rule_commands(_ninja(pch=False))["msvc_pch"])

    def test_the_real_build_ninja_is_wired(self):
        """No specimen -> FAIL, never skip.

        A `skipTest` here would turn "this repo's compile edges are unwired"
        into a green run on any tree where `configure.py` had not been run --
        which is every fresh clone, and is exactly the shape of vacuity this
        file exists to prevent.
        """
        real = REPO_ROOT / "build.ninja"
        self.assertTrue(
            real.is_file(),
            f"{real} does not exist, so the wiring of THIS repo's compile "
            f"edges is unverified. Run `python3 configure.py` and re-run. "
            f"This is a failure and not a skip on purpose.")
        buf = _CapturedStderr()
        with buf:
            rc = vop.check_compile_edge(REPO_ROOT, quiet=True)
        self.assertEqual(rc, 0, f"the real build.ninja is not wired:\n{buf.text}")
        # In-test control that the assertion above can fail: the same checker,
        # on the same file with the pass stripped, must go red.
        stripped = real.read_text()
        for rule in vop.COMPILE_RULES:
            self.assertIn(f"rule {rule}\n", stripped)
        mangled = stripped.replace("obj_build_metadata_patcher.py",
                                   "obj_DISABLED_patcher.py")
        (self.root / "build.ninja").write_text(mangled)
        self.assert_red("do not run obj_build_metadata_patcher.py")


# --------------------------------------------------------------------------
# The per-object pass itself (`--obj`), which the compile edges invoke
# --------------------------------------------------------------------------

def _cli(repo: Path, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(CHECKER), "--repo", str(repo),
                           *args], capture_output=True, text=True)


def _meta_cli(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(METADATA_PATCHER), *args],
                          capture_output=True, text=True)


class ObjModeTest(unittest.TestCase):

    def setUp(self):
        self._td = tempfile.TemporaryDirectory(prefix="obj-mode-")
        self.addCleanup(self._td.cleanup)
        self.tmp = Path(self._td.name)

    def _obj(self, name="raw.obj", **kw) -> Path:
        p = self.tmp / name
        p.write_bytes(synthetic_obj(**kw))
        return p

    def test_obj_zeroes_both_fields_preserves_mtime_and_is_idempotent(self):
        p = self._obj()
        # Precondition, asserted rather than assumed: a fixture that arrived
        # already clean would make every claim below vacuous.
        self.assertNotEqual(coff_timestamp(p.read_bytes()), 0)
        self.assertEqual(objname_sigs(p.read_bytes()), [0xDEADBEEF])

        os.utime(p, ns=(123456789_000000000, 123456789_000000000))
        mtime_before = p.stat().st_mtime_ns

        r = _meta_cli("--obj", str(p))
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(r.stdout, "", "the compile edge's stdout is parsed as "
                                      "`deps = msvc`; this pass must be silent")
        self.assertEqual(coff_timestamp(p.read_bytes()), 0)
        self.assertEqual(objname_sigs(p.read_bytes()), [0])
        self.assertEqual(p.stat().st_mtime_ns, mtime_before,
                         "mtime moved -- ninja's .ninja_deps for this object "
                         "goes stale and it gets recompiled forever "
                         "(scripts/obj_patch_io.py)")

        after = p.read_bytes()
        self.assertEqual(_meta_cli("--obj", str(p)).returncode, 0)
        self.assertEqual(p.read_bytes(), after, "second run was not a no-op")

    def test_obj_check_mode_is_2_when_pending_and_0_when_clean(self):
        p = self._obj()
        r = _meta_cli("--obj", str(p), "--check")
        self.assertEqual(r.returncode, 2, r.stderr + r.stdout)
        self.assertIn("still carries", r.stderr)
        self.assertEqual(coff_timestamp(p.read_bytes()), 0x6A95A2E7,
                         "--check must not write")
        self.assertEqual(_meta_cli("--obj", str(p)).returncode, 0)
        self.assertEqual(_meta_cli("--obj", str(p), "--check").returncode, 0)

    def test_obj_refuses_things_that_are_not_objects(self):
        """The vacuity that hides inside a permissive parser.

        `plan()` returns `[]` for an empty file, a truncated one, and an x86
        object alike -- the same answer it gives for a perfectly normalised
        object.  Single-object mode is wired into a compile edge, so it asserts
        instead.
        """
        good = self._obj("good.obj")
        self.assertEqual(_meta_cli("--obj", str(good)).returncode, 0)  # control

        empty = self.tmp / "empty.obj"
        empty.write_bytes(b"")
        r = _meta_cli("--obj", str(empty))
        self.assertEqual(r.returncode, meta.EXIT_NOT_AN_OBJECT, r.stderr)
        self.assertIn("too small", r.stderr)

        wrong = self.tmp / "x86.obj"
        data = bytearray(synthetic_obj())
        struct.pack_into("<H", data, 0, 0x8664)
        wrong.write_bytes(bytes(data))
        r = _meta_cli("--obj", str(wrong))
        self.assertEqual(r.returncode, meta.EXIT_NOT_AN_OBJECT, r.stderr)
        self.assertIn("COFF machine 0x8664", r.stderr)

        truncated = self.tmp / "trunc.obj"
        truncated.write_bytes(synthetic_obj()[:30])
        r = _meta_cli("--obj", str(truncated))
        self.assertEqual(r.returncode, meta.EXIT_NOT_AN_OBJECT, r.stderr)
        self.assertIn("truncated", r.stderr)

        r = _meta_cli("--obj", str(self.tmp / "nope.obj"))
        self.assertEqual(r.returncode, 1, r.stderr)
        self.assertIn("does not exist", r.stderr)

    def test_batch_and_obj_are_mutually_exclusive_and_neither_is_optional(self):
        p = self._obj()
        self.assertEqual(_meta_cli("--batch", "--obj", str(p)).returncode, 1)
        self.assertEqual(coff_timestamp(p.read_bytes()), 0x6A95A2E7,
                         "the refused invocation wrote anyway")
        r = _meta_cli()
        self.assertEqual(r.returncode, 1)
        self.assertIn("--batch", r.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
