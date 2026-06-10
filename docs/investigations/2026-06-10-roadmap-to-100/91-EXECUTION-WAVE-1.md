# 91 — Execution Wave 1 (Phase 0 tooling + native quick wins)

**Date:** 2026-06-10. **Planner:** Fable (orchestrator). **Executors:** Opus implementation
agents + Sonnet mechanical/verification agents, one git worktree per lane.

Wave 1 implements the highest-ROI block of `90-ROADMAP.md`: Phase 0 measurement
integrity (items 0.1–0.7, 0.9) and the two native quick wins (N.1, N.2). Zero decomp
work. Everything here either fixes the numbers we steer by or converts silent native
failures into ranked worklists.

## Global rules (every lane)

1. **Work in your own worktree.** Create it with
   `scripts/setup_worktree.sh /home/free/code/milohax/wt-wave1-<lane> wave1/<lane>`
   (run from the main repo). Do ALL edits there. Run `ninja` once in the worktree
   before any objdiff-based measurement (fresh worktrees mis-measure otherwise).
2. **Never** run `git stash` anywhere, never commit to dc3 `main`, never write to
   `/home/free/code/milohax/dc3-decomp/decomp.db` (read via absolute path is fine).
   No `Co-Authored-By` lines.
3. All DB-mutating tools must default to **dry-run** and require `--apply`. Wave 1
   delivers dry-run output + the exact apply command; the orchestrator applies on main.
4. Commit your work to your `wave1/<lane>` branch with a clear message. Your final
   report must include: branch name, worktree path, files changed, validation commands
   you actually ran with their real output, dry-run counts, risks, and exact apply steps.
5. Evidence base: read `00-INDEX.md`, `90-ROADMAP.md`, and your lane's source docs in
   this folder before coding. If reality contradicts a doc claim (these were written by
   agents, one claim was already refuted), report the contradiction — do not silently
   improvise.

## Lane A — measurement-sync core (Opus) — roadmap 0.2 / 0.3 / 0.4 / 0.7

Owner files: `scripts/sync_match_percent.py` (modify), `scripts/reconcile_db.py` (new).
Source docs: 03, 04.

1. **Normalized dual-store.** decomp.db stores fuzzy percent today; the canonical gate
   is the normalized scorer. First, determine where normalized percent is available:
   check whether `build/373307D9/report.json` function entries carry a normalized field;
   if not, find how to make the report generator emit it (objdiff report config) or
   derive it. Then: store BOTH (`current_percent` stays fuzzy for continuity; add/fill a
   `match_percent_normalized` column), and key `--promote` verdict=COMPLETE off
   **normalized==100**. Expected effect (doc 04 F2): ~206 fns currently <100 fuzzy but
   100 normalized become promotable. Report the actual count you measure.
2. **Demote path.** sync never demotes: add verdict COMPLETE→NULL when current<100
   (doc 03 F1/F4 says 639 FALSE-COMPLETE + 20 stale COMPLETE rows exist). Also flag
   rows whose symbol no longer appears in report.json.
3. **Stale is_stub clear.** Clear `is_stub` when current_percent>=100 (doc 04 F3:
   1,728 rows).
4. **`scripts/reconcile_db.py` drift detector.** Read-only by default; checks, with
   loud per-check counts and nonzero exit on drift: (a) db.current_percent vs
   report fuzzy differs ≥0.5; (b) verdict=COMPLETE AND current<100; (c) is_stub=1 AND
   current>=100; (d) symbols in db but absent from report (and vice versa, authorable
   only). `--fix` applies the corrections that sync owns. Intended to run
   post-`sync_match_percent.py` / nightly.
5. **Tests:** exercise against a COPY of decomp.db (copy it inside your worktree);
   verify dry-run counts approximate the audit numbers (639 / 20 / 1,728 / 206) and
   report the actual figures — drift from the audit numbers is expected and worth noting.

Acceptance: dry-run reproduces (approximately) the four audit drift counts; promote/demote
logic unit-tested; no decomp.db writes happened.

## Lane B — authorable-denominator metrics (Sonnet) — roadmap 0.1 / 0.9, Tier-1 #4

Owner files: `scripts/progress_metrics.py` (new), `docs/PROGRESS_METRICS.md` (generated),
`scripts/measure_progress.sh` (small addition). Source docs: 02, 05, 06.

1. **`scripts/progress_metrics.py`**: read `build/373307D9/report.json`, compute and
   print (a) total XEX code bytes/fns matched %, (b) **authorable** bytes/fns matched %
   excluding units under `default/xdk/*` and `default/lib/*` (start from
   `SDK_UNIT_PREFIXES` in sync_match_percent.py — reconcile the two lists and use one
   shared definition, e.g. a small `scripts/authorable.py` helper), (c) remaining
   authorable bytes, (d) complete-unit counts. `--markdown` mode regenerates
   `docs/PROGRESS_METRICS.md`.
2. **`docs/PROGRESS_METRICS.md`**: document the four coexisting headline numbers
   (43.8% XDK-diluted fuzzy / authorable fuzzy ~77.5-78.5% / normalized / strict-reloc
   pending Lane C), name **authorable normalized %** as canonical, and explain the
   reloc-mode caveat in two sentences (doc 02). This doc is the anti-drift anchor.
3. Add an `--authorable` flag to `measure_progress.sh` that delegates to the new script
   (keep the change minimal; don't refactor the existing script).

Acceptance: authorable matched % computes into the 76–80% band (if not, investigate and
explain rather than force); the exclusion list is shared, not duplicated; PROGRESS_METRICS.md
generated and committed on the branch.

## Lane C — strict-reloc recertification (Opus) — roadmap 0.5 / 0.6, Tier-1 #3/#5

Owner repos: `../objdiff` fork (Rust) + dc3 `scripts/analysis/`. Source doc: 02.
This quantifies the ONLY uncounted measurement risk: wrong-call-target false-100%s
under the lenient reloc mode (`functionRelocDiffs=none`) feeding report.json.

1. In the objdiff fork (make a worktree: `git -C /home/free/code/milohax/objdiff
   worktree add /home/free/code/milohax/wt-objdiff-strict wave1/strict-reloc`), add a
   `FunctionRelocDiffs::NameOnly` mode: reloc diff matches iff target symbol name (+
   section) matches, addend ignored. Follow the existing enum/config plumbing
   (`objdiff-core`), add a snapshot/unit test.
2. Find how dc3 generates `build/373307D9/report.json` (ninja rule / script), and
   produce `build/373307D9/report_strict.json` with NameOnly mode using your objdiff
   build. Do NOT change what report.json itself uses — strict is a side channel.
3. **Classifier** `scripts/analysis/reloc_strict_classify.py`: for each function 100%
   in report.json but <100% strict, classify the reloc mismatches: target-name mismatch
   (genuine false-100% — wrong callee/data symbol) vs addend-only (benign). Emit JSON +
   a summary table. Doc 02 expects ~11,052 lenient-100/strict-<100 candidates with the
   genuine subset much smaller — measure it.
4. Write results to `docs/investigations/2026-06-10-roadmap-to-100/14-strict-recert-results.md`:
   the genuine false-100% count and list (authorable only), top offending units, and a
   recommendation (recert cadence, whether any "matched" fns must be reopened).

Acceptance: NameOnly mode merged on the objdiff branch with a passing test;
report_strict.json generated from the current build; the genuine false-100% number
exists with a function list. The headline deliverable is THE NUMBER.

## Lane D — native quick wins (Opus) — roadmap N.1 / N.2, Tier-3 #13/#15

Owner files: `native/CMakeLists.txt`, `native/src/engine_stubs_generated.cpp` + its
generator, HTTP debug server. Source doc: 12 (UNVERIFIED — re-confirm claims first).

1. **json-c into native (N.1).** Confirm the gap: `src/system/net/json-c/*.c` exists
   but is absent from native/CMakeLists.txt, so online JSON parses to 0. Check how the
   symbols currently resolve (weak stubs in engine_stubs_generated.cpp? missing
   entirely?). Wire the .c files into the native build; fix LP64/host-compiler issues
   minimally; ensure real definitions beat any generated stubs. Add a milo-tests case
   parsing a small JSON document through the game's entry points (e.g. whatever
   RockCentral/MOTD uses). Baseline 371/371 milo-tests must still pass.
2. **HX_STUB_TRACE + /api/stubs (N.2).** Find the generator that produces
   engine_stubs_generated.cpp. Add an opt-in (`DC3_STUB_TRACE=1` env or compile flag)
   per-stub hit counter (name → count, lock-free or mutexed, cheap when off) and a
   `/api/stubs` endpoint on the HTTP debug server returning ranked hit counts.
   Doc 12 claims 171 silent return-0 stubs — report the real count.
3. **Stretch (only if 1–2 are done):** link-time/CI warning when a generated weak stub
   is the final definition of a symbol that has a real .c/.cpp in tree (the json-c
   failure-mode detector).
4. Validation: native build compiles (you may need `dangerouslyDisableSandbox` for GPU;
   prefer build-only + milo-tests which need no GPU); milo-tests pass; if feasible boot
   `scripts/dc3-agent-test.sh` and curl `/api/health` + `/api/stubs`.

Acceptance: json-c parse test green + 371/371 still green; /api/stubs returns ranked
counts from a real run (or, if boot is infeasible in the worktree, from a unit-level
exercise); claims from doc 12 re-confirmed or corrected.

## Verification stage (Sonnet, one per lane, adversarial)

Default-refute. In the lane's worktree: re-run the lane's claimed validation commands;
check each acceptance criterion; check global rules (no main commits, no decomp.db
writes — `git -C /home/free/code/milohax/dc3-decomp status`, db mtime). Verdict
pass/fail with evidence; a fail must say exactly what to fix.

## Orchestrator follow-up (Fable, after the workflow)

Review lane branches → merge to main → run Lane A apply steps on main (single writer)
→ re-run reconcile.py to confirm zero drift → commit `92-WAVE-1-RESULTS.md`.
