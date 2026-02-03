"""
Comprehensive tool implementations for pyghidra-mcp.
"""

import functools
import logging
import typing
from pathlib import Path
from typing import Optional, Tuple

from pyghidra_mcp.models import (
    CrossReferenceInfo,
    DecompiledFunction,
    ExportInfo,
    FunctionInfo,
    ImportInfo,
    SymbolInfo,
)
from pyghidra_mcp.symbol_lookup import (
    SymbolMatcher,
    demangle_msvc,
    extract_method_name,
    extract_class_name,
    DEFAULT_MAP_FILE,
)

if typing.TYPE_CHECKING:
    import ghidra
    from ghidra.program.model.listing import Function
    from pyghidra_mcp.cache_manager import CacheManager
    from pyghidra_mcp.context import ProgramInfo as ContextProgramInfo


logger = logging.getLogger(__name__)


def handle_exceptions(func):
    """Decorator to handle exceptions in tool methods"""

    @functools.wraps(func)
    def wrapper(*args, **kwargs):
        try:
            return func(*args, **kwargs)
        except Exception as e:
            logger.error(f"Error in {func.__name__}: {str(e)}")
            raise

    return wrapper


class GhidraTools:
    """Comprehensive tool handler for Ghidra MCP tools"""

    def __init__(
        self,
        program_info: "ContextProgramInfo",
        cache_manager: "CacheManager | None" = None,
        map_file: Optional[Path] = None,
    ):
        """Initialize with a Ghidra ProgramInfo object

        Args:
            program_info: Ghidra program information
            cache_manager: Optional cache manager for decompilation caching
            map_file: Optional path to linker map file for address lookups
        """
        self.program_info = program_info
        self.program = program_info.program
        self.decompiler = program_info.decompiler
        self.cache_manager = cache_manager

        # Initialize symbol matcher for multi-strategy lookups
        map_path = map_file or DEFAULT_MAP_FILE
        self.symbol_matcher = SymbolMatcher(map_path)

        # Compute binary hash for cache lookups
        self.binary_hash = None
        if cache_manager and program_info.file_path:
            try:
                from pyghidra_mcp.cache_manager import compute_binary_hash
                self.binary_hash = compute_binary_hash(program_info.file_path)
            except Exception as e:
                logger.debug(f"Could not compute binary hash: {e}")

    def _get_filename(self, func: "ghidra.program.model.listing.Function"):
        max_path_len = 12
        return f"{func.getName()[:max_path_len]}-{func.entryPoint}"

    def find_function(self, name: str) -> Optional["Function"]:
        """
        Find a function using multi-strategy lookup.

        Tries in order:
        1. Address lookup (from map file) - O(1) hash lookup
        2. Exact name match - O(n) iteration
        3. Demangled name match (Class::Method format)
        4. Method name only match
        5. Partial/substring match

        Args:
            name: Function name (mangled, demangled, or partial)

        Returns:
            Ghidra Function object or None if not found
        """
        fm = self.program.getFunctionManager()

        # Strategy 0: Direct hex address (e.g., "0x82E4E6B8" or "82E4E6B8")
        raw_addr = None
        stripped = name.strip().lower().replace("0x", "")
        if stripped and all(c in "0123456789abcdef" for c in stripped):
            try:
                raw_addr = int(stripped, 16)
            except ValueError:
                pass
        if raw_addr is not None:
            addr_formats = [
                f"0x{raw_addr:08x}",
                f"ram:0x{raw_addr:08x}",
            ]
            for addr_str in addr_formats:
                try:
                    addr = self.program.getAddressFactory().getAddress(addr_str)
                    if addr:
                        func = fm.getFunctionAt(addr)
                        if func:
                            logger.debug(f"Found function by direct address: {name} -> {func.name}")
                            return func
                        func = fm.getFunctionContaining(addr)
                        if func:
                            logger.debug(f"Found function by direct address (containing): {name} -> {func.name}")
                            return func
                except Exception as e:
                    logger.debug(f"Direct address lookup failed for {addr_str}: {e}")

        # Strategy 1: Address lookup from map file (O(1) - try first!)
        address = self.symbol_matcher.get_address(name)
        if address:
            # Try multiple address formats - Ghidra can be picky
            addr_formats = [
                f"0x{address:08x}",  # 0x82674a08
                f"{address:08x}",   # 82674a08
                f"ram:0x{address:08x}",  # ram:0x82674a08
                f"ram:{address:08x}",  # ram:82674a08
            ]
            found_addr = None  # Track successful address parse
            for addr_str in addr_formats:
                logger.debug(f"Attempting address lookup: {name} -> {addr_str}")
                try:
                    addr = self.program.getAddressFactory().getAddress(addr_str)
                    if addr:
                        found_addr = addr  # Save for later function creation
                        # Try exact match first
                        func = fm.getFunctionAt(addr)
                        if func:
                            logger.debug(f"Found function by address lookup (exact): {name} -> {addr_str}")
                            return func
                        # Try containing function (in case address is slightly off)
                        func = fm.getFunctionContaining(addr)
                        if func:
                            logger.debug(f"Found function by address lookup (containing): {name} -> {addr_str} -> {func.name}")
                            return func
                        logger.debug(f"No function at/containing address {addr_str}")
                    else:
                        logger.debug(f"Address factory returned None for {addr_str}")
                except Exception as e:
                    logger.debug(f"Address lookup failed for {addr_str}: {e}")

            # Strategy 1b: Try creating function at address if not found
            # Some symbols exist in map file but Ghidra didn't create functions during analysis
            if found_addr:
                try:
                    from ghidra.app.cmd.function import CreateFunctionCmd
                    cmd = CreateFunctionCmd(found_addr)
                    success = cmd.applyTo(self.program)
                    if success:
                        func = fm.getFunctionAt(found_addr)
                        if func:
                            logger.info(f"Created function at 0x{address:08x} for: {name}")
                            return func
                    else:
                        # Check if address is inside another function (inlined/thunk)
                        containing_func = fm.getFunctionContaining(found_addr)
                        if containing_func:
                            logger.warning(f"Address 0x{address:08x} is inside {containing_func.name} - "
                                         "possibly inlined or a jump target")
                        else:
                            logger.debug(f"Failed to create function at 0x{address:08x}")
                except Exception as e:
                    logger.debug(f"Failed to create function at 0x{address:08x}: {e}")
        else:
            logger.debug(f"No address found in map file for: {name}")

        # Strategy 2: Exact name match (O(n) - only if map lookup fails)
        functions = fm.getFunctions(True)
        for func in functions:
            if name == func.name:
                logger.debug(f"Found function by exact match: {name}")
                return func

        # Strategy 3-5: Try search variants (demangled, method name, etc.)
        variants = self.symbol_matcher.get_search_variants(name)
        for variant, match_type in variants:
            if match_type == "exact":
                continue  # Already tried

            # Try exact match on variant
            functions = fm.getFunctions(True)
            for func in functions:
                func_name = func.name

                if match_type == "demangled" and variant == func_name:
                    logger.debug(f"Found function by demangled match: {name} -> {func_name}")
                    return func

                if match_type == "short_demangled" and variant == func_name:
                    logger.debug(f"Found function by short demangled match: {name} -> {func_name}")
                    return func

                if match_type == "method":
                    # Method name might match end of function name
                    # e.g., variant="PoseMeshes" matches func_name="CharBonesMeshes::PoseMeshes"
                    if func_name == variant or func_name.endswith(f"::{variant}"):
                        logger.debug(f"Found function by method name match: {name} -> {func_name}")
                        return func

        # Strategy 6: Partial/substring match (last resort)
        # Try to find function where method name is in the function name
        method_name = extract_method_name(name)
        class_name = extract_class_name(name)

        if method_name:
            functions = fm.getFunctions(True)
            candidates = []
            for func in functions:
                func_name = func.name
                # Check if method name appears in function name
                if method_name.lower() in func_name.lower():
                    # Prefer matches that also have class name
                    if class_name and class_name.lower() in func_name.lower():
                        candidates.insert(0, func)  # Higher priority
                    else:
                        candidates.append(func)

            if candidates:
                logger.debug(f"Found function by partial match: {name} -> {candidates[0].name}")
                return candidates[0]

        logger.warning(f"Function not found with any strategy: {name}")
        return None

    def find_function_address(self, name: str) -> Optional["ghidra.program.model.address.Address"]:
        """
        Find a function's address using multi-strategy lookup.

        Similar to find_function but returns the address directly,
        useful for cross-reference lookups.

        Args:
            name: Function name (mangled, demangled, or partial)

        Returns:
            Ghidra Address object or None if not found
        """
        # Try finding the function first
        func = self.find_function(name)
        if func:
            return func.getEntryPoint()

        # If function not found, try direct address lookup
        address = self.symbol_matcher.get_address(name)
        if address:
            addr_str = self.symbol_matcher.format_address_for_ghidra(address)
            try:
                return self.program.getAddressFactory().getAddress(addr_str)
            except Exception as e:
                logger.debug(f"Address conversion failed: {e}")

        return None

    @handle_exceptions
    def decompile_function(self, name: str, timeout: int = 0) -> DecompiledFunction:
        """Decompiles a function in a specified binary and returns its pseudo-C code.

        Uses multi-strategy lookup to find functions by:
        - Exact name match
        - Address lookup from map file
        - Demangled name (Class::Method)
        - Method name only
        - Partial/substring match
        """
        from ghidra.app.decompiler import DecompileResults
        from ghidra.util.task import ConsoleTaskMonitor

        # Use multi-strategy lookup
        func = self.find_function(name)
        if not func:
            raise ValueError(f"Function {name} not found")

        # Try cache first
        address = str(func.getEntryPoint())
        if self.cache_manager and self.binary_hash:
            cached = self.cache_manager.get(address, self.binary_hash)
            if cached:
                logger.info(f"Cache hit for {name} at {address}")
                return DecompiledFunction(
                    name=self._get_filename(func),
                    code=cached,
                    signature=None,  # Could be parsed from cached code if needed
                )

        # Cache miss - decompile
        logger.debug(f"Cache miss for {name}, decompiling...")
        monitor = ConsoleTaskMonitor()
        result: DecompileResults = self.decompiler.decompileFunction(
            func, timeout, monitor
        )
        if "" == result.getErrorMessage():
            code = result.decompiledFunction.getC()
            sig = result.decompiledFunction.getSignature()
        else:
            code = result.getErrorMessage()
            sig = None

        # Store in cache
        if self.cache_manager and self.binary_hash and code:
            self.cache_manager.put(address, self.binary_hash, code)

        return DecompiledFunction(
            name=self._get_filename(func), code=code, signature=sig
        )

    @handle_exceptions
    def search_functions_by_name(
        self, query: str, offset: int = 0, limit: int = 100
    ) -> list[FunctionInfo]:
        """Searches for functions within a binary by name."""
        from ghidra.program.model.listing import Function

        if not query:
            raise ValueError("Query string is required")

        funcs = []
        fm = self.program.getFunctionManager()
        functions = fm.getFunctions(True)
        # Search for functions containing the query string
        for func in functions:
            func: Function
            if query.lower() in func.name.lower():
                funcs.append(FunctionInfo(name=func.name, entry_point=str(func.getEntryPoint())))
        return funcs[offset : limit + offset]

    @handle_exceptions
    def search_symbols_by_name(
        self, query: str, offset: int = 0, limit: int = 100
    ) -> list[SymbolInfo]:
        """Searches for symbols within a binary by name."""
        from ghidra.program.model.symbol import SymbolTable

        if not query:
            raise ValueError("Query string is required")

        symbols_info = []
        st: SymbolTable = self.program.getSymbolTable()
        symbols = st.getAllSymbols(True)
        rm = self.program.getReferenceManager()

        # Search for symbols containing the query string
        for symbol in symbols:
            if query.lower() in symbol.name.lower():
                ref_count = len(list(rm.getReferencesTo(symbol.getAddress())))
                symbols_info.append(
                    SymbolInfo(
                        name=symbol.name,
                        address=str(symbol.getAddress()),
                        type=str(symbol.getSymbolType()),
                        namespace=str(symbol.getParentNamespace()),
                        source=str(symbol.getSource()),
                        refcount=ref_count,
                    )
                )
        return symbols_info[offset : limit + offset]

    @handle_exceptions
    def list_exports(self) -> list[ExportInfo]:
        """Lists all exported functions and symbols from a specified binary."""
        exports = []
        symbols = self.program.getSymbolTable().getAllSymbols(True)
        for symbol in symbols:
            if symbol.isExternalEntryPoint():
                exports.append(ExportInfo(name=symbol.getName(), address=str(symbol.getAddress())))
        return exports

    @handle_exceptions
    def list_imports(self) -> list[ImportInfo]:
        """Lists all imported functions and symbols for a specified binary."""
        imports = []
        symbols = self.program.getSymbolTable().getExternalSymbols()
        for symbol in symbols:
            imports.append(
                ImportInfo(name=symbol.getName(), library=str(symbol.getParentNamespace()))
            )
        return imports

    @handle_exceptions
    def list_cross_references(self, name_or_address: str) -> list[CrossReferenceInfo]:
        """Finds and lists all cross-references (x-refs) to a given function or address within a binary.

        Uses multi-strategy lookup to find functions by:
        - Direct address (hex string like "823486e0")
        - Exact name match
        - Address lookup from map file
        - Demangled name (Class::Method)
        - Method name only
        - Partial/substring match
        """
        # Try parsing as direct address first
        addr = None
        try:
            addr = self.program.getAddressFactory().getAddress(name_or_address)
        except Exception:
            pass

        # If not a valid address, use multi-strategy function lookup
        if addr is None:
            addr = self.find_function_address(name_or_address)

        if addr is None:
            raise ValueError(f"Could not find function or address: {name_or_address}")

        cross_references = []

        # Get references
        rm = self.program.getReferenceManager()
        references = rm.getReferencesTo(addr)

        for ref in references:
            func = self.program.getFunctionManager().getFunctionContaining(ref.getFromAddress())
            cross_references.append(
                CrossReferenceInfo(
                    function_name=func.getName() if func else None,
                    from_address=str(ref.getFromAddress()),
                    to_address=str(ref.getToAddress()),
                    type=str(ref.getReferenceType()),
                )
            )
        return cross_references
