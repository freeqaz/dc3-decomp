"""Pattern registry — importing this module registers all built-in patterns."""

# Import pattern modules to trigger __init_subclass__ registration
from . import variable_extraction  # noqa: F401
from . import signed_unsigned  # noqa: F401
from . import inline_assignment  # noqa: F401
from . import comparison_equivalence  # noqa: F401
from . import argument_swap  # noqa: F401
from . import declaration_reorder  # noqa: F401
from . import declaration_movement  # noqa: F401
from . import comma_split  # noqa: F401
from . import branch_polarity  # noqa: F401
from . import comparison_flip  # noqa: F401
from . import fma_reorder  # noqa: F401
from . import commutative_swap  # noqa: F401  # 0/143 wins — needs improvement
from . import empty_size_swap  # noqa: F401   # 0/38 wins — needs improvement
from . import ternary_swap  # noqa: F401      # 0/10 wins — needs improvement

from .base import get_all_patterns, get_pattern, list_patterns

__all__ = ["get_all_patterns", "get_pattern", "list_patterns"]
