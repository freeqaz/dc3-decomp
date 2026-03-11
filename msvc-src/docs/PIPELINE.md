# c2.dll Compilation Pipeline — Traced from Binary

## Entry Point

```
InvokeCompilerPass (export, VA 0x10BEBFFD)
|   Converts char** args to wchar_t** via MultiByteToWideChar
|   Calls fcn.10b7f3e7 (the real entry)
|
fcn.10b7f3e7 (VA 0x10B7F3E7)
|   mov edx, [arg_8h]  ; argc (wchar)
|   mov ecx, [arg_4h]  ; argv (wchar)
|   jmp 0x10b7f3b6
|
Main Pipeline (VA 0x10B7F3B6)
├── call fcn.10b7e4f0       ; [1] INIT — initialize compiler state
├── call fcn.10b75957       ; [2] IL LOAD — read front-end IL file
├── if no fatal error:
│   ├── call fcn.10b7f369   ; [3] OPTIMIZE — run optimization passes
│   └── (fallthrough)
├── call fcn.10b7e616       ; [4] CODEGEN — code generation + emission
├── call fcn.10b7ee2b       ; [5] CLEANUP — finalize .obj
└── return [data.10c2ea8c]  ; error/status code
```

## [3] Optimizer — fcn.10b7f369 (VA 0x10B7F369)

```
fcn.10b7f369
└── call fcn.10b7f1ff       ; Per-function optimization loop
```

### Per-Function Loop (VA 0x10B7F1FF -> 0x10B7F15F)

Iterates a linked list of functions at `data.10c4630c`. For each function:

```
for (func = func_list; func != NULL; func = func->next_at_0x78) {
    set_current_function(func);          // data.10c40214 = func

    if (func->flags & 0x20 && !(func->flags & 2))
        continue;  // skip certain functions

    func->flags |= 2;                   // mark as processed

    call fcn.10b7f000(func)             // [3a] Function preparation
    call fcn.10b7e6af(func)             // [3b] * OPTIMIZATION PASSES
    call fcn.10bec297()                 // [3c] Post-optimization (codegen?)
    data.10c2e2ec = 0                   // reset flag
    call fcn.10bda2ac()                 // [3d] ???
    call fcn.10b7e1c4()                 // [3e] Cleanup
}
```

### [3b] Per-Function Passes — fcn.10b7e6af (VA 0x10B7E6AF)

This function dispatches the optimization passes for a single function.
Takes `this` in ECX (function descriptor at ESI).

```
fcn.10b7e6af(func):
    if (func->field_94 & 0x0C000000)    // certain function types skip all passes
        return;

    if (data.10c2e2fc) {                // global config flag
        call fcn.10b7dbf6()             // pre-pass A
        call fcn.10b7dc51(func)         // pre-pass B
    }

    call fcn.10b7dd2c(func)             // * Pass group 1
    call fcn.10b7ddff(func)             // * Pass group 2
    call fcn.10b7de4a(func)             // * Pass group 3

    if (data.10c2e2fc) {
        call fcn.10b7ded5(func)         // mid-pass (conditional)
    }

    call fcn.10b7df57(func)             // * Pass group 4
    call fcn.10b7e032(func)             // * Pass group 5

    if (data.10c6f1c8) {                // another config flag
        call 0x10b9c836(func)           // * Pass group 6 (VMX/vector?)
    }
```

**These 5-6 sub-functions are the actual optimization pass groups.**
Each likely dispatches a subset of the 35 named passes.

## [4] Code Generation — fcn.10b7e616 (VA 0x10B7E616)

```
fcn.10b7e616:
    if config flags set:
        call fcn.10b9c79a(0)            // VMX setup?

    data.10c37d2c = 4                   // set mode
    call fcn.10be7861()                 // * Main codegen pipeline
    call fcn.10b6fe01()                 // Additional codegen

    if (data.10c462c4) {                // PGO/profiling enabled
        call fcn.10c1bdc0()             // logging
        call fcn.10c20187(0x1b)         // ???
        call fcn.10c20187(0x1c)         // ???
    }
```

### fcn.10be7861 — Codegen Core (VA 0x10BE7861)

```
fcn.10be7861:
    call fcn.10b99093()                 // Codegen init
    call fcn.10be70db()                 // Instruction selection?

    if (!data.10c46308) {               // not in error state
        call [data.10c433c8]()          // * Function pointer — late pass A
    }
    call [data.10c433cc]()              // * Function pointer — late pass B
    jmp  fcn.10be717f                   // Final emission
```

Note: `data.10c433c8` and `data.10c433cc` contain function pointers that are initialized
at runtime (not statically — values in .data are 0x39E53934 / 0x3A943A88 which are
unresolved addresses, likely set during `InvokeCompilerPass` initialization).

## Key Data Addresses

| Address (VA)   | Name/Role                                           |
|----------------|-----------------------------------------------------|
| 0x10C2E980     | Optimizer config struct (contains pass name tables)  |
| 0x10C2E9E4     | Main pass name table (30 entries)                    |
| 0x10C2EA5C     | Null terminator (end of pass name table)             |
| 0x10C2EA60     | Post-table data (more config)                        |
| 0x10C2EA8C     | Error/status code (returned by pipeline)             |
| 0x10C2EB38     | Fatal error flag                                     |
| 0x10C2EC70     | Option string pointer                                |
| 0x10C4630C     | Function linked list head                            |
| 0x10C40214     | Current function being processed                     |
| 0x10C462C4     | PGO/profiling enabled flag                           |
| 0x10C46308     | Error state flag                                     |
| 0x10C6F1C8     | VMX/vector optimization level                        |
| 0x10C433C8     | Late-stage codegen function pointer A                 |
| 0x10C433CC     | Late-stage codegen function pointer B                 |
| 0x10C37D2C     | Codegen mode (set to 4 during emission)              |

## Source File References Found

| Address  | Source Path                                           |
|----------|------------------------------------------------------|
| 0x10B13828 | `e:\bt\278379\vctools\compiler\be\p2\misc.c`       |

Build number **278379** confirms this is from the same build tree.
Path structure: `vctools\compiler\be\p2\` — "be" = back-end, "p2" = phase 2 (c2).

## Next Steps

1. **Disassemble pass groups**: fcn.10b7dd2c through fcn.10b7e032 — map which
   named passes each group dispatches
2. **Find COLOR**: Should be in one of the pass groups. Look for references to
   the pass name table entry at index 14 (VA 0x10C2EA1C)
3. **Find G5_SPECIAL**: Pass table entry at index 19 (VA 0x10C2EA30)
4. **Trace function pointer init**: Find where data.10c433c8/cc get their values —
   these late-stage passes may include peephole optimization
5. **Map the function descriptor struct**: The linked list nodes at +0x78 with
   flags at +0x4C and field at +0x94
