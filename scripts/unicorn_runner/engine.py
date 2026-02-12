"""Unicorn PPC32 execution engine for function comparison."""

import struct
import sys
import os

# Use local Unicorn checkout
UNICORN_PATH = "/home/free/code/milohax/unicorn/bindings/python"
sys.path.insert(0, UNICORN_PATH)
os.environ["LIBUNICORN_PATH"] = "/home/free/code/milohax/unicorn/build"

from unicorn import Uc, UC_ARCH_PPC, UC_MODE_PPC32, UC_MODE_BIG_ENDIAN
from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_READ_UNMAPPED, UC_HOOK_MEM_WRITE_UNMAPPED
from unicorn import UcError, UC_ERR_FETCH_UNMAPPED
from unicorn.ppc_const import *

from .memory_map import (
    STACK_BASE, OBJECT_BASE, GLOBAL_BASE, TRAMPOLINE_BASE, CODE_BASE,
    RDATA_BASE, VTABLE_BASE, SENTINEL_ADDR, REGION_SIZE, STACK_INIT,
    MSR_FP_BIT, TRAMPOLINE_STUB, VTABLE_SLOTS, VTABLE_TRAMP_OFFSET,
)


class ExecutionResult:
    """Result of executing a function in Unicorn."""

    def __init__(self, call_log, r3, f1, object_memory, globals_memory, error=None):
        self.call_log = call_log
        self.r3 = r3
        self.f1 = f1
        self.object_memory = object_memory
        self.globals_memory = globals_memory
        self.error = error


def execute_function(patched_code, trampolines, func_size, timeout=5_000_000,
                     verbose=False, rdata_bytes=None):
    """Execute a patched function in Unicorn and return the result.

    Args:
        patched_code: bytearray of patched function code
        trampolines: dict of symbol_name -> trampoline_addr
        func_size: size of the function in bytes
        timeout: execution timeout in microseconds
        verbose: print execution trace
        rdata_bytes: optional bytes to load at RDATA_BASE (switch table data)

    Returns:
        ExecutionResult with call log, return value, and memory snapshots
    """
    mu = Uc(UC_ARCH_PPC, UC_MODE_PPC32 + UC_MODE_BIG_ENDIAN)

    # Map all regions
    mu.mem_map(STACK_BASE, REGION_SIZE)
    mu.mem_map(OBJECT_BASE, REGION_SIZE)
    mu.mem_map(GLOBAL_BASE, REGION_SIZE)
    mu.mem_map(TRAMPOLINE_BASE, REGION_SIZE)
    mu.mem_map(CODE_BASE, REGION_SIZE)

    # Map rdata region for switch tables (if provided)
    if rdata_bytes is not None:
        mu.mem_map(RDATA_BASE, REGION_SIZE)
        mu.mem_write(RDATA_BASE, rdata_bytes)

    # Load patched function code
    mu.mem_write(CODE_BASE, bytes(patched_code))

    # Write trampoline stubs
    for addr in trampolines.values():
        mu.mem_write(addr, TRAMPOLINE_STUB)

    # Set up mock vtable for bctrl virtual dispatch:
    # OBJECT_BASE+0 → VTABLE_BASE (vtable pointer)
    # VTABLE_BASE[slot] → trampoline stub address
    # Each vtable slot points to a unique trampoline stub that returns 0.
    mu.mem_map(VTABLE_BASE, REGION_SIZE)
    vtable_data = bytearray(VTABLE_SLOTS * 4)
    trampoline_data = bytearray(VTABLE_SLOTS * 8)
    for slot in range(VTABLE_SLOTS):
        tramp_addr = TRAMPOLINE_BASE + VTABLE_TRAMP_OFFSET + (slot * 8)
        struct.pack_into(">I", vtable_data, slot * 4, tramp_addr)
        trampoline_data[slot * 8 : slot * 8 + 8] = TRAMPOLINE_STUB
    mu.mem_write(VTABLE_BASE, bytes(vtable_data))
    mu.mem_write(TRAMPOLINE_BASE + VTABLE_TRAMP_OFFSET, bytes(trampoline_data))
    mu.mem_write(OBJECT_BASE, struct.pack(">I", VTABLE_BASE))

    # Initialize registers
    mu.reg_write(UC_PPC_REG_1, STACK_INIT)        # SP
    mu.reg_write(UC_PPC_REG_2, 0)                  # r2 — unused (no TOC-relative addressing)
    mu.reg_write(UC_PPC_REG_3, OBJECT_BASE)        # this
    mu.reg_write(UC_PPC_REG_LR, SENTINEL_ADDR)     # return sentinel

    # Enable FP unit (required for any float instruction)
    msr = mu.reg_read(UC_PPC_REG_MSR)
    mu.reg_write(UC_PPC_REG_MSR, msr | MSR_FP_BIT)

    # Call logging
    call_log = []

    def hook_trampoline_call(uc, address, size, user_data):
        # Only log the first instruction of each stub (li r3, 0)
        # Each stub is 8 bytes, so we check alignment
        if (address - TRAMPOLINE_BASE) % 8 != 0:
            return

        lr = uc.reg_read(UC_PPC_REG_LR)
        source_offset = lr - CODE_BASE - 4

        entry = {
            "call_index": len(call_log),
            "args": {
                "r3": uc.reg_read(UC_PPC_REG_3),
                "r4": uc.reg_read(UC_PPC_REG_4),
                "r5": uc.reg_read(UC_PPC_REG_5),
                "r6": uc.reg_read(UC_PPC_REG_6),
            },
            "trampoline_addr": address,
            "source_offset": source_offset,
        }
        call_log.append(entry)

        if verbose:
            print(f"  Call #{entry['call_index']}: "
                  f"tramp=0x{address:08X} "
                  f"r3=0x{entry['args']['r3']:08X} "
                  f"r4=0x{entry['args']['r4']:08X} "
                  f"r5=0x{entry['args']['r5']:08X} "
                  f"r6=0x{entry['args']['r6']:08X} "
                  f"src_off=0x{source_offset:X}")

    mu.hook_add(UC_HOOK_CODE, hook_trampoline_call,
                begin=TRAMPOLINE_BASE, end=TRAMPOLINE_BASE + REGION_SIZE - 1)

    # Safety net: map-on-demand for unmapped memory accesses
    # In auto-fixture mode, functions may dereference zeroed pointers or
    # access memory outside our pre-mapped regions. We map new pages on
    # demand (zeroed) so execution can continue — both sides see the same
    # behavior since they start from identical state.
    mapped_pages = set()
    unmapped_accesses = []

    def hook_unmapped_access(uc, access, address, size, value, user_data):
        page_base = address & ~0xFFF  # 4KB page alignment
        if page_base not in mapped_pages:
            try:
                uc.mem_map(page_base, 0x1000)
                mapped_pages.add(page_base)
            except Exception:
                return False  # Can't map — stop emulation
        unmapped_accesses.append(address)
        if verbose:
            access_type = "READ" if access in (16, 17) else "WRITE"
            print(f"  UNMAPPED {access_type}: addr=0x{address:08X} size={size} (mapped page 0x{page_base:08X})")
        return True  # Continue execution

    mu.hook_add(UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED,
                hook_unmapped_access)

    # Execute
    error = None
    try:
        mu.emu_start(CODE_BASE, CODE_BASE + func_size, timeout=timeout)
    except UcError as e:
        if e.errno == UC_ERR_FETCH_UNMAPPED:
            pc = mu.reg_read(UC_PPC_REG_PC)
            if pc == SENTINEL_ADDR:
                pass  # Normal return
            else:
                error = f"Unexpected fetch from unmapped 0x{pc:08X}"
        else:
            error = str(e)

    # Note: unmapped accesses are handled by map-on-demand, not treated as errors

    # Capture output state
    r3 = mu.reg_read(UC_PPC_REG_3)
    f1 = mu.reg_read(UC_PPC_REG_FPR0 + 1)
    object_memory = bytes(mu.mem_read(OBJECT_BASE, REGION_SIZE))
    globals_memory = bytes(mu.mem_read(GLOBAL_BASE, REGION_SIZE))

    return ExecutionResult(
        call_log=call_log,
        r3=r3,
        f1=f1,
        object_memory=object_memory,
        globals_memory=globals_memory,
        error=error,
    )
