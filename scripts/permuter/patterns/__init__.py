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
from . import bool_materialize  # noqa: F401
from . import max_to_conditional  # noqa: F401
from . import early_return_merge  # noqa: F401
from . import bool_return_expr  # noqa: F401
from . import fsel_template  # noqa: F401
from . import alloca_intrinsic  # noqa: F401
from . import sizeof_signed_cast  # noqa: F401
from . import initializer_literal  # noqa: F401
from . import single_return  # noqa: F401
from . import bit_test_bool  # noqa: F401
from . import byte_mask_extraction  # noqa: F401
from . import commutative_swap  # noqa: F401  # 0/143 wins — needs improvement
from . import empty_size_swap  # noqa: F401   # 0/38 wins — needs improvement
from . import ternary_swap  # noqa: F401      # 0/10 wins — needs improvement
from . import condition_arithmetic  # noqa: F401
from . import pragma_fp_contract  # noqa: F401
from . import hoist_sret  # noqa: F401
from . import noreturn_attr  # noqa: F401
from . import const_overload  # noqa: F401
from . import const_ref_swap  # noqa: F401  # Swap copy init <-> const ref binding for struct types
from . import member_ref_bind  # noqa: F401  # Binds member/param to local ref to fix callee-saved regswaps
from . import temp_elimination  # noqa: F401  # Inline single-use temps to fix commutative/regswap
from . import fabs_variant  # noqa: F401  # Swap fabs/fabsf/std::fabs for float width fixes
from . import milo_log_swap  # noqa: F401  # Swap MILO_WARN/NOTIFY/LOG/FAIL macros
from . import float_double_literal  # noqa: F401  # Swap 0.001 <-> 0.001f literal suffixes
from . import float_literal_pressure  # noqa: F401  # Swap inline float literals <-> static consts
from . import float_const_static  # noqa: F401  # Convert float literals <-> static const for GPR/FPR prologue fix
from . import objptr_bool_extract  # noqa: F401  # Extract ObjPtr to raw ptr before && chains (cmpwi->cmplwi)
from . import iterator_deref_style  # noqa: F401  # (*it).member <-> it->member
from . import assignment_reorder  # noqa: F401  # Reorder consecutive assignment statements
from . import statement_reorder  # noqa: F401  # Reorder independent statements within blocks
from . import milo_str_conv  # noqa: F401  # Add .Str() to Symbol args in MILO macros
from . import milo_call_merge  # noqa: F401  # Merge duplicate MILO calls via shared variable
from . import nor_prevention  # noqa: F401  # Widen narrow XOR inputs to avoid NOR peepholes
from . import prologue_pressure  # noqa: F401  # Manipulate callee-saved register count via pressure changes
from . import parameter_live_range  # noqa: F401  # Kill bs param live range after LOAD_REVS via d.stream
from . import reference_elimination  # noqa: F401  # Eliminate multi-use ref/ptr vars (inverse of member_ref_bind)
from . import return_call_merge  # noqa: F401  # Merge/split duplicate return-call branches
from . import subscript_ref_bind  # noqa: F401  # Bind repeated arr[i] to local ref (inverse of reference_elimination)
from . import switch_if_convert  # noqa: F401  # Convert switch <-> if/else-if chains
from . import tail_call_reorder  # noqa: F401  # Reorder trailing calls to expose tail-call codegen
from . import null_guard_elimination  # noqa: F401  # Remove redundant null checks (if (ptr) ptr->M() -> ptr->M())
from . import varargs_cast  # noqa: F401  # Add (char *) casts to MILO macro varargs
from . import bool_to_uchar  # noqa: F401  # Change bool locals to unsigned char
from . import type_width_change  # noqa: F401  # Generalized int type width/sign changes
from . import guard_to_nested  # noqa: F401  # Convert guard returns <-> nested if blocks
from . import noinline_stub  # noqa: F401  # Mark trivial same-TU callees as __declspec(noinline)
from . import math_return_cast  # noqa: F401  # Add/remove (float) cast on math function returns (frsp fix)
from . import deep_member_ref_bind  # noqa: F401  # Bind ptr->member chains to local refs (double-indirection)
from . import loop_condition_cache  # noqa: F401  # Cache/uncache member access in loop conditions
from . import color_copy_shape  # noqa: F401  # Switch channel-wise vs aggregate color copy forms
from . import native_guard_camera_wrap  # noqa: F401  # Normalize inline UI camera select/restore to helpers
from . import rb3_source_hint  # noqa: F401  # Targeted ternary/if-else swaps guided by RB3 reference source
from . import assert_line_fix  # noqa: F401  # Fix drifted MILO_ASSERT line numbers
from . import math_func_promotion  # noqa: F401  # Swap sqrt/sin/cos/etc <-> sqrtf/sinf/cosf/etc
from . import null_guard_insert  # noqa: F401  # Insert missing null guards (complement to elimination)
from . import missing_call  # noqa: F401  # Detect and uncomment missing function calls (opt-in diagnostic)
from . import cast_insertion  # noqa: F401  # Add/remove/swap casts guided by Ghidra decompilation
from . import iterator_index_compare  # noqa: F401  # Convert it1<it2 to (it1-begin)<(it2-begin)
from . import loop_condition_subtract  # noqa: F401  # Rewrite a>=b to a-b>=0 in loops (subf. vs cmpw)
from . import foreach_to_dowhile  # noqa: F401  # Convert FOREACH to do-while with pre-guard
from . import u8_to_unsigned_long  # noqa: F401  # Widen u8 intermediates to prevent rlwinm fusion
from . import value_address_caching  # noqa: F401  # Swap ref binding <-> value caching for register alloc
from . import scope_narrowing  # noqa: F401  # Move declarations into narrower scopes (if/else/loop/block)
from . import scope_widening  # noqa: F401  # Hoist declarations OUT of narrower scopes — for OFFSET_SWAP / slot inversion
from . import slot_pad  # noqa: F401  # Insert dummy local at function top to shift slot allocations
from . import redundant_guard_elimination  # noqa: F401  # Remove exhaustive else-if/if-or guards
from . import accessor_outline  # noqa: F401  # Outline inlined accessors via noinline wrappers
from . import handler_inline  # noqa: F401  # Named/temp Message vars and handler wrapper inlining
from . import variable_inline  # noqa: F401  # Inline single-assignment locals at use sites (inverse of variable_extraction)
from . import iter_address_of  # noqa: F401  # &*<expr> <-> <expr> for iterator/pointer call args
from . import helper_inline  # noqa: F401  # Reverse-inline a trivial header helper at its call site
from . import goto_skip_to_ifelse  # noqa: F401  # Eliminate forward-skip gotos with negated if
from . import goto_to_return  # noqa: F401  # Substitute goto with return statement at target label
from . import goto_to_continue  # noqa: F401  # Replace `goto L` with `continue` for end-of-loop labels
from . import loop_rotation_to_while  # noqa: F401  # Convert `goto check; do{...}while()` to `while(true){...; if break; ...}`
from . import nested_goto_skip_to_ifelse  # noqa: F401  # Merge nested-if conditions to skip past a goto-to-outer-scope label
from . import bare_label_loop_to_while  # noqa: F401  # Sibling-label variant of loop_rotation_to_while (no `do` keyword)
from . import common_tail_goto_to_duplicate  # noqa: F401  # Duplicate else-clause tail to eliminate a forward goto-into-else
from . import bitpack_or_reorder  # noqa: F401  # Sort `A|B|C` OR-chains by descending shift amount (high bits first)
from . import mutex_if_to_else_if  # noqa: F401  # Merge adjacent mutually-exclusive ifs into if/else-if (drop redundant reload)
from . import demorgan_guard  # noqa: F401  # if(A&&B&&C){body} <-> if(!A||!B||!C) return; body
from . import positive_branch_invert  # noqa: F401  # if(c)return F; mid; return T  <->  if(!c){mid;return T;} return F
from . import member_readback  # noqa: F401  # Replace arg bool-read with stored member (clrlwi. vs cmpwi)
from . import cache_repeated_call  # noqa: F401  # Hoist repeated identical call expr into local (v.end() 2x)
from . import symbol_str_compare  # noqa: F401  # Add .Str()/.mStr to Symbol operands in == / != (cmplw vs strcmp)
from . import abs_empty_else_negate  # noqa: F401  # if(x>0){}else{x=-x} -> x=Abs(x); kills mfcr/cror boolean materialization
from . import store_then_compound_add  # noqa: F401  # member = base + call() -> member = base; member += call();
from . import compound_or_widening_drop  # noqa: F401  # u16 |= int <-> u16 = u16 | int (drop clrlwi on narrow-type compound assign)

from .base import get_all_patterns, get_pattern, list_patterns

__all__ = ["get_all_patterns", "get_pattern", "list_patterns"]
