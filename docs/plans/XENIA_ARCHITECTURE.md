# Xenia Architecture Deep Dive

Reference documentation for the Xenia Xbox 360 emulator internals, written in the context of the DC3 decomp project's runtime validation work.

**Xenia source**: `~/code/milohax/xenia/` (fork of `xenia-project/xenia`)

---

## Stock Xenia vs Our Custom Modifications

This doc covers both upstream Xenia architecture and our project-specific additions. It's important to distinguish between them since some features described here don't exist in stock Xenia.

### What's Stock Xenia (upstream `xenia-project/xenia`)
- All core emulator subsystems (Memory, Processor, GraphicsSystem, AudioSystem, KernelState, VFS)
- The Vulkan backend (`src/xenia/gpu/vulkan/`) — cross-platform, works on Linux and Windows
- Command processor, PM4 packet handling, ring buffer
- VdSwap, VBlank, interrupt dispatch, CPU-GPU synchronization
- XEX loading, import resolution, PE section mapping
- Linux is an officially supported platform — Vulkan is the **only** GPU backend on Linux (D3D12 is Windows-only)
- Linux build uses premake5 + gmake2, requires `libvulkan-dev`, `libx11-dev`, `libx11-xcb-dev`, GTK3

### What's Our Custom Work (in our fork)
- **x64 JIT System V ABI fix** (`x64_backend.cc`) — stock Xenia's JIT thunks only implemented the Windows x64 calling convention. We added `#if XE_PLATFORM_LINUX` guards to `EmitHostToGuestThunk`, `EmitGuestToHostThunk`, and `EmitResolveFunctionThunk` for System V ABI. **Without this, the JIT crashes on Linux.**
- **Headless mode** (`xenia_headless_main.cc`, `emulator_headless.cc`) — new `xenia-headless` binary that runs without a window, using null/nop backends or Vulkan without a presenter. Stock Xenia has no headless mode.
- **Async pipeline compilation** (`vulkan_pipeline_cache.h/.cc`) — `SetHeadlessMode()` offloads `vkCreateGraphicsPipelines()` to background threads, preventing CP deadlocks. Stock Xenia compiles pipelines synchronously on the CP thread.
- **Frame capture** (`vulkan_command_processor.cc`) — `--dump_frames_path` PPM dump, warmup strategy (N-2/N-1/N), `--force_all_draws`, `--headless_capture_interval` cvars
- **PE override** (`xex_module.cc`) — `--pe_override` flag to swap decomp PE sections into XEX-loaded memory
- **Non-blocking BeginSubmission** (`vulkan_command_processor.cc`) — headless fence check passes `0` to `AwaitSubmissionAndUpdateCompleted` instead of blocking
- **XAudio2 dummy driver** (`xboxkrnl_audio.cc`) — dummy handle + incrementing tic for nop audio
- **Async I/O fix** (`xboxkrnl_io.cc`) — removed `STATUS_PENDING` for synchronous reads
- **Memory aliasing fix** (`memory_posix.cc`) — `MAP_SHARED|MAP_FIXED` for `MapFileView`; `mprotect` for `AllocFixed`

### Vulkan on Linux — Current Reality

The Vulkan backend is cross-platform and well-supported in stock Xenia. However, **stock Xenia's x64 JIT is broken on Linux** — the calling convention thunks only handle Windows x64 ABI. Our fork fixes this, making the full emulator functional on Linux.

The `xenia-headless` binary and all headless-related Vulkan features (draw skipping, async pipelines, frame capture, non-blocking fences) are entirely our creation and don't exist upstream.

---

## Table of Contents

1. [High-Level Architecture](#high-level-architecture)
2. [Thread Architecture](#thread-architecture)
3. [Memory Model](#memory-model)
4. [GPU Emulation Pipeline](#gpu-emulation-pipeline)
5. [Command Processor](#command-processor)
6. [Vulkan Backend](#vulkan-backend)
7. [Swap / Present Pipeline](#swap--present-pipeline)
8. [CPU-GPU Synchronization](#cpu-gpu-synchronization)
9. [XEX Loading and PE Override](#xex-loading-and-pe-override)
10. [Headless Mode](#headless-mode)
11. [Key Problems and Blockers](#key-problems-and-blockers)

---

## High-Level Architecture

Xenia emulates the Xbox 360 as a set of cooperating subsystems, each owned by the `Emulator` class (`src/xenia/emulator.h`):

```
Emulator
├── Memory              — Guest RAM (512MB virtual + 512MB physical)
├── cpu::Processor      — PPC → x64 JIT compiler + execution engine
├── gpu::GraphicsSystem — GPU emulation (Xenos → Vulkan/D3D12/null)
├── apu::AudioSystem    — Audio hardware (XMA decoder + mixing)
├── hid::InputSystem    — Controller/HID emulation
├── kernel::KernelState — Xbox 360 kernel (threads, objects, syscalls)
├── vfs::VirtualFileSystem — File system (disc, STFS, folders)
└── cpu::ExportResolver — Import/export thunk resolution
```

The `Emulator::Setup()` method wires everything together. Factory lambdas create the GPU, APU, and HID backends, allowing different implementations (Vulkan vs D3D12, nop audio, etc.).

**Key source files:**
- `src/xenia/emulator.h` / `.cc` — Top-level orchestration
- `src/xenia/memory.h` / `.cc` — Guest memory management
- `src/xenia/kernel/kernel_state.h` / `.cc` — Kernel object tracking

---

## Thread Architecture

Xenia creates both **host threads** (C++ threads running on the host CPU) and **guest threads** (emulated Xbox 360 PPC threads that execute via JIT on host threads). The distinction matters because guest threads have associated PPC register state and run JIT-compiled code.

### System Threads (Host)

These are created by Xenia's subsystems during initialization:

| Thread | Created By | Purpose | Source |
|--------|-----------|---------|--------|
| **GPU Commands** | `CommandProcessor::Initialize()` | Processes GPU ring buffer PM4 packets | `command_processor.cc:79-86` |
| **GPU VSync** | `GraphicsSystem::Setup()` | Fires VBlank interrupts at 60Hz | `graphics_system.cc:106-125` |
| **XMA Decoder** | `AudioSystem` | Decodes XMA audio packets | `apu/` |
| **Audio Worker** | `AudioSystem` | Mixes and submits audio | `apu/` |

Both GPU threads are `XHostThread` instances — they run host code but have associated guest thread state so they can execute guest interrupt callbacks.

### Guest Threads

Created by the game via `ExCreateThread` / `KeInitThread`. Each `XThread` (`src/xenia/kernel/xthread.h`) wraps:

- A host `threading::Thread` — the actual OS thread
- A `cpu::ThreadState` — PPC register context (GPRs, FPRs, CR, LR, CTR, XER)
- Guest stack memory — allocated in guest address space
- A `X_KTHREAD` structure in guest memory — the kernel thread object visible to guest code

**Thread lifecycle:**
1. `XThread::Create()` — allocates guest stack, creates `X_KTHREAD` in guest memory, spawns host thread
2. Host thread enters `XThread::Execute()` — sets up PPC context, calls `processor_->Execute(thread_state_, start_address)`
3. JIT compiles PPC code on-demand, executes on host thread
4. Guest syscalls trap into host code via import thunks (`sc` instruction → thunk handler)

**For DC3:** The game spawns 16 threads total: main thread, D3D worker threads, audio threads, loading threads. The main thread handle is `F8000028`.

### CPU-to-Thread Affinity

The Xbox 360 has 3 physical cores × 2 hardware threads = 6 logical CPUs. Xenia's `XThread` tracks a `current_cpu` field but doesn't enforce host CPU affinity — all guest threads run on whatever host cores the OS schedules.

The `X_KPCR` (Processor Control Region) struct at a per-CPU address provides `current_thread`, `stack_base_ptr`, `stack_end_ptr`, and `dpc_active` to guest code.

---

## Memory Model

### Address Space Layout

The Xbox 360 has a 32-bit virtual address space. Xenia maps this to host memory:

```
Guest Address Space (32-bit)
┌──────────────────────────────────────┐
│ 0x00000000 - 0x3FFFFFFF              │  Virtual memory (user space)
│   0x00010000: typical PE base        │
│   0x7FC80000: GPU register MMIO      │  (mapped via AddVirtualMappedRange)
├──────────────────────────────────────┤
│ 0x40000000 - 0x7EFFFFFF              │  Virtual memory (continued)
├──────────────────────────────────────┤
│ 0x80000000 - 0x8BFFFFFF  (192MB)     │  XEX virtual memory, 64KB pages
│   0x82330000: DC3 .text section      │  (PE loaded here by XEX loader)
│ 0x8C000000 - 0x8FFFFFFF  (64MB)      │  XEX virtual memory, 64KB pages (encrypted)
│ 0x90000000 - 0x9FFFFFFF  (256MB)     │  XEX virtual memory, 4KB pages
├──────────────────────────────────────┤
│ 0xA0000000 - 0xBFFFFFFF  (512MB)     │  Physical memory, 64KB pages
│ 0xC0000000 - 0xDFFFFFFF  (512MB)     │  Physical memory, 16MB pages
│ 0xE0000000 - 0xFFFFFFFF  (512MB)     │  Physical memory, 4KB pages (+0x1000 offset)
└──────────────────────────────────────┘
```

**Important:** `0x80000000–0x9FFFFFFF` is XEX virtual memory (`kGuestXex` heap type), NOT physical aliasing. Only `0xA0000000`, `0xC0000000`, and `0xE0000000` are physical memory mappings. The `0xE0` range has a 4KB offset from the `0xA0`/`0xC0` ranges (emulated via `host_address_offset` on the CPU side).

DC3's PE is loaded at `0x82330000` — this is in the XEX virtual range, mapped by the XEX loader during module load. The `TranslatePhysical()` mask `0x1FFFFFFF` strips the upper 3 bits to get a physical offset (e.g., `0xA0123456 & 0x1FFFFFFF = 0x00123456`).

**Host representation:**
- `virtual_membase_` (~`0x100000000` on host) — guest virtual address → `virtual_membase_ + guest_addr`
- `physical_membase_` (~`0x200000000` on host) — guest physical address → `physical_membase_ + (guest_addr & 0x1FFFFFFF)`

The physical memory mappings at `0xA0000000`, `0xC0000000`, and `0xE0000000` all access the same 512MB physical RAM with different page sizes (64KB, 16MB, 4KB respectively). On real hardware these have different caching policies. The `0x80000000–0x9FFFFFFF` range is separate — it's XEX virtual memory where executable code and data are loaded.

### GPU Register MMIO

GPU registers are memory-mapped at `0x7FC80000` (16-bit register space, 4 bytes each). Xenia intercepts reads/writes via MMIO callbacks:

- **Write to `CP_RB_WPTR` (0x7FC801C5×4)** → `CommandProcessor::UpdateWritePointer()` — the game telling the GPU "I wrote new commands up to this index"
- **Read from `R500_D1MODE_V_COUNTER`** → returns VBlank counter
- **Read from `RB_EDRAM_TIMING`** → returns hardcoded timing value

Source: `graphics_system.cc:190-232`

---

## GPU Emulation Pipeline

### Overview: Xbox 360 Xenos GPU

The Xenos is an ATI/AMD GPU with a unified shader architecture. Key concepts:

- **EDRAM** — 10MB embedded memory used as render targets (color + depth). Not directly accessible as textures; must be "resolved" (copied) to main memory.
- **Ring Buffer** — Circular buffer in physical memory where the CPU writes PM4 command packets for the GPU to consume.
- **PM4 Packets** — GPU command protocol inherited from ATI R5xx/R6xx. Types include register writes, draws, interrupts, memory writes.
- **Fetch Constants** — 32 texture fetch descriptors + vertex fetch descriptors stored in GPU registers. Define how textures/vertices are sampled.
- **Shaders** — Unified shader ISA. Vertex and pixel shaders use the same instruction set.

### Pipeline Flow

```
┌─────────────────────────────────────────────────────────────────────┐
│  GAME CODE (PPC, runs on CPU JIT)                                   │
│                                                                     │
│  1. VdInitializeEngines() — init GPU hardware                       │
│  2. VdSetGraphicsInterruptCallback(cb, data) — register ISR         │
│  3. VdInitializeRingBuffer(phys_ptr, size_log2) — set up ring buf   │
│  4. VdEnableRingBufferRPtrWriteBack(ptr, block_size) — writeback    │
│  5. Game writes PM4 packets directly to ring buffer memory          │
│  6. Game writes CP_RB_WPTR register (MMIO at 0x7FC801C5*4)          │
│                                                     │               │
└─────────────────────────────────────────────────────│───────────────┘
                                                      │
                                    GPU MMIO write triggers
                                    UpdateWritePointer()
                                                      │
                                                      ▼
┌─────────────────────────────────────────────────────────────────────┐
│  COMMAND PROCESSOR (runs on "GPU Commands" host thread)             │
│                                                                     │
│  WorkerThreadMain() loop:                                           │
│    while (worker_running_) {                                        │
│      spin/wait until write_ptr != read_ptr                          │
│      ExecutePrimaryBuffer(read_ptr, write_ptr)                      │
│        → ExecutePacket() per PM4 packet                             │
│        → dispatches to:                                             │
│           Type 0: Register writes                                   │
│           Type 3: Commands (DRAW_INDX, XE_SWAP, INTERRUPT, etc.)    │
│      write back read_ptr to guest memory                            │
│    }                                                                │
│                                                                     │
│  Key commands:                                                      │
│    PM4_DRAW_INDX    → IssueDraw() or IssueCopy()                    │
│    PM4_XE_SWAP      → IssueSwap() [Xenia custom, from VdSwap]      │
│    PM4_INTERRUPT    → DispatchInterruptCallback()                   │
│    PM4_EVENT_WRITE_SHD → write value to guest memory (GPU→CPU sync) │
│    PM4_WAIT_REG_MEM → spin until guest memory/register matches      │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
                              │
                    ┌─────────┼──────────┐
                    ▼         ▼          ▼
              Vulkan       D3D12       Null
              Backend      Backend     Backend
```

---

## Command Processor

**Source:** `src/xenia/gpu/command_processor.h` / `.cc`

The `CommandProcessor` base class is the heart of GPU emulation. It:

1. Runs on a dedicated host thread ("GPU Commands")
2. Consumes PM4 packets from the ring buffer
3. Dispatches to virtual methods (`IssueDraw`, `IssueCopy`, `IssueSwap`) implemented by backends

### Worker Thread Main Loop

```cpp
// command_processor.cc:202-259
void CommandProcessor::WorkerThreadMain() {
    SetupContext();  // Backend-specific init (Vulkan device, etc.)

    while (worker_running_) {
        // Process any queued host-side callbacks
        while (!pending_fns_.empty()) { ... }

        // Spin-wait for new commands from the game
        uint32_t write_ptr = write_ptr_index_.load();
        if (read_ptr_index_ == write_ptr) {
            // Spin up to 500 iterations, then wait on event (5ms timeout)
            PrepareForWait();  // Backend: submit pending Vulkan work
            do { MaybeYield(); } while (no new commands);
            ReturnFromWait();
        }

        // Execute all available packets
        read_ptr_index_ = ExecutePrimaryBuffer(read_ptr_index_, write_ptr);

        // Write back read pointer to guest memory
        // (tells the game how far the GPU has consumed)
        store_and_swap(read_ptr_writeback_ptr_, read_ptr_index_);
    }
}
```

### Ring Buffer Setup

The game calls kernel exports to configure the ring buffer:

1. `VdInitializeRingBuffer(phys_ptr, size_log2)` — sets `primary_buffer_ptr_` and `primary_buffer_size_`
2. `VdEnableRingBufferRPtrWriteBack(ptr, block_size_log2)` — enables GPU→CPU read pointer writeback
3. Game writes `CP_RB_WPTR` register via MMIO → triggers `UpdateWritePointer()` → signals `write_ptr_index_event_`

### Key PM4 Packet Types

| Packet | Handler | Purpose |
|--------|---------|---------|
| Type 0 | `ExecutePacketType0` | Direct GPU register write(s) |
| `PM4_NOP` | `ExecutePacketType3_NOP` | No-op padding |
| `PM4_INTERRUPT` | `ExecutePacketType3_INTERRUPT` | Generate interrupt to guest CPU |
| `PM4_XE_SWAP` | `ExecutePacketType3_XE_SWAP` | Xenia-specific swap/present |
| `PM4_DRAW_INDX` | `ExecutePacketType3_DRAW_INDX` → `ExecutePacketType3Draw` | Draw primitives |
| `PM4_EVENT_WRITE_SHD` | `ExecutePacketType3_EVENT_WRITE_SHD` | Write value to guest memory (fence) |
| `PM4_WAIT_REG_MEM` | `ExecutePacketType3_WAIT_REG_MEM` | Spin until memory/register condition met |
| `PM4_INDIRECT_BUFFER` | `ExecutePacketType3_INDIRECT_BUFFER` | Execute commands from another buffer |
| `PM4_SET_CONSTANT` | `ExecutePacketType3_SET_CONSTANT` | Set GPU register constants |
| `PM4_IM_LOAD` | `ExecutePacketType3_IM_LOAD` | Load shader microcode |

### Draw Dispatch

`ExecutePacketType3Draw()` in the base class reads draw parameters and unconditionally calls `IssueDraw()`. The copy/draw distinction happens inside each backend's `IssueDraw()` implementation:

In `VulkanCommandProcessor::IssueDraw()` (`vulkan_command_processor.cc:2453`):
1. Checks `RB_MODECONTROL.edram_mode`:
   - If `kCopy` → calls `IssueCopy()` (EDRAM resolve to main memory)
   - Otherwise → proceeds with actual rendering
2. Headless mode can skip non-copy draws (see [Headless Mode](#headless-mode))

This copy vs. non-copy distinction is **critical for headless mode**.

---

## Vulkan Backend

**Source:** `src/xenia/gpu/vulkan/`

### Class Hierarchy

```
CommandProcessor (base)
└── VulkanCommandProcessor
    ├── VulkanSharedMemory        — Guest memory → Vulkan buffer mirroring
    ├── VulkanTextureCache         — Texture fetch → VkImage management
    ├── VulkanRenderTargetCache    — EDRAM render targets → VkImage
    ├── VulkanPipelineCache        — Shader + state → VkPipeline
    │   └── SpirvShaderTranslator  — Xenos shader → SPIR-V (owned by pipeline cache)
    ├── VulkanPrimitiveProcessor   — Index/vertex buffer processing
    └── DeferredCommandBuffer      — Batched Vulkan command recording

GraphicsSystem (base)
└── VulkanGraphicsSystem
    └── creates VulkanCommandProcessor
    └── owns VulkanProvider (Vulkan instance/device)
```

### Submission Model

The Vulkan CP uses a **deferred command buffer** pattern:

1. Commands are recorded into `DeferredCommandBuffer` (a CPU-side command list)
2. At submission time, they're replayed into a real `VkCommandBuffer`
3. Submissions are tracked via `VulkanGPUCompletionTimeline` (monotonic submission IDs)
4. Fences track GPU completion of each submission

```
BeginSubmission()
  → check fences / completion timeline
  → reset deferred_command_buffer_

[... record draws, copies, barriers into deferred buffer ...]

EndSubmission()
  → allocate VkCommandBuffer from pool
  → deferred_command_buffer_.Execute(command_buffer) — replay into real VkCommandBuffer
  → vkEndCommandBuffer
  → vkQueueSubmit with fence (via completion_timeline_)
  → advance completion timeline
```

### IssueDraw Flow (Vulkan)

```
VulkanCommandProcessor::IssueDraw()
  1. Check edram_mode → if kCopy, call IssueCopy() instead
  2. [Headless check: skip if not render frame]
  3. Analyze vertex + pixel shaders (translate Xenos → SPIR-V)
  4. Update shared memory (sync guest→GPU)
  5. Resolve render target bindings
  6. Look up or create VkPipeline
  7. Bind descriptor sets (textures, samplers, constants)
  8. Record draw command into deferred command buffer
```

### IssueCopy Flow (EDRAM Resolve)

`IssueCopy()` handles the Xbox 360 "resolve" operation — copying EDRAM render target data to a main memory texture. This is how the game gets rendered content out of EDRAM (which is not directly addressable as a texture).

On Xenia Vulkan, this translates to render target → texture copy operations.

---

## Swap / Present Pipeline

### The VdSwap Pathway

Frame presentation follows this path from guest code to host display:

```
Guest game calls D3D Present()
  → D3D runtime calls VdSwap() kernel export
    → VdSwap writes PM4 packets into ring buffer (64 dwords reserved):
       1. Type 0: Set SHADER_CONSTANT_FETCH_00_0 (6 regs, frontbuffer texture fetch)
       2. PM4_XE_SWAP packet: kSwapSignature + frontbuffer_ptr + width + height
       3. Type 2 NOP padding (fills remaining 64-word slot)
    → Updates CP_RB_WPTR register

GPU Commands thread picks up packets:
  → ExecutePacketType3_XE_SWAP()
    → Calls IssueSwap(frontbuffer_ptr, width, height)
    → [Optional: PPM frame dump from guest memory]
    → Increments swap counter

[Separately, from other PM4 packets the game submits:]
  → ExecutePacketType3_INTERRUPT()
    → DispatchInterruptCallback(source=1, cpu_mask)
    → Executes guest interrupt handler on GPU Commands thread
    → Guest callback signals "frame done" to waiting game threads

[From VSync thread, independently at 60Hz:]
  → MarkVblank()
    → Increments vblank counter
    → DispatchInterruptCallback(source=0, cpu=2)
    → Executes same guest interrupt handler on VSync thread
```

### VdSwap Kernel Export Detail

`VdSwap` (`xboxkrnl_video.cc:355-440`) is called from the guest's D3D runtime:

1. Receives frontbuffer texture fetch constant, dimensions, format
2. Translates virtual frontbuffer address → physical address
3. Writes into a 64-dword ring buffer slot:
   - PM4 Type 0 packet: set `SHADER_CONSTANT_FETCH_00_0` (6 regs for the texture fetch)
   - PM4 Type 3 `XE_SWAP` packet: `kSwapSignature` (`make_fourcc("SWAP")` = `0x53574150`) + frontbuffer physical address + width + height
   - Remaining dwords filled with PM4 Type 2 NOP packets
4. Writes `CP_RB_WPTR` to trigger command processor

### IssueSwap (Vulkan Backend)

`VulkanCommandProcessor::IssueSwap()` (`vulkan_command_processor.cc:1263+`):

**With a presenter (windowed mode):**
- Acquires swap chain image
- Copies frontbuffer texture → swap chain image
- Presents to display

**Without a presenter (headless mode):**
- Flushes pending submissions
- Optionally captures frame to PPM via GPU readback:
  1. `RequestSwapTexture()` loads the frontbuffer VkImage
  2. Creates staging buffer (`VK_MEMORY_PROPERTY_HOST_VISIBLE`)
  3. `vkCmdCopyImageToBuffer` → map → write PPM
- Reports frame timing statistics

---

## CPU-GPU Synchronization

The game and GPU synchronize through several mechanisms:

### 1. Ring Buffer Read Pointer Writeback

The GPU periodically writes its `read_ptr_index_` back to a guest memory location (`read_ptr_writeback_ptr_`). The game reads this to know how far the GPU has consumed.

### 2. PM4_EVENT_WRITE_SHD

The GPU writes a value to a guest memory address. The game polls this address (or a register backed by it) to detect completion.

### 3. PM4_INTERRUPT (Critical for DC3)

The GPU fires a guest interrupt callback:
```cpp
// command_processor.cc:931-944
bool ExecutePacketType3_INTERRUPT(...) {
    uint32_t cpu_mask = reader->ReadAndSwap<uint32_t>();
    for (int n = 0; n < 6; n++) {
        if (cpu_mask & (1 << n)) {
            graphics_system_->DispatchInterruptCallback(1, n);
        }
    }
}
```

`DispatchInterruptCallback()` (`graphics_system.cc:250-269`) **executes guest PPC code on the GPU Commands thread:**
```cpp
void GraphicsSystem::DispatchInterruptCallback(uint32_t source, uint32_t cpu) {
    auto thread = XThread::GetCurrentThread();
    uint64_t args[] = {source, interrupt_callback_data_};
    processor_->ExecuteInterrupt(thread->thread_state(), interrupt_callback_,
                                 args, xe::countof(args));
}
```

This is a **blocking call** — whichever thread calls it stops until the guest interrupt handler returns. `DispatchInterruptCallback` runs from **two different threads**:

- **GPU Commands thread** — via `ExecutePacketType3_INTERRUPT()` (source=1, from PM4_INTERRUPT packets in the ring buffer)
- **GPU VSync thread** — via `MarkVblank()` → `DispatchInterruptCallback(0, 2)` (source=0, from the 60Hz VBlank timer)

If the guest interrupt handler on the GPU Commands thread waits for something that needs GPU progress, **deadlock**.

### 4. PM4_WAIT_REG_MEM

The command processor spins waiting for a guest memory location or register to match a condition:
```cpp
// Spin until condition is met
while (!matched) {
    // MakeCoherent() only called when polling COHER_STATUS_HOST register
    uint32_t value = value_ref & mask;
    matched = compare(value, ref);  // EQ, NEQ, GT, GTE, LT, LTE
    if (!matched) {
        MaybeYield();
        // yield threshold based on 'wait' field from the packet
    }
}
```

### 5. VBlank Interrupts

The VSync thread fires `DispatchInterruptCallback(source=0, cpu=2)` at 60Hz via `MarkVblank()`. This also increments the vblank counter that games use for frame pacing. Note: the TODO comment in the source says this dispatch shouldn't be needed here, but without it the CP blocks waiting for interrupt-driven code.

### The Deadlock Problem

**DC3 is extremely sensitive to CP timing.** The game's synchronization uses two interrupt sources:

- **VBlank (source=0):** Fired by the VSync thread at 60Hz. Always runs on the VSync thread, so it's unaffected by CP workload.
- **CP Interrupt (source=1):** Fired by `PM4_INTERRUPT` packets in the ring buffer. Runs on the GPU Commands thread.

The deadlock pattern:
1. Game submits draw commands + `PM4_INTERRUPT` to the ring buffer
2. Game waits for the interrupt callback to signal "frame done"
3. GPU Commands thread must process all draws before reaching the `PM4_INTERRUPT` packet
4. If draw processing takes too long (shader compilation, pipeline creation), the interrupt is delayed
5. Game thread spins waiting → deadlock-like stall

With async pipeline compilation, draws that lack a ready pipeline are skipped (return true immediately), so the CP reaches `PM4_INTERRUPT` quickly. This is why DC3 now runs at full speed with the async pipeline fix — only 9 unique pipeline states are needed, and they warm up in <10 seconds.

---

## XEX Loading and PE Override

### Normal XEX Loading

1. `Emulator::LaunchXexFile()` → `kernel::UserModule::LoadFromFile()`
2. XEX header parsed: encryption, compression, import tables, section headers
3. PE extracted from XEX (may need decryption + decompression)
4. PE sections mapped into guest memory at their specified virtual addresses:
   - `.text` at `0x82330000` (for DC3)
   - `.rdata`, `.data`, `.pdata`, etc. follow
5. Import resolution: kernel/library function thunks patched
6. Entry point called: `mainCRTStartup` at the PE's `AddressOfEntryPoint`

### PE Section Layout (DC3 Original)

The PE is loaded into XEX virtual memory (0x80000000+ range):

```
Section    VA Start      VA End        Purpose
.text      0x82330000    0x82Exxxxx    Code
.rdata     0x82Exxxxx    ...           Read-only data (vtables, strings)
.data      ...           ...           Read-write data
.pdata     ...           ...           Exception unwind info
.reloc     ...           ...           Game data (NOT standard PE reloc!)
```

### Import Thunks

Xbox 360 import thunks have a specific format in guest memory:
```
sc 2        ; System call to kernel
blr         ; Return
nop         ; Padding
nop         ; Padding
```
(16 bytes per thunk)

The kernel patches these during module loading. Variable imports are simply addresses written to guest memory.

### PE Override (`--pe_override`)

Custom Xenia feature (`src/xenia/cpu/xex_module.cc:1063-1159`) for testing decomp binaries:

1. Original XEX loads normally (for metadata, imports, encryption headers)
2. Import resolution runs on original memory layout
3. PE override reads the decomp `default.exe`:
   - Parses PE headers
   - Copies each section's raw data into guest memory at the section's VA
4. Re-patches import thunks (they got overwritten by section copy)
5. Re-writes variable import values

**Status: BLOCKED** — The decomp linker produces different function addresses:
- Original `.text` starts at VA `0x82330000`
- Decomp `.text` starts at VA `0x82331600` (shifted by `0x1600`)
- Per-function offsets vary non-uniformly (link order differs)
- Entry point `mainCRTStartup` is at the wrong address → crash

---

## Headless Mode

**Source:** `src/xenia/app/xenia_headless_main.cc`, `emulator_headless.cc`

> **Note:** Everything in this section is **our custom work** — stock Xenia has no headless mode.

Headless mode runs Xenia without a window. The key architectural difference:

### No Presenter

When `GraphicsSystem::presenter()` returns `nullptr`:
- VulkanCommandProcessor knows it's headless
- No swap chain, no display
- Frame output via PPM file dump instead

### Draw Skipping (The Critical Optimization)

```cpp
// vulkan_command_processor.cc:2469-2476
// HEADLESS: Skip non-copy draws unless this is a capture frame
if (!graphics_system_->presenter() && !headless_render_frame_ && !cvars::force_all_draws) {
    return true;  // Skip draw, pretend success
}
```

**Why this is necessary:** Processing non-copy draws requires:
- Shader translation (Xenos → SPIR-V)
- Pipeline creation (VkPipeline)
- Memory synchronization
- Actual Vulkan draw recording

Each of these takes time. The CP thread is single-threaded, and while it's doing draw work, it can't process `PM4_INTERRUPT` packets. DC3's game threads are waiting for those interrupts. Result: deadlock.

**Copy draws always execute** because they're EDRAM resolves — needed for the frontbuffer to have content for frame capture.

### Warmup Strategy for Frame Capture

To capture an actual rendered frame without deadlocking:

1. **Frame N-2 (warmup):** Enable `headless_render_frame_`, use synchronous pipeline compilation. Pipelines compile and cache.
2. **Frame N-1 (warmup):** Draws execute with cached pipelines (fast). EDRAM gets populated.
3. **Frame N (capture):** Readback EDRAM content via staging buffer → PPM.

Between capture frames, draws are skipped and the CP processes commands at full speed.

### Non-Blocking Fence Check

```
BeginSubmission headless fix: Non-blocking fence check instead of blocking wait.
Without this, BeginSubmission blocks waiting for completed Vulkan fences,
preventing the CP from processing PM4_INTERRUPT commands.
```

### Performance Comparison

| Mode | Swaps/20s | FPS | Deadlock? | Notes |
|------|-----------|-----|-----------|-------|
| Null GPU (no rendering) | 600+ | ~30 | No | Baseline |
| Vulkan, skip non-copy draws | 611 | ~30.5 | No | Same as null |
| Vulkan, all draws, sync pipelines | 12-13 | ~0.6 | **Yes** | Pipeline creation blocks CP |
| **Vulkan, all draws, async pipelines** | **611** | **~30.5** | **No** | **Full speed, matches null GPU** |
| Vulkan, all draws + readback | 5 | ~0.25 | No | Readback is sole bottleneck (122x) |

---

## Key Problems and Blockers

### 1. PE Override Address Mismatch

**Problem:** Decomp PE has different function addresses than original.

**Root cause:** MSVC linker object ordering differs. Original build used a specific link order; our decomp link order places objects differently.

**Impact:** Entry point jumps to wrong code → crash.

**Possible fixes:**
- Generate COMDAT order file from original `.map` to match link order
- Per-function address patching using objdiff database
- Custom XEX builder with matching metadata

### 2. CP Timing Sensitivity

**Problem:** DC3 deadlocks if the command processor takes too long on any single packet.

**Root cause:** Game uses `PM4_INTERRUPT` for frame completion signaling. The interrupt is processed in-order with draws. Slow draws delay the interrupt → game thread spins forever.

**Mitigation:** Skip non-copy draws in headless mode. For windowed mode, async pipeline compilation helps but doesn't fully solve it.

### 3. Shader Compilation Performance (SOLVED)

**Problem:** First-time shader compilation was slow (~0.5 fps).

**Root cause:** Each unique shader must be translated Xenos → SPIR-V → compiled to VkPipeline.

**Resolution:** Async pipeline compilation (custom, see below). DC3 only uses ~9 unique pipeline states (observed at runtime) — warmup completes in <10 seconds with only 2-3 stalls. After warmup, the game runs at its internal fixed timestep (~33 fps). Xenia also supports persistent shader storage (`--store_shaders`).

### 4. Frame Content / Readback Performance

**Problem:** GPU readback drops performance from 30fps to 0.25fps (122x slower).

**Root cause:** Current readback path does:
- 2x `AwaitAllQueueOperationsCompletion()` (full GPU drain)
- Per-frame staging buffer + memory + command pool allocation/deallocation
- 2.7MB synchronous PPM file write

**Captured frames so far:** Solid colors and boot screens. Game needs 2000+ swaps (60+ seconds) and scripted input to reach menus/gameplay.

---

## Appendix: Key Source File Index

| File | Purpose |
|------|---------|
| `src/xenia/emulator.h/.cc` | Top-level emulator, subsystem setup |
| `src/xenia/memory.h/.cc` | Guest memory management |
| `src/xenia/gpu/command_processor.h/.cc` | Base CP, PM4 packet parsing |
| `src/xenia/gpu/graphics_system.h/.cc` | GPU system base, VSync, MMIO, interrupts |
| `src/xenia/gpu/vulkan/vulkan_command_processor.h/.cc` | Vulkan draw/swap/copy implementation |
| `src/xenia/gpu/vulkan/vulkan_graphics_system.h/.cc` | Vulkan system setup |
| `src/xenia/gpu/vulkan/vulkan_pipeline_cache.h/.cc` | Shader→pipeline management |
| `src/xenia/gpu/vulkan/vulkan_render_target_cache.h/.cc` | EDRAM render target tracking |
| `src/xenia/gpu/vulkan/vulkan_shared_memory.h/.cc` | Guest→GPU memory mirroring |
| `src/xenia/kernel/xthread.h/.cc` | Guest thread emulation |
| `src/xenia/kernel/xboxkrnl/xboxkrnl_video.cc` | VdSwap, VdInitializeRingBuffer, etc. |
| `src/xenia/cpu/xex_module.cc` | XEX/PE loading, PE override |
| `src/xenia/app/xenia_headless_main.cc` | Headless entry point |
| `src/xenia/app/emulator_headless.cc` | Headless emulator wrapper |
