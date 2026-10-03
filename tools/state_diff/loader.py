"""Loader probe: load a .milo into a FRESH ObjectDir and dump every object.

Tier A of docs/plans/XENIA_ORACLE.md: the probe does the work. It runs

    {set $xg_d<N> {load_objects "<milo>"}}

which is ``DirLoader::LoadObjects`` into a new ObjectDir that nothing else in
the engine references, then reads, for every object ``object_list`` returns
from that dir:

* ``_class`` (``class_name``), ``_type`` (``get_type``), ``_dir`` (owning dir);
* every property the Milo editor schema names for the class chain
  (:mod:`editor_schema`), each guarded by ``{$o has (path)}``;
* every array's ``size``, then (pass 2, 3) its first ``max_elems`` elements.

Values are formatted **inside the engine** with a type tag, ``<typeid>:<value>``,
so a property that is a float on one side and an int on the other is a visible
difference rather than a formatting accident: floats ``%.9g`` (round-trips a
float32), ints ``%d``, objects by name / ``<null>``, everything else ``sprint``.

The same programs run on the original game under Xenia (``xenia:<sock>``) and
on the native port (``native`` HTTP), so a difference is a difference between
the two ``Load``/``SyncProperty`` implementations, not between two tools.

Failure handling. A page whose evaluation is refused (the original binary
traps a MILO_FAIL/MILO_ASSERT thrown by a getter; native reports ok=false) is
split in half and retried, down to one read; a single refused read is
recorded as ``<refused>``. That isolates the faulting property, and a read
that refuses on one side only is itself a finding.
"""

from __future__ import annotations

import time
from dataclasses import dataclass, field
from pathlib import Path

from . import editor_schema as es
from .probe import dta_quote, _UNSAFE_NAME
from .transport import Target, TransportError

FS = "~@~"   # field separator
RS = "~#~"   # record separator
ABSENT = "<absent>"
REFUSED = "<refused>"
GONE = "<gone>"

#: Per-array element caps by the array's LAST path component, overriding
#: ``max_elems``. Vertex/face arrays are thousands long and one element of
#: each is enough to see a layout bug; the full count is always read.
ELEM_CAPS = {"verts": 4, "faces": 4, "frames": 16}

END = "~$~"  # payload terminator: its absence means the reply was cut short

#: Script / reply budgets. Both transports cap the script at 16384 (portable:
#: < 16384) and the reply at 32768 -- but the Xenia DTA channel ALSO cuts a
#: kDataString result at 4096 bytes with no sentinel (dc3_dta_channel.cc
#: ReadCString(addr, max = 4096)), measured: a 4410-byte object_list page
#: came back as exactly 4096 bytes. So every payload carries the END
#: terminator and a page without it is bisected, and the reply estimate
#: stays well under 4096 so bisection is the exception.
MAX_SCRIPT = 15000
MAX_REPLY_EST = 2800

#: In-engine value formatter. ``$v`` holds the value; appends ``<type>:<val>``.
#: type ids: 0 int, 1 float, 4 object, 5 symbol, 18 string (obj/Data.h).
FMT = (
    '{set $t {type $v}}'
    '{strcat $s {sprintf "%d:" $t}}'
    '{if_else {== $t 1} {strcat $s {sprintf "%.9g" $v}}'
    ' {if_else {== $t 0} {strcat $s {sprintf "%d" $v}}'
    '  {if_else {== $t 4} {if_else $v {strcat $s {$v name}} {strcat $s "<null>"}}'
    '   {strcat $s {sprint $v}}}}}'
)


def path_key(path: tuple) -> str:
    return "/".join(str(p) for p in path)


def dta_path(path: tuple) -> str:
    return "(" + " ".join(str(p) for p in path) + ")"


@dataclass
class Read:
    obj: str
    path: tuple
    mode: str  # "v" value (has-guarded) | "n" array size (unguarded)

    @property
    def key(self) -> str:
        return path_key(self.path) + ("/#" if self.mode == "n" else "")


@dataclass
class LoaderStats:
    requests: int = 0
    refused_pages: int = 0
    refused_reads: int = 0
    max_script: int = 0
    max_reply: int = 0
    seconds: float = 0.0


class LoaderProbe:
    def __init__(self, target: Target, schemas: dict, *, max_elems: int = 64,
                 max_depth: int = 3, batch: int = 8, log=None):
        self.t = target
        self.schemas = schemas
        self.max_elems = max_elems
        self.max_depth = max_depth
        self.batch = batch
        self.stats = LoaderStats()
        self.log = log or (lambda *a: None)

    # -- low level ------------------------------------------------------

    def _eval_many(self, programs: list[str]) -> list:
        """Evaluate pages, batching only as many as fit ONE request body.

        A console batch is concatenated into one body, and the clients refuse
        a body >= 16384 for the whole batch (dc3_eval DtaError). So batches
        are packed by size, never by count alone.
        """
        out = []
        batches: list[list[str]] = []
        cur: list[str] = []
        size = 0
        for p in programs:
            if len(p) >= 16383:
                raise TransportError(f"page of {len(p)} bytes exceeds the portable cap")
            self.stats.max_script = max(self.stats.max_script, len(p))
            if cur and (size + len(p) + 2 >= MAX_SCRIPT or len(cur) >= self.batch):
                batches.append(cur)
                cur, size = [], 0
            cur.append(p)
            size += len(p) + 2
        if cur:
            batches.append(cur)
        for chunk in batches:
            res = self.t.eval_batch(chunk)
            self.stats.requests += len(chunk)
            out.extend(res)
        return out

    def eval1(self, expr: str):
        r = self.t.eval_dta(expr, timeout=120.0)
        self.stats.requests += 1
        return r

    # -- load + enumerate ---------------------------------------------

    def load(self, milo: str, var: str) -> dict:
        if _UNSAFE_NAME.search(milo):
            raise ValueError(f"unsafe milo path {milo!r}")
        r = self.eval1("{set %s {load_objects %s}}" % (var, dta_quote(milo)))
        if not r.ok:
            raise TransportError(f"load_objects {milo}: {r.error}")
        info = self.eval1(
            '{if_else %s {sprint {%s class_name} "~@~" {%s name}} "<null>"}' % (var, var, var))
        if not info.ok or info.text == "<null>":
            raise TransportError(f"load_objects {milo} returned null ({info.text or info.error})")
        cls, _, name = info.text.partition(FS)
        return {"class": cls, "name": name}

    def enumerate(self, var: str) -> list[str]:
        n = self.eval1("{size {object_list %s Object FALSE}}" % var)
        if not n.ok:
            raise TransportError(f"object_list size: {n.error}")
        total = int(str(n.value))
        names: list[str] = []
        step = 60
        for lo in range(0, total, step):
            hi = min(total, lo + step)
            r = self.eval1(
                '{do ($a {object_list %s Object FALSE}) ($s "") '
                '{foreach_int $i %d %d {strcat $s {elem $a $i} "%s"}} {strcat $s "%s"} $s}'
                % (var, lo, hi, RS, END))
            if not r.ok:
                raise TransportError(f"object_list page {lo}: {r.error}")
            if not r.text.endswith(END):
                raise TransportError(f"object_list page {lo}: reply cut short "
                                     f"({len(r.text)} bytes, no terminator)")
            chunk = r.text[:-len(END)].split(RS)
            if chunk and chunk[-1] == "":
                chunk.pop()
            if len(chunk) != hi - lo:
                raise TransportError(f"object_list page {lo}: {len(chunk)} names for {hi - lo}")
            names.extend(chunk)
        if len(names) != total:
            raise TransportError(f"enumerated {len(names)} of {total}")
        return names

    # -- paged reads ----------------------------------------------------

    def _block(self, var: str, obj: str, reads: list[Read], meta: bool) -> str:
        vals = [r for r in reads if r.mode == "v"]
        sizes = [r for r in reads if r.mode == "n"]
        body = ""
        if meta:
            body += ('{strcat $s {sprint {$o class_name}} "%s" {sprint {$o get_type}} "%s"}'
                     '{set $p {$o dir}}{if_else $p {strcat $s {$p name}} {strcat $s "<null>"}}'
                     '{strcat $s "%s"}' % (FS, FS, FS))
        if vals:
            body += ('{foreach $q (%s) {if_else {$o has $q} {do {set $v {$o get $q 0}} %s}'
                     ' {strcat $s "%s"}} {strcat $s "%s"}}'
                     % (" ".join(dta_path(r.path) for r in vals), FMT, ABSENT, FS))
        if sizes:
            body += ('{foreach $q (%s) {strcat $s {sprintf "%%d" {$o size $q}} "%s"}}'
                     % (" ".join(dta_path(r.path) for r in sizes), FS))
        return ('{set $o {find_obj %s %s}}{if_else $o {do %s} {strcat $s "%s"}}{strcat $s "%s"}'
                % (var, dta_quote(obj), body, GONE, RS))

    @staticmethod
    def _order(reads: list[Read]) -> list[Read]:
        # The block emits value reads first, then sizes; parse in that order.
        return [r for r in reads if r.mode == "v"] + [r for r in reads if r.mode == "n"]

    def _program(self, var: str, groups: list[tuple[str, list[Read], bool]]) -> str:
        blocks = "".join(self._block(var, o, rs, m) for o, rs, m in groups)
        return ('{do ($s "") ($o 0) ($p 0) ($q 0) ($v 0) ($t 0) %s {strcat $s "%s"} $s}'
                % (blocks, END))

    @staticmethod
    def _reply_estimate(groups) -> int:
        return sum(24 * (len(rs) + (3 if m else 0)) + 8 for _o, rs, m in groups)

    def _pages(self, var: str, items: list[tuple[str, list[Read], bool]]):
        """Greedy packing of (obj, reads, meta) items, splitting big objects."""
        pages: list[list] = []
        cur: list = []
        for obj, reads, meta in items:
            # Split one object's reads into chunks that fit alone.
            chunks: list[list[Read]] = []
            c: list[Read] = []
            for r in reads:
                trial = c + [r]
                if c and (len(self._program(var, [(obj, trial, meta)])) > MAX_SCRIPT
                          or self._reply_estimate([(obj, trial, meta)]) > MAX_REPLY_EST):
                    chunks.append(c)
                    c = []
                c.append(r)
            chunks.append(c)
            for i, ch in enumerate(chunks):
                g = (obj, ch, meta and i == 0)
                trial = cur + [g]
                if cur and (len(self._program(var, trial)) > MAX_SCRIPT
                            or self._reply_estimate(trial) > MAX_REPLY_EST):
                    pages.append(cur)
                    cur = []
                cur.append(g)
        if cur:
            pages.append(cur)
        return pages

    def _parse(self, text: str, groups) -> dict:
        out: dict = {}
        if not text.endswith(END):
            raise ValueError(f"reply cut short at {len(text)} bytes (no terminator)")
        recs = text[:-len(END)].split(RS)
        if recs and recs[-1] == "":
            recs.pop()
        if len(recs) != len(groups):
            raise ValueError(f"{len(recs)} records for {len(groups)} objects")
        for (obj, reads, meta), rec in zip(groups, recs):
            ent = out.setdefault(obj, {})
            if rec == GONE:
                ent["_gone"] = True
                continue
            vals = rec.split(FS)
            if vals and vals[-1] == "":
                vals.pop()
            want = (3 if meta else 0) + len(reads)
            if len(vals) != want:
                raise ValueError(f"{obj}: {len(vals)} values for {want}")
            if meta:
                ent["_class"], ent["_type"], ent["_dir"] = vals[:3]
                vals = vals[3:]
            for r, v in zip(self._order(reads), vals):
                ent[r.key] = v
        return out

    def read(self, var: str, items) -> dict:
        """Run ``items`` [(obj, reads, meta)], bisecting refused pages."""
        result: dict = {}
        todo = self._pages(var, items)
        while todo:
            progs = [self._program(var, g) for g in todo]
            replies = self._eval_many(progs)
            nxt: list = []
            for groups, rep in zip(todo, replies):
                ok = rep.ok
                parsed = None
                if ok:
                    self.stats.max_reply = max(self.stats.max_reply, len(rep.text))
                    try:
                        parsed = self._parse(rep.text, groups)
                    except ValueError as e:
                        self.log(f"  unparseable page ({e}); bisecting")
                        ok = False
                if ok:
                    for o, ent in parsed.items():
                        result.setdefault(o, {}).update(ent)
                    continue
                self.stats.refused_pages += 1
                # Bisect: objects first, then one object's reads.
                if len(groups) > 1:
                    h = len(groups) // 2
                    nxt += [groups[:h], groups[h:]]
                    continue
                obj, reads, meta = groups[0]
                if meta and reads:
                    nxt += [[(obj, [], True)], [(obj, reads, False)]]
                elif len(reads) > 1:
                    h = len(reads) // 2
                    nxt += [[(obj, reads[:h], False)], [(obj, reads[h:], False)]]
                else:
                    ent = result.setdefault(obj, {})
                    if meta:
                        ent["_class"] = ent["_type"] = ent["_dir"] = REFUSED
                    for r in reads:
                        ent[r.key] = REFUSED
                        self.stats.refused_reads += 1
                    err = getattr(rep, "error", "") or ""
                    self.log(f"  refused: {obj} {[r.key for r in reads] or 'meta'}: {err[:160]}")
            todo = nxt
        return result

    # -- the probe ------------------------------------------------------

    def capture_milo(self, milo: str, var: str) -> dict:
        """``milo`` may carry ``!ClassA,ClassB``: objects of those classes keep
        their meta row (so presence and class are still compared) but their
        properties are not read -- used to keep a venue golden small."""
        t0 = time.time()
        milo, _, excl = milo.partition("!")
        exclude = {c for c in excl.split(",") if c}
        d = self.load(milo, var)
        names = self.enumerate(var)
        dupes = sorted({n for n in names if names.count(n) > 1})
        uniq = sorted(set(names))
        bad = [n for n in uniq if _UNSAFE_NAME.search(n)]
        uniq = [n for n in uniq if n not in bad]
        self.log(f"{milo}: dir {d['name']} [{d['class']}], {len(names)} objects"
                 + (f", {len(dupes)} duplicate names" if dupes else ""))

        # Pass 0: meta for everything.
        objects = self.read(var, [(n, [], True) for n in uniq])
        # Pass 1: schema leaves for each object's class (+ type).
        items = []
        leaf_index: dict[str, dict] = {}
        for n in uniq:
            ent = objects.get(n, {})
            cls = ent.get("_class", "")
            if ent.get("_gone") or cls in ("", REFUSED) or cls in exclude:
                continue
            typ = ent.get("_type", "")
            leaves = es.class_leaves(self.schemas, cls, typ if typ not in ("''", "") else "")
            reads = [Read(n, lf.path, "n" if lf.kind == "size" else "v") for lf in leaves]
            leaf_index[n] = {lf.path: lf for lf in leaves}
            if reads:
                items.append((n, reads, False))
        objects_vals = self.read(var, items)
        for n, ent in objects_vals.items():
            objects.setdefault(n, {}).update(ent)
        # Passes 2..depth: array elements.
        pending = {n: [lf for lf in idx.values() if lf.kind == "size"]
                   for n, idx in leaf_index.items()}
        for _depth in range(self.max_depth):
            items = []
            nxt_pending: dict[str, list] = {}
            for n, size_leaves in pending.items():
                reads = []
                for lf in size_leaves:
                    raw = objects.get(n, {}).get(path_key(lf.path) + "/#")
                    try:
                        size = int(raw)
                    except (TypeError, ValueError):
                        continue
                    cap = ELEM_CAPS.get(str(lf.path[-1]), self.max_elems)
                    for i in range(min(size, cap)):
                        for sub in es.element_leaves(lf.elem, lf.path + (i,)):
                            reads.append(Read(n, sub.path, "n" if sub.kind == "size" else "v"))
                            if sub.kind == "size":
                                nxt_pending.setdefault(n, []).append(sub)
                if reads:
                    items.append((n, reads, False))
            if not items:
                break
            vals = self.read(var, items)
            for n, ent in vals.items():
                objects.setdefault(n, {}).update(ent)
            pending = nxt_pending
        self.stats.seconds += time.time() - t0
        return {
            "milo": milo,
            "excluded_classes": sorted(exclude),
            "dir": d,
            "object_count": len(names),
            "duplicate_names": dupes,
            "unsafe_names": bad,
            "objects": objects,
        }


def load_default_schemas(repo_root: Path) -> tuple[dict, str]:
    return es.load_schemas(repo_root / es.DEFAULT_SCHEMA_ROOT)
