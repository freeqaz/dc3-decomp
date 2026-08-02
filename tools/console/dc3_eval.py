#!/usr/bin/env python3
"""Evaluate DTA script against a running Dance Central 3 engine and print the reply.

Three transports, all driving the *same* DTA text so the results are directly
comparable:

  http      HTTP DTA-eval endpoint.  Covers both the native port
            (POST /api/dta/eval, docs/tools/HTTP_DEBUG_SERVER.md) and an
            RB3Enhanced-style console DLL (GET /execute?script=...).
            Request/response, fully non-interactive.  Preferred when available.

  file      Real console, *no binary modification*.  The script is pushed to the
            console over FTP as a loose .dta, the game's RndConsole runs
            `{run "<drive>:\\...\\p.dta"}`, the script writes its answer to a file
            next to the XEX, and this tool pulls that file back over FTP.
            The only step this tool cannot do for you is the keypress that
            re-runs `{run ...}` (see --hid-cmd).

  appchild  Raw AppChild command channel (console dials out to TCP 4543 and
            executes serialised DataArrays).  Command injection only -- the wire
            protocol carries no result -- so it is paired with the `file`
            transport's return path.  Requires launch arguments; see
            docs/native/CONSOLE_DTA_EVAL.md.

See docs/native/CONSOLE_DTA_EVAL.md for activation steps and the honest list of
hardware unknowns.
"""

from __future__ import annotations

import argparse
import difflib
import ftplib
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

# Symbols that DataArray::Load (src/system/obj/DataArray.cpp:497-506) will
# expand from the macro table instead of leaving alone.  Never emit these bare.
MACRO_SYMBOLS = {"HX_XBOX", "HX_WIN", "HX_NG"}


class DtaError(Exception):
    pass


# --------------------------------------------------------------------------
# Minimal DTA reader.  Produces ("kind", payload) tuples mirroring DataNode.
# --------------------------------------------------------------------------

_TOKEN_END = set(" \t\r\n(){}[]")


def parse_dta(text: str):
    """Parse DTA source into a list of top-level nodes."""
    nodes, pos = _parse_seq(text, 0, None)
    if pos != len(text):
        raise DtaError("unbalanced closing delimiter at offset %d" % pos)
    return nodes


_CLOSERS = {"(": ")", "{": "}", "[": "]"}
_KINDS = {"(": kDataArray, "{": kDataCommand, "[": kDataProperty}


def _parse_seq(text: str, pos: int, closer):
    out = []
    n = len(text)
    while True:
        # whitespace and comments
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
        # bare token
        start = pos
        while pos < n and text[pos] not in _TOKEN_END and text[pos] not in '"\'':
            pos += 1
        tok = text[start:pos]
        if not tok:
            raise DtaError("empty token at offset %d" % start)
        out.append(_classify(tok))
    # unreachable


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
# NetStream is constructed BinStream(true) (NetStream.cpp:8) => little endian
# on the wire (ReadEndian byte-swaps on the big-endian console).
#
# DataArray::Load  (DataArray.cpp:488-...):  s16 size, s16 line, s16 deprecated,
#                                            then `size` DataNodes
# DataNode::Load   (DataNode.cpp:727-...):   s32 type, then per-type payload
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

    api="native"  -> POST /api/dta/eval, JSON envelope (native port)
    api="execute" -> GET  /execute?script=... , plain text (DC3Enhanced DLL)
    api="auto"    -> try native, fall back to execute
    """

    def __init__(self, host: str, api: str = "auto", timeout: float = 10.0):
        if "://" not in host:
            host = "http://" + host
        self.base = host.rstrip("/")
        self.api = api
        self.timeout = timeout

    def _native(self, script: str) -> str:
        req = urllib.request.Request(
            self.base + "/api/dta/eval",
            data=script.encode("utf-8"),
            method="POST",
            headers={"Content-Type": "text/plain"},
        )
        with urllib.request.urlopen(req, timeout=self.timeout) as resp:
            body = resp.read().decode("utf-8", "replace")
        try:
            doc = json.loads(body)
        except ValueError:
            return body.strip()
        if isinstance(doc, dict) and "data" in doc:
            data = doc["data"]
            if isinstance(data, dict) and "value" in data:
                return str(data["value"])
            return json.dumps(data)
        return body.strip()

    def _execute(self, script: str) -> str:
        url = self.base + "/execute?" + urllib.parse.urlencode({"script": script})
        with urllib.request.urlopen(url, timeout=self.timeout) as resp:
            return resp.read().decode("utf-8", "replace").strip()

    def eval(self, script: str) -> str:
        if self.api == "native":
            return self._native(script)
        if self.api == "execute":
            return self._execute(script)
        try:
            return self._native(script)
        except (urllib.error.HTTPError, urllib.error.URLError, OSError):
            return self._execute(script)


# --------------------------------------------------------------------------
# Transport: file (RndConsole + FTP)
# --------------------------------------------------------------------------

PROBE_NAME = "p.dta"
OUT_NAME = "dc3_out.txt"
DONE_NAME = "dc3_done.txt"


def build_probe(script: str, token: str, out_name: str = OUT_NAME,
                done_name: str = DONE_NAME, raw: bool = False) -> str:
    """Wrap a user script so its value lands in a file the PC can fetch.

    {sprint <expr>}                 -> DataFunc.cpp:72 (DataSprint), stringifies
    {write_string_to_file p s 0}    -> DataFunc.cpp:1088 (OnWriteStringToFile);
                                       the 4th arg MUST be 0, otherwise the
                                       stream is opened in kAppend.
    The paths are deliberately *relative*: FileIsLocal() MILO_ASSERTs on a
    "game" drive (src/system/os/File_Win.cpp:9-13), and a relative write goes
    through AsyncFile::New straight to the title's working directory, i.e. the
    folder the XEX was launched from.
    """
    if raw:
        body = script
    else:
        body = ("{write_string_to_file \"%s\" {sprint {do %s}} 0}"
                % (out_name, script))
    return ("; generated by tools/console/dc3_eval.py -- do not edit\n"
            "{do\n"
            "   %s\n"
            "   {write_string_to_file \"%s\" \"%s\" 0}\n"
            "}\n" % (body, done_name, token))


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
        token = "dc3-%d" % (time.time_ns() & 0xFFFFFFFF)
        probe = build_probe(script, token, raw=raw)
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
            sys.stderr.write("pushed %s/%s (%d bytes)\n"
                             % (remote_dir, PROBE_NAME, len(probe)))

        self._trigger()

        deadline = time.time() + self.timeout
        last_err = None
        while time.time() < deadline:
            time.sleep(self.poll)
            try:
                ftp = self._connect()
            except OSError as exc:  # console busy / FTP server single-session
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
                    return self._get(ftp, out_path).decode("utf-8", "replace").strip()
                except ftplib.all_errors:
                    # script ran but produced no output file
                    return ""
            finally:
                try:
                    ftp.quit()
                except ftplib.all_errors:
                    pass
        raise TimeoutError(
            "no %s with token %s after %.0fs (last FTP error: %s).  Did the "
            "console run %s ?" % (DONE_NAME, token, self.timeout, last_err,
                                  self.console_line()))

    def _trigger(self):
        if self.hid_cmd:
            if self.verbose:
                sys.stderr.write("trigger: %s\n" % self.hid_cmd)
            subprocess.run(self.hid_cmd, shell=True, check=False)
            return
        sys.stderr.write(
            "\n  >>> On the console keyboard: ESC, Up-arrow, Enter, ESC\n"
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
        """Send one command.  Returns nothing -- the channel has no reply."""
        frame = encode_command(script)
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
            store["/g/" + OUT_NAME] = b"song_select_screen"
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
        got = tr.eval("{ui current_screen}")
        if got != "song_select_screen":
            failures.append("file round-trip got %r" % got)
        if "{sprint {do {ui current_screen}}}" not in store["/g/dc3/p.dta"].decode():
            failures.append("pushed probe wrong:\n%s" % store["/g/dc3/p.dta"].decode())

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

    # {sync_app_child} : 1 node, kDataSymbol
    frame = encode_command("{sync_app_child}")
    want = (struct.pack("<hhh", 1, 0, 0)
            + struct.pack("<i", kDataSymbol)
            + struct.pack("<i", len("sync_app_child"))
            + b"sync_app_child")
    check("encode sync", frame, want)

    # {print "x"} : 2 nodes
    frame = encode_command('{print "x"}')
    want = (struct.pack("<hhh", 2, 0, 0)
            + struct.pack("<i", kDataSymbol) + struct.pack("<i", 5) + b"print"
            + struct.pack("<i", kDataString) + struct.pack("<i", 1) + b"x")
    check("encode print", frame, want)

    # nested command
    frame = encode_command("{set $x {+ 1 2}}")
    want = (struct.pack("<hhh", 3, 0, 0)
            + struct.pack("<i", kDataSymbol) + struct.pack("<i", 3) + b"set"
            + struct.pack("<i", kDataVar) + struct.pack("<i", 1) + b"x"
            + struct.pack("<i", kDataCommand)
            + struct.pack("<hhh", 3, 0, 0)
            + struct.pack("<i", kDataSymbol) + struct.pack("<i", 1) + b"+"
            + struct.pack("<i", kDataInt) + struct.pack("<i", 1)
            + struct.pack("<i", kDataInt) + struct.pack("<i", 2))
    check("encode nested", frame, want)

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

    probe = build_probe("{+ 1 2}", "tok1")
    for needle in ("{sprint {do {+ 1 2}}}", '"dc3_out.txt"',
                   '"dc3_done.txt" "tok1" 0'):
        if needle not in probe:
            failures.append("probe missing %r:\n%s" % (needle, probe))

    failures.extend(_self_test_file_transport())

    if failures:
        for f in failures:
            print("FAIL " + f)
        print("\n%d failure(s)" % len(failures))
        return 1
    print("self-test OK")
    return 0


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def read_script(args) -> str:
    if args.expr is not None:
        return args.expr
    if args.file == "-":
        return sys.stdin.read()
    with open(args.file, "r", encoding="utf-8") as fh:
        return fh.read()


def build_parser():
    p = argparse.ArgumentParser(
        prog="dc3_eval.py",
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    src = p.add_mutually_exclusive_group()
    src.add_argument("-e", "--expr", help="DTA script text")
    src.add_argument("-f", "--file", help="read DTA script from a file ('-' = stdin)")

    p.add_argument("-t", "--transport", default="http",
                   choices=("http", "file", "appchild"))
    p.add_argument("--timeout", type=float, default=20.0,
                   help="seconds to wait for a reply (default 20)")
    p.add_argument("-v", "--verbose", action="store_true")
    p.add_argument("--self-test", action="store_true",
                   help="run offline unit tests and exit")
    p.add_argument("--print-bootstrap", action="store_true",
                   help="print the one-time RndConsole line and exit")
    p.add_argument("--raw", action="store_true",
                   help="file transport: push the script verbatim, do not wrap "
                        "it in {sprint}/{write_string_to_file}")

    g = p.add_argument_group("http transport")
    g.add_argument("--host", default=os.environ.get("DC3_HTTP_HOST", "localhost:9090"),
                   help="host:port of the DTA-eval HTTP server "
                        "(default localhost:9090 = native port)")
    g.add_argument("--api", default="auto", choices=("auto", "native", "execute"),
                   help="native = POST /api/dta/eval, execute = GET /execute?script=")

    g = p.add_argument_group("file transport (console over FTP)")
    g.add_argument("--ftp-host", default=os.environ.get("DC3_XBOX"),
                   help="console IP running an FTP server (DashLaunch/FSD/Aurora)")
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
                   help="shell command that presses ESC/Up/Enter/ESC on the "
                        "console (e.g. a USB-HID bridge). If unset, this tool "
                        "prompts you to do it by hand.")

    g = p.add_argument_group("appchild transport")
    g.add_argument("--bind", default="0.0.0.0")
    g.add_argument("--port", type=int, default=APPCHILD_PORT)

    g = p.add_argument_group("comparison")
    g.add_argument("--diff", action="store_true",
                   help="run the script on BOTH the selected transport and the "
                        "native port (--native-host), then unified-diff them")
    g.add_argument("--native-host", default="localhost:9090")
    return p


def require(args, *names):
    missing = [n for n in names if not getattr(args, n.replace("-", "_"))]
    if missing:
        raise SystemExit("error: --%s required for this transport"
                         % " --".join(missing))


def make_file_transport(args):
    require(args, "ftp-host", "ftp-dir", "game-path")
    return FileTransport(
        ftp_host=args.ftp_host, ftp_dir=args.ftp_dir, game_path=args.game_path,
        user=args.ftp_user, password=args.ftp_pass, port=args.ftp_port,
        timeout=args.timeout, hid_cmd=args.hid_cmd, subdir=args.subdir,
        verbose=args.verbose)


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)

    if args.self_test:
        return self_test()

    if args.print_bootstrap:
        print(make_file_transport(args).console_line())
        return 0

    if args.expr is None and args.file is None:
        raise SystemExit("error: one of -e/--expr or -f/--file is required")
    script = read_script(args).strip()
    if not script:
        raise SystemExit("error: empty script")

    if args.transport == "http":
        result = HttpTransport(args.host, args.api, args.timeout).eval(script)
    elif args.transport == "file":
        result = make_file_transport(args).eval(script, raw=args.raw)
    else:
        tr = AppChildTransport(args.bind, args.port, args.timeout, args.verbose)
        try:
            tr.wait_for_console()
            tr.send(script)
            ack = tr.sync()
            sys.stderr.write("frame released (ack=%d)\n" % ack)
        finally:
            tr.close()
        result = ("<appchild has no return path -- use --transport file, or a "
                  "{write_string_to_file ...} in your script>")

    if args.diff:
        native = HttpTransport(args.native_host, "native", args.timeout).eval(script)
        delta = difflib.unified_diff(
            native.splitlines(), result.splitlines(),
            fromfile="native(%s)" % args.native_host,
            tofile="console(%s)" % args.transport, lineterm="")
        out = "\n".join(delta)
        print(out if out else "identical")
        return 0 if not out else 1

    print(result)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
    except (DtaError, TimeoutError, ConnectionError) as exc:
        sys.stderr.write("error: %s\n" % exc)
        sys.exit(2)
