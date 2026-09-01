#!/usr/bin/env python3
"""Sabotage tests for the undefined-symbol guard.

The guard exists because `-Wl,--no-undefined` on the native link cannot see the
"declared, called, defined nowhere" class: clang at -O2 can delete the only call
site before the linker runs, which is exactly what hid `JoypadSendKeepAlive` for
the whole life of `src/system/os/Joypad_Xbox.cpp`.

A guard nobody has watched FAIL is not a guard, and this project has shipped
three of those. So every test below builds a fixture, asserts GREEN, breaks
exactly one thing, asserts RED *and pins the exit code*, then restores and
asserts GREEN again. The fixtures are synthesised COFF objects, so the tests do
not need a built tree and run in well under a second.

Run:  python3 -m pytest tests/test_undefined_decomp_symbols.py -q
      python3 tests/test_undefined_decomp_symbols.py       (unittest fallback)
"""

from __future__ import annotations

import importlib.util
import struct
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CHECKER = REPO_ROOT / "scripts" / "check_undefined_decomp_symbols.py"

IMAGE_SYM_CLASS_EXTERNAL = 2
IMAGE_SYM_CLASS_STATIC = 3


def _load_checker():
    spec = importlib.util.spec_from_file_location("_cuds_under_test", CHECKER)
    mod = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(mod)
    return mod


cuds = _load_checker()


def make_obj(path: Path, defined=(), undefined=(), common=()):
    """Write a minimal PPC COFF object with the requested external symbols.

    Only the fields `coff_externals` reads are meaningful: the header's symbol
    table pointer/count, the 18-byte symbol records, and the string table. One
    real section exists so a "defined" symbol can point at a section number.
    """
    syms, strtab = [], bytearray(b"\x00\x00\x00\x00")

    def name_field(name: str) -> bytes:
        raw = name.encode("latin1")
        if len(raw) <= 8:
            return raw.ljust(8, b"\x00")
        offset = len(strtab)
        strtab.extend(raw + b"\x00")
        return struct.pack("<II", 0, offset)

    for n in defined:
        syms.append(name_field(n) + struct.pack("<IhHBB", 0x10, 1, 0x20,
                                                IMAGE_SYM_CLASS_EXTERNAL, 0))
    for n in undefined:
        syms.append(name_field(n) + struct.pack("<IhHBB", 0, 0, 0x20,
                                                IMAGE_SYM_CLASS_EXTERNAL, 0))
    for n, size in common:
        # secnum 0 with a NON-zero value is a COMMON symbol -- a definition.
        syms.append(name_field(n) + struct.pack("<IhHBB", size, 0, 0,
                                                IMAGE_SYM_CLASS_EXTERNAL, 0))

    header_size = 20 + 40
    text = b"\x4e\x80\x00\x20"  # blr
    text_off = header_size
    symptr = text_off + len(text)

    out = bytearray()
    out += struct.pack("<HHIIIHH", 0x1F2, 1, 0, symptr, len(syms), 0, 0)
    out += (b".text".ljust(8, b"\x00")
            + struct.pack("<IIIIIIHHI", 0, 0, len(text), text_off, 0, 0, 0, 0, 0x60000020))
    out += text
    for s in syms:
        out += s
    strtab[0:4] = struct.pack("<I", len(strtab))
    out += strtab
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(bytes(out))


class Fixture:
    """A throwaway repo layout the checker can be pointed at."""

    def __init__(self, stack):
        self.root = Path(stack.enter_context(tempfile.TemporaryDirectory()))
        self.build = self.root / "build" / "373307D9"
        self.src = self.build / "src"
        self.inventory = self.root / "inventory.txt"
        self._saved = (cuds.BUILD, cuds.OUR_OBJ_ROOTS, cuds.TARGET_OBJ_ROOT,
                       cuds.INVENTORY, cuds.REPO)
        cuds.REPO = str(self.root)
        cuds.BUILD = str(self.build)
        cuds.OUR_OBJ_ROOTS = [str(self.src), str(self.build / "pch")]
        cuds.TARGET_OBJ_ROOT = str(self.build / "obj")
        cuds.INVENTORY = str(self.inventory)
        stack.callback(self.restore)

    def restore(self):
        (cuds.BUILD, cuds.OUR_OBJ_ROOTS, cuds.TARGET_OBJ_ROOT,
         cuds.INVENTORY, cuds.REPO) = self._saved


class UndefinedSymbolGuardTest(unittest.TestCase):
    def setUp(self):
        import contextlib
        self.stack = contextlib.ExitStack()
        self.addCleanup(self.stack.close)
        self.fx = Fixture(self.stack)

    # -- the real-world shape, end to end -------------------------------------

    def test_missing_body_is_red_and_defining_it_is_green(self):
        """The JoypadSendKeepAlive shape: caller in one TU, no definer anywhere."""
        make_obj(self.fx.src / "system/os/Joypad.obj",
                 defined=["JoypadPollCommon"], undefined=["JoypadSendKeepAlive"])
        make_obj(self.fx.src / "system/os/Joypad_Xbox.obj", defined=["JoypadPoll"])

        # GREEN control FIRST: with the symbol in the inventory the gate passes,
        # so the RED below is attributable to the symbol and not to the fixture.
        self.fx.inventory.write_text("EXT JoypadSendKeepAlive\n")
        self.assertEqual(cuds.do_check(str(self.fx.inventory), quiet=True), 0)

        # RED: not in the inventory -> exit 1.
        self.fx.inventory.write_text("# nothing here\n")
        self.assertEqual(cuds.do_check(str(self.fx.inventory), quiet=True), 1)

        # GREEN again once some TU actually defines it -- the real fix, not an
        # inventory edit.
        make_obj(self.fx.src / "system/os/Joypad_Xbox.obj",
                 defined=["JoypadPoll", "JoypadSendKeepAlive"])
        self.assertEqual(cuds.do_check(str(self.fx.inventory), quiet=True), 0)

    # -- the gate must not be disarmable --------------------------------------

    def test_missing_inventory_is_not_green(self):
        """`rm` on the gate's own evidence file must not read as a pass."""
        make_obj(self.fx.src / "a.obj", undefined=["SomeMissingThing"])
        rc = cuds.do_check(str(self.fx.root / "definitely-not-here.txt"), quiet=True)
        self.assertEqual(rc, 2, "a deleted inventory must not exit 0")

    def test_empty_inventory_is_not_green(self):
        """Truncating the file to nothing is the same disarm by another route."""
        make_obj(self.fx.src / "a.obj", undefined=["SomeMissingThing"])
        self.fx.inventory.write_text("")
        self.assertEqual(cuds.do_check(str(self.fx.inventory), quiet=True), 1)

    def test_comments_only_inventory_is_not_green(self):
        """Rewriting the body as prose diffs like a doc change. It must still fail."""
        make_obj(self.fx.src / "a.obj", undefined=["SomeMissingThing"])
        self.fx.inventory.write_text("# all clear, nothing undefined here\n")
        self.assertEqual(cuds.do_check(str(self.fx.inventory), quiet=True), 1)

    def test_no_objects_is_unreadable_not_green(self):
        """An unbuilt tree must say so rather than report a clean bill of health."""
        self.fx.inventory.write_text("EXT Whatever\n")
        self.assertEqual(cuds.do_check(str(self.fx.inventory), quiet=True), 2)

    # -- COFF reading correctness ---------------------------------------------

    def test_definition_in_any_object_satisfies_a_reference_in_another(self):
        make_obj(self.fx.src / "caller.obj", undefined=["Shared"])
        make_obj(self.fx.src / "definer.obj", defined=["Shared"])
        self.fx.inventory.write_text("")
        self.assertEqual(cuds.do_check(str(self.fx.inventory), quiet=True), 0)

    def test_common_symbol_counts_as_a_definition(self):
        """secnum 0 with a non-zero value is COMMON (bss), not undefined.

        Reading it as undefined would flood the inventory with false entries and
        train everyone to ignore the gate.
        """
        make_obj(self.fx.src / "caller.obj", undefined=["gThing"])
        make_obj(self.fx.src / "bss.obj", common=[("gThing", 64)])
        self.fx.inventory.write_text("")
        self.assertEqual(cuds.do_check(str(self.fx.inventory), quiet=True), 0)

    def test_long_names_come_from_the_string_table(self):
        """Real mangled names blow past the 8-byte inline field."""
        long_name = "?" + "A" * 200 + "@@YAXXZ"
        make_obj(self.fx.src / "caller.obj", undefined=[long_name])
        self.fx.inventory.write_text("")
        self.assertEqual(cuds.do_check(str(self.fx.inventory), quiet=True), 1)
        self.fx.inventory.write_text("EXT %s\n" % long_name)
        self.assertEqual(cuds.do_check(str(self.fx.inventory), quiet=True), 0)

    # -- the improvement path -------------------------------------------------

    def test_implementing_a_body_is_exit_3_not_exit_1(self):
        """Shrinking the residue must be advisory.

        Implementing missing bodies is the whole job of this repo. If that broke
        every other lane's build the gate would be switched off within a day --
        and a gate that gets switched off catches nothing.
        """
        make_obj(self.fx.src / "a.obj", defined=["NowDefined"])
        self.fx.inventory.write_text("GAP NowDefined  # target defines it in x.obj\n")
        self.assertEqual(cuds.do_check(str(self.fx.inventory), quiet=True), 3)

    def test_new_symbol_wins_over_stale_entry(self):
        """A regression must not be masked by an unrelated improvement."""
        make_obj(self.fx.src / "a.obj", defined=["NowDefined"], undefined=["BrandNew"])
        self.fx.inventory.write_text("GAP NowDefined\n")
        self.assertEqual(cuds.do_check(str(self.fx.inventory), quiet=True), 1)

    # -- the script's own selftest -------------------------------------------

    def test_selftest_is_vacuous_on_an_empty_tree(self):
        """--selftest must refuse to pass when there was nothing to remove."""
        self.assertEqual(cuds.do_selftest(), 5)

    def test_selftest_passes_on_a_real_residue(self):
        make_obj(self.fx.src / "a.obj", undefined=["Missing1", "Missing2"])
        self.assertEqual(cuds.do_selftest(), 0)


class WiringTest(unittest.TestCase):
    """The check has to actually RUN. A correct script nobody calls guards nothing.

    Parses `configure.py` rather than grepping the whole file for the script's
    name: the name also appears in comments, so a substring test would stay
    green on a tree where the build step had been deleted.
    """

    def test_configure_registers_the_post_compile_step(self):
        text = (REPO_ROOT / "configure.py").read_text()
        marker = 'python3 scripts/check_undefined_decomp_symbols.py --check'
        self.assertIn(marker, text,
                      "the post-compile step that runs the checker is gone")
        # It must be a `cmd` value, not prose. Find the quoted string that holds
        # it and require a "cmd" key on the same logical entry.
        idx = text.index(marker)
        window = text[max(0, idx - 400):idx]
        self.assertIn('"cmd"', window,
                      "the checker is mentioned but not wired as a build command")
        self.assertIn('undefined_symbols_checked.stamp', text,
                      "the step has no ninja output, so nothing depends on it")

    def test_improvement_exit_is_tolerated_but_failure_is_not(self):
        """The ninja command must swallow exit 3 and ONLY exit 3."""
        text = (REPO_ROOT / "configure.py").read_text()
        self.assertIn('|| test $$? -eq 3', text,
                      "exit 3 (improvement) should not fail the build")
        self.assertNotIn('--check || true', text,
                         "swallowing every failure would disarm the gate")


if __name__ == "__main__":
    unittest.main()
