# DC3 Decomp Style Guide

Patterns and conventions observed in the codebase. Follow these for consistency.

## List/Container Traversal

### Prefer `FOREACH` for simple iteration

```cpp
// Good
FOREACH (it, mRunningNodes) {
    if ((*it)->ClassName() != FlowLabel::StaticClassName()) {
        (*it)->RequestStopCancel();
    }
}

// Bad - raw pointer arithmetic
void *head = *(void **)((char *)this + 0x38);
void *node = head;
do {
    next = *(void **)((char *)node + 0x14);
    // ...
} while (next != NULL);
```

### Safe iteration when the loop body can modify the list

When calling methods that may add/remove nodes from the list being iterated (e.g., `RequestStop()` on children can modify `mRunningNodes`), use the manual next-iterator pattern:

```cpp
auto it = mRunningNodes.begin();
while (it != mRunningNodes.end()) {
    auto next_it = it;
    next_it++;
    (*it)->RequestStop();
    it = next_it;
}
```

### Iterator dereference

- Use `(*it)` to access the pointed-to object from `ObjPtrList` iterators
- Use `it->Obj()` when working with `ObjPtrVec` iterators that wrap object pointers

## Variable Declaration Order

Local variable declaration order affects stack layout and therefore codegen on PowerPC. When objdiff reports an OFFSET_SWAP pattern, try reordering local declarations to match the target binary's stack layout.

## Bit Manipulation (ByteGrinder-style ops)

The standard pattern for rotate-like bit operations is **shift-then-XOR**, not XOR-then-shift:

```cpp
// Standard pattern: (right_shift ^ xor_val) | (left_shift & mask)
u32 result = ((w >> 2) ^ 0x3F) | ((w << 6) & 0xC0);

// Not: (w ^ xor_val) | ((w & mask) << shift)
```

## Access Specifiers

- Keep members `protected` or `private` unless confirmed public via DWARF or asserts
- Add getters/setters for external access rather than making members public
- Use `friend` classes for closely related types (e.g., `Foo` and `FooHandle`)

## Assertions and Macros

- Do not modify `MILO_ASSERT()` call text without testing carefully -- the string content is baked into the binary
- Do not modify `OBJ_MEM_OVERLOAD` macro arguments without verification

## Macro Usage

See [MACROS.md](MACROS.md) for comprehensive macro documentation covering handlers, property sync, serialization, and more.

### Critical Macro Rules

1. **MILO_ASSERT text is frozen** - The condition string (`#cond`) is baked into the binary; changing the condition expression will cause a mismatch
2. **OBJ_MEM_OVERLOAD requires exact line numbers** - The line number parameter must match the original source
3. **Handler ordering affects codegen** - Place `HANDLE_SUPERCLASS` after local handlers; don't reorder without verification
4. **INIT_REVS placement** - Must be placed after `Save()` and `Load()` implementations, not before
5. **Load uses `d` stream** - After `LOAD_REVS`, use `d >>` and `d.stream` instead of `bs`

## General Conventions

- Use `u8`, `u32`, `s32` etc. for fixed-width types (defined in `types.h`)
- Use `Symbol` for interned strings, not raw `const char *`
- Use `String` (Milo engine string) rather than `std::string`
- Use `FOREACH` macro (from `utl/Std.h`) over manual iterator boilerplate
- Unsigned zero comparisons: use `x > 0` instead of `x != 0` (generates `ble` vs `beq` on PPC)

## Codegen-Sensitive Patterns (Do Not Change)

These affect compiled output and must be preserved exactly:

- Loop structure (`for` vs `while`, increment position)
- Conditional ordering (affects branch prediction / instruction scheduling)
- Specific type widths and casts
- `MILO_ASSERT()` string content
- Operand order in commutative operations (`a | b` vs `b | a`)
