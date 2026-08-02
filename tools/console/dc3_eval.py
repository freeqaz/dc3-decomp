#!/usr/bin/env python3
"""Evaluate DTA on a Dance Central 3 engine -- real console or native port.

CLI and importable API are deliberately the same shape as RB3Enhanced's
`tools/rb3e_dta.py`, so a state-differ can drive an RB3 console and a DC3
console with the same code:

    dc3_eval.py <host> '<script>'          dc3_eval.py <host> -f probe.dta
    dc3_eval.py <host> -                   dc3_eval.py <host> --repl
    from dc3_eval import evaluate, ConsoleError, DEFAULT_PORT

Transports (`-T/--transport`):

  http      HTTP DTA-eval endpoint.  `--api eval` is the RB3Enhanced contract
            (POST /dta/eval, raw body, port 21070) that a DC3Enhanced DLL should
            expose; `--api native` is the native port's JSON endpoint
            (POST /api/dta/eval, :9090, docs/tools/HTTP_DEBUG_SERVER.md);
            `--api execute` is the legacy GET /execute?script= fallback.

  file      Real console, *no binary modification*.  The batch is pushed over
            FTP as a loose .dta, the game's RndConsole runs
            `{run "<drive>:\\...\\p.dta"}`, the script writes its answers to a
            file next to the XEX, and this tool pulls that file back.  One FTP
            round-trip and ONE keypress for a whole batch.

  appchild  Raw AppChild channel (console dials out to TCP 4543 and executes
            serialised DataArrays).  Command injection only -- the wire protocol
            carries no result.  Requires launch arguments.

Every transport exposes `eval(script) -> str` and `eval_batch(scripts) -> [str]`,
so the differ can swap transports without caring about the wire.

See docs/native/CONSOLE_DTA_EVAL.md for activation steps and the honest list of
hardware unknowns.
"""

from __future__ import annotations

import argparse
import difflib
import ftplib
import http.client
import io
import json
import os
import re
import socket
import struct
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

# --------------------------------------------------------------------------
# Contract constants -- these mirror RB3Enhanced include/DTAEval.h and
# source/net_http_server.c.  Keep them in lockstep with that repo.
# --------------------------------------------------------------------------
DEFAULT_PORT = 0x524E            # 21070, RB3Enhanced's HTTP server port
NATIVE_PORT = 9090               # DC3 native port's debug server
MAX_SCRIPT_BYTES = 16384         # RB3E_DTA_SCRIPT_MAX; >= this is a 413
MAX_RESULT_BYTES = 32768         # RB3E_DTA_OUTPUT_MAX
MAX_NESTING = 64                 # sizeof(stack) in RB3E_DTAEval_Validate

# Exact console-side markers.
TRUNCATION_NOTICE = ("\n!! output truncated, raise RB3E_DTA_OUTPUT_MAX or "
                     "split the script\n")
TRUNCATION_PREFIX = "!! output truncated"
PARSE_ERROR_BODY = "!! parse error"
RESULT_PREFIX = "=> "            # Nth "=> " line == Nth command's return value
REFUSED_PREFIX = "!! refused"    # e.g. "=> !! refused: bad command pointer"

# Placeholder for batch elements the console never got to because output filled
# up first.  Distinct from a refusal: these did not run at all.
NOT_EXECUTED = ("<not executed: batch was cut short by the console's %d-byte "
                "output cap>" % MAX_RESULT_BYTES)


def is_refusal(result: str) -> bool:
    """True if the console executed this slot but refused the command."""
    return result.startswith(REFUSED_PREFIX)

# --------------------------------------------------------------------------
# DataNode type tags (src/system/obj/Data.h:22-44)
# --------------------------------------------------------------------------
kDataInt = 0
kDataFloat = 1
kDataVar = 2
kDataFunc = 3
kDataObject = 4
kDataSymbol = 5
kDataUnhandled = 6
kDataIfdef = 7
kDataElse = 8
kDataEndif = 9
kDataArray = 16
kDataCommand = 17
kDataString = 18
kDataProperty = 19
kDataGlob = 20
kDataDefine = 32
kDataInclude = 33
kDataMerge = 34
kDataIfndef = 35
kDataAutorun = 36
kDataUndef = 37

APPCHILD_PORT = 0x11BF  # 4543, src/system/os/AppChild.cpp:15

# Symbols that DataArray::Load (src/system/obj/DataArray.cpp:497-506) expands
# from the macro table instead of leaving alone.  Never emit these bare.
MACRO_SYMBOLS = {"HX_XBOX", "HX_WIN", "HX_NG"}

_CLOSERS = {"(": ")", "{": "}", "[": "]"}
_KINDS = {"(": kDataArray, "{": kDataCommand, "[": kDataProperty}


class DtaError(Exception):
    """The script is malformed, or too large, and was not sent."""


class ConsoleError(Exception):
    """The console answered, but refused or failed the request.

    Same shape as RB3Enhanced tools/rb3e_dta.py:41-47.
    """

    def __init__(self, status, reason, body):
        super().__init__("HTTP %s %s: %s" % (status, reason, body))
        self.status = status
        self.body = body


# --------------------------------------------------------------------------
# Client-side validation.
#
# A port of RB3Enhanced's console-side RB3E_DTAEval_Validate
# (source/DTAEval.c:272-334), run here so a malformed script costs a syntax
# error instead of a round trip.  The console-side failure modes are expensive:
# DataReadString faults on unbalanced input, and a *balanced* script naming a
# nonexistent object still reaches MILO_FAIL, which is a C++ throw that neither
# the DLL nor the AppChild path can catch -- costing the user a reboot
# mid-session.  So: reject what is cheaply provable, and treat anything that
# survives as trusted.
#
# Deliberately matches the C: ';' starts a line comment, '"' toggles string mode
# with NO backslash escapes, nesting past 64 is rejected, an embedded NUL ends
# the scan.  Two safe additions: '/* */' block comments (skipping them can only
# ever prevent a false reject) and a size cap.  "'" is treated as a quoted
# symbol only when its partner is on the same line, so a stray apostrophe cannot
# cause a false reject.
# --------------------------------------------------------------------------

def validate_script(script: str, limit: int = MAX_SCRIPT_BYTES) -> str:
    """Return the script, or raise DtaError if it cannot possibly parse."""
    if not script.strip():
        raise DtaError("empty script")
    raw = script.encode("utf-8", errors="replace")
    if len(raw) >= limit:
        raise DtaError("script is %d bytes; the console rejects >= %d (413)"
                       % (len(raw), limit))

    stack = []
    i, n, line = 0, len(script), 1
    while i < n:
        c = script[i]
        if c == "\0":
            break                       # matches the C: scan stops at NUL
        if c == "\n":
            line += 1
            i += 1
        elif c == ";":
            while i < n and script[i] != "\n":
                i += 1
        elif script.startswith("/*", i):
            end = script.find("*/", i + 2)
            if end < 0:
                raise DtaError("unterminated /* comment (line %d)" % line)
            line += script.count("\n", i, end)
            i = end + 2
        elif c == '"':
            end = script.find('"', i + 1)
            if end < 0:
                raise DtaError("unterminated string literal on line %d" % line)
            line += script.count("\n", i, end)
            i = end + 1
        elif c == "'":
            eol = script.find("\n", i + 1)
            end = script.find("'", i + 1, None if eol < 0 else eol)
            i = (end + 1) if end >= 0 else (i + 1)
        elif c in "({[":
            if len(stack) >= MAX_NESTING:
                raise DtaError("nested deeper than %d on line %d"
                               % (MAX_NESTING, line))
            stack.append((c, line))
            i += 1
        elif c in ")}]":
            if not stack:
                raise DtaError("unmatched '%s' on line %d" % (c, line))
            opener, opened_at = stack.pop()
            if _CLOSERS[opener] != c:
                raise DtaError("'%s' opened on line %d is closed by '%s' on "
                               "line %d" % (opener, opened_at, c, line))
            i += 1
        else:
            i += 1
    if stack:
        opener, opened_at = stack[-1]
        raise DtaError("unclosed '%s' opened on line %d" % (opener, opened_at))
    return script


def check_truncated(body: str) -> None:
    """Raise if a SINGLE-command response was clipped.

    Only for the one-command paths (/execute, and a batch of one), where there
    is no salvageable prefix -- the one answer we asked for is incomplete, so
    returning it would silently hand the differ a clipped payload.  Batches go
    through split_results(), which can keep the good prefix instead.
    """
    if TRUNCATION_PREFIX in body:
        raise ConsoleError(200, "OK (truncated)",
                           "console output exceeded %d bytes and was truncated; "
                           "split the script into smaller batches"
                           % MAX_RESULT_BYTES)


def split_results(body: str, count: int):
    """Split an RB3Enhanced /dta/eval body into one result per command.

    Returns `(results, truncated)`.

    The body is captured print output interleaved with one `=> <value>` line
    per top-level command (RB3Enhanced source/DTAEval.c).  Everything emitted
    since the previous `=> ` line is that command's printed output.

    The contract is exactly one marker per command, in order, INCLUDING refused
    commands (which emit `=> !! refused: ...`), with one exception: **truncation
    stops the batch early**, so a truncated body legitimately carries fewer
    markers than commands sent.  Those two cases need opposite handling:

    * fewer markers AND the truncation banner -> a legitimate partial result.
      The markers we did get are correctly attributed to the first N commands;
      the rest simply never ran.  Caller re-issues the remainder.
    * any count mismatch WITHOUT the banner -> a genuine protocol
      inconsistency.  Refuse to attribute at all rather than risk silently
      pairing command i's script with command j's answer.
    """
    if body.strip() == PARSE_ERROR_BODY:
        raise ConsoleError(200, "OK (parse error)",
                           "console could not parse the script")
    truncated = TRUNCATION_PREFIX in body
    results, printed = [], []
    for raw_line in body.splitlines():
        if raw_line.startswith(RESULT_PREFIX):
            value = raw_line[len(RESULT_PREFIX):].strip()
            prefix = "\n".join(printed).strip()
            results.append((prefix + "\n" + value).strip() if prefix else value)
            printed = []
        elif raw_line.startswith(TRUNCATION_PREFIX):
            continue                    # banner is framing, not output
        else:
            printed.append(raw_line)

    if not results:
        if truncated:
            return [], True             # not even one command fitted
        # single-expression path: no "=> " markers at all
        return [body.strip()], False

    if len(results) > count or (len(results) < count and not truncated):
        raise ConsoleError(
            200, "OK (protocol mismatch)",
            "console returned %d result markers for %d commands and did not "
            "report truncation; refusing to attribute results to commands"
            % (len(results), count))
    return results[:count], truncated


def join_batch(scripts):
    """Body for a batch: concatenated top-level commands.

    RB3Enhanced detects a batch by proving every top-level node is a COMMAND
    (source/DTAEval.c:406-443); the client sends nothing special.  Newline
    separators (rather than the bare concatenation in their doc example) are
    required for correctness: a script ending in a ';' line comment would
    otherwise swallow the command after it.
    """
    return "\n".join(s.strip() for s in scripts)


# --------------------------------------------------------------------------
# Minimal DTA reader (used by the AppChild wire encoder).
# --------------------------------------------------------------------------

_TOKEN_END = set(" \t\r\n(){}[]")


def parse_dta(text: str):
    """Parse DTA source into a list of top-level nodes."""
    nodes, pos = _parse_seq(text, 0, None)
    if pos != len(text):
        raise DtaError("unbalanced closing delimiter at offset %d" % pos)
    return nodes


def _parse_seq(text: str, pos: int, closer):
    out = []
    n = len(text)
    while True:
        while pos < n:
            c = text[pos]
            if c in " \t\r\n":
                pos += 1
            elif c == ";":
                while pos < n and text[pos] != "\n":
                    pos += 1
            elif text.startswith("/*", pos):
                end = text.find("*/", pos + 2)
                if end < 0:
                    raise DtaError("unterminated /* comment")
                pos = end + 2
            else:
                break
        if pos >= n:
            if closer:
                raise DtaError("unterminated '%s'" % closer)
            return out, pos
        c = text[pos]
        if c in ")}]":
            if c != closer:
                raise DtaError("unexpected '%s' at offset %d" % (c, pos))
            return out, pos + 1
        if c in "({[":
            inner, pos = _parse_seq(text, pos + 1, _CLOSERS[c])
            out.append((_KINDS[c], inner))
            continue
        if c == '"':
            end = text.find('"', pos + 1)
            if end < 0:
                raise DtaError("unterminated string literal")
            out.append((kDataString, text[pos + 1:end]))
            pos = end + 1
            continue
        if c == "'":
            end = text.find("'", pos + 1)
            if end < 0:
                raise DtaError("unterminated quoted symbol")
            out.append((kDataSymbol, text[pos + 1:end]))
            pos = end + 1
            continue
        start = pos
        while pos < n and text[pos] not in _TOKEN_END and text[pos] not in '"\'':
            pos += 1
        tok = text[start:pos]
        if not tok:
            raise DtaError("empty token at offset %d" % start)
        out.append(_classify(tok))


_INT_RE = re.compile(r"^[+-]?\d+$")
_HEX_RE = re.compile(r"^0[xX][0-9a-fA-F]+$")
_FLOAT_RE = re.compile(r"^[+-]?(\d+\.\d*|\.\d+|\d+)([eE][+-]?\d+)?$")


def _classify(tok: str):
    if tok.startswith("$"):
        return (kDataVar, tok[1:])
    if _INT_RE.match(tok):
        return (kDataInt, int(tok))
    if _HEX_RE.match(tok):
        return (kDataInt, int(tok, 16))
    if _FLOAT_RE.match(tok) and ("." in tok or "e" in tok or "E" in tok):
        return (kDataFloat, float(tok))
    if tok == "TRUE":
        return (kDataInt, 1)
    if tok == "FALSE":
        return (kDataInt, 0)
    return (kDataSymbol, tok)


# --------------------------------------------------------------------------
# AppChild wire encoder.
#
# AppChild::Poll (src/system/os/AppChild.cpp:67-77) does:
#     DataArrayPtr cmd; *mStream >> cmd; cmd->Execute(true);
# operator>>(BinStream&, DataArrayPtr&) (src/system/obj/Data.h:751) calls
# DataArray::Load directly -- there is NO leading "non-null" bool, unlike
# operator>>(BinStream&, DataArray*&) at DataArray.cpp:1011.
#
# NetStream is constructed BinStream(true) (NetStream.cpp:8) => little endian on
# the wire (ReadEndian byte-swaps on the big-endian console).
#
# DataArray::Load  (DataArray.cpp:488-...):  s16 size, s16 line, s16 deprecated,
#                                            then `size` DataNodes
# DataNode::Load   (DataNode.cpp:727-792):   s32 type, then per-type payload
# --------------------------------------------------------------------------

def encode_array(nodes, line: int = 0, deprecated: int = 0) -> bytes:
    buf = bytearray()
    buf += struct.pack("<hhh", len(nodes), line, deprecated)
    for node in nodes:
        buf += encode_node(node)
    return bytes(buf)


def _pstr(s: str) -> bytes:
    """int32 length + raw bytes, no NUL (BinStream::operator<<(const char*))."""
    raw = s.encode("utf-8")
    return struct.pack("<i", len(raw)) + raw


def encode_node(node) -> bytes:
    kind, payload = node
    head = struct.pack("<i", kind)
    if kind in (kDataArray, kDataCommand, kDataProperty):
        return head + encode_array(payload)
    if kind == kDataInt:
        return head + struct.pack("<i", payload)
    if kind == kDataFloat:
        return head + struct.pack("<f", payload)
    if kind == kDataString:
        # DataArray::LoadGlob(bs, true): int32 len, then len raw bytes
        raw = payload.encode("utf-8")
        return head + struct.pack("<i", len(raw)) + raw
    if kind in (kDataSymbol, kDataVar, kDataFunc, kDataObject, kDataIfdef,
                kDataIfndef, kDataDefine, kDataInclude, kDataMerge, kDataUndef):
        if kind == kDataSymbol and payload in MACRO_SYMBOLS:
            raise DtaError(
                "symbol %r is a DTA macro; DataArray::Load would substitute it"
                % payload)
        return head + _pstr(payload)
    raise DtaError("cannot encode node type %d" % kind)


def encode_command(text: str) -> bytes:
    """Encode one DTA command as an AppChild frame.

    AppChild executes the top-level array itself, so `{print "x"}` must be sent
    as the *contents* of the braces, not as a one-element wrapper.
    """
    nodes = parse_dta(text)
    if len(nodes) == 1 and nodes[0][0] in (kDataCommand, kDataArray):
        nodes = nodes[0][1]
    return encode_array(nodes)


# --------------------------------------------------------------------------
# Transport: HTTP
# --------------------------------------------------------------------------

class HttpTransport:
    """DTA eval over HTTP.

    api="eval"    -> POST /dta/eval, raw body, `=> ` result lines.  The
                     RB3Enhanced contract (port 21070) a DC3Enhanced DLL should
                     expose.  One request per batch -- per-request latency
                     dominates, so batching is the whole point.
    api="native"  -> POST /api/dta/eval, JSON envelope (native port, :9090)
    api="execute" -> GET  /execute?script=..., plain text (legacy; the console's
                     request_path[250] buffer caps this at ~200 bytes)
    api="auto"    -> eval, then native, then execute
    """

    def __init__(self, host: str, port: int = DEFAULT_PORT, api: str = "auto",
                 timeout: float = 10.0, auto_page: bool = True):
        self.host = host
        self.port = port
        self.api = api
        self.timeout = timeout
        self.auto_page = auto_page

    # -- raw HTTP ---------------------------------------------------------
    def _request(self, method: str, path: str, body: bytes = None) -> str:
        conn = http.client.HTTPConnection(self.host, self.port,
                                          timeout=self.timeout)
        try:
            headers = {"Connection": "close"}
            if body is not None:
                headers["Content-Type"] = "text/plain"
                headers["Content-Length"] = str(len(body))
            conn.request(method, path, body=body, headers=headers)
            resp = conn.getresponse()
            text = resp.read().decode("utf-8", errors="replace")
            if resp.status != 200:
                raise ConsoleError(resp.status, resp.reason, text.strip())
            return text
        finally:
            conn.close()

    # -- per-api ----------------------------------------------------------
    def _post_batch(self, scripts):
        body = join_batch(scripts).encode("utf-8", errors="replace")
        if len(body) >= MAX_SCRIPT_BYTES:
            raise DtaError("batch body is %d bytes; the console rejects >= %d "
                           "(RB3E_DTA_SCRIPT_MAX, 413) -- send fewer commands "
                           "per batch" % (len(body), MAX_SCRIPT_BYTES))
        return split_results(self._request("POST", "/dta/eval", body),
                             len(scripts))

    def _eval_batch(self, scripts):
        """Run a batch, transparently paging around the output cap.

        Truncation stops the console's batch early, which is the ordinary
        "your dump is bigger than 32 KB" signal rather than an error.  Re-issue
        the un-run tail until everything has an answer.  This terminates: each
        pass consumes at least one command (a lone command whose own output
        overflows is recorded as truncated and stepped over).
        """
        if len(scripts) == 1:
            text = self._request("POST", "/dta/eval",
                                 scripts[0].encode("utf-8", errors="replace"))
            check_truncated(text)       # nothing to salvage from one command
            results, _ = split_results(text, 1)
            return results

        out, remaining = [], list(scripts)
        while remaining:
            results, truncated = self._post_batch(remaining)
            if not truncated:
                out.extend(results)     # split_results proved the counts match
                break
            if results:
                out.extend(results)
                remaining = remaining[len(results):]
            else:
                # The very first command overflowed the cap on its own, so
                # paging cannot make progress. Record it and step past.
                out.append("<truncated: this command's own output exceeds the "
                           "%d-byte cap; narrow it>" % MAX_RESULT_BYTES)
                remaining = remaining[1:]
            if remaining and not self.auto_page:
                out.extend([NOT_EXECUTED] * len(remaining))
                break
        return out

    def _native_one(self, script: str) -> str:
        text = self._request("POST", "/api/dta/eval",
                             script.encode("utf-8", errors="replace"))
        try:
            doc = json.loads(text)
        except ValueError:
            return text.strip()
        if isinstance(doc, dict) and "data" in doc:
            data = doc["data"]
            if isinstance(data, dict) and "value" in data:
                return str(data["value"])
            return json.dumps(data)
        return text.strip()

    def _execute_one(self, script: str) -> str:
        path = "/execute?" + urllib.parse.urlencode({"script": script})
        text = self._request("GET", path)
        check_truncated(text)
        return text.strip()

    # -- public -----------------------------------------------------------
    def eval_batch(self, scripts):
        scripts = [validate_script(s) for s in scripts]
        if self.api == "eval":
            return self._eval_batch(scripts)
        if self.api == "native":
            return [self._native_one(s) for s in scripts]
        if self.api == "execute":
            return [self._execute_one(s) for s in scripts]
        attempts = (self._eval_batch,
                    lambda ss: [self._native_one(s) for s in ss],
                    lambda ss: [self._execute_one(s) for s in ss])
        last = None
        for attempt in attempts:
            try:
                return attempt(scripts)
            except ConsoleError as exc:
                # 404 means "wrong endpoint, try the next"; anything else is a
                # real refusal (403 disabled, 409 busy, 413 too long, ...) and
                # must not be masked by falling through.
                if exc.status != 404:
                    raise
                last = exc
            except OSError as exc:
                last = exc
        raise ConnectionError(
            "no DTA-eval endpoint answered on %s:%d (tried /dta/eval, "
            "/api/dta/eval, /execute); last error: %s"
            % (self.host, self.port, last))

    def eval(self, script: str) -> str:
        return self.eval_batch([script])[0]


def evaluate(host, script, port=DEFAULT_PORT, timeout=10.0):
    """Run `script` on a console and return its output.

    Signature-compatible with RB3Enhanced tools/rb3e_dta.py:50, so a differ can
    import either module and call the same function.  Raises ConsoleError if the
    console refused, OSError if it could not be reached, DtaError if the script
    is malformed (this client validates first; rb3e_dta.py does not).
    """
    validate_script(script)
    return HttpTransport(host, port, "eval", timeout)._request(
        "POST", "/dta/eval", script.encode("utf-8", errors="replace"))


# --------------------------------------------------------------------------
# Transport: file (RndConsole + FTP)
# --------------------------------------------------------------------------

PROBE_NAME = "p.dta"
OUT_NAME = "dc3_out.txt"
DONE_NAME = "dc3_done.txt"


def record_marker(token: str) -> str:
    return "~~DC3REC:%s~~" % token


def build_probe(scripts, token: str, out_name: str = OUT_NAME,
                done_name: str = DONE_NAME, raw: bool = False) -> str:
    """Wrap a batch of user scripts so their values land in a file we can fetch.

    One record per command, in order -- the same batch contract as
    /dta/eval, expressed in DTA.  Records are separated by a per-run marker
    rather than a newline because TextFileStream::Print rewrites '\\n' to CRLF
    and DTA string literals have no escape syntax.

    {sprint <expr>}              -> DataFunc.cpp:72   (DataSprint) stringifies
    {strcat $v a b}              -> DataFunc.cpp:1217 (DataStrCat) appends
                                    IN PLACE; arg 1 must be a $var, and
                                    DataNode::Str evaluates commands for the
                                    remaining args (DataNode.cpp:423-424)
    {write_string_to_file p s 0} -> DataFunc.cpp:1088 (OnWriteStringToFile);
                                    the 4th arg MUST be 0, otherwise
                                    `array->Size() > 3 ? array->Int(3) : true`
                                    opens the stream in kAppend

    Accumulating into a var and writing once means the whole batch needs exactly
    one successful truncating write -- it never depends on kAppend working.

    Output paths are deliberately *relative*: FileIsLocal() MILO_ASSERTs on a
    "game" drive (src/system/os/File_Win.cpp:9-13), and a relative write goes
    through AsyncFile::New straight to the title's working directory, i.e. the
    folder the XEX was launched from.
    """
    if isinstance(scripts, str):
        scripts = [scripts]
    lines = ["; generated by tools/console/dc3_eval.py -- do not edit", "{do"]
    if raw:
        lines += ["   " + s for s in scripts]
    else:
        mark = record_marker(token)
        lines.append('   {set $dc3_r ""}')
        for script in scripts:
            lines.append('   {strcat $dc3_r {sprint {do %s}} "%s"}'
                         % (script.strip(), mark))
        lines.append('   {write_string_to_file "%s" $dc3_r 0}' % out_name)
    lines.append('   {write_string_to_file "%s" "%s" 0}' % (done_name, token))
    lines.append("}")
    return "\n".join(lines) + "\n"


def split_records(blob: str, token: str, count: int):
    """Split a batch result file back into one result per command."""
    mark = record_marker(token)
    if mark not in blob:
        return [blob.strip()]           # raw mode, or a single unmarked payload
    parts = blob.split(mark)
    if parts and not parts[-1].strip():
        parts.pop()
    out = [p.strip() for p in parts]
    while len(out) < count:
        out.append("<missing: console produced %d of %d records>"
                   % (len(parts), count))
    return out[:count]


class FileTransport:
    def __init__(self, ftp_host, ftp_dir, game_path, user="xbox", password="xbox",
                 port=21, timeout=20.0, poll=0.5, hid_cmd=None, subdir="dc3",
                 verbose=False):
        self.ftp_host = ftp_host
        self.ftp_dir = ftp_dir.rstrip("/")
        self.game_path = game_path.rstrip("\\/")
        self.user = user
        self.password = password
        self.port = port
        self.timeout = timeout
        self.poll = poll
        self.hid_cmd = hid_cmd
        self.subdir = subdir
        self.verbose = verbose

    # -- FTP helpers ------------------------------------------------------
    def _connect(self):
        ftp = ftplib.FTP()
        ftp.connect(self.ftp_host, self.port, timeout=self.timeout)
        ftp.login(self.user, self.password)
        return ftp

    @staticmethod
    def _quiet_delete(ftp, path):
        try:
            ftp.delete(path)
        except ftplib.all_errors:
            pass

    @staticmethod
    def _quiet_mkd(ftp, path):
        try:
            ftp.mkd(path)
        except ftplib.all_errors:
            pass

    def _get(self, ftp, path):
        sink = io.BytesIO()
        ftp.retrbinary("RETR " + path, sink.write)
        return sink.getvalue()

    # -- public -----------------------------------------------------------
    def console_line(self):
        """The one line the user types into the RndConsole, once per boot.

        The trailing '/' is deliberate: RndConsole::ExecuteLine
        (src/system/rndobj/Console.cpp:421-424) strips it and hides the console,
        and the history entry keeps it (push_front happens first, at :420).  So
        every later run is Esc, Up, Enter -- and the console closes itself.
        """
        return '{run "%s\\%s\\%s"}/' % (self.game_path, self.subdir, PROBE_NAME)

    def eval(self, script: str, raw: bool = False) -> str:
        return self.eval_batch([script], raw=raw)[0]

    def eval_batch(self, scripts, raw: bool = False):
        """One FTP round-trip and ONE keypress for the whole batch."""
        scripts = [validate_script(s) for s in scripts]
        token, blob = self._run(scripts, raw)
        results = split_records(blob, token, len(scripts))
        for r in results:
            if len(r.encode("utf-8")) > MAX_RESULT_BYTES:
                raise ConsoleError(200, "OK (truncated)",
                                   "a record is %d bytes, over the %d-byte "
                                   "result cap" % (len(r), MAX_RESULT_BYTES))
        return results

    def _run(self, scripts, raw: bool):
        token = "dc3-%d" % (time.time_ns() & 0xFFFFFFFF)
        probe = build_probe(scripts, token, raw=raw)
        if len(probe.encode("utf-8")) >= MAX_SCRIPT_BYTES:
            raise DtaError("generated probe is %d bytes, over the %d-byte limit "
                           "-- send fewer commands per batch"
                           % (len(probe.encode("utf-8")), MAX_SCRIPT_BYTES))
        remote_dir = "%s/%s" % (self.ftp_dir, self.subdir)
        out_path = "%s/%s" % (self.ftp_dir, OUT_NAME)
        done_path = "%s/%s" % (self.ftp_dir, DONE_NAME)

        ftp = self._connect()
        try:
            self._quiet_mkd(ftp, remote_dir)
            self._quiet_delete(ftp, out_path)
            self._quiet_delete(ftp, done_path)
            ftp.storbinary("STOR %s/%s" % (remote_dir, PROBE_NAME),
                           io.BytesIO(probe.encode("utf-8")))
        finally:
            try:
                ftp.quit()
            except ftplib.all_errors:
                pass

        if self.verbose:
            sys.stderr.write("pushed %s/%s (%d bytes, %d command(s))\n"
                             % (remote_dir, PROBE_NAME, len(probe), len(scripts)))

        self._trigger()

        deadline = time.time() + self.timeout
        last_err = None
        while time.time() < deadline:
            time.sleep(self.poll)
            try:
                ftp = self._connect()
            except OSError as exc:      # console busy / single-session FTP
                last_err = exc
                continue
            try:
                try:
                    done = self._get(ftp, done_path).decode("utf-8", "replace")
                except ftplib.all_errors as exc:
                    last_err = exc
                    continue
                if token not in done:
                    continue
                try:
                    blob = self._get(ftp, out_path).decode("utf-8", "replace")
                except ftplib.all_errors:
                    blob = ""           # script ran but wrote no output file
                return token, blob.strip()
            finally:
                try:
                    ftp.quit()
                except ftplib.all_errors:
                    pass
        raise TimeoutError(
            "no %s with token %s after %.1fs (last FTP error: %s).  Did the "
            "console run %s ?" % (DONE_NAME, token, self.timeout, last_err,
                                  self.console_line()))

    def _trigger(self):
        if self.hid_cmd:
            if self.verbose:
                sys.stderr.write("trigger: %s\n" % self.hid_cmd)
            subprocess.run(self.hid_cmd, shell=True, check=False)
            return
        sys.stderr.write(
            "\n  >>> On the console keyboard: Esc, Up-arrow, Enter\n"
            "      (first time this boot, type: %s )\n\n" % self.console_line())


# --------------------------------------------------------------------------
# Transport: appchild (TCP 4543, console dials out to us)
# --------------------------------------------------------------------------

class AppChildTransport:
    """Serve the AppChild command channel.

    The console is the TCP *client*: AppChild's constructor does
    NetStream::ClientConnect(HolmesResolveIP().mIP, 0x11BF), so this side
    listens.  AppChild::Poll blocks the game each frame reading DataArrays and
    executing them until one of them calls {sync_app_child}, which writes a
    single little-endian u16 (1) back and releases the frame.
    """

    def __init__(self, bind="0.0.0.0", port=APPCHILD_PORT, timeout=60.0,
                 verbose=False):
        self.bind = bind
        self.port = port
        self.timeout = timeout
        self.verbose = verbose
        self._srv = None
        self._conn = None

    def wait_for_console(self):
        self._srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._srv.bind((self.bind, self.port))
        self._srv.listen(1)
        self._srv.settimeout(self.timeout)
        sys.stderr.write("waiting for AppChild on %s:%d ...\n"
                         % (self.bind, self.port))
        self._conn, addr = self._srv.accept()
        self._conn.settimeout(self.timeout)
        sys.stderr.write("console connected from %s:%d\n" % addr)
        return addr

    def send(self, script: str):
        """Send one command. Returns nothing -- the channel has no reply."""
        frame = encode_command(validate_script(script))
        if self.verbose:
            sys.stderr.write("appchild tx %d bytes: %s\n"
                             % (len(frame), frame.hex()))
        self._conn.sendall(frame)

    def sync(self):
        """Send {sync_app_child} and wait for the u16 frame ack."""
        self.send("{sync_app_child}")
        data = b""
        while len(data) < 2:
            chunk = self._conn.recv(2 - len(data))
            if not chunk:
                raise ConnectionError("console closed the AppChild stream")
            data += chunk
        return struct.unpack("<H", data)[0]

    NO_RESULT = ("<appchild has no return path -- use -T file, or put a "
                 "{write_string_to_file ...} in your script and fetch it>")

    def eval_batch(self, scripts):
        """Interface parity: sends every command, then one {sync_app_child}."""
        for script in scripts:
            self.send(script)
        self.sync()
        return [self.NO_RESULT] * len(scripts)

    def eval(self, script: str) -> str:
        return self.eval_batch([script])[0]

    def close(self):
        for sock in (self._conn, self._srv):
            if sock:
                try:
                    sock.close()
                except OSError:
                    pass


# --------------------------------------------------------------------------
# Self test (no hardware needed)
# --------------------------------------------------------------------------

def _self_test_file_transport():
    """Exercise the FTP round-trip against an in-process fake console."""
    failures = []
    store = {}

    class FakeFTP:
        run_probe = True

        def connect(self, host, port, timeout=None):
            pass

        def login(self, user, password):
            pass

        def quit(self):
            pass

        def mkd(self, path):
            pass

        def delete(self, path):
            if path not in store:
                raise ftplib.error_perm("550 not found")
            del store[path]

        def storbinary(self, cmd, fh):
            path = cmd.split(" ", 1)[1]
            store[path] = fh.read()
            if not FakeFTP.run_probe:
                return
            text = store[path].decode()
            token = re.search(r'"dc3_done\.txt" "([^"]+)"', text).group(1)
            mark = record_marker(token)
            n = text.count("{strcat $dc3_r")
            store["/g/" + OUT_NAME] = mark.join(
                ["r%d" % i for i in range(n)]).encode() + mark.encode()
            store["/g/" + DONE_NAME] = token.encode()

        def retrbinary(self, cmd, callback):
            path = cmd.split(" ", 1)[1]
            if path not in store:
                raise ftplib.error_perm("550 not found")
            callback(store[path])

    real = ftplib.FTP
    ftplib.FTP = FakeFTP
    try:
        tr = FileTransport("1.2.3.4", "/g", "Hdd1:\\g", timeout=5, poll=0.01,
                           hid_cmd="true")
        if tr.console_line() != '{run "Hdd1:\\g\\dc3\\p.dta"}/':
            failures.append("console_line: %r" % tr.console_line())
        if tr.eval("{ui current_screen}") != "r0":
            failures.append("single round-trip failed")
        probe = store["/g/dc3/p.dta"].decode()
        if "{sprint {do {ui current_screen}}}" not in probe:
            failures.append("pushed probe wrong:\n%s" % probe)

        got = tr.eval_batch(["{a}", "{b}", "{c}"])
        if got != ["r0", "r1", "r2"]:
            failures.append("batch round-trip got %r" % got)
        probe = store["/g/dc3/p.dta"].decode()
        if probe.count("{strcat $dc3_r") != 3 or probe.count(
                "write_string_to_file") != 2:
            failures.append("batch probe wrong:\n%s" % probe)

        store.clear()
        FakeFTP.run_probe = False
        tr = FileTransport("1.2.3.4", "/g", "Hdd1:\\g", timeout=0.3, poll=0.05,
                           hid_cmd="true")
        try:
            tr.eval("{x}")
            failures.append("stale console did not time out")
        except TimeoutError:
            pass
    finally:
        FakeFTP.run_probe = True
        ftplib.FTP = real
    return failures


def self_test() -> int:
    failures = []

    def check(name, got, want):
        if got != want:
            failures.append("%s:\n  got  %r\n  want %r" % (name, got, want))

    # ---- DTA reader -----------------------------------------------------
    check("parse int", parse_dta("42"), [(kDataInt, 42)])
    check("parse hex", parse_dta("0x10"), [(kDataInt, 16)])
    check("parse float", parse_dta("1.5"), [(kDataFloat, 1.5)])
    check("parse var", parse_dta("$x"), [(kDataVar, "x")])
    check("parse sym", parse_dta("foo"), [(kDataSymbol, "foo")])
    check("parse str", parse_dta('"hi"'), [(kDataString, "hi")])
    check("parse cmd", parse_dta("{+ 1 2}"),
          [(kDataCommand, [(kDataSymbol, "+"), (kDataInt, 1), (kDataInt, 2)])])
    check("parse array", parse_dta("(a 1)"),
          [(kDataArray, [(kDataSymbol, "a"), (kDataInt, 1)])])
    check("parse prop", parse_dta("[p]"), [(kDataProperty, [(kDataSymbol, "p")])])
    check("comment", parse_dta("; nope\n7"), [(kDataInt, 7)])
    check("block comment", parse_dta("/* x */ 7"), [(kDataInt, 7)])

    # ---- AppChild wire format -------------------------------------------
    frame = encode_command("{sync_app_child}")
    check("encode sync", frame,
          struct.pack("<hhh", 1, 0, 0) + struct.pack("<i", kDataSymbol)
          + struct.pack("<i", len("sync_app_child")) + b"sync_app_child")

    check("encode print", encode_command('{print "x"}'),
          struct.pack("<hhh", 2, 0, 0)
          + struct.pack("<i", kDataSymbol) + struct.pack("<i", 5) + b"print"
          + struct.pack("<i", kDataString) + struct.pack("<i", 1) + b"x")

    check("encode nested", encode_command("{set $x {+ 1 2}}"),
          struct.pack("<hhh", 3, 0, 0)
          + struct.pack("<i", kDataSymbol) + struct.pack("<i", 3) + b"set"
          + struct.pack("<i", kDataVar) + struct.pack("<i", 1) + b"x"
          + struct.pack("<i", kDataCommand) + struct.pack("<hhh", 3, 0, 0)
          + struct.pack("<i", kDataSymbol) + struct.pack("<i", 1) + b"+"
          + struct.pack("<i", kDataInt) + struct.pack("<i", 1)
          + struct.pack("<i", kDataInt) + struct.pack("<i", 2))

    try:
        encode_command("{foo HX_XBOX}")
        failures.append("macro symbol should have been rejected")
    except DtaError:
        pass

    for bad in ("{unbalanced", "}", '"unterminated'):
        try:
            parse_dta(bad)
            failures.append("parser accepted %r" % bad)
        except DtaError:
            pass

    # ---- validator (port of RB3E_DTAEval_Validate) -----------------------
    for good in ('{print "a"}', '{a}{b}', '; c\n{a}', '{a "}"}', "{a 'sym'}",
                 "{a} ; trailing", "{a \"don't\"}", "/* {  */ {a}"):
        try:
            validate_script(good)
        except DtaError as exc:
            failures.append("validator rejected valid %r: %s" % (good, exc))
    for bad, why in (("{a", "unclosed"), ("{a)", "mismatched"), ("}", "unmatched"),
                     ('{a "x}', "unterminated string"), ("   ", "empty"),
                     ("{" * (MAX_NESTING + 1), "too deep")):
        try:
            validate_script(bad)
            failures.append("validator accepted %r (%s)" % (bad, why))
        except DtaError:
            pass
    try:
        validate_script("{a %s}" % ("x" * MAX_SCRIPT_BYTES))
        failures.append("validator accepted an oversize script")
    except DtaError:
        pass

    # ---- /dta/eval response parsing -------------------------------------
    # RB3Enhanced's own worked example: {print "a"}{rb3e_get_song_count}
    # prints `"a"` (with quotes -- DTAEval_PrintNode quotes STRING_VALUE), then
    # `=> 0` for print's return, then `=> 83`.
    check("split 2 cmds", split_results('"a"\n=> 0\n=> 83\n', 2),
          (['"a"\n0', "83"], False))
    check("split single", split_results("just a value", 1),
          (["just a value"], False))
    # Refusals are in-band and keep the markers 1:1, so they attribute normally.
    check("split refusal", split_results("=> 1\n=> !! refused: bad command "
                                         "pointer\n=> 3\n", 3),
          (["1", "!! refused: bad command pointer", "3"], False))
    check("is_refusal", (is_refusal("!! refused: bad command pointer"),
                         is_refusal("83")), (True, False))

    # Case 1: fewer markers WITH the banner == legitimate partial result.
    check("split truncated partial",
          split_results("=> 1\n=> 2\n" + TRUNCATION_NOTICE, 5),
          (["1", "2"], True))
    check("split truncated none", split_results(TRUNCATION_NOTICE, 4), ([], True))

    # Case 2: count mismatch WITHOUT the banner == protocol violation.
    for body, count, why in (("=> 1\n", 3, "too few, no banner"),
                             ("=> 1\n=> 2\n", 1, "too many")):
        try:
            split_results(body, count)
            failures.append("split_results accepted %s" % why)
        except ConsoleError:
            pass

    try:
        check_truncated("stuff" + TRUNCATION_NOTICE)
        failures.append("truncated single response was not rejected")
    except ConsoleError as exc:
        if exc.status != 200:
            failures.append("truncation should surface as a 200-with-error")
    try:
        split_results(PARSE_ERROR_BODY + "\n", 1)
        failures.append("parse-error body was not rejected")
    except ConsoleError:
        pass
    check("join batch", join_batch(["{a}", " {b} "]), "{a}\n{b}")

    # ---- probe generation -----------------------------------------------
    probe = build_probe("{+ 1 2}", "tok1")
    for needle in ("{sprint {do {+ 1 2}}}", '{set $dc3_r ""}',
                   '"dc3_out.txt" $dc3_r 0', '"dc3_done.txt" "tok1" 0'):
        if needle not in probe:
            failures.append("probe missing %r:\n%s" % (needle, probe))
    check("split_records", split_records("a~~DC3REC:t~~b~~DC3REC:t~~", "t", 2),
          ["a", "b"])

    failures.extend(_self_test_file_transport())

    if failures:
        for f in failures:
            print("FAIL " + f)
        print("\n%d failure(s)" % len(failures))
        return 1
    print("self-test OK")
    return 0


# --------------------------------------------------------------------------
# CLI -- positional shape mirrors RB3Enhanced tools/rb3e_dta.py
# --------------------------------------------------------------------------

def build_parser():
    p = argparse.ArgumentParser(
        prog="dc3_eval.py",
        description="Evaluate a DTA script on a Dance Central 3 engine "
                    "(real console or native port).",
        epilog="CLI shape mirrors RB3Enhanced tools/rb3e_dta.py. "
               "See docs/native/CONSOLE_DTA_EVAL.md.",
    )
    # A single greedy positional list rather than two `nargs="?"` positionals:
    # argparse cannot place the second one when a flag separates them, so
    # `dc3_eval.py <host> -p 21070 '{script}'` would fail with "unrecognized
    # arguments".  Split by hand instead, preserving the `<host> <script>` order.
    p.add_argument("args", nargs="*", metavar="host [script]",
                   help="console IP or hostname (not needed for -T file, which "
                        "uses --ftp-host), then the DTA script to run; "
                        "'-' reads the script from stdin")
    p.add_argument("-f", "--file",
                   help="read the script from this file instead of the argument")
    p.add_argument("-p", "--port", type=int, default=DEFAULT_PORT,
                   help="HTTP server port (default %d; use %d for the native "
                        "port)" % (DEFAULT_PORT, NATIVE_PORT))
    p.add_argument("-t", "--timeout", type=float, default=10.0,
                   help="socket timeout in seconds (default 10)")
    p.add_argument("--repl", action="store_true",
                   help="interactive prompt, one script per line")
    p.add_argument("-e", "--expr", help="alias for the script positional")
    p.add_argument("-b", "--batch", action="append", metavar="SCRIPT",
                   help="add one command to a batch; repeatable. One round trip, "
                        "one result line per command.")
    p.add_argument("-T", "--transport", default="http",
                   choices=("http", "file", "appchild"))
    p.add_argument("--no-auto-page", dest="auto_page", action="store_false",
                   help="do not re-issue the tail of a batch that the console "
                        "cut short at its %d-byte output cap; mark the un-run "
                        "commands instead" % MAX_RESULT_BYTES)
    p.add_argument("--api", default="auto",
                   choices=("auto", "eval", "native", "execute"),
                   help="eval = POST /dta/eval (RB3Enhanced contract), "
                        "native = POST /api/dta/eval, "
                        "execute = GET /execute?script=")
    p.add_argument("-v", "--verbose", action="store_true")
    p.add_argument("--self-test", action="store_true",
                   help="run offline unit tests and exit")
    p.add_argument("--print-bootstrap", action="store_true",
                   help="print the one-time RndConsole line and exit")
    p.add_argument("--raw", action="store_true",
                   help="file transport: push the script verbatim, do not wrap "
                        "it in {sprint}/{write_string_to_file}")

    g = p.add_argument_group("file transport (console over FTP)")
    g.add_argument("--ftp-host", default=os.environ.get("DC3_XBOX"),
                   help="console IP running an FTP server (defaults to $DC3_XBOX, "
                        "then the host positional)")
    g.add_argument("--ftp-port", type=int, default=21)
    g.add_argument("--ftp-user", default=os.environ.get("DC3_FTP_USER", "xbox"))
    g.add_argument("--ftp-pass", default=os.environ.get("DC3_FTP_PASS", "xbox"))
    g.add_argument("--ftp-dir", default=os.environ.get("DC3_FTP_DIR"),
                   help="FTP path of the folder containing default.xex, "
                        "e.g. /Hdd1/Games/DanceCentral3")
    g.add_argument("--game-path", default=os.environ.get("DC3_GAME_PATH"),
                   help="the SAME folder as the title sees it, with a "
                        "multi-character drive, e.g. 'Hdd1:\\Games\\DanceCentral3'. "
                        "Single-character drives and 'game:' do not work -- see "
                        "docs/native/CONSOLE_DTA_EVAL.md")
    g.add_argument("--subdir", default="dc3",
                   help="subfolder for the probe script (default 'dc3')")
    g.add_argument("--hid-cmd",
                   help="shell command that presses Esc/Up/Enter on the console "
                        "(e.g. a USB-HID bridge). If unset, this tool prompts "
                        "you to do it by hand.")

    g = p.add_argument_group("appchild transport")
    g.add_argument("--bind", default="0.0.0.0")
    g.add_argument("--appchild-port", type=int, default=APPCHILD_PORT)

    g = p.add_argument_group("comparison")
    g.add_argument("--diff", action="store_true",
                   help="also run the batch on the native port and unified-diff "
                        "the two; exit 1 if they differ")
    g.add_argument("--native-host", default="localhost")
    g.add_argument("--native-port", type=int, default=NATIVE_PORT)
    return p


def make_transport(args, parser):
    if args.transport == "http":
        if not args.host:
            parser.error("the http transport needs a host")
        return HttpTransport(args.host, args.port, args.api, args.timeout,
                             auto_page=args.auto_page)
    if args.transport == "file":
        ftp_host = args.ftp_host or args.host
        missing = [n for n, v in (("--ftp-host or a host positional", ftp_host),
                                  ("--ftp-dir", args.ftp_dir),
                                  ("--game-path", args.game_path)) if not v]
        if missing:
            parser.error("the file transport needs " + ", ".join(missing))
        return FileTransport(
            ftp_host=ftp_host, ftp_dir=args.ftp_dir, game_path=args.game_path,
            user=args.ftp_user, password=args.ftp_pass, port=args.ftp_port,
            timeout=args.timeout, hid_cmd=args.hid_cmd, subdir=args.subdir,
            verbose=args.verbose)
    return AppChildTransport(args.bind, args.appchild_port, args.timeout,
                             args.verbose)


def collect_scripts(args, parser):
    if args.batch:
        if args.script or args.file or args.expr:
            parser.error("give --batch, or a script/--file/--expr, not both")
        return list(args.batch)
    if args.file:
        if args.script or args.expr:
            parser.error("give a script argument or --file, not both")
        try:
            with open(args.file, "r", encoding="utf-8") as handle:
                return [handle.read()]
        except OSError as err:
            print("could not read %s: %s" % (args.file, err), file=sys.stderr)
            raise SystemExit(1)
    if args.expr:
        return [args.expr]
    if args.script == "-":
        return [sys.stdin.read()]
    if args.script:
        return [args.script]
    parser.error("need a script argument, --file, --expr, --batch, '-' for "
                 "stdin, or --repl")


def repl(transport, host_label):
    print("connected to %s - one script per line, Ctrl-D to quit" % host_label)
    while True:
        try:
            line = input("dta> ")
        except (EOFError, KeyboardInterrupt):
            print()
            return 0
        if not line.strip():
            continue
        try:
            print(transport.eval(line))
        except (DtaError, ConsoleError) as err:
            print("error: %s" % err, file=sys.stderr)
        except OSError as err:
            print("connection failed: %s" % err, file=sys.stderr)
            return 1


def main(argv=None) -> int:
    parser = build_parser()
    # parse_intermixed_args, not parse_args: the latter matches positionals in
    # contiguous groups, so `dc3_eval.py <host> -p 21070 '{script}'` would die
    # with "unrecognized arguments" once a flag separated the two positionals.
    args = parser.parse_intermixed_args(argv)

    if len(args.args) > 2:
        parser.error("expected at most 'host' and 'script', got %d positional "
                     "arguments -- quote the script" % len(args.args))
    args.host = args.args[0] if args.args else None
    args.script = args.args[1] if len(args.args) > 1 else None

    if args.self_test:
        return self_test()

    if args.print_bootstrap:
        args.transport = "file"
        print(make_transport(args, parser).console_line())
        return 0

    if args.repl:
        if args.script or args.file or args.expr or args.batch:
            parser.error("--repl takes no script")
        return repl(make_transport(args, parser),
                    args.host or args.ftp_host or "console")

    scripts = collect_scripts(args, parser)
    transport = make_transport(args, parser)

    try:
        if args.transport == "appchild":
            transport.wait_for_console()
            try:
                results = transport.eval_batch(scripts)
            finally:
                transport.close()
        elif args.transport == "file":
            results = transport.eval_batch(scripts, raw=args.raw)
        else:
            results = transport.eval_batch(scripts)
    except ConsoleError as err:
        print("error: %s" % err, file=sys.stderr)
        return 2
    except socket.timeout:
        print("error: timed out after %ss - is the game running and past the "
              "boot screen?" % args.timeout, file=sys.stderr)
        return 1
    except (TimeoutError, ConnectionError) as err:
        print("error: %s" % err, file=sys.stderr)
        return 1
    except OSError as err:
        print("error: could not reach the console: %s" % err, file=sys.stderr)
        return 1

    if args.diff:
        native = HttpTransport(args.native_host, args.native_port, "native",
                               args.timeout).eval_batch(scripts)
        delta = list(difflib.unified_diff(
            native, results,
            fromfile="native(%s:%d)" % (args.native_host, args.native_port),
            tofile="console(%s)" % args.transport, lineterm=""))
        print("\n".join(delta) if delta else "identical")
        return 1 if delta else 0

    for result in results:
        print(result)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
    except DtaError as exc:
        sys.stderr.write("error: %s\n" % exc)
        sys.exit(1)
