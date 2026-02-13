# decomp-permuter (ARCHIVED)

> **This tool is not compatible with DC3.** Use the [C++ Permuter](../permuter/INDEX.md) instead.

decomp-permuter automatically permutes C code to find variations that better match a target binary.

**Repository:** `~/code/milohax/decomp-permuter`
**Upstream:** https://github.com/simonlindholm/decomp-permuter

## Why This Doesn't Work for DC3

1. **C++ not supported** - Uses pycparser which only parses C. DC3 is entirely C++.
2. **COFF object format** - Xbox 360 MSVC produces COFF files; permuter expects ELF.
3. **MSVC build syntax** - Uses `/Fo` instead of `-o` flags that import.py expects.

## Use the C++ Permuter Instead

```bash
python -m scripts.permuter \
    --symbol "?BurnXfm@RndMesh@@QAAXXZ" \
    --source src/system/rndobj/Mesh.cpp \
    --function "RndMesh::BurnXfm" \
    --dry-run
```

See [C++ Permuter documentation](../permuter/INDEX.md) for full usage.

---

## Legacy Documentation (for reference only)

## Installation

```bash
cd ~/code/milohax/decomp-permuter

# Install Python dependencies (pycparser <3.0 required due to API changes)
python3 -m pip install 'pycparser<3.0' toml

# Optional: faster string matching
python3 -m pip install Levenshtein

# Test installation
python3 permuter.py --help
```

## Quick Start

The permuter works by creating variations of your C code and compiling each to find which produces assembly closer to the target.

### 1. Import a Function

```bash
cd ~/code/milohax/dc3-decomp

# Import a function for permutation
# Syntax: import.py <source.cpp> <function_name>
python3 ~/code/milohax/decomp-permuter/import.py \
    src/system/math/Interp.cpp \
    "Interp::SetSlew"
```

This creates a directory `nonmatch/Interp__SetSlew/` containing:
- `base.c` - Preprocessed source with the target function
- `target.s` - Target assembly to match
- `target.o` - Assembled target object
- `compile.sh` - Script to compile the C file
- `settings.toml` - Per-function settings

### 2. Run the Permuter

```bash
# Run with multiple threads
python3 ~/code/milohax/decomp-permuter/permuter.py nonmatch/Interp__SetSlew/ -j4

# Show more candidates
python3 ~/code/milohax/decomp-permuter/permuter.py nonmatch/Interp__SetSlew/ -j4 --show 10
```

### 3. Interpret Results

The permuter outputs candidates with scores. Lower is better, 0 means perfect match.

```
Score: 45 (previous best)
Score: 42 (new best!)
  int temp_r3 = foo;  // <- suggested change
```

Apply promising changes to your source file and rebuild to verify.

## PERM Macros

For more control, add PERM macros to test specific variations:

### PERM_GENERAL - Try Alternatives

```c
// Try different expressions
int x = PERM_GENERAL(a + b, b + a, (a) + (b));

// Try different statement forms
PERM_GENERAL(
    result = cond ? a : b;,
    if (cond) result = a; else result = b;
)
```

### PERM_VAR - Meta Variables

```c
// Define alternatives for reuse
PERM_VAR(type, int)
PERM_VAR(type, unsigned int)

PERM_VAR(type) x = 0;
PERM_VAR(type) y = 1;
```

### PERM_RANDOMIZE - Enable Random Permutation

When using manual PERM macros, random permutation is disabled by default. Re-enable it for specific regions:

```c
int foo(int a) {
    PERM_RANDOMIZE(
        int temp = a * 2;
        return temp + 1;
    )
}
```

### PERM_LINESWAP - Reorder Statements

```c
PERM_LINESWAP(
    x = 1;
    y = 2;
    z = 3;
)
```

### Other Macros

| Macro | Description |
|-------|-------------|
| `PERM_INT(lo, hi)` | Integer in range [lo, hi] |
| `PERM_ONCE([key,] code)` | Include code exactly once across multiple uses |
| `PERM_IGNORE(code)` | Skip parsing (for asm blocks, etc.) |
| `PERM_PRETEND(code)` | Parse but remove from output |
| `PERM_FORCE_SAMELINE(code)` | Force statements on same line |

## DC3 Workflow

### Finding Functions to Permute

```bash
# Find near-match functions (good permuter candidates)
objdiff-cli report query build/373307D9/report.json \
    --functions --min-percent 90 --max-percent 99 --limit 10
```

### Full Example

```bash
# 1. Check function status
objdiff-cli report function build/373307D9/report.json "CharClip::AllocSize"

# 2. Import for permutation
python3 ~/code/milohax/decomp-permuter/import.py \
    src/system/char/CharClip.cpp \
    "CharClip::AllocSize"

# 3. Optional: Add PERM macros to nonmatch/CharClip__AllocSize/base.c
# Edit the file to add variations you want to test

# 4. Run permuter
python3 ~/code/milohax/decomp-permuter/permuter.py \
    nonmatch/CharClip__AllocSize/ -j4 --show 5

# 5. Apply best changes to src/system/char/CharClip.cpp

# 6. Rebuild and verify
ninja build/373307D9/src/system/char/CharClip.obj
objdiff-cli diff -p . "CharClip::AllocSize"
```

## Command Line Options

```bash
python3 ~/code/milohax/decomp-permuter/permuter.py <dir> [options]

Options:
  -j N              Use N parallel threads (recommended)
  --show N          Show top N candidates (default: 1)
  --seed N          Random seed for reproducibility
  --stack-diffs     Include stack position differences in scoring
  --debug           Print debug information
  --stop-on-zero    Stop when a perfect match is found
```

## Tips

### When to Use the Permuter

- **Good for:** Register allocation issues, minor instruction reordering, expression variations
- **Less effective:** Large structural differences, wrong control flow, missing functionality

### Best Practices

1. **Start with a close match** - The permuter works best when you're already 90%+ matching
2. **Use PERM macros sparingly** - Too many combinations explode exponentially
3. **Check the suggested changes** - Not all improvements are semantically correct
4. **Clean up after** - Remove the `nonmatch/` directory when done

### Troubleshooting

**"Can't find root dir"**
- Ensure `permuter_settings.toml` exists in project root
- Ensure `build.ninja` exists

**Import fails**
- Run `ninja` first to ensure build system is set up
- Check that the source file and function exist

**No improvements found**
- Try adding manual PERM macros for specific variations
- The function may need structural changes the permuter can't find

## Project Configuration

DC3 uses `permuter_settings.toml` at project root:

```toml
build_system = "ninja"
compiler_type = "base"
objdump_command = "build/binutils/powerpc-eabi-objdump"
```

## References

- [decomp-permuter README](https://github.com/simonlindholm/decomp-permuter/blob/main/README.md)
- [objdiff CLI Usage](../OBJDIFF_CLI_USAGE.md)
- [m2c Documentation](m2c.md)
