# dtk Tail Block Merge: Proposed Fix for Curl_resolv

## Problem

`Curl_resolv` reports 94.5% match. The missing 5.5% is `Curl_resolv_timeout`, a separate function the MSVC compiler placed immediately after `Curl_resolv` with a tail call (`b`, not `bl`) back into it. dtk's CFA analysis detects this as a "tail block" and merges the two functions into one symbol in the target .obj, preventing objdiff from comparing them independently.

Additionally, 2 `diff_arg` mismatches on `Curl_cfree` (lis/lwz pair) are relocation encoding differences -- identical instructions, different reloc type in reconstructed vs compiled .obj. These are likely unfixable.

## Root Cause in dtk

**File:** `~/code/milohax/jeff/src/analysis/cfa.rs`

### `merge_tail_blocks()` (line 588)

Post-pass that runs after all functions are detected. Iterates adjacent function pairs and checks if the second is a "tail block" of the first. If so:
1. Removes the tail block's function entry from `self.functions` (line 630)
2. Adds address to `self.merged_tail_blocks` (line 632)
3. Extends predecessor's end address (line 636)

### `check_tail_block()` (line 664)

Returns `Some` when a gap between functions (max 64 bytes) meets ALL conditions:
- Has at least one backward branch (`b` or `bc`) into the preceding function
- Ends with `blr` (unconditional return)
- No `bl` (function calls) or `bctr` (indirect branches)
- No branches to addresses outside both the gap and the preceding function

For `Curl_resolv_timeout` (7 instructions, 28 bytes at 0x8256AAB8):
```
li r11, 0x0          # *entry = NULL prep
cmpwi cr6, r7, 0x0   # compare timeoutms < 0
stw r11, 0x0, r6     # *entry = NULL
bge cr6, 0x510        # if timeoutms >= 0, skip to tail call (within gap)
li r3, -0x2           # return CURLRESOLV_TIMEDOUT
blr                   # return  <-- ends_with_blr = true
b 0x310               # tail call into Curl_resolv  <-- has_backward_branch = true
```

All conditions met. Merge happens unconditionally.

### `apply()` (line 152)

The merged symbol gets renamed to `__DELETED_Curl_resolv_timeout` with flags:
`Stripped | NoWrite | NoExport | RelocationIgnore`

This means `write_symbols()` skips it, and the COFF .obj never gets a `Curl_resolv_timeout` symbol.

## symbols.txt Regeneration Flow

1. `load_analyze_xex()` at `xex.rs:468` reads symbols.txt, captures `FileReadInfo` (mtime + hash)
2. CFA runs `merge_tail_blocks()`, deletes `Curl_resolv_timeout` symbol
3. `split_write_obj_exe()` at `xex.rs:239` calls `write_symbols_file()`
4. `write_if_unchanged()` at `config.rs:214` checks mtime -- if file unchanged since read, writes the regenerated version (without the merged symbol)

The mtime check only preserves user edits made AFTER the split finishes. Edits made before `touch config.yml && ninja` get overwritten because the split reads the file fresh.

## Proposed Fix

In `merge_tail_blocks()`, skip merging when the candidate function has a `scope:global` symbol. A global scope means the user or map file explicitly defined it as a real exported function, not an auto-discovered artifact.

**Location:** `cfa.rs` line ~613, before the `check_tail_block()` call.

**Proposed guard:**
```rust
// Skip merging if the candidate has a global-scope symbol
// (user or map file explicitly defined it as a real function)
if let Ok(Some((_, sym))) = obj.symbols.kind_at_section_address(
    func_addr.section,
    func_addr.address,
    ObjSymbolKind::Function,
) {
    if sym.flags.scope() == ObjSymbolScope::Global {
        continue;
    }
}
```

**API references used:**
- `obj.symbols.kind_at_section_address(section, addr, kind)` at `symbols.rs:384` -- returns the symbol at an address filtered by kind
- `sym.flags.scope()` at `symbols.rs:62` -- returns `ObjSymbolScope::{Global, Local, Weak, Unknown}`
- `ObjSymbolScope::Global` at `symbols.rs:27`

## Risks / Things to Verify

1. **False positives**: Are there global-scope symbols that ARE legitimate tail blocks? Map files might mark internal helper functions as global. Need to check if any of the other ~9 tail block merges in the project have global scope.

2. **Scope source**: Where does the global scope come from for auto-discovered functions? If CFA auto-detection sets scope to global by default, this guard would be useless. Need to verify that only map file / user-defined symbols get `scope:global`.

3. **Other projects**: This is a shared tool. The fix should be safe for GameCube/Wii projects too, not just Xbox 360.

4. **Alternative approaches**:
   - A dedicated `nomerge` flag in symbols.txt instead of overloading `scope:global`
   - A config option like `preserve_global_tail_blocks: true`
   - Checking if the symbol came from the user's symbols.txt vs auto-discovery

## Verification Plan

After applying the fix:
1. Rebuild dtk: `cd ~/code/milohax/jeff && cargo build --release`
2. Copy binary: `cp target/release/dtk ~/code/milohax/dc3-decomp/build/tools/dtk`
3. Add symbol to symbols.txt:
   ```
   Curl_resolv_timeout = .text:0x8256AAB8; // type:function size:0x20 scope:global
   ```
4. Re-split: `touch config/373307D9/config.yml && ninja`
5. Verify symbol survives: check target .obj for `Curl_resolv_timeout` in COFF symbol table
6. Verify Curl_resolv match improves (should drop the 7 delete instructions)
7. Check ALL other tail block merges still work (grep build log for "Merging tail block")
8. Run full `ninja build/373307D9/report.json` and diff against previous report

## Current State

- `Curl_resolv` named correctly in symbols.txt (was `fn_8256A8D0`)
- `Curl_resolv_timeout` at 0x8256AAB8 confirmed in map file
- Source code in `src/system/net/curl/lib/hostip.c` is correct
- 94.5% match reported, effectively at tooling limit without this dtk fix
