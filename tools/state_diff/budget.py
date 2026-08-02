"""Request budget: the caps every probe must live within.

The native port's HTTP debug endpoint has no meaningful limits, so it is very
easy to build a probe that works perfectly on localhost and is unusable on real
hardware. Probes are therefore *paged by construction* and validated against
the tightest transport we intend to support, so what runs on localhost runs on
a console.

Known console transports (RB3Enhanced, port 21070, hand-rolled
``source/net_http_server.c`` — NOT civetweb):

``POST /dta/eval`` — **the default**. Script is the RAW request body, so there
    is no URL-encoding tax: **16 KB in, 32 KB out**, and truncation is always
    reported explicitly rather than silently. A top-level *array* of commands
    executes each in turn and returns one result line per command.

``GET /execute?script=`` — **legacy fallback**. The binding limit is
    ``request_path[250]`` and the script arrives URL-ENCODED, so every ``{``
    costs 3 bytes as ``%7B``. On a brace-heavy DTA script the real usable
    budget is roughly **80-150 characters** — far too small for anything but a
    single scalar read. Use :meth:`Limits.legacy_get` only to prove a probe
    degrades gracefully.

Hard rules that hold on every transport:

* ``DataReadString`` **faults on unbalanced braces** (MILO_FAIL, C++ EH, not
  catchable from the server's C code), so scripts are validated before they are
  ever sent, regardless of whether caps are enforced.
* **Input is trusted.** A *balanced* script naming a nonexistent object or
  using preprocessor syntax still faults the title. Probes therefore never
  guess names: every property read is gated on a name that was enumerated in
  this session (see :func:`probe.assert_enumerated`).
* **``print`` produces no output on a retail console.** In retail TU5
  ``Debug::Print`` compiled to a bare ``blr`` and was ICF-folded onto an empty
  stub, and ``TextStream::operator<<`` dispatches through that vtable slot, so
  every ``TheDebug << ...`` is a no-op. Probes must carry their payload in the
  command's RETURN VALUE, never in a print side effect. Native's ``print``
  works, so a probe that relies on it looks fine locally and returns nothing on
  hardware.

The caps are parameters, not constants: switching transports is a config
change (``--script-cap`` / ``--result-cap`` / ``--transport``), not a rewrite.

By default the native capture driver enforces the console caps, so a native-only
run still proves console viability. ``--no-console-caps`` opts out.
"""

from __future__ import annotations

from dataclasses import dataclass


class BudgetError(ValueError):
    """Raised at author/compile time when a probe cannot fit the caps."""


@dataclass(frozen=True)
class Limits:
    """Transport caps. ``enforced=False`` disables checking (native-only runs)."""

    #: Max DTA script bytes per request.
    max_script: int = 16383
    #: Max reply bytes per request.
    max_result: int = 32768
    #: Require exactly one top-level DTA command per request.
    one_command: bool = True
    enforced: bool = True
    name: str = "portable"

    @staticmethod
    def portable() -> "Limits":
        """Intersection of every transport we intend to support. THE DEFAULT.

        Both sides now cap the script at RB3E_DTA_SCRIPT_MAX (16384) and the
        reply at RB3E_DTA_OUTPUT_MAX (32768) — native/src/platform/
        DtaEvalSupport.h:27 deliberately mirrors the console constants.

        **The boundary differs by one byte, and that matters.** The console
        rejects a body of *exactly* 16384 (``len(raw) >= limit``,
        tools/console/dc3_eval.py:160), while the native endpoint accepts
        16384 and rejects 16385 (verified live: 16384 OK, 16385 -> 413). So the
        portable rule is ``< 16384``, i.e. a max of **16383**. A page sized to
        16384 passes every local test and then fails exactly once, confusingly,
        on hardware — which is precisely the class of bug this profile exists
        to prevent.

        (Historical note for anyone re-deriving this: the native endpoint used
        to reject at 8192. That was never an intentional limit — it was
        cpp-httplib's CPPHTTPLIB_FORM_URL_ENCODED_PAYLOAD_MAX_LENGTH firing on
        url-encoded bodies and returning a bodyless 413 before the handler ran.
        Fixed in 26cc0088; do not re-introduce 8192 anywhere.)
        """
        return Limits(16383, 32768, True, True, "portable")

    @staticmethod
    def native_http() -> "Limits":
        """The native port's endpoint: accepts 16384, rejects 16385."""
        return Limits(16384, 32768, True, True, "native_http")

    @staticmethod
    def post_eval() -> "Limits":
        """RB3Enhanced ``POST /dta/eval``: rejects a body of exactly 16384."""
        return Limits(16383, 32768, True, True, "post_eval")

    @staticmethod
    def legacy_get() -> "Limits":
        """RB3Enhanced ``GET /execute?script=``: ~120 usable chars after URL
        encoding (request_path[250], ``{`` costs 3 bytes as ``%7B``)."""
        return Limits(120, 1023, True, True, "legacy_get")

    @staticmethod
    def unlimited() -> "Limits":
        """Native HTTP endpoint: no meaningful caps. Brace validation still runs."""
        return Limits(1 << 30, 1 << 30, False, False, "unlimited")

    @staticmethod
    def named(name: str) -> "Limits":
        try:
            return {"portable": Limits.portable,
                    "native_http": Limits.native_http,
                    "post_eval": Limits.post_eval,
                    "legacy_get": Limits.legacy_get,
                    "unlimited": Limits.unlimited}[name]()
        except KeyError:
            raise ValueError(
                f"unknown transport profile {name!r}; expected portable | "
                "native_http | post_eval | legacy_get | unlimited"
            ) from None

    @property
    def result_headroom(self) -> int:
        """Bytes of reply we actually plan for.

        Kept below ``max_result`` so a slightly-wider-than-predicted value
        (a long object name, a -0.0001234 float) cannot silently truncate.
        """
        return max(64, int(self.max_result * 0.80))


def brace_balance(text: str) -> tuple[bool, str]:
    """Check ``{} () []`` balance, ignoring bracket chars inside DTA strings.

    ``DataReadString`` faults on unbalanced input, so this runs on every script
    before it leaves the process.
    """
    pairs = {"}": "{", ")": "(", "]": "["}
    stack: list[str] = []
    in_str = False
    i = 0
    while i < len(text):
        c = text[i]
        if in_str:
            if c == "\\":
                i += 2
                continue
            if c == '"':
                in_str = False
        elif c == '"':
            in_str = True
        elif c in "{([":
            stack.append(c)
        elif c in "})]":
            if not stack:
                return False, f"unmatched {c!r} at offset {i}"
            if stack[-1] != pairs[c]:
                return False, f"{c!r} at offset {i} closes {stack[-1]!r}"
            stack.pop()
        i += 1
    if in_str:
        return False, "unterminated string literal"
    if stack:
        return False, f"unclosed {stack[-1]!r} ({len(stack)} open)"
    return True, ""


def count_top_level_commands(text: str) -> int:
    """Number of top-level ``{...}`` groups."""
    n, depth, in_str, i = 0, 0, False, 0
    while i < len(text):
        c = text[i]
        if in_str:
            if c == "\\":
                i += 2
                continue
            if c == '"':
                in_str = False
        elif c == '"':
            in_str = True
        elif c in "{([":
            if c == "{" and depth == 0:
                n += 1
            depth += 1
        elif c in "})]":
            depth -= 1
        i += 1
    return n


def validate_script(text: str, limits: Limits, where: str = "script") -> None:
    """Raise :class:`BudgetError` if ``text`` cannot be safely sent.

    Brace balance is checked even when limits are not enforced — an unbalanced
    script can fault the title on either target, so that check is unconditional.
    """
    ok, why = brace_balance(text)
    if not ok:
        raise BudgetError(f"{where}: unbalanced DTA ({why}); refusing to send")

    if not limits.enforced:
        return

    if len(text) > limits.max_script:
        raise BudgetError(
            f"{where}: {len(text)} chars exceeds script cap {limits.max_script}. "
            "Split the probe into more pages (fewer fields or objects per "
            "request), or raise --script-cap if the transport allows it."
        )
    if limits.one_command:
        n = count_top_level_commands(text)
        if n != 1:
            raise BudgetError(
                f"{where}: {n} top-level commands, transport allows exactly 1. "
                "Wrap the whole program in a single {do ...}."
            )


def check_result(text: str, limits: Limits, where: str = "result") -> str | None:
    """Return a warning if a reply looks truncated by the transport cap."""
    if not limits.enforced:
        return None
    if len(text) >= limits.max_result:
        return (
            f"{where}: reply is {len(text)} bytes, at or over the {limits.max_result}"
            " byte cap — it was probably TRUNCATED. Reduce the page size."
        )
    return None
