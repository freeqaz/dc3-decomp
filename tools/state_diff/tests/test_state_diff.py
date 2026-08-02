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

from state_diff import budget, diff, normalize, probe, sweep  # noqa: E402
from state_diff.budget import BudgetError, Limits  # noqa: E402
from state_diff.normalize import Snapshot  # noqa: E402
from state_diff.transport import (DirSpecError, EvalResult,  # noqa: E402
                                  NativeHttpTarget, ObjRef, ReplayTarget,
                                  Target, dir_expr, is_main_dir)

PANEL = "panel:main_panel"


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


# --------------------------------------------------------------------------
# dir scopes (GAP 1: reaching inside a loaded .milo panel dir)
# --------------------------------------------------------------------------

def test_dir_expr_accepts_the_three_documented_forms():
    assert dir_expr(None) == "main"
    assert dir_expr("main") == "main"
    assert dir_expr(PANEL) == "{main_panel loaded_dir}"
    assert dir_expr("{main_panel loaded_dir}") == "{main_panel loaded_dir}"
    assert is_main_dir("main") and is_main_dir("") and not is_main_dir(PANEL)


def test_dir_expr_refuses_injection_and_unbalanced_dta():
    for bad in ["panel:{oops", "panel:a b", 'main"} {print "pwn', "{unbalanced"]:
        try:
            dir_expr(bad)
        except DirSpecError:
            pass
        else:
            raise AssertionError(f"expected DirSpecError for {bad!r}")


def test_panel_scope_binds_the_dir_once_per_page():
    """The dir expression is bound to $d ONCE, not inlined per object.

    Inlining would send the panel one `loaded_dir` message per property read
    and inflate every object block, silently shrinking objects-per-page for
    exactly the scopes this feature exists to serve.
    """
    p = probe.Probe(id="t", scope=probe.Scope(dir=PANEL, guard=False),
                    fields=[probe.Field("showing", "prop", ["showing"], "bool")])
    prog = p.pages(["motd.lbl", "parent_motd.trans"], Limits.portable())[0].program
    assert prog.count("{main_panel loaded_dir}") == 1
    assert prog.startswith('{do ($s "") ($o 0) ($p 0) ($d {main_panel loaded_dir})')
    assert prog.count('{find_obj $d "') == 2


def test_panel_scope_budget_accounts_for_the_dir_expression():
    """A long dir expression eats into the same script cap as the objects."""
    long_dir = "{%s loaded_dir}" % ("p" * 200)
    short = probe.Probe(id="t", scope=probe.Scope(dir="main"))
    long_ = probe.Probe(id="t", scope=probe.Scope(dir=long_dir))
    assert long_.wrapper_len() - short.wrapper_len() == len(long_dir) - len("main")


def test_all_shipped_probes_fit_the_portable_budget_in_a_panel_dir():
    names = [f"object_name_{i}" for i in range(60)]
    for pid, p in probe.load_all().items():
        if p.kind == "scalars":
            continue
        scoped = probe.rescope(p, dir_spec=PANEL)
        pages = scoped.pages(names, Limits.portable())
        assert pages, pid
        for pg in pages:
            budget.validate_script(pg.program, Limits.portable(), pid)
            assert len(pg.program) < 16384, (pid, len(pg.program))


def test_rescope_does_not_mutate_the_loaded_probe():
    """load_all() hands out one Probe per id and noise.py sweeps them in a
    loop, so an in-place override would leak into the next capture."""
    p = probe.load_all()["transforms"]
    before = (p.scope.dir, list(p.scope.names), p.scope.limit)
    q = probe.rescope(p, dir_spec=PANEL, names=["motd.lbl"], limit=3)
    assert (p.scope.dir, list(p.scope.names), p.scope.limit) == before
    assert q.scope.dir == PANEL and q.scope.names == ["motd.lbl"]
    assert q.scope.limit == 3 and q.fields is not None


def test_rescope_rejects_a_bad_dir_at_author_time():
    try:
        probe.rescope(probe.Probe(id="t"), dir_spec="panel:{nope")
    except DirSpecError:
        pass
    else:
        raise AssertionError("expected DirSpecError")


class _Recorder(Target):
    """Records every script it is asked to evaluate; replies from a queue."""

    def __init__(self, replies):
        self.scripts: list[str] = []
        self.replies = list(replies)

    def eval_dta(self, expr, timeout=15.0):
        self.scripts.append(expr)
        v = self.replies.pop(0) if self.replies else ""
        return EvalResult(ok=True, type="string", value=v)


def test_roster_defaults_to_object_list_with_a_real_cursor():
    """`object_list` returns a SORTED, indexable name array (Utl.cpp:289), so
    paging is a genuine cursor rather than a re-walk."""
    t = _Recorder(["#2;motd.lbl|HamLabel;parent_motd.trans|Trans;"])
    refs = t.roster(PANEL, isa=["Trans"])
    assert [(r.name, r.type) for r in refs] == [
        ("motd.lbl", "HamLabel"), ("parent_motd.trans", "Trans")]
    s = t.scripts[0]
    assert "{object_list $d Trans FALSE}" in s
    assert "{foreach_int $i 0 {min 250 {size $a}}}" not in s  # bounds are inline
    assert "{foreach_int $i 0 {min 250 {size $a}}" in s
    assert "($d {main_panel loaded_dir})" in s


def test_roster_object_list_pages_with_the_count_header():
    page = 3
    t = _Recorder(["#5;a|M;b|M;c|M;", "#5;d|M;e|M;"])
    refs = t.roster(PANEL, isa=["M"], page=page)
    assert [r.name for r in refs] == ["a", "b", "c", "d", "e"]
    assert len(t.scripts) == 2
    assert "{foreach_int $i 0 {min 3 " in t.scripts[0]
    assert "{foreach_int $i 3 {min 6 " in t.scripts[1]


def test_roster_object_list_refuses_to_page_without_a_count_header():
    """Paging blind would stop early and silently drop objects, which reads
    downstream as 'missing on one side'."""
    errs: list[str] = []
    t = _Recorder(["a|M;b|M;"])
    refs = t.roster(PANEL, isa=["M"], errors=errs)
    assert [r.name for r in refs] == ["a", "b"]
    assert any("no count header" in e for e in errs)


def test_roster_iterate_backend_pages_with_an_ordinal_window():
    """`iterate` has no cursor, so paging is an ordinal window. A full window
    must trigger another request; a short one must stop."""
    t = _Recorder(["a|M;b|M;c|M;", "d|M;e|M;"])
    refs = t.roster(PANEL, isa=["M"], page=3, method="iterate")
    assert [r.name for r in refs] == ["a", "b", "c", "d", "e"]
    assert len(t.scripts) == 2
    assert "{>= $n 0}" in t.scripts[0] and "{< $n 3}" in t.scripts[0]
    assert "{>= $n 3}" in t.scripts[1] and "{< $n 6}" in t.scripts[1]


def test_roster_uses_iterate_self_when_not_recursing():
    """object_list is ALWAYS recursive (Utl.cpp:292), so --no-recurse must
    route to iterate_self rather than silently recursing anyway."""
    t = _Recorder(["a|Mesh;"])
    t.roster(PANEL, isa=["Mesh"], recurse=False)
    assert "iterate_self Mesh" in t.scripts[0]
    try:
        t.roster(PANEL, isa=["Mesh"], recurse=False, method="object_list")
    except DirSpecError as e:
        assert "always recursive" in str(e)
    else:
        raise AssertionError("expected DirSpecError")


def test_roster_reports_malformed_entries_instead_of_dropping_them():
    errs: list[str] = []
    t = _Recorder(["#2;good|Mesh;mangled;"])
    refs = t.roster(PANEL, isa=["Mesh"], errors=errs)
    assert [r.name for r in refs] == ["good"]
    assert any("mangled" in e for e in errs)


def test_zero_class_filter_is_reported_loudly_with_the_dta_name():
    """A C++ class name enumerates ZERO, which is indistinguishable from 'no
    such objects exist' — the exact failure that makes a diff look clean while
    it is blind."""
    errs: list[str] = []
    t = _Recorder(["#0;"])
    assert t.roster(PANEL, isa=["RndDrawable"], errors=errs) == []
    assert len(errs) == 1
    assert "ZERO" in errs[0] and "'Draw'" in errs[0]


def test_shipped_probes_only_use_dta_class_names():
    """Measured: RndDrawable -> 0 where Draw -> 45. Every shipped isa/classes
    entry must be in the DTA vocabulary or the probe is silently blind."""
    cmap = probe.load_class_map()
    assert cmap.get("classes"), "probes/dta_classes.json is missing or empty"
    for pid, p in probe.load_all().items():
        warn = probe.validate_classes(p.scope.isa + p.scope.classes, pid)
        assert warn == [], (pid, warn)


def test_exact_class_filter_keeping_nothing_is_reported():
    """`--classes RndDrawable` enumerates fine via isa and then matches
    nothing, because `classes` is an EXACT filter on the reported class."""
    p = probe.Probe(id="t", scope=probe.Scope(dir=PANEL, isa=["Trans"],
                                              classes=["RndDrawable"]),
                    fields=[probe.Field("x", "prop", ["x"], "float")])
    t = _Recorder(["#2;a|Mesh;b|Trans;"])
    recs, stats = probe.run_probe(t, p, Limits.portable())
    assert recs == {}
    assert any("Use the DTA name 'Draw'" in e for e in stats.errors)
    assert any("kept NONE" in e and "'Mesh'" in e for e in stats.errors)


def test_validate_classes_names_the_dta_replacement():
    w = probe.validate_classes(["RndGroup"], "t")
    assert len(w) == 1 and "'Group'" in w[0] and "ZERO" in w[0]
    assert probe.validate_classes(["Draw", "Trans", "Mesh"], "t") == []


def test_roster_class_must_be_a_bare_symbol():
    try:
        _Recorder([]).roster(PANEL, isa=['Mesh"} {print "pwn'])
    except DirSpecError:
        pass
    else:
        raise AssertionError("expected DirSpecError")


def test_isa_and_classes_are_not_conflated():
    """`isa` is a SUBCLASS gate (what iterate implements); `classes` is an
    EXACT post-filter. Passing one as the other silently empties the roster."""
    sc = probe.Scope(isa=["Trans"])
    assert sc.roster_classes() == ["Trans"]
    # An exact-filtering target must ignore isa and keep everything.
    r = ReplayTarget({}, roster_data=[{"name": "m", "type": "Mesh"}])
    assert [x.name for x in r.roster("main", None, isa=["Trans"])] == ["m"]


def test_empty_panel_scope_is_an_explicit_error_not_silent_zero():
    """Zero objects would read downstream as 'everything is missing on this
    side' — the worst report this differ can produce."""
    p = probe.Probe(id="t", scope=probe.Scope(dir=PANEL, isa=["Light"]),
                    fields=[probe.Field("x", "prop", ["x"], "float")])
    t = _Recorder(["#0;"])
    recs, stats = probe.run_probe(t, p, Limits.portable())
    assert recs == {}
    assert any("enumerated ZERO" in e for e in stats.errors)


def test_scope_dir_mismatch_is_a_blocker():
    a = Snapshot(probe="p", target="native", meta={"scope_dir": "main"})
    b = Snapshot(probe="p", target="console", meta={"scope_dir": PANEL})
    f = diff.diff_snapshots(a, b)
    assert f[0].severity == diff.BLOCKER and f[0].field == "scope_dir"


# --------------------------------------------------------------------------
# sweep (GAP 2)
# --------------------------------------------------------------------------

def _have_imaging() -> bool:
    try:
        import numpy  # noqa: F401
        from PIL import Image  # noqa: F401
    except ImportError:
        return False
    return True


def _png(rect) -> bytes:
    """A 200x100 black frame with one white rectangle (x0, x1)."""
    import io

    from PIL import Image
    im = Image.new("RGB", (200, 100), (0, 0, 0))
    if rect:
        x0, x1 = rect
        for x in range(x0, x1):
            for y in range(40, 60):
                im.putpixel((x, y), (255, 255, 255))
    buf = io.BytesIO()
    im.save(buf, "PNG")
    return buf.getvalue()


class _FakeNative(NativeHttpTarget):
    """Native target with canned frames; records every property write."""

    def __init__(self, frames, roster_names=("motd.lbl",), fail_shot_at=None):
        super().__init__(base="http://127.0.0.1:1")
        self.frames = list(frames)
        self.writes: list[str] = []
        self.value = "96.038147"
        self._names = list(roster_names)
        self.shots = 0
        self.fail_shot_at = fail_shot_at

    def health(self):
        return True

    def roster(self, dir_name="main", classes=None, **kw):
        return [ObjRef(n, "HamLabel", dir_name) for n in self._names]

    def eval_dta(self, expr, timeout=15.0):
        import re as _re
        m = _re.search(r"\{\$o set \([^)]*\) ([^}]*)\}", expr)
        if m:
            self.writes.append(m.group(1))
            self.value = m.group(1)
        return EvalResult(ok=True, type="string", value=self.value)

    def screenshot(self, timeout=60.0):
        self.shots += 1
        if self.fail_shot_at == self.shots:
            from state_diff.transport import TransportError
            raise TransportError("simulated screenshot failure")
        return self.frames[min(self.shots - 1, len(self.frames) - 1)]


def test_sweep_parse_prop_rejects_injection():
    assert sweep.parse_prop("local_xfm x") == ["local_xfm", "x"]
    assert sweep.parse_prop("local_xfm.x") == ["local_xfm", "x"]
    for bad in ["x) 0} {print (y", "a b\"c", ""]:
        try:
            sweep.parse_prop(bad)
        except sweep.SweepError:
            pass
        else:
            raise AssertionError(f"expected SweepError for {bad!r}")


def test_sweep_region_is_clipped_and_validated():
    assert sweep.parse_region(None, 200, 100) == (0, 0, 200, 100)
    assert sweep.parse_region("-5,10,400,50", 200, 100) == (0, 10, 200, 50)
    try:
        sweep.parse_region("10,10,10,50", 200, 100)
    except sweep.SweepError:
        pass
    else:
        raise AssertionError("expected SweepError for an empty region")


def test_sweep_values_from_range():
    class A:
        values = None
        range = (0.0, 10.0)
        steps = 3
    assert sweep.parse_values(A()) == [0.0, 5.0, 10.0]


def test_sweep_measurement_bbox_is_in_absolute_coordinates():
    if not _have_imaging():
        return
    img = sweep.decode_png(_png((50, 80)))
    m = sweep.measure(img, (20, 30, 200, 100), bg_threshold=24)
    assert m["fg"] == {"px": 600, "x0": 50, "x1": 79, "y0": 40, "y1": 59,
                       "cx": 64.5, "cy": 49.5}


def test_sweep_delta_measures_change_against_the_baseline():
    if not _have_imaging():
        return
    base = sweep.decode_png(_png((50, 80)))
    now = sweep.decode_png(_png((50, 120)))
    m = sweep.measure(now, (0, 0, 200, 100), baseline=base)
    # Only the newly-lit columns differ; the shared prefix cancels.
    assert m["delta"]["x0"] == 80 and m["delta"]["x1"] == 119


def test_sweep_analysis_finds_a_pinned_edge_and_a_monotonic_one():
    """The field-test finding, reduced: left edge pinned, right edge tracks."""
    series = [{"measure": {"fg": {"x0": 53, "x1": 100 + 50 * i,
                                  "y0": 10, "y1": 20, "cx": 0, "cy": 0,
                                  "px": 5}}}
              for i in range(5)]
    a = sweep.analyse(series, "fg")
    assert a["x0"]["pinned"] and a["x0"]["spread"] == 0
    assert a["x1"]["monotonic"] and a["x1"]["direction"] == "up"
    assert a["x1"]["spread"] == 200
    # noise is unmeasured with one capture per value: null, NOT zero.
    assert a["x1"]["noise"] is None and a["x1"]["above_noise"] is None


def test_sweep_repeats_measure_the_noise_floor():
    """An animating object produces same-value spread; a movement smaller than
    that spread is not a movement."""
    red = sweep.reduce_repeats([{"x1": 100, "px": 1}, {"x1": 140, "px": 3},
                                {"x1": 120, "px": 2}])
    assert red["x1"] == 120 and red["x1_spread"] == 40 and red["n"] == 3
    series = [{"measure": {"fg": {"x1": 120, "x1_spread": 40}}},
              {"measure": {"fg": {"x1": 150, "x1_spread": 12}}}]
    assert sweep.noise_floor(series, "fg")["x1"] == 40
    a = sweep.analyse(series, "fg")
    assert a["x1"]["spread"] == 30 and a["x1"]["above_noise"] is False


def test_sweep_flags_a_saturated_foreground_threshold():
    region = [0, 0, 200, 100]
    sat = {k: {"values": v} for k, v in
           {"x0": [0, 0], "x1": [199, 199], "y0": [0, 0], "y1": [99, 99]}.items()}
    assert sweep.flag_saturated(sat, region)
    ok = {k: {"values": v} for k, v in
          {"x0": [5, 9], "x1": [80, 90], "y0": [0, 0], "y1": [99, 99]}.items()}
    assert sweep.flag_saturated(ok, region) is None


def test_sweep_runs_and_restores_the_original_value():
    if not _have_imaging():
        return
    frames = [_png((50, 60 + 20 * i)) for i in range(6)]
    t = _FakeNative(frames)
    res = sweep.run_sweep(t, PANEL, "motd.lbl", ["local_xfm", "x"],
                          [0.0, 100.0, 200.0], settle_frames=0, repeat=1)
    assert [s["literal"] for s in res.series] == ["0", "100", "200"]
    assert res.restored is True
    assert t.writes[-1] == "96.038147"      # restored last
    assert res.object_class == "HamLabel"
    assert any("noise floor was NOT measured" in w for w in res.warnings)


def test_sweep_restores_even_when_the_capture_blows_up():
    """A sweep that dies holding a swept value silently poisons every capture
    taken afterwards, so the restore is a `finally`, not a happy path."""
    if not _have_imaging():
        return
    t = _FakeNative([_png((50, 60))], fail_shot_at=1)
    try:
        sweep.run_sweep(t, PANEL, "motd.lbl", ["local_xfm", "x"], [1.0],
                        settle_frames=0)
    except Exception:  # noqa: BLE001 - the baseline capture is expected to fail
        pass
    assert t.writes and t.writes[-1] == "96.038147"


def test_sweep_refuses_an_object_that_was_never_enumerated():
    """Same two-pass rule as the probes: naming a nonexistent object faults
    the title, and a sweep would then WRITE to it."""
    if not _have_imaging():
        return
    t = _FakeNative([_png((50, 60))], roster_names=("other.lbl",))
    try:
        sweep.run_sweep(t, PANEL, "ghost.lbl", ["local_xfm", "x"], [1.0],
                        settle_frames=0)
    except probe.UnenumeratedName as e:
        assert "two-pass" in str(e)
    else:
        raise AssertionError("expected UnenumeratedName")
    assert t.writes == []          # nothing was written to a guessed name


def test_sweep_scripts_are_budget_valid():
    prefix = sweep._obj_prefix(dir_expr(PANEL), "motd.lbl")
    prog = prefix + '{$o set (local_xfm x) 1.5}{sprintf "%.9g" 1}}'
    budget.validate_script(prog, Limits.portable(), "sweep")


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
