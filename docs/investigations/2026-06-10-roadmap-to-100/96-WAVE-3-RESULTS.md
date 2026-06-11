# 96 — Execution Wave 3 Results

**Date:** 2026-06-11. **Synthesizer:** Fable (orchestrator synthesis agent).
**Plan:** [`95-EXECUTION-WAVE-3.md`](95-EXECUTION-WAVE-3.md). **Wave-2 results:**
[`94-WAVE-2-RESULTS.md`](94-WAVE-2-RESULTS.md). **Scope:** gameplay unblock + feet
residual (A), unicorn evidence refresh (B), native live-bug burndown with tests (C),
scoring-plane reconciliation (D).

All four lanes completed in isolated worktrees. **All four passed** adversarial verdict
(Lane C `pass` after one repair round; Lane D `pass` with two doc-text required-fixes).
**No lane committed to `main`** and **no lane wrote `decomp.db`** — the live DB mtime is
unchanged (`Jun 10 23:39:38`) and main HEAD is still `1d8409f4` (the Wave-3 plan doc).
Branches are staged for the orchestrator to merge and apply.

> **Build-plane rule (Wave-2 doc-16 lesson, now enforced):** every match-percent and
> verdict number below names its build plane. Worktree `run_objdiff` readings are claims;
> final certification happens on `main` after the sync. Lane D exists precisely because a
> worktree reading is *not* evidence about main.

---

## TL;DR headline numbers

| Metric | Value | Build plane / notes |
|---|---|---|
| **Boot frontier** | `main_screen` → **`game_screen` (state=playing)** | Lane A; full UI chain attract→…→game_screen, EXIT=0 over 5000 frames, worldLoaded=1 venuePresent=1 |
| **What unblocked gameplay** | `Sound::SynthPoll` — **TWO** real decomp bugs (not one) | mSamples wrong-erase (SIGSEGV double-free) + mDelayArgs passes `this` instead of `cur->mEventReceiver` |
| **SynthPoll PPC match** | **79.3% → 91.7%** normalized | Lane A worktree plane; residual ~8% is FPR/scheduling floor — RE-MEASURE on main |
| **Feet gate** | **REACHABLE now, FAILS by design** | worst toe Z **−4.30** (ankle +1.6..+2.0); residual sharpened to a TOE-vs-ANKLE divergence, not a whole-foot drop |
| **Unicorn flip-list** | **60 candidate real bugs** under stale "equivalent" certs | of 305 EQUIVALENT flips: 225 signal-version churn + 19 artifact + 60 candidate-bug + 1 other |
| **Unicorn cert census** | stale-backed **843 → 3**; certifiable **970 → 948** | measured on a COPY of live DB; certs re-distribute toward weaker floor classes (more honest, not bigger) |
| **Live bugs fixed (Lane C)** | **4 functions fixed-with-tests** (threshold met) | DecodeDxt5Alpha, SetCrossfadeJump, EnableDetector, **CSHA1::Transform** (new this round); 14 GTest cases |
| **Scoring mechanism named** | **PCH-staleness worktree rebuild** + `clean_stale_objects.sh` skips `.c` files | Lane D; obj-level byte/md5 evidence (parsedate three distinct binaries); on main 8/20 COMPLETE (was claimed 9) |

**One thing requires care before merge (not a blocker):** lanes **A and C both edit
`native/CMakeLists.txt` at the same line** → a real git merge conflict (trivial to
resolve, keep both test-registration blocks). See the merge-order section. Everything else
merges cleanly.

---

## Per-lane outcomes

### Lane A — gameplay unblock + feet-bug residual (Opus) — **PASS**

- **Branch:** `wave3/a-gameplay-feet` (commit `d315f725`) · **Worktree:** `/home/free/code/milohax/wt-wave3-a-gameplay-feet`
- **Files (5):** `src/system/synth/Sound.cpp`, `native/tests/test_sound_synthpoll.cpp` (new, 4 tests),
  `native/CMakeLists.txt`, `docs/.../15-native-stub-worklist.md`, `docs/.../19-feet-ik-wave3.md` (new).
- **SynthPoll — two distinct decomp bugs (root-caused from the Xbox asm):**
  **(1)** the `mSamples` cleanup loop did `cur=*it; it++; mSamples.erase(it)` — erasing the
  element *after* `cur` and `erase(end())` on the last node (the double-free SIGSEGV). The
  target saves the pre-increment iterator (asm idx 57/59) and erases *that* node (idx 78). Fix:
  save `auto curIt = it` before `it++`, erase `curIt`. **(2)** the `mDelayArgs` delayed-play
  loop forwarded `this` as `Play`'s event-receiver arg; the target loads `cur->mEventReceiver`
  (idx 26 `lwz r7,0xc,r31` — obj is the 4th arg in r7). Both are pure source-logic corrections
  that improve host behavior AND the PPC match — **not** HX_NATIVE guards.
- **PPC neutrality / improvement (Lane A worktree plane, run_objdiff):** `?SynthPoll@Sound@@UAAXXZ`
  **79.3% → 91.7%** normalized (re-measured by the verifier, exact match to claim). `Sound::Stop`
  (a similar untouched loop in the same unit) stays **100.0%** — no regression. Residual ~8% is
  FPR/scheduling noise (commutative `fadds`, fader `GetVal` stack-slot order, SetPan/SetSpeed
  scheduling) — behaviorally neutral lowering diffs.
- **Boot frontier:** with SynthPoll fixed, `dc3-native` now boots the full UI chain
  attract→title→main→choose_mode→song_select→loading→…→**`game_screen`**, final
  state=playing, worldLoaded=1 venuePresent=1 doSongAnim=1, **EXIT=0** over 5000 frames.
  Strictly past the Wave-2 `main_screen` frontier.
- **Gameplay stub worklist:** captured live at `game_screen` (`/api/stubs`): 5 distinct / 2304
  hits — OutputDebugStringA 2011, vorbis_synthesis_poll 290, DmGetSystemInfo 1, DmMapDevkitDrive 1,
  **NuiIdentityAbort 1** (the only NEW stub vs the boot-only table). Appended to doc 15.
- **Feet gate (`FeetNotBelowFloorDuringGameplay`):** now **REACHABLE** (was BLOCKED in Wave-2 on
  the boot crash). Runs and **FAILS as designed** with real telemetry — 818 foot samples, L-toe
  worst Z **−4.30** (ankle +2.00, 801/818 below floor), R-toe worst Z **−4.30** (ankle +1.60,
  781/818 below). Residual sharpened: the **ankle plants but the toe sinks ~4u** — a TOE-vs-ANKLE
  divergence on the gameplay song-move/poll-order path (`HamDriver.cpp:95-101`). Closed leads
  (DoFSM int/float, mConstraints) not re-litigated. Documented in new doc 19.
- **Contradictions:** Wave-2 docs described SynthPoll as a *single* mSamples double-free — there
  are **two** bugs (the mDelayArgs `this`-vs-`mEventReceiver` bug was not previously noted).
  Wave-2 follow-up #1 implied `vorbis_synthesis_poll` was the gate to gameplay — it is **NOT**;
  fixing the SynthPoll iterator logic alone unblocked the full gameplay boot, and
  vorbis_synthesis_poll remains a no-audio stub that does not block reaching game_screen. The
  audit expectation that the gameplay path would surface many high-value new stubs is **not borne
  out** (same handful as boot + one Kinect shim). Wave-2 18-doc's feet residual was "UNCONFIRMED,
  gate unreachable" — now confirmed reachable and refined to toe-vs-ankle (the whole foot does not
  drop, only the toe sinks).
- **Risks:** the feet bug itself is **NOT fixed** — the gate still fails by design (toe −4.3); a
  fix was deliberately not attempted to avoid endangering the now-working gameplay boot (deep IK
  poll-order/read-stale divergence, multiple prior plant-repair experiments already gated off as
  destabilizing). Needs a dedicated IK lane iterating against the live gate. The SynthPoll match is
  91.7% **in the worktree**, not 100% — re-measure on main. 8 pre-existing milo-tests failures
  (ObjectLifetime/MergeScope/MeshVertexLoading/RndCamProjection) exist in the baseline, unrelated to
  this lane (verified isolated to the Sound unit). The `erase(it++)` std::list idiom is the
  intentional match-driven form, not accidental UB. dc3-native in a worktree needs `DC3_DATA`
  pointing at the main repo's `orig-assets`.
- **Verdict required-fixes:** none.

### Lane B — unicorn evidence refresh (Opus) — **PASS**

- **Branch:** `wave3/b-unicorn-refresh` (commit `24f8c32d`) · **Worktree:** `/home/free/code/milohax/wt-wave3-b-unicorn-refresh`
- **Files (6):** `scripts/unicorn/source_hash.py` (new), `scripts/unicorn/refresh_frontier.py` (new),
  `scripts/unicorn/apply_refresh.py` (new), `scripts/unicorn/test_refresh.py` (new, 12 tests),
  `docs/.../20-unicorn-evidence-refresh.md` (new), `.gitignore` (worktree result artifacts).
- **Runner identified (no rebuild):** `scripts/unicorn/batch_to_db.py` drives `scripts/unicorn_runner/`
  (Unicorn PPC32-BE differential execution, 2-fill probe schedule); deps present (unicorn 2.1.4,
  capstone 5.0.7).
- **Source-hash freshness gate (new):** per-function sha256 of the decomp `.text` COMDAT bytes +
  ordered reloc offset/type/target-name list. This gates on *source/codegen change* where the
  existing `unicorn_signal_version` gates on *runner-semantics change* — together a complete
  freshness story. Verifier independently recomputed `function_source_hash(...Rand.obj, ?Seed...)`
  = `3512be973af2dacb` deterministically, matching the stored value.
- **Frontier re-run (Lane B worktree plane @ `1d8409f4`, obj cache reflink-identical to main):**
  1,312 of 1,314 authorable-partial fns across 414 units in **33s, reproducible** (8 SKIPPED for
  anon-namespace name skew / stub-size mismatch). Results written to a **worktree-local DB + JSON**,
  never the live decomp.db.
- **THE FLIP-LIST (the deliverable):** of **600 stale-EQUIVALENT** certs, **295 stay EQUIVALENT**
  and **305 flip**, adjudicated by cause: **225 signal_version churn** (expected — the v2
  cap-exhaustion rule, NOT new bugs), **19 cosmetic-artifact** reclassifications, **60 CANDIDATE
  REAL BUGS** hiding under stale "equivalent" certs, +1 other (EQUIV→SKIPPED). Worked example:
  **`Rand::Seed`** writes the MT-state array with wrong high-16-bits (same signed-arith family as
  the Wave-2 `Rand::Int` fix) — independently re-executed by the verifier as DIVERGENT (20 memory
  diffs, decomp `0xFFFFxxxx` vs orig `0x5665xxxx`), a confirmed real divergence masked by a stale
  equivalent cert. The 60 candidate classes: cap_exhausted_decomp 22, call_arg 19,
  cap_exhausted_orig 10, object_memory 5, call_count 3, unmapped 1; 57/60 stable_divergent.
- **No-evidence coverage:** 334 of 335 newly tested — 93 EQUIVALENT, 236 DIVERGENT, 5 SKIPPED.
- **Cert census delta (certify_floor.py on a COPY of live decomp.db @ `1d8409f4`):** stale-backed
  certs **843 → 3**; certifiable **970 → 948** (equivalent 600→415, artifact 246→111, icf 16→84,
  permuter 108→338). 12/12 unit tests pass.
- **Contradictions:** Wave-2 doc 94 / 17-runbook treated the 843 stale certs as "valid signals,
  just dated" — but 60 were MASKING real divergences and 225 more rested on the looser pre-v2
  cap-exhaustion-as-EQUIVALENT rule. "An unedited EQUIVALENT fn is still EQUIVALENT" holds only
  for the SAME signal version. Doc 94's "344 no-evidence" → the live DB measures **335** (9-fn
  drift). The assumption a refresh would *grow* the cert count is wrong: certifiable shrank
  970→948 and re-distributed toward weaker floor classes — the value is FRESH honest certs plus
  the flip-list, not a higher count.
- **Risks (carry into adjudication):** the 60 flips are CANDIDATES, not confirmed bugs. The
  call_arg subset (19) is mostly likely-false-positive `__FILE__`/MakeString-pointer noise; the
  one-sided cap-exhaustion subset (32) may be zero-fill fixture artifacts. **Strongest signals:**
  the 9 object_memory/call_count/unmapped + `Rand::Seed`. **CERT-MASKING caveat:** after the
  refresh lands, **40 of the 60** candidate bugs fall through to a weaker-but-valid floor cert
  (permuter_exhausted 30, icf_merged 10) via precedence; only 20 become fully "open." **The
  flip-list — not the cert column — is the source of truth for divergence-to-fix.** The 26
  recovered (DIVERGENT→EQUIVALENT) include 13 prior orig_error — verify a runner-semantics change
  drove them before granting equivalent certs. All verdicts carry the standard zero/0xCD
  auto-fixture mock-fidelity caveat ("behaviorally identical under the null/fill path," not a
  formal proof).
- **Verdict required-fixes:** none.

### Lane C — native live-bug burndown with tests (Opus) — **PASS** *(after one repair round)*

- **Branch:** `wave3/c-live-bugs` (2 commits: `2ddec493` prior pass + `97b649d2` repair) · **Worktree:** `/home/free/code/milohax/wt-wave3-c-live-bugs`
- **Files (11):** `src/system/math/SHA1.{cpp,h}`, `src/system/hamobj/HamAudio.{cpp,h}`,
  `src/system/hamobj/MoveAsyncDetector.cpp`, `src/system/rndobj/Bitmap.cpp`,
  `native/tests/test_{dxt5_alpha,crossfade_jump,enable_detector,sha1}.cpp`, `native/CMakeLists.txt`.
- **Acceptance met:** **4 functions fixed-with-tests** (threshold ≥4), each with a regression test
  — **14 GTest cases total** (4 Dxt5Alpha + 3 HamAudioCrossfade + 2 EnableDetector + 5 Sha1), all
  14 PASS from `orig-assets` cwd (re-run by the verifier).
- **New fix this round — `CSHA1::Transform` (math/SHA1):** the prior pass escalated this as the
  highest-value target; now fully root-caused. The decompiled source is faithful to the
  big-endian ILP32-long Xbox target but **doubly wrong on the little-endian LP64 native/web host:**
  **(1)** `unsigned long l[16]` is 64-bit on LP64 — `l[16]` is 128 bytes (overruns the 64-byte
  union) and the SHA1 word math runs on 64-bit words; the round-state locals `a..e` are also
  `unsigned long`, so `rol()`/`blk()` never wrap at 32 bits. **(2)** even with 32-bit words the
  host reads each message word little-endian — the reverse of the big-endian order SHA1 expects.
  All three sub-fixes are under `#ifdef HX_NATIVE` (32-bit `unsigned int` union word + 32-bit
  `a..e` + a byteswap in `blk0`); the PPC source keeps `unsigned long` verbatim. Verified against
  FIPS-180 vectors (empty, 'abc', 56-byte, 1000-'a' — verifier independently recomputed all four
  via Python hashlib, exact match). Affects all CSHA1 users (HDCache save-data hashing,
  StreamChecksum asset integrity).
- **EnableDetector test added:** `test_enable_detector.cpp` (2 cases) pins the activation transition
  the prior pass fixed (mActive set, frame offsets reset to −1, `mLastDetectFracs` cleared to
  *integer*-zero bits via bit-cast). Private access via the `#define private public` test-macro
  hack — **no shipping-header churn** (unlike the HamAudio test setters). Test-first validated:
  fails when the int-store clear is removed (fracs stay poisoned `0x7FC00000`).
- **PPC neutrality (Lane C worktree plane, run_objdiff — all re-measured by the verifier):**
  `CSHA1::Transform` **55.7% → 55.7%** (HX_NATIVE-only, PPC byte-identical — a giant-unrolled
  regswap floor), `EnableDetector` 97.3%, `DecodeDxt5Alpha` 80.5%, `SetCrossfadeJump` 85.2% (bgt↔bge
  diff_op stays eliminated; remaining lfd↔bl is the `__savefpr_28` epilogue floor). No regression on
  any of the 4.
- **Contradictions:** the prior Lane-C report's CSHA1 contradiction ("byteswap necessary but
  insufficient — 2nd divergence in Final/m_count") was a **misdiagnosis** — Final/m_count use
  endian-independent byte extraction and are correct; the real second divergence is the LP64 word
  size. CSHA1::Transform is now a FIXED native bug with tests, not an escalation. Doc 11's "53-fn
  definitely-live" framing remains optimistic (most are regalloc/FPR/stack floors), but the set DOES
  contain real isolatable behavioral bugs beyond the named ones — CSHA1::Transform was NOT in doc
  11's named list yet is a genuine high-impact fix; per-function asm diagnosis (not just the name
  list) is the right discovery method. `ThreadTask::Replace` (obj/Task, 82.7%) remains DEFERRED
  (needs DWARF/Ghidra intent for erase vs remove).
- **Risks:** the CSHA1 fix is entirely HX_NATIVE-guarded (proven invisible to PPC), so no PPC-match
  risk. The EnableDetector test constructs detectors via raw `operator new` + memset(0) + placement-new
  of only the vector/set members — sound for libstdc++'s empty-vector representation, but
  setup-fragile if that representation ever changed (the assertions are representation-independent).
  **Pre-existing carryover (the verifier's one required-fix, NON-BLOCKING):** `HamAudio.h` still
  contains two **unguarded public test-only methods** (`SetStreamsForTest`, `SetCrossfadeStateForTest`)
  from the prior commit `2ddec493` — should be compile-gated (`#ifdef MILO_TESTS`/`HX_NATIVE`) in a
  follow-up. A full serialized ctest (GPU/death-test heavy) was not run end-to-end; the 4 lane suites
  + a 73-test pure-logic subset are green and orthogonal to the GPU/media suites (only failure is the
  pre-existing ObjectLifetime cascade bug).
- **Verdict required-fixes:** one NON-BLOCKING follow-up — compile-gate the two unguarded
  `HamAudio.h` test setters.

### Lane D — scoring-plane reconciliation (Sonnet) — **PASS** *(two doc-text fixes)*

- **Branch:** `wave3/d-scoring-recon` (commit `9794d52f`) · **Worktree:** `/home/free/code/milohax/wt-wave3-d-scoring-recon`
- **Files (1, doc-only):** `docs/.../16-single-blocker-recert.md` (appended a 122-line WAVE-3
  RECONCILIATION section). No source edits, no DB writes.
- **Re-measured all 20 blockers on MAIN** (`project_dir=/home/free/code/milohax/dc3-decomp`,
  sequential run_objdiff). Result: **8/20 confirmed COMPLETE on main** (was claimed 9 in Wave-2;
  **`parsedate` demoted to 99.8%** — 1 mismatch `subi r29,r21,0x50` vs `0x4c,weekday` data-addend).
  The 8 COMPLETE (verifier re-confirmed each at 100.0% on main): `UsbMidiGuitar::Poll`,
  `BinkMovieImpl::PlatformCacheFile`, `UIListSlot::Draw`, `CharServoBone::DoRegulate`,
  `Curl_http_readwrite_headers`, `Curl_proxyCONNECT`, `CampaignPerformer::OnMovePassed`,
  `FitnessGoalMgr::QueueCmdChangeProfileOnlineID`.
- **Mechanism NAMED with obj-level evidence:** the worktree-vs-main divergence is **PCH-staleness
  rebuilds of `.c`-sourced objects** during `setup_worktree.sh`, compounded by a **bug in
  `clean_stale_objects.sh` that silently skips `.c` files** (it maps `.obj`→`.cpp`; for a `.c`
  source the `.cpp` doesn't exist, so the `-f` check fails and the file is skipped). Decisive
  evidence: `parsedate.obj` exists as **three distinct binaries** — target=11177 B (Mar 18,
  md5 `8594e914`), main-base=11273 B (May 12, md5 `f5deb791`), worktree-base=11289 B (Jun 10) —
  proving the worktree's ninja warmup rebuilt the `.c` object to different output. `EstimateDraw`
  (Rnd_NG) improved 97.6% → **99.6%** via a genuine post-Wave-2 source commit (the DxRnd
  Resume/Suspend vtable-slot swap in `Rnd_NG.h`).
- **Contradictions:** doc 16's Wave-2 table claimed 9/20 COMPLETE — corrected to **8/20**
  (parsedate 99.8% on main, not 100%). EstimateDraw was listed 97.6% NeedsInvestigation —
  corrected to 99.6% MaybeFixable (genuine improvement).
- **Risks:** `parsedate`'s `subi` addend mismatch (`0x50` vs `0x4c`+weekday) is a real decomp bug
  needing a source fix (the data symbol reference is missing/wrong) — do NOT `--promote` it.
  `clean_stale_objects.sh` silently skips all `.c`-sourced objects, so any worktree measurement of
  curl/lib, json-c, or other `.c` units may differ from main if the warmup triggered a PCH rebuild.
  The 8 COMPLETE units must be verified by `sync_match_percent.py --promote` (the authoritative
  gate) before counting in `authorable_done`.
- **Verdict required-fixes (both DOC-TEXT, NON-BLOCKING — fix in doc 16 with the merge):**
  1. **Wrong commit attribution.** Doc 16 credits `5aed1dca` (a docs-only commit) for the
     EstimateDraw improvement. The actual source-change commit is **`c0ad4a96`** ("match(platform):
     Xbox platform cluster"), whose message states "DxRnd Resume/Suspend VTABLE SLOTS were swapped
     in Rnd_NG.h"; the Rnd_NG.obj rebuild timestamp is consistent with `c0ad4a96`. Replace
     `5aed1dca` with `c0ad4a96` in doc 16.
  2. **Misleading metric.** The measurement "2224/2224 (all in obj/ dir)" conflates
     `build/373307D9/obj/` (immutable target-binary extracts, never rebuilt, irrelevant to the
     stale-check) with `build/373307D9/src/` (the `.c`-compiled base objects the bug actually
     affects). The real affected count is **~120 `.c`-sourced objs in `build/373307D9/src/`**.
     Correct the metric and drop the misleading `obj/` reference.

---

## Consolidated decomp.db apply-steps runbook

**Single writer:** the orchestrator runs these on `main` after merging the branches. In Wave 3,
**Lane B's apply is the only `decomp.db` writer.** Lanes A (native source+build+ctest), C (shared
source + native tests), and D (doc-only) are DB-read-only. As in Wave 2, the certs gate off
`match_percent_normalized`, so **`sync_match_percent.py` must run first.** Run from repo root on
`main`.

```bash
# 0. Merge the branches first (see merge-order section), resolving the A/C
#    native/CMakeLists.txt conflict (keep both test-registration blocks). Then:

# 1. Make match_percent_normalized current AND certify the new wins.
#    Promotes Lane A's SynthPoll (~79 -> ~92 on main) and re-measures Lane C/D functions.
#    reconcile check (a) may flag the SynthPoll drift on the next run — EXPECTED and benign
#    (a real improvement, not a stale cache).
python3 scripts/sync_match_percent.py --build --promote --demote

# 2. Clear residual db-only stale stubs (harmless if already clean).
python3 scripts/reconcile_db.py --fix

# 3. Make Lane B's refresh results available on main (the results DB is gitignored — copy it
#    across or pass its path with --results). Then DRY-RUN preview (writes nothing):
RES=/home/free/code/milohax/wt-wave3-b-unicorn-refresh/unicorn_refresh.db
python3 scripts/unicorn/apply_refresh.py --results "$RES"          # expect: WOULD UPDATE 1304, not in live DB 0

# 4. APPLY Lane B's fresh unicorn evidence (single writer). Adds unicorn_source_hash +
#    unicorn_source_hash_at columns and writes 1304 fresh verdicts+hashes; 8 SKIPPED fns keep
#    their prior verdict.
python3 scripts/unicorn/apply_refresh.py --results "$RES" --apply

# 5. Re-certify from the FRESH evidence (replaces the Wave-2 stale-backed certs).
#    Dry-run first (expect ~948 certifiable, stale-backed 843 -> 3), then apply.
python3 scripts/certify_floor.py
python3 scripts/certify_floor.py --migrate --apply

# 6. Confirm + record.
python3 scripts/reconcile_db.py            # expect check (e) drift = 0
python3 scripts/certify_floor.py --summary

# 7. (optional, after merge) Promote Lane D's 8 confirmed-COMPLETE single-blocker units.
#    Do NOT promote parsedate (99.8% on main — needs a source fix for the subi addend).
python3 scripts/sync_match_percent.py --build --promote
```

**Cadence (wire, do not crontab):** re-run `scripts/unicorn/refresh_frontier.py` (~33s) after any
sync that moves percents, then `apply_refresh.py --only-fresh-source` (skips rows whose source-hash
is unchanged), then `reconcile_db.py --fix` + `certify_floor.py --apply`.

**ADJUDICATION (hand to a Wave-4 fix lane):** triage the 60-row candidate-bug flip-list in
`unicorn_refresh.json` — start with the 9 strong object_memory/call_count/unmapped flips +
`Rand::Seed`. **Use the flip-list, NOT the cert column**, as the divergence source of truth: 40 of
the 60 fall through to a weaker-but-valid floor cert post-refresh.

---

## Merge order for `wave3/*` branches (with cross-lane conflict check)

`git diff --name-only main wave3/<lane>` was run for all four branches. **There is ONE real git
conflict** — lanes A and C both insert at the same line of `native/CMakeLists.txt`. Verified with
`git merge-tree`: the conflict block is the test-registration list (A adds
`tests/test_sound_synthpoll.cpp`; C adds `tests/test_{dxt5_alpha,crossfade_jump,enable_detector,sha1}.cpp`)
both immediately after `tests/test_native_boot_crashes.cpp`. **Resolution is trivial — keep both
blocks** (union the inserted lines). All other paths are disjoint.

Also note (NOT git conflicts): A's `19-feet-ik-wave3.md` and B's `20-unicorn-evidence-refresh.md`
share the `19-` number (distinct filenames — renumber one post-merge to keep the index monotonic).
Lane A and Lane C do NOT share any synth/ source file (A owns `src/system/synth/Sound.cpp`; C touches
`hamobj/HamAudio`, `math/SHA1`, `rndobj/Bitmap`), and their new test `.cpp` files are distinct names
— the ONLY collision is the CMakeLists registration line.

Recommended order:

1. **`wave3/a-gameplay-feet`** (`d315f725`) — merge first. Lands the SynthPoll source fix (the
   gameplay unblock) and registers its test. All edits are real source-logic corrections (no
   HX_NATIVE guard) that improve both host behavior and the PPC match. Build post-merge with
   `-DCMAKE_BUILD_TYPE=RelWithDebInfo -DDawn_DIR=/home/free/code/milohax/dc3-decomp-deps/dawn/lib/cmake/Dawn
   -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++`.
2. **`wave3/c-live-bugs`** (`2ddec493` + `97b649d2`) — merge after A. **Resolve the
   `native/CMakeLists.txt` conflict here** (keep both A's and C's test-registration lines). Then
   `ninja milo-tests` and run the 14 GTest cases (`Sha1.*:EnableDetector.*:Dxt5Alpha.*:HamAudioCrossfade.*`)
   from `orig-assets` — expect 14/14. Apply the non-blocking HamAudio.h test-setter compile-gate as
   a follow-up.
3. **`wave3/d-scoring-recon`** (`9794d52f`) — independent, doc-only. Apply the two doc-text fixes
   (commit attribution `5aed1dca`→`c0ad4a96`; the ~120-`.c`-objs metric) to doc 16 with the merge.
4. **`wave3/b-unicorn-refresh`** (`24f8c32d`) — merge last among the functional lanes; run its DB
   apply step **after** `sync_match_percent.py` (runbook above). Only Wave-3 DB writer; its new
   `scripts/unicorn/*` files don't collide with any other branch.

A/B/C/D are otherwise git-independent. The order above is the recommended one (A first to land the
gameplay unblock; B's DB step last after the sync).

---

## What blocks merging

**Nothing hard-blocks merge.** All four lanes pass. The required care items:

- **CROSS-LANE CONFLICT (must resolve, trivial):** lanes A and C both edit
  `native/CMakeLists.txt` at the same insertion line. Resolve by keeping both test-registration
  blocks when merging C after A. This is the one item that cannot be a clean auto-merge.
- **One runbook ordering rule (must follow):** Lane B's `apply_refresh.py --apply` and
  `certify_floor.py --apply` MUST run **after** `sync_match_percent.py` (certs and the refresh
  freshness gate both key off `match_percent_normalized`).
- **Lane C NON-BLOCKING follow-up:** compile-gate the two unguarded public test-only methods in
  `HamAudio.h` (`SetStreamsForTest`, `SetCrossfadeStateForTest`).
- **Lane D NON-BLOCKING doc-text fixes:** correct the commit attribution (`5aed1dca`→`c0ad4a96`)
  and the misleading "2224/2224" metric (real: ~120 `.c`-sourced objs in `build/373307D9/src/`) in
  doc 16.
- **Note for the orchestrator:** main is at `1d8409f4` (the Wave-3 plan doc), not `85d2aa78` as the
  plan text stated — Wave 2 already landed. The Wave-3 build-plane numbers (SynthPoll 91.7%, the
  4 Lane-C functions, the unicorn census) are worktree readings; the final certs are on main after
  the sync.

---

## Open follow-ups for Wave 4

1. **Dedicated IK lane against the live feet gate.** The gameplay boot now reaches `game_screen` and
   `FeetNotBelowFloorDuringGameplay` runs and fails by design (toe Z −4.30, ankle plants at
   +1.6..+2.0). The residual is sharpened to a **TOE-vs-ANKLE divergence** on the gameplay
   song-move/poll-order path (`HamDriver.cpp:95-101`). Attack it with the gate live; CLOSED leads
   (DoFSM int/float, mConstraints) must NOT be re-litigated. Prior plant-repair experiments
   (CharIKFoot Push 13/14, Dc3CleanPlant) were gated off as destabilizing — a fix must not endanger
   the now-working boot. May need empirical native poll-order capture and/or Xenia live ground truth.
2. **Adjudicate the 60-row unicorn candidate-bug flip-list** (Lane B's deliverable, in
   `unicorn_refresh.json`). Start with the 9 strong object_memory/call_count/unmapped + `Rand::Seed`
   (a confirmed signed-arith MT-state bug — same family as the Wave-2 Rand::Int fix). Hand confirmed
   bugs to a fix lane with tests. REMEMBER: use the flip-list, not the cert column (40/60 are
   cert-masked). Verify the 26 DIVERGENT→EQUIVALENT recoveries (13 prior orig_error) are
   runner-semantics changes, not fixture flakes, before granting equivalent certs.
3. **Fix `parsedate`** (Lane D): the `subi` addend `0x50` vs `0x4c,weekday` is a real decomp bug —
   a missing/wrong data-symbol reference in the source. After the fix, promote it (currently 99.8%
   on main, deliberately NOT promoted). Then push the remaining single-blocker cohort (Lane D's
   LikelyFixable: HttpReqCurl::WriteMemoryCallback, CharIKRod::Copy, IdentityInfo::Identified,
   CharIKHead::Poll, CheatsManager::CallCheatScript; MaybeFixable: PropSync FPR regswap).
4. **Fix `clean_stale_objects.sh` to handle `.c` sources** (Lane D mechanism): add a `.c` branch in
   the stale-mode loop alongside the `.cpp` mapping. Until fixed, ~120 `.c`-sourced objs in
   `build/373307D9/src/` are silently skipped, so worktree measurements of curl/lib, json-c, and
   other `.c` units may diverge from main after a worktree ninja warmup. This is the root of the
   Wave-2 "9 promotable" over-count.
5. **Continue the native live-bug burndown** (Lane C method). The doc-11 53-fn set's named entries
   are mostly floors, but per-function asm + native-semantics diagnosis found a genuine isolatable
   bug NOT on the list (`CSHA1::Transform`). The unexamined ~36 of the 53 likely hold a few more.
   `ThreadTask::Replace` (82.7%) is still DEFERRED pending DWARF/Ghidra intent (erase vs remove).
6. **Compile-gate the `HamAudio.h` test setters** and audit for any other unguarded test-only seams
   in shipping headers (the Lane C non-blocking required-fix).
7. **Wire the unicorn refresh cadence** into the post-sync flow (`refresh_frontier.py` ~33s +
   `apply_refresh.py --only-fresh-source` + recert) so the floor certs never go 98 days stale again.
   The source-hash gate makes staleness detectable per-row instead of just dated.
8. **Triage the 8 pre-existing milo-tests failures** (ObjectLifetime MergeDirs/MergeScope,
   MeshVertexLoading skinning-decode, RndCamProjection GPU-math) — present in the baseline,
   unrelated to any Wave-3 lane, but they should be cleared before the suite is a trustworthy gate.
9. **Attack the genuine open residual** behind the now-fresh `authorable_done` view (post-refresh
   census), prioritizing the routable/real-bug classes left open over the certified cosmetic floors.
10. **Renumber the colliding `19-*` docs** (Lane A `19-feet-ik-wave3.md` vs Lane B
    `20-unicorn-evidence-refresh.md`) to keep the investigation index monotonic. Cosmetic.
