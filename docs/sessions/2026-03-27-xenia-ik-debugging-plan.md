# Xenia IK Debugging Plan — 2026-03-27

## Goal

Determine why character feet clip through the floor on the native port but NOT on the real Xbox game. The decomp code structure appears to match the original, but the behavior differs. We need to capture runtime IK values from the original game via Xenia to find the divergence.

## Background

- Ankle Z = 1.10 on native (should be ~3-4 to keep foot sole above floor)
- Toe Z = -2.90 on native (below floor at Z=0)
- 749/756 gameplay telemetry samples show feet below floor
- The ground clamp in `HamIKEffector::Poll()` is inside `if (totalWeight < 1.0f)` — when IK constraints sum to >= 1.0, the clamp is skipped
- A previous Ghidra comparison claimed the structure matches the original, but the user verified the real Xbox game does NOT have this issue
- Failing test: `GameplayTelemetryTest.FeetNotBelowFloorDuringGameplay`

## Key Question

**What is `totalWeight` during gameplay on the real Xbox?** This determines whether the ground clamp path even executes. Three scenarios:

1. `totalWeight >= 1.0` on Xbox too → clamp never fires on either platform, but Xbox feet stay above ground via some other mechanism (constraint targets? different weights?)
2. `totalWeight < 1.0` on Xbox → clamp fires and works, meaning our `ApplyConstraints` returns different weights despite being 100% matched
3. Something else entirely — a code path we're not seeing, or a subtle computation difference in the constraint/blending math

## Key Addresses (from `config/373307D9/symbols.txt`)

| Function | Address | Size | Match% |
|----------|---------|------|--------|
| HamIKEffector::Poll | `0x824C21E8` | 0x4FC | 83.3% |
| ApplyConstraints | `0x824BF5D8` | 0x244 | 100% |
| GetGroundHeight | `0x824BF820` | 0x78 | 77.8% |
| ApplyPosConstraints | `0x824BF430` | 0x1A8 | 100% |
| ComputeHandPullAndQuat | `0x824C0A80` | — | 86.4% |
| GetType | `0x824C0820` | 0x25C | 100% |
| IKElbow | `0x824C16D8` | 0xF0 | 100% |
| DoFancyElbow | `0x824C17C8` | 0x3FC | — |

## Approach: PPC Bytepatch Instrumentation

### Why not guest function overrides?

The initial plan proposed using `RegisterGuestFunctionOverride` with trampolines to the
original code. This **doesn't work** because:

1. `RegisterGuestFunctionOverride` calls `SetupExtern()`, which replaces the function
   entirely — the original PPC code is never JIT-compiled
2. Calling `processor->Execute()` on the same address recurses into the override handler
3. The ArkFile::Read "trampoline" pattern in the hack pack is actually a **complete
   replacement** — it never calls the original code

### What works: PPC `blr`-patching + guest memory slots

We use the same pattern as the DataReadStream and gSystemConfig code caves already in
`dc3_hack_pack.cc` (lines 4630-4760). The approach:

1. **Allocate guest memory** for telemetry float slots via `SystemHeapAlloc`
2. **Scan target functions** for `blr` instructions (`0x4E800020`)
3. **Build PPC code caves** in the `protocol_debug_string` dead code region that:
   - Store `f1` (float return value) to a guest memory slot using `lis r11` + `stfs f1`
   - Execute `blr` (the real return)
4. **Patch each `blr`** in the target function to `b code_cave` (unconditional branch)
5. **Read slots from host side** via a `RegisterGuestFunctionOverride` on
   `HolmesClientPoll` (called every frame, already noop-stubbed)

This is non-invasive: the original function code runs unmodified except that returns
detour through a 3-instruction cave that stores f1 before returning.

### Implementation Detail

**Code cave layout** (in `protocol_debug_string` area, starting at +28 after existing caves):

| Offset | Size | Type | Purpose |
|--------|------|------|---------|
| +28 | 12B | return | ApplyConstraints: `stfs f1` (totalWeight) |
| +40 | 12B | return | GetGroundHeight: `stfs f1` (groundHeight) |
| +52 | 12B | return | GetType: `stw r3` (EffectorType enum) |
| +64 | 12B | return | ApplyPosConstraints: `stfs f1` (posWeight) |
| +76 | 16B | entry | Poll: `stw r3` (this ptr) + displaced insn + b back |
| +92 | 20B | entry | IKElbow: `lfs f0,8(r4)` (v.z) + displaced insn + b back |
| +112 | 16B | entry | DoFancyElbow: `stfs f1` (handWeight) + displaced insn + b back |

Total: 128 bytes of 304 available.

**Guest memory telemetry slots** (allocated via SystemHeapAlloc, 40 bytes):

| Offset | Type | Field | Source |
|--------|------|-------|--------|
| +0 | float | totalWeight | ApplyConstraints f1 return |
| +4 | float | groundHeight | GetGroundHeight f1 return |
| +8 | uint32 | effectorType | GetType r3 return (0=none,1=pelvis,2=ankle,3=hand) |
| +12 | float | posWeight | ApplyPosConstraints f1 return |
| +16 | uint32 | pollThisPtr | Poll r3 at entry (HamIKEffector*) |
| +20 | float | ikElbowZ | IKElbow entry v.z (ankle Z before elbow modifies parents) |
| +24 | float | fancyWeight | DoFancyElbow entry f1 (hand effector totalWeight) |
| +28..+39 | — | reserved | — |

**Host-side derived data** (read through `pollThisPtr`):

The host-side log reader dereferences the captured `this` pointer to read
HamIKEffector member fields from guest memory:

| Member | Offset | What it tells us |
|--------|--------|-----------------|
| mEffector | +0x44 | ObjPtr to the bone being IK'd |
| mGround | +0x6C | ObjPtr to ground plane transform (may be null) |
| mMore | +0x80 | ObjPtr to chained effector (recursive constraints) |
| mConstraints | +0xBC | ObjVector — count = (finish - start) / 0x18 |

**PPC instruction encodings:**
```
lis r11, hi16(addr)       → 0x3D600000 | (hi & 0xFFFF)      ; load slot base
stfs f1, lo16(addr)(r11)  → 0xD02B0000 | (lo & 0xFFFF)      ; store float ret
stw r3, lo16(addr)(r11)   → 0x906B0000 | (lo & 0xFFFF)      ; store int ret/arg
blr                       → 0x4E800020
b target                  → 0x48000000 | ((delta) & 0x03FFFFFC)
```

Note: when `lo16(addr)` has bit 15 set (>= 0x8000), increment `hi` by 1 to compensate
for sign extension in the displacement field (standard PPC `addis` adjustment).

## Instrumented Call Chain

```
Poll(this)                          ← entry cave captures this* (r3)
  │
  ├─ GetType()                      ← return cave captures EffectorType (r3)
  │
  ├─ ApplyConstraints(q, neutral)   ← return cave captures totalWeight (f1)
  │   └─ mMore->ApplyConstraints()  (recursive — same cave, last caller wins)
  │
  ├─ [if ankle/pelvis, totalWeight < 1.0]:
  │   └─ GetGroundHeight(ground)    ← return cave captures groundHeight (f1)
  │
  ├─ [if pelvis]:
  │   └─ ApplyPosConstraints(...)   ← return cave captures posWeight (f1)
  │
  ├─ IKElbow(v)                     ← entry cave captures v.z (ankle Z pre-elbow)
  │
  ├─ [if hand effector]:
  │   └─ DoFancyElbow(handQ, f1)    ← entry cave captures f1 (handWeight)
  │
  └─ host reader derives:
      ├─ mEffector ptr  (bone identity)
      ├─ mGround ptr    (ground plane ref)
      ├─ mMore ptr      (chained effector)
      └─ constraint count
```

## Running

```bash
$XENIA_BIN \
  --target=./orig-assets/debug.xex \
  --gpu=vulkan \
  --dc3_nui_patch_layout=original \
  --dc3_crt_skip_nui=true \
  --dc3_ik_telemetry=true
```

Navigate to gameplay. IK values print to Xenia log every 60 frames.

## Using the Telemetry Data

### Building and running

```bash
# Build xenia-headless (from xenia repo root)
make -C build config=checked_linux xenia-headless

# Launch with IK telemetry enabled
build/bin/Linux/Checked/xenia-headless \
  --target=/path/to/orig-assets/debug.xex \
  --gpu=vulkan \
  --dc3_nui_patch_layout=original \
  --dc3_crt_skip_nui=true \
  --dc3_ik_telemetry=true 2>&1 | tee /tmp/xenia_ik.log
```

Navigate to gameplay (start a song). The log will contain lines like:
```
DC3:IK [frame 360] type=ankle totalWeight=0.8523 groundHeight=0.0000 posWeight=0.0000 ikElbowZ=3.2100 fancyWeight=0.0000 this=83A12340 effector=83B45670 ground=00000000 more=00000000 constraints=2
DC3:IK [frame 420] type=ankle totalWeight=1.0214 groundHeight=0.0000 posWeight=0.0000 ikElbowZ=3.1890 fancyWeight=0.0000 this=83A12340 effector=83B45670 ground=00000000 more=83A12500 constraints=2
```

### Reading the output

Each log line is a **snapshot** of the last values written to the telemetry slots.
Because multiple effector types are polled per frame (ankle, hand, pelvis, head),
the slot values reflect whichever effector was polled **last** before the reader fires.
Filter by `type=ankle` for the foot-clipping investigation.

Field reference:

| Field | Source | What it tells you |
|-------|--------|-------------------|
| type | GetType r3 | Which effector was last polled (ankle=2 is what we care about) |
| totalWeight | ApplyConstraints f1 | Sum of constraint weights; if >= 1.0 the ground clamp is skipped |
| groundHeight | GetGroundHeight f1 | Ground plane Z reference; 0 = default floor |
| posWeight | ApplyPosConstraints f1 | Position-only constraint weight (pelvis path) |
| ikElbowZ | IKElbow r4+8 | Ankle Z value passed to IKElbow (before parent bone modification) |
| fancyWeight | DoFancyElbow f1 | Hand effector totalWeight passed to DoFancyElbow |
| this | Poll r3 | Guest address of HamIKEffector instance |
| effector | this+0x44 | The bone being IK'd (mEffector ObjPtr) |
| ground | this+0x6C | Ground plane transform (mGround ObjPtr), 0 = none |
| more | this+0x80 | Chained effector for recursive constraints |
| constraints | this+0xBC | Number of constraint entries in mConstraints vector |

### Extracting and analyzing

Pull the IK lines from the log:
```bash
grep 'DC3:IK \[frame' /tmp/xenia_ik.log > /tmp/ik_samples.txt
```

**Filter to ankle effectors only:**
```bash
grep 'type=ankle' /tmp/ik_samples.txt > /tmp/ik_ankle.txt
```

**1. Is totalWeight ever >= 1.0 for ankles?**
```bash
awk -F'totalWeight=' '{print $2}' /tmp/ik_ankle.txt | awk '{print $1}' | \
  awk '{if ($1+0 >= 1.0) above++; else below++} END {print ">=1.0:", above+0, "<1.0:", below+0}'
```
- If **always >= 1.0**: the ground clamp in Poll never fires on Xbox either. The
  constraint targets themselves position feet above ground. Investigate constraint
  target WorldXfm values — the divergence is in the input data, not the clamp logic.
- If **mixed or always < 1.0**: the clamp path executes on Xbox. Compare the
  totalWeight distribution between Xbox and native to find where weights diverge.

**2. What is groundHeight?**
```bash
awk -F'groundHeight=' '{print $2}' /tmp/ik_ankle.txt | awk '{print $1}' | sort -u
```
- If **always 0.0**: the ground plane is at Z=0 (expected for most venues).
- If **non-zero**: a `mGround` RndTransformable is set, and its WorldXfm.v.z is the
  reference. Compare with native to check for divergent ground transforms.

**3. Does the ankle have a mMore chained effector?**
```bash
awk -F'more=' '{print $2}' /tmp/ik_ankle.txt | awk '{print $1}' | sort -u
```
- If **non-zero**: the ankle effector chains to another HamIKEffector via mMore,
  meaning `ApplyConstraints` recurses. The recursive call adds its own weight
  contributions. A missing or different mMore on native would change totalWeight.

**4. How many constraints?**
```bash
awk -F'constraints=' '{print $2}' /tmp/ik_ankle.txt | sort | uniq -c | sort -rn
```
- Typical ankle effectors have 1-3 constraints. If the count differs from native,
  constraint data failed to load or resolve.

### Decision tree

```
totalWeight >= 1.0 on Xbox?
├── YES → Ground clamp never fires on either platform
│         → Feet stay above floor because constraint TARGETS position them there
│         → Next step: instrument constraint target WorldXfm positions
│         → The native port divergence is in constraint target data, not clamp code
│
└── NO (totalWeight < 1.0) → Ground clamp fires on Xbox
    │
    ├── groundHeight matches native?
    │   ├── YES → Clamp fires with same inputs
    │   │         → The clamp logic itself works, but something AFTER Poll
    │   │           overrides it on native (or vice versa)
    │   │         → Check for CharIKFoot, secondary Poll passes, parent transforms
    │   │
    │   └── NO → Ground reference diverges
    │           → Check mGround ObjPtr resolution — the target object may be
    │             different or unresolved on native
    │
    ├── constraint count matches native?
    │   └── NO → Constraint data loading diverges
    │           → Check ObjVector serialization and ObjPtr resolution
    │
    ├── mMore pointer matches?
    │   └── NO → Chained effector missing/different
    │           → Check HamIKEffector::Load and ObjPtr<HamIKEffector> resolution
    │
    └── totalWeight differs between platforms?
        → ApplyConstraints is 100% matched, so same code produces different
          output — the INPUT data differs (constraint target positions,
          NeutralWorldXfm, or mWeight values)
        → Next step: instrument inside ApplyConstraints to log per-constraint
          contributions (would need additional PPC caves)
```

### Generating a unit test fixture from the data

The telemetry output provides enough to build a native-port unit test that
reproduces the exact Xbox conditions:

```cpp
// Fixture from Xenia IK telemetry capture
TEST(HamIKFixture, AnkleFromXboxCapture) {
    // From: DC3:IK [frame 360] type=ankle totalWeight=0.85 ...
    //       this=83A12340 constraints=2

    // Set up a HamIKEffector with the same member state:
    // - N constraints with captured target positions
    // - Same mGround pointer (null or valid)
    // - Same mMore chain
    // Then call Poll() and verify:
    // - totalWeight matches Xbox value (0.85)
    // - Final ankle Z > groundHeight (foot above floor)
}
```

To capture the per-constraint target data needed for a full fixture, extend
the instrumentation as described below.

### Extending the instrumentation

The `protocol_debug_string` region has ~176 bytes of free space starting at
offset +128. Each additional return cave costs 12 bytes (3 PPC instructions).

To instrument a new function:
1. Add its address to `Dc3Addresses` in `dc3_hack_pack.cc`
2. Add a new cave offset constant and `BuildReturnCaveF1`/`BuildReturnCaveR3` call
3. Call `PatchBlrInstructions` on the new function
4. Add a slot constant and read in the `ik_log_handler` lambda
5. Bump `kIkTelemetrySize` to accommodate the new slot

## Alternative: Quick GDB Check

For a fast one-off inspection before building the full hook:

```bash
# Terminal 1: Launch Xenia with GDB stub
$XENIA_BIN --target=./orig-assets/debug.xex --gpu=vulkan \
  --dc3_nui_patch_layout=original --dc3_crt_skip_nui=true \
  --dc3_gdb_rsp_stub=true --dc3_gdb_rsp_port=9001

# Terminal 2: Attach GDB
powerpc-none-eabi-gdb
(gdb) target remote 127.0.0.1:9001
(gdb) break *0x824C21E8           # HamIKEffector::Poll entry
(gdb) continue
# Navigate to gameplay, then when breakpoint hits:
(gdb) break *0x824C21E8+0x154     # After ApplyConstraints call (approximate)
(gdb) continue
(gdb) info float                   # f1 = totalWeight
(gdb) print $f1                    # The key value
```

Note: GDB RSP stub is slow for automated capture but fine for a single manual check.

## What Success Looks Like

After this investigation, we'll know one of:
1. **totalWeight is always < 1.0 on Xbox** → our constraint weights differ despite 100% match (look at input data: constraint targets, bone positions)
2. **totalWeight >= 1.0 on Xbox too** → the ground clamp never fires on either, but Xbox's constraints produce ankle positions above ground (look at constraint target positions)
3. **There's a code path we're missing** → the Ghidra comparison missed something, or there's a secondary system (CharIKFoot?) that adjusts foot height after HamIKEffector

## Files

- `src/system/hamobj/HamIKEffector.cpp` — the IK code with the ground clamp
- `native/tests/test_gameplay_telemetry.cpp` — `FeetNotBelowFloorDuringGameplay` test (currently failing)
- `native/src/telemetry/GameplayTelemetry.cpp` — foot bone telemetry capture
- Xenia: `/home/free/code/milohax/xenia/src/xenia/dc3_hack_pack.cc` — guest function overrides + PPC caves
- Xenia tools: `/home/free/code/milohax/xenia/tools/dc3_trace_on_break.sh`, `dc3_runtime_telemetry_diff.py`
