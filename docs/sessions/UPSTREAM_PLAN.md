# Upstream Contribution Plan

Tracking forks and local changes that should be pushed upstream.

## 1. Ghidra VMX128 (highest impact) — REBASED + PUSHED

**Upstream issue:** [NationalSecurityAgency/ghidra#2094](https://github.com/NationalSecurityAgency/ghidra/issues/2094) (open since 2020)

**GitHub fork:** https://github.com/freeqaz/ghidra (fork of `NationalSecurityAgency/ghidra`)

**Branch:** [`vmx128`](https://github.com/freeqaz/ghidra/tree/vmx128) — rebased onto latest upstream `master` (`82076c183d64`)

**Local repo:** `~/code/milohax/vmx128-research/ghidra-upstream/`

**Structure:** 11 commits on `vmx128` branch:
- 10 cherry-picked from `0dinD/ghidra` vmx128 branch (structural VMX128 SLEIGH definitions, Xenon processor variant)
- 1 ours (`56ec2c4bcf`): full pcode semantics for all 77 VMX128 opcodes

**Our commit adds:** ~2,550 lines across 2 files:

| File | Changes |
|------|---------|
| `vmx128.sinc` | 86 pcodeop definitions, full/stub pcode semantics for all 77 VMX128 opcodes, 128-bit register sizes, immediate field extractions (vpermwi128, vpkd3d128, vrlimi128), D3D type support |
| `altivec.sinc` | ~148 lines of fixes/compatibility changes |

**Validated:** 13,836 VMX128 instructions decoded correctly on DC3 binary. See `docs/vmx128/COMPARISON_REPORT.md`.

### Steps

- [x] Fork `NationalSecurityAgency/ghidra` under `freeqaz`
- [x] Cherry-pick 0dinD's 10 VMX128 commits onto latest upstream `master`
- [x] Commit our pcode semantics on top
- [x] Push `vmx128` branch to `freeqaz/ghidra`
- [ ] Coordinate with 0dinD — their branch is the foundation; either PR to their fork or collaborate on a joint upstream PR
- [ ] Open PR to upstream Ghidra referencing issue #2094
- [ ] Include test evidence (headless comparison results from `docs/vmx128/COMPARISON_REPORT.md`)
- [ ] Verify SLEIGH compilation succeeds in upstream CI (Gradle build)

### Notes

- 0dinD's original branch was based on `Ghidra_12.0_build` tag; our rebase brings it to current `master` (post-12.0.1)
- The `ppc_64_xenon.slaspec` and `ppc.ldefs` entry for `PowerPC:BE:64:Xenon` come from 0dinD's commits
- Our additions are the bulk of the pcode semantics and register fixes
- D3D pack/unpack (`vpkd3d128`, `vupkd3d128`) remain as pcodeop stubs — acceptable for upstream
- Old shallow clone still at `~/code/milohax/vmx128-research/ghidra-vmx128/` (can be removed)

---

## 2. pyghidra-mcp (XEX support; RTTI follow-on)

**Upstream:** `clearbluejar/pyghidra-mcp`

**Active checkout:** `../pyghidra-mcp/`

**Historical local copy:** `tools/pyghidra-mcp-fork/` — vendored inside dc3-decomp. Keep only until references are cleaned up and the vendored copy is removed.

**Our changes:** XEX (Xbox 360 executable) detection and automatic language selection:

| File | Changes |
|------|---------|
| `context.py` | XEX2 magic number detection (`0x58455832`), automatic `PowerPC:BE:64:Xenon` language/compiler spec |
| `server.py` | XEX detection logic |

**GitHub fork:** https://github.com/freeqaz/pyghidra-mcp

**Branch:** `feature/xex-support` — 1 commit (`9f5dc13`): XEX2 detection + PowerPC:BE:64:Xenon language auto-selection

**Important scope note:** the current upstreaming work is about XEX import, map-backed symbol lookup, and related decomp workflow improvements. Automated RTTI/class-hierarchy recovery is a separate follow-on effort, tracked in `docs/plans/PYGHIDRA_MCP_RTTI_RECOVERY.md`.

### Steps

- [x] Fork `clearbluejar/pyghidra-mcp` under `freeqaz`
- [x] Diff our vendored copy against upstream to extract clean changesets
- [x] Commit XEX support as clean patches — applied to upstream code structure (not vendored copy)
- [ ] Open PR to `clearbluejar/pyghidra-mcp`
- [ ] Once merged upstream, remove `tools/pyghidra-mcp-fork/` from dc3-decomp and switch to the upstream package
- [ ] After vendored-copy removal, open a separate RTTI/class-recovery workstream against `../pyghidra-mcp`

---

## 3. m2c (Xbox 360 Xenon + MSVC symbols)

**Upstream:** `matt-kempster/m2c`

**GitHub fork:** https://github.com/freeqaz/m2c

**Branches with changes (pushed to origin):**

| Branch | Commits | Description |
|--------|---------|-------------|
| `xbox-360-xenon-support` | 5 | MSVC symbol parsing + Xbox 360 Xenon CPU + VMX128 instruction support |
| `fix/msvc-symbol-parsing` | 2 | Subset: MSVC-mangled symbol handling only |

**Uncommitted changes:** modifications to `instruction.py`, `translate.py`, `test_msvc_symbols.py`.

### Steps

- [x] Clean up uncommitted changes, finalize tests — committed as `20de88a` on `xbox-360-xenon-support`
- [ ] Open PR for MSVC symbol parsing first (smaller, independently useful)
- [ ] Open PR for Xbox 360 Xenon/VMX128 support (larger, depends on MSVC fix)
- [ ] Verify tests pass on upstream CI

---

## 4. jeff (XEX splitter fixes)

**Upstream:** `rjkiv/jeff`

**GitHub fork:** https://github.com/freeqaz/jeff

**Branches with changes (pushed to `fork` remote):**

| Branch | Commits | Description |
|--------|---------|-------------|
| `fix/jump-table-crash` | 5 | XEX split fixes, jump table crash fix, absolute jump table support, unit tests |
| `fix/xex-split-asm-bugs` | 1 | Subset of the above |

### Steps

- [ ] Open PR from `fix/jump-table-crash` to `rjkiv/jeff` (includes all fixes + tests)
- [ ] `fix/xex-split-asm-bugs` is a subset — no separate PR needed

---

## 5. objdiff (analysis pattern detection)

**Upstream:** `encounter/objdiff`

**Local repo:** `~/code/milohax/objdiff/`

**Branch:** `feature/analysis-pattern-detection` — 3 local commits + uncommitted changes:

| Commits | Description |
|---------|-------------|
| `e1e04ff` | Analysis pattern detection and report subcommands |
| `6a7675e` | Typed instruction args in JSON output, improved pattern detection |
| `643482d` | Commutative op/offset swap patterns, DWARF2 line info, typed args |

**Uncommitted:** Committed as `090be59` (map file support, DWARF2 line info, typed instruction args).

**GitHub fork:** https://github.com/freeqaz/objdiff

**Branch:** `feature/analysis-pattern-detection` — 4 commits pushed to `freeqaz/objdiff`

### Steps

- [x] Fork `encounter/objdiff` under `freeqaz`
- [x] Clean up uncommitted changes and decide what to include — committed as `090be59`
- [x] Push `feature/analysis-pattern-detection` branch
- [ ] Open PR — may need discussion with maintainer since it adds significant new CLI functionality
- [ ] Consider splitting into smaller PRs (typed args, pattern detection, map file support)

---

## Priority Order

1. **jeff** — Bug fixes with tests, same maintainer as dc3-decomp, easiest win
2. **Ghidra VMX128** — Highest community impact, 5-year-old open issue
3. **m2c** — Adds a new target architecture, moderate complexity
4. **pyghidra-mcp** — Small diff, straightforward
5. **objdiff** — Largest scope, needs most cleanup and maintainer discussion
