"""Pattern base class with auto-registration."""

from __future__ import annotations

from abc import ABC, abstractmethod
from typing import Iterator

from ..types import Diagnosis, FunctionContext, Variant

_REGISTRY: dict[str, Pattern] = {}


class Pattern(ABC):
    """Base class for source transformation patterns.

    Subclasses are auto-registered by their `name` attribute via __init_subclass__.
    """

    name: str = ""

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


def get_pattern(name: str) -> Pattern:
    """Get a registered pattern by name."""
    if name not in _REGISTRY:
        raise KeyError(
            f"Unknown pattern '{name}'. Available: {', '.join(sorted(_REGISTRY))}"
        )
    return _REGISTRY[name]


def get_all_patterns() -> list[Pattern]:
    """Return all registered patterns."""
    return list(_REGISTRY.values())


def list_patterns() -> list[str]:
    """Return names of all registered patterns."""
    return sorted(_REGISTRY.keys())
