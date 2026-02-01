"""Pattern registry — importing this module registers all built-in patterns."""

# Import pattern modules to trigger __init_subclass__ registration
from . import variable_extraction  # noqa: F401
from . import signed_unsigned  # noqa: F401
from . import inline_assignment  # noqa: F401

from .base import get_all_patterns, get_pattern, list_patterns

__all__ = ["get_all_patterns", "get_pattern", "list_patterns"]
