---
name: vtable
description: Dump vtable layout for a class from original COFF .obj files. Maps each slot to the actual virtual function symbol AND its declaration-order name from the class header, so ICF-merged entries (OnlyReturns / merged_Returns1 / etc.) are still identifiable. Use when debugging vtable offset mismatches in objdiff.
argument-hint: "[class-name] [--offset 0xNN | --diff-pair 0xTGT 0xSRC]"
allowed-tools: Bash(python3 scripts/dump_vtable.py *), Bash(python3 -c *), Read, Grep, Glob
---

# Vtable Dump Skill

Dump vtable layouts from original .obj files to identify which virtual function lives at each slot. The output has two columns:

- **Symbol**: the actual function symbol bound to the slot in the .obj reloc table. For ICF-merged stubs this is misleading (e.g. `OnlyReturns`, `merged_Returns1`, `?Draw@RndDrawable@@UAAXXZ` reused in unrelated slots).
- **Annotation**: the virtual function name inferred from the class header and parent-class chain. This is what the slot actually represents.

Always trust the Annotation column over the Symbol column when they conflict.

## Arguments

`$ARGUMENTS`

## Steps

1. **Parse the arguments.** `$0` is a class name. Optional flags:
   - `--offset 0xNN` — show a single slot at byte offset NN
   - `--diff-pair 0xTGT 0xSRC` — show two slots side-by-side. Use this when objdiff reports a vcall mismatch like `lwz r12, 0x14(r12)` (target) vs `lwz r12, 0x18(r12)` (source).
   - `--obj <path>` — override auto-detected .obj path (auto-detection sometimes fails for classes whose header name doesn't match the .obj basename, e.g. RndDrawable lives in `Draw.obj`)
   - `--demangle` / `-d` — demangle the Symbol column to `Class::Method`

2. **For a virtual-call mismatch (most common case)**:
   ```bash
   python3 scripts/dump_vtable.py $0 --diff-pair 0xTGT 0xSRC
   ```
   The summary line tells you which virtual the target binary dispatches to and which one the decomp source dispatches to. If they differ, change the source code's virtual call site (or fix the virtual declaration order).

3. **For a single-slot lookup**:
   ```bash
   python3 scripts/dump_vtable.py $0 --offset 0xNN
   ```

4. **For a full dump**:
   ```bash
   python3 scripts/dump_vtable.py $0
   ```

5. **From Python** (used by other scripts):
   ```python
   import sys; sys.path.insert(0, 'scripts')
   from dump_vtable import lookup_vtable_offset
   entry = lookup_vtable_offset('RndDrawable', 0x14)
   # -> {'slot': 5, 'offset': 20, 'symbol': '?Draw@RndDrawable@@UAAXXZ', 'demangled': 'RndDrawable::Draw'}
   ```

## How the Annotation column works

For class `X` with parent chain `X -> Y -> ... -> Hmx::Object`:
- The vtable's first N slots are the parent chain's slots in their layout order; annotation = `[inherited from <where declared>] <method name>` or `[Hmx::Object] <method name>` for the 22 fixed Object slots.
- Slots beyond N are `X`'s own newly-declared virtuals (in header declaration order); annotation = `[new in X] <method name>`.
- Virtuals declared with `override` or that match a parent's virtual name are treated as overrides (reuse parent's slot).
- For classes that virtually inherit (e.g. RndDrawable : public virtual RndHighlightable), the primary `??_7Class@@6B@` vtable contains ONLY the class's new virtuals; the inherited Object slots are in a separate `??_7Class@@6BObject@Hmx@@@` subobject vtable.

The annotator falls back to no annotation (rather than guessing) when it can't prove the mapping.

## Tips

- The Symbol column may show an unrelated ICF-merged function (`OnlyReturns`, `?SetEngine@CTrigramStore@...`, `?CharAdvance@RndFontBase@...`). These are linker-folded placeholders — the slot's true purpose is shown in the Annotation column.
- Vtable byte offsets: each slot is 4 bytes on PPC.
- The `??_R4` entry is RTTI metadata, not a virtual slot. It's labelled `[R4]` in the dump rather than getting a slot number.
- `dump_vtable.py resolve <class> <sub_offset> <slot>` is the legacy subcommand for the `resolve-vcall` skill; it still works.

## When to Use

- objdiff shows `diff_arg` on `lwz` instructions with different offsets in vtable load patterns
- You need to identify which virtual function corresponds to a vtable slot
- ICF-merged symbols (`OnlyReturns`, `merged_Returns1`) make it unclear which virtual is being called
- Before/after changing virtual function call ordering to verify slot correctness
