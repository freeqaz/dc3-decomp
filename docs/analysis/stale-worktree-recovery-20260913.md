# Stale-worktree recovery, 2026-09-13: five lanes adjudicated, nothing landed

An audit counted 22 dc3 worktrees holding committed work that never reached
`main`. Five were handed to a recovery lane as the most promising. **All five
turned out to be non-landable, and the reason matters: four of them were
*parallel duplicates* whose findings had already reached `main` through a
sibling lane, usually in a more developed form.** "Committed but never merged"
is not the same as "unlanded work", and an audit that counts branches cannot
tell the two apart.

Ruler `functionRelocDiffs=name_check`, `objdiff-cli 4.2.8 (032122696555, xxh3
14ac591a0814e6c9)`. Every number below is from a full `ninja` in a worktree,
never from `decomp.db`.

**Two different baselines, because `main` moved mid-task** — stated rather than
smoothed over, since the reflog is the only reason it was noticed:

* the `cert-near100` numbers are against **`e8fb81efc`**;
* the `harvest` numbers are against **`9fea5f811`**, which landed 10 commits
  later.

Eight of the nine harvest TUs are byte-identical across that move, so those
baselines are unaffected. The ninth is not, and it matters: `37cd16a2d`
("`UpdateFromDepthBufferClip`: the clip coords are int, not unsigned — 87.8 →
97.3") is the rewrite that makes the harvest edit for that function stale, and
it landed *inside* the window. The 97.27273 quoted below is post-rewrite.

## Verdicts

| branch | claim | verdict |
|---|---|---|
| `audit/cert-near100-20260820` | 2 named wins | both superseded; **no measurable effect** |
| `fix/scope-idx-5-20260819` | `DingoJob::SendCallback` 82.6→90.9 | superseded by lane 6 (→99.1); already a documented non-merge |
| `verify/guard-leaks` | `DelayEffect::Process` 95.7→99.4 + detector | byte-identical fix already on `main`; detector on `main` is 1094 lines vs 653 |
| `verify/rebase-probe` | 4 `ObjPtr` source claims | all four already true on `main`; merging would **revert** native-engine work |
| `harvest-sweep-wins` (9 dirty files) | permuter sweep wins | 3 already landed, 1 stale, 3 inert on 100% functions, 1 noise, **1 contradicts the target** |

## `audit/cert-near100-20260820`

`MultiTempoTempoMap::PointForTime` — `git rebase main` **dropped the commit as
"patch contents already upstream"**. Confirms at 100.0%, 44 instructions, all
equal.

`FloatKeys::SetFrame` — the branch hoists `val`/`ref`/`prev`/`next` to the outer
scope; `main` already hoists `val` alone. Built both ways in one worktree:

| version | canonical | instructions | rows |
|---|---|---|---|
| `main` (hoists `val` only) | 100.0% | 162 | 1 `diff_arg`: `fmuls` f31↔f0 |
| branch (hoists all four) | 100.0% | 162 | 1 `diff_arg`: `fmuls` f31↔f0 |

Identical. The extra hoist buys nothing and costs readability — it moves three
declarations out of the only branch that uses them, leaving them uninitialised
on the other path. Dropped.

## `fix/scope-idx-5-20260819`

Already adjudicated on `main`, by name, in
[`docs/decomp/patterns/fixable-scope-index.md`](../decomp/patterns/fixable-scope-index.md)
("Two claims about this instrument that were tested and REFUTED"). `main`'s
`DingoJob::SendCallback` is a strict **superset** of the branch's: same
single-braced `if`, same `TheServer.Logout()`, same
`static ServerStatusChangedMsg msg(kServerStatusDisconnected); TheServer.Export(msg, true)`,
plus `session_id`, an `OnlineID` temp and corrected pair ordering.

The disconnect broadcast is therefore corroborated by two independent lanes,
which is worth recording — the brief flagged it as a behavioural claim needing
verification, and it holds.

The doc says one thing was salvaged from this lane, the back-reference blanking.
**Verified by content, not by the claim**: `BACKREF = re.compile(r'@(\d)@')` is
at `scripts/analysis/scope_index_census.py:161`. Landing the branch's census
would remove 408 lines `main` has.

## `verify/guard-leaks`

`main`'s `src/system/dsp/DelayEffect.cpp` already carries the `#ifdef HX_NATIVE`
guard with a **byte-identical** comment, landed as `8d7ac01b3`. `main` then went
further with 8 more commits, including a retraction the branch never made
("the insert count is the cheap discriminator" — 76% wrong) and a correction to
the detector's own arithmetic. Detector: `main` 1094 lines, branch 653.

## `verify/rebase-probe`

All four source claims are already true on `main`, verified by content:

- no `__declspec(noinline)` anywhere in `ObjPtr_p.h` / `Object.h`
- `ObjPtrVec::Set` is the plain `erase(it)` / `SetObjConcrete(obj)` form
- `ObjPtrList::Unlink` carries the "single `mSize--`, single return" shape
- `StandardStream.h` is byte-identical between branch and `main`

Merging it would have been actively harmful: the branch predates `RefAudit`,
`DeathWatch`, `friend class DefaultPhysicsManager`, `push_front`, and
`PruneDeadRefs` — the last of which explicitly *replaced* the `mRefs.Clear()`
"ring amnesia" bug. It would also re-break `icf_bucket_census.py`, whose branch
version does `sys.path.insert(0,"/tmp/vfy"); import foldcheck` — the exact defect
`main`'s version documents fixing ("it was written in /tmp during verification;
importing it from there is why the committed copy did not run").

## `harvest-sweep-wins` — the one worth reading

Nine uncommitted files dated 2026-06-02, raw permuter output (`_tmp0`, `_ref0`,
`_tmp5`). Preserved verbatim first, before evaluation, as `217991af5` on
**`recover/harvest-dirty-20260602`** — unmerged, and it should stay that way.

Baselines on today's `main`:

| function | canonical | disposition |
|---|---|---|
| `NetCacheMgr::AddLoaderRef` | 88.90476 | edit already on `main` as `listEnd` |
| `XboxContentMgr::PollRefresh` | 89.63745 | → 89.64143 (+0.004pp) |
| `RhythmDetector::ProcessFrames` | 91.12013 | → 91.88312 **but wrong — see below** |
| `RndText::ReplaceMissingCharacters` | 93.80347 | edit already on `main` |
| `NgSpotlightDrawer::RenderSphere` | 94.49515 | edit already on `main` |
| `LiveCameraInput::…UpdateFromDepthBufferClip` | 97.27273 | stale — `main` rewrote the function |
| `RhythmBattlePlayer::AnimateBoxyState` | 100.0 | redundant null check, inert |
| `AnimTask::Poll` | 100.0 | **semantically broken** |
| `PoseFatalities::UpdateMatchingPose` | 100.0 | `_tmp0` hoist, inert |

`AnimTask::Poll`'s edit rewrites `frame = Mod(frame - mMin, mMax - mMin) + mMin`
as `frame = mMin; frame += Mod(frame - mMin, …)` — after the first statement the
`Mod` argument is identically zero. The function is already 100% on `main`.

### A higher percentage that contradicts the target

`RhythmDetector::ProcessFrames` is the one to remember. The edit changes
`tickDiff > 0` to `tickDiff >= 0` and **measures 0.76pp higher**
(91.12013 → 91.88312). It is still wrong:

```
idx 70:  TGT ble  cr6   <-- skip body when tickDiff <= 0, i.e. `> 0`
         SRC blt  cr6   <-- skip body when tickDiff <  0, i.e. `>= 0`
```

`main`'s version reports `diff_op: none (good!)`. The `>= 0` version *introduces*
that opcode divergence and pays for it out of unrelated register-allocation
churn elsewhere in a 317-instruction function. It also changes behaviour:
`hadBlendedFrames = true` would fire when `tickDiff == 0`, with the loop body
running zero times.

> **A percentage is not proof of correctness, and it is not even proof of
> *local* correctness.** In a function with ~108 register-swap rows, a real
> opcode regression is comfortably cheaper than the scheduling noise it
> displaces. Read the `diff_op` list, not just the headline — and when an edit
> changes a comparison, find the branch it lowers to and check its polarity
> against the target.

## Method note

Four of these five lanes were duplicates. The cheap discriminator was not
measurement — it was `git log <base>..main -- <touched files>` and a content
comparison, which settled `verify/guard-leaks` and `verify/rebase-probe` in
minutes without a build. **Diff the branch against `main` before rebasing it**;
`git rebase` telling you "patch contents already upstream" is the good case, and
a clean apply onto moved code is the dangerous one.

---

# Part 2: the remaining fifteen, 2026-09-13

Second pass over the same 22-worktree inventory
(`/home/free/tmp/wt-inventory-2026-09-13/dc3_unrec.tsv`), covering everything
Part 1 did not: the five above are excluded, and so are `dc3-sweep`
(`sweep-allunder100`, modifies `config/373307D9/symbols.txt` — the target-side
baseline, a separate and delicate task) and `dc3-ik-charlocal`
(`ik-test-charlocal`, marked DATA-TEST, do not merge). That leaves **fifteen
branch names**.

Baseline `main` = **`d84b37f83`** throughout. Ruler `functionRelocDiffs=name_check`,
`objdiff-cli 4.2.8 (032122696555, xxh3 14ac591a0814e6c9)`.

## Fifteen names, twelve branches

Deduplicate by commit sha before doing anything else — the brief's own warning,
and it holds:

| tip | branch names pointing at it |
|---|---|
| `a209f8c14` | `fix/native-stub-shadow`, `verify/kinect-base`, `verify/stub-shadow` |
| `842e51ce1` | `verify/kinect-pixel`, `verify/kinect-reloc` |

and `a209f8c14` ⊂ `842e51ce1` ⊂ `c505093f1` (`verify/kinect-camera`), so six of
the fifteen names are one lane's history seen from five points along it. Twelve
distinct tips remain; the whole Kinect/stub-shadow family adjudicates once.

## Verdicts

| branch | commits | verdict | evidence |
|---|---|---|---|
| `verify/scanner-truthfulness` | 15 | **ALREADY LANDED** | 15/15 commit subjects have exact twins on `main`; `main` then added 38 more commits to the same files |
| `verify/mcp-gaps` | 11 | **ALREADY LANDED** | 11/11 exact twins (landed via the `mcp-gaps` branch) |
| `verify/compose-both` | 14 | **ALREADY LANDED** | 11/14 twins; the 3 others are 2 merge commits + one **message-only** commit (zero file changes) |
| `verify/kinect-camera` | 13 | **ALREADY LANDED** (code) / **WOULD REVERT NEWER WORK** | 10/13 twins; `main` moved 22 commits ahead on the three `src/` files it carries |
| `verify/kinect-pixel` = `verify/kinect-reloc` | 10 | same, subset | strict prefix of `verify/kinect-camera` |
| `fix/native-stub-shadow` = `verify/kinect-base` = `verify/stub-shadow` | 5 | **ALREADY LANDED** | 5/5 twins; strict prefix of the above |
| `harvest-threadtask-replace` | 1 | **GENUINELY UNLANDED → LANDED TODAY** | `ThreadTask::Replace` 82.34375 → 100.0, 32/32 equal |
| `fix/worktree-relative-ninja` | 1 | **GENUINELY UNLANDED, claim STILL TRUE → detector landed today, unwired** | nothing on `main` refuses a foreign `build.ninja`; 0 of 47 trees affected now |
| `fix/well-b-icf-attrib` | 2 | **GENUINELY UNLANDED** (negative result, not landed) | 443-line script absent from `main`; the lead it refutes is unreachable from `main` |
| `ws3-dc3-relocname-measurement` | 1 | **SUPERSEDED BY MAIN** | 407-line doc (2026-08-06) vs `main`'s 828-line one (2026-08-19) covering the same class |
| `selfdistill-scoring` | 1 | **GENUINELY UNLANDED, but itself stale** | regenerates a witness JSON against `15a64d92f`, three weeks behind `main` |
| `v7eval-wt-20260805` | 1 | **WOULD REVERT NEWER WORK — do not merge** | rewrites `config/373307D9/symbols.txt`; 23 `main` commits have touched it since |

**Tally: 6 already landed, 1 superseded, 1 would-revert, 4 genuinely unlanded
— of which 2 were landed today and 2 deliberately were not.** Part 1's finding
holds and strengthens: *"committed but never merged" is not "unlanded work"*.

## Claims tested against `main`'s CURRENT code

The brief's cheapest check for an instrument branch is not to merge it but to
ask whether its defect is still there. Every claim below was read out of
`main`'s working tree today, not out of a commit message.

| claim (branch) | verdict on `main` today |
|---|---|
| `norm_sym` strips the class name, so wrong-callee bugs cancel out (`d8dcc18d2`) | **FIXED.** `scripts/analysis/audit_normalized_masking.py:96` carries the branch's own `BEGIN HEURISTIC CHANGE` comment and the `@unnamed@`-only regex |
| `batch_pattern_scan` examined 200 of 1,751, and the wrong 200 (`c54643c9a`) | **FIXED.** `--limit` defaults to `0`; the 200/1,751 truncation is spelled out in `--help`, and an explicit `--limit` is labelled `TRUNCATED` |
| 18,130 COMPLETE rows carry a false auto-AT_LIMIT justification (`e3bd62ce8`) | **FIXED**, and gone past: `main` has `00f316f99` (callee gate refuses a scan from another instrument) and `6a4a8188d` on top |
| four DTA scanners print "no issues found" having checked nothing (`a50b0cce6`) | **FIXED**, twin `e41f060bb` |
| nothing refuses a `build.ninja` generated for a different tree (`aa4b8f70d`) | **STILL TRUE.** `scripts/verify_ninja_root.py` absent; no equivalent anywhere in `scripts/`, `tools/`, `tests/`. See below |
| offset-mismatch enrichment ignores the base register (`821bdeeab`) | **FIXED**, twin on `main`, and `CLAUDE.md` documents it |

The one survivor was written fresh against `main` rather than merged; the rest
needed nothing.

## `verify/scanner-truthfulness` — the highest-value class, and it is done

Worth stating plainly because it was flagged as the prize: this is
instrument-truthfulness work, it *was* as valuable as advertised, and it
**already reached `main` in full**. All fifteen subjects have exact twins.
Nine of its 41 files are byte-identical to `main`; the other 32 are all
*larger* on `main` (`sync_objdiff.py` 1,138 → 1,462 lines; `home_store_census.py`
382 → 592; `test_honesty_dta.py` 579 → 895). `main` carries `d6805276f`
("Rebase onto main: merge three tools that both lanes rewrote"), which is the
rebase that landed it. Merging the branch now would remove ~4,000 lines.

## `verify/kinect-camera` — landed, and now a regression vector

Ten of its thirteen commits are on `main`. The three that are not add a single
482-line file, `docs/analysis/2026-08-19-verify-kinect-camera-path.md`, which
does not exist on `main`. That doc is the only genuinely unlanded artifact in
the whole Kinect family.

Merging the branch to get it would be actively harmful. `main` has moved
**22 commits** across the three `src/` files the branch also carries:

| file | branch vs `main` | what `main` gained |
|---|---|---|
| `src/system/dsp/EQEffect.cpp` | 195+/206− | 8 commits, incl. the 4×-wrong band-3 coefficients and the `_blkmov` copy loop |
| `src/system/os/File.cpp` | 22+/24− | `FileMakePathBuf` 98.2079 → 100 |
| `src/system/gesture/LiveCameraInput.cpp` | 58+/38− | `GetColorCameraProperty` 91.8 → 100.0; `GetTweakedAutoexposure` 69.88 → 94.33 |

`native/src/platform/NuiImageSurface_Native.{cpp,h}`, `native_link_glue.cpp`
and `test_camera_yuv_decode.cpp` are already byte-identical on `main`.

## `fix/well-b-icf-attrib` — a negative result worth keeping, in prose not code

Two commits, one 443-line script (`scripts/analysis/icf_nonsurvivor_rows.py`),
absent from `main`. It refutes the "dtk mis-attributes ICF folds" hypothesis and
then rejects all 15 of its own `symbols.txt` rename candidates. The reasoning is
the durable part, and it is recorded here so a third lane does not raise the
lead again:

> **`config/373307D9/splits.txt` — not `symbols.txt` — assigns address ranges to
> translation units.** `symbols.txt` only names symbols *within* whatever unit
> already owns the address, and objdiff pairs per unit. So a rename can never
> move code between objects, and is only useful when the address's owning unit
> is already the unit whose base object compiles the proposed spelling.

13 of the 15 sit in `xdk/` ranges dc3 does not decompile at all; the other 2
land in `system/synth_xbox/Synth.cpp` while the compiled `??_G` spelling lives
in `FxSendCompress.cpp` — a TU-organisation difference, fixable only by moving
code between `.cpp` files. 12 of the 15 are already declared in
`build/373307D9/icf_aliases.map`, which is dc3's correct mechanism for the class.

The script is not landed: `grep -rn 'rename_candidates' scripts/` on `main`
returns nothing, so the "15 free wins" reading it exists to refuse is not
reachable from `main` today, and `main` already carries five other `icf_*`
analysis scripts.

## `selfdistill-scoring` — unlanded, and merging it would not fix the artifact

`scripts/addr_identity_witness.json` on `main` is the **2026-08-09** generation
(`target_repo_rev 21f7f331…`, 53 pairings). The branch regenerates it
(2026-08-30, `target_repo_rev 15a64d92f`, 16 pairings — 10 BYTES, 6 SIZE-FORCED)
because, in its own words, *"the split config has since named 52 of those
addresses … the whole file refused, which is the designed degrade."*

So `main`'s copy is a self-refusing artifact — but the replacement is itself
three weeks behind `main`. The right action is to regenerate against today's
`main` with decomp-synth's `addr_identity_gen.py`, not to merge a 2026-08-24
snapshot. Left unlanded on that basis, not on "no value".

## `v7eval-wt-20260805` — the same class as `dc3-sweep`

One commit, and it rewrites `config/373307D9/symbols.txt` (23 insertions, 52
deletions) to "baseline the split symbols.txt this worktree's dtk actually
produces" — from 2026-08-05, using a `dtk` that is not the deployed one.
`symbols.txt` is a tracked ninja dependency; editing it re-triggers the split
and rewrites all 2,223 target objects, i.e. **the target side of every diff in
the project**. 23 `main` commits have touched the file since the branch's base.
Do not merge. Treat it exactly as `dc3-sweep` is being treated.

## Method notes, added to Part 1's

1. **Deduplicate by sha before counting.** Fifteen names were twelve branches,
   and six of the fifteen were one lane. An inventory that counts *names* will
   overstate the backlog by whatever fraction of it is checkpoints.
2. **Subject-line twin matching is the cheapest discriminator of all** — cheaper
   than the file diff, and it survives a rebase, which `git branch --merged`
   does not. `git log --format=%s <base>..<branch>` against the same over
   `main`, compared as sets, settled six branches in one command.
   ⚠ Do it in a language with real quoting. Doing it as
   `git log --grep="$s"` in a shell loop silently mis-reported 5 of 15
   scanner-truthfulness commits as unlanded, because backticks and double
   quotes inside the subjects were interpreted by the shell.
3. **A branch that is 10/13 landed is more dangerous than one that is 0/13
   landed**, because it looks live and merges cleanly onto code that moved
   underneath it. `verify/kinect-camera` is that shape.
4. **"Still true on `main`?" beats "mergeable?" for every instrument branch.**
   Six such claims were checked by reading `main`'s code; five were fixed and
   needed nothing at all. Only the sixth justified any work, and that work was
   written fresh.
