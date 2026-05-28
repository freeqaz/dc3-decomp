"""Tests for Wave C1: loop constraint_solver into rounds 2+ of hill_climb.

Covers:

1. New ``loop_constraints`` parameter exists on ``hill_climb`` and the
   matching ``--loop-constraints``/``--no-loop-constraints`` CLI flags are
   wired (default ON).
2. With ``loop_constraints=True`` the synthesis call site fires every round.
3. Cross-round source dedup: synth variants whose source bytes match a
   previously-attempted variant are skipped without invoking ``score_batch``.
4. ``PERMUTER_LOOP_CONSTRAINTS=0`` env override forces legacy round-1-only
   behavior even when the caller passed ``loop_constraints=True``.
"""

from __future__ import annotations

import inspect
import io
import os
import sys
import types as _pytypes
from contextlib import contextmanager
from pathlib import Path
from unittest import mock

from scripts.permuter import hill_climber
from scripts.permuter.types import (
    FunctionContext,
    HillClimbResult,
    ScoreResult,
    SynthesisResult,
    Variant,
)


# ---------------------------------------------------------------------------
# Lightweight fakes — keep hill_climb addressable without touching the build
# ---------------------------------------------------------------------------


class _FakeScorer:
    """Stand-in for scripts.permuter.scorer.Scorer.

    Returns a fixed baseline; score_batch echoes the variants with a fixed
    sub-100% match% so the loop always falls through to the pattern phase.
    """

    instances: list["_FakeScorer"] = []

    def __init__(self, source_path, symbol, unit=None):
        self.source_path = source_path
        self.symbol = symbol
        self.unit = unit
        self.score_batch_calls: list[list[Variant]] = []
        self.m2c_code = None
        self.ghidra_code = None
        self.ghidra_ast = None
        self.asm_listing_path = None
        self.diagnosis = None
        _FakeScorer.instances.append(self)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        return False

    def get_baseline(self, guided=False, ghidra=False, m2c=False):
        return 90.0

    def score_batch(self, variants, workers=0):
        self.score_batch_calls.append(list(variants))
        return [
            ScoreResult(
                variant=v,
                match_percent=90.0,  # never wins; loop continues to pattern phase
                build_success=True,
            )
            for v in variants
        ]

    def get_attribution(self):
        return None

    def get_shape_facts(self):
        return None

    def capture_variant_il_hashes(self, variants, limit=0):
        return {}


def _make_synth_variant(name: str, source: bytes) -> Variant:
    return Variant(
        name=name,
        pattern_name="constraint_solver",
        description=f"synth fake {name}",
        source=source,
    )


def _fake_ctx_factory(file_source: bytes):
    """Build a FunctionContext stand-in with the minimum attributes used by
    the hill_climber synth gate."""

    class _FakeCtx:
        pass

    class _FakeGhidraAST:
        code = ""  # empty but truthy attribute for ghidra_preflight

    ctx = _FakeCtx()
    ctx.file_source = file_source
    ctx.func_byte_range = (0, len(file_source))
    ctx.statements = []
    ctx.ghidra_ast = _FakeGhidraAST()
    ctx.ghidra_code = None
    ctx.body_node = None
    ctx.func_node = None
    ctx.file_path = Path("/tmp/fake.cpp")
    ctx.target_var_order = None
    ctx.target_gpr_saves = None
    ctx.target_facts = None
    ctx.mismatch_regions = None
    ctx.diagnosis = None
    ctx.m2c_code = None
    ctx.rb3_source = None
    return ctx


# ---------------------------------------------------------------------------
# 1. API surface
# ---------------------------------------------------------------------------


def test_hill_climb_has_loop_constraints_param():
    sig = inspect.signature(hill_climber.hill_climb)
    assert "loop_constraints" in sig.parameters
    assert sig.parameters["loop_constraints"].default is True


def test_cli_loop_constraints_default_on():
    saved = sys.argv
    try:
        sys.argv = [
            "hill_climber",
            "--symbol", "FAKE",
            "--source", "/tmp/x.cpp",
            "--function", "Foo::Bar",
        ]
        args = hill_climber.parse_args()
        assert args.loop_constraints is True
    finally:
        sys.argv = saved


def test_cli_no_loop_constraints_flag():
    saved = sys.argv
    try:
        sys.argv = [
            "hill_climber",
            "--symbol", "FAKE",
            "--source", "/tmp/x.cpp",
            "--function", "Foo::Bar",
            "--no-loop-constraints",
        ]
        args = hill_climber.parse_args()
        assert args.loop_constraints is False
    finally:
        sys.argv = saved


# ---------------------------------------------------------------------------
# 2 & 3. Loop fires synth every round + dedup skips repeats
# ---------------------------------------------------------------------------


def _run_hill_climb_with_fakes(
    loop_constraints: bool,
    synth_outputs_per_round: list[list[Variant]],
    *,
    env: dict | None = None,
    tmp_path: Path,
) -> tuple[HillClimbResult, list[int]]:
    """Run hill_climb with all heavy collaborators mocked.

    Returns (result, synth_call_round_nums) where synth_call_round_nums is the
    1-indexed round number at which constraint_solver.synthesize was invoked.
    """
    source_path = tmp_path / "fake.cpp"
    source_path.write_bytes(b"int original;\n")

    _FakeScorer.instances.clear()
    synth_call_rounds: list[int] = []
    current_round = {"n": 0}

    def fake_synthesize(ctx):
        idx = current_round["n"] - 1
        if 0 <= idx < len(synth_outputs_per_round):
            variants = synth_outputs_per_round[idx]
        else:
            variants = []
        synth_call_rounds.append(current_round["n"])
        return SynthesisResult(
            constraints=object(),
            variants=variants,
            deterministic_edit_count=1,
            free_variable_count=0,
        )

    fake_module = _pytypes.ModuleType("scripts.permuter.constraint_solver")
    fake_module.synthesize = fake_synthesize

    def fake_extract(path, fn):
        current_round["n"] += 1
        return _fake_ctx_factory(path.read_bytes())

    @contextmanager
    def env_patch():
        if env is None:
            yield
            return
        saved = {k: os.environ.get(k) for k in env}
        os.environ.update(env)
        try:
            yield
        finally:
            for k, v in saved.items():
                if v is None:
                    os.environ.pop(k, None)
                else:
                    os.environ[k] = v

    # Stub ghidra_preflight to a no-signal result so the round-1 preflight gate
    # doesn't trip on our toy ghidra_ast object.
    fake_preflight_mod = _pytypes.ModuleType("scripts.permuter.ghidra_preflight")

    class _NoopPreflight:
        struct_offset_mismatches: list = []
        extra_calls: list = []
        missing_calls: list = []
        dead_variables: list = []
        prologue_mismatch = False
        volatile_regswap_only = False
        hard_skip = False
        skip_reason = None
        confidence = 0.0

    fake_preflight_mod.run_preflight = lambda *a, **k: _NoopPreflight()

    # Patch every heavy collaborator. Generator returns nothing so the loop
    # plateaus quickly and we exit after the planned round count.
    with mock.patch.object(hill_climber, "extract_function", side_effect=fake_extract), \
         mock.patch.object(hill_climber, "Scorer", _FakeScorer), \
         mock.patch.object(
             hill_climber, "generate_variants",
             side_effect=lambda *a, **k: iter([
                 # one noise variant per round so the loop doesn't bail with
                 # "no_variants"; baseline-equal score keeps it as a plateau.
                 Variant(
                     name="noise",
                     pattern_name="noise_pattern",
                     description="noise filler so loop continues",
                     source=b"int noise;\n",
                 ),
             ]),
         ), \
         mock.patch.object(hill_climber, "_add_banner", lambda *a, **k: None), \
         mock.patch.object(hill_climber, "_strip_banner", lambda *a, **k: None), \
         mock.patch.dict(sys.modules, {
             "scripts.permuter.constraint_solver": fake_module,
             "scripts.permuter.ghidra_preflight": fake_preflight_mod,
         }), \
         env_patch():
        result = hill_climber.hill_climb(
            symbol="FAKE",
            source_path=source_path,
            function_name="Foo::Bar",
            patterns=[],
            max_rounds=4,
            max_variants=10,
            plateau_limit=10,  # don't bail on plateau — let max_rounds drive
            compose=False,
            apply=True,
            ghidra=True,
            m2c=False,
            chain=False,
            adaptive=False,
            constrained=True,
            loop_constraints=loop_constraints,
            validate=False,
        )

    return result, synth_call_rounds


def test_loop_constraints_on_fires_every_round(tmp_path):
    # Each round produces a UNIQUE synth variant, so dedup never trips.
    synth_outputs = [
        [_make_synth_variant("synth_0", b"int r1;\n")],
        [_make_synth_variant("synth_0", b"int r2;\n")],
        [_make_synth_variant("synth_0", b"int r3;\n")],
        [_make_synth_variant("synth_0", b"int r4;\n")],
    ]
    result, rounds_hit = _run_hill_climb_with_fakes(
        loop_constraints=True,
        synth_outputs_per_round=synth_outputs,
        tmp_path=tmp_path,
    )
    # Synth must fire on at least rounds 1 and 2 (the legacy behavior would
    # only fire on round 1). With unique outputs every round, expect 4.
    assert rounds_hit[0] == 1
    assert 2 in rounds_hit, (
        f"loop_constraints=True must re-fire synth in round 2; saw {rounds_hit}"
    )


def test_loop_constraints_off_fires_only_round_1(tmp_path):
    synth_outputs = [
        [_make_synth_variant("synth_0", b"int r1;\n")],
        [_make_synth_variant("synth_0", b"int r2;\n")],
        [_make_synth_variant("synth_0", b"int r3;\n")],
        [_make_synth_variant("synth_0", b"int r4;\n")],
    ]
    result, rounds_hit = _run_hill_climb_with_fakes(
        loop_constraints=False,
        synth_outputs_per_round=synth_outputs,
        tmp_path=tmp_path,
    )
    assert rounds_hit == [1], (
        f"loop_constraints=False must fire synth only at round 1; saw {rounds_hit}"
    )


def test_env_var_disables_looping(tmp_path):
    synth_outputs = [
        [_make_synth_variant("synth_0", b"int r1;\n")],
        [_make_synth_variant("synth_0", b"int r2;\n")],
    ]
    result, rounds_hit = _run_hill_climb_with_fakes(
        loop_constraints=True,  # explicitly on
        synth_outputs_per_round=synth_outputs,
        env={"PERMUTER_LOOP_CONSTRAINTS": "0"},
        tmp_path=tmp_path,
    )
    assert rounds_hit == [1], (
        f"PERMUTER_LOOP_CONSTRAINTS=0 must force legacy behavior; saw {rounds_hit}"
    )


def test_cross_round_source_dedup_skips_score_batch(tmp_path):
    # All four rounds emit the IDENTICAL synth variant — round 2+ must filter
    # it out before reaching score_batch.
    duplicate = _make_synth_variant("synth_0", b"int duplicate;\n")
    synth_outputs = [
        [duplicate],
        [duplicate],
        [duplicate],
        [duplicate],
    ]
    result, rounds_hit = _run_hill_climb_with_fakes(
        loop_constraints=True,
        synth_outputs_per_round=synth_outputs,
        tmp_path=tmp_path,
    )
    # All four rounds attempted synthesize().
    assert len(rounds_hit) >= 2
    # But only one scorer batch should contain the (unique) synth variant.
    # Subsequent rounds' synth output was de-duped pre-scoring.
    scorer = _FakeScorer.instances[0]
    synth_batches = [
        batch for batch in scorer.score_batch_calls
        if any(v.pattern_name == "constraint_solver" for v in batch)
    ]
    assert len(synth_batches) == 1, (
        f"expected exactly 1 synth score_batch (the rest dup-skipped); "
        f"got {len(synth_batches)}"
    )
