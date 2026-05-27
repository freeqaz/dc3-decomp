"""Variant outcome predictor (roadmap B4).

Ranks candidate variants by estimated win-probability so the build queue can
cull the bottom fraction when it's over a tight budget — compile-run is ~56%
of the inner loop (see bench/BASELINE.md), so not compiling low-value variants
is the throughput lever.

Design — deliberately dependency-light (no sklearn/torch):

  Model = empirical-Bayes win-rate over the per-variant features recorded in
  climb_history.climb_variant. For each (pattern_label) and each
  (pattern_label, diag_fingerprint) cell we estimate P(win) with a Beta(α, β)
  prior, then blend:

      score = w_pd * p(pattern, diag) + w_p * p(pattern) + w_g * p(global)

  Numeric features (func_loc, func_stmts, beam_depth) are folded in as a small
  multiplicative nudge: variants on larger functions get a mild boost because
  big functions have more codegen degrees of freedom (more variants pay off),
  while tiny functions plateau fast. The nudge is intentionally weak so it
  can't override the per-pattern signal.

WHY empirical-Bayes and not logistic regression: the history is *thin* (the
roadmap's explicit blocker). A Beta-prior win-rate degrades gracefully — with
zero data every variant scores the global prior, so ranking is a no-op tie and
nothing gets wrongly culled. A fitted logistic model on a handful of rows would
overfit and confidently mis-rank. The prior strength (β) is the only knob and
it directly controls "how much data before we trust a cell".

The model is consumed by ``generator.generate_variants`` only when the
``PERMUTER_PREDICTOR`` env flag is enabled (default OFF) AND the queue exceeds
``budget`` — see ``rank_and_cull``.
"""

from __future__ import annotations

import os
from dataclasses import dataclass, field
from pathlib import Path

# Beta prior. Matches the optimistic prior convention in generator.py
# (_BAYESIAN_ALPHA / _BAYESIAN_BETA): ~9% baseline win rate, β=10 means a cell
# needs roughly its own scale of evidence before it moves off the prior.
_PRIOR_ALPHA = 1.0
_PRIOR_BETA = 10.0
_GLOBAL_P = _PRIOR_ALPHA / (_PRIOR_ALPHA + _PRIOR_BETA)  # ~0.091

# Blend weights for the three win-rate estimates (sum to 1.0). The joint
# (pattern, diagnosis) cell is most specific so it dominates when it has data;
# the global term is a floor so a brand-new pattern still scores sanely.
_W_PATTERN_DIAG = 0.5
_W_PATTERN = 0.35
_W_GLOBAL = 0.15

# Numeric-nudge envelope: the size factor multiplies the blended score by at
# most ±this fraction, so it only ever breaks ties between similar patterns.
_SIZE_NUDGE = 0.10

# Env flag gating the whole feature in generate_variants. Default OFF: history
# is thin, so a wrong cull would lose real winners.
_ENV_FLAG = "PERMUTER_PREDICTOR"

# Fraction of the over-budget queue to cull (the "bottom fraction"). Overridable
# via PERMUTER_PREDICTOR_CULL for A/B sweeps.
_DEFAULT_CULL_FRACTION = 0.5


def predictor_enabled() -> bool:
    """True when PERMUTER_PREDICTOR is set to a truthy value."""
    return os.environ.get(_ENV_FLAG, "").strip().lower() in ("1", "true", "on", "yes")


def cull_fraction() -> float:
    """Bottom fraction of the over-budget queue to drop (default 0.5)."""
    raw = os.environ.get("PERMUTER_PREDICTOR_CULL", "")
    try:
        frac = float(raw)
    except (TypeError, ValueError):
        return _DEFAULT_CULL_FRACTION
    return min(0.95, max(0.0, frac))


@dataclass
class _Cell:
    """Win/total counts for one categorical cell."""

    wins: int = 0
    total: int = 0

    def p(self) -> float:
        """Beta-posterior mean win-rate for this cell."""
        return (self.wins + _PRIOR_ALPHA) / (self.total + _PRIOR_ALPHA + _PRIOR_BETA)


@dataclass
class VariantFeatures:
    """The feature vector the predictor scores.

    Mirrors the columns recorded in climb_history.climb_variant so a candidate
    variant at generation time can be scored against historical outcomes.
    """

    pattern_label: str
    diag_fingerprint: str | None = None
    func_loc: int | None = None
    func_stmts: int | None = None
    beam_depth: int | None = None


@dataclass
class WinPredictor:
    """Empirical-Bayes win-rate model over recorded variant outcomes."""

    pattern_cells: dict[str, _Cell] = field(default_factory=dict)
    pattern_diag_cells: dict[tuple[str, str], _Cell] = field(default_factory=dict)
    global_cell: _Cell = field(default_factory=_Cell)
    # Median function LOC across training data, for the size nudge midpoint.
    median_loc: float | None = None

    # ---- training -----------------------------------------------------

    @classmethod
    def train(cls, rows: list[dict]) -> "WinPredictor":
        """Build a model from climb_history.load_variant_training_data() rows.

        Each row is a dict with keys: pattern_label, diag_fingerprint,
        func_loc, func_stmts, beam_depth, delta, won. Robust to an empty list
        (returns a model that scores everything at the global prior).
        """
        model = cls()
        locs: list[int] = []
        for row in rows:
            won = bool(row.get("won"))
            label = row.get("pattern_label") or "?"
            diag = row.get("diag_fingerprint")

            model.global_cell.total += 1
            model.global_cell.wins += 1 if won else 0

            pc = model.pattern_cells.setdefault(label, _Cell())
            pc.total += 1
            pc.wins += 1 if won else 0

            if diag is not None:
                key = (label, diag)
                dc = model.pattern_diag_cells.setdefault(key, _Cell())
                dc.total += 1
                dc.wins += 1 if won else 0

            loc = row.get("func_loc")
            if isinstance(loc, (int, float)) and loc > 0:
                locs.append(int(loc))

        if locs:
            locs.sort()
            model.median_loc = float(locs[len(locs) // 2])
        return model

    @classmethod
    def from_history(cls, db_path: Path | None = None) -> "WinPredictor":
        """Convenience: train directly from the climb_history DB."""
        from .climb_history import load_variant_training_data
        return cls.train(load_variant_training_data(db_path=db_path))

    # ---- scoring ------------------------------------------------------

    def _size_factor(self, func_loc: int | None) -> float:
        """Multiplicative nudge in [1-_SIZE_NUDGE, 1+_SIZE_NUDGE] from size.

        Larger-than-median functions nudge up, smaller nudge down. Neutral (1.0)
        when we have no size data or no training median.
        """
        if not func_loc or func_loc <= 0 or not self.median_loc:
            return 1.0
        # Map the log-ratio of size vs median through tanh into [-1, 1], then
        # scale by the nudge envelope. tanh keeps extreme sizes bounded.
        import math
        ratio = math.log(func_loc / self.median_loc)
        return 1.0 + _SIZE_NUDGE * math.tanh(ratio)

    def score(self, feats: VariantFeatures) -> float:
        """Estimate P(this variant improves the score), in roughly [0, 1].

        With no training data every variant returns the global prior, so a
        ranking pass is a stable no-op (all equal) and culling can't pick
        wrongly — exactly the safe behaviour we want while history is thin.
        """
        label = feats.pattern_label or "?"

        p_global = self.global_cell.p()
        pc = self.pattern_cells.get(label)
        p_pattern = pc.p() if pc else p_global

        p_pd = p_pattern
        if feats.diag_fingerprint is not None:
            dc = self.pattern_diag_cells.get((label, feats.diag_fingerprint))
            if dc is not None:
                p_pd = dc.p()

        blended = (
            _W_PATTERN_DIAG * p_pd
            + _W_PATTERN * p_pattern
            + _W_GLOBAL * p_global
        )
        return blended * self._size_factor(feats.func_loc)


def rank_and_cull(
    items: list,
    feature_of,
    budget: int,
    model: WinPredictor,
    cull_frac: float | None = None,
) -> list:
    """Rank ``items`` by predicted win-probability and cull the bottom fraction.

    Budget-gated: when ``len(items) <= budget`` the list is returned **unchanged
    and in original order** (no reordering, no culling) — the predictor only
    intervenes when the queue is genuinely over budget. This keeps the common
    case a pure no-op.

    When over budget, items are scored, sorted high→low, and the bottom
    ``cull_frac`` is dropped — but never below ``budget`` items (we always keep
    at least the budget). Sorting is stable on the original index so equal
    scores preserve input order (important: with thin history all scores tie,
    so this degrades to "keep the first ``budget`` items").

    ``feature_of(item) -> VariantFeatures`` extracts features from each item.
    """
    n = len(items)
    if n <= budget:
        return items

    frac = cull_frac if cull_frac is not None else cull_fraction()
    # How many to keep: drop the bottom `frac` of the queue, but never go below
    # the budget (budget is the floor — we still want a full budget of work).
    keep = max(budget, int(round(n * (1.0 - frac))))
    keep = min(keep, n)

    scored = [
        (model.score(feature_of(item)), idx, item)
        for idx, item in enumerate(items)
    ]
    # Sort by score desc, then original index asc (stable tie-break).
    scored.sort(key=lambda t: (-t[0], t[1]))
    return [item for _score, _idx, item in scored[:keep]]
