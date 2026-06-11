# 19 — Unicorn Evidence Refresh (Wave 3 Lane B)

**Date:** 2026-06-10. **Lane:** B (unicorn evidence refresh) — 94 follow-up #4,
roadmap 0.8. **Branch:** `wave3/b-unicorn-refresh`. **Worktree:**
`/home/free/code/milohax/wt-wave3-b-unicorn-refresh`. **Build plane for every
match-percent / verdict number below: the worktree at HEAD `1d8409f4`**, whose
decomp `.obj` cache is reflink-identical to `main@1d8409f4` (stale-obj check: 2
SDK objs only, no frontier units). All cert censuses were computed against a
**COPY** of the live `decomp.db` — the live DB was never written by this lane.

## Problem (from Wave 2)

843 of 970 floor certs rested on unicorn data ~98 days old (Feb–Mar 2026); 335
authorable-partial-frontier fns had no unicorn evidence at all. The behavioral
plane was stale and undetectably so: the only freshness signal stored was
`unicorn_tested_at` (a date), so a verdict could not be known stale the moment
the function's source changed — only when it got old.

## 1. Runner identified (no rebuild needed)

The unicorn behavioral plane was filled by **`scripts/unicorn/batch_to_db.py`**,
which drives **`scripts/unicorn_runner/`** (Phase 0–5, `docs/tools/UNICORN_FUNCTION_RUNNER.md`).
It loads each compiled function into Unicorn PPC32-BE, mocks externals via
trampolines, and compares observable state (r3/f1 return, call log, object-memory
diff) between decomp `.obj` and original `.obj`. The probe schedule it used:
two fills (zero, `0xCD`), zero args, no typed memory. Verdicts land in
`functions.unicorn_{verdict,class,confidence,tested_at,signal_version,probe_schedule_hash}`.

Runtime deps (already present): unicorn 2.1.4 at
`/home/free/code/milohax/unicorn/{bindings/python,build}`, capstone 5.0.7.
`SIGNAL_VERSION` (in `scripts/unicorn_runner/signal_version.py`) is currently
**3** — it gates verdicts on *runner-semantics* changes; the stale rows predate
it (`signal_version=NULL`).

## 2. What I built (worktree-local, zero live-DB writes)

- **`scripts/unicorn/source_hash.py`** — the **source-hash freshness gate**.
  Per function it hashes the decomp `.text` COMDAT bytes **plus** the ordered
  reloc (offset,type,target-name) list. Source unchanged ⇒ identical `.obj`
  bytes+relocs ⇒ identical hash ⇒ the prior verdict still applies; source
  changed ⇒ recompiled body ⇒ different hash ⇒ verdict is detectably stale.
  This gates on *source* where `signal_version` gates on *runner semantics* —
  together a complete freshness story.
- **`scripts/unicorn/refresh_frontier.py`** — re-runs the **same** batch_to_db
  schedule over the authorable partial frontier, stamps each result with the
  source-hash, writes to a **separate** results DB (`unicorn_refresh.db`) +
  JSON sidecar, and computes the **flip list** (prior live verdict vs fresh
  verdict), adjudicating each flip's cause (below). 1,312 fns / 414 units in
  **33 s** (12 workers).
- **`scripts/unicorn/apply_refresh.py`** — the orchestrator's single-writer
  apply step (dry-run by default). Adds `unicorn_source_hash` +
  `unicorn_source_hash_at` columns and writes the fresh verdicts + hashes.

## 3. The flip list — the headline deliverable

A *flip* = a function whose behavioral verdict changed between the stale live
value and the fresh re-run. **Not every flip is a new bug** — most are the
expected consequence of `SIGNAL_VERSION` advancing v1→v3 since the stale data
was taken. `refresh_frontier.py` adjudicates each flip's **cause**:

| `flip_cause` | meaning | count (EQ→DIV) |
|---|---|---|
| `signal_version` | EQ→DIV from the v2 cap-exhaustion / v3 wild-jump tightening (prior EQUIV was a truncation artifact) — **expected churn, not a new bug** | 225 |
| `artifact` | EQ→DIV into a cosmetic build/emulation class (`build_env`/`regalloc`/`stack_layout`/`merged_*`/`fpr_precision`) — still a floor; the cert just moves `equivalent`→`artifact:*` | 19 |
| **`candidate_bug`** | **EQ→DIV into a real-bug class (`object_memory`/`call_count`/`call_arg`/`unmapped`) or one-sided cap — a behavior divergence that was hiding under a stale `equivalent` floor cert. ADJUDICATE EACH.** | **60** |
| `recovered` | DIV→EQ (a prior divergence now tests equivalent) | 26 |

### Stale-EQUIVALENT cohort outcome (lane item 3)

Of **600** prior-EQUIVALENT frontier fns re-tested:
- **295 stayed EQUIVALENT** (floor certs that survive the refresh, now fresh).
- **305 flipped** — but only **60 are candidate real bugs**; 225 are signal-version
  churn, 19 are cosmetic-artifact reclassifications, 1 other.

### The 60 candidate_bug flips (each was `floor_certificate=equivalent`)

**IMPORTANT — these are CANDIDATES requiring adjudication, not a confirmed
bug list.** 57/60 are `stable_divergent` (divergent across both fill patterns).
Bucketed by signal strength:

**A. Strong real-bug classes (9)** — object-memory / call-count / unmapped diffs,
least likely to be fixture noise:

| symbol | unit | class | norm% | conf |
|---|---|---|---|---|
| `?Seed@Rand@@QAAXH@Z` | math/Rand | object_memory | 94.0 | stable_div |
| `?Poll@CharFeedback@@UAAXXZ` | hamobj/CharFeedback | object_memory | 98.37 | stable_div |
| `?UpdateColorModulation@RndPostProc@@IAAXXZ` | rndobj/PostProc | object_memory | 97.94 | stable_div |
| `?UpdateFakeArmPos@SkeletonUpdate@@AAAXXZ` | gesture/SkeletonUpdate | object_memory | 97.06 | stable_div |
| `?Enter@CharEyes@@UAAXXZ` | char/CharEyes | object_memory | 93.17 | stable_div |
| `?IsValidScrollPos@DirectionGestureFilterSingleUser@@...` | gesture/DirectionGestureFilter | call_count | 99.95 | stable_div |
| `?IsValidSwipePosition@DirectionGestureFilterSingleUser@@...` | gesture/DirectionGestureFilter | call_count | 93.1 | stable_div |
| `?MemPopTemp@@YAXXZ` | utl/MemMgr | call_count | 91.11 | stable_div |
| `?FaceCenter@@YAXPAVRndMesh@@PAVFace@1@AAVVector3@@@Z` | rndobj/Mesh | unmapped | 93.68 | input_sensitive |

**Worked example — `Rand::Seed` is a real bug.** Under emulation the decomp
writes the MT-state array with the **high 16 bits wrong** (`0xFFFFxxxx` where orig
writes `0x5665xxxx`, `0x20DAxxxx`, …; low 16 bits match) across all 20 state
words — a systematic signed/upper-bits arithmetic divergence, the same family as
the `Rand::Int` signed-modulo bug Wave 2 Lane A fixed. This is a genuine
behavioral divergence that the stale `equivalent` cert was masking. **Hand this
one to Lane C / a follow-up as a confirmed-bug candidate.**

**B. `call_arg` class (19)** — mostly **likely false positives**: the differing
arg is a string/`__FILE__`/`MakeString`-region pointer the classifier could not
auto-prove as `build_env` because one side's pointer lands in the co-load region
(e.g. `SetPausedHelper`: decomp r4 in the co-loaded MakeString region vs orig in
globals). Worth a 2-minute look each but expect ~build_env. Full list in
`unicorn_refresh.json`.

**C. One-sided cap-exhaustion (32)** — `cap_exhausted_decomp`/`_orig`: one side
loops where the other terminates under zero/`0xCD` fill. Could be a real
loop-bound divergence OR a fixture artifact (uninitialised loop counter under
zero-fill). Lower priority; needs a real fixture to confirm.

### Cert-masking caveat (critical for using the flip-list)

After the refresh lands, **40 of the 60 candidate_bugs fall through to a weaker
floor cert** (`permuter_exhausted` 30, `icf_merged` 10) because cert precedence
re-covers them; only **20 become fully `open`**. **The flip-list must therefore
be consulted independently of the cert** — a function can be behaviorally
DIVERGENT yet still carry a (technically valid) weaker floor cert. The 60-row
candidate list in `unicorn_refresh.json` is the source of truth for "behavior
divergence to adjudicate", not the cert column.

### No-evidence cohort (335 → newly tested)

Of the 335 no-evidence frontier fns, **334** got a fresh verdict (1 not in
objdiff.json): **93 EQUIVALENT** (new floor-cert candidates), **236 DIVERGENT**
(20 cosmetic-artifact, 17 real-bug class, rest cap/wild-jump), **5 SKIPPED**.

### Recovered (26 DIV→EQ)

26 prior-DIVERGENT now test EQUIVALENT — mostly `orig_error` (13) and
`call_count` (7). These were divergences the v1 signal over-reported; they now
qualify for an `equivalent` cert. Worth spot-checking the `orig_error` ones (a
true orig-error floor shouldn't simply become EQUIVALENT — verify the runner
change, not a fixture flake, drove it).

## 4. Cert census delta (measured on a COPY of live decomp.db, worktree plane)

`certify_floor.py` BEFORE (stale data) vs AFTER (refresh applied to the copy):

| metric | BEFORE (stale) | AFTER (refresh) |
|---|---|---|
| certifiable today | 970 | **948** |
| ├ `equivalent` | 600 | 415 |
| ├ `artifact:*` | 246 | 111 |
| ├ `icf_merged` | 16 | 84 |
| ├ `permuter_exhausted` | 108 | 338 |
| **backed by STALE unicorn (>60d)** | **843** | **3** |
| on FRESH evidence | 127 | **945** |
| no evidence at all | 344 | 366 |

The refresh is **more honest, not bigger**: net −22 certs because 305 stale
`equivalent` certs were re-judged (225 lost their EQUIVALENT basis to the v2 cap
rule; 60 are candidate bugs; 19 reclassified to `artifact:*`). Functions that
lost `equivalent` mostly fell through to a weaker-but-valid floor
(`permuter_exhausted`/`icf_merged`), so the headline-bytes "done" figure barely
moves — but **843 → 3 stale-backed certs** means the done view now rests on fresh
evidence, which was the entire point. The +22 no-evidence (344→366) are the
SKIPPED/ERROR fns the refresh couldn't re-test.

## 5. Apply runbook (orchestrator, single-writer, on main, after merge)

This lane wrote **nothing** to the live DB. The orchestrator applies, in order:

```bash
# Pre: the wave-3 Lane-B branch is merged; sync_match_percent.py has run so
# match_percent_normalized is current (certs + frontier gate off it).

# 1. (one-time) bring the refresh results DB onto main. It lives in the lane
#    worktree; copy it next to the repo or pass its path with --results.
RES=/home/free/code/milohax/wt-wave3-b-unicorn-refresh/unicorn_refresh.db

# 2. DRY-RUN: preview the migration + verdict updates (writes NOTHING).
python3 scripts/unicorn/apply_refresh.py --results "$RES"
#    expect: "WOULD UPDATE: 1304, not in live DB: 0"

# 3. APPLY: add unicorn_source_hash + unicorn_source_hash_at columns and write
#    1,304 fresh verdicts (+ source hashes). The 8 SKIPPED fns keep their prior
#    verdict (we couldn't re-test them — anon-namespace name skew / stub size).
python3 scripts/unicorn/apply_refresh.py --results "$RES" --apply

# 4. Re-certify from the now-FRESH evidence. certify_floor.py reads the updated
#    unicorn_* columns; tested_at is today so STALE_DAYS no longer fires.
python3 scripts/certify_floor.py                       # dry-run census (expect ~948)
python3 scripts/certify_floor.py --migrate --apply     # write certs

# 5. Confirm no stale certs remain and record the done-view headline.
python3 scripts/reconcile_db.py
python3 scripts/certify_floor.py --summary
```

Re-test cadence going forward: `refresh_frontier.py` is cheap (~33 s). Re-run it
whenever `sync_match_percent.py` moves percents, then re-apply; a verdict whose
`unicorn_source_hash` no longer matches the rebuilt `.obj` is provably stale and
should be re-tested before its cert is trusted (`apply_refresh.py
--only-fresh-source` skips rows whose hash is unchanged).

## 6. Contradictions / corrections to prior docs

- Wave-2 doc 94 (and `17-floor-cert-apply-runbook.md`) treated the 843
  stale-unicorn certs as "valid floor SIGNALS, just dated". **The refresh shows
  60 of the stale `equivalent` certs were masking real behavior divergences**
  (candidate bugs), and 225 more rested on the pre-v2 cap-exhaustion-as-EQUIV
  rule that the current signal correctly calls DIVERGENT. So "an unedited
  EQUIVALENT fn is still EQUIVALENT" is true *for the same signal version* — but
  these were a different (looser) signal version, which is exactly why a refresh
  was needed, not just a date stamp.
- Frontier no-evidence count: doc 94 said **344**; the live DB measures **335**
  with no `unicorn_verdict`. Minor drift (9 fns gained/lost evidence since); the
  refresh covers both framings (it re-tests the whole 1,314 frontier).
- The cert census did **not** simply grow with fresh data; it shrank 970→948 and
  re-distributed toward weaker floor classes. Any planning that assumed "refresh
  → more certs" is wrong; the value is *fresh, honest* certs + a flip-list of
  candidate bugs, not a higher count.

## Files

- `scripts/unicorn/source_hash.py` (new) — per-fn source fingerprint.
- `scripts/unicorn/refresh_frontier.py` (new) — frontier sweep + flip-list.
- `scripts/unicorn/apply_refresh.py` (new) — orchestrator apply (dry-run default).
- `scripts/unicorn/test_refresh.py` (new) — unit tests for the gate + adjudicator.
- `unicorn_refresh.{db,json}` (worktree artifacts, gitignored) — full results +
  the 60-row candidate-bug flip list + 334-row no-evidence verdicts.
