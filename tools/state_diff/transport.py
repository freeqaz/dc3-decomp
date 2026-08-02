"""Transports for state-diff capture targets.

A *target* is anything that can evaluate a DTA expression and hand back the
resulting scalar. The native port speaks HTTP (see docs/tools/HTTP_DEBUG_SERVER.md);
real-hardware targets (RB3Enhanced, the DC3 debug XEX) speak whatever their
sibling tooling implements. Everything above this module is transport-agnostic:
probes compile to DTA text, and the differ consumes normalized snapshots.

Console targets do NOT need a bespoke subclass: :class:`ConsoleTarget` adapts
anything exposing ``eval(script)`` / ``eval_batch(scripts)``, which is the
interface shared by ``tools/console/dc3_eval.py`` (this repo: HTTP, file and
app-child transports) and ``tools/rb3e_dta.py`` in RB3Enhanced. That pair is
the single seam between this tool and real hardware.
"""

from __future__ import annotations

import abc
import json
import re
import sys
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass, field
from typing import Any, Iterable

from .budget import brace_balance

# Sentinel returned for DTA string values the engine could not produce.
ABSENT = "<absent>"
NULL_OBJ = "<null>"


class TransportError(RuntimeError):
    """Raised when the transport itself fails (connection, timeout, protocol)."""


# --------------------------------------------------------------------------
# Directory scopes
# --------------------------------------------------------------------------
#
# `main` (ObjectDir::Main()) holds the ~687 globals and NOTHING that lives in a
# loaded .milo panel dir: ObjDirItr's recursion does not cross into a PanelDir
# hung off a Panel, so `/api/objects?dir=main&recurse=true` cannot see
# `motd.lbl` or its siblings. Measured on dc3-native at main_screen:
#
#     {main iterate Cam ...}                     -> 5   [tex proc cam] ... [ui.cam]
#     {{main_panel loaded_dir} iterate Cam ...}  -> 2   camera1.cam, turbo_shell.cam
#
# The two sets are disjoint. Menu screens are where the highest-value UI bugs
# live, so a scope has to be able to name the panel dir directly.

#: A bare directory name: a symbol we hand straight to `find_obj` / `iterate`.
_BARE_DIR = re.compile(r"^[A-Za-z_][A-Za-z0-9_.+\-]*$")


class DirSpecError(ValueError):
    """Raised when a --dir spec cannot be turned into a safe DTA expression."""


def dir_expr(spec: str | None) -> str:
    """Compile a scope/``--dir`` spec into a DTA expression yielding an ObjectDir.

    Three forms, in increasing order of escape-hatch-ness::

        main                    -> main                        (the global dir)
        panel:main_panel        -> {main_panel loaded_dir}      (sugar)
        {main_panel loaded_dir} -> verbatim                     (any expression)

    Verified live: ``find_obj`` and ``iterate`` both accept a *variable holding
    a dir* as well as a bare symbol, so the compiled probe binds the expression
    once per page (``{do ($d <expr>) ... {find_obj $d "name"} ...}``) instead of
    re-evaluating it per object. That keeps the emitted script the same size for
    a panel dir as for ``main``, which is what preserves the paging budgets.

    The verbatim form is an *author* escape hatch, not a user-input surface:
    it is brace-validated here and the resulting page still goes through
    :func:`budget.validate_script`, but a caller who passes a bogus object name
    inside it will fault the title exactly as any other bad DTA would. Prefer
    ``panel:`` where it fits.
    """
    s = (spec or "main").strip()
    if not s:
        return "main"
    if s.startswith("panel:"):
        name = s[len("panel:"):].strip()
        if not _BARE_DIR.match(name):
            raise DirSpecError(
                f"panel name {name!r} is not a bare symbol; use the explicit "
                "'{<panel> loaded_dir}' form if you really mean it"
            )
        return "{%s loaded_dir}" % name
    if s.startswith("{"):
        ok, why = brace_balance(s)
        if not ok:
            raise DirSpecError(f"dir expression {s!r} is unbalanced DTA ({why})")
        return s
    if not _BARE_DIR.match(s):
        raise DirSpecError(
            f"dir spec {s!r} is neither a bare symbol, a 'panel:<name>' sugar, "
            "nor a '{...}' DTA expression"
        )
    return s


def is_main_dir(spec: str | None) -> bool:
    """True when the spec is the plain global dir (the fast /api/objects path)."""
    return (spec or "main").strip() in ("", "main")


#: Separators for the DTA roster payload. ``|`` splits name from class, ``;``
#: splits entries. Object names in DC3 contain brackets and spaces (`[ui.cam]`,
#: `[default lit]`) but not these two, and an entry that does not split cleanly
#: is reported rather than silently dropped.
ROSTER_KV = "|"
ROSTER_SEP = ";"

#: Entries per roster request: 250 x ~40B ~= 10 KB, comfortably inside the
#: 32 KB reply cap's 80% headroom.
ROSTER_PAGE = 250

#: Prefix of the count header the object_list roster emits as its first entry.
ROSTER_COUNT = "#"

#: C++ class names that look plausible in a probe spec and enumerate ZERO,
#: because the class filter resolves through the shipped DTA `objects`
#: superclass graph rather than the C++ type system. Measured live 2026-08-02:
#: RndDrawable -> 0 where Draw -> 45; RndGroup -> 0 where Group -> 7. The
#: full vocabulary lives in probes/dta_classes.json.
CPP_ALIASES = {
    "RndDrawable": "Draw", "RndTransformable": "Trans", "RndGroup": "Group",
    "RndMesh": "Mesh", "RndMat": "Mat", "RndTex": "Tex", "RndCam": "Cam",
    "RndLight": "Light", "RndEnviron": "Environ", "RndAnimatable": "Anim",
    "RndFont": "Font", "RndText": "Text", "RndMovie": "Movie",
    "RndParticleSys": "ParticleSys", "RndPollable": "Poll",
    "Hmx::Object": "Object",
}


def _zero_class_hint(dir_name: str, cls: str, method: str) -> str:
    """Explain a zero-result class filter loudly.

    A wrong class name fails as an EMPTY result, which is indistinguishable
    from "no such objects exist" — so a silent zero would make a state diff
    look clean while it was actually blind. Never record one silently.
    """
    fix = CPP_ALIASES.get(cls)
    if fix is None and cls.startswith(("Rnd", "Hmx")):
        fix = cls[3:] or None
    tail = (f" {cls!r} is a C++ class name; the DTA name is {fix!r}."
            if fix else
            " If that is unexpected, check the name against "
            "probes/dta_classes.json: filters resolve through the shipped "
            "`objects` superclass graph (DTA names), not C++ class names.")
    return (f"roster {dir_name}: class filter {cls!r} ({method}) enumerated "
            f"ZERO objects.{tail}")


@dataclass
class EvalResult:
    """Result of one DTA evaluation.

    ``ok`` False means the engine refused or crashed evaluating the expression.
    The engine survives DTA SIGSEGV (HttpServer installs handlers around eval),
    so a failed eval is recoverable: callers should record the failure for that
    record and continue rather than aborting the capture.
    """

    ok: bool
    type: str | None = None
    value: Any = None
    error: str | None = None

    @property
    def text(self) -> str:
        """Value coerced to str; '' when the eval failed."""
        if not self.ok or self.value is None:
            return ""
        return str(self.value)


@dataclass
class ObjRef:
    """One entry in a target's object roster."""

    name: str
    type: str
    dir: str | None = None
    extra: dict = field(default_factory=dict)


def decode_node(data: dict) -> dict:
    """Normalize one serialized DataNode into ``{type, value}``.

    The native endpoint serializes every DataType with a name, a numeric
    ``typeId`` and a payload (native/src/platform/DtaEvalSupport.cpp). Two
    cases need real handling rather than a raw ``data["value"]``:

    * **Non-finite floats.** JSON cannot represent NaN/Inf, so they arrive as
      ``"value": null`` plus ``"special": "nan" | "inf" | "-inf"``. Reading
      ``value`` alone would silently turn a NaN transform — exactly the bug
      worth finding — into a null. (Before this was fixed the endpoint emitted
      bare ``nan``/``inf``, which is invalid JSON and would have crashed the
      parser outright.) They are mapped to the strings the normalizer already
      uses so they survive into the snapshot and diff as real values.
    * **Non-UTF-8 strings and globs**, which arrive base64 with an explicit
      ``"encoding"`` field.
    """
    special = data.get("special")
    if special is not None:
        return {"type": str(data.get("type")), "value": {
            "nan": "NaN", "inf": "Inf", "-inf": "-Inf"}.get(special, special)}

    value = data.get("value")
    if data.get("encoding") == "base64" and isinstance(value, str):
        import base64
        try:
            value = base64.b64decode(value).decode("utf-8", "replace")
        except Exception:  # noqa: BLE001 - keep the raw payload if it is not text
            pass
    return {"type": str(data.get("type")), "value": value}


class Target(abc.ABC):
    """Abstract capture target.

    Subclasses MUST implement :meth:`eval_dta`. Everything else has a default
    built on top of DTA evaluation, so a console client that can only do
    "send DTA text, get scalar back" is a complete target.
    """

    #: Short identifier recorded in snapshot metadata ("native", "rb3e", "xex").
    name: str = "target"

    # ---- required -------------------------------------------------------

    @abc.abstractmethod
    def eval_dta(self, expr: str, timeout: float = 15.0) -> EvalResult:
        """Evaluate a DTA expression and return the final node's value.

        The expression is a sequence of DTA commands, e.g.::

            {set $s ""}{set $o {object "[ui.cam]"}}{strcat $s {$o name}}{symbol $s}

        Only the LAST command's value is returned. Implementations must map the
        engine's DataNode types to ``EvalResult.type`` using these names:
        ``int``, ``float``, ``symbol``, ``object``, ``string``. Probe payloads
        arrive as ``string`` (kDataString, typeId 18); see :func:`decode_node`
        for the non-finite-float and base64 cases.
        """

    # ---- optional overrides --------------------------------------------

    def eval_batch(self, exprs: list[str], timeout: float = 30.0) -> list["EvalResult"]:
        """Evaluate several scripts in ONE round trip where the transport can.

        The console clients expose ``eval_batch`` precisely so a whole probe
        can be a single request — which matters most on the DC3 file transport,
        where one round trip is also one human keypress. The default falls back
        to sequential evaluation.

        Implementations MUST return exactly one result per input, in order, or
        raise. Returning a short list would silently shift every later result
        onto the wrong object and produce a confident, completely wrong
        divergence report.
        """
        return [self.eval_dta(e, timeout=timeout) for e in exprs]

    def health(self) -> bool:
        """True when the target is alive and able to evaluate."""
        try:
            return self.eval_dta("{+ 1 1}", timeout=5.0).value == 2
        except TransportError:
            return False

    def describe(self) -> dict:
        """Metadata recorded in the snapshot header (build id, version, ...).

        Keep this free of run-varying values (uptime, frame) — those belong in
        the volatile section that the normalizer elides.
        """
        return {"target": self.name}

    def roster(
        self,
        dir_name: str = "main",
        classes: Iterable[str] | None = None,
        *,
        isa: Iterable[str] | None = None,
        limit: int = 0,
        recurse: bool = True,
        page: int = ROSTER_PAGE,
        method: str = "auto",
        errors: list[str] | None = None,
    ) -> list[ObjRef]:
        """Enumerate objects in the dir named by ``dir_name`` (a --dir spec).

        Two portable primitives, both verified live against
        ``{main_panel loaded_dir}`` on dc3-native headless, 2026-08-02, and
        cross-checked against each other (identical counts for every class):

        ``object_list`` *(default when recursing)*
            ``{object_list $d <Class> FALSE}`` returns a **sorted DataArray of
            name strings** (``Utl.cpp:289``), always recursive, indexable with
            ``{elem $a $i}``. That gives a real cursor, so paging is
            ``{foreach_int $i lo {min hi {size $a}} ...}`` — deterministic
            order for free, which the snapshot format wants anyway.
            *(It used to SIGSEGV. The bug was never in ``object_list``: it was
            an LP64 stride bug in ``DataArray::SortNodes``, which hardcoded 8
            as the qsort element size — ``sizeof(DataNode)`` on PPC32 but 16 on
            LP64 — so ``ObjectList``'s closing ``SortNodes(0)`` strided over
            half-nodes. Fixed in 8c73183d.)*

        ``iterate`` *(used for ``recurse=False``, and as an explicit fallback)*
            ``{$d iterate_self <Class> $o {...}}`` is the only way to enumerate
            **this dir only**; ``object_list`` has no non-recursive mode.
            Paging is an ordinal window because ``iterate`` has no cursor.

        Both are SAFE against the DTA script objects that
        :data:`probe.DANGEROUS_CLASSES` exists to avoid: the class filter runs
        inside the engine, and the only message sent per object is
        ``class_name``, which ``Hmx::Object::Handle`` answers itself
        (Object.cpp:144) *before* the ``HANDLE_ARRAY(mTypeDef)`` fallthrough at
        :159 that would execute game script.

        **Class names must be DTA names, not C++ names.** Filtering resolves
        through the shipped ``objects`` superclass graph
        (``IsASubclass`` -> ``SystemConfig("objects", child)``), which is keyed
        by DTA names. ``RndDrawable`` and ``RndGroup`` return **0** where
        ``Draw`` returns 45 and ``Group`` returns 7 — and a wrong name fails
        *silently as an empty result*, which is indistinguishable from "no such
        objects exist" and would make a state diff look clean while blind. Any
        class that enumerates zero is therefore reported through ``errors``,
        and :func:`probe.validate_classes` checks names against the shipped
        ``probes/dta_classes.json`` before a capture starts.

        ``isa`` and ``classes`` do NOT mean the same thing and must not be
        conflated. ``isa`` is the probe's SUBCLASS gate and is exactly what
        both primitives implement engine-side, so it is the enumeration filter
        here. ``classes`` is an EXACT post-filter applied later by
        :meth:`probe.Scope.select`; a target that enumerates by exact class
        (``/api/objects``, :class:`ReplayTarget`) applies it directly and
        ignores ``isa``.
        """
        dexpr = dir_expr(dir_name)
        if method == "auto":
            method = "object_list" if recurse else "iterate"
        if method not in ("object_list", "iterate"):
            raise DirSpecError(
                f"unknown enumeration method {method!r}; expected auto | "
                "object_list | iterate")
        if method == "object_list" and not recurse:
            raise DirSpecError(
                "object_list is always recursive (Utl.cpp:292 walks with "
                "ObjDirItr(dir, true)); use --enumerate iterate for --no-recurse")

        out: list[ObjRef] = []
        seen: set[str] = set()
        for cls in list(isa or classes or ["Object"]):
            if not _BARE_DIR.match(cls):
                raise DirSpecError(f"roster class {cls!r} is not a bare symbol")
            before = len(out)
            if method == "object_list":
                self._roster_object_list(dexpr, dir_name, cls, page, limit,
                                         out, seen, errors)
            else:
                self._roster_iterate(dexpr, dir_name, cls, page, limit, recurse,
                                     out, seen, errors)
            if len(out) == before and errors is not None:
                errors.append(_zero_class_hint(dir_name, cls, method))
        return out

    # -- roster back ends -------------------------------------------------

    def _entry(self, entry: str, dir_name: str, cls: str,
               out: list[ObjRef], seen: set[str],
               errors: list[str] | None) -> None:
        name, sep, klass = entry.rpartition(ROSTER_KV)
        if not sep:
            if errors is not None:
                errors.append(
                    f"roster {dir_name}/{cls}: entry {entry!r} has no "
                    f"{ROSTER_KV!r} separator; skipped")
            return
        if name in seen:
            return
        seen.add(name)
        out.append(ObjRef(name=name, type=klass, dir=dir_name))

    def _roster_object_list(self, dexpr, dir_name, cls, page, limit,
                            out, seen, errors) -> None:
        """Cursor paging over the sorted name array ``object_list`` returns."""
        lo, total = 0, None
        while total is None or lo < total:
            hi = lo + page
            expr = (
                '{do ($s "") ($d %s) ($a {object_list $d %s FALSE})'
                '{strcat $s "%s" {sprintf "%%d" {size $a}} "%s"}'
                "{foreach_int $i %d {min %d {size $a}}"
                ' {strcat $s {elem $a $i} "%s"'
                ' {{find_obj $d {elem $a $i}} class_name} "%s"}}'
                "$s}"
            ) % (dexpr, cls, ROSTER_COUNT, ROSTER_SEP, lo, hi,
                 ROSTER_KV, ROSTER_SEP)
            res = self.eval_dta(expr, timeout=30.0)
            if not res.ok:
                if errors is not None:
                    errors.append(f"roster {dir_name}/{cls}: {res.error}")
                return
            for entry in res.text.split(ROSTER_SEP):
                entry = entry.strip()
                if not entry:
                    continue
                if entry.startswith(ROSTER_COUNT):
                    try:
                        total = int(entry[len(ROSTER_COUNT):])
                    except ValueError:
                        if errors is not None:
                            errors.append(
                                f"roster {dir_name}/{cls}: bad count header "
                                f"{entry!r}; refusing to page blind")
                        return
                    continue
                self._entry(entry, dir_name, cls, out, seen, errors)
            if total is None:
                if errors is not None:
                    errors.append(
                        f"roster {dir_name}/{cls}: reply carried no count "
                        "header; refusing to page blind")
                return
            lo = hi
            if limit and len(out) >= limit:
                return

    def _roster_iterate(self, dexpr, dir_name, cls, page, limit, recurse,
                        out, seen, errors) -> None:
        """Ordinal-window paging: ``iterate`` has no cursor, so each request
        re-walks the dir and emits only ordinals in ``[lo, lo+page)``."""
        verb = "iterate" if recurse else "iterate_self"
        lo = 0
        while True:
            hi = lo + page
            expr = (
                '{do ($s "") ($n 0) ($d %s)'
                "{$d %s %s $o"
                '{if {&& {>= $n %d} {< $n %d}}'
                ' {strcat $s {$o name} "%s" {$o class_name} "%s"}}'
                "{set $n {+ $n 1}}}"
                "$s}"
            ) % (dexpr, verb, cls, lo, hi, ROSTER_KV, ROSTER_SEP)
            res = self.eval_dta(expr, timeout=30.0)
            if not res.ok:
                if errors is not None:
                    errors.append(f"roster {dir_name}/{cls}: {res.error}")
                return
            got = 0
            for entry in res.text.split(ROSTER_SEP):
                entry = entry.strip()
                if not entry:
                    continue
                got += 1
                self._entry(entry, dir_name, cls, out, seen, errors)
            if got < page:
                return
            lo = hi
            if limit and len(out) >= limit:
                return


class NativeHttpTarget(Target):
    """The native port's embedded HTTP debug server."""

    name = "native"

    #: 127.0.0.1, not "localhost": the engine's cpp-httplib server binds IPv4
    #: only, and on hosts where `localhost` resolves to ::1 first every request
    #: fails with ECONNREFUSED even though curl (which falls back) works.
    DEFAULT_BASE = "http://127.0.0.1:9090"

    def __init__(self, base: str = DEFAULT_BASE, timeout: float = 15.0):
        self.base = base.rstrip("/")
        self.timeout = timeout

    def _get(self, path: str, timeout: float | None = None) -> Any:
        url = f"{self.base}{path}"
        try:
            with urllib.request.urlopen(url, timeout=timeout or self.timeout) as r:
                return json.loads(r.read().decode("utf-8", "replace"))
        except (urllib.error.URLError, OSError, json.JSONDecodeError) as e:
            raise TransportError(f"GET {url}: {e}") from e

    def eval_dta(self, expr: str, timeout: float = 15.0) -> EvalResult:
        url = f"{self.base}/api/dta/eval"
        req = urllib.request.Request(url, data=expr.encode("utf-8"), method="POST")
        try:
            with urllib.request.urlopen(req, timeout=timeout) as r:
                body = r.read().decode("utf-8", "replace")
        except urllib.error.HTTPError as e:
            # The engine reports DTA failures as a 4xx/5xx with a JSON body.
            # Surface that as an eval failure (recoverable, recorded per
            # record) rather than a transport failure (aborts the page) —
            # otherwise a single bad property reads as "the target went away".
            raw = e.read().decode("utf-8", "replace") if e.fp else ""
            try:
                payload = json.loads(raw)
                return EvalResult(ok=False, error=str(payload.get("error", raw[:200])))
            except json.JSONDecodeError:
                if e.code == 413:
                    raise TransportError(
                        f"POST {url}: 413 Payload Too Large ({len(expr)} bytes). "
                        "Lower --script-cap."
                    ) from e
                return EvalResult(ok=False, error=f"HTTP {e.code}: {raw[:200]}")
        except (urllib.error.URLError, OSError) as e:
            raise TransportError(f"POST {url}: {e}") from e
        try:
            payload = json.loads(body)
        except json.JSONDecodeError as e:
            raise TransportError(f"POST {url}: bad JSON: {e}") from e

        if not payload.get("ok"):
            return EvalResult(ok=False, error=str(payload.get("error", "unknown")))
        return EvalResult(ok=True, **decode_node(payload.get("data") or {}))

    def health(self) -> bool:
        try:
            return bool(self._get("/api/health", timeout=5.0).get("ok"))
        except TransportError:
            return False

    def describe(self) -> dict:
        return {"target": self.name, "base": self.base}

    def volatile(self) -> dict:
        """Run-varying values, recorded but elided by the normalizer."""
        try:
            d = self._get("/api/health", timeout=5.0).get("data", {})
            return {"frame": d.get("frame"), "uptime_s": d.get("uptime_s")}
        except TransportError:
            return {}

    def current_screen(self) -> str | None:
        try:
            return self._get("/api/screen", timeout=5.0).get("data", {}).get("screen")
        except TransportError:
            return None

    def screenshot(self, timeout: float = 60.0) -> bytes:
        """Grab the current framebuffer as PNG bytes.

        **This works under ``MILO_HEADLESS=1`` on a box with no display and no
        GPU** — verified live, 1280x720 RGBA PNG, ~750 KB. The capture is
        queued and executed on the main thread after ``EndDrawing()``
        (HttpServer.cpp:847), so the image is a fully rendered frame and the
        call also doubles as a "advance to the next presented frame" primitive.
        """
        url = f"{self.base}/api/screenshot"
        try:
            with urllib.request.urlopen(url, timeout=timeout) as r:
                data = r.read()
        except (urllib.error.URLError, OSError) as e:
            raise TransportError(f"GET {url}: {e}") from e
        if not data.startswith(b"\x89PNG"):
            raise TransportError(
                f"GET {url}: reply is not a PNG ({data[:80]!r})"
            )
        return data

    def roster(self, dir_name: str = "main", classes: Iterable[str] | None = None,
               **kw) -> list[ObjRef]:
        """Enumerate via /api/objects for ``main``, via DTA ``iterate`` elsewhere.

        ``/api/objects`` is one request instead of one per class, so it stays
        the default for the global dir. It cannot serve a panel dir, though:
        its ``dir=`` parameter resolves the name through
        ``ObjectDir::Main()->FindObject`` (HttpServer.cpp:560), and a PanelDir
        reached via ``{<panel> loaded_dir}`` is not registered there — and even
        when it is findable, the name collides (`main_panel`'s loaded dir is
        itself *named* "main"). Panel scopes therefore take the portable
        ``iterate`` path, which is also what a console target will use, so both
        sides of a diff enumerate identically.
        """
        if not is_main_dir(dir_name):
            return super().roster(dir_name, classes, **kw)
        payload = self._get("/api/objects?recurse=true")
        data = payload.get("data", payload)
        if isinstance(data, dict):
            data = data.get("objects", [])
        refs = [
            ObjRef(name=o.get("name", ""), type=o.get("type", ""), dir=dir_name)
            for o in data
            if o.get("name")
        ]
        if classes:
            want = set(classes)
            refs = [r for r in refs if r.type in want]
        return refs


class ConsoleTarget(Target):
    """Adapter over the landed console DTA-eval clients.

    Wraps any object exposing ``eval(script) -> str`` and
    ``eval_batch(scripts) -> list[str]`` — the interface deliberately shared by

    * ``tools/console/dc3_eval.py`` in this repo (HttpTransport, FileTransport,
      AppChildTransport), and
    * ``tools/rb3e_dta.py`` in RB3Enhanced (branch feature/dta-eval-channel).

    so RB3-over-HTTP, DC3-over-file and DC3-over-HTTP all reach the differ
    through one seam.

    Response-parsing hazards this adapter is responsible for NOT laundering
    into a snapshot:

    * The reply is captured print output interleaved with ``=> <value>``
      markers, not one clean line per command. The clients own that parsing.
    * **A refused batch element emits no marker**, shifting the index of every
      later command. The clients refuse to attribute when marker count and
      command count disagree; :meth:`eval_batch` here turns that refusal into
      per-request failures rather than storing mis-attributed values.
    * Truncation is a sentinel sentence in the body and a parse error comes back
      as **HTTP 200** with body ``!! parse error``. Both raise in the client.
      A raised error means CAPTURE FAILED — never "this object has empty state".
    """

    name = "console"

    def __init__(self, client, name: str = "console", meta: dict | None = None):
        if not (hasattr(client, "eval") and hasattr(client, "eval_batch")):
            raise TypeError(
                "ConsoleTarget needs a client exposing eval()/eval_batch(); "
                "see tools/console/dc3_eval.py"
            )
        self.client = client
        self.name = name
        self._meta = meta or {}

    # Exceptions the clients raise. Imported lazily so this module stays
    # importable without the console tooling on the path.
    @staticmethod
    def _client_errors():
        errs: list[type] = [ConnectionError, OSError]
        try:
            import dc3_eval  # type: ignore
            errs += [dc3_eval.ConsoleError, dc3_eval.DtaError]
        except Exception:  # noqa: BLE001 - optional dependency
            pass
        return tuple(errs)

    def _wrap(self, text: str) -> EvalResult:
        # Probes always end in {symbol ...}, so the payload is a string.
        return EvalResult(ok=True, type="symbol", value=text)

    def eval_dta(self, expr: str, timeout: float = 15.0) -> EvalResult:
        try:
            return self._wrap(self.client.eval(expr))
        except self._client_errors() as e:
            # Distinguish "the console refused this script" (recoverable,
            # recorded against the record) from "the link is gone" (abort).
            if isinstance(e, (ConnectionError, OSError)):
                raise TransportError(f"{self.name}: {e}") from e
            return EvalResult(ok=False, error=f"{type(e).__name__}: {e}")

    def eval_batch(self, exprs: list[str], timeout: float = 30.0) -> list[EvalResult]:
        if not exprs:
            return []
        try:
            out = self.client.eval_batch(list(exprs))
        except self._client_errors() as e:
            if isinstance(e, (ConnectionError, OSError)):
                raise TransportError(f"{self.name}: {e}") from e
            return [EvalResult(ok=False, error=f"{type(e).__name__}: {e}")
                    for _ in exprs]
        if len(out) != len(exprs):
            # The client could not line results up with commands. Attributing
            # them anyway is the single worst failure mode this tool has, so
            # fail the whole batch loudly instead.
            return [
                EvalResult(
                    ok=False,
                    error=(f"batch attribution refused: {len(out)} result(s) for "
                           f"{len(exprs)} command(s); values cannot be matched "
                           "to objects"),
                )
                for _ in exprs
            ]
        return [self._wrap(t) for t in out]

    def describe(self) -> dict:
        return {"target": self.name, **self._meta}


def console_target(spec: str = "", **kwargs) -> ConsoleTarget:
    """Build a :class:`ConsoleTarget` from ``tools/console/dc3_eval.py``.

    ``spec`` is ``host[:port]`` for the HTTP transport. Other transports
    (file, app-child) should be constructed directly and passed to
    :class:`ConsoleTarget`.
    """
    import importlib.util
    from pathlib import Path

    mod_path = Path(__file__).resolve().parents[1] / "console" / "dc3_eval.py"
    if not mod_path.exists():
        raise NotImplementedError(f"console client not found at {mod_path}")
    spec_ = importlib.util.spec_from_file_location("dc3_eval", mod_path)
    mod = importlib.util.module_from_spec(spec_)
    sys.modules.setdefault("dc3_eval", mod)
    spec_.loader.exec_module(mod)  # type: ignore[union-attr]

    host, _, port = spec.partition(":")
    client = mod.HttpTransport(host or "127.0.0.1",
                               int(port) if port else mod.DEFAULT_PORT,
                               **kwargs)
    return ConsoleTarget(client, name="console", meta={"host": host, "port": port})


class ReplayTarget(Target):
    """Replays recorded eval responses. Used by the unit tests and for
    offline re-normalization of a raw capture."""

    name = "replay"

    def __init__(self, responses: dict[str, dict], roster_data: list[dict] | None = None,
                 meta: dict | None = None):
        self._responses = responses
        self._roster = [ObjRef(**r) for r in (roster_data or [])]
        self._meta = meta or {"target": "replay"}

    def eval_dta(self, expr: str, timeout: float = 15.0) -> EvalResult:
        rec = self._responses.get(expr)
        if rec is None:
            return EvalResult(ok=False, error="no recorded response")
        return EvalResult(**rec)

    def describe(self) -> dict:
        return dict(self._meta)

    def roster(self, dir_name: str = "main", classes: Iterable[str] | None = None,
               **kw) -> list[ObjRef]:
        refs = self._roster
        if classes:
            want = set(classes)
            refs = [r for r in refs if r.type in want]
        return refs



def make_target(spec: str, **kwargs) -> Target:
    """Build a target from a CLI spec string.

    ``native`` or ``native:http://host:port`` -> :class:`NativeHttpTarget`
    ``console`` -> :class:`ConsoleTarget` (stub, raises)
    """
    if spec == "native":
        return NativeHttpTarget(**kwargs)
    if spec.startswith("native:"):
        return NativeHttpTarget(base=spec.split(":", 1)[1], **kwargs)
    if spec == "console" or spec.startswith("console:"):
        return console_target(spec.partition(":")[2], **kwargs)
    raise ValueError(f"unknown target spec: {spec!r}")
