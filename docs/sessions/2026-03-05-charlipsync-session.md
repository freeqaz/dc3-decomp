# CharLipSync Unit Session — 2026-03-05

## Summary

Focused on CharLipSync and related functions. Key header fix (`vector<String>` → `vector<FilePath>`) had cascading positive effects across multiple functions.

## Results

| Function | Before | After | Status |
|----------|--------|-------|--------|
| Generator::RemoveViseme | 43.1% | 100% | COMPLETE |
| Generator::NextFrame | 87.5% | 100% | COMPLETE |
| Generator::Print | 62.9% | 92.8% | AT_LIMIT |
| PlayBack::Poll | 85.5% | 96.5% | AT_LIMIT |
| PlayBack::Set | 58.7% | 93.2% | AT_LIMIT |
| CharLipSyncDriver::SetLipSync | 73.5% | 94.7% | AT_LIMIT |

## Changes & Why They Helped

### 1. Member type correction: `vector<String>` → `vector<FilePath>` (header)

**Impact**: Affected all functions that touch `mVisemes`. FilePath extends String with no extra members (both 8 bytes), but generates different template instantiations — `vector::_M_erase<FilePath>` vs `vector::_M_erase<String>`, different `MakeString` specializations, etc.

**Detection method**: Ghidra showed `_M_erase` on `vector<FilePath>` in the target binary. Confirmed by examining `erase()` call signatures in objdiff.

### 2. Local reference binding: `std::vector<unsigned char> &data = lipSync->mData`

**Impact**: In RemoveViseme, eliminated double-indirection (`this->mLipSync->mData[cur]` on every access). The local reference `data` gets a callee-saved register, reducing loads per iteration.

**Detection method**: Ghidra decompilation showed a local variable aliasing the data vector.

### 3. Assert expression text matching

**Impact**: `MILO_ASSERT(data[cur] < ...)` vs `MILO_ASSERT(lipSync->mData[cur] < ...)` generates different `MakeString<>` template instantiation due to `#cond` stringification — different string lengths = different function calls = different branch targets.

**Detection method**: Comparing MakeString template parameter lengths in objdiff output.

### 4. Integer division in NextFrame: `count / 2`

**Impact**: Each viseme entry is 2 bytes (index + weight), so the count of entries = `(size - 1 - lastCount) / 2`. Missing `/2` generated completely different arithmetic (no `srwi` shift-right-by-1).

**Detection method**: Ghidra showed `(uint)lVar3 >> 1` in decompilation.

### 5. Float cast on math library return: `(int)(float)ceil(frame)`

**Impact**: `ceil()` returns `double` on PPC. The explicit `(float)` cast adds an `frsp` (round-to-single-precision) instruction before the `fctiwz` integer conversion, matching the target.

**Detection method**: objdiff showed missing `frsp` instruction.

### 6. Constructor float initialization in PlayBack::Weight

**Impact**: Adding `mPrevWeight(0), mNextWeight(0), mCurWeight(0)` to the Weight constructor changes how `vector<Weight>::resize()` initializes the prototype element, generating `stfs` stores for the float members.

**Detection method**: Missing float stores in vector initialization code.

### 7. Loop condition re-read via `mLipSync->mFrames` (double deref)

**Impact**: Using `mLipSync->mFrames` (through `this->mLipSync->mFrames`) each iteration instead of caching to a local generates different load patterns — the compiler re-loads through the pointer chain.

**Detection method**: Ghidra showed the loop condition reads through two indirections.

## Permuter Improvement Analysis

Of the 7 changes, only 3 are realistically automatable as permuter patterns. The rest require semantic understanding (correct types, data structure knowledge, constructor layout) that belongs in Ghidra-powered diagnostics instead.

### Automatable as Permuter Patterns

1. **Math return cast** — `float_double_literal` only handles literal suffixes, not `(float)ceil(x)` or `(float)sqrt(x)` style casts. Detection: `frsp` near `fctiwz` in target.
2. **Deep member ref binding** — `member_ref_bind` handles `this->mFoo` but not `obj->mSubMember` (chained pointer dereference binding). Detection: repeated lwz chains through same pointer.
3. **Loop condition caching/uncaching** — no pattern for cached vs uncached member access in loop conditions.

### Better Served by Ghidra Diagnostics (not permuter patterns)

4. **Type correction** (vector<String> → vector<FilePath>) — requires knowing the correct type from the binary
5. **Integer division** (/ 2) — requires understanding data structure semantics
6. **Constructor initializer expansion** — requires knowing which members should be initialized
7. **Assert text matching** — requires comparing MakeString string lengths from binary

These should be integrated into `analyze_function` / `ghidra-decompile` as diagnostic warnings during decomp work.
