"""Pattern base class with auto-registration."""

from __future__ import annotations

from abc import ABC, abstractmethod
from typing import Iterator

from ..types import Diagnosis, FunctionContext, Variant

_REGISTRY: dict[str, Pattern] = {}


class Pattern(ABC):
    """Base class for source transformation patterns.

    Subclasses are auto-registered by their `name` attribute via __init_subclass__.
    Set ``opt_in = True`` on patterns that should only run when explicitly requested
    via ``--patterns``, not in default batch sweeps.
    """

    name: str = ""
    opt_in: bool = False
    safety_tier: str = "normal"
    structural_domain: str = "general"
    follow_ups: tuple[str, ...] = ()
    requires_context: tuple[str, ...] = ()

    def __init_subclass__(cls, **kwargs):
        super().__init_subclass__(**kwargs)
        if cls.name:
            _REGISTRY[cls.name] = cls()

    @abstractmethod
    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        """Yield source variants by applying this pattern to the function."""

    def relevant(self, diagnosis: Diagnosis) -> bool:
        """Return False to skip this pattern based on diagnosis.

        Default: always relevant. Override in subclasses to filter based
        on mismatch types present in the diagnosis.
        """
        return True

    def priority(self, diagnosis: Diagnosis) -> float:
        """Return 0.0-1.0 priority score for budget allocation.

        Higher priority = more variants allocated. 0.0 = skip entirely.
        Default: 1.0 if relevant(), 0.0 if not.

        Override in subclasses to provide diagnosis-specific scoring.
        Priorities let the budget allocator concentrate variants on
        patterns most likely to help for the specific mismatch profile.
        """
        return 1.0 if self.relevant(diagnosis) else 0.0

    def metadata(self) -> dict[str, object]:
        """Return lightweight declarative metadata for orchestration/tests."""
        return {
            "name": self.name,
            "opt_in": self.opt_in,
            "safety_tier": self.safety_tier,
            "structural_domain": self.structural_domain,
            "follow_ups": list(self.follow_ups),
            "requires_context": list(self.requires_context),
        }


def get_pattern(name: str) -> Pattern:
    """Get a registered pattern by name."""
    if name not in _REGISTRY:
        raise KeyError(
            f"Unknown pattern '{name}'. Available: {', '.join(sorted(_REGISTRY))}"
        )
    return _REGISTRY[name]


def get_all_patterns(include_opt_in: bool = False) -> list[Pattern]:
    """Return all registered patterns.

    By default, excludes opt-in patterns (those with ``opt_in = True``).
    Pass ``include_opt_in=True`` to include them.
    """
    if include_opt_in:
        return list(_REGISTRY.values())
    return [p for p in _REGISTRY.values() if not p.opt_in]


def list_patterns(include_opt_in: bool = False) -> list[str]:
    """Return names of all registered patterns.

    By default, excludes opt-in patterns. Pass ``include_opt_in=True``
    to include them (e.g. for ``--patterns all`` or ``--patterns noinline_stub``).
    """
    if include_opt_in:
        return sorted(_REGISTRY.keys())
    return sorted(k for k, v in _REGISTRY.items() if not v.opt_in)


def get_pattern_metadata(include_opt_in: bool = False) -> dict[str, dict[str, object]]:
    """Return metadata for registered patterns."""
    patterns = get_all_patterns(include_opt_in=include_opt_in)
    return {pattern.name: pattern.metadata() for pattern in patterns}
