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
