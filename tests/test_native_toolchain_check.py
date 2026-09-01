"""Sabotage tests for scripts/native_toolchain_check.py.

The checker exists because ninja could not see gtest 1.17 -> 1.18 arrive
(2026-09-01): pacman restores upstream mtimes, so the NEWER files landed with an
OLDER mtime than build.ninja and every mtime-based staleness rule stayed green
over a build dir whose binaries could not even be exec'd.

So the load-bearing property is not "it notices a change" -- it is "it notices a
change THAT AN MTIME COMPARISON WOULD MISS". test_drift_is_detected_when_mtime_goes_backwards
is that assertion, and it is written so that a checker which fell back to mtimes
would fail it.

Every case here is a deliberate defect; test_healthy_build_dir_is_current is the
vacuity control that fails a checker which simply always complains.
"""

import os
import shutil
import subprocess
import sys
import textwrap

import pytest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CHECKER = os.path.join(REPO_ROOT, "scripts", "native_toolchain_check.py")


def run(*args):
    return subprocess.run(
        [sys.executable, CHECKER] + list(args),
        capture_output=True,
        text=True,
        timeout=120,
    )


@pytest.fixture
def build_dir(tmp_path):
    """A minimal fake build dir: a build.ninja naming two external inputs."""
    ext = tmp_path / "sysroot"
    ext.mkdir()
    lib = ext / "libfake.so.1.17.0"
    lib.write_bytes(b"gtest 1.17 content")
    mod = ext / "FakeConfig.cmake"
    mod.write_text("set(FAKE_VERSION 1.17.0)\n")

    bd = tmp_path / "build"
    bd.mkdir()
    (bd / "build.ninja").write_text(
        textwrap.dedent(
            """\
            build fake-target: LINK obj/a.o {lib}
            build build.ninja: RERUN_CMAKE {mod}
            """
        ).format(lib=lib, mod=mod)
    )
    return bd, lib, mod


def test_healthy_build_dir_is_current(build_dir):
    """Vacuity control: a checker that always complains fails here."""
    bd, _, _ = build_dir
    assert run("--record", str(bd)).returncode == 0
    res = run("--check", str(bd))
    assert res.returncode == 0, res.stdout + res.stderr
    assert "current" in res.stdout


def test_no_fingerprint_is_its_own_code(build_dir):
    bd, _, _ = build_dir
    res = run("--check", str(bd))
    assert res.returncode == 4
    assert "NO FINGERPRINT" in res.stdout


def test_missing_external_input_is_moved(build_dir):
    """The loud form: the soname was deleted by the upgrade."""
    bd, lib, _ = build_dir
    run("--record", str(bd))
    lib.unlink()
    res = run("--check", str(bd))
    assert res.returncode == 2, res.stdout
    assert "TOOLCHAIN MOVED" in res.stdout
    assert str(lib) in res.stdout
    assert "REMEDY: clean-rebuild" in res.stdout


def test_drift_is_detected_when_mtime_goes_backwards(build_dir):
    """The point of the whole script.

    An in-place library replacement whose mtime is OLDER than the fingerprint is
    exactly what pacman produces and exactly what ninja cannot see. A checker
    that compared mtimes -- or that trusted 'newer than' in any form -- would
    report this tree as current.
    """
    bd, lib, _ = build_dir
    run("--record", str(bd))
    fp_mtime = os.path.getmtime(bd / "toolchain_fingerprint.txt")

    lib.write_bytes(b"gtest 1.18 content, different length entirely")
    old = fp_mtime - 86400 * 3
    os.utime(lib, (old, old))
    assert os.path.getmtime(lib) < fp_mtime  # the trap, made explicit

    res = run("--check", str(bd))
    assert res.returncode == 3, res.stdout
    assert "TOOLCHAIN DRIFTED" in res.stdout
    assert "library content changed" in res.stdout
    assert "REMEDY: clean-rebuild" in res.stdout


def test_cmake_module_drift_asks_only_for_reconfigure(build_dir):
    """Proportionality: a cmake module change alters rules, not object code."""
    bd, _, mod = build_dir
    run("--record", str(bd))
    mod.write_text("set(FAKE_VERSION 1.18.0)\n")
    res = run("--check", str(bd))
    assert res.returncode == 3, res.stdout
    assert "cmake module changed" in res.stdout
    assert "REMEDY: reconfigure" in res.stdout
    assert "clean-rebuild" not in res.stdout


def test_missing_build_ninja_is_an_error_not_a_pass(tmp_path):
    res = run("--check", str(tmp_path))
    assert res.returncode == 1
    assert "no build.ninja" in res.stderr


@pytest.mark.skipif(
    shutil.which("cc") is None or shutil.which("ldd") is None,
    reason="needs a C compiler and ldd to build the unloadable-binary fixture",
)
def test_unloadable_binary_is_detected_independently_of_build_ninja(tmp_path):
    """The check that would have said, in one line, what took an hour.

    main's milo-tests had six unresolvable DT_NEEDED entries. This tier reads
    the ARTIFACT, not build.ninja, so it still fires on a build dir that was
    reconfigured (fresh build.ninja, no missing inputs) but never rebuilt.
    """
    sysroot = tmp_path / "sysroot"
    sysroot.mkdir()
    (tmp_path / "dep.c").write_text("int dep_fn(void){return 7;}\n")
    subprocess.run(
        ["cc", "-shared", "-fPIC", "-o", str(sysroot / "libdep.so.1"),
         "-Wl,-soname,libdep.so.1", str(tmp_path / "dep.c")],
        check=True, timeout=120,
    )
    (tmp_path / "main.c").write_text("int dep_fn(void);int main(void){return dep_fn();}\n")

    bd = tmp_path / "build"
    bd.mkdir()
    # No external inputs named in build.ninja at all: the ONLY way to catch this
    # is to interrogate the binary.
    (bd / "build.ninja").write_text("build prog: LINK obj/main.o\n")
    subprocess.run(
        ["cc", "-o", str(bd / "prog"), str(tmp_path / "main.c"),
         "-L", str(sysroot), "-l:libdep.so.1", "-Wl,-rpath," + str(sysroot)],
        check=True, timeout=120,
    )
    (bd / "milo_test_required_targets.txt").write_text("prog\n")

    assert run("--record", str(bd)).returncode == 0
    assert run("--check", str(bd)).returncode == 0, "fixture must start healthy"

    (sysroot / "libdep.so.1").unlink()

    res = run("--check", str(bd))
    assert res.returncode == 2, res.stdout
    assert "cannot load" in res.stdout
    assert "libdep.so.1" in res.stdout
