# Documentation Archive

Superseded documents live here. They are kept because they record how the
project reasoned at a point in time — including the negative results and the
strategies that were tried and rejected — which is the part a summary always
loses.

## Rules

1. **Archived files are byte-for-byte as they were written.** Nothing in this
   tree is edited after it lands. If a document says something now known to be
   wrong, the correction goes in that archive's `MANIFEST.md`, never into the
   document. A "corrected" archive is no longer evidence of what was believed.
2. **Every archive directory has a `MANIFEST.md`** with one row per file: what
   it was, when it was last touched, the specific claim that went stale, and
   what supersedes it. Read the manifest before reading the file.
3. **Nothing here is a source of current numbers.** Every headline in this tree
   is a snapshot from its own date, on whatever ruler was in use then — and the
   ruler itself changed in 2026-08 (see the manifest). Current numbers live in
   [`../STATE_OF_THE_DECOMP.md`](../STATE_OF_THE_DECOMP.md) and
   [`../PROGRESS_METRICS.md`](../PROGRESS_METRICS.md).
4. **Internal links inside archived files are frozen too.** Where a document
   links to a sibling that landed in a different subdirectory, the link is dead.
   The manifest records which ones.

## Archives

| Archive | Date | What it holds |
|---|---|---|
| [`2026-08-17-doc-audit/`](2026-08-17-doc-audit/MANIFEST.md) | 2026-08-17 | 42 files: five 2026-02 status snapshots, nine decomp planning/burndown worklists, and the 2026-02/03 tooling experiments (codex coordination, context-enrichment A/B, meta-strategy scoring, unicorn Phase-1 design, native-test audits). |

## Related history that is *not* archived

- [`../sessions/`](../sessions/) — dated work-session logs. History by
  construction; they are not maintained and are not expected to be true now.
- [`../investigations/`](../investigations/) — dated investigation lanes, each
  self-contained with its own findings.
- [`../analysis/`](../analysis/) — machine-generated lane artifacts (JSON/JSONL)
  from specific investigations, e.g. the 2026-08-12 `name_check` residency
  split.
