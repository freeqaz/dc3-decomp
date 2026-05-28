"""Regression tests for the HX_NATIVE / preprocessor-directive write guard.

Background
----------
The native cross-platform port lives behind ``#ifdef HX_NATIVE`` / ``#else`` /
``#endif`` blocks interleaved with the matched (Wii/PPC) source, e.g.::

    inline void Multiply(const Vector3 &vin, const Hmx::Matrix3 &mtx, Vector3 &vout) {
    #ifdef HX_NATIVE
        vout.Set( ... C++ body ... );      // native fork
    #else
        register __vec2x32float__ i1, i2;  // matched PPC asm
        ASM_BLOCK( ... )
    #endif
    }

The permuter only ever optimizes the matched (``#else``) branch. A bug let a
winning variant be written back with the ``#ifdef HX_NATIVE`` fork DELETED,
silently corrupting the port (RB3 commit f8a3a379 "post-permuter-wipe salvage"
re-applied a whole session's worth of wiped native blocks by hand).

The fix is a universal safety net in :func:`atomic_write_bytes` — every apply
path (hill_climber, beam_search, evolutionary, __main__, …) funnels through it,
and it refuses any write that drops the preprocessor-conditional or
``HX_NATIVE`` count vs. the file already on disk.
"""

from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path

from scripts.permuter.file_util import (
    DirectivePreservationError,
    _count_directives,
    _is_ephemeral_path,
    atomic_write_bytes,
)


# The canonical native-fork shape from Mtx.h::Multiply (V21).
_FORKED_SOURCE = b"""inline void Multiply(const Vector3 &vin, const Hmx::Matrix3 &mtx, Vector3 &vout) {
#ifdef HX_NATIVE
    vout.Set(mtx.x.x * vin.x, mtx.x.y * vin.y, mtx.x.z * vin.z);
#else
    register __vec2x32float__ i1, i2, m1, m2, o1, o2;
    register const Vector3 *_vin = &vin;
    register const Hmx::Matrix3 *_mtx = &mtx;
    register Vector3 *_vout = &vout;
    ASM_BLOCK( psq_l i1, Vector3.x(_vin), 0, 0 )
#endif
}
"""

# A legitimate optimization of ONLY the matched (#else) branch — i1/i2 swapped,
# directives intact. This is what a correct permuter variant looks like.
_MATCHED_BRANCH_EDIT = b"""inline void Multiply(const Vector3 &vin, const Hmx::Matrix3 &mtx, Vector3 &vout) {
#ifdef HX_NATIVE
    vout.Set(mtx.x.x * vin.x, mtx.x.y * vin.y, mtx.x.z * vin.z);
#else
    register __vec2x32float__ i2, i1, m1, m2, o1, o2;
    register const Vector3 *_vin = &vin;
    register const Hmx::Matrix3 *_mtx = &mtx;
    register Vector3 *_vout = &vout;
    ASM_BLOCK( psq_l i1, Vector3.x(_vin), 0, 0 )
#endif
}
"""

# The corruption: matched body kept, the #ifdef HX_NATIVE / #else / #endif
# skeleton (and the native fork it guarded) wiped out.
_WIPED_SOURCE = b"""inline void Multiply(const Vector3 &vin, const Hmx::Matrix3 &mtx, Vector3 &vout) {
    register __vec2x32float__ i2, i1, m1, m2, o1, o2;
    register const Vector3 *_vin = &vin;
    register const Hmx::Matrix3 *_mtx = &mtx;
    register Vector3 *_vout = &vout;
    ASM_BLOCK( psq_l i1, Vector3.x(_vin), 0, 0 )
}
"""

_BANNER = (
    b"/* ===== PERMUTER LOCK\n"
    b"   Started: 2026-05-28 12:00\n"
    b"===== */\n"
)


class TestDirectiveCounting(unittest.TestCase):
    def test_counts_conditionals_and_hx_native(self):
        self.assertEqual(_count_directives(_FORKED_SOURCE), (3, 1))

    def test_wiped_source_has_zero(self):
        self.assertEqual(_count_directives(_WIPED_SOURCE), (0, 0))

    def test_matched_branch_edit_preserves_counts(self):
        self.assertEqual(
            _count_directives(_MATCHED_BRANCH_EDIT),
            _count_directives(_FORKED_SOURCE),
        )

    def test_ifndef_and_elif_counted(self):
        src = b"#ifndef X\n#elif Y\n#else\n#endif\n"
        # ifndef + elif + else + endif = 4 conditionals, 0 HX_NATIVE
        self.assertEqual(_count_directives(src), (4, 0))

    def test_define_and_include_not_counted(self):
        # Only the conditional skeleton is guarded, not #define / #include.
        src = b"#define FOO 1\n#include <x.h>\n"
        self.assertEqual(_count_directives(src), (0, 0))


class TestWriteGuard(unittest.TestCase):
    def setUp(self):
        self._dir = tempfile.mkdtemp(prefix="dirguard_test_")
        self.path = Path(self._dir) / "Mtx.h"
        # Ensure the guard is active even if a parent shell exported the
        # opt-out (the live coordinator may run with it set).
        self._saved_env = os.environ.pop("PERMUTER_ALLOW_DIRECTIVE_DROP", None)

    def tearDown(self):
        for p in Path(self._dir).iterdir():
            p.unlink()
        Path(self._dir).rmdir()
        if self._saved_env is not None:
            os.environ["PERMUTER_ALLOW_DIRECTIVE_DROP"] = self._saved_env

    def test_repro_wipe_is_blocked(self):
        """The exact f8a3a379 corruption: applying a variant that drops the
        #ifdef HX_NATIVE fork must be refused, leaving the file intact."""
        atomic_write_bytes(self.path, _FORKED_SOURCE)
        with self.assertRaises(DirectivePreservationError):
            atomic_write_bytes(self.path, _WIPED_SOURCE)
        # File on disk is unchanged — the native fork survives.
        self.assertEqual(self.path.read_bytes(), _FORKED_SOURCE)

    def test_legitimate_matched_branch_edit_is_allowed(self):
        """Optimizing only the matched (#else) branch, directives intact,
        must go through cleanly."""
        atomic_write_bytes(self.path, _FORKED_SOURCE)
        atomic_write_bytes(self.path, _MATCHED_BRANCH_EDIT)
        self.assertEqual(self.path.read_bytes(), _MATCHED_BRANCH_EDIT)

    def test_first_create_is_allowed(self):
        """Creating a brand-new file (no prior on-disk content) is never
        blocked, even if it has no directives."""
        atomic_write_bytes(self.path, _WIPED_SOURCE)
        self.assertEqual(self.path.read_bytes(), _WIPED_SOURCE)

    def test_banner_add_and_strip_preserve_directives(self):
        """The hill_climber lock banner is prepended/stripped via
        atomic_write_bytes; that must not trip the guard."""
        atomic_write_bytes(self.path, _FORKED_SOURCE)
        atomic_write_bytes(self.path, _BANNER + _FORKED_SOURCE)  # add banner
        self.assertEqual(self.path.read_bytes(), _BANNER + _FORKED_SOURCE)
        atomic_write_bytes(self.path, _FORKED_SOURCE)  # strip banner
        self.assertEqual(self.path.read_bytes(), _FORKED_SOURCE)

    def test_restore_original_is_allowed(self):
        """Restoring the (directive-rich) original over a (probe) state never
        reduces the count, so a restore always passes."""
        atomic_write_bytes(self.path, _FORKED_SOURCE)
        # A restore writes the original back — same counts → allowed.
        atomic_write_bytes(self.path, _FORKED_SOURCE)
        self.assertEqual(self.path.read_bytes(), _FORKED_SOURCE)

    def test_adding_a_new_fork_is_allowed(self):
        """A variant that ADDS directives (more conditionals than before) is
        fine — the guard only blocks reductions."""
        atomic_write_bytes(self.path, _WIPED_SOURCE)  # no directives
        atomic_write_bytes(self.path, _FORKED_SOURCE)  # adds the fork
        self.assertEqual(self.path.read_bytes(), _FORKED_SOURCE)

    def test_opt_out_env_allows_drop(self):
        """The escape hatch lets a deliberate fork-removal through."""
        atomic_write_bytes(self.path, _FORKED_SOURCE)
        os.environ["PERMUTER_ALLOW_DIRECTIVE_DROP"] = "1"
        try:
            atomic_write_bytes(self.path, _WIPED_SOURCE)
        finally:
            os.environ.pop("PERMUTER_ALLOW_DIRECTIVE_DROP", None)
        self.assertEqual(self.path.read_bytes(), _WIPED_SOURCE)


class TestEphemeralPaths(unittest.TestCase):
    def test_working_copy_is_ephemeral(self):
        self.assertTrue(_is_ephemeral_path(Path("/repo/src/.permuter_work_Mtx.h")))
        self.assertTrue(_is_ephemeral_path(Path("/repo/src/.permuter_work_3_Mtx.h")))
        self.assertTrue(_is_ephemeral_path(Path("/repo/src/.permuter_pp_out_Mtx.h")))

    def test_samename_tempdir_is_ephemeral(self):
        # Same-basename work file inside a permuter-prefixed temp dir.
        self.assertTrue(
            _is_ephemeral_path(Path("/repo/src/.permuter_fast_123_456/Mtx.h"))
        )
        self.assertTrue(_is_ephemeral_path(Path("/tmp/permuter_abc/Mtx.h")))

    def test_tmp_suffix_is_ephemeral(self):
        self.assertTrue(_is_ephemeral_path(Path("/repo/src/Mtx.h.tmp")))

    def test_real_source_is_not_ephemeral(self):
        self.assertFalse(_is_ephemeral_path(Path("/repo/src/system/math/Mtx.h")))
        self.assertFalse(_is_ephemeral_path(Path("/repo/src/band3/game/Game.cpp")))

    def test_ephemeral_write_skips_guard(self):
        """Writing a directive-dropping variant to a throwaway working copy is
        allowed (it's a compile input, validated then discarded)."""
        d = tempfile.mkdtemp(prefix="permuter_eph_test_")
        try:
            work = Path(d) / ".permuter_work_Mtx.h"
            atomic_write_bytes(work, _FORKED_SOURCE)
            # Dropping directives on the ephemeral path must NOT raise.
            atomic_write_bytes(work, _WIPED_SOURCE)
            self.assertEqual(work.read_bytes(), _WIPED_SOURCE)
        finally:
            for p in Path(d).iterdir():
                p.unlink()
            Path(d).rmdir()


if __name__ == "__main__":
    unittest.main()
