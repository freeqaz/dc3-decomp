# Compiler Checkpoint/Snapshot Exploration

**Date**: 2026-03-04
**Status**: Research — multiple viable approaches identified

## The Core Problem

For the permuter, we compile the same TU 100+ times with tiny variations (reordering declarations, changing `!=` to `>`, etc.). The variations only affect one function — **99% of the compilation work is identical** across all variants.

## Measured Phase Breakdown

```
Full compile: 3.30s
├── Preprocessing (/P):       3.22s  (97.6%)    ← ALL filesystem I/O lives here
│   ├── Filesystem I/O:       1.47s  (44.5%)    ← 22K getdents + 108K stats
│   ├── Token scanning:       ~1.0s  (30%)
│   └── Macro expansion:      ~0.75s (23%)
├── Parsing + semantics:      ~0.05s (1.5%)
├── Optimization (O1):        ~0.02s (0.6%)
└── Codegen + .obj write:     ~0.01s (0.3%)
```

Preprocessing is **97.6%** of compile time. The actual code generation is ~80ms. This means any approach that can skip preprocessing gives us close to **3.5× speedup per compile** (3.3s → 0.94s measured).

## Key Measurement: Preprocess-Once Compile-Many

```
Preprocess to .i:   3.22s  (one-time cost)
Compile from .i:    0.94s  (per variant — no includes, no filesystem scanning)
Match%:             IDENTICAL to normal build (verified on 100% and partial functions)
```

The .i file is 68K lines (1.7MB), 70% header expansion, 30% source body. `#line` directives preserve `__FILE__` and line numbers, so `MakeString` template names match.

---

## Approach 1: Preprocess-Once, Splice-Many (MOST PRACTICAL)

### Concept

Preprocess the source file once to get a `.i` file. For each permuter variant, splice only the changed function body into the `.i` file and compile from that. Skip preprocessing entirely for variants 2-N.

```
Step 1 (once):    .cpp → /P → .i file (3.2s)
Step 2 (once):    Parse .i to find function boundaries (byte offsets)
Step 3 (N times): Splice variant function body into .i → compile (0.94s each)
```

### Implementation

```python
class PreprocessedScorer:
    def __init__(self, source_path, symbol):
        # 1. Preprocess once
        self.pp_output = preprocess(source_path)  # 3.2s

        # 2. Find function boundaries in .i
        # The permuter already knows the function's byte range in .cpp
        # Map .cpp line numbers → .i line numbers via #line directives
        self.func_start_line = find_function_start_in_pp(self.pp_output, symbol)
        self.func_end_line = find_function_end_in_pp(self.pp_output, symbol)

        # 3. Split .i into prefix/suffix (headers + other functions)
        self.prefix = self.pp_output[:self.func_start_offset]
        self.suffix = self.pp_output[self.func_end_offset:]

    def score(self, variant):
        # Extract the function body from variant source
        variant_func = extract_function_body(variant.source, self.func_name)

        # Splice into preprocessed template
        spliced_i = self.prefix + variant_func + self.suffix

        # Write to temp file and compile (0.94s vs 3.3s)
        write(temp_path, spliced_i)
        compile_from_i(temp_path, output_obj)
        return objdiff(output_obj)
```

### Complications

1. **Function body extraction from variants**: The permuter modifies the `.cpp` file, not the `.i` file. We need to re-preprocess the changed function body, or map the `.cpp` changes to `.i` changes. Since permuter variants only change declaration order / operator choices within one function, the preprocessed form of the function body is nearly identical — we could preprocess just the function body (fast, no headers).

2. **#line directives**: The spliced function body needs correct `#line` markers to preserve `__FILE__`/`__LINE__`. Since the variant only changes code within the function (same file, similar line numbers), this is manageable.

3. **Static locals**: If the function has `static Symbol foo("bar")`, the preprocessed form includes guard variables that depend on position. Reordering declarations might change guard variable indices. Need to verify this doesn't shift guard naming.

### Variant: Preprocess the Variant Function Body Only

Instead of splicing raw C++ into the `.i` file, preprocess just the changed function in isolation:

```python
# Extract variant's function body
variant_func_cpp = extract_function_from_cpp(variant.source)

# Preprocess just that (tiny file, fast — maybe 50ms)
variant_func_i = preprocess_fragment(variant_func_cpp)

# Splice preprocessed function into preprocessed template
spliced_i = self.prefix + variant_func_i + self.suffix
```

This avoids the `#line` mapping problem because both sides are preprocessed. The function fragment preprocessing would be very fast (~50ms) because it has no `#include` directives.

**Problem**: Preprocessor state (macros, typedefs) from headers wouldn't be available when preprocessing the fragment in isolation. Would need to extract the macro definitions from the `.i` and prepend them.

### Expected Impact

| Step | Current | With PP-once |
|------|---------|-------------|
| First variant | 3.3s | 3.2s (preprocess) + 0.94s (compile) = 4.14s |
| Variants 2-100 | 3.3s each = 327s | 0.94s each = 93s |
| **100 variants total** | **330s** | **97s (3.4× faster)** |
| With 8× parallel | 5.6s × 13 batches = 73s | 1.5s × 13 batches = 20s |

---

## Approach 2: CRIU Process Checkpoint/Restore

### Concept

Use [CRIU](https://criu.org/) (Checkpoint/Restore In Userspace) to snapshot the wibo+cl.exe process after it finishes preprocessing, then restore from snapshot for each variant.

```
Step 1: Start wibo+cl.exe, let it preprocess
Step 2: At the right moment, CRIU dumps process memory/state to disk
Step 3: For each variant:
    a. CRIU restore from dump
    b. Process continues from the checkpoint with variant's source
    c. Collect .obj output
```

### The "Right Moment" Problem

CRIU can checkpoint any running process, but we need to freeze cl.exe **after** preprocessing and **before** codegen. Options:

**A. Breakpoint on file write**: cl.exe writes the .obj file after all compilation phases. We could set a breakpoint on the `CreateFileA` call for the output .obj. But this is too late — we want to checkpoint BEFORE codegen, not after.

**B. FUSE-controlled source file**: Serve the source file via a FUSE filesystem that blocks on read until we signal. Flow:
1. Start cl.exe with source path pointing to FUSE mount
2. FUSE returns the header portion immediately
3. When cl.exe reads past the function body, FUSE blocks
4. CRIU checkpoints the process (preprocessor has finished headers, blocked on source read)
5. For each variant:
   a. CRIU restore
   b. FUSE unblocks and returns variant's function body
   c. cl.exe finishes compilation

**Problem**: MSVC doesn't read source files line-by-line — it reads the entire file upfront into memory. We can't selectively block part of the file.

**C. Pipe-based source**: Feed source via stdin or a named pipe. Block the pipe after sending the header includes. But MSVC cl.exe reads from files, not pipes.

**D. Timed checkpoint**: Profile the preprocessing phase, then checkpoint after a fixed delay. Unreliable — preprocessing time varies.

**E. Instrument wibo**: Add a checkpoint hook in wibo that fires when cl.exe closes all include files (end of preprocessing). Since wibo intercepts all Win32 API calls, it can detect when `CloseHandle` is called on the last include file and trigger `CRIU dump`.

### Implementation with Approach E

```cpp
// In wibo's CloseHandle implementation:
static int open_include_count = 0;

BOOL CloseHandle(HANDLE h) {
    if (is_include_file(h)) {
        open_include_count--;
        if (open_include_count == 0 && getenv("WIBO_CHECKPOINT_ON_PP_DONE")) {
            // Signal the CRIU controller to checkpoint us
            kill(getpid(), SIGSTOP);
        }
    }
    return real_CloseHandle(h);
}
```

Then the controller:
```bash
# Start cl.exe, it will SIGSTOP after preprocessing
wibo WIBO_CHECKPOINT_ON_PP_DONE=1 cl.exe /c source.cpp &
PID=$!
wait_for_stop $PID

# Checkpoint
criu dump -t $PID -D /tmp/checkpoint/ --shell-job

# For each variant:
#   1. Modify source file
#   2. Restore
criu restore -D /tmp/checkpoint/ --shell-job
```

### Problems

1. **CRIU restores to the same PID** — can't run multiple restores in parallel without PID namespace tricks
2. **File descriptors are restored** — cl.exe had the source file open with a specific seek position. We need the source file to contain the variant content at restore time. Since cl.exe reads the whole file upfront, the source content is already in cl.exe's memory buffer. We'd need to patch that buffer.
3. **Kernel support**: CRIU needs `CONFIG_CHECKPOINT_RESTORE=y`. Available on this system (CRIU 4.2 installed).
4. **CRIU overhead**: Dump is ~50-200ms, restore is ~50-100ms. For 100 variants that's 5-10s overhead.

### Verdict

CRIU is elegant in theory but the "right moment" problem and source-file-already-in-memory problem make it impractical for this use case. The preprocessed-file approach achieves the same speedup with 1/10th the complexity.

---

## Approach 3: Fork in Wibo (AMBITIOUS)

### Concept

Modify wibo to `fork()` at the preprocessing/compilation phase boundary. The parent keeps the preprocessed state; for each variant, fork a child that inherits the preprocessed memory and compiles with a different function body.

```
Parent process:
  1. Start cl.exe
  2. Let preprocessing complete
  3. Detect phase boundary (same as CRIU approach E)
  4. Loop:
     a. Receive variant source via pipe
     b. fork()
     c. Child: patch source buffer in memory, continue compilation
     d. Parent: wait for child, go to (a)
```

### The Source Buffer Patch Problem

After preprocessing, cl.exe has the entire source in memory as a token stream. To compile a different function body, we'd need to:
1. Find the token buffer in cl.exe's heap
2. Replace the function's tokens with the variant's tokens
3. Update any internal data structures (symbol tables, scope chains)

This requires reverse-engineering cl.exe's internal data structures — **infeasible** for a closed-source compiler.

### Alternative: Fork Before Source Read

If we could fork **before** cl.exe opens the source file but **after** it processes all `/I` paths and initializes its include search state:

```
1. Start cl.exe with a dummy source file (empty .cpp)
2. Let it initialize (process flags, set up include paths)
3. Fork
4. Each child: replace the source file, let cl.exe open and compile it
```

**Problem**: cl.exe's initialization is interleaved with source reading — it opens the source file as one of its first actions. There's no "after init, before source" boundary.

### Verdict

Fork-based approaches require either (a) reverse-engineering MSVC's internals to patch token buffers, or (b) a phase boundary that doesn't exist. Not practical.

---

## Approach 4: VM Snapshot (SLEDGEHAMMER)

### Concept

Run wibo+cl.exe inside a lightweight VM (Firecracker, QEMU microVM, or even a Linux container with CRIU). Snapshot the VM after preprocessing, restore for each variant.

### Implementation with Firecracker

```
1. Boot a Firecracker microVM with the source tree mounted
2. Start wibo+cl.exe inside the VM
3. At the preprocessing boundary, snapshot the VM
4. For each variant:
   a. Restore VM from snapshot
   b. Source file has been modified on the host (virtio-fs mount)
   c. VM continues compilation
   d. Collect .obj from virtio-fs
```

### Performance

- Firecracker snapshot: ~100ms
- Firecracker restore: ~50ms
- virtio-fs overhead: ~20% on I/O
- **Per-variant: ~1.1s** (0.94s compile + 0.05s restore + 0.1s overhead)

### Verdict

Same speedup as the preprocessed-file approach but with enormous infrastructure complexity. Only makes sense if we need sub-100ms restores (we don't).

---

## Approach 5: Wibo Compiler Daemon (CREATIVE)

### Concept

Modify wibo to keep cl.exe alive between compilations by hooking its `ExitProcess` call. When cl.exe tries to exit, wibo intercepts and restarts it from `main()` with new arguments.

```cpp
// In wibo's ExitProcess implementation:
void WINAPI ExitProcess(UINT exitCode) {
    if (getenv("WIBO_DAEMON_MODE")) {
        // Don't exit — signal completion and wait for next job
        write(daemon_pipe, &exitCode, sizeof(exitCode));

        // Read next job's arguments
        char* new_args = read_next_job(daemon_pipe);

        // Jump back to cl.exe's main()
        longjmp(cl_main_entry, 1);  // This is the wild part
    }
    _exit(exitCode);
}
```

### Problems

1. **cl.exe's static state**: Global variables, static locals, heap allocations from the previous compilation are still live. cl.exe doesn't clean up between runs (it's designed to run once).
2. **Memory leaks**: Each compilation leaks all memory from the previous one. After 100 runs, you've leaked 100× the working set.
3. **longjmp from ExitProcess**: Skips all destructors and cleanup. Stack may be corrupted.

### Variant: Process Pool

Instead of keeping one cl.exe alive, pre-fork N wibo+cl.exe processes that block on a pipe before opening the source file:

```python
class CompilerPool:
    def __init__(self, n_workers=8):
        self.workers = []
        for i in range(n_workers):
            # Start wibo with WIBO_WAIT_FOR_SOURCE=1
            # cl.exe starts, initializes, then blocks reading from a pipe
            proc = subprocess.Popen(
                ["wibo", "WIBO_WAIT_FOR_SOURCE=1", "cl.exe", "/c", ...],
                stdin=subprocess.PIPE, stdout=subprocess.PIPE
            )
            self.workers.append(proc)

    def compile(self, source_bytes):
        worker = self.get_idle_worker()
        worker.stdin.write(source_bytes)  # Unblock; cl.exe reads and compiles
        result = worker.stdout.read()     # Read .obj output
        return result
```

**Problem**: Same as fork approach — cl.exe opens the source file as one of its first actions. There's no "wait for source" point.

### Verdict

Daemon mode can't work with an unmodified cl.exe because it has no clean restart mechanism. The process pool variant fails because cl.exe has no "wait for input" mode.

---

## Approach 6: Preprocessor State Serialization (HYBRID)

### Concept

The preprocessor's output IS the serialized state — that's what the `.i` file is. But we need the macro definitions too, not just the expanded tokens.

MSVC's `/P` flag outputs the preprocessed source. We can also get macro definitions with `/d1PP` (undocumented, dumps preprocessor state):

```bash
cl.exe /P /d1PP source.cpp  # Outputs .i file with #define directives preserved
```

If this works, we could:
1. Preprocess once → get `.i` with full macro state
2. For each variant, prepend the macro state to the variant's function body
3. Compile the combined file (fast — no include scanning)

### Expected Impact

Same as Approach 1 (0.94s per variant) but solves the "fragment preprocessing needs header macros" problem.

---

## Recommended Path

### Tier 1: Do Now (immediate 3.4× speedup per variant)

**Preprocess-Once, Compile .i** (Approach 1, simplified version):
1. Before the scoring loop, preprocess the source: `cl.exe /P source.cpp → source.i`
2. For each variant, the permuter already modifies `source.cpp` — also modify `source.i` at the corresponding location
3. Compile from `source.i` instead of `source.cpp`: skip all include scanning

The "splice" step is simple for the permuter because:
- The permuter knows exactly which function it's modifying
- The function boundaries in `.i` can be found by `#line` directives matching the `.cpp` line numbers
- Declaration reordering within a function doesn't change `#line` markers

Implementation: ~4 hours. Modify `Scorer` to preprocess once, maintain a `.i` template, and splice variant function bodies.

### Tier 2: Do Soon (additional 1.5× from wibo caching)

**Directory + stat cache in wibo** (from WIBO_COMPILER_OPTIMIZATION.md):
- Reduces the 0.94s `.i`-compile time further (the `.i` file still triggers filesystem access for the output path and compiler DLLs)
- More importantly, improves parallel scaling beyond 8 workers

### Tier 3: Explore Later (diminishing returns)

- CRIU / VM snapshot: Only if the `.i` approach has unforeseen problems
- Compiler daemon: Only if someone reverse-engineers cl.exe's internals

---

## Projected Combined Impact

| Configuration | Per-variant | 100 variants | Notes |
|--------------|------------|-------------|-------|
| Current (sequential) | 3.3s | 330s | Baseline |
| + Preprocess-once | 0.94s | 97s | **3.4× faster** |
| + Wibo dir cache | 0.65s | 68s | **4.9× faster** |
| + 8× parallel | 0.65s/8 ≈ 0.12s amortized | ~12s | **27× faster** |
| + Score dedup (30% hit) | — | ~8s | **~40× faster** |

That turns a 5.5-minute scoring round into **8 seconds**.
