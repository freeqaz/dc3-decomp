#!/usr/bin/env python3
"""The auto-AT_LIMIT callee gate: a provenance guard plus an adjudicator.

WHAT THIS REPLACES
==================
``sync_objdiff.py``'s auto-AT_LIMIT rule issues certificates that say "this
function is unfixable".  Before 2026-08-22 it protected itself from the
wrong-callee class with one query::

    SELECT symbol FROM v_function_patterns
     WHERE ruler = 'name_check'
       AND pattern IN ('WRONG_CALLEE','TEMPLATE_INSTANTIATION_MISMATCH')

and refused to certify anything in the result.  That query has two structural
problems, both measured on 2026-08-21 (task #134) and both previously recorded
only as comments at the call site.

PROBLEM 1 -- NOTHING REFRESHED THE SCAN IT READS
------------------------------------------------
``v_latest_pattern_scan`` is written *only* by a hand-run
``scripts/analysis/pattern_census.py --apply``.  No ninja edge, no wrapper.  So
the gate aged against whatever ``bin/objdiff-cli`` happened to be installed the
last time somebody ran the census -- and an aged scan does not fail, it returns
a *narrower set*, which reads exactly like "no wrong callees here".

Measured: the recorded scan was objdiff **4.2.6** while the installed binary was
**4.2.7**, and the 4.2.6 set did not contain
``?Copy@FxSend@@UAAXPBVObject@Hmx@@W4CopyType@23@@Z`` -- the single highest-value
row in the population, byte-identical but for one relocation naming a different
function, 172 B, sitting at ``COMPLETE`` with reason "auto: all mismatches
unfixable".  The gate had a live hole on precisely the row it most needed to
cover, and would have re-certified it on the next sync.

THE FIX, AND WHY IT IS A READER GUARD AND NOT A NINJA EDGE
----------------------------------------------------------
``ensure_current_scan()`` compares the scan's recorded ``tool_version`` against
``bin/objdiff-cli --version`` and **raises** ``StalePatternScanError`` rather
than certifying from a scan taken under a different instrument.  Absence of a
scan raises too: absence of a measurement must never become evidence of absence
of the bug.

A ninja edge was considered and rejected, on two grounds:

1.  *It would not cover the failure that actually happened.*  The staleness axis
    was the **tool binary**, and ``bin/objdiff-cli`` has no ninja edge at all --
    the ``cargo`` rule is deliberately depfile-less and this repo defaults to a
    prebuilt binary, so nothing rebuilds or even stats it (CLAUDE.md, "No cargo
    depfile" / "Nothing rebuilds dtk/objdiff-cli for you").  An edge keyed on
    build inputs would have re-run the census many times on 2026-08-21 and still
    handed the gate a 4.2.6 scan.  Making ``bin/objdiff-cli`` an implicit input
    would re-fire a whole-binary sweep in three repos at once, because that path
    is a symlink shared with ``../rb3`` and ``../rb3-xenon``.
2.  *Cost.*  A whole-binary census is ~30 s per ruler.  This repo was bitten on
    2026-08-21 by an ``always`` edge that re-ran a 14 s report on every
    steady-state build (fixed in ``tools/project.py`` with write-if-changed +
    ``restat``); adding a 30 s one is the same mistake with a bigger constant.

The reader guard cannot silently degrade because **the certificate-issuing path
must consult the scan, and the consult is the assertion**.  There is no code
path that issues a certificate without first proving the scan was taken by the
installed binary.  A stale scan is an exception with both version strings in it;
it is not a smaller set.

``scripts/verify_pattern_scan_current.py --check`` is the same assertion as a
~0.1 s standalone command, for anything measuring outside ``sync_objdiff``.

PROBLEM 2 -- IT REFUSED RATHER THAN JUDGED
------------------------------------------
Blocking on the raw pattern also withheld a certificate from every row whose
finding is real but provably not actionable.  Of the 138 rows carrying a callee
pattern under 4.2.7, **71 (51%)** are artifacts.  This module judges three
classes and keeps refusing on the rest.

JUDGED -- does NOT block a certificate
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
``unverifiable_pairing``
    Every callee row on the function carries objdiff 4.2.7's own
    ``Fixability::Unverifiable``: the finding is inside a symbol pair objdiff
    **guessed by byte signature** (an MSVC EH funclet whose counterpart
    routinely belongs to a different parent function), so the differing ``bl``
    is not evidence about our source at all.  objdiff co-reports these as
    ``UNVERIFIABLE_PAIRING``.  This is objdiff's declaration, not our inference.

``icf_fold``
    Every divergent callee pair resolves to ONE address in
    ``orig/373307D9/ham_xbox_r.map``, the shipped MSVC linker map for this exact
    build.  ``/OPT:ICF`` folded the two names; the call is the same bytes to the
    same code, so this relocation *cannot* be why the row is short of 100%.  The
    gap is ``scripts/symbol_aliases.json``, not the source.

``merged_stub``
    Our side calls a synthesised ``merged_*`` placeholder -- dtk had no name for
    a fold survivor.  This is #112's documented refusal class
    (``docs/analysis/icf-survivor-names-20260819.md``); it is config work in
    ``config/373307D9/symbols.txt``, never source work, so no source edit can
    move the row.  ``?DoVelocity@NgPostProc@@IAAXXZ`` is the canonical member.

STILL REFUSING -- blocks a certificate
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
``real_other_address``
    The two names sit at DIFFERENT addresses, or our name is absent from the
    map.  We call other code.  Open, fixable source work.

``unresolved``
    The map cannot adjudicate the pair -- the target name is absent (anonymous
    namespace, a name mangled per-TU), or one of the names is listed at MORE
    THAN ONE address so "same address" has no single answer.  An adjudication
    that cannot be made is not an adjudication in our favour.

``no_evidence``
    A ``likely_fixable`` callee row whose ``details`` payload is missing or
    unparseable.  Same rule: no evidence, no certificate.

WHY THE ICF SUB-CASE IS SAFE, AND THE ONE THAT WOULD NOT HAVE BEEN
------------------------------------------------------------------
There is a real trap here and it is worth stating exactly, because getting it
wrong in an automated gate is worse than the conservatism being removed.  A fold
group proves the *printed name* is unreliable; it does **not** prove *our* call
is right.  On 2026-08-21 the ``fix/callee-rest`` lane (``5aba5e30e``) fixed
twelve rows precisely by refusing to stop at the printed name: it took the
target's callee ADDRESS, expanded the whole fold group there, and found exactly
one member the enclosing function could plausibly call.  Nine of the twelve
printed a callee from a completely unrelated subsystem
(``CFEWaveStreamDecoder::GetMaxProcess``, ``NetStream::Fail``,
``NgDOFProc::FocalPlane``, ...).

That trap does **not** reach the ``icf_fold`` class as tested here, and the
reason is structural rather than statistical: the test requires *both* names to
resolve to the same address, so our emitted call and the target's go to the same
code and the bytes are identical.  The lane's twelve rows are the other shape --
the target's name is in a fold group but OUR callee is at a different address --
which this module classifies ``real_other_address`` and blocks.

The record confirms it.  All twelve rows the lane fixed were bucketed
``FIXABLE_wrong_callee`` (10), ``FIXABLE_call_order`` (1) or ``PRIZE_crosses_row``
(1) by ``readjudicate_callee_verdicts.py``.  **Zero** came from ``RIGHT_icf_fold``
or ``RIGHT_pairing_artifact``.  Five of them still print a charge *after* being
fixed and only then resolve to one address -- i.e. rows enter the fold class as
the fix lands, not before it.

The residual, and it is deliberately out of scope: certifying AT_LIMIT on a fold
says "no source edit reaches 100%", which is what AT_LIMIT means.  It does not
say the call is *semantically* right -- two functions can fold because both are
empty while naming different subsystems.  That is a native-port concern, not a
matchability one, and the evidence sentence written into ``verdict_reason``
names the address so the class stays greppable.

Ambiguity is refused rather than resolved: if either name is listed at more than
one address the pair is ``unresolved``.  CLAUDE.md records three loud findings in
this family that turned out to be config defects rather than source bugs, so the
adjudicator is required to be able to name ONE address per side or say nothing.
"""

from __future__ import annotations

import json
import re
import sqlite3
import subprocess
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]

MAP_REL = "orig/373307D9/ham_xbox_r.map"
MAP_LINE = re.compile(r"^\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{8})\s")

#: the two objdiff classes that mean "you are calling a different function".
CALLEE_PATTERNS = ("WRONG_CALLEE", "TEMPLATE_INSTANTIATION_MISMATCH")

#: objdiff's own fixability label for a finding inside a byte-signature-guessed
#: symbol pair.  Anything else on a callee row is treated as actionable.
UNVERIFIABLE = "unverifiable"

#: dtk's synthesised name for a fold survivor it could not name (#112).
MERGED_STUB_RE = re.compile(r"(?:^|[?@])_?merged_")

#: the ruler.  `none` is structurally blind to this entire class; `all` also
#: charges addends and is ~99.8% noise.
DEFAULT_RULER = "name_check"


class StalePatternScanError(RuntimeError):
    """The recorded pattern scan was not taken by the installed objdiff-cli.

    Raised rather than silently narrowing the gate's input set.  See this
    module's docstring, PROBLEM 1.
    """


class LinkerMapError(RuntimeError):
    """The shipped linker map could not be read or parsed."""


# --------------------------------------------------------------------------
# provenance
# --------------------------------------------------------------------------

def installed_objdiff_version(repo_root: Path | str = REPO_ROOT) -> str:
    """``bin/objdiff-cli --version`` verbatim -- the string pattern_scans stores.

    The version string carries the git hash, which is a far better identity than
    an mtime: three objdiff releases landed in the two days before this guard was
    written, and two of them changed the callee vocabulary.
    """
    cli = Path(repo_root) / "bin" / "objdiff-cli"
    if not cli.exists():                      # a worktree symlinks bin/
        cli = REPO_ROOT / "bin" / "objdiff-cli"
    if not cli.exists():
        raise StalePatternScanError(
            f"bin/objdiff-cli not found (looked in {repo_root} and {REPO_ROOT}); "
            f"cannot establish which instrument the pattern scan should match")
    out = subprocess.run([str(cli), "--version"], capture_output=True, text=True,
                         check=True)
    return out.stdout.strip()


def latest_scan(db: sqlite3.Connection, ruler: str = DEFAULT_RULER) -> dict | None:
    """The newest recorded scan for `ruler`, or None."""
    db.row_factory = sqlite3.Row
    try:
        row = db.execute(
            "SELECT * FROM v_latest_pattern_scan WHERE ruler = ?", (ruler,)
        ).fetchone()
    except sqlite3.Error:                     # pre-v17 schema
        return None
    return dict(row) if row else None


def ensure_current_scan(db: sqlite3.Connection, *, ruler: str = DEFAULT_RULER,
                        repo_root: Path | str = REPO_ROOT) -> dict:
    """Return the latest `ruler` scan, or raise `StalePatternScanError`.

    Three refusals, each on a column ``pattern_scans`` already records:

    * no scan at all for this ruler -- there is nothing to certify from;
    * ``tool_version`` != the installed ``objdiff-cli --version``;
    * ``tree_verified = 0`` -- the census did not assert a post-compile fixed
      point, so its pattern set was measured on an unsettled tree.
    """
    installed = installed_objdiff_version(repo_root)
    scan = latest_scan(db, ruler)
    if scan is None:
        raise StalePatternScanError(
            f"no pattern scan recorded for ruler={ruler!r}.\n"
            f"  installed objdiff-cli: {installed}\n"
            f"The auto-AT_LIMIT gate reads this scan to find LikelyFixable callee\n"
            f"divergences.  With no scan, an empty result would read as 'no wrong\n"
            f"callees' and a fixable function would be certified unfixable.\n"
            f"Re-derive it:\n"
            f"  python3 scripts/analysis/pattern_census.py --ruler {ruler} --apply")
    if scan["tool_version"] != installed:
        raise StalePatternScanError(
            f"pattern scan id={scan['id']} (ruler={ruler}) was taken by a DIFFERENT "
            f"objdiff-cli.\n"
            f"  scan      : {scan['tool_version']}\n"
            f"  installed : {installed}\n"
            f"  scanned   : {scan['project_dir']} @ {scan['build_rev']} "
            f"finished {scan['finished_at']}\n"
            f"Nothing refreshes this scan automatically -- see "
            f"scripts/orchestrator/callee_gate.py, PROBLEM 1.  Measured 2026-08-21: "
            f"a 4.2.6 scan read by a 4.2.7 binary did NOT contain ?Copy@FxSend@@, a "
            f"real wrong callee worth 172 B, so the gate would have re-certified it "
            f"as unfixable.\n"
            f"Re-derive it:\n"
            f"  python3 scripts/analysis/pattern_census.py --ruler {ruler} --apply")
    if "xxh3 unavailable" in scan["tool_version"] or "xxh3 " not in scan["tool_version"]:
        # The two strings agree, but on an identity that is only the git commit.
        # objdiff's own `build_id.rs` is explicit about which half is which: the
        # `xxh3` field is the xxHash3-64 of the running executable and is
        # AUTHORITATIVE, while the commit stamp is ADVISORY because cargo
        # re-runs build.rs only on declared inputs and "a tracked file was
        # edited but not staged" is not one.  Two binaries with the same commit
        # and different bytes are a real state, and it is the state that burned
        # a lane on 2026-08-12.  Without the hash, string equality proves
        # nothing worth a certificate.
        #
        # (A `-dirty` suffix on an otherwise-matching string is deliberately NOT
        # refused: the xxh3 already distinguishes those bytes from any other
        # build, so a dirty binary is still a fully identified instrument.  It
        # is also the normal state of `bin/objdiff-cli` here -- that path is a
        # symlink shared with ../rb3 and ../rb3-xenon and it went from
        # `4.2.7 (76c8da87e040)` to `4.2.7 (0a9716466e95-dirty)` mid-session on
        # 2026-08-22 because another lane rebuilt it.  Refusing dirtiness would
        # be a guard people turn off.)
        raise StalePatternScanError(
            f"pattern scan id={scan['id']} (ruler={ruler}) records a tool_version "
            f"with no binary hash: {scan['tool_version']!r}\n"
            f"Only the git commit is advisory (objdiff build_id.rs); without the "
            f"xxh3 of the executable, matching version strings do not prove the "
            f"same instrument.  Re-derive the scan with a binary that can read "
            f"its own executable:\n"
            f"  python3 scripts/analysis/pattern_census.py --ruler {ruler} --apply")
    if not scan["tree_verified"]:
        raise StalePatternScanError(
            f"pattern scan id={scan['id']} (ruler={ruler}) records tree_verified=0: "
            f"the census did not assert a post-compile fixed point, so its pattern "
            f"set was measured on an unsettled tree.  Re-derive it:\n"
            f"  python3 scripts/analysis/pattern_census.py --ruler {ruler} --apply")
    return scan


# --------------------------------------------------------------------------
# the adjudicator: the linker map that made the image
# --------------------------------------------------------------------------

@dataclass
class LinkerMap:
    """``orig/373307D9/ham_xbox_r.map`` -- name <-> address, both directions."""
    addr: dict[str, str]
    by_addr: dict[str, list[str]]
    ambiguous: set[str]
    path: Path

    def address(self, name: str) -> str | None:
        """The one address for `name`, or None if absent OR listed more than once."""
        if name in self.ambiguous:
            return None
        return self.addr.get(name)

    def group(self, address: str) -> list[str]:
        """Every name the linker parked at `address` -- the /OPT:ICF fold group."""
        return self.by_addr.get(address, [])


def load_linker_map(project_dir: Path | str = REPO_ROOT) -> LinkerMap:
    path = Path(project_dir) / MAP_REL
    if not path.exists():                     # worktrees symlink orig/; be explicit
        path = REPO_ROOT / MAP_REL
    if not path.exists():
        raise LinkerMapError(f"{MAP_REL} not found under {project_dir} or {REPO_ROOT}")
    addr: dict[str, str] = {}
    by_addr: defaultdict[str, list[str]] = defaultdict(list)
    ambiguous: set[str] = set()
    for line in path.open(errors="replace"):
        m = MAP_LINE.match(line)
        if not m:
            continue
        name, a = m.group(1), m.group(2)
        if name in addr:
            if addr[name] != a:
                ambiguous.add(name)
        else:
            addr[name] = a
        if name not in by_addr[a]:
            by_addr[a].append(name)
    if not addr:
        raise LinkerMapError(
            f"parsed 0 symbols from {path} -- refusing to adjudicate blind")
    return LinkerMap(addr, dict(by_addr), ambiguous, path)


def classify_pair(lmap: LinkerMap, target: str, base: str) -> str:
    """One divergent (target_symbol, base_symbol) callee pair -> a verdict.

    ``MERGED_STUB`` / ``ICF_FOLD`` are non-actionable; the rest block.
    """
    if MERGED_STUB_RE.search(base) or MERGED_STUB_RE.search(target):
        return "MERGED_STUB"
    if target in lmap.ambiguous or base in lmap.ambiguous:
        return "AMBIGUOUS"
    at, ab = lmap.address(target), lmap.address(base)
    if at and ab:
        return "ICF_FOLD" if at == ab else "REAL_OTHER_ADDR"
    if at and not ab:
        return "BASE_NOT_IN_MAP"
    return "TARGET_NOT_IN_MAP"


#: pair verdict -> (blocks?, gate reason).  Explicit and total: an unknown
#: verdict is a programming error, not a silent pass.
_PAIR_DISPOSITION = {
    "MERGED_STUB":       (False, "merged_stub"),
    "ICF_FOLD":          (False, "icf_fold"),
    "REAL_OTHER_ADDR":   (True,  "real_other_address"),
    "BASE_NOT_IN_MAP":   (True,  "real_other_address"),
    "TARGET_NOT_IN_MAP": (True,  "unresolved"),
    "AMBIGUOUS":         (True,  "unresolved"),
}


# --------------------------------------------------------------------------
# the gate
# --------------------------------------------------------------------------

@dataclass
class CalleeGate:
    """Per-symbol adjudication of the callee class, for auto-AT_LIMIT.

    ``blocked`` maps symbol -> reason for the rows that must NOT be certified;
    ``cleared`` maps symbol -> reason for the rows judged non-actionable.
    Together they are the whole callee population of the scan.
    """
    scan: dict
    blocked: dict[str, str] = field(default_factory=dict)
    cleared: dict[str, str] = field(default_factory=dict)
    evidence: dict[str, list[str]] = field(default_factory=dict)
    linker_map_path: str = ""

    def blocks(self, symbol: str) -> bool:
        return symbol in self.blocked

    def reason(self, symbol: str) -> str | None:
        return self.blocked.get(symbol) or self.cleared.get(symbol)

    def counts(self) -> dict[str, int]:
        out: dict[str, int] = {}
        for reason in self.blocked.values():
            out[f"block:{reason}"] = out.get(f"block:{reason}", 0) + 1
        for reason in self.cleared.values():
            out[f"clear:{reason}"] = out.get(f"clear:{reason}", 0) + 1
        return out

    def summary(self) -> str:
        c = self.counts()
        head = (f"callee gate: scan id={self.scan['id']} ruler={self.scan['ruler']} "
                f"{self.scan['tool_version']}\n"
                f"  population {len(self.blocked) + len(self.cleared)}  "
                f"blocked {len(self.blocked)}  judged non-actionable {len(self.cleared)}")
        body = "".join(f"\n    {k:32} {v:4}" for k, v in sorted(c.items()))
        return head + body


def _callee_rows(db: sqlite3.Connection, scan_id: int) -> dict[str, list[sqlite3.Row]]:
    db.row_factory = sqlite3.Row
    rows = db.execute(
        "SELECT f.symbol AS symbol, p.pattern, p.fixability, p.details "
        "  FROM function_patterns p JOIN functions f ON f.id = p.function_id "
        " WHERE p.scan_id = ? AND p.pattern IN (%s)"
        % ",".join("?" * len(CALLEE_PATTERNS)),
        (scan_id, *CALLEE_PATTERNS)).fetchall()
    out: defaultdict[str, list[sqlite3.Row]] = defaultdict(list)
    for r in rows:
        out[r["symbol"]].append(r)
    return dict(out)


def _pairs(details: str | None) -> list[tuple[str, str]] | None:
    """(target, base) callee pairs from a detector payload, or None if unreadable."""
    if not details:
        return None
    try:
        payload = json.loads(details)
    except (ValueError, TypeError):
        return None
    dc = payload.get("divergent_callees")
    if not isinstance(dc, list) or not dc:
        return None
    pairs = []
    for c in dc:
        t, b = c.get("target_symbol"), c.get("base_symbol")
        if not t or not b:
            return None
        pairs.append((t, b))
    return pairs


def build_callee_gate(db: sqlite3.Connection, *, ruler: str = DEFAULT_RULER,
                      repo_root: Path | str = REPO_ROOT,
                      project_dir: Path | str | None = None) -> CalleeGate:
    """Adjudicate every callee finding in the latest `ruler` scan.

    Raises `StalePatternScanError` if that scan was not taken by the installed
    objdiff-cli, and `LinkerMapError` if the adjudicator cannot be loaded.  Both
    are refusals: the caller must surface them, never fall back to certifying.
    """
    scan = ensure_current_scan(db, ruler=ruler, repo_root=repo_root)
    lmap = load_linker_map(project_dir or repo_root)
    gate = CalleeGate(scan=scan, linker_map_path=str(lmap.path))

    for symbol, rows in _callee_rows(db, scan["id"]).items():
        actionable = [r for r in rows if (r["fixability"] or "") != UNVERIFIABLE]
        if not actionable:
            # objdiff 4.2.7 itself marked every finding on this function
            # Fixability::Unverifiable: the enclosing pair was guessed by byte
            # signature, so the differing `bl` says nothing about our source.
            gate.cleared[symbol] = "unverifiable_pairing"
            gate.evidence[symbol] = [f"{r['pattern']}: objdiff fixability="
                                     f"{r['fixability']}" for r in rows]
            continue

        verdicts: list[tuple[str, str, str]] = []   # (verdict, target, base)
        unreadable = False
        for r in actionable:
            pairs = _pairs(r["details"])
            if pairs is None:
                unreadable = True
                break
            for t, b in pairs:
                verdicts.append((classify_pair(lmap, t, b), t, b))
        if unreadable or not verdicts:
            gate.blocked[symbol] = "no_evidence"
            gate.evidence[symbol] = ["callee finding carries no readable "
                                     "divergent_callees payload"]
            continue

        # A function is cleared only when EVERY pair is non-actionable.  One
        # unadjudicable pair is enough to withhold the certificate.
        blocking = [v for v in verdicts if _PAIR_DISPOSITION[v[0]][0]]
        if blocking:
            # report the strongest (most specific) blocking reason
            order = ["real_other_address", "unresolved"]
            reasons = {_PAIR_DISPOSITION[v[0]][1] for v in blocking}
            gate.blocked[symbol] = next(r for r in order if r in reasons)
            gate.evidence[symbol] = [
                f"{v[0]}: target {v[1]} @ {lmap.address(v[1])} vs base {v[2]} "
                f"@ {lmap.address(v[2])}" for v in blocking]
        else:
            reasons = {_PAIR_DISPOSITION[v[0]][1] for v in verdicts}
            # merged_stub is the more specific statement when both appear
            gate.cleared[symbol] = ("merged_stub" if "merged_stub" in reasons
                                    else "icf_fold")
            gate.evidence[symbol] = [
                f"{v[0]}: target {v[1]} @ {lmap.address(v[1])} vs base {v[2]} "
                f"@ {lmap.address(v[2])}" for v in verdicts]
    return gate


if __name__ == "__main__":                    # a 1-second census of the gate
    import argparse
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--db", default=str(REPO_ROOT / "decomp.db"))
    ap.add_argument("--ruler", default=DEFAULT_RULER)
    ap.add_argument("--project-dir", default=str(REPO_ROOT))
    ap.add_argument("--symbol", default=None, help="explain one symbol and exit")
    a = ap.parse_args()
    con = sqlite3.connect(a.db)
    g = build_callee_gate(con, ruler=a.ruler, project_dir=a.project_dir)
    if a.symbol:
        verdict = "BLOCKED" if g.blocks(a.symbol) else (
            "cleared" if a.symbol in g.cleared else "not in the callee population")
        print(f"{a.symbol}\n  {verdict}  ({g.reason(a.symbol)})")
        for e in g.evidence.get(a.symbol, []):
            print(f"    {e}")
    else:
        print(g.summary())
