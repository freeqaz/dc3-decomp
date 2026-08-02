"""Unit tests for the state-diff tooling.

These run with no engine: they exercise the probe compiler, the budget
validator, the normalizer, the differ's ranking/collapsing rules and the noise
machinery against synthetic snapshots and a ReplayTarget.

    python3 -m pytest tools/state_diff/tests/ -q          (from repo root)
    python3 tools/state_diff/tests/test_state_diff.py     (no pytest needed)
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from state_diff import budget, diff, normalize, probe  # noqa: E402
from state_diff.budget import BudgetError, Limits  # noqa: E402
from state_diff.normalize import Snapshot  # noqa: E402
from state_diff.transport import EvalResult, ObjRef, ReplayTarget  # noqa: E402


# --------------------------------------------------------------------------
# budget
# --------------------------------------------------------------------------

def test_brace_balance():
    assert budget.brace_balance("{a {b} (c)}")[0]
    assert not budget.brace_balance("{a {b}")[0]
    assert not budget.brace_balance("{a)}")[0]
    assert not budget.brace_balance('{a "unterminated}')[0]
    # Brackets inside strings must not count.
    assert budget.brace_balance('{find_obj main "[ui.cam]"}')[0]
    assert budget.brace_balance('{x "a{b"}')[0]


def test_top_level_command_count():
    assert budget.count_top_level_commands("{a}{b}") == 2
    assert budget.count_top_level_commands("{do {a}{b}}") == 1


def test_validate_rejects_unbalanced_even_when_unenforced():
    # An unbalanced script faults the title on every transport, so brace
    # checking is unconditional.
    try:
        budget.validate_script("{a", Limits.unlimited())
    except BudgetError as e:
        assert "unbalanced" in str(e)
    else:
        raise AssertionError("expected BudgetError")


def test_validate_rejects_oversize_and_multi_command():
    try:
        budget.validate_script("{" + "a" * 500 + "}", Limits(max_script=100))
    except BudgetError as e:
        assert "exceeds script cap" in str(e)
    else:
        raise AssertionError("expected size BudgetError")

    try:
        budget.validate_script("{a}{b}", Limits(max_script=1000))
    except BudgetError as e:
        assert "top-level commands" in str(e)
    else:
        raise AssertionError("expected one-command BudgetError")


# --------------------------------------------------------------------------
# probe compilation
# --------------------------------------------------------------------------

def test_all_shipped_probes_fit_the_portable_budget():
    """Every probe must page cleanly under the portable caps.

    This is the check that stops a probe which only works on localhost from
    reaching a live console session.
    """
    names = [f"object_name_{i}" for i in range(60)]
    for pid, p in probe.load_all().items():
        if p.kind == "scalars":
            for prog in p.programs:
                budget.validate_script(prog, Limits.portable(), pid)
            continue
        pages = p.pages(names, Limits.portable())
        assert pages, pid
        for pg in pages:
            budget.validate_script(pg.program, Limits.portable(), pid)


def test_emitted_reads_carry_a_default():
    """`{$o get (x)}` with 3 nodes hard-fails on console; a default is required."""
    f = probe.Field("showing", "prop", ["showing"], "bool")
    assert f.value_expr() == "{$o get (showing) 0}"


def test_isa_gate_wraps_the_field_reads():
    """The is_a check must gate reads, not just be reported next to them."""
    p = probe.Probe(id="t", scope=probe.Scope(isa=["Draw"], guard=False),
                    fields=[probe.Field("showing", "prop", ["showing"], "bool")])
    block = p._object_block("thing", p.fields)
    gate_at = block.index("{$o is_a Draw}")
    read_at = block.index("{$o get (showing) 0}")
    assert gate_at < read_at
    assert "{if_else {$o is_a Draw}" in block


def test_script_objects_are_excluded_by_default():
    """Bare `Object`s are DTA script objects: messaging them runs game script."""
    sc = probe.Scope(isa=["Draw"])
    assert "Object" in sc.exclude_classes
    roster = [ObjRef("script_thing", "Object"), ObjRef("a_mesh", "Mesh")]
    assert [r.name for r in sc.select(roster)] == ["a_mesh"]


def test_unenumerated_names_are_refused():
    try:
        probe.assert_enumerated(["ghost"], {"real"}, "t")
    except probe.UnenumeratedName as e:
        assert "two-pass" in str(e)
    else:
        raise AssertionError("expected UnenumeratedName")


def test_unsafe_names_are_refused():
    try:
        probe.assert_enumerated(['bad"name'], {'bad"name'}, "t")
    except probe.UnenumeratedName as e:
        assert "unsafe" in str(e)
    else:
        raise AssertionError("expected UnenumeratedName")
    # Real DC3 names with brackets/spaces are fine inside a quoted string.
    probe.assert_enumerated(["[ui.cam]", "[default lit]"],
                            {"[ui.cam]", "[default lit]"}, "t")


def test_roundtrip_against_replay_target():
    """Compile -> (replayed) reply -> parse, with the real separators."""
    p = probe.Probe(
        id="t", scope=probe.Scope(dir="main", guard=False),
        fields=[probe.Field("showing", "prop", ["showing"], "bool"),
                probe.Field("mat", "prop", ["mat"], "obj")])
    names = ["alpha", "beta"]
    pages = p.pages(names, Limits.portable())
    assert len(pages) == 1
    F, R = probe.FIELD_SEP, probe.RECORD_SEP
    reply = f"1{F}1{F}red_mat{F}{R}1{F}0{F}<null>{F}{R}"
    target = ReplayTarget({pages[0].program: {"ok": True, "type": "symbol",
                                              "value": reply}},
                          roster_data=[{"name": "alpha", "type": "Mesh"},
                                       {"name": "beta", "type": "Mesh"}])
    recs, stats = probe.run_probe(target, p, Limits.portable())
    assert recs["alpha"] == {"showing": "1", "mat": "red_mat", "_class": "Mesh"}
    assert recs["beta"]["mat"] == "<null>"
    assert stats.eval_failures == 0


def test_short_batch_is_never_attributed():
    """A transport returning too few results must fail, not misalign."""
    class ShortBatch(ReplayTarget):
        def eval_batch(self, exprs, timeout=30.0):
            return []  # fewer results than commands
    p = probe.Probe(id="t", scope=probe.Scope(guard=False),
                    fields=[probe.Field("showing", "prop", ["showing"], "bool")])
    t = ShortBatch({}, roster_data=[{"name": "a", "type": "Mesh"}])
    recs, stats = probe.run_probe(t, p, Limits.portable())
    assert recs["a"]["_error"] == "batch attribution refused"
    assert any("refusing to attribute" in e for e in stats.errors)


# --------------------------------------------------------------------------
# normalizer
# --------------------------------------------------------------------------

def test_color_unpacks_to_channels():
    c = normalize.unpack_color(str(0xFF00FF00))
    assert c == {"r": 0, "g": 255, "b": 0, "a": 255}


def test_float_tolerance_classes_differ():
    # rotation is rounded harder than translation, because euler angles are
    # derived from the matrix and fragile near gimbal lock.
    assert normalize.tolerance_class("world_pitch") == "rotation"
    assert normalize.tolerance_class("world_x") == "translation"
    assert normalize.canon_float("1.00000004", "world_x") == 1.0
    assert normalize.canon_float("-0.0", "world_x") == 0.0


def test_paths_are_platform_normalized():
    assert normalize.canon_path("D:\\Game\\Tex\\Foo.BMP") == "/game/tex/foo.bmp"
    assert normalize.canon_path("game:/ui//x.png") == "/ui/x.png"


def test_addresses_are_scrubbed():
    assert normalize.scrub_addresses("obj_0x8241ab30") == "obj_<addr>"


def test_enums_decode():
    assert normalize.canon_value("2", "blend", "int") == "kBlendAdd"
    assert normalize.canon_value("1", "z_mode", "int") == "kZModeNormal"


def test_absent_and_null_are_distinct():
    assert normalize.canon_value(probe.ABSENT, "mat", "obj") is None
    assert normalize.canon_value("<null>", "mat", "obj") == "<null>"


# --------------------------------------------------------------------------
# differ
# --------------------------------------------------------------------------

def _snap(objs, target="native", probe_id="p", screen="main_screen", scalars=None):
    return Snapshot(probe=probe_id, target=target, meta={"screen": screen},
                    objects=objs, scalars=scalars or {})


def test_identical_snapshots_produce_nothing():
    a = _snap({"m": {"showing": True, "draw_order": 0.5}})
    b = _snap({"m": {"showing": True, "draw_order": 0.5}}, target="console")
    assert diff.diff_snapshots(a, b) == []


def test_sub_tolerance_float_is_not_a_finding():
    a = _snap({"m": {"world_x": 1.0}})
    b = _snap({"m": {"world_x": 1.00001}}, target="console")
    assert diff.diff_snapshots(a, b) == []


def test_ranking_puts_binding_above_geometry():
    a = _snap({"m": {"mat": "red", "world_x": 0.0}})
    b = _snap({"m": {"mat": "<null>", "world_x": 99.0}}, target="console")
    f = diff.diff_snapshots(a, b)
    assert f[0].field == "mat" and f[0].severity == diff.CRITICAL
    assert f[0].category == "unbound"
    assert f[1].field == "world_x" and f[1].severity == diff.MEDIUM


def test_identical_changes_collapse_into_one_finding():
    a = _snap({f"mesh_{i}": {"mat": "shared"} for i in range(47)})
    b = _snap({f"mesh_{i}": {"mat": "<null>"} for i in range(47)},
              target="console")
    f = diff.diff_snapshots(a, b)
    assert len(f) == 1
    assert f[0].count == 47
    assert f[0].field == "mat"


def test_screen_mismatch_is_a_blocker():
    a = _snap({}, screen="main_screen")
    b = _snap({}, target="console", screen="song_select_screen")
    f = diff.diff_snapshots(a, b)
    assert f[0].severity == diff.BLOCKER and f[0].field == "screen"


def test_missing_objects_reported_once():
    a = _snap({"x": {"showing": True}, "y": {"showing": True}})
    b = _snap({"x": {"showing": True}}, target="console")
    f = diff.diff_snapshots(a, b)
    assert any(x.field == "__missing__" and x.objects == ["y"] for x in f)


def test_zero_transitions_escalate():
    # A collapsed bounding sphere and an unloaded texture are worse than a
    # plain numeric change in the same field.
    a = _snap({"m": {"sphere_radius": 5.0, "size_kb": 128}})
    b = _snap({"m": {"sphere_radius": 0.0, "size_kb": 0}}, target="console")
    sev = {x.field: x.severity for x in diff.diff_snapshots(a, b)}
    assert sev["sphere_radius"] == diff.CRITICAL
    assert sev["size_kb"] == diff.CRITICAL


def test_noise_profile_suppresses_and_annotates():
    a = _snap({"m": {"draw_order": 1.0}})
    b = _snap({"m": {"draw_order": 9.0}}, target="console")
    prof = {"unstable": {"p": {"*": ["draw_order"]}}}
    assert diff.diff_snapshots(a, b, prof) == []
    inc = diff.diff_snapshots(a, b, prof, include_unstable=True)
    assert len(inc) == 1 and inc[0].unstable and inc[0].severity == diff.INFO


def test_schema_difference_when_field_absent_on_one_side():
    a = _snap({"m": {"showing": True}})
    b = _snap({"m": {"showing": None}}, target="console")
    f = diff.diff_snapshots(a, b)
    assert f[0].category == "schema"


# --------------------------------------------------------------------------
# noise machinery
# --------------------------------------------------------------------------

def test_noise_detects_and_generalizes_a_wobbling_field():
    """The measurement must catch a field that moves, and generalize it."""
    from state_diff import noise

    class Wobble:
        """Target whose `draw_order` changes every capture."""
        def __init__(self):
            self.n = 0

    p = probe.Probe(id="p", scope=probe.Scope(guard=False),
                    fields=[probe.Field("showing", "prop", ["showing"], "bool"),
                            probe.Field("draw_order", "prop", ["draw_order"],
                                        "float")])
    names = ["a", "b"]
    pages = p.pages(names, Limits.portable())
    F, R = probe.FIELD_SEP, probe.RECORD_SEP
    state = {"n": 0}

    class T(ReplayTarget):
        def eval_dta(self, expr, timeout=15.0):
            state["n"] += 1
            v = state["n"]
            return EvalResult(ok=True, type="symbol",
                              value=f"1{F}1{F}{v}.0{F}{R}1{F}1{F}{v}.0{F}{R}")

    t = T({}, roster_data=[{"name": n, "type": "Mesh"} for n in names])
    prof, snaps = noise.measure(t, p, Limits.portable(), runs=4, settle_s=0)
    assert prof["summary"]["unstable_cells"] == 2      # 2 objects x 1 field
    assert prof["unstable"]["p"]["*"] == ["draw_order"]  # generalized
    assert "showing" not in prof["unstable"]["p"]["*"]


def test_portable_cap_is_one_byte_under_the_console_limit():
    """The console rejects a body of EXACTLY 16384; native accepts it.

    Portable must therefore be `< 16384`. Getting this wrong produces pages
    that pass every local test and fail exactly once on hardware.
    """
    portable, native = Limits.portable(), Limits.native_http()
    assert portable.max_script == 16383
    assert native.max_script == 16384

    ok_16383 = "{" + "a" * 16381 + "}"
    at_16384 = "{" + "a" * 16382 + "}"
    assert len(ok_16383) == 16383 and len(at_16384) == 16384

    budget.validate_script(ok_16383, portable)          # fits both sides
    budget.validate_script(at_16384, native)            # native accepts it
    try:
        budget.validate_script(at_16384, portable)      # console would 413
    except BudgetError as e:
        assert "exceeds script cap" in str(e)
    else:
        raise AssertionError("portable must reject a body of exactly 16384")


def test_no_probe_page_can_reach_the_console_reject_size():
    names = [f"object_name_{i}" for i in range(60)]
    for pid, p in probe.load_all().items():
        if p.kind == "scalars":
            for prog in p.programs:
                assert len(prog) < 16384, pid
            continue
        for pg in p.pages(names, Limits.portable()):
            assert len(pg.program) < 16384, (pid, len(pg.program))


def test_non_finite_floats_survive_decoding():
    """NaN/Inf arrive as null + a `special` field; reading `value` alone
    would silently discard exactly the bug worth finding."""
    from state_diff.transport import decode_node
    assert decode_node({"type": "float", "value": None,
                        "special": "nan"})["value"] == "NaN"
    assert decode_node({"type": "float", "value": None,
                        "special": "inf"})["value"] == "Inf"
    assert decode_node({"type": "float", "value": None,
                        "special": "-inf"})["value"] == "-Inf"
    # and they normalize to something a diff can compare
    assert normalize.canon_float("NaN", "world_x") == "NaN"


def test_base64_payloads_are_decoded():
    from state_diff.transport import decode_node
    assert decode_node({"type": "string", "encoding": "base64",
                        "value": "aGk="})["value"] == "hi"
    assert decode_node({"type": "string", "encoding": "utf8",
                        "value": "hi"})["value"] == "hi"


def test_payload_is_not_interned_as_a_symbol():
    """Pages return the raw string; wrapping in {symbol ...} would leak a
    unique symbol per page, forever."""
    p = probe.Probe(id="t", scope=probe.Scope(guard=False),
                    fields=[probe.Field("showing", "prop", ["showing"], "bool")])
    prog = p.pages(["a"], Limits.portable())[0].program
    assert prog.endswith("$s}")
    assert "{symbol" not in prog


def _run_all():
    fns = [(n, f) for n, f in sorted(globals().items())
           if n.startswith("test_") and callable(f)]
    failed = 0
    for name, fn in fns:
        try:
            fn()
            print(f"  PASS {name}")
        except Exception as e:  # noqa: BLE001
            failed += 1
            print(f"  FAIL {name}: {type(e).__name__}: {e}")
    print(f"\n{len(fns) - failed}/{len(fns)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(_run_all())
