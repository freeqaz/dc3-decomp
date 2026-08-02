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
import sys
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass, field
from typing import Any, Iterable

# Sentinel returned for DTA string values the engine could not produce.
ABSENT = "<absent>"
NULL_OBJ = "<null>"


class TransportError(RuntimeError):
    """Raised when the transport itself fails (connection, timeout, protocol)."""


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

    def roster(self, dir_name: str = "main", classes: Iterable[str] | None = None) -> list[ObjRef]:
        """Enumerate objects in ``dir_name``.

        Uses DTA ``iterate``, the portable enumeration primitive: it works on
        the original binary and — since the ``ObjectDir::Iterate`` type-filter
        fix (4e4cf851) — natively too. Verified live on both.

        This is also the SAFER path, because ``iterate`` filters by class
        inside the engine and therefore never messages the DTA script objects
        that :data:`probe.DANGEROUS_CLASSES` exists to avoid.
        :class:`NativeHttpTarget` still overrides it with ``/api/objects``
        purely because that is one request instead of one per class.
        """
        out: list[ObjRef] = []
        for cls in classes or ["Object"]:
            expr = (
                '{set $s ""}'
                f'{{{{object "{dir_name}"}} iterate {cls} $o '
                '{strcat $s {$o name} "\\n"}}'
                "{symbol $s}"
            )
            res = self.eval_dta(expr)
            if not res.ok:
                continue
            for nm in res.text.split("\n"):
                if nm:
                    out.append(ObjRef(name=nm, type=cls, dir=dir_name))
        return out


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

    def roster(self, dir_name: str = "main", classes: Iterable[str] | None = None) -> list[ObjRef]:
        """Enumerate via /api/objects (DTA ``iterate`` is broken natively)."""
        q = "?recurse=true"
        if dir_name and dir_name != "main":
            q = f"?dir={urllib.parse.quote(dir_name)}&recurse=true"
        payload = self._get(f"/api/objects{q}")
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

    def roster(self, dir_name: str = "main", classes: Iterable[str] | None = None) -> list[ObjRef]:
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
