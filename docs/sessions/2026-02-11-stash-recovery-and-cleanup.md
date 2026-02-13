# Stash Recovery & Cleanup (2026-02-11)

## Background

A previous agent ran `git clean` on the repo, causing a major regression. Work was saved across 16 git stash entries. This session audited all stashes, extracted valuable content, and cleaned up duplicates.

## What Was Recovered

### Flow::PostLoad (from stash@{14}, now @{9})

Replaced a 7-line stub with the full ~165-line implementation. Match: 0% -> **73.7%** (1936 bytes).

The full implementation covers:
- Proxy vs non-proxy loading paths
- Dynamic property deserialization (rev < 5 vs rev >= 5)
- `kDataObject` property loading via `FlowNode::LoadObjectFromMainOrDir`
- Legacy revision support (rev < 3): event providers, trigger/stop events, `PropTriggerDefn` lists
- Post-load fixups: `unk170` -> `mPrivate`, `RefreshPortLabelLists`, inline proxy override

Also added:
- 4 includes: `obj/DirLoader.h`, `obj/ObjPtr_p.h`, `utl/MakeString.h`, `<list>`
- `BinStream& operator>>(BinStream&, Flow::DynamicPropertyEntry&)` definition

**Remaining work on PostLoad:** At 73.7%, there are likely register allocation or control flow differences to investigate. The function is large (1936 bytes) so incremental matching improvements are expected to be slow.

### Tooling Review Appendix (from stash@{2}, now @{2})

The doc at `docs/sessions/2026-02-09-tooling-review-code-authoring.md` was missing its ~237-line "Appendix: Data-Backed Assessment" section with concrete numbers from `decomp.db`. Restored to full 736-line version.

### objects.json Matching Flags (from stash@{13}, now @{8})

162 compilation units had been regressed from "Matching" to "NonMatching" in `config/373307D9/objects.json`. All 162 were verified at 100% match via objdiff and restored. Linked files: 1 -> **163** (0.02% -> 1.99%).

### Orchestrate Path Fix (stash@{5}, now @{5}) -- NOT applied

The stash changed `sys.path.insert(0, str(_project_root / "scripts"))` to `sys.path.insert(0, str(_scripts_dir))` in `bin/orchestrate`. This is **wrong** -- `_scripts_dir` resolves to `bin/` but the `orchestrator` package lives in `scripts/orchestrator/`. HEAD was already correct.

## Stash Cleanup

Dropped 6 stashes (16 -> 10):
- @{15}: duplicate of @{14}
- @{12}: empty test-objdiff-workflow
- @{11}, @{10}, @{9}, @{8}: four duplicate "Rename unknown fields" stashes, all superseded by HEAD

## Remaining Stash Inventory (post-cleanup)

| Index | Base Branch | Description | Value |
|-------|------------|-------------|-------|
| @{0} | dev | more pgoress | SkeletonClip/TexMovie/Str.h WIP |
| @{1} | main | more movedir | Mic.h layout explorations |
| @{2} | usb-midi-guitar-fix | NavList sorting | Tooling review appendix (extracted), progress script diffs |
| @{3} | (detached) | RhythmBattle::OnBeat | MCP config + UILabel alternative approaches |
| @{4} | dev (recovered) | regression fixes | Codegen alternatives for CharBones, AsyncFile, Bitmap, UI.cpp |
| @{5} | (detached) | automated patch push | merged-symbols work + orchestrate script changes |
| @{6} | dev | more | HamIKSkeleton alternative approach |
| @{7} | dev | temp stash for refactor | UIListState branchless, System.cpp branchless codegen |
| @{8} | test-objdiff-workflow | small progress | objects.json flags (extracted) |
| @{9} | freeqaz/wip-lazer-meta-ham | WIP snapshot | Flow::PostLoad (extracted) + other WIP |

## Codegen Alternatives Worth Testing Later

These alternative implementations were found in stashes and may help reach 100% match on specific functions:

| Function | Alternative | Stash |
|----------|------------|-------|
| `CharBones::ShortVector3::ToShort` | Precomputed constant `0.039674062281847f` | @{4} |
| `AsyncFile::Seek` | `int newPos` vs `unsigned int` | @{4} |
| `Bitmap::ConvertColor` / `DecodeDxtColor` | Restructured branches | @{4} |
| `UI::IsGameScreenActive` | No-goto version | @{4} |
| `UIListState::SetSelected` | Branchless `sign_val >> 31` XOR | @{7} |
| `System.cpp gUsingCD` | Branchless mask pattern | @{7} |
| `CharBonesMeshes::PoseMeshes` | `char*` arithmetic instead of typed pointer | @{9} |
| `ClipDistMap::FindBestNode` | Alternative structure | @{9} |
| `Str.h StackString` | Different inheritance order | @{9} |
