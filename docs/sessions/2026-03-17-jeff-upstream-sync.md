# Jeff Upstream Sync Analysis — 2026-03-17

Analysis of divergence between our jeff fork (`freeqaz/jeff`, branch `main` at `521027a`)
and upstream (`rjkiv/jeff`, branch `main` at `1f9a699`). Fork point: `c2fb03a`.

## Status Summary

| Direction | Commits | Character |
|-----------|---------|-----------|
| Our fork ahead of upstream | 128 | CFA redesign, VM rearchitecture, COMDAT/ICF fixes, CRT splitting |
| Upstream ahead of our fork | 15 | GC/Wii removal, LZX decompression, pdata fixes, code cleanup |

Both sides share the same fork point (`c2fb03a`). Our fork also includes the
`fix/xex-split-asm-bugs` PR that was merged upstream as commit `60cdaec`.

---

## Upstream Changes We're Missing (15 commits)

### Must-Have for DC3

#### 1. LZX Decompression (`fe80ef3` + `b42ce3c` + `a0671c0`)
- **What**: Implements actual LZX decompression for compressed XEX files. Previously
  `bail!("LZX not supported")`. Adds `lzxd = "0.2.6"` dependency.
- **Why we want it**: DC3's target binary is uncompressed (debug build), but other
  Xbox 360 titles we may analyze use LZX compression. General robustness.
- **Conflict risk**: Low — touches `xex.rs` decompression path, orthogonal to our
  CFA/COMDAT changes.

#### 2. pdata Auto-Split Fix (`1f9a699`)
- **What**: Clears existing `.pdata` splits before regenerating them via `split_pdata()`.
  Previously could produce duplicate/overlapping splits. Also removes unused `ObjInfo`
  fields (`split_meta`, `entry`) and unused `ObjUnit` field (`comment_version`).
- **Why we want it**: We rely on pdata for function boundary detection. Incorrect pdata
  splits could cause silent CFA errors.
- **Conflict risk**: Medium — touches `split.rs`, `obj/mod.rs`, `cfa.rs` (removes
  `locate_sda_bases` and `locate_bss_memsets`). Our `split.rs` has heavy CRT-splitting
  additions but in different regions. `cfa.rs` is completely rewritten in our fork.

### Nice-to-Have

#### 3. GC/Wii Removal (`c1b1d95`)
- **What**: Deletes ~12,600 lines — `dol.rs`, `dwarf.rs`, `elf.rs`, `rel.rs`, `rarc.rs`,
  `u8_arc.rs`, `wad.rs`, `signatures.rs`, `comment.rs`, `bin2c.rs`, `extab.rs`, `lcf.rs`,
  `ncompress.rs`, `disc.rs`, `shasum.rs`, plus VFS layers. Removes 7 Cargo dependencies.
- **Why**: We're Xbox 360-only. Less code to maintain, faster builds. But our fork still
  has these files and they're inert — not causing problems.
- **Conflict risk**: Low (pure deletions), but adds merge noise.

#### 4. Code Cleanup (`97bea7d`, `9213a9f`, `818a414`, `856e1cc`, `7bd3a51`, `80fadbc`, `0e44584`)
- **What**: README refactor, badge updates, default exe name, removed unused includes,
  readme typo fix. Cosmetic.
- **Conflict risk**: None.

#### 5. Rust API Modernization (spread across commits)
- **What**: `&Vec<u8>` → `&[u8]`, explicit `return` removal, `unwrap()` simplifications.
- **Why**: Better Rust idioms. Not functional.

---

## Our Fork's Unique Changes (128 commits)

### Critical Code (would break DC3 decomp to lose)

| Commit | Feature | Lines | Impact |
|--------|---------|-------|--------|
| `81b41db` | ICF symbol naming — keep original mangled names instead of `merged_<addr>` | ~50 | Linker needs real symbols for hybrid linking |
| `4578093` | CFA bounds checking — skip malformed symbols instead of crashing | ~160 | Production robustness |
| `cf01a80` | COMDAT code-sections-only — data/rdata stay in parent section | ~30 | Prevents vtable relocation loss |
| `6c709ec` | CFA redesign — `AnalyzerState` → `CfaConfig` + `CfaResult` | ~1,700 | Better architecture, testable phases |
| `c6987b4` | VM type rearchitecture — `Gpr/GprValue/GprSource` → `RegState/Value/Provenance` | net -150 | Simpler register tracking |
| `d330415` | CRT initializer splitting + `.CRT$XCU` section renaming | ~190 | Correct startup order via `_initterm()` |
| `8de8375` | REL24 addend preservation for split/COMDAT offsets | ~40 | Relocation correctness |

### Completed & Cleaned Up

| Commit | Feature | Lines | Notes |
|--------|---------|-------|-------|
| `d682ee9` | Remove shadow VM + pipeline abstraction | -4,840 | Phase rollout completed; dead code removed |
| 66+ commits | Phase r1–r7 CFA validation rollout | docs | Historical; the working state is what remains |

### Test Infrastructure (additive, no conflicts)

- `cfa_tests.rs` (969 lines), test assets, validation scripts
- Jump table confidence scoring (`JumpTableConfidence`, 9-point system)
- `.rdata` jump table support for Xbox 360

---

## Conflict Analysis

### High-Conflict Files

| File | Upstream Change | Our Change | Resolution |
|------|----------------|------------|------------|
| `src/analysis/cfa.rs` | Removes `locate_sda_bases`, `locate_bss_memsets` (~115 lines) | Complete rewrite (675→1,742 lines) | **Take ours**. Upstream's deletions are already gone in our rewrite |
| `src/util/split.rs` | Removes `SplitMeta`, adds `sec.splits.clear()` for pdata | Adds CRT splitting, `globalize_symbols` param, `is_crt_array_symbol()` | **Merge carefully** — cherry-pick `splits.clear()` fix into our version |
| `src/cmd/xex.rs` | Uses old `AnalyzerState` API, adds LZX | Uses `run_cfa()`/`apply_cfa()` API | **Keep our API, port LZX code** |
| `Cargo.toml` | Removes 7 deps, adds `lzxd` | Version 1.9.2, keeps all deps | **Add `lzxd`, optionally remove unused deps** |
| `src/obj/mod.rs` | Removes `split_meta`, `entry`, `comment_version` | Untouched | **Take upstream's field removals** (we don't use them either) |

### Low-Conflict / Orthogonal

| File | Status |
|------|--------|
| `src/obj/relocations.rs` | Both sides have identical unaligned relocation fix — clean merge |
| `src/obj/splits.rs` | Upstream adds `clear()` method — we don't modify this file — clean merge |
| GC/Wii file deletions | Pure deletions — can accept or skip |

---

## Recommended Merge Strategy

### Option A: Selective Cherry-Pick (recommended)

Minimizes risk. Pull only the valuable changes from upstream without touching our
architecture.

**Phase 1 — LZX decompression** (low risk)
```bash
# In jeff repo, from our main branch
git cherry-pick fe80ef3   # LZX decompression implementation
git cherry-pick b42ce3c   # LZX window size cleanup
git cherry-pick a0671c0   # min() import cleanup
```
These touch `xex.rs` decompression path only. Our CFA/split changes are in different
code regions. Expect minor context conflicts from our `&Vec<u8>` → `&[u8]` differences.

**Phase 2 — pdata fix** (medium risk)
```bash
# Manual port — don't cherry-pick 1f9a699 directly (too many file touches)
```
Manually add `sec.splits.clear()` call in our `split_pdata()` function. Also remove
`split_meta`, `entry`, `comment_version` fields from `ObjInfo`/`ObjUnit` if we're not
using them. The `cfa.rs` changes in this commit (removing `locate_sda_bases`) are
already gone in our rewrite.

**Phase 3 — Cargo.toml cleanup** (low risk)
- Add `lzxd = "0.2.6"` dependency
- Optionally remove deps we don't use: `base16ct`, `base64`, `cbc`, `indent`,
  `orthrus-ncompress`, `rayon`, `size`, `gnuv2_demangle`, `sanitise-file-name`
- Keep `snafu` if any of our code uses it

**Phase 4 — GC/Wii removal** (optional, low priority)
- Accept or skip `c1b1d95` depending on whether we want to maintain multi-platform
  capability. These files are inert for Xbox 360 work.

### Option B: Full Merge

```bash
git merge remotes/origin/main
```

Expect conflicts in: `cfa.rs` (complete rewrite), `split.rs` (both modified),
`xex.rs` (both modified), `Cargo.toml` (version + deps). Resolution: take our
versions for all conflicted files, then manually port the LZX and pdata fixes.

This is essentially the same work as Option A but with more merge noise.

### Option C: Rebase Our Work Onto Upstream

Not recommended. 128 commits with heavy restructuring would be painful to rebase.
The CFA rewrite (`6c709ec`) alone would conflict with nearly every upstream change
that touches analysis code.

---

## Concurrent Work Considerations

The jeff maintainer (rjkiv) is the same person as the dc3-decomp maintainer. Their
upstream changes suggest a direction toward:
1. **Xbox 360 focus** (removing GC/Wii code)
2. **Simpler architecture** (keeping `AnalyzerState` monolithic)
3. **Incremental fixes** (pdata, unused fields)

Our fork went the opposite direction architecturally (CFA redesign, VM rearchitecture).
Both approaches produce correct output for DC3. The question is whether to propose
our CFA architecture upstream or keep it as a fork divergence.

**Recommendation**: Keep our CFA architecture. It's better-tested (969 lines of tests)
and more maintainable (config/result separation). If rjkiv wants it upstream, we can
PR it. For now, cherry-pick their fixes into our fork.

---

## Validation Plan

After any merge, rebuild jeff and re-split the DC3 XEX:

```bash
cd ~/code/milohax/jeff
cargo build --release

cd ~/code/milohax/dc3-decomp
scripts/build/rebuild_jeff_link.sh    # re-split + relink
ninja                                 # full rebuild
scripts/measure_progress.sh --functions --detailed HEAD  # verify no regressions
```

Compare `build/373307D9/report.json` before and after — total function count and
match percentages should be identical (or improved if pdata fix catches edge cases).

---

## Action Items

- [x] Implement LZX decompression (ported from `fe80ef3`, `b42ce3c`, `a0671c0`)
- [x] Add `lzxd = "0.2.6"` dependency to `Cargo.toml`
- [x] Add `ObjSplits::clear()` method (from `1f9a699`)
- [x] Fix `split_pdata()` to clear before regenerating (from `1f9a699`)
- [x] Add tests: `test_obj_splits_clear`, `test_split_pdata_clears_existing_splits`, `test_split_pdata_idempotent`
- [x] Add LZX tests: `test_lzx_decompression_round_trip`, `test_lzx_decompression_truncated_block`, `test_lzx_decompression_block_too_small`, `test_lzx_decompression_zero_chunk_terminates`
- [x] Build + test: 97/97 tests passing
- [x] Re-split DC3 XEX and verify zero regressions: 40.626553%, 47030 functions, 27238 matched (identical)
- [ ] Remove unused `split_meta`/`entry`/`comment_version` fields — BLOCKED: still used by GC/Wii code (elf.rs, dol.rs, map.rs)
- [ ] Decide on GC/Wii file removal (low priority)
- [ ] Consider PR of our CFA architecture to upstream (long-term)
