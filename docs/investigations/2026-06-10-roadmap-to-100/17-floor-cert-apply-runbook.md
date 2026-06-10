# 17 — Floor-Certificate Apply Runbook (Lane B, Wave 2)

**Date:** 2026-06-10. **Lane:** wave2/b-floor-certs. **Owner script:** `scripts/certify_floor.py`
(new), `scripts/reconcile_db.py` (extended with check (e)). Source: doc 08, roadmap 3.1.

Makes "done — only cosmetic/floor mismatches remain" *queryable* instead of vibes.
A **floor certificate** records that a function is legitimately below 100% because the
residual diff is a known cosmetic artifact (register/FPR swap, reloc/offset shift,
commutative operand order, ICF fold, …) and NOT an un-fixed behavioral bug.

All Lane-B work was developed and validated against a **COPY** of the live
`decomp.db` inside the worktree. **The live `/home/free/code/milohax/dc3-decomp/decomp.db`
was never written** (verified: it still has 0 `floor_*` columns). These steps are the
single-writer apply for the orchestrator on `main`.

## What lands

1. **Schema migration (idempotent)** — five additive columns on `functions`:
   - `floor_certificate TEXT` — NULL | `equivalent` | `artifact:<class>` |
     `permuter_exhausted` | `icf_merged` | `pgo_block_sink` (last is manual-only,
     never auto-fired).
   - `floor_cert_pct REAL` — `match_percent_normalized` captured at cert time. Any
     later normalized change invalidates the cert (reconcile check (e)).
   - `floor_cert_build TEXT` — git short rev of the source tree at cert time (provenance).
   - `floor_cert_at TEXT` — ISO timestamp.
   - `floor_cert_evidence TEXT` — JSON evidence, **including unicorn staleness**
     (`unicorn_tested_at`, `unicorn_age_days`, `unicorn_stale`) so a cert built on
     stale unicorn data (doc 04 F6) can be invalidated / re-tested.
2. **`authorable_done` SQL view** — authorable (non-SDK per `scripts/authorable.py`,
   not `merged_/lbl_/fn_/??_` artifacts, not `excluded`) with a `done_state`
   (`matched`/`stub`/`certified`/`open`) and `is_done` column.
3. **reconcile_db.py check (e)** — stale-floor-cert detector; `--fix` clears certs
   whose normalized percent moved (so certify re-evaluates them on the next run).

## Evidence model (certifiable iff normalized < 100 AND one holds)

Precedence (strongest first): `equivalent` > `artifact:<class>` > `icf_merged` >
`permuter_exhausted`.

| Cert | Evidence | Source |
|---|---|---|
| `equivalent` | unicorn_verdict='EQUIVALENT' (behaviorally identical under emulation) | unicorn |
| `artifact:<class>` | DIVERGENT but unicorn_class ∈ {build_env, regalloc, stack_layout, merged_call, merged_arg, fpr_precision, **orig_error**} | unicorn |
| `icf_merged` | merged_symbol_count > 0 (Identical COMDAT Folding) | DB flag |
| `permuter_exhausted` | ≥1 attempt ended at_limit/stuck AND no attempt ever beat current normalized | attempts table |

**Deliberately NOT auto-certified** (doc 08 F6 routable / real-bug residue):
`call_count` (needs per-function adjudication), `error`, `call_arg`, `object_memory`,
`return_value`, `cap_exhausted`. These stay `open`. `primary_pattern` is NEVER used as
evidence (doc 08 F8: it is stale/noisy — shows on 100% functions).

## Headline measurements (Lane B item 4) — measured on the live-DB copy at build f8256e0a

Authorable partial frontier (0 < normalized < 100): **1,314 functions**.

| Number | Value |
|---|---|
| **Certifiable TODAY from existing evidence** | **970** |
| — on FRESH evidence (no stale-unicorn dependency) | **127** |
| — blocked on STALE unicorn (>60d old, re-test before trusting) | **843** |
| **No evidence at all (un-certifiable today)** | **344** |

Per-class certifiable: equivalent **600**, artifact:* **246**, permuter_exhausted **108**,
icf_merged **16**.

> Caveat: 843/970 certs rest on unicorn data tested Feb–Mar 2026 (98 days old at apply
> time). Their cert *stores* that staleness; re-running the unicorn oracle and
> `certify_floor.py --apply` would refresh them. Only **127** certs (ICF + permuter +
> 3 fresh-unicorn) are independent of stale unicorn.

### Canonical done view (`authorable_done`, 20,836 authorable fns / 4,917,888 bytes)

| State | Fns | Bytes |
|---|---|---|
| matched (norm==100) | 18,920 | 3,846,396 |
| stub (is_stub=1) | 388 | 117,696 |
| certified (floor cert) | 970 | 665,888 |
| **open (no cert, <100)** | **558** | **287,908** |

- **DONE without certs:** 19,308/20,836 fns (**92.67%**) / 3,964,092 bytes (**80.61%**)
- **DONE with certs:** 20,278/20,836 fns (**97.32%**) / 4,629,980 bytes (**94.15%**)

So floor certification reframes the project: **558 authorable functions / ~288 KB are
the genuine remaining-work residue** (everything else is matched, a stub, or certified
cosmetic floor). That 558 is the true Lane-B "done definition" gap.

## Apply steps (orchestrator, single writer on main, run from repo root)

Run **after** Lane A's sync runbook (so `match_percent_normalized` is current — certs
gate off it). `certify_floor.py` reads `match_percent_normalized`; if that column is
stale the certs will be too.

```bash
# 0. Ensure normalized percents are current (Lane A's sync owns this; cert gates off it).
#    python3 scripts/sync_match_percent.py --build --promote --demote   # (Lane A step)

# 1. DRY-RUN first — review the census; writes NOTHING.
python3 scripts/certify_floor.py

# 2. Migrate schema (5 columns + authorable_done view) AND write certs. Idempotent.
python3 scripts/certify_floor.py --migrate --apply

# 3. Confirm: reconcile check (e) reports 0 stale certs right after apply.
python3 scripts/reconcile_db.py            # (e) stale floor certificates: 0

# 4. Canonical done-view headline (read-only).
python3 scripts/certify_floor.py --summary
```

### Nightly / post-sync guard (wire, do not crontab)

`scripts/reconcile_db.py` check (e) now invalidates stale certs. After any future
`sync_match_percent.py` re-sync that moves a function's normalized percent, re-run:

```bash
python3 scripts/reconcile_db.py --fix     # clears certs whose percent moved (e)
python3 scripts/certify_floor.py --apply  # re-certifies from fresh evidence
```

## Risks / notes

- **Stale-unicorn dependency (the dominant caveat):** 843/970 certs depend on unicorn
  data ~3 months old (doc 04 F6). They are flagged `unicorn_stale=true` in evidence and
  remain trustworthy as floor *signals* (a function that was EQUIVALENT and hasn't been
  edited is still EQUIVALENT), but a unicorn re-run is the proper refresh. Only 127 certs
  are stale-unicorn-independent.
- **`pgo_block_sink` not auto-fired:** the 361-fn PGO block-sink floor
  (`at-limit-systemic.md` §7) is not a queryable DB flag, so it is never auto-certified.
  The enum value exists for manual/orchestrator use.
- **reconcile_db.py edit is additive** (a single new check (e) + cert columns in the
  SELECT, gated on `has_cert`). It is backward-compatible with a DB lacking the columns
  (verified: the wave-1 `test_measurement_sync.py` suite still passes). Lane D does not
  touch reconcile_db.py — no conflict.
- **`icf_merged` is small (16):** most ICF folds are already filtered out of the frontier
  as `merged_` artifact symbols; only 16 frontier functions carry `merged_symbol_count>0`
  without stronger unicorn evidence.
- Certs are recomputed wholesale on every `--apply` (idempotent, ~1s) — there is no
  partial/incremental write, so a re-apply after a sync naturally drops certs that no
  longer qualify and adds newly-qualifying ones.
