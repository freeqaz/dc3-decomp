"""Math function float/double promotion swapping.

Win rate: proven in 1 manual fix (RndAnimatable::OnAnimate, std::fabs -> fabsf).

Math functions like sqrt(), sin(), cos() etc. have float-suffix variants
(sqrtf, sinf, cosf) that generate single-precision instructions (fdivs, fmuls)
instead of double-precision (fdiv, fmul) + round-to-single (frsp).

Note: fabs/fabsf/std::fabs are handled by fabs_variant.py.
This pattern covers the remaining math functions.

Transformations:
    sqrt(x)   -> sqrtf(x)       (and reverse)
    sin(x)    -> sinf(x)
    cos(x)    -> cosf(x)
    exp(x)    -> expf(x)
    pow(x,y)  -> powf(x,y)
    log(x)    -> logf(x)
    ceil(x)   -> ceilf(x)
    floor(x)  -> floorf(x)
    atan2(x)  -> atan2f(x)
    asin(x)   -> asinf(x)
    acos(x)   -> acosf(x)
    tan(x)    -> tanf(x)

Detection signals:
    - fdiv vs fdivs, fmul vs fmuls, fadd vs fadds, fsub vs fsubs
    - frsp (round to single precision) differences
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Maps double function -> float function and vice versa
# Excludes fabs/fabsf/std::fabs (handled by fabs_variant.py)
_DOUBLE_TO_FLOAT: dict[bytes, bytes] = {
    b"sqrt": b"sqrtf",
    b"sin": b"sinf",
    b"cos": b"cosf",
    b"exp": b"expf",
    b"pow": b"powf",
    b"log": b"logf",
    b"log10": b"log10f",
    b"ceil": b"ceilf",
    b"floor": b"floorf",
    b"atan2": b"atan2f",
    b"asin": b"asinf",
    b"acos": b"acosf",
    b"tan": b"tanf",
    b"atan": b"atanf",
    b"hypot": b"hypotf",
}

# Build reverse map
_FLOAT_TO_DOUBLE: dict[bytes, bytes] = {v: k for k, v in _DOUBLE_TO_FLOAT.items()}

# Also handle std:: qualified versions
_STD_DOUBLE_TO_FLOAT: dict[bytes, bytes] = {
    b"std::" + k: v for k, v in _DOUBLE_TO_FLOAT.items()
}

# All function names we recognize
_ALL_NAMES: set[bytes] = (
    set(_DOUBLE_TO_FLOAT.keys())
    | set(_FLOAT_TO_DOUBLE.keys())
    | set(_STD_DOUBLE_TO_FLOAT.keys())
)

# FP single vs double opcode pairs
_SINGLE_OPCODES = {"fdivs", "fmuls", "fadds", "fsubs", "fmadds", "fmsubs", "fnmsubs", "fnmadds"}
_DOUBLE_OPCODES = {"fdiv", "fmul", "fadd", "fsub", "fmadd", "fmsub", "fnmsub", "fnmadd"}


class MathFuncPromotionPattern(Pattern):
    name = "math_func_promotion"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        for d in diagnosis.diff_ops:
            # Single vs double FP instruction mismatch
            if d.target_opcode in _SINGLE_OPCODES and d.base_opcode in _DOUBLE_OPCODES:
                return True
            if d.target_opcode in _DOUBLE_OPCODES and d.base_opcode in _SINGLE_OPCODES:
                return True
            # frsp (round to single) — strong signal of float/double mismatch
            if d.target_opcode == "frsp" or d.base_opcode == "frsp":
                return True
            # lfd vs lfs — float width mismatch
            if (d.target_opcode == "lfd" and d.base_opcode == "lfs") or \
               (d.target_opcode == "lfs" and d.base_opcode == "lfd"):
                return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # Higher priority with stronger signals
        for d in diagnosis.diff_ops:
            if (d.target_opcode in _SINGLE_OPCODES and d.base_opcode in _DOUBLE_OPCODES) or \
               (d.target_opcode in _DOUBLE_OPCODES and d.base_opcode in _SINGLE_OPCODES):
                return 0.7
            if d.target_opcode == "frsp" or d.base_opcode == "frsp":
                return 0.7
        return 0.3

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        body = ctx.body_node
        counter = 0

        # Find all math function calls
        call_sites = _find_math_calls(body, source)
        if not call_sites:
            return

        # Strategy 1: Individual swaps
        for call_node, func_node, current_name in call_sites:
            if counter >= 10:
                break

            replacement = _get_swap(current_name)
            if replacement is None:
                continue

            ed = SourceEditor(source)
            ed.replace_range(func_node.start_byte, func_node.end_byte, replacement)

            try:
                new_source = ed.apply()
            except ValueError:
                continue

            cur = current_name.decode("utf-8", errors="replace")
            rep = replacement.decode("utf-8", errors="replace")
            yield Variant(
                name=f"mathfunc_{counter}",
                pattern_name=self.name,
                description=f"Swap {cur}() -> {rep}()",
                source=new_source,
            )
            counter += 1

        # Strategy 2: Swap ALL math calls at once (same direction)
        if len(call_sites) >= 2 and counter < 15:
            # Try double->float direction
            ed = SourceEditor(source)
            swapped = 0
            for call_node, func_node, current_name in call_sites:
                repl = _DOUBLE_TO_FLOAT.get(current_name) or _STD_DOUBLE_TO_FLOAT.get(current_name)
                if repl:
                    ed.replace_range(func_node.start_byte, func_node.end_byte, repl)
                    swapped += 1
            if swapped > 0:
                try:
                    new_source = ed.apply()
                    yield Variant(
                        name=f"mathfunc_all_{counter}",
                        pattern_name=self.name,
                        description=f"Promote {swapped} math calls to float variants",
                        source=new_source,
                    )
                    counter += 1
                except ValueError:
                    pass

            # Try float->double direction
            ed = SourceEditor(source)
            swapped = 0
            for call_node, func_node, current_name in call_sites:
                repl = _FLOAT_TO_DOUBLE.get(current_name)
                if repl:
                    ed.replace_range(func_node.start_byte, func_node.end_byte, repl)
                    swapped += 1
            if swapped > 0:
                try:
                    new_source = ed.apply()
                    yield Variant(
                        name=f"mathfunc_all_{counter}",
                        pattern_name=self.name,
                        description=f"Demote {swapped} math calls to double variants",
                        source=new_source,
                    )
                    counter += 1
                except ValueError:
                    pass


def _get_swap(name: bytes) -> bytes | None:
    """Get the swap target for a math function name."""
    if name in _DOUBLE_TO_FLOAT:
        return _DOUBLE_TO_FLOAT[name]
    if name in _FLOAT_TO_DOUBLE:
        return _FLOAT_TO_DOUBLE[name]
    if name in _STD_DOUBLE_TO_FLOAT:
        return _STD_DOUBLE_TO_FLOAT[name]
    return None


def _find_math_calls(
    node: Node, source: bytes
) -> list[tuple[Node, Node, bytes]]:
    """Find call_expression nodes calling math functions.

    Returns [(call_node, func_node, func_name_bytes), ...]
    """
    results = []
    for n in walk(node):
        if n.type != "call_expression":
            continue

        func = n.child_by_field_name("function")
        if func is None:
            continue

        func_text = source[func.start_byte:func.end_byte]
        if func_text in _ALL_NAMES:
            results.append((n, func, func_text))

    return results
