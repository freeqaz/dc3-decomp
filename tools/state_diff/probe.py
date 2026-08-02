"""Probe specs: parse ``probes/*.dta``, compile to paged DTA, execute.

A probe is a declarative spec written in DTA syntax (readable by the engine's
own data language and by this parser). The driver compiles it into *pages* —
each page is one request that fits the transport caps in :mod:`budget` — and
parses the delimited replies back into records.

Paging is structural, not incidental. A probe is split across BOTH axes:

* **fields**: when a probe's field set does not fit one object's read in a
  single script, fields are grouped across several requests.
* **objects**: as many objects as fit the remaining script and reply budget
  share a page.

That means a probe never "dumps a tree in one request", so what works on the
native HTTP endpoint also works over a capped console channel — including the
legacy ~120-char GET transport, where paging degrades to one read per request
rather than failing.

Spec grammar (a DTA array)::

    (probe <id>
        (doc "one line description")
        (discriminates "what class of visual bug this catches")
        (scope (dir main) (isa Draw) (classes Mesh) (limit 500)
               (names "[ui.cam]"))
        (roster_program_file "x.prog.dta")   ; optional DTA enumeration
        (fields
            (<key> prop (<prop> <sub> ...) <kind>)
            (<key> msg  <handler>          <kind>)))

or, for global/singleton state::

    (probe <id> (kind scalars) (line_sep ";")
        (program_file "a.prog.dta" "b.prog.dta"))

``kind`` is one of: ``int`` ``float`` ``bool`` ``color`` ``sym`` ``obj``.

* ``prop`` fields go through ``{$o get (path) <default>}``. The default is
  mandatory: ``Hmx::Object::OnGet`` (src/system/obj/Object.cpp:870-899) uses
  ``Property(sym, a->Size() < 4)``, so a 3-node get hard-fails on console.
  They are additionally guarded by ``{$o has (path)}`` so a class that lacks
  the property yields ``<absent>`` rather than a misleading 0.
* ``msg`` fields dispatch a handler directly (``{$o showing}``). Only declare
  these where ``scope.isa`` guarantees the handler exists.
* ``obj`` fields resolve to the referenced object's *name*, or ``<null>``.
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable

from .budget import BudgetError, Limits, check_result, validate_script
from .transport import ABSENT, NULL_OBJ, ObjRef, Target, TransportError

PROBE_DIR = Path(__file__).parent / "probes"

FIELD_SEP = "~@~"
RECORD_SEP = "~#~"
ERROR = "<error>"

KINDS = {"int", "float", "bool", "color", "sym", "obj"}

#: Conservative reply-size estimate per field kind, in bytes (value only).
RESULT_ESTIMATE = {
    "float": 12, "int": 8, "bool": 1, "color": 11, "sym": 28, "obj": 28,
}


# --------------------------------------------------------------------------
# Minimal DTA / s-expression reader (sufficient for probe specs)
# --------------------------------------------------------------------------

_TOKEN = re.compile(r'''
      (?P<ws>\s+)
    | (?P<comment>;[^\n]*)
    | (?P<open>[(\[{])
    | (?P<close>[)\]}])
    | (?P<string>"(?:[^"\\]|\\.)*")
    | (?P<atom>[^\s()\[\]{};"]+)
''', re.VERBOSE)


def parse_dta(text: str) -> list:
    """Parse DTA source into nested Python lists."""
    stack: list[list] = [[]]
    pos = 0
    while pos < len(text):
        m = _TOKEN.match(text, pos)
        if not m:
            raise ValueError(f"cannot tokenize at offset {pos}: {text[pos:pos + 40]!r}")
        pos = m.end()
        kind = m.lastgroup
        if kind in ("ws", "comment"):
            continue
        if kind == "open":
            stack.append([])
        elif kind == "close":
            if len(stack) == 1:
                raise ValueError("unbalanced ')' in probe spec")
            done = stack.pop()
            stack[-1].append(done)
        elif kind == "string":
            raw = m.group()[1:-1]
            stack[-1].append(raw.replace('\\"', '"').replace("\\\\", "\\"))
        else:
            stack[-1].append(m.group())
    if len(stack) != 1:
        raise ValueError("unbalanced '(' in probe spec")
    return stack[0]


def _strip_comments(text: str) -> str:
    """Drop ``;`` line comments and collapse to one line.

    Program files are sent verbatim as the request body, so comments must go
    and newlines must not split a DTA token. Semicolons inside double-quoted
    DTA strings are preserved (they are the scalars line separator).
    """
    out: list[str] = []
    for line in text.splitlines():
        buf, in_str, i = [], False, 0
        while i < len(line):
            c = line[i]
            if in_str:
                buf.append(c)
                if c == "\\" and i + 1 < len(line):
                    buf.append(line[i + 1])
                    i += 2
                    continue
                if c == '"':
                    in_str = False
            elif c == '"':
                in_str = True
                buf.append(c)
            elif c == ";":
                break
            else:
                buf.append(c)
            i += 1
        seg = "".join(buf).strip()
        if seg:
            out.append(seg)
    return " ".join(out)


def _sections(body: list) -> dict[str, list]:
    out: dict[str, list] = {}
    for item in body:
        if isinstance(item, list) and item and isinstance(item[0], str):
            out[item[0]] = item[1:]
    return out


def dta_quote(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


#: Characters that must never reach the DTA parser inside an object name.
#: Names are always emitted inside a quoted DTA string, and `[`/`]`/spaces are
#: safe there (real DC3 names look like "[ui.cam]" and "[default lit]" —
#: verified live). Only characters that break OUT of the quoting are dangerous:
#: an escaped quote or a control byte can terminate the string early and leave
#: the parser with garbage, and `DataReadString` faults via MILO_FAIL rather
#: than returning an error — not catchable from the console server's C code.
_UNSAFE_NAME = re.compile(r'["\\\x00-\x1f\x7f]')


class UnenumeratedName(ValueError):
    """Raised when a probe would read a name that was never enumerated."""


def assert_enumerated(names: Iterable[str], enumerated: set[str], probe_id: str) -> None:
    """Refuse to emit reads for names that did not come back from enumeration.

    Input to the console DTA parser is TRUSTED: a syntactically valid script
    naming an object that does not exist still faults the title. So probes are
    strictly two-pass — enumerate real names, then read only those names. An
    author-supplied ``(names ...)`` entry that no longer exists in the running
    game is a crash, not a miss, which is why this is an error rather than a
    silent skip.
    """
    for n in names:
        if n not in enumerated:
            raise UnenumeratedName(
                f"probe {probe_id}: refusing to read {n!r} — it was not "
                "returned by enumeration in this session. Probes must be "
                "two-pass; naming a nonexistent object faults the title."
            )
        bad = _UNSAFE_NAME.search(n)
        if bad:
            raise UnenumeratedName(
                f"probe {probe_id}: object name {n!r} contains unsafe "
                f"character {bad.group()!r} for the DTA parser; refusing to emit."
            )


# --------------------------------------------------------------------------
# Spec model
# --------------------------------------------------------------------------


@dataclass
class Field:
    key: str
    source: str          # "prop" | "msg"
    path: list[str]      # prop path, or [handler] for msg
    kind: str

    DEFAULTS = {"float": "0.0", "int": "0", "bool": "0", "color": "0",
                "sym": '""', "obj": "0"}

    def value_expr(self, var: str = "$o") -> str:
        if self.source == "prop":
            return "{%s get (%s) %s}" % (
                var, " ".join(self.path), self.DEFAULTS[self.kind]
            )
        return "{%s %s}" % (var, self.path[0])

    def commands(self, var: str = "$o", acc: str = "$s", guard: bool = True) -> str:
        """DTA command(s) appending this field's stringified value to ``acc``.

        ``guard`` wraps prop reads in ``{$o has (path)}`` so a class lacking the
        property yields ``<absent>`` instead of the `get` default (which is
        indistinguishable from a real 0). The guard roughly DOUBLES the emitted
        script, which under the 1000-char console cap directly halves how many
        objects fit in a request — so probes whose ``scope.isa`` already
        guarantees the property set turn it off via ``(guard 0)``.
        """
        raw = self.value_expr(var)
        if self.kind == "obj":
            body = (
                "{set $p %s}"
                '{if_else $p {strcat %s {$p name}} {strcat %s "%s"}}'
                % (raw, acc, acc, NULL_OBJ)
            )
        elif self.kind == "float":
            body = '{strcat %s {sprintf "%%.9g" %s}}' % (acc, raw)
        elif self.kind in ("int", "bool", "color"):
            body = '{strcat %s {sprintf "%%d" %s}}' % (acc, raw)
        else:
            body = "{strcat %s %s}" % (acc, raw)

        if self.source == "prop" and guard:
            g = "{%s has (%s)}" % (var, " ".join(self.path))
            body = '{if_else %s {do %s} {strcat %s "%s"}}' % (g, body, acc, ABSENT)
        return body + '{strcat %s "%s"}' % (acc, FIELD_SEP)

    def result_estimate(self) -> int:
        return RESULT_ESTIMATE.get(self.kind, 28) + len(FIELD_SEP)


#: Roster classes that must never be messaged. A bare `Object` in `main` is a
#: DTA *script* object: its behaviour lives in `mTypeDef`, and Hmx::Object
#: dispatches unmatched messages there via HANDLE_ARRAY(mTypeDef)
#: (src/system/obj/Object.cpp:159). Sending one even a harmless-looking
#: `is_a` therefore EXECUTES GAME SCRIPT — observed live: probing
#: `campaign_commence_mindcontrol` produced "CAMPAIGN FLOW ERROR at 'is_a'"
#: followed by a SIGSEGV, and enough of those killed the engine. `main` holds
#: ~170 of them, so this exclusion is what makes broad probes survivable.
DANGEROUS_CLASSES = ("Object",)


@dataclass
class Scope:
    dir: str = "main"
    isa: list[str] = field(default_factory=list)
    classes: list[str] = field(default_factory=list)
    names: list[str] = field(default_factory=list)
    #: Roster classes to skip entirely. Defaults to DANGEROUS_CLASSES.
    exclude_classes: list[str] = field(
        default_factory=lambda: list(DANGEROUS_CLASSES))
    limit: int = 0
    #: Wrap prop reads in `has` to distinguish <absent> from a real 0.
    #: Turn off (guard 0) when `isa` already guarantees the property set —
    #: it roughly halves the emitted script and doubles objects-per-request.
    guard: bool = True

    def select(self, roster: Iterable[ObjRef]) -> list[ObjRef]:
        refs = list(roster)
        if self.classes:
            want = set(self.classes)
            refs = [r for r in refs if r.type in want]
        if self.exclude_classes:
            # Applied only when the roster actually carries class info; a DTA
            # roster_program returns bare names (type "") and is trusted to
            # have filtered server-side already.
            skip = set(self.exclude_classes)
            refs = [r for r in refs if not r.type or r.type not in skip]
        if self.names:
            want = set(self.names)
            refs = [r for r in refs if r.name in want]
        # Stable traversal order: sort by (type, name) so output is diffable
        # regardless of the engine's hash-table iteration order.
        refs.sort(key=lambda r: (r.type, r.name))
        if self.limit:
            refs = refs[: self.limit]
        return refs


@dataclass
class Page:
    """One request: a set of fields read for a set of objects."""

    fields: list[Field]
    names: list[str]
    program: str


# Wrapper / per-object script overheads, measured from the emitted text.
_WRAPPER = len('{do ($s "") ($o 0) ($p 0) $s}')
_OBJ_FIXED = len('{set $o {find_obj main ""}}{if_else $o {do } {strcat $s "0~@~"}}'
                 '{strcat $s "~#~"}')
_REC_RESULT_FIXED = len("1" + FIELD_SEP) + len(RECORD_SEP)


@dataclass
class Probe:
    id: str
    doc: str = ""
    discriminates: str = ""
    scope: Scope = field(default_factory=Scope)
    fields: list[Field] = field(default_factory=list)
    path: Path | None = None
    #: "objects" walks a roster; "scalars" runs fixed programs and parses
    #: ``key=value`` pairs (global/singleton state, engine-native iterators).
    kind: str = "objects"
    programs: list[str] = field(default_factory=list)
    roster_program: str = ""
    line_sep: str = ";"

    @property
    def keys(self) -> list[str]:
        return [f.key for f in self.fields]

    # -- compilation --------------------------------------------------

    def _object_block(self, name: str, fields: list[Field]) -> str:
        """Commands producing one record for ``name`` (inside the {do} wrapper).

        Uses ``find_obj`` rather than ``object`` because ``object`` hard-fails
        on a missing name (src/system/obj/DataFunc.cpp:658-672) whereas
        ``find_obj`` returns null (DataFunc.cpp:1167-1182). The record body is
        wrapped in a null check so an object deleted between roster and read
        degrades to a skipped record instead of a crash.
        """
        miss = '{strcat $s "0%s"}' % FIELD_SEP
        reads = '{strcat $s "1%s"}' % FIELD_SEP
        reads += "".join(f.commands(guard=self.scope.guard) for f in fields)

        if self.scope.isa:
            checks = " ".join("{$o is_a %s}" % c for c in self.scope.isa)
            gate = checks if len(self.scope.isa) == 1 else "{|| %s}" % checks
            # The is_a check must GATE the field reads, not merely be reported
            # alongside them. Reading RndDrawable/RndMat properties off the
            # plain script Objects that share `main` is both wasted work and a
            # crash risk, and a crashed eval leaks DTA call-stack entries that
            # eventually kill the engine (see MAX_EVAL_FAILURES).
            inner = '{if_else %s {do %s} %s}' % (gate, reads, miss)
        else:
            inner = '{do %s}' % reads

        return (
            "{set $o {find_obj %s %s}}" % (self.scope.dir, dta_quote(name))
            + '{if_else $o %s %s}' % (inner, miss)
            + '{strcat $s "%s"}' % RECORD_SEP
        )

    def _program(self, names: list[str], fields: list[Field]) -> str:
        """One page's DTA, as a SINGLE top-level command."""
        blocks = "".join(self._object_block(n, fields) for n in names)
        # The payload is returned as a raw DTA string, not wrapped in
        # {symbol ...}. The wrapper used to be required because the endpoint
        # could not serialize kDataString; now that it can, dropping it avoids
        # interning every page's payload into the global symbol table forever
        # (the hierarchy probe alone would leak ~500 unique symbols per run).
        return '{do ($s "") ($o 0) ($p 0) %s$s}' % blocks

    def field_groups(self, longest_name: str, limits: Limits) -> list[list[Field]]:
        """Split fields so ONE object's block fits the script and reply caps.

        Costs are measured on the actually-emitted text (via
        :meth:`_object_block`) rather than estimated, so the result is exact
        for the script axis; only the reply axis is predicted.
        """
        if not limits.enforced:
            return [list(self.fields)]

        # Room for one object's block: whole cap minus the {do ...} wrapper.
        script_budget = limits.max_script - _WRAPPER
        result_budget = limits.result_headroom - _REC_RESULT_FIXED
        if result_budget <= 0:
            raise BudgetError(
                f"probe {self.id}: reply cap {limits.max_result} leaves no room "
                "for a record"
            )

        groups: list[list[Field]] = []
        cur: list[Field] = []
        r = 0
        for f in self.fields:
            trial = cur + [f]
            if len(self._object_block(longest_name, trial)) > script_budget or (
                cur and r + f.result_estimate() > result_budget
            ):
                if not cur:
                    raise BudgetError(
                        f"probe {self.id}: field {f.key!r} alone does not fit the "
                        f"{limits.max_script}-char script cap for object name "
                        f"{longest_name!r}. Shorten the property path, narrow the "
                        "scope, or raise --script-cap."
                    )
                groups.append(cur)
                cur, r = [], 0
            cur.append(f)
            r += f.result_estimate()
        if cur:
            groups.append(cur)
        return groups

    def pages(self, names: list[str], limits: Limits) -> list[Page]:
        """Split (objects x fields) into requests that each fit the caps."""
        if not names or not self.fields:
            return []
        longest = max(names, key=len)
        out: list[Page] = []
        for group in self.field_groups(longest, limits):
            per_obj_result = sum(f.result_estimate() for f in group) + _REC_RESULT_FIXED
            cur: list[str] = []
            script = _WRAPPER
            r = 0
            for n in names:
                cost = len(self._object_block(n, group))
                if cur and limits.enforced and (
                    script + cost > limits.max_script
                    or r + per_obj_result > limits.result_headroom
                ):
                    out.append(Page(group, cur, self._program(cur, group)))
                    cur, script, r = [], _WRAPPER, 0
                cur.append(n)
                script += cost
                r += per_obj_result
            if cur:
                out.append(Page(group, cur, self._program(cur, group)))
        for p in out:
            validate_script(p.program, limits, f"probe {self.id}")
        return out

    # -- parsing ------------------------------------------------------

    def parse_page(self, text: str, page: Page) -> dict[str, dict]:
        """Split a page reply into ``{object_name: {field: raw_str}}``."""
        out: dict[str, dict] = {}
        chunks = text.split(RECORD_SEP)
        if chunks and chunks[-1] == "":
            chunks.pop()
        for name, chunk in zip(page.names, chunks):
            vals = chunk.split(FIELD_SEP)
            if vals and vals[-1] == "":
                vals.pop()
            if not vals:
                out[name] = {"_error": "empty record"}
                continue
            gate, rest = vals[0], vals[1:]
            if gate == "0":
                continue  # missing object, or failed the is_a gate
            if len(rest) != len(page.fields):
                out[name] = {
                    "_error": f"field count {len(rest)} != {len(page.fields)}",
                    "_raw": chunk[:200],
                }
                continue
            out[name] = {f.key: v for f, v in zip(page.fields, rest)}
        for name in page.names[len(chunks):]:
            out[name] = {"_error": "truncated page"}
        return out


# --------------------------------------------------------------------------
# Loading
# --------------------------------------------------------------------------


def load_probe(path: str | Path) -> Probe:
    path = Path(path)
    forms = parse_dta(path.read_text())
    top = next((f for f in forms if isinstance(f, list) and f and f[0] == "probe"), None)
    if top is None:
        raise ValueError(f"{path}: no (probe ...) form found")
    pid = top[1]
    sec = _sections(top[2:])

    kind = (sec.get("kind") or ["objects"])[0]
    if kind not in ("objects", "scalars"):
        raise ValueError(f"{path}: unknown probe kind {kind!r}")

    line_sep = (sec.get("line_sep") or [";"])[0]

    if kind == "scalars":
        progs: list[str] = list(sec.get("program", []))
        for fn in sec.get("program_file", []):
            progs.append(_strip_comments((path.parent / fn).read_text()))
        if not progs:
            raise ValueError(
                f"{path}: scalars probe needs (program ...) or (program_file ...)"
            )
        return Probe(
            id=pid,
            doc=(sec.get("doc") or [""])[0],
            discriminates=(sec.get("discriminates") or [""])[0],
            path=path,
            kind="scalars",
            programs=progs,
            line_sep=line_sep,
        )

    scope_kv = _sections(sec.get("scope", []))
    scope = Scope(
        dir=(scope_kv.get("dir") or ["main"])[0],
        isa=list(scope_kv.get("isa", [])),
        classes=list(scope_kv.get("classes", [])),
        names=list(scope_kv.get("names", [])),
        limit=int((scope_kv.get("limit") or [0])[0]),
        guard=bool(int((scope_kv.get("guard") or [1])[0])),
        exclude_classes=list(
            scope_kv["exclude_classes"] if "exclude_classes" in scope_kv
            else DANGEROUS_CLASSES
        ),
    )

    roster_prog = ""
    if sec.get("roster_program_file"):
        roster_prog = _strip_comments(
            (path.parent / sec["roster_program_file"][0]).read_text()
        )
    elif sec.get("roster_program"):
        roster_prog = sec["roster_program"][0]

    fields: list[Field] = []
    for f in sec.get("fields", []):
        if not isinstance(f, list) or len(f) < 4:
            raise ValueError(f"{path}: bad field spec {f!r}")
        key, source, target, kind_ = f[0], f[1], f[2], f[3]
        if kind_ not in KINDS:
            raise ValueError(f"{path}: field {key}: unknown kind {kind_!r}")
        if source == "prop":
            if not isinstance(target, list):
                raise ValueError(f"{path}: field {key}: prop target must be (a b c)")
            fields.append(Field(key, "prop", [str(x) for x in target], kind_))
        elif source == "msg":
            fields.append(Field(key, "msg", [str(target)], kind_))
        else:
            raise ValueError(f"{path}: field {key}: unknown source {source!r}")

    return Probe(
        id=pid,
        doc=(sec.get("doc") or [""])[0],
        discriminates=(sec.get("discriminates") or [""])[0],
        scope=scope,
        fields=fields,
        path=path,
        roster_program=roster_prog,
        line_sep=line_sep,
    )


def load_all(probe_dir: Path | None = None) -> dict[str, Probe]:
    d = Path(probe_dir or PROBE_DIR)
    probes = {}
    for f in sorted(d.glob("*.dta")):
        if f.name.endswith(".prog.dta"):
            continue
        p = load_probe(f)
        probes[p.id] = p
    return probes


def load_manifest(probe_dir: Path | None = None) -> dict:
    d = Path(probe_dir or PROBE_DIR)
    mf = d / "manifest.json"
    return json.loads(mf.read_text()) if mf.exists() else {}


# --------------------------------------------------------------------------
# Execution
# --------------------------------------------------------------------------


@dataclass
class RunStats:
    requests: int = 0
    eval_failures: int = 0
    fallbacks: int = 0
    objects: int = 0
    max_script: int = 0
    max_result: int = 0
    aborted: bool = False
    errors: list[str] = field(default_factory=list)


#: Abort a probe after this many failed evals. This is a SAFETY limit, not a
#: convenience one: a DTA eval that SIGSEGVs is caught by the native port's
#: sigsetjmp recovery (native/src/platform/HttpServer.cpp:328-479), but that
#: recovery does NOT unwind `gCallStackPtr`. Every failed eval therefore leaks
#: DTA call-stack entries until `MILO_ASSERT(gCallStackPtr - gCallStack <
#: HANDLE_STACK_SIZE)` (src/system/obj/DataArray.cpp:47) starts firing, after
#: which the engine dies on the MAIN thread in unrelated code (observed:
#: CursorPanel::Poll -> Hmx::Object::Property -> DataArray::FindArray).
#: So once evals start failing, the target is on a countdown to a crash and
#: further data is untrustworthy — stop and say so.
MAX_EVAL_FAILURES = 25


def run_probe(
    target: Target,
    probe: Probe,
    limits: Limits | None = None,
    roster: list[ObjRef] | None = None,
    max_eval_failures: int = MAX_EVAL_FAILURES,
    page_batch: int = 16,
) -> tuple[dict[str, dict], RunStats]:
    """Execute ``probe`` against ``target``, one request per page.

    Failures are recorded per-object rather than aborting: a page that fails is
    retried one object at a time so a single crashing object cannot blank out
    its neighbours.
    """
    limits = limits or Limits()
    stats = RunStats()

    if probe.kind == "scalars":
        return _run_scalars(target, probe, limits, stats)

    if roster is None:
        roster = _roster_for(target, probe, limits, stats)
    enumerated = {r.name for r in roster}
    if probe.scope.names:
        missing = [n for n in probe.scope.names if n not in enumerated]
        if missing:
            stats.errors.append(
                f"{probe.id}: {len(missing)} name(s) from the spec's (names ...) "
                f"were not enumerated and will NOT be read: {missing[:5]}"
            )
    selected = probe.scope.select(roster)
    # Defense in depth: every name we are about to emit must have come back
    # from enumeration this session. Reading an object that does not exist
    # faults the title rather than returning an error.
    assert_enumerated([r.name for r in selected], enumerated, probe.id)
    stats.objects = len(selected)
    by_name = {r.name: r for r in selected}
    names = [r.name for r in selected]

    records: dict[str, dict] = {}
    all_pages = probe.pages(names, limits)

    # Pages are dispatched through eval_batch so transports that support it
    # (the console clients do) send a whole chunk in ONE round trip — on the
    # DC3 file transport that is also one human keypress. Chunking rather than
    # one giant batch keeps the eval-failure breaker able to intervene.
    for chunk_start in range(0, len(all_pages), page_batch):
        if stats.eval_failures >= max_eval_failures:
            stats.aborted = True
            stats.errors.append(
                f"{probe.id}: ABORTED after {stats.eval_failures} eval failures. "
                "Each failed eval leaks a DTA call-stack entry on the native "
                "port, so continuing would crash the engine and the remaining "
                "data would be untrustworthy. Narrow the probe scope."
            )
            break
        chunk = all_pages[chunk_start:chunk_start + page_batch]
        for pg in chunk:
            stats.max_script = max(stats.max_script, len(pg.program))
        try:
            results = target.eval_batch([pg.program for pg in chunk])
            stats.requests += len(chunk)
        except TransportError as e:
            stats.errors.append(f"transport: {e}")
            for pg in chunk:
                for n in pg.names:
                    records.setdefault(n, {})["_error"] = ERROR
            continue
        if len(results) != len(chunk):
            # Never attribute a short batch: values would land on the wrong
            # objects and the report would be confidently wrong.
            stats.errors.append(
                f"{probe.id}: batch returned {len(results)} result(s) for "
                f"{len(chunk)} page(s); refusing to attribute"
            )
            for pg in chunk:
                for n in pg.names:
                    records.setdefault(n, {})["_error"] = "batch attribution refused"
            continue

        for page, res in zip(chunk, results):
            _consume(probe, page, res, records, by_name, stats, limits,
                     target, max_eval_failures)

    return records, stats


def _consume(probe, page, res, records, by_name, stats, limits, target,
             max_eval_failures) -> None:
    """Parse one page's reply, degrading to per-object reads if it is bad."""
    if res.ok:
        warn = check_result(res.text, limits, f"probe {probe.id}")
        if warn:
            stats.errors.append(warn)
        stats.max_result = max(stats.max_result, len(res.text))
        parsed = probe.parse_page(res.text, page)
        if not any("_error" in v for v in parsed.values()):
            _merge(records, parsed, by_name)
            return

    # Page failed or came back malformed: degrade to one object per call.
    stats.fallbacks += 1
    for n in page.names:
        if stats.eval_failures >= max_eval_failures:
            records.setdefault(n, {})["_error"] = "skipped (breaker tripped)"
            continue
        solo = Page(page.fields, [n], probe._program([n], page.fields))
        try:
            r1 = target.eval_dta(solo.program)
            stats.requests += 1
        except TransportError as e:
            stats.errors.append(f"transport: {e}")
            records.setdefault(n, {})["_error"] = ERROR
            continue
        if not r1.ok:
            stats.eval_failures += 1
            stats.errors.append(f"{probe.id}/{n}: {r1.error}")
            records.setdefault(n, {})["_error"] = r1.error or ERROR
            continue
        _merge(records, probe.parse_page(r1.text, solo), by_name)


def _run_scalars(target, probe, limits, stats):
    out: dict[str, dict] = {}
    for prog in probe.programs:
        validate_script(prog, limits, f"probe {probe.id}")
        stats.max_script = max(stats.max_script, len(prog))
        try:
            res = target.eval_dta(prog, timeout=30.0)
            stats.requests += 1
        except TransportError as e:
            stats.errors.append(f"transport: {e}")
            continue
        if not res.ok:
            stats.eval_failures += 1
            stats.errors.append(f"{probe.id}: {res.error}")
            continue
        stats.max_result = max(stats.max_result, len(res.text))
        warn = check_result(res.text, limits, f"probe {probe.id}")
        if warn:
            stats.errors.append(warn)
        for line in res.text.split(probe.line_sep):
            line = line.strip()
            if not line or "=" not in line:
                continue
            k, v = line.split("=", 1)
            out[k.strip()] = {"value": v}
    stats.objects = len(out)
    return out, stats


def _roster_for(target, probe, limits, stats) -> list[ObjRef]:
    """Enumerate objects, preferring a probe-supplied DTA roster program.

    A DTA roster program is portable (it is how the console side must do it),
    so when a probe supplies one it wins over the transport's native listing.
    """
    if probe.roster_program:
        validate_script(probe.roster_program, limits, f"probe {probe.id} roster")
        try:
            res = target.eval_dta(probe.roster_program, timeout=30.0)
            stats.requests += 1
        except TransportError as e:
            stats.errors.append(f"roster transport: {e}")
            return []
        if not res.ok:
            stats.errors.append(f"{probe.id} roster: {res.error}")
            return []
        stats.max_result = max(stats.max_result, len(res.text))
        warn = check_result(res.text, limits, f"probe {probe.id} roster")
        if warn:
            # A truncated roster silently drops objects, which would read as
            # "missing on one side" in the diff — the worst possible failure.
            stats.errors.append(warn + " ROSTER MAY BE INCOMPLETE.")
        return [
            ObjRef(name=n.strip(), type="", dir=probe.scope.dir)
            for n in res.text.split(probe.line_sep)
            if n.strip()
        ]
    return target.roster(probe.scope.dir, probe.scope.classes or None)


def _merge(records: dict, parsed: dict, by_name: dict[str, ObjRef]) -> None:
    """Merge one page's records in, annotating with roster class info."""
    for n, rec in parsed.items():
        dst = records.setdefault(n, {})
        dst.update(rec)
        ref = by_name.get(n)
        if ref is not None and ref.type:
            dst.setdefault("_class", ref.type)
