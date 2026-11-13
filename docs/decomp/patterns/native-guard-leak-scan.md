# A native fix written without a guard leaks into the Xbox build

The PPC build and the native port compile the same `src/` tree. Native-only behaviour therefore has to sit behind a guard macro that the PPC build never defines (`HX_NATIVE`, `HX_WEB`, `__EMSCRIPTEN__`, …). This page covers the opposite defect from an over-broad guard: a native crash fix written **without** a guard. It compiles into the Xbox build and silently changes the code the decomp is trying to match.

Last verified against the tree: 2026-11-13 (np-docs @ 2d678db7e).

## Why it is hard to see

The function's score moves by a few instructions, which looks like ordinary residual noise, and nothing in the source says "native". The leaked line is usually the defensive idiom that native bring-up reaches for: a null or empty check whose body is `return`, `continue` or `break`.

Two confirmed instances:

- **`HamIKSkeleton::SetBone`:** `if (!t2) return;` was added by `5d19777db` ("native: venue rendering…") and guarded by `866ba1082`. It now sits inside `#ifdef HX_NATIVE` at `src/system/hamobj/HamIKSkeleton.cpp`, because the target dereferences `t2` unconditionally.
- **`DelayEffect::Process`:** an `if (!mBuffer) return;` added by `f8a417405` ("native: v0xE mogg audio pipeline…"). `src/system/dsp/DelayEffect.cpp` no longer contains a guard.

## Detector

```bash
python3 scripts/analysis/native_guard_leak_scan.py --repo . --signal all
python3 scripts/analysis/native_guard_leak_scan.py --repo . --signal all --leading-stmts 2
python3 scripts/analysis/native_guard_leak_scan.py --self-test   # negative control
```

`--signal` takes one of `blame`, `content`, `shape`, `both` (the default) or `all`. Each signal is reported as its own tier:

| Tier | What it flags |
|---|---|
| blame | The line is still attributed (`git blame -w -M`) to a commit whose *subject* marks it as native-port work, and it is outside any guard conditional. |
| content | The line, or a comment just above it, names the native or web port while sitting outside a guard. |
| interpolated / guard-shape | Refinements of blame. Either the commit owns only a small part of a body somebody else decompiled, or the line has the defensive-guard shape. |
| shape | Every unguarded defensive guard in `src/`, whoever wrote it. This is an upper bound on the population, not a bug list. |

Other flags:

- `--leading-stmts N` (default 2; 0 disables it) restricts the sub-100% list to guards among the first N statements of their function. Both confirmed leaks had that shape.
- `--json OUT` writes the results as machine-readable JSON.
- `--self-test` re-injects the `SetBone` leak on a scratch commit. It asserts the scanner reports the leak and stays silent on the guarded site. It refuses to run on a dirty tree, because its rollback is a hard reset.

Run `ninja` first. The scanner filters on `build/373307D9/report.json`, and in a fresh worktree that file describes main, not your branch; see [BUILD_SYSTEM.md](../../tools/BUILD_SYSTEM.md#a-fresh-worktrees-reportjson-describes-main-not-your-branch). Every run prints a provenance banner naming the commit it scanned and the report it filtered with. Quote counts together with that banner; a count without a tree is not comparable to anything.

## Adjudicating a hit

**A hit is not a verdict.** The Xbox build is full of genuine null checks, empty checks and early returns, so the false-positive rate is high by nature.

- **Do not use objdiff's `insert` count as the discriminator.** Most target-faithful guards carry inserts, from address recomputation that has nothing to do with a guard. Deleting null checks because a function "has inserts" removes code the Xbox build genuinely has: a correctness regression that looks like a match improvement.
- **A useful screen** is an *our-side-only compare or branch* in the prologue region: an `insert` whose SRC is a `cmp*`/`b*`, or a `replace` whose SRC is one and whose TGT is not. That is the shape a leaked early-out takes.
- **The screen does not settle it.** Read the function's prologue in the target disassembly (`build/373307D9/asm/`) and check whether the target performs the test.

The 2026-08-19 measurements behind these rules (tier counts at two named commits, and how often each screen fires on adjudicated target-faithful sites) are recorded in the archived audit linked below. Re-run the scanner rather than quoting them.

## Related

- The rule this enforces, from the native guard policy: a guard is for a real platform divergence, never for crash suppression. See [docs/native/reference/PORTING.md](../../native/reference/PORTING.md#guard-policy).
- [native-shadow-bodies-are-unmeasured.md](native-shadow-bodies-are-unmeasured.md): the opposite case, a guarded native body that objdiff never measures.
- [rounded-100-hides-real-bugs.md](rounded-100-hides-real-bugs.md): why the cost of a leak has to be counted in instructions, not read off a rounded percentage.
- [SCANNER_TRUTHFULNESS.md](../../tools/SCANNER_TRUTHFULNESS.md) §5.2: where this scanner sits relative to the scanner-honesty contract.

## Source

Lifted from the "inverse audit" section of [archive: native/HACK_AUDIT.md](../../archive/2026-11-13-native-doc-audit/native/HACK_AUDIT.md#the-inverse-audit-native-fixes-that-were-never-guarded-2026-08-19).
