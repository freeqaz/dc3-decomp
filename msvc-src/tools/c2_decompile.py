#!/usr/bin/env python3
"""Decompile c2.dll functions using pyghidra directly (no MCP server).

Prerequisites:
    pip install pyghidra
    export GHIDRA_INSTALL_DIR=/opt/ghidra  # or wherever Ghidra is installed

Usage:
    python3 c2_decompile.py decompile 0x10bc6487
    python3 c2_decompile.py decompile 0x10bc9550 0x10bc9fda 0x10bc69f1
    python3 c2_decompile.py callees 0x10bc6487
    python3 c2_decompile.py callers 0x10bc6487
    python3 c2_decompile.py strings "register"
    python3 c2_decompile.py info 0x10bc6487
    python3 c2_decompile.py read-bytes 0x10c3b6a0 256

Known addresses (COLOR register allocator):
    0x10bc6487  color_init (entry, 23 bytes)
    0x10bc62b6  color_dispatch (465 bytes)
    0x10bc514a  color_alloc_simple (842 bytes)
    0x10bc5494  color_alloc_complex (1089 bytes)
    0x10bc58d5  color_select_reg (1891 bytes)
    0x10bc4be9  color_spill_cost (220 bytes)
    0x10bc6038  color_resolve_conflict (387 bytes)
    0x10bc61bb  color_assign_regs (251 bytes)
    0x10bc4ded  color_process_node (354 bytes)
"""
import os
import sys

os.environ.setdefault("GHIDRA_INSTALL_DIR", "/opt/ghidra")

import pyghidra

C2_DLL = os.path.join(
    os.path.dirname(__file__),
    "..", "..", "build", "compilers", "X360", "16.00.11886.00", "c2.dll"
)

_launcher_started = False

def ensure_launcher():
    global _launcher_started
    if not _launcher_started:
        pyghidra.start()
        _launcher_started = True


def _resolve_func(program, addr_str):
    """Resolve address string to a Ghidra Function object."""
    addr = program.getAddressFactory().getDefaultAddressSpace().getAddress(int(addr_str, 16))
    func = program.getFunctionManager().getFunctionAt(addr)
    if not func:
        func = program.getFunctionManager().getFunctionContaining(addr)
    return func


def decompile_at(program, addr_str):
    """Decompile function at given address."""
    from ghidra.app.decompiler import DecompInterface

    func = _resolve_func(program, addr_str)
    if not func:
        print(f"No function found at {addr_str}")
        return

    decomp = DecompInterface()
    decomp.openProgram(program)

    result = decomp.decompileFunction(func, 120, None)
    if result.decompileCompleted():
        print(f"// Function: {func.getName()} @ {func.getEntryPoint()}")
        print(f"// Size: {func.getBody().getNumAddresses()} bytes")
        print(result.getDecompiledFunction().getC())
    else:
        print(f"Decompilation failed for {addr_str}: {result.getErrorMessage()}")

    decomp.dispose()


def get_callees(program, addr_str):
    """List functions called by the function at addr."""
    func = _resolve_func(program, addr_str)
    if not func:
        print(f"No function at {addr_str}")
        return

    print(f"Callees of {func.getName()} @ {func.getEntryPoint()}:")
    called = func.getCalledFunctions(None)
    for f in sorted(called, key=lambda x: str(x.getEntryPoint())):
        size = f.getBody().getNumAddresses()
        print(f"  {f.getEntryPoint()} ({size:>5}b)  {f.getName()}")
    print(f"\nTotal: {called.size()} callees")


def get_callers(program, addr_str):
    """List functions that call the function at addr."""
    func = _resolve_func(program, addr_str)
    if not func:
        print(f"No function at {addr_str}")
        return

    print(f"Callers of {func.getName()} @ {func.getEntryPoint()}:")
    callers = func.getCallingFunctions(None)
    for f in sorted(callers, key=lambda x: str(x.getEntryPoint())):
        size = f.getBody().getNumAddresses()
        print(f"  {f.getEntryPoint()} ({size:>5}b)  {f.getName()}")
    print(f"\nTotal: {callers.size()} callers")


def func_info(program, addr_str):
    """Print function info at address."""
    func = _resolve_func(program, addr_str)
    if not func:
        print(f"No function at {addr_str}")
        return

    print(f"Name: {func.getName()}")
    print(f"Entry: {func.getEntryPoint()}")
    print(f"Size: {func.getBody().getNumAddresses()} bytes")
    sig = func.getSignature()
    print(f"Signature: {sig}")
    print(f"Calling convention: {func.getCallingConventionName()}")
    print(f"# callees: {func.getCalledFunctions(None).size()}")
    print(f"# callers: {func.getCallingFunctions(None).size()}")


def search_strings(program, query):
    """Search for strings containing query."""
    from ghidra.program.model.data import StringDataType

    listing = program.getListing()
    mem = program.getMemory()
    count = 0
    for block in mem.getBlocks():
        if not block.isInitialized():
            continue
        data_iter = listing.getDefinedData(block.getStart(), True)
        while data_iter.hasNext():
            data = data_iter.next()
            if data.getAddress().compareTo(block.getEnd()) > 0:
                break
            dt = data.getBaseDataType()
            if dt is not None and ("string" in dt.getName().lower() or "char" in dt.getName().lower()):
                try:
                    val = str(data.getValue())
                    if query.lower() in val.lower():
                        print(f"  {data.getAddress()}  {val[:120]}")
                        count += 1
                        if count >= 100:
                            print(f"  ... (truncated at 100)")
                            break
                except:
                    pass
        if count >= 100:
            break
    print(f"\n{count} strings found")


def read_bytes(program, addr_str, length):
    """Read raw bytes from the binary at addr."""
    addr = program.getAddressFactory().getDefaultAddressSpace().getAddress(int(addr_str, 16))
    mem = program.getMemory()
    buf = bytearray(length)
    mem.getBytes(addr, buf)

    # Print hex dump
    for i in range(0, length, 16):
        chunk = buf[i:i+16]
        hex_part = " ".join(f"{b:02x}" for b in chunk)
        ascii_part = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        offset = int(addr_str, 16) + i
        print(f"  {offset:08x}  {hex_part:<48s}  {ascii_part}")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    cmd = sys.argv[1]
    args = sys.argv[2:]

    ensure_launcher()

    with pyghidra.open_program(os.path.realpath(C2_DLL)) as flat:
        program = flat.getCurrentProgram()

        if cmd == "decompile":
            for addr in args:
                decompile_at(program, addr)
                if addr != args[-1]:
                    print("\n" + "=" * 70 + "\n")
        elif cmd == "callees":
            get_callees(program, args[0])
        elif cmd == "callers":
            get_callers(program, args[0])
        elif cmd == "info":
            for addr in args:
                func_info(program, addr)
                print()
        elif cmd == "strings":
            search_strings(program, args[0])
        elif cmd == "read-bytes":
            read_bytes(program, args[0], int(args[1]) if len(args) > 1 else 64)
        else:
            print(f"Unknown command: {cmd}")


if __name__ == "__main__":
    main()
