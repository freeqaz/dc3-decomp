# Server Logging
# ---------------------------------------------------------------------------------
import logging
import os
import signal
import socket
import subprocess
import sys
import time
from collections.abc import AsyncIterator
from contextlib import asynccontextmanager
from pathlib import Path
from logging.handlers import RotatingFileHandler
from typing import Optional

import click
import pyghidra
from mcp.server import Server
from mcp.server.fastmcp import Context, FastMCP
from mcp.shared.exceptions import McpError
from mcp.types import INTERNAL_ERROR, INVALID_PARAMS, ErrorData

from pyghidra_mcp.__init__ import __version__
from pyghidra_mcp.cache_manager import CacheManager
from pyghidra_mcp.context import ProgramInfo as ContextProgramInfo, PyGhidraContext
from pyghidra_mcp.models import (
    CrossReferenceInfos,
    DecompiledFunction,
    ExportInfos,
    FunctionSearchResults,
    ImportInfos,
    ProgramInfo,
    ProgramInfos,
    SymbolSearchResults,
)
from pyghidra_mcp.tools import GhidraTools

# Setup logging with both console and file output
def setup_logging(log_file: Optional[str] = None) -> logging.Logger:
    """Configure logging with console and optional file output."""
    logger = logging.getLogger(__name__)
    logger.setLevel(logging.DEBUG)

    # Console handler (stderr for stdio transport compatibility)
    console_handler = logging.StreamHandler(sys.stderr)
    console_handler.setLevel(logging.INFO)
    console_formatter = logging.Formatter("%(asctime)s [%(levelname)s] %(message)s")
    console_handler.setFormatter(console_formatter)
    logger.addHandler(console_handler)

    # File handler with rotation (if log file specified)
    if log_file:
        log_path = Path(log_file)
        log_path.parent.mkdir(parents=True, exist_ok=True)

        file_handler = RotatingFileHandler(
            log_file,
            maxBytes=10 * 1024 * 1024,  # 10 MB
            backupCount=10,  # Keep 10 files
        )
        file_handler.setLevel(logging.DEBUG)
        file_formatter = logging.Formatter(
            "%(asctime)s [%(levelname)s] %(name)s: %(message)s"
        )
        file_handler.setFormatter(file_formatter)
        logger.addHandler(file_handler)

    return logger

# Initialize logger (will be reconfigured with log file in main())
logger = setup_logging()
logger.info(f"Server initialized (version {__version__})")

# Service start time for uptime tracking
SERVICE_START_TIME = time.time()


# Init Pyghidra
# ---------------------------------------------------------------------------------
@asynccontextmanager
async def server_lifespan(server: Server) -> AsyncIterator[PyGhidraContext]:
    """Manage server startup and shutdown lifecycle."""
    try:
        yield server._pyghidra_context
    finally:
        # pyghidra_context.close()
        pass


# Port Management and Diagnostics
# ---------------------------------------------------------------------------------
def cleanup_stale_port(port: int = 8000, timeout_seconds: int = 5) -> bool:
    """Kill stale processes using the port and wait for it to become available.

    Args:
        port: Port number to clean up
        timeout_seconds: How long to wait for port to become free

    Returns:
        True if port is available, False if timeout
    """
    try:
        result = subprocess.run(
            ["lsof", "-i", f":{port}", "-t"],
            capture_output=True,
            text=True,
            timeout=5,
        )
        if result.stdout.strip():
            pids = result.stdout.strip().split("\n")
            for pid in pids:
                try:
                    pid_int = int(pid.strip())
                    os.kill(pid_int, signal.SIGKILL)
                    logger.info(f"Killed stale process {pid_int} on port {port}")
                    time.sleep(0.1)
                except (ValueError, ProcessLookupError, PermissionError) as e:
                    logger.debug(f"Could not kill PID {pid}: {e}")
    except FileNotFoundError:
        logger.debug("lsof not available, skipping port cleanup")
    except Exception as e:
        logger.debug(f"Port cleanup error: {e}")

    # Wait for port to be free
    start_time = time.time()
    while time.time() - start_time < timeout_seconds:
        try:
            sock = socket.create_connection(("127.0.0.1", port), timeout=1)
            sock.close()
            time.sleep(0.2)
        except (ConnectionRefusedError, socket.timeout):
            logger.info(f"Port {port} is now available")
            return True

    logger.warning(f"Port {port} still in use after {timeout_seconds}s (may proceed anyway)")
    return False


def diagnose() -> None:
    """Run diagnostics for the Ghidra service and print results."""
    print("=" * 70)
    print("Ghidra Service Diagnostics")
    print("=" * 70)

    # Check Ghidra install
    ghidra_home = os.environ.get("GHIDRA_INSTALL_DIR")
    print(f"\nGhidra Installation:")
    print(f"  GHIDRA_INSTALL_DIR: {ghidra_home}")
    if ghidra_home:
        print(f"  Exists: {os.path.exists(ghidra_home)}")
        if os.path.exists(ghidra_home):
            print(f"  Writable: {os.access(ghidra_home, os.W_OK)}")

    ghidra_user = os.environ.get("GHIDRA_USER_HOME")
    print(f"  GHIDRA_USER_HOME: {ghidra_user}")
    if ghidra_user:
        print(f"  Exists: {os.path.exists(ghidra_user)}")
        if os.path.exists(ghidra_user):
            print(f"  Writable: {os.access(ghidra_user, os.W_OK)}")

    # Check Java
    java_home = os.environ.get("JAVA_HOME")
    print(f"\nJava Configuration:")
    print(f"  JAVA_HOME: {java_home}")
    if java_home and os.path.exists(os.path.join(java_home, "bin", "java")):
        print(f"  Java executable found")

    # Check port
    port = 8000
    print(f"\nPort Status (Port {port}):")
    try:
        sock = socket.create_connection(("127.0.0.1", port), timeout=1)
        sock.close()
        print(f"  Status: IN USE (likely by existing service)")
    except (ConnectionRefusedError, socket.timeout):
        print(f"  Status: AVAILABLE")
    except Exception as e:
        print(f"  Status: ERROR - {e}")

    # Check temp directories
    print(f"\nTemporary Directories:")
    for tmpdir in ["/tmp/claude", "/tmp"]:
        exists = os.path.exists(tmpdir)
        writable = os.access(tmpdir, os.W_OK) if exists else False
        print(f"  {tmpdir}: exists={exists}, writable={writable}")

    # Check logs
    print(f"\nService Logs:")
    log_file = "/tmp/claude/pyghidra-service.log"
    if os.path.exists(log_file):
        size = os.path.getsize(log_file)
        print(f"  {log_file}: {size} bytes")
        try:
            with open(log_file, "r") as f:
                lines = f.readlines()
                print(f"  Last 5 log entries:")
                for line in lines[-5:]:
                    print(f"    {line.rstrip()}")
        except Exception as e:
            print(f"  Error reading logs: {e}")
    else:
        print(f"  {log_file}: not found")

    print("\n" + "=" * 70)


mcp = FastMCP("pyghidra-mcp", lifespan=server_lifespan)


def _get_program_info_or_raise(
    pyghidra_context: PyGhidraContext, binary_name: str
) -> ContextProgramInfo:
    """Get program info or raise McpError if not found."""
    program_info = pyghidra_context.programs.get(binary_name)
    if not program_info:
        available_progs = list(pyghidra_context.programs.keys())
        raise McpError(
            ErrorData(
                code=INVALID_PARAMS,
                message=f"Binary {binary_name} not found. Available binaries: {available_progs}",
            )
        )
    return program_info


# MCP Tools
# ---------------------------------------------------------------------------------
# Health Check and Status Tools
# ---------------------------------------------------------------------------------
@mcp.tool()
def get_service_health(ctx: Context) -> dict:
    """Returns health status of the Ghidra service.

    This endpoint can be called to verify the service is running and responsive.
    Returns uptime, version, and Ghidra readiness status.
    """
    try:
        pyghidra_context: PyGhidraContext = ctx.request_context.lifespan_context
        uptime_seconds = int(time.time() - SERVICE_START_TIME)
        ghidra_ready = (
            len(pyghidra_context.programs) > 0
            if pyghidra_context else False
        )

        return {
            "status": "healthy",
            "version": __version__,
            "uptime_seconds": uptime_seconds,
            "ghidra_ready": ghidra_ready,
            "programs_loaded": len(pyghidra_context.programs) if pyghidra_context else 0,
        }
    except Exception as e:
        logger.error(f"Health check error: {e}")
        return {
            "status": "error",
            "version": __version__,
            "error": str(e),
        }


@mcp.tool()
async def decompile_function(binary_name: str, name: str, ctx: Context) -> DecompiledFunction:
    """Decompiles a function in a specified binary and returns its pseudo-C code.

    Args:
        binary_name: The name of the binary containing the function.
        name: The name of the function to decompile.
    """
    try:
        pyghidra_context: PyGhidraContext = ctx.request_context.lifespan_context
        program_info = _get_program_info_or_raise(pyghidra_context, binary_name)
        cache_manager = getattr(mcp, '_cache_manager', None)
        tools = GhidraTools(program_info, cache_manager=cache_manager)
        return tools.decompile_function(name)
    except Exception as e:
        raise McpError(
            ErrorData(code=INTERNAL_ERROR, message=f"Error decompiling function: {e!s}")
        ) from e


@mcp.tool()
def search_functions_by_name(
    binary_name: str, query: str, ctx: Context, offset: int = 0, limit: int = 100
) -> FunctionSearchResults:
    """Searches for functions within a binary by name.

    Args:
        binary_name: The name of the binary to search within.
        query: The substring to search for in function names (case-insensitive).
        offset: The number of results to skip.
        limit: The maximum number of results to return.
    """
    try:
        pyghidra_context: PyGhidraContext = ctx.request_context.lifespan_context
        program_info = _get_program_info_or_raise(pyghidra_context, binary_name)
        tools = GhidraTools(program_info)
        functions = tools.search_functions_by_name(query, offset, limit)
        return FunctionSearchResults(functions=functions)
    except Exception as e:
        raise McpError(
            ErrorData(code=INTERNAL_ERROR, message=f"Error searching for functions: {e!s}")
        ) from e


@mcp.tool()
def search_symbols_by_name(
    binary_name: str, query: str, ctx: Context, offset: int = 0, limit: int = 100
) -> SymbolSearchResults:
    """Searches for symbols within a binary by name.

    Args:
        binary_name: The name of the binary to search within.
        query: The substring to search for in symbol names (case-insensitive).
        offset: The number of results to skip.
        limit: The maximum number of results to return.
    """
    try:
        pyghidra_context: PyGhidraContext = ctx.request_context.lifespan_context
        program_info = _get_program_info_or_raise(pyghidra_context, binary_name)
        tools = GhidraTools(program_info)
        symbols = tools.search_symbols_by_name(query, offset, limit)
        return SymbolSearchResults(symbols=symbols)
    except Exception as e:
        raise McpError(
            ErrorData(code=INTERNAL_ERROR, message=f"Error searching for symbols: {e!s}")
        ) from e


@mcp.tool()
def list_project_binaries(ctx: Context) -> list[str]:
    """Lists the names of all binaries currently loaded in the Ghidra project."""
    try:
        pyghidra_context: PyGhidraContext = ctx.request_context.lifespan_context
        return list(pyghidra_context.programs.keys())
    except Exception as e:
        raise McpError(
            ErrorData(code=INTERNAL_ERROR, message=f"Error listing project binaries: {e!s}")
        ) from e


@mcp.tool()
def list_project_program_info(ctx: Context) -> ProgramInfos:
    """Retrieves detailed information for all programs (binaries) in the project."""
    try:
        pyghidra_context: PyGhidraContext = ctx.request_context.lifespan_context
        program_infos = []
        for _name, pi in pyghidra_context.programs.items():
            program_infos.append(
                ProgramInfo(
                    name=pi.name,
                    file_path=str(pi.file_path) if pi.file_path else None,
                    load_time=pi.load_time,
                    analysis_complete=pi.analysis_complete,
                    metadata=pi.metadata,
                )
            )
        return ProgramInfos(programs=program_infos)
    except Exception as e:
        raise McpError(
            ErrorData(
                code=INTERNAL_ERROR,
                message=f"Error listing project program info: {e!s}",
            )
        ) from e


@mcp.tool()
def list_exports(binary_name: str, ctx: Context) -> ExportInfos:
    """Lists all exported functions and symbols from a specified binary.

    Args:
        binary_name: The name of the binary to list exports from.
    """
    try:
        pyghidra_context: PyGhidraContext = ctx.request_context.lifespan_context
        program_info = _get_program_info_or_raise(pyghidra_context, binary_name)
        tools = GhidraTools(program_info)
        exports = tools.list_exports()
        return ExportInfos(exports=exports)
    except Exception as e:
        raise McpError(
            ErrorData(code=INTERNAL_ERROR, message=f"Error listing exports: {e!s}")
        ) from e


@mcp.tool()
def list_imports(binary_name: str, ctx: Context) -> ImportInfos:
    """Lists all imported functions and symbols for a specified binary.

    Args:
        binary_name: The name of the binary to list imports from.
    """
    try:
        pyghidra_context: PyGhidraContext = ctx.request_context.lifespan_context
        program_info = _get_program_info_or_raise(pyghidra_context, binary_name)
        tools = GhidraTools(program_info)
        imports = tools.list_imports()
        return ImportInfos(imports=imports)
    except Exception as e:
        raise McpError(
            ErrorData(code=INTERNAL_ERROR, message=f"Error listing imports: {e!s}")
        ) from e


@mcp.tool()
def list_cross_references(
    binary_name: str, name_or_address: str, ctx: Context
) -> CrossReferenceInfos:
    """Finds and lists all cross-references (x-refs) to a given function or address within a binary. This is crucial for understanding how code and data are used and related.

    Args:
        binary_name: The name of the binary to search for cross-references in.
        name_or_address: The name of the function or a specific address (e.g., '0x1004010') to find cross-references to.
    """
    try:
        pyghidra_context: PyGhidraContext = ctx.request_context.lifespan_context
        program_info = _get_program_info_or_raise(pyghidra_context, binary_name)
        tools = GhidraTools(program_info)
        cross_references = tools.list_cross_references(name_or_address)
        return CrossReferenceInfos(cross_references=cross_references)
    except Exception as e:
        raise McpError(
            ErrorData(code=INTERNAL_ERROR, message=f"Error listing cross-references: {e!s}")
        ) from e


@mcp.tool()
def get_cache_stats(ctx: Context) -> dict:
    """Returns decompilation cache statistics.

    Returns cache hit count, entry count, hit rate, and cache size.
    Useful for diagnostics and understanding cache performance.
    """
    try:
        cache_manager = getattr(mcp, '_cache_manager', None)
        if not cache_manager:
            return {
                "enabled": False,
                "message": "Cache not initialized",
            }
        return cache_manager.get_stats()
    except Exception as e:
        logger.error(f"Error getting cache stats: {e}")
        return {
            "error": str(e),
        }


def _detect_binary_language(binary_path: Path) -> tuple[str | None, str | None]:
    """Detect binary format and return language/compiler IDs if needed.

    When XEXLoaderWV is installed via _install_xex_loader(), XEX files are
    handled natively and don't need a language hint. Falls back to explicit
    language specification if the loader isn't available.
    """
    try:
        with binary_path.open("rb") as f:
            header = f.read(4)
            if header.startswith(b"XEX2"):
                # Check if XEXLoaderWV is installed
                ghidra_dir = os.environ.get("GHIDRA_INSTALL_DIR", "")
                ext_dir = Path(ghidra_dir) / "Extensions" / "XEXLoaderWV" if ghidra_dir else None
                if ext_dir and ext_dir.exists():
                    # XEX loader handles format parsing, but we must specify
                    # the Xenon language variant for VMX128 instruction support
                    logger.info("XEX binary detected, using XEXLoaderWV with Xenon language")
                    return "PowerPC:BE:64:Xenon", None
                else:
                    # Fallback: import as raw binary with PowerPC language
                    logger.info("XEX binary detected, no XEXLoaderWV - using raw import")
                    return "PowerPC:BE:64:Xenon", None
    except Exception as e:
        logger.debug(f"Could not detect language for {binary_path}: {e}")
    return None, None


def _install_xex_loader(launcher: "pyghidra.HeadlessPyGhidraLauncher"):
    """Install XEXLoaderWV extension if available, so Ghidra can import XEX files natively."""
    # Look for the built dist zip first (preferred by install_plugin)
    xex_loader_home = Path.home() / "code" / "milohax" / "XEXLoaderWV" / "XEXLoaderWV"
    dist_dir = xex_loader_home / "dist"
    if dist_dir.exists():
        zips = sorted(dist_dir.glob("*.zip"))
        if zips:
            zip_path = zips[-1]  # Latest zip
            try:
                details = pyghidra.ExtensionDetails.from_file(xex_loader_home)
                launcher.install_plugin(zip_path, details)
                logger.info(f"Installed XEXLoaderWV from {zip_path}")
                return
            except Exception as e:
                logger.warning(f"install_plugin failed with zip: {e}")

    # Fallback: add jar to classpath directly
    ghidra_dir = os.environ.get("GHIDRA_INSTALL_DIR", "")
    if not ghidra_dir:
        return
    jar = Path(ghidra_dir) / "Extensions" / "XEXLoaderWV" / "lib" / "XEXLoaderWV.jar"
    if jar.exists():
        try:
            launcher.add_class_files(jar)
            logger.info(f"Added XEXLoaderWV jar to classpath: {jar}")
        except Exception as e:
            logger.warning(f"Failed to add XEXLoaderWV jar: {e}")
    else:
        logger.warning("XEXLoaderWV not found")


def init_pyghidra_context(
    mcp: FastMCP, input_paths: list[Path], project_name: str, project_directory: str
) -> FastMCP:
    if not input_paths:
        raise ValueError("Missing Input Paths!")

    bin_paths = [Path(p) for p in input_paths]

    logger.info(f"Analyzing {', '.join(map(str, bin_paths))}")
    logger.info(f"Project: {project_name}")
    logger.info(f"Project: Location {project_directory}")

    # init pyghidra with XEX loader extension
    launcher = pyghidra.HeadlessPyGhidraLauncher(verbose=False)
    _install_xex_loader(launcher)
    launcher.start()

    # init PyGhidraContext / import + analyze binaries
    pyghidra_context = PyGhidraContext(project_name, project_directory)
    logger.info(f"Importing binaries: {project_directory}")

    # Import with language detection for XEX files
    for bin_path in bin_paths:
        language, compiler = _detect_binary_language(Path(bin_path))
        if language:
            logger.info(f"Detected XEX binary, using language: {language}")
        pyghidra_context.import_binary(bin_path, language=language, compiler=compiler)

    logger.info(f"Analyize project: {pyghidra_context.project}")
    pyghidra_context.analyze_project()

    mcp._pyghidra_context = pyghidra_context

    return mcp


# MCP Server Entry Point
# ---------------------------------------------------------------------------------


@click.command(context_settings={"help_option_names": ["-h", "--help"]})
@click.version_option(
    __version__,
    "-v",
    "--version",
    help="Show version and exit.",
)
@click.option(
    "-t",
    "--transport",
    type=click.Choice(["stdio", "streamable-http", "sse"]),
    default="stdio",
    envvar="MCP_TRANSPORT",
    help="Transport protocol to use: stdio, streamable-http, or sse (legacy)",
)
@click.option(
    "--project-name",
    default="pyghidra_mcp",
    help="Name of the Ghidra project.",
)
@click.option(
    "--project-directory",
    default="pyghidra_mcp_projects",
    type=click.Path(),
    help="Directory to store the Ghidra project.",
)
@click.option(
    "--cache-dir",
    type=click.Path(),
    default=None,
    help="Directory to store decompilation cache (cache.db). Defaults to current directory.",
)
@click.option(
    "--cache-disabled",
    is_flag=True,
    default=False,
    help="Disable decompilation caching for this run.",
)
@click.option(
    "--cache-clear",
    is_flag=True,
    default=False,
    help="Clear entire decompilation cache and exit.",
)
@click.option(
    "--cache-stats",
    is_flag=True,
    default=False,
    help="Print cache statistics and exit.",
)
@click.option(
    "--log-file",
    type=click.Path(),
    default=None,
    help="Log file path (optional). If set, logs will be written to this file with rotation.",
)
@click.option(
    "--diagnose",
    is_flag=True,
    help="Run service diagnostics and exit.",
)
@click.argument("input_paths", type=click.Path(exists=True), nargs=-1, required=False)
def main(
    transport: str,
    input_paths: list[Path],
    project_name: str,
    project_directory: str,
    cache_dir: Optional[str],
    cache_disabled: bool,
    cache_clear: bool,
    cache_stats: bool,
    log_file: Optional[str],
    diagnose: bool,
) -> None:
    """PyGhidra Command-Line MCP server

    - input_paths: Path to one or more binaries to import, analyze, and expose with pyghidra-mcp
    - transport: Supports stdio, streamable-http, and sse transports.
    For stdio, it will read from stdin and write to stdout.
    For streamable-http and sse, it will start an HTTP server on port 8000.

    """
    # Reconfigure logging with file handler if specified
    global logger
    logger = setup_logging(log_file)
    logger.info(f"PyGhidra MCP Server starting (version {__version__})")

    # Handle diagnostic mode
    if diagnose:
        diagnose()
        sys.exit(0)

    # Initialize cache manager
    cache_dir_path = Path(cache_dir) if cache_dir else Path.cwd()
    cache_manager = CacheManager(cache_dir=cache_dir_path, enabled=not cache_disabled)
    mcp._cache_manager = cache_manager

    # Handle cache management commands
    if cache_clear:
        cleared = cache_manager.clear()
        logger.info(f"Cache cleared: {cleared} entries removed")
        print(f"Cache cleared: {cleared} entries removed")
        return

    if cache_stats:
        stats = cache_manager.get_stats()
        import json
        logger.info(f"Cache stats: {json.dumps(stats)}")
        print(json.dumps(stats, indent=2))
        return

    # Require input_paths for normal operation
    if not input_paths:
        click.echo("Error: input_paths are required for normal operation", err=True)
        click.echo("Use --cache-stats or --cache-clear with no input_paths for cache-only operations", err=True)
        sys.exit(1)

    # Clean up stale port before starting HTTP service
    if transport in ["streamable-http", "sse"]:
        logger.info("Cleaning up stale port 8000...")
        cleanup_stale_port(port=8000, timeout_seconds=5)

    logger.info(f"Using transport: {transport}")
    logger.info(f"Project: {project_name} at {project_directory}")

    init_pyghidra_context(mcp, input_paths, project_name, project_directory)

    try:
        logger.info("Starting MCP service...")
        if transport == "stdio":
            mcp.run(transport="stdio")
        elif transport == "streamable-http":
            mcp.run(transport="streamable-http")
        elif transport == "sse":
            mcp.run(transport="sse")
        else:
            raise ValueError(f"Invalid transport: {transport}")
    except KeyboardInterrupt:
        logger.info("Service interrupted by user")
    except Exception as e:
        logger.error(f"Service error: {e}", exc_info=True)
        raise
    finally:
        logger.info("Closing service...")
        if hasattr(mcp, "_pyghidra_context") and mcp._pyghidra_context:
            mcp._pyghidra_context.close()
        logger.info("Service stopped")


if __name__ == "__main__":
    main()
