#!/usr/bin/env python3
"""`scripts/check_alias_map_soundness.py` is only a gate if the GENERATED
build.ninja runs it.

The script re-tests `scripts/symbol_aliases.json` with relocations charged. A
false entry in that map makes objdiff forgive a real call-target difference, so
a row scores 100.0 under every ruler this repo has; two REAL defects hid inside
that fold (`CharClip::EvaluateChannel`, `CharBonesSamples::Print`). The script
being correct and uninvoked is the state it shipped in: correct docs plus a
correct helper that nothing runs enforces nothing.

What this file pins, all from the PARSED manifest rather than from source text:

  * an edge PRODUCES `alias_map_soundness_checked.stamp`, built by `run_script`;
  * its command still runs `scripts/check_alias_map_soundness.py --check`;
  * it is CHAINED on `objs_patched_verified.stamp` (so it reads patched
    objects), on `all_source` (so a recompiled object re-fires it), on the
    check's own source (so editing the check re-fires it) and on
    `scripts/symbol_aliases.json` (the file under test -- `all_source` is a
    phony over .obj files and does NOT cover it);
  * the NEXT consumer, `undefined_symbols_checked.stamp`, depends on it, so the
    gate is not a dangling stamp and the report edges are reached through it;
  * it appears in the `post-compile` phony, AFTER its predecessor.

A whole-file substring grep is exactly what this repo has been burned by -- a
manifest with the edge removed still contains every string the step is built
from. So the parser is structural and every check reads edge fields.

The detector is shown failing on the REAL manifest with one targeted edit per
case; a case that does not go red is a broken test, not a passing one.

Run:  python3 -m pytest tests/test_alias_map_soundness_wiring.py -q
      python3 tests/test_alias_map_soundness_wiring.py      (unittest fallback)
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
BUILD_NINJA = REPO_ROOT / "build.ninja"

STAMP = "build/373307D9/alias_map_soundness_checked.stamp"
PREV = "build/373307D9/objs_patched_verified.stamp"
NEXT = "build/373307D9/undefined_symbols_checked.stamp"
PHONY = "post-compile"
SCRIPT = "scripts/check_alias_map_soundness.py"
MAP = "scripts/symbol_aliases.json"

#: ninja_syntax breaks lines at 78 columns with a trailing `$`.
_CONT = re.compile(r" \$\n\s*")
_BUILD = re.compile(r"^build\s+(.+?):\s*([A-Za-z0-9_.-]+)\s*(.*)$")


def unwrap(text: str) -> str:
    return _CONT.sub(" ", text)


def parse_manifest(text: str) -> dict:
    """`{output: {rule, explicit, implicit, order_only, vars}}`.

    Structural, not textual: an edge's inputs are what follows `|` / `||` on
    its `build` line, and its command is the `cmd` binding attached to it.
    """
    edges: dict = {}
    cur = None
    for line in unwrap(text).splitlines():
        if not line.strip():
            cur = None
            continue
        m = _BUILD.match(line)
        if m:
            outs_decl, rule, rest = m.groups()
            out_tokens = outs_decl.split("|")
            outs = out_tokens[0].split()
            order_only = []
            if "||" in rest:
                rest, tail = rest.split("||", 1)
                order_only = tail.split()
            implicit = []
            if "|" in rest:
                rest, tail = rest.split("|", 1)
                implicit = tail.split()
            edge = dict(rule=rule, explicit=rest.split(), implicit=implicit,
                        order_only=order_only, vars={})
            for out in outs:
                edges[out] = edge
            cur = edge
            continue
        if cur is not None and line[:1] in (" ", "\t"):
            key, sep, value = line.strip().partition(" = ")
            if sep:
                cur["vars"][key] = value
        else:
            cur = None
    return edges


def wire_problems(edges: dict) -> list:
    """Every way the alias-map gate can be absent or ineffective, named."""
    out = []
    edge = edges.get(STAMP)
    if edge is None:
        return [f"no edge in build.ninja produces {STAMP} -- the alias-map "
                f"soundness check is NOT wired into the build"]
    if edge["rule"] != "run_script":
        out.append(f"{STAMP} is built by rule {edge['rule']!r}, not 'run_script'")

    cmd = edge["vars"].get("cmd", "")
    if SCRIPT not in cmd:
        out.append(f"{STAMP}'s command does not run {SCRIPT}: {cmd!r}")
    if "--check" not in cmd.split():
        out.append(f"{STAMP}'s command is not in gate mode (no --check): {cmd!r}")

    for dep, why in ((PREV, "reads UNPATCHED objects -- the two defects it "
                            "exists to catch are call-target differences, which "
                            "the anon-ns/guard renames can change"),
                     ("all_source", "a recompiled object would not re-fire it "
                                    "(order-only is not enough; see configure.py)")):
        if dep not in edge["implicit"]:
            out.append(f"{STAMP} does not depend on {dep} -- {why}")

    if SCRIPT not in edge["implicit"]:
        out.append(f"{STAMP} does not depend on its own source {SCRIPT}, so "
                   f"editing the check re-fires nothing")
    if MAP not in edge["implicit"]:
        out.append(f"{STAMP} does not depend on {MAP}, the file under test "
                   f"(`all_source` covers .obj files only)")

    nxt = edges.get(NEXT)
    if nxt is None:
        out.append(f"no edge produces {NEXT}, the gate's consumer")
    elif STAMP not in nxt["implicit"] and STAMP not in nxt["explicit"]:
        out.append(f"{NEXT} does not depend on {STAMP} -- the gate's stamp is "
                   f"dangling, so nothing downstream is ordered behind it")

    phony = edges.get(PHONY)
    if phony is None:
        out.append(f"no `{PHONY}` phony edge")
    else:
        order = phony["explicit"] + phony["implicit"]
        if STAMP not in order:
            out.append(f"`{PHONY}` does not depend on {STAMP}, so `ninja "
                       f"post-compile` and the report edges never reach it")
        elif PREV in order and order.index(STAMP) < order.index(PREV):
            out.append(f"`{PHONY}` lists {STAMP} BEFORE {PREV}; the gate must "
                       f"read objects the patch passes have already finished")
    return out


def _block_span(lines, out):
    start = None
    for i, line in enumerate(lines):
        if line.startswith(f"build {out}:") or line.startswith(f"build {out} "):
            start = i
            break
    if start is None:
        raise AssertionError(f"the fixture has no edge for {out}; cannot sabotage it")
    end = start + 1
    while end < len(lines) and lines[end].strip() and lines[end][:1] in (" ", "\t"):
        end += 1
    return start, end


def without_edge(text, out):
    lines = unwrap(text).splitlines()
    start, end = _block_span(lines, out)
    return "\n".join(lines[:start] + lines[end:])


def edit_edge(text, out, fn):
    lines = unwrap(text).splitlines()
    start, end = _block_span(lines, out)
    return "\n".join(lines[:start] + fn(lines[start:end]) + lines[end:])


class ManifestFixture(unittest.TestCase):
    """The REAL generated manifest of the tree this test runs in."""

    @classmethod
    def setUpClass(cls):
        if not BUILD_NINJA.is_file():
            raise unittest.SkipTest(
                f"no {BUILD_NINJA} -- run `python3 configure.py` in this tree; "
                f"an unconfigured tree cannot vouch for the wiring"
            )
        cls.text = BUILD_NINJA.read_text(encoding="utf-8", errors="replace")
        cls.edges = parse_manifest(cls.text)
        # Anti-vacuity: a parser that silently returned {} would make every
        # "the step is missing" case below pass for the wrong reason.
        assert cls.edges, "parsed no edges out of a non-empty build.ninja"

    def assert_problems(self, edges, expected_substrings, exactly=None):
        problems = wire_problems(edges)
        self.assertTrue(problems, "expected the detector to fail; it passed")
        if exactly is not None:
            self.assertEqual(len(problems), exactly,
                             f"expected exactly {exactly} problem(s), got "
                             f"{len(problems)}:\n  " + "\n  ".join(problems))
        for want in expected_substrings:
            self.assertTrue(any(want in p for p in problems),
                            f"no problem mentions {want!r}; got:\n  "
                            + "\n  ".join(problems))
        return problems


class TestWiredIntoGeneratedManifest(ManifestFixture):

    def test_generated_manifest_wires_the_check(self):
        self.assertEqual(wire_problems(self.edges), [],
                         "the alias-map soundness check is not correctly wired "
                         "into the post-compile chain")

    def test_the_edge_is_what_we_think_it_is(self):
        """Pin the shape, so a passing `wire_problems` cannot come from a
        different edge that happens to carry the same output name."""
        edge = self.edges[STAMP]
        self.assertEqual(edge["rule"], "run_script")
        self.assertEqual(edge["vars"].get("cmd"),
                         "python3 scripts/check_alias_map_soundness.py --check")
        self.assertIn(PREV, edge["implicit"])
        self.assertIn(MAP, edge["implicit"])
        self.assertIn(SCRIPT, edge["implicit"])
        self.assertIn("all_source", edge["implicit"])


class TestTheDetectorCanFail(ManifestFixture):
    """One targeted edit per failure mode, applied to the REAL manifest."""

    def test_absent_edge_is_named(self):
        sabotaged = without_edge(self.text, STAMP)
        edges = parse_manifest(sabotaged)
        # The sabotage landed on one edge and not the whole file.
        self.assertNotIn(STAMP, edges)
        self.assertIn(NEXT, edges)
        self.assert_problems(edges, ["no edge in build.ninja produces", STAMP],
                             exactly=1)

    def test_unchained_step_is_named(self):
        sabotaged = edit_edge(
            self.text, STAMP,
            lambda ls: [ln.replace(PREV, "build/373307D9/data_stubs.stamp")
                        for ln in ls])
        self.assertIn(PREV, parse_manifest(self.text)[STAMP]["implicit"])
        self.assert_problems(parse_manifest(sabotaged),
                             [f"does not depend on {PREV}"], exactly=1)

    def test_command_that_stops_naming_the_check_is_named(self):
        sabotaged = edit_edge(
            self.text, STAMP,
            lambda ls: [ln.replace("cmd = python3 scripts/check_alias_map_soundness.py --check",
                                   "cmd = true") for ln in ls])
        edges = parse_manifest(sabotaged)
        self.assertEqual(edges[STAMP]["vars"]["cmd"], "true")
        self.assert_problems(edges, ["does not run", "not in gate mode"],
                             exactly=2)

    def test_dangling_next_consumer_is_named(self):
        sabotaged = edit_edge(
            self.text, NEXT,
            lambda ls: [ln.replace(f" {STAMP}", "") for ln in ls])
        edges = parse_manifest(sabotaged)
        self.assertIn(STAMP, edges)  # the gate edge itself survives
        self.assert_problems(edges, ["does not depend on " + STAMP], exactly=1)

    def test_step_missing_from_the_phony_is_named(self):
        sabotaged = edit_edge(
            self.text, PHONY,
            lambda ls: [ln.replace(f" {STAMP}", "") for ln in ls])
        self.assert_problems(parse_manifest(sabotaged),
                             [f"`{PHONY}` does not depend on {STAMP}"])

    def test_map_dependency_removal_is_named(self):
        sabotaged = edit_edge(
            self.text, STAMP,
            lambda ls: [ln.replace(f" {MAP}", "") for ln in ls])
        self.assert_problems(parse_manifest(sabotaged),
                             [f"does not depend on {MAP}"], exactly=1)


if __name__ == "__main__":
    unittest.main(verbosity=2)
