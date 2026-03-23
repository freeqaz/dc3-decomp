#!/usr/bin/env python3
"""Run setupCypher from the DC3 debug binary in Unicorn PPC emulator.

Loads the entire .text section from debug.xex into Unicorn, sets up the mogg
header data in memory, calls setupCypher, and dumps the derived AES key.
This gives us the GROUND TRUTH key that the PPC binary produces.

Usage:
    python3 scripts/analysis/mogg_unicorn_test.py
"""

import struct
import sys
import os
from pathlib import Path

# Setup Unicorn from local checkout
MILOHAX_DIR = Path(__file__).resolve().parent.parent.parent.parent
UNICORN_DIR = MILOHAX_DIR / "unicorn"
sys.path.insert(0, str(UNICORN_DIR / "bindings" / "python"))
os.environ["LIBUNICORN_PATH"] = str(UNICORN_DIR / "build")

from unicorn import Uc, UC_ARCH_PPC, UC_MODE_PPC32, UC_MODE_BIG_ENDIAN
from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_READ_UNMAPPED, UC_HOOK_MEM_WRITE_UNMAPPED
from unicorn.ppc_const import *

# ============================================================================
# Constants from the debug.xex symbol table
# ============================================================================

# Virtual addresses from dc_symbols.txt
SETUP_CYPHER     = 0x82330940  # VorbisReader::setupCypher — WRONG, this isn't setupCypher
# Actually setupCypher is not in the symbol list. Let me use the functions it calls:
GET_MASHER       = 0x823301A0
GET_KEY          = 0x82330940  # KeyChain::getKey
GRIND_ARRAY      = None  # Need to find ByteGrinder::GrindArray address

# The debug.xex has PE data at offset 0x3000. Base address = 0x82000000.
XEX_BASE = 0x82000000
XEX_PE_OFFSET = 0x3000

# We need to find setupCypher. It's part of VorbisReader which is in VorbisReader.cpp.
# Let's search for it in the symbol table.

def main():
    repo = Path(__file__).resolve().parent.parent.parent

    # Read symbols to find setupCypher
    symbols = {}
    sym_file = repo / "docs" / "dc_symbols.txt"
    with open(sym_file) as f:
        for line in f:
            line = line.strip()
            if ": " in line:
                addr_str, name = line.split(": ", 1)
                try:
                    addr = int(addr_str, 16)
                    symbols[name] = addr
                except ValueError:
                    pass

    # Find setupCypher
    for name, addr in symbols.items():
        if "setupCypher" in name:
            print(f"Found: {name} @ 0x{addr:08x}")

    # Find GrindArray
    for name, addr in symbols.items():
        if "GrindArray" in name:
            print(f"Found: {name} @ 0x{addr:08x}")

    # Find all ByteGrinder methods
    for name, addr in symbols.items():
        if "ByteGrinder" in name:
            print(f"Found: {name} @ 0x{addr:08x}")

    # Find all KeyChain methods
    for name, addr in symbols.items():
        if "KeyChain" in name:
            print(f"Found: {name} @ 0x{addr:08x}")

    # Find ctr_start, register_cipher, rijndael
    for name, addr in symbols.items():
        if any(k in name.lower() for k in ["ctr_start", "register_cipher", "rijndael"]):
            print(f"Found: {name} @ 0x{addr:08x}")

    print("\n--- Loading debug.xex ---")
    xex_path = repo / "orig-assets" / "debug.xex"
    with open(xex_path, "rb") as f:
        xex_data = f.read()

    # The PE image starts at XEX_PE_OFFSET. Virtual base = XEX_BASE.
    # File offset = PE_OFFSET + (virtual - base)
    image = xex_data[XEX_PE_OFFSET:]
    image_size = len(image)
    print(f"Image size: {image_size} bytes (0x{image_size:x})")

    # Read the mogg header data
    mogg_path = repo / "orig-assets" / "extracted" / "songs" / "boyfriend" / "boyfriend.mogg"
    with open(mogg_path, "rb") as f:
        mogg = f.read()

    hdr_size = struct.unpack_from("<I", mogg, 4)[0]
    print(f"Mogg header size: {hdr_size}")

    # Parse mogg header fields (same as CheckHmxHeader)
    pos = 8  # skip version + hdrSize
    ogg_ver = struct.unpack_from("<I", mogg, pos)[0]; pos += 4
    ogg_gran = struct.unpack_from("<I", mogg, pos)[0]; pos += 4
    ogg_count = struct.unpack_from("<I", mogg, pos)[0]; pos += 4
    pos += ogg_count * 8  # skip OggMap lookup

    nonce = mogg[pos:pos+16]; pos += 16
    magic_a = struct.unpack_from("<q", mogg, pos)[0]; pos += 8
    magic_b = struct.unpack_from("<q", mogg, pos)[0]; pos += 8
    stuff1 = mogg[pos:pos+16]; pos += 16
    stuff2 = mogg[pos:pos+16]; pos += 16
    key_idx_raw = struct.unpack_from("<q", mogg, pos)[0]; pos += 8
    key_idx = int(key_idx_raw) % 6 + 6

    print(f"Nonce: {nonce.hex()}")
    print(f"MagicA: {magic_a}")
    print(f"MagicB: {magic_b}")
    print(f"KeyIndex: {key_idx}")
    print(f"Stuff2 (for HvDecrypt): {stuff2.hex()}")

    # Now set up Unicorn to run the full image
    print("\n--- Setting up Unicorn ---")
    mu = Uc(UC_ARCH_PPC, UC_MODE_PPC32 + UC_MODE_BIG_ENDIAN)

    # Map the full image at the base address
    # Round up to 4KB page boundary
    map_size = (image_size + 0xFFF) & ~0xFFF
    mu.mem_map(XEX_BASE, map_size)
    mu.mem_write(XEX_BASE, image)
    print(f"Mapped {map_size} bytes at 0x{XEX_BASE:08x}")

    # Map stack
    STACK_BASE = 0x80000000
    STACK_SIZE = 0x100000  # 1MB
    mu.mem_map(STACK_BASE, STACK_SIZE)
    stack_top = STACK_BASE + STACK_SIZE - 0x100
    mu.reg_write(UC_PPC_REG_1, stack_top)  # r1 = SP

    # Map heap/data area for dynamic allocations
    HEAP_BASE = 0x90000000
    HEAP_SIZE = 0x1000000  # 16MB
    mu.mem_map(HEAP_BASE, HEAP_SIZE)

    # Enable FPU
    msr = mu.reg_read(UC_PPC_REG_MSR)
    mu.reg_write(UC_PPC_REG_MSR, msr | (1 << 13))

    # Set up a sentinel return address
    SENTINEL = 0x81000000
    mu.mem_map(SENTINEL & ~0xFFF, 0x1000)
    mu.reg_write(UC_PPC_REG_LR, SENTINEL)

    # Hook for debugging
    insn_count = [0]
    last_pc = [0]
    def hook_code(uc, address, size, user_data):
        if address == SENTINEL:
            uc.emu_stop()
            return
        insn_count[0] += 1
        last_pc[0] = address
        if insn_count[0] <= 20 or insn_count[0] % 1000 == 0:
            pass  # silent

    mu.hook_add(UC_HOOK_CODE, hook_code)

    # Unmapped memory handler
    def on_unmapped(uc, access, address, size, value, user_data):
        page = address & ~0xFFF
        try:
            uc.mem_map(page, 0x1000)
            return True
        except:
            print(f"  UNMAPPED: access={access} addr=0x{address:08x} size={size}")
            return False

    mu.hook_add(UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED, on_unmapped)

    # Now we need to call setupCypher. But setupCypher is a method on VorbisReader
    # and requires a 'this' pointer with member variables set up.
    # Instead, let's call the individual functions manually:

    # Step 1: getMasher
    print("\n--- Step 1: getMasher ---")
    masher_addr = HEAP_BASE  # 32 bytes for masher output
    mu.reg_write(UC_PPC_REG_3, masher_addr)  # r3 = output buffer
    mu.reg_write(UC_PPC_REG_LR, SENTINEL)

    try:
        insn_count[0] = 0
        mu.emu_start(GET_MASHER, SENTINEL, timeout=5_000_000)
        masher = mu.mem_read(masher_addr, 32)
        print(f"  Masher: {bytes(masher).hex()}")
        print(f"  Instructions executed: {insn_count[0]}")
    except Exception as e:
        print(f"  ERROR: {e}")
        print(f"  Last PC: 0x{last_pc[0]:08x} (offset 0x{last_pc[0]-XEX_BASE:x})")
        print(f"  Instructions executed: {insn_count[0]}")
        # Check what function the crash is in
        crash_addr = last_pc[0]
        closest = None
        for name, addr in symbols.items():
            if addr <= crash_addr:
                if closest is None or addr > closest[1]:
                    closest = (name, addr)
        if closest:
            print(f"  Crashed in: {closest[0]} @ 0x{closest[1]:08x} (+0x{crash_addr - closest[1]:x})")
        masher = mu.mem_read(masher_addr, 32)
        print(f"  Partial masher: {bytes(masher).hex()}")

    # Step 2: getKey
    print("\n--- Step 2: getKey ---")
    gkey_addr = HEAP_BASE + 0x100  # 16 bytes for key output
    masher_addr2 = HEAP_BASE  # reuse masher
    mu.reg_write(UC_PPC_REG_3, key_idx)         # r3 = key index
    mu.reg_write(UC_PPC_REG_4, gkey_addr)        # r4 = output key buffer
    mu.reg_write(UC_PPC_REG_5, masher_addr2)     # r5 = masher buffer
    mu.reg_write(UC_PPC_REG_1, stack_top)        # reset SP
    mu.reg_write(UC_PPC_REG_LR, SENTINEL)

    try:
        mu.emu_start(GET_KEY, SENTINEL, timeout=10_000_000)
        gkey = mu.mem_read(gkey_addr, 16)
        print(f"  gKey: {bytes(gkey).hex()}")
    except Exception as e:
        print(f"  ERROR: {e}")
        gkey = mu.mem_read(gkey_addr, 16)
        print(f"  Partial gKey: {bytes(gkey).hex()}")

    # Compare with our native output
    native_gkey = bytes.fromhex("df600ccc86924f5991ce094094404658")
    print(f"  Native gKey: {native_gkey.hex()}")
    print(f"  Match: {bytes(gkey) == native_gkey}")


if __name__ == "__main__":
    main()
