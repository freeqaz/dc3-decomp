# Type-Aware Fixture Generation: Design Doc

## Summary

**Proposal**: Use class layout info to generate structured initial object memory for the unicorn runner, replacing uniform byte-fill patterns with type-appropriate values for scalar members (bool, float, enum, int).

**Scope decision**: Scalars only. Containers and pointers stay zeroed. Validate experimentally before heavy investment.

**Data sources**: Two complementary sources, DC3-first with RB2 fallback:
1. **DC3 struct_db** (`struct_db.sqlite`, built from DC3 headers with `// 0xOFFSET` annotations) — accurate DC3 layouts, 937 classes with members, covers DC3-specific classes (CampaignPerformer, AccomplishmentManager). No member sizes (inferred from offset gaps + type heuristics).
2. **RB2 DWARF dump** (`rb2_dump.cpp`) — 1,832 classes with offset+size info, broader Milo engine coverage. Fallback for classes not annotated in DC3 headers.
3. DC3 .obj debug info is a dead end — no debug sections exist (no CodeView, no DWARF, no PDB).

**Priority**: Typed fixtures first (low effort, natural extension), then bctr handling, permuter guard rail, Phase 2 sentinel probing.

---

## What We Know (Data-Driven)

### Divergence Root Causes (from stress test, 1,779 divergent functions)

| Cause | % of divergences | Would typed fixtures help? |
|-------|-----------------|---------------------------|
| Loop iteration over zeroed containers (vector, list) | ~60% | **No** — containers need internal pointer graphs, not just member values |
| `__FILE__` / merged symbol differences | ~25% | **No** — already auto-filtered by Phase 0 classification |
| Structural code differences (decomp not done) | ~10% | **No** — real code bugs |
| Semantic differences from scalar initial values | ~5% | **Yes** — bool/float/enum initial values change branch decisions |

**Bottom line**: Typed fixtures address ~5% of remaining divergences — roughly **90 functions** out of 1,779. Real value, but scoped.

### RB2 DWARF Coverage

- **1,832 classes** in RB2 dump (7MB, 218K lines)
- Covered key classes: DirLoader (168B/16 members), Profile (912B/18 members), LightPreset (308B/30 members), UILabel (480B/23 members), Character (672B/14 members)
- **Missing** from top divergent units: CampaignPerformer (56 div functions), AccomplishmentManager (24 div functions)
- ByteGrinder (23 div functions): 4B, 0 members — useless

### DC3 struct_db Coverage (Primary Source)

Built from DC3 source headers with `// 0xOFFSET` annotations (`tools/struct_db.py build src/ include/`):
- **2,105 classes total, 937 with annotated members, 6,068 member entries**
- Has: member name, C++ type string, offset. **No size** (must infer).
- Covers DC3-specific classes missing from RB2:

| Class | struct_db members | RB2 members | Notes |
|-------|-------------------|-------------|-------|
| CampaignPerformer | **11** | not found | #1 most divergent unit (56 div functions) |
| AccomplishmentManager | **14** | not found | 24 div functions |
| Character | **20** | 14 | More members in DC3 |
| DirLoader | 15 | 16 | Similar coverage |
| LightPreset | 24 | 30 | RB2 has more |
| Profile | 3 | 18 | RB2 has far more |
| UILabel | 3 | 23 | RB2 has far more |
| ByteGrinder | 0 | 0 | No data in either |

### Data Source Comparison

| | DC3 struct_db | RB2 DWARF | Ghidra MCP | DC3 .obj debug |
|---|---|---|---|---|
| Has offset | Yes | Yes | Sometimes | **Dead end** (no debug sections) |
| Has size | **No** (infer) | Yes | No | N/A |
| Has type | C++ strings | DWARF types | Decompiled | N/A |
| DC3-specific classes | **Yes** | No | Theoretically | N/A |
| Accuracy for DC3 | **Exact** | Approximate | Variable | N/A |
| Bulk query | Yes (sqlite) | Yes (parsed) | No (per-function) | N/A |
| Classes with data | 937 | 1,832 | Unknown | 0 |

**Strategy**: DC3 struct_db first (accurate DC3 layouts), RB2 DWARF fallback (broader coverage, has sizes).

### Member Type Distribution (9,140 members across all RB2 classes)

| Type category | Count | % | Typed fixture value |
|---------------|-------|---|---------------------|
| Raw pointers (`T*`) | 1,623 | 17.8% | NULL = same as zero fill |
| `int` / `unsigned int` / `long` | 1,960 | 21.4% | Small values — marginal gain over random fill |
| `unsigned char` (bool proxy) | 1,182 | 12.9% | **Real win**: 0/1 vs 0xCD matters for bool comparisons |
| `float` | 1,132 | 12.4% | **Real win**: valid floats vs NaN from 0xCDCDCDCD |
| Container types (`vector`, `list`, `ObjPtr`, etc.) | ~1,000 | ~10.9% | Zeroed (same as current) |
| Other (Symbol, String, Color, enum, etc.) | ~1,243 | 13.6% | Mixed — Color gets valid RGBA, Symbol stays NULL |

### What Random Fill Already Covers

Phase 1 probing runs 8 times with fill bytes 0x00, 0xCD, and 6 random values. This already varies scalar values — an `int` field at offset 0x30 gets 0x00000000, 0xCDCDCDCD, 0x7B7B7B7B, etc.

What random fill does **poorly**:
- **`bool`**: 0xCD is truthy but not 0/1. Compiler may emit `cmpwi r3, 1` — 0xCD fails. Type-aware fill guarantees valid bool values.
- **`float`**: 0xCDCDCDCD is NaN. Functions doing float arithmetic on NaN take different paths. Type-aware fill guarantees non-NaN values.
- **`enum`**: Random fill gives garbage enum values. Switch default cases always hit; valid enum values exercise specific cases.

---

## Design

### Architecture

```
prober.py (existing)
  └── typed_fixture.py (new)
        ├── ClassLayoutCache
        │     ├── loads DC3 struct_db (primary)
        │     ├── loads RB2 DWARF parser (fallback)
        │     └── get_members(class_name) → list[MemberInfo] | None
        ├── generate_typed_object(class_name, rng, fill_byte) → bytearray | None
        ├── extract_class_from_symbol(mangled_name) → str | None
        └── infer_size(member, next_member) → int
```

### `ClassLayoutCache` — Dual Data Source

```python
class ClassLayoutCache:
    """Lazy-loaded class layout info from DC3 struct_db + RB2 DWARF."""

    def __init__(self, struct_db_path="struct_db.sqlite", rb2_dump_path=None):
        self._struct_db = None   # DC3 struct_db (primary)
        self._rb2_parser = None  # RB2 DWARF (fallback)

    def get_members(self, class_name):
        """Get member list for class. DC3 struct_db first, then RB2.

        Returns list of MemberInfo(name, type_str, offset, size) or None.
        """
        # Try DC3 struct_db first (accurate DC3 layouts)
        members = self._try_struct_db(class_name)
        if members:
            return members
        # Fall back to RB2 DWARF (broader coverage, has sizes)
        return self._try_rb2(class_name)
```

### Size Inference (for struct_db members)

struct_db has offset but no size. Infer from offset gaps and type heuristics:

```python
def infer_size(member, next_offset, total_size):
    """Infer member size from offset gap or type name."""
    # If there's a next member, size = gap
    if next_offset is not None:
        return next_offset - member.offset

    # Last member: use type heuristics
    return TYPE_SIZES.get(normalize_type(member.type_str), 4)

TYPE_SIZES = {
    'bool': 1, 'unsigned char': 1, 'char': 1,
    'short': 2, 'unsigned short': 2,
    'int': 4, 'unsigned int': 4, 'long': 4, 'float': 4,
    'double': 8, 'long long': 8,
    'Symbol': 4, 'ObjPtr': 12, 'ObjOwnerPtr': 12,
    'String': 12, 'ObjPtrList': 20, 'Color': 16,
    'Vector2': 8, 'Vector3': 12, 'Timer': 56,
}
```

### `generate_typed_object` Logic

```python
def generate_typed_object(class_name, rng, fill_byte=0x00, size=REGION_SIZE):
    """Generate type-aware object memory from class layout.

    Returns bytearray of `size` bytes, or None if class not found.
    Tries DC3 struct_db first, falls back to RB2 DWARF.
    Falls back to fill_byte for gaps between known members.
    """
    members = cache.get_members(class_name)
    if not members:
        return None

    # Start with fill byte (covers gaps and unknown regions)
    mem = bytearray([fill_byte]) * size

    # Vtable pointer at offset 0
    struct.pack_into(">I", mem, 0, VTABLE_BASE)

    # Fill known members with type-appropriate values
    for member in members:
        fill_member(mem, member, rng)

    return mem
```

### Type-Specific Fill Rules

| Member type | Fill strategy | Rationale |
|-------------|---------------|-----------|
| `int`, `long` | `rng.choice([0, 1, -1, 2, 10])` | Small values; include negative |
| `unsigned int`, `unsigned long` | `rng.choice([0, 1, 2, 10, 100])` | Non-negative small values |
| `unsigned char` (1B, used as bool) | `rng.choice([0, 1])` | Valid bool — **primary win** |
| `signed short`, `unsigned short` | `rng.choice([0, 1, 2])` | Small values |
| `float` | `rng.choice([0.0, 1.0, -1.0, 0.5])` | Valid non-NaN — **second biggest win** |
| `double` | `rng.choice([0.0, 1.0])` | Valid non-NaN |
| `enum *` | `0` | Safe default; don't know valid ranges |
| `class Symbol` | `0` (4B) | Just a `const char*` — NULL is safe |
| `class String` | `0` (12B, cap=0 str=NULL) | Valid empty string |
| `class vector` | `0` (12B) | Valid empty vector (start=finish=cap=NULL) |
| `class list` | `0` (8B) | Valid empty list |
| `class ObjPtr`, `class ObjOwnerPtr` | `0` (12B) | Valid null ref |
| `class ObjPtrList` | `0` (20B) | Valid empty list |
| `class Color` | `(1.0, 1.0, 1.0, 1.0)` (16B) | Valid RGBA |
| `class Timer` | `0` (56B) | Stopped timer |
| `T*` (raw pointer) | `0` (4B) | NULL — safe default |
| Unknown type | `fill_byte` | Fallback |

Type matching: parse the `type` string from RB2 DWARF. Match by prefix/suffix:
- Ends with `*` → pointer → zero
- Starts with `class vector` or `class list` → container → zero
- `unsigned char` with size 1 → bool
- `float` → float
- `enum ` prefix → enum → zero
- `int`, `long`, etc. → integer with appropriate signedness

### Symbol → Class Name Extraction

```python
def extract_class_from_symbol(mangled):
    """Extract class name from MSVC mangled symbol.

    ?Method@ClassName@@... → 'ClassName'
    ??0ClassName@@...      → 'ClassName' (constructor)
    ??1ClassName@@...      → 'ClassName' (destructor)
    ?FreeFunc@@...         → None
    """
```

Regex: `??[0-9](\w+)@@` for ctors/dtors, `?\w+@(\w+)@@` for methods. Returns None for free functions and namespaced types that need deeper parsing — acceptable since ~95% of functions are simple member functions.

### Engine Change

Add `object_memory: bytes | None = None` parameter to `UnicornEngine.execute()` and `execute_function()`:

```python
def execute(self, ..., object_memory=None):
    # ... existing fill logic for stack, object, global, vtable regions ...

    # Override object region if typed memory provided
    if object_memory is not None:
        mu.mem_write(OBJECT_BASE, bytes(object_memory[:REGION_SIZE]))

    # Vtable pointer always on top (overrides first 4 bytes)
    mu.mem_write(OBJECT_BASE, _VTABLE_PTR)
```

The fill pattern still applies to **stack, globals, and on-demand pages**. Only the object region gets typed memory. This is correct because `this` points to OBJECT_BASE and typed fixtures describe the `this` object's layout.

### Prober Integration

```python
def probe_function(symbol, decomp_coff, orig_coff, runs=8, typed=False, ...):
    """
    Fill pattern sequence when typed=True:
      Run 0: zero fill, no typed memory (baseline)
      Run 1: 0xCD fill, no typed memory
      Run 2: zero fill + typed object memory (seed A)
      Run 3: 0xCD fill + typed object memory (seed B)
      Run 4+: random byte patterns (no typed memory)

    When class info unavailable: falls back to all fill-byte runs.
    """
```

`RunDetail` gains `fixture_type: str` field: `"fill"`, `"typed"`, or `"typed+fill"`.

### Threading Through `_run_comparison_core`

Add `object_memory` parameter to `_run_comparison_core()`. The prober generates the memory and passes it through. `_run_comparison_core` passes it to `engine.execute()`. Both sides (decomp and orig) always get the **same** object memory — the comparison must be differential.

### CLI

```bash
# Single function with typed fixtures
python3 -m scripts.unicorn_runner.probe --unit DirLoader --symbol "?GetAll@DirLoader@@UAEXXZ" --typed --runs 8

# Batch with typed fixtures
python3 -m scripts.unicorn_runner.probe --unit LightPreset --batch --typed --runs 8
```

### New Files

- `scripts/unicorn_runner/typed_fixture.py`

### Modified Files

- `scripts/unicorn_runner/engine.py` — `object_memory` param (~5 lines)
- `scripts/unicorn_runner/prober.py` — `typed` param, fixture generation (~20 lines)
- `scripts/unicorn_runner/probe.py` — `--typed` CLI flag (~5 lines)
- `scripts/unicorn_runner/run.py` — thread `object_memory` through `_run_comparison_core` (~10 lines)

---

## What NOT to Build

1. **DC3 COFF debug parser** — no debug sections exist in the .obj files. Dead end, confirmed by investigation.
2. **Recursive object graph construction** — initializing pointed-to objects requires sub-object allocation and recursive typing. High complexity, marginal gain.
3. **Non-empty container mocking** — creating vector elements/list nodes requires element type knowledge and storage allocation. A vector with 1 garbage element isn't more realistic for differential testing.
4. **Ghidra bulk type extraction** — no bulk data type query API. Must decompile per-function. Too slow for building a layout cache.
5. **Hypothesis/property-based testing** — solves input space exploration, not structured memory generation.

---

## Expected Impact

### Estimates

| Scenario | Functions flipped | Equivalence rate |
|----------|-------------------|-----------------|
| Optimistic | ~90 (5% of divergent) | 93.1% → ~93.5% |
| Realistic | ~30-50 | 93.1% → ~93.3% |
| Pessimistic | <10 | Negligible |

Primary benefit: bools and floats exercising correct branch paths in functions that currently diverge only because 0xCD/NaN causes different control flow.

### Effort

~180 lines total, ~2-3 hours implementation. Low risk — new code is isolated in `typed_fixture.py`, engine change is 5 lines.

---

## Validation Plan (Before Full Implementation)

1. Pick 10 DIVERGENT functions from units with RB2 class info:
   - DirLoader (16 members, well-covered)
   - Profile (18 members)
   - LightPreset (30 members, 40 divergent functions)
   - UILabel (23 members, 26 divergent functions)
2. For each: manually construct typed object memory as a bytearray
3. Run with typed memory vs zero fill vs 0xCD fill using `_run_comparison_core`
4. Count flips: DIVERGENT → EQUIVALENT
5. **Decision gate**: If <3 functions flip out of 10, reconsider whether the feature is worth building

This validation can be done as a standalone script (~50 lines) before touching any production code.

---

## Open Questions

1. **`unsigned char` size=1 ≠ always bool**: Some `unsigned char` members are actual byte values (flags, bitfields). Treating all size-1 `unsigned char` as bool (0/1) is usually safe but may miss cases where the original value was e.g. 0xFF. Low risk — 0 and 1 are always valid for both interpretations.

2. **Inherited member traversal**: RB2 dump has parent class names. `collect_all_members` must recursively gather parent members (walking inheritance). The `get_member_at_offset` API in `rb2_dwarf.py` already does this — reuse that logic.

3. **Member overlap/gaps**: RB2 dump may have gaps between members (padding, alignment) or members that overlap parent data. The fill strategy handles this: gaps get fill_byte, parent members get typed values at their correct offsets.

4. **RB2 vs DC3 layout drift**: For DC3-specific classes or classes that evolved significantly, RB2 offsets may be wrong. This would cause typed fixtures to write values at wrong offsets — potentially worse than uniform fill. Mitigation: if a function diverges MORE with typed fixtures than without, the layout is probably wrong. The prober naturally handles this since typed runs are mixed with non-typed runs.
