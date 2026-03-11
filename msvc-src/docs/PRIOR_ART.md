# Prior Art: Reverse Engineering MSVC

## Primary Sources

### Geoff Chappell — MSVC Studies (Gold Standard)
- **URL**: geoffchappell.com/studies/msvc/
- **Coverage**: VS .NET 2003 (v13.00), extensively documented
- **Key pages**:
  - Compiler modules pipeline: `/cl/modules.htm`
  - C2 code generator: `/cl/c2/index.htm`
  - C2 options: `/cl/c2/options/index.htm`
  - C1XX front-end: `/cl/c1xx/index.htm`
- **Relevance**: Architecture is similar to our v16.00; specific internals will differ

### Lectem — MSVC Hidden Flags (VS 2017)
- **URL**: lectem.github.io/msvc/reverse-engineering/build/2019/01/21/MSVC-hidden-flags.html
- **Method**: IDA Pro + OllyDbg to extract `/d1` and `/d2` flags
- **Key findings**: Flag names map internal subsystems (inlining, devirtualization, vectorization)
- **Relevance**: Direct map to c2.dll's internal pass structure

### Aras Pranckevičius — /d2cgsummary & /d1reportTime
- **URL**: aras-p.info/blog/2017/10/23/Best-unknown-MSVC-flag-d2cgsummary/
- **URL**: aras-p.info/blog/2019/01/21/Another-cool-MSVC-flag-d1reportTime/
- **Key findings**: Undocumented diagnostic flags that instrument compilation
- **Relevance**: Directly usable with our compiler version

### assarbad/msvc-undoc
- **GitHub**: github.com/assarbad/msvc-undoc
- **Content**: Structured YAML of undocumented MSVC + linker options
- **Relevance**: Reference for hidden flags in our version

### Microsoft/microsoft-pdb
- **GitHub**: github.com/microsoft/microsoft-pdb
- **Content**: Partial PDB implementation source
- **Relevance**: Internal data structures used by compiler + debugger

## Compiler Architecture References

### Microsoft C++ Team Blog Posts
- **SSA form**: Introduced VS 2015 Update 3; two passes (before/after loop opt)
- **Passes documented**: CSE (GVN), PRE, CFG opt, bit estimator, aliased SSA, loop unrolling/unswitching, SLP vectorization
- **Inlining**: General inliner with variable/memory estimation; `/Ob3`; partial through indirects
- **Register allocation**: No public details (but our empirical work maps it well)
- **IR**: Internal, not serializable — "MSVC cannot ingest a serialized form of the compiler's IR"

### Microsoft Phoenix (Defunct)
- **What**: Research compiler framework meant to replace c2.dll
- **Status**: Discontinued ~2008, April 2008 CTP SDK was last release
- **Architecture**: Modular IR-centric design, pluggable file readers/writers
- **Key fact**: Phoenix directly shared/used c2.dll's back-end for x86
- **Relevance**: If the SDK is obtainable, it contains interface definitions for c2

### Clang/C2 (VS 2015-2017)
- **What**: Microsoft's experiment wiring Clang front-end to c2.dll back-end
- **Status**: Discontinued
- **Key fact**: Proves c2.dll has a defined input interface (the IL format)
- **Relevance**: The interface was never documented publicly

## Xbox 360 PPC Compiler Lineage

```
VC++ 4.x (NT4 PPC, 1996)
    ↓
WinCE PPC cross-compilers (VC5/VC6)
    ↓
Xbox 360 XDK compiler (fork ~2003-2005)
    ↓
Our version: 16.00.11886.00 (build 78379, ~2010)
```

- The PPC back-end descends from the WinCE PPC line, NOT from the x86 mainline
- Xbox 360 uses WinCE-format exception tables (confirming the lineage)
- This explains some quirks: the PPC codegen may be less optimized than contemporary x86

## Related Projects

### XenonRecomp
- **What**: Static recompiler for Xbox 360 binaries → x64
- **Relevance**: Understands MSVC PPC codegen patterns (jump tables, save/restore, idioms)
- **GitHub**: github.com/hedge-dev/XenonRecomp

### decomp.me / decomp.dev
- **What**: Collaborative decompilation platforms
- **Relevance**: Many projects use MSVC for GameCube/Wii/Xbox targets; shared knowledge

### ReactOS / Wine
- **Approach**: Run actual MSVC binaries, not reimplementation
- **Relevance**: No one has attempted to reimplement cl.exe/c2.dll

## What Has Never Been Done

- No public project has decompiled ANY production compiler back-end (GCC, LLVM, MSVC)
- The closest is Geoff Chappell's documentation, which is analysis not decompilation
- Compiler internals are among the most complex software to reverse-engineer
- However: targeted subsystem RE (register allocator, specific passes) IS feasible

## Our Unique Advantages

1. **34,000+ input/output pairs** — massive test suite for the compiler
2. **Frozen target** — version 16.00.11886.00 will never change
3. **x86 binary** — Ghidra handles x86 PE32 well
4. **Rich diagnostics** — extensive internal logging strings
5. **Known pass names** — the optimization pipeline is labeled
6. **Empirical knowledge** — we already understand many codegen patterns from decomp work
