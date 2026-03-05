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
from . import negation_split  # noqa: F401
from . import and_split  # noqa: F401
from . import bool_cast  # noqa: F401
from . import bitwise_accumulator  # noqa: F401
from . import max_to_conditional  # noqa: F401
from . import early_return_merge  # noqa: F401
from . import bool_return_expr  # noqa: F401
from . import fsel_template  # noqa: F401
from . import alloca_intrinsic  # noqa: F401
from . import sizeof_signed_cast  # noqa: F401
from . import initializer_literal  # noqa: F401
from . import single_return  # noqa: F401
from . import bit_test_bool  # noqa: F401
from . import commutative_swap  # noqa: F401  # 0/143 wins — needs improvement
from . import empty_size_swap  # noqa: F401   # 0/38 wins — needs improvement
from . import ternary_swap  # noqa: F401      # 0/10 wins — needs improvement
from . import pragma_fp_contract  # noqa: F401
from . import hoist_sret  # noqa: F401
from . import noreturn_attr  # noqa: F401
from . import const_overload  # noqa: F401
from . import member_ref_bind  # noqa: F401  # Binds member/param to local ref to fix callee-saved regswaps
from . import temp_elimination  # noqa: F401  # Inline single-use temps to fix commutative/regswap
from . import fabs_variant  # noqa: F401  # Swap fabs/fabsf/std::fabs for float width fixes
from . import milo_log_swap  # noqa: F401  # Swap MILO_WARN/NOTIFY/LOG/FAIL macros
from . import float_double_literal  # noqa: F401  # Swap 0.001 <-> 0.001f literal suffixes
from . import objptr_bool_extract  # noqa: F401  # Extract ObjPtr to raw ptr before && chains (cmpwi->cmplwi)
from . import iterator_deref_style  # noqa: F401  # (*it).member <-> it->member
from . import assignment_reorder  # noqa: F401  # Reorder consecutive assignment statements
from . import milo_str_conv  # noqa: F401  # Add .Str() to Symbol args in MILO macros
from . import milo_call_merge  # noqa: F401  # Merge duplicate MILO calls via shared variable
from . import prologue_pressure  # noqa: F401  # Manipulate callee-saved register count via pressure changes
from . import parameter_live_range  # noqa: F401  # Kill bs param live range after LOAD_REVS via d.stream

from .base import get_all_patterns, get_pattern, list_patterns

__all__ = ["get_all_patterns", "get_pattern", "list_patterns"]
