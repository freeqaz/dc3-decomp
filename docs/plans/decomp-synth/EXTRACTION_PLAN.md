# decomp-synth — Extraction Plan

Split the custom C++ source permuter at `scripts/permuter/` into its own public
repository, **`decomp-synth`** (canonical checkout: the sibling `../decomp-synth`),
reusable across decomp projects. It must serve **three** in-house consumers from
day one — **rb3** (RB3, Wii, MetroWerks `mwcceppc`), **rb3-xenon** (RB3, Xbox 360,
MSVC PPC), and **dc3** (Dance Central 3, Xbox 360, MSVC PPC) — and be installable
by strangers against an open target. Consumers **install** it (shared venv); they
do not embed or symlink it (see §6).

> **Status:** design review complete (2026-05-28). This revision is backed by a
> five-track audit of the actual tree; findings and file:line evidence are inline.
> The headline change from the first draft: the project abstraction **conflates
> game identity with toolchain**, which blocks rb3-xenon, and there is already a
> monkeypatch shim in production proving it. See §1 and the
> [audit appendix](#appendix-audit-findings).
>
> **Progress (2026-05-29): extraction complete.** The standalone repo lives at
> `../decomp-synth` (fresh git history). All workstreams landed:
> - **§1** project model config-driven from `decomp-synth.json` (game×toolchain
>   split, `obj_layout`, `scorer.py` re-keyed to `toolchain`); shim deleted;
>   dc3 byte-identical, rb3-xenon resolves flat objs with no shim.
> - **§2** decoupled: vendored `diff_inspect`, host hooks optional, `decomp_synth`
>   package imports standalone in a clean tree-sitter-only venv.
> - **§3/§4** `pyproject.toml` (MIT, tree-sitter trio, `decomp-synth` console
>   script), tool paths config/env-driven, `$DECOMP_SYNTH_REPO`/`--repo-root`.
> - **§5** 2 game-specific patterns dropped (114→112), Milo vocab centralized in
>   `dialect.py`, DBs schema-only (auto-create, no seed).
> - **§7** LICENSE + README + examples; game symbols / machine paths scrubbed.
> - **§8** portable lane green (`pytest -m "not integration"`: 1875 passed, 86
>   integration-marked); GitHub Actions CI.
> - **§6** installed editable into the shared venv; all three repos verified
>   consuming it.
>
> **Cutover complete (2026-05-29).** All three repos now consume the installed
> `decomp_synth` (editable in the shared venv); the in-tree `scripts/permuter/`
> was removed from dc3 and `decomp_synth` is the sole source. Commits: dc3
> `855f3582` (inbound consumers + skill + docs flipped off `scripts.permuter`) and
> `03960f53` (in-tree deletion), rb3-xenon `eed3c94`, rb3 `93439c5` (symlinks
> removed). The extraction is **done** — this doc is retained for posterity; the
> canonical copy lives in the `../decomp-synth` repo.

## Why "decomp-synth"

The tool is not a blind permuter (cf. simonlindholm's `decomp-permuter`, which is
C-only and archived at `../decomp-permuter` as incompatible). It is **guided,
search-based source synthesis**: it reconstructs candidate C++ from m2c output,
Ghidra hints, a learned strategy DB, and beam/hill-climb search, then drives
toward a byte-exact object match. The name claims that differentiator and keeps
the `decomp-` ecosystem prefix (`decomp-permuter`, `decomp-toolkit`/dtk,
decomp.me). *(Pre-launch: confirm `decomp-synth` is free on PyPI and GitHub — a
sandboxed check was inconclusive.)*

## What we're extracting — true scope

The first draft's "~29k LOC across ~60 modules" counts **only the top-level
modules**. The real tree is ~4× that:

| Area | Files | LOC | Notes |
|---|---|---|---|
| Core engine (top-level `*.py`) | 60 | 29,273 | search, extract, compose, score, project abstraction |
| `patterns/` | 124 | 43,800 | **largest + most sensitive** — the transformation rule library |
| `tests/` | ~110 | 39,719 | 1,966 tests; mostly synthetic fixtures |
| `bench/` | ~20 | 2,503 | A/B harness + **checked-in IP-bearing result JSON** |
| `experiments/` | 2 | 220 | dev throwaway — does not ship |
| **Total** | **~300** | **~115,515** | excludes `__pycache__` |

The practical takeaway: **`patterns/` is the crown jewel and the biggest IP
surface, not the core.** The plan must treat it as a first-class workstream, not
a footnote (§5).

### Already partway to generic

`project.py` has a `ProjectConfig` dataclass and a `ProjectType` enum, a compiler
abstraction (`mwcc | msvc`), and per-project `permuter.json`. `repo_paths.py`
detects roots via `SZBE69_B8` (RB3), `373307D9` (DC3), or `objdiff.json`. And a
**second, fully config-driven** plane already exists: `compiler_dialect` is read
from `permuter.json` (`project_config.py`) and threaded through ~20 patterns that
gate C++11-vs-C++98 syntax — *this is the model the rest should follow.* The bones
of a generic tool exist; the work is finishing the job and cutting the package
free of its host.

## Work breakdown

### 1. Project model: split game × toolchain, drive from config  *(core effort — bigger than first drafted)*

**The blocker the first draft missed.** `ProjectType` is a closed two-member enum
`{DC3, RB3}` where each member secretly bundles *game + console + compiler + obj
format + path layout*. rb3-xenon is **RB3-the-game on the DC3-toolchain** (title
`45410914`, `msvc_ppc_16.00.11886.00`, `.obj`, `/Fo`) — a combination the enum
**cannot represent**. The axes are orthogonal and must be modeled as such:

- **game / build_id** — `373307D9`, `SZBE69_B8`, `45410914`
- **toolchain** — `msvc` vs `mwcc` (drives `output_flag`, `obj_extension`,
  `uses_cd_prefix`, ninja-command parsing, and the mwcc-only splice fast path)
- **obj-path layout** — *new third axis, see below*

**This is not speculative — there is already a workaround in production.**
`rb3-xenon/scripts/permuter_rb3xenon.py` monkeypatches the symlinked package at
process start because it *cannot* edit `project.py` (one physical copy, shared via
symlink — see §6). Its own docstring is the bug report: the name heuristic
(`project.py:262-266`) sees `"rb3"` in the path and **mis-classifies rb3-xenon as
Wii/mwcceppc**, pointing every path at a `.o` tree that doesn't exist. The shim
forces the DC3 codepath, overrides `build_id`, and **rebinds
`target_obj_for_base_obj`**.

That last rebind exposes the **third axis**: DC3 derives the target obj by
mirroring the src subtree (`build/<id>/obj/system/math/Rot.obj`), but rb3-xenon's
dtk split emits a **flat** `obj/` keyed by basename. The fix generalizes *all
three* projects: **consult `objdiff.json`'s authoritative `base_path →
target_path` map** instead of deriving paths by per-project convention. objdiff.json
is project-agnostic and already present in every consumer.

Concrete work:
- Replace the `ProjectType` enum with a descriptor loaded entirely from
  `permuter.json`: `build_id`, `toolchain`, `obj_extension`, `output_flag`,
  `uses_cd_prefix`, `objdiff_cli`, `m2c_target`, `obj_layout`.
- **Re-key the 18 `if project_type == …` branches in `project.py` on `toolchain`,
  not game.** They are all really mwcc-vs-msvc logic (ninja parsing,
  `/Fo`-vs-`-o`, cd-prefix).
- **Fix `scorer.py:348`** (`project_type != ProjectType.RB3` gating the
  mwcceppc-only preprocess-splice fast path) to gate on `toolchain == "mwcc"`.
  As written it would **misfire for rb3-xenon** (RB3 game, but MSVC).
- Drop the repo-name heuristic; detect via `permuter.json` presence + its
  declared `build_id`.
- Make `target_obj_for_base_obj` read objdiff.json's map (kills the shim's rebind).
- Merge the two `permuter.json` loaders (`project_config.py` reads only
  `compiler`; `project.py::_make_config` hardcodes the rest) into one schema.
- Ship `permuter.json` for **all three** projects as worked examples.

**Acceptance test for this workstream:** rb3-xenon runs `scan_and_permute`
end-to-end with the stock package and **zero monkeypatching** —
`permuter_rb3xenon.py` is deleted. If the shim can be retired, the abstraction is
proven generic.

### 2. Cut the package free of its host  *(core effort — this is the real work, and it's bidirectional)*

The first draft framed this as "convert `scripts.permuter` imports to relative."
The internal rename *is* mostly mechanical (770 imports are already relative; only
16 absolute + 103 test + 7 bench files + two `-m scripts.permuter` string literals
to rewrite). **The hard part is that the package is entangled with four sibling
trees in *both* directions**, and one of those couplings is an **eager top-level
import that breaks `import decomp_synth` outright**:

**Outbound (permuter → host):**
- `diagnosis.py:19` — **EAGER** `from scripts.analysis.diff_inspect import …`
  (5 pure objdiff-JSON-parsing functions). `diagnosis` is core, so this is the
  **#1 blocker**: the package won't even import standalone. → **Vendor** these
  functions into decomp-synth (they parse objdiff output, which decomp-synth
  already owns as its scoring backend).
- `scorer.py`, `project.py` → `scripts.unicorn_runner.run` (behavioral
  equivalence; lazy; DC3-only).
- `batch_auto.py`, `beam_search.py`, `hill_climber.py` →
  `scripts.orchestrator.{database, rb3_pairing, db_helpers}` (lazy).
- `scorer.py`, `ppc_shape_facts.py`, `patterns/declaration_reorder.py`,
  `ghidra_cache.py` → `tools.compiler_trace.*`, `tools.ghidra.mcp_client`,
  `msvc-src/tools` IL tools (lazy, feature-gated).

**Inbound (host → permuter) — these re-point to the installed `decomp_synth` (§6):**
- `scripts/ai_advisor.py`, `scripts/analysis/diff_inspect.py`,
  `scripts/analysis/reclassify_at_limit.py`, `tools/compiler_trace/regmap_solver.py`
  all `from scripts.permuter.* import …`.

**Architecture decision — dependency inversion at the host seam.** decomp-synth
defines the synthesis engine (extract / generate / score / search / patterns) plus
**Protocol interfaces** for optional host capabilities (reference-source lookup,
behavioral verification, IL dedup, Ghidra hints, DB ingest). The host repo
*registers* implementations; absent registration, each capability degrades to a
no-op. This removes every outbound coupling except the diff_inspect helpers, which
get **vendored**. Inbound consumers just re-point to the new package name
(mechanical, host-repo side).

> **Verified non-issues:** the MCP orchestrator does **not** invoke the permuter
> as a subprocess, and ninja does **not** call it — so the only callers to keep
> green are the five inbound Python imports above.

### 3. Packaging
- `pyproject.toml`: runtime deps are **only** `tree-sitter`, `tree-sitter-cpp`,
  `tree-sitter-c` (the host `requirements.txt` omits `tree-sitter-c` — a latent
  bug; `ghidra_ast.py:20` imports it). Optional extra `[clang]` →
  `libclang` (already fully guarded, degrades to "unknown"). Dev extra `[test]` →
  `pytest`. **Nothing else is third-party** — `unicorn`, `pcpp`, `mcp`,
  `pydantic`, `requests`, etc. in the host requirements belong to the orchestrator,
  not the permuter.
- `requires-python = ">=3.10"` (code targets 3.9 via PEP 585 runtime subscripts;
  3.10 is the only interpreter actually tested — pick the safe floor).
- Console entry point `decomp-synth`; keep `python -m decomp_synth.<module>`
  working for the batch scripts.

### 4. External tool boundaries
- **`objdiff-cli`** (the scoring backend, **required**): path is currently
  hardcoded to the repo-relative `"bin/objdiff-cli"` (`project.py:282,293`). Make
  it config / `$DECOMP_SYNTH_OBJDIFF` / PATH-resolved, and emit a clear "install
  from <url>, expected to emit `fuzzy_match_percent` JSON" error instead of a raw
  subprocess failure.
- **`m2c`** (*optional* guidance): path hardcoded to `~/code/milohax/m2c/m2c.py`
  (`m2c.py:13`). Make it `$M2C_PATH` / config / `python -m m2c`. Already degrades
  cleanly (prints "m2c: unavailable", returns `None`) when absent.
- **Ghidra MCP** (*optional*): already best-in-class — read-through SQLite cache,
  lazy MCP import, and a **circuit breaker** (3 failures → backoff). One rough
  edge: `ghidra_cache.py` raises `FileNotFoundError` if `decomp.db` is absent;
  treat "no DB" as "no cache" for public users.
- **`ninja` is a hard runtime dependency** (invoked bare; the scorer replays the
  project's `ninja -t commands <obj>` output, which is how `wibo`/`cl.exe`/
  `mwcceppc` stay entirely project-side — good design, keep it). Document it.
- MSVC/`wibo`/MetroWerks toolchains stay project-side, referenced only via the
  replayed ninja command. No compiler paths in decomp-synth. ✔

### 5. Generic engine vs project-learned data  *(promoted to a real workstream)*

`patterns/` (44k LOC) is **mostly clean generic compiler transforms**, but ~11 of
124 files embed Milo-engine / game vocabulary, and the learned databases are
saturated with game IP:

- **DBs are IP and must not ship.** `strategy.db` (2.3 MB) stores real mangled
  symbols per pattern; `permuter_cache.db` (232 MB) and `decomp.db` (277 MB) store
  symbols + absolute source paths. **All live at repo root, all gitignored, never
  committed** (verified). → Ship **schema-creating code only**; consuming repos
  accumulate their own (gitignored) DBs. Optionally publish an **anonymized seed**
  (pattern → success-rate priors with symbol names stripped) so the tool isn't
  cold-start dumb.
- **Engine-vocabulary patterns** hardcode `MILO_*` macros, `MakeString`, `Symbol`,
  `TheDebug`, etc. (`milo_log_swap.py`, `milo_call_merge.py`, `symbol_str_compare.py`,
  …), and two are fully game-specific: `native_guard_camera_wrap.py` (hardcodes a
  DC3 source shape) and `rb3_source_hint.py`. → Introduce a **project-dialect
  config**: the generic core ships clean; the Milo macro/symbol vocabulary loads
  from a per-project dialect file (mirrors the existing `compiler_dialect` plane).
  Drop or genericize the two game-specific patterns.
- `compiler_atlas.py` (37k) is **clean** generic MWCC/PPC compiler RE — ships as-is.

### 6. Consumption model — and migrating off the symlink
**decomp-synth is an installed package, not code embedded in each repo.** The
first draft proposed a per-repo git submodule — that was wrong; it's just a
fancier symlink. The symlink/submodule instinct only arises from the *current*
coupling: the tool is run as `python -m scripts.permuter` with the repo root as
CWD, so the package has to physically sit at `<repo>/scripts/permuter` to be
importable (`scripts/` has no `__init__.py`; it's found via CWD-on-`sys.path`, not
because it's installed). Today **rb3 and rb3-xenon both symlink** `scripts/permuter`
into the dc3 tree to satisfy that — one physical copy in three places — which is
*also* why rb3-xenon can't edit `project.py` and resorts to a runtime monkeypatch.

Once §2/§3 make it an installable package that takes the target repo as an input
(`--repo-root`/CWD), that coupling is gone and **no consumer holds the tool's
code at all**:

- **One canonical git repo** at `../decomp-synth` (sibling, like `../milo-native-engine`).
- **Installed into the (already shared) venv.** `rb3-xenon/venv` is *already* a
  symlink to `dc3-decomp/venv`; a single `pip install` reaches all consumers that
  share it. In-house dev uses an **editable install** (`pip install -e
  ../decomp-synth`) → edit once, all three see it live, which is the
  fast-iteration benefit the submodule was meant to give, without embedding.
- **Reproducibility/pinning** lives in each repo's `requirements.txt` as a pinned
  SHA/version (`decomp-synth @ git+…@<sha>`), or a tagged release for external
  users. This is the Python-idiomatic analogue of `MILO_ENGINE_PIN` (which is a
  C++/CMake `add_subdirectory` path-pin precisely because the engine is *compiled
  into* the binary — not applicable to a Python tool).
- Each consumer keeps only its **project config** (`permuter.json`, `objdiff.json`,
  baseline objs, `bin/objdiff-cli`) — which it already has.
- Migration steps: (a) land `../decomp-synth` as its own repo; (b) `pip install -e`
  it into the shared venv; (c) re-point the 5 inbound host imports
  `scripts.permuter.*` → `decomp_synth.*`; (d) update the `permute` skill + docs
  from `-m scripts.permuter…` to `-m decomp_synth…`; (e) delete the rb3 +
  rb3-xenon `scripts/permuter` symlinks and `permuter_rb3xenon.py`.

### 7. Public-repo hygiene  *(must do before going public)*
**Fresh repo, fresh history — do not import dc3's git history.** (Decision
2026-05-28: history preservation is explicitly not wanted, which also moots the
in-history-IP concern — those files simply aren't copied over.) Copy the working
tree into the new `../decomp-synth` repo with a clean initial commit. The blockers
are therefore purely about which *files* get copied, not history rewriting:
- **[BLOCKER] Tracked IP-bearing data files.** `bench/*.json`,
  `bench/final_sweep/*`, `bench/*.md`, `tests/benchmark_targets.json`,
  `tests/benchmark_beam_results.json` embed real DC3/RB3 mangled symbols, source
  paths, baseline percentages, and an internal branch SHA. → Delete / regenerate
  against an open corpus.
- **[SCRUB]** Real symbols in docstrings/test trace-fixtures → synthetic
  placeholders. Hardcoded `/home/free/...` paths in 3 bench/test files → env/CLI.
  `experiments/` and bench result JSON do not ship.
- Add LICENSE (recommend **MIT** or **Apache-2.0** — matches the ecosystem:
  m2c/tree-sitter MIT, objdiff Apache/MIT) + README + a minimal worked example
  against an **open** decomp target so strangers can run it.

### 8. Tests & CI
- `pytest` collects **1,966 tests cleanly** (only hard import at collection is
  `tree_sitter`). Full run: **18 failures, 1,948 pass** — and **none are IP/binary
  related**. They split into stale test-vs-code drift (`test_repo_paths`
  `_FALLBACK_ROOT`, `test_hard_filters` default flip, score-preservation semantics,
  6 synthetic-snippet pattern-drift cases) and **one** environment bug
  (`test_preprocess_cache` writes a `.permuter_bak` into a read-only source path
  instead of a tmpdir). **Fix all 18 regardless of extraction** — they're real rot.
- **Public CI anchor:** ~1,500+ tests are pure-unit needing only Python +
  `tree-sitter` (no toolchain, no DB, no game IP — fixtures are synthetic). Mark
  the ~11 DB/build/objdiff integration files `@pytest.mark.integration` and
  deselect them in the public lane. For an end-to-end integration lane, build a
  tiny **open toy target** (2–3 hand-written `.cpp` → `.o`, an `objdiff.json`, a
  seeded fixture `decomp.db`) using `bench/run.py`'s harness format.

## Effort shape  *(revised)*

The first draft's "1–2 are the real work, 3 & 6 mechanical" undersold it. Revised:

- **Hardest / architectural:** §2 host-decoupling (eager `diagnosis.py` import +
  dependency inversion for 4 optional capabilities) and §1 game×toolchain split
  (+ obj-layout via objdiff.json). These are coupled — do them together on a branch.
- **Substantial:** §5 (patterns dialect split + DB seeding policy), §7 (IP data
  removal — file selection, not history scrub), §8 (fix 18 tests + author CI).
- **Mechanical:** §3 packaging, §4 path-from-config, internal import rename, §6
  pip-install wiring + skill/doc command updates.

**Recommended sequence:** (1) vendor diff_inspect + prove `import decomp_synth`
standalone; (2) config-driven game×toolchain model in-place on a branch; (3) prove
it against **all three** projects, with retiring `permuter_rb3xenon.py` as the
go/no-go gate; (4) dialect-split patterns + DB seeding; (5) copy clean tree into a
fresh `../decomp-synth` repo (no history), excluding IP files, add license/CI/
example; (6) `pip install -e` into the shared venv, re-point the 5 inbound imports
+ skill/docs, delete the rb3 + rb3-xenon symlinks and shim.

## Definition of done
The extraction is complete when **all** of these hold:
1. `import decomp_synth` succeeds in a clean venv with only the three tree-sitter
   packages installed (no host trees on `sys.path`). *(§2, §3)*
2. `permuter_rb3xenon.py` is deleted and rb3-xenon runs `scan_and_permute`
   end-to-end with the stock package. *(§1 — the go/no-go gate)*
3. All three repos run the tool from their own CWD against the shared venv install;
   no `scripts/permuter` symlink remains, and the 5 inbound host imports resolve
   `decomp_synth.*`. *(§6)*
4. Public `pytest` lane is green (integration tests deselected; the 18 drift/env
   failures fixed). *(§8)*
5. `../decomp-synth` has fresh history, a LICENSE, a README, an open worked
   example, and contains no game symbols, learned DBs, or `/home/free/...` paths.
   *(§5, §7)*

## Resolved questions
- **Where does learned data live?** Engine ships schema + empty DBs; each repo
  accumulates its own (gitignored). Optional anonymized priors seed (symbols
  stripped). *(§5)*
- **Fresh vs preserved history?** Fresh — start a brand-new repo, do not import
  dc3 history (decided 2026-05-28). *(§7)*
- **How do the three repos consume it?** As an installed package in the shared
  venv (editable `pip install -e ../decomp-synth` in-house; pinned SHA/tag in
  `requirements.txt` for reproducibility/external). **Not** a per-repo symlink or
  submodule — those were artifacts of the old import-root=repo-root coupling. *(§6)*

## Open questions
- Dialect-pattern boundary: do `MILO_*`/`MakeString`/`Symbol` patterns ship as a
  bundled "Milo dialect" example, or stay fully private? (Leaning: ship as an
  example dialect, since the vocabulary alone isn't game IP.)
- Anonymized strategy-DB seed — worth the effort, or start every repo cold?
- Does rb3-xenon's **retail size-optimized** build (vs DC3's debug build) surface
  toolchain knobs the descriptor doesn't yet model (opt flags in the replayed
  ninja command)? Validate during the §1 acceptance test.
- **rb3-xenon symbol naming (found during §1 validation, orthogonal to config):**
  its dtk-split target objs carry `fn_<addr>` names, not mangled MSVC names, and
  the checked-in `report.json` is stale (a fresh `report generate` shows
  `fn_82758A50` etc.). The config resolves the correct target obj, but matching a
  mangled symbol needs the target-obj symbol-naming/report-refresh sorted out.
  This is an rb3-xenon data-pipeline issue, not a `decomp-synth` concern.
- **`repo_paths.py` consistency (deferred to avoid colliding with the concurrent
  test-fix thread):** `project.py::_resolve_repo_root` now treats
  `decomp-synth.json`/`permuter.json` as a root marker, but `repo_paths.py::
  _detect_repo_root` (separate, for DB paths) still keys only on the dir markers +
  `objdiff.json`. Equivalent today for all three repos; add the json marker there
  for a project that ships *only* `decomp-synth.json`.

---

## Appendix: audit findings

Five parallel read-only audits (2026-05-28) over the live tree. Key file:line
evidence, condensed:

**Project coupling** — `ProjectType` enum `project.py:27-29`; 18 enum branches in
`project.py`, 1 in `scorer.py:348` (mwcc fast-path mis-keyed on game); name
heuristic `project.py:262-266`; two unreconciled config planes (`project.py::
_make_config` hardcodes everything but `compiler`, which `project_config.py` reads
from `permuter.json`); both real `permuter.json` files carry only `{"compiler":…}`.
rb3-xenon proof: `rb3-xenon/scripts/permuter_rb3xenon.py` (monkeypatches
`_make_config`, `_detect_project_type`, `target_obj_for_base_obj`).

**Package/path coupling** — 770 internal relative imports (OK); 16 absolute + 103
test + 7 bench to rename; `batch_validate.py:261` & `bench/c1_source_diff_ab.py:92`
have `-m scripts.permuter` string literals. **Eager** external import
`diagnosis.py:19` (breaks standalone import). Outbound lazy deps:
`scripts.unicorn_runner`, `scripts.orchestrator.{database,rb3_pairing,db_helpers}`,
`tools.compiler_trace.*`, `tools.ghidra.mcp_client`. Inbound: `scripts/ai_advisor.py`,
`scripts/analysis/{diff_inspect,reclassify_at_limit}.py`,
`tools/compiler_trace/regmap_solver.py`. Repo-root via `parents[2]` /
CWD-walk (`repo_paths.py:16`, `project.py:314`) — add explicit `--repo-root`.
Orchestrator & ninja do **not** invoke the permuter.

**IP / hygiene** — git history under `scripts/permuter` clean (no DB/archive/logs
ever committed; `.gitignore:89` `*.db`). Root DBs `strategy.db` 2.3 MB /
`permuter_cache.db` 232 MB / `decomp.db` 277 MB carry symbols + abs paths (gitignored).
**Tracked** IP data: `bench/*.json`, `bench/final_sweep/*`, `tests/benchmark_*.json`.
11/124 patterns embed engine vocab; worst: `native_guard_camera_wrap.py`,
`rb3_source_hint.py`. `compiler_atlas.py` clean. No secrets/tokens/keys.

**Dependencies / tools** — third-party set is exactly `tree-sitter`,
`tree-sitter-cpp`, `tree-sitter-c` (last one missing from host `requirements.txt`),
optional `libclang`, dev `pytest`. Min Python 3.10 (3.9 by PEP 585). objdiff-cli
hardcoded `bin/objdiff-cli` (`project.py:282,293`); m2c hardcoded
`~/code/milohax/m2c/m2c.py` (`m2c.py:13`); Ghidra degrades via circuit breaker
(`ghidra_cache.py:28-103`); compilers stay project-side via `ninja -t commands`
replay (`scorer.py:265`); `ninja` is a hard runtime dep.

**Tests / entry points** — 1,966 collected, 18 fail (drift + 1 read-only-fs
write), none IP/binary. ~1,500+ pure-unit (Python+tree-sitter). No `.obj`/`.asm`
IP fixtures. Entry points: `__main__.py` (single-function CLI) + batch modules
`scan_and_permute`, `batch_auto`, `batch_triage`, `batch_sweep`,
`batch_unit_climber`, `batch_validate`. `bench/`+`experiments/` dev-only;
`.claude/` empty. No existing CI references the permuter.
