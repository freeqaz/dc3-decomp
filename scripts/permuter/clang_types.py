"""Lightweight libclang type resolver for the permuter.

Uses the native CMake build's compile_commands.json to parse TUs and
resolve types at specific byte offsets. Strips -DHX_NATIVE=1 so we
analyze PPC code paths (where the decomp bugs live).

Graceful degradation: if clang.cindex is unavailable, is_available()
returns False and all resolve_* functions return None.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum, auto
from pathlib import Path
from typing import Optional

# Lazy singletons
_IDX = None
_COMPDB = None
_RESOURCE_DIR: Optional[str] = None
_TU_CACHE: dict[str, object] = {}  # filepath -> TranslationUnit
_ARGS_CACHE: dict[str, list[str]] = {}  # filepath -> filtered args
_INITIALIZED = False
_AVAILABLE: Optional[bool] = None

# Project root (two levels up from scripts/permuter/)
_PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent


class TypeKind(Enum):
    VOID = auto()
    BOOL = auto()
    SIGNED_INT = auto()
    UNSIGNED_INT = auto()
    FLOAT = auto()
    POINTER = auto()
    RECORD = auto()  # struct/class/union
    ENUM = auto()
    OTHER = auto()


@dataclass
class TypeInfo:
    kind: TypeKind
    spelling: str
    is_pointer: bool
    is_signed_int: bool
    is_unsigned_int: bool
    is_float: bool
    size: int = 0  # sizeof in bytes, 0 if unknown


def is_available() -> bool:
    """Check if libclang is available for type resolution."""
    global _AVAILABLE
    if _AVAILABLE is not None:
        return _AVAILABLE
    try:
        import clang.cindex  # noqa: F401
        _AVAILABLE = True
    except ImportError:
        _AVAILABLE = False
    return _AVAILABLE


def resolve_type_at(
    filepath: Path, byte_offset: int, source: bytes
) -> TypeInfo | None:
    """Resolve the type of the expression at a given byte offset.

    Args:
        filepath: Path to the source file.
        byte_offset: tree-sitter byte offset into source.
        source: Full file source bytes (for line/col calculation).

    Returns TypeInfo or None if resolution fails.
    """
    tu = _get_tu(filepath, source)
    if tu is None:
        return None
    line, col = _byte_offset_to_linecol(source, byte_offset)
    cursor = _cursor_at(tu, filepath, line, col)
    if cursor is None:
        return None
    if cursor.type is None or cursor.type.kind is None:
        return None
    return _make_type_info(cursor.type.get_canonical())


def resolve_call_return_type(
    filepath: Path, byte_offset: int, source: bytes
) -> TypeInfo | None:
    """Resolve the return type of a call expression at byte_offset.

    The byte_offset should point to the start of the call (the function
    name or object in `obj.method()`). Returns the return type of the
    called function.
    """
    tu = _get_tu(filepath, source)
    if tu is None:
        return None

    from clang.cindex import CursorKind

    line, col = _byte_offset_to_linecol(source, byte_offset)
    cursor = _cursor_at(tu, filepath, line, col)
    if cursor is None:
        return None

    # CALL_EXPR cursor's type is the return type
    if cursor.kind == CursorKind.CALL_EXPR:
        if cursor.type is not None:
            return _make_type_info(cursor.type.get_canonical())

    # Try the referenced declaration's result type
    ref = cursor.referenced
    if ref is not None:
        if ref.kind in (CursorKind.FUNCTION_DECL, CursorKind.CXX_METHOD):
            result_type = ref.result_type
            if result_type is not None:
                return _make_type_info(result_type.get_canonical())

    # Fallback: expression type at cursor
    if cursor.type is not None:
        return _make_type_info(cursor.type.get_canonical())

    return None


def resolve_decl_type(
    filepath: Path, byte_offset: int, source: bytes
) -> TypeInfo | None:
    """Resolve the type of a variable declaration at byte_offset.

    The byte_offset should point to the declarator (variable name) or
    the start of the declaration statement.
    """
    tu = _get_tu(filepath, source)
    if tu is None:
        return None

    from clang.cindex import CursorKind

    line, col = _byte_offset_to_linecol(source, byte_offset)
    cursor = _cursor_at(tu, filepath, line, col)
    if cursor is None:
        return None

    # VAR_DECL cursor has the full declared type
    if cursor.kind == CursorKind.VAR_DECL:
        return _make_type_info(cursor.type.get_canonical())

    # DECL_REF_EXPR — reference to a variable
    if cursor.kind == CursorKind.DECL_REF_EXPR:
        ref = cursor.referenced
        if ref is not None and ref.kind == CursorKind.VAR_DECL:
            return _make_type_info(ref.type.get_canonical())

    # Fallback: cursor type
    if cursor.type is not None:
        return _make_type_info(cursor.type.get_canonical())

    return None


# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------

def _init() -> bool:
    """Initialize libclang singletons. Returns True if successful."""
    global _IDX, _COMPDB, _RESOURCE_DIR, _INITIALIZED
    if _INITIALIZED:
        return _IDX is not None
    _INITIALIZED = True

    if not is_available():
        return False

    try:
        from clang.cindex import CompilationDatabase, Index

        _IDX = Index.create()
        compdb_dir = _PROJECT_ROOT / "native" / "build"
        if not (compdb_dir / "compile_commands.json").exists():
            return False
        _COMPDB = CompilationDatabase.fromDirectory(str(compdb_dir))
        _RESOURCE_DIR = _get_clang_resource_dir()
        return True
    except Exception:
        return False


def _get_clang_resource_dir() -> str | None:
    """Get the system clang resource dir for header resolution."""
    import subprocess

    for cmd in ["clang++", "clang"]:
        try:
            return (
                subprocess.check_output(
                    [cmd, "-print-resource-dir"], stderr=subprocess.DEVNULL
                )
                .decode()
                .strip()
            )
        except (FileNotFoundError, subprocess.CalledProcessError):
            continue
    return None


def _filter_compile_args(raw_args: list[str], filepath: str) -> list[str]:
    """Filter compile_commands args for libclang parsing."""
    filtered = []
    skip_next = False
    for a in raw_args[1:]:  # skip compiler path
        if skip_next:
            skip_next = False
            continue
        if a in ("-o", "-c"):
            skip_next = True
            continue
        if a == filepath:
            continue
        if a.startswith("--driver-mode"):
            continue
        filtered.append(a)
    filtered.append("-w")
    # Remove HX_NATIVE — we want to analyze PPC code path
    filtered = [a for a in filtered if a != "-DHX_NATIVE=1"]
    return filtered


def _get_args(filepath: Path) -> list[str] | None:
    """Get filtered compile arguments for a file."""
    key = str(filepath.resolve())
    if key in _ARGS_CACHE:
        return _ARGS_CACHE[key]

    if not _init():
        return None

    cmds = _COMPDB.getCompileCommands(str(filepath))
    if not cmds:
        # Try with resolved path
        cmds = _COMPDB.getCompileCommands(key)
    if not cmds:
        return None

    args = _filter_compile_args(list(cmds[0].arguments), str(filepath))
    if _RESOURCE_DIR:
        args.extend(["-resource-dir", _RESOURCE_DIR])

    _ARGS_CACHE[key] = args
    return args


def _get_tu(filepath: Path, source: bytes | None = None):
    """Get or create a cached TranslationUnit for a file."""
    key = str(filepath.resolve())
    if key in _TU_CACHE:
        return _TU_CACHE[key]

    args = _get_args(filepath)
    if args is None:
        return None

    try:
        unsaved = None
        if source is not None:
            # Provide source as unsaved file so libclang uses in-memory content
            unsaved = [(str(filepath), source)]

        tu = _IDX.parse(str(filepath), args=args, unsaved_files=unsaved)
        _TU_CACHE[key] = tu
        return tu
    except Exception:
        _TU_CACHE[key] = None
        return None


def _byte_offset_to_linecol(source: bytes, offset: int) -> tuple[int, int]:
    """Convert a byte offset to 1-based (line, column) for libclang."""
    line = source[:offset].count(b"\n") + 1
    last_nl = source.rfind(b"\n", 0, offset)
    if last_nl == -1:
        col = offset + 1  # no newline before offset, column is offset+1
    else:
        col = offset - last_nl  # distance from last newline (1-based)
    return line, col


def _cursor_at(tu, filepath: Path, line: int, col: int):
    """Get the deepest cursor at a specific source location."""
    from clang.cindex import Cursor, SourceLocation

    f = tu.get_file(str(filepath))
    if f is None:
        return None
    loc = SourceLocation.from_position(tu, f, line, col)
    cursor = Cursor.from_location(tu, loc)
    if cursor is None:
        return None
    # Check that the cursor is actually at our location (not a fallback)
    if cursor.location.file is None:
        return None
    return cursor


def _make_type_info(canonical_type) -> TypeInfo:
    """Create a TypeInfo from a libclang canonical type."""
    from clang.cindex import TypeKind as CTypeKind

    kind = canonical_type.kind
    spelling = canonical_type.spelling

    is_pointer = kind == CTypeKind.POINTER
    is_signed_int = kind in (
        CTypeKind.CHAR_S,
        CTypeKind.SCHAR,
        CTypeKind.SHORT,
        CTypeKind.INT,
        CTypeKind.LONG,
        CTypeKind.LONGLONG,
        CTypeKind.INT128,
    )
    is_unsigned_int = kind in (
        CTypeKind.UCHAR,
        CTypeKind.CHAR_U,
        CTypeKind.USHORT,
        CTypeKind.UINT,
        CTypeKind.ULONG,
        CTypeKind.ULONGLONG,
        CTypeKind.UINT128,
    )
    is_float = kind in (
        CTypeKind.FLOAT,
        CTypeKind.DOUBLE,
        CTypeKind.LONGDOUBLE,
    )

    # Map to our TypeKind enum
    if kind == CTypeKind.BOOL:
        tk = TypeKind.BOOL
    elif is_pointer:
        tk = TypeKind.POINTER
    elif is_signed_int:
        tk = TypeKind.SIGNED_INT
    elif is_unsigned_int:
        tk = TypeKind.UNSIGNED_INT
    elif is_float:
        tk = TypeKind.FLOAT
    elif kind == CTypeKind.VOID:
        tk = TypeKind.VOID
    elif kind == CTypeKind.RECORD:
        tk = TypeKind.RECORD
    elif kind == CTypeKind.ENUM:
        tk = TypeKind.ENUM
    else:
        tk = TypeKind.OTHER

    # Guard against SIGILL: clang_Type_getSizeOf crashes on UNEXPOSED/INVALID
    # types with a signal (not catchable by Python). Only call get_size() on
    # types we know are safe.
    size = 0
    safe_for_size = kind not in (
        CTypeKind.UNEXPOSED, CTypeKind.INVALID, CTypeKind.DEPENDENT,
        CTypeKind.FUNCTIONPROTO, CTypeKind.FUNCTIONNOPROTO,
        CTypeKind.INCOMPLETEARRAY, CTypeKind.MEMBERPOINTER,
    )
    if safe_for_size:
        try:
            size = canonical_type.get_size()
        except Exception:
            size = 0

    return TypeInfo(
        kind=tk,
        spelling=spelling,
        is_pointer=is_pointer,
        is_signed_int=is_signed_int,
        is_unsigned_int=is_unsigned_int,
        is_float=is_float,
        size=size if size > 0 else 0,
    )


def clear_cache() -> None:
    """Clear TU and args caches. Useful between test runs."""
    _TU_CACHE.clear()
    _ARGS_CACHE.clear()
