#!/usr/bin/env python3
"""Hardware smoke test for the console DTA-eval chain.

One command, cheapest checks first, that says in under a minute whether the
PC -> console channel works and, if it does not, WHICH link is broken.

    python3 tools/console/hw_smoke.py                  # uses $DC3_XBOX
    python3 tools/console/hw_smoke.py 192.168.8.180
    python3 tools/console/hw_smoke.py --write          # also test XBDM upload
                                                       # (writes+deletes a temp file)

Stages, in ascending cost:

    [0] TCP reachability      730 / 21070 / 21   -- ~1s
    [1] XBDM identity         which title is actually running
    [2] XBDM getfile          binary channel, read-only
    [3] RB3Enhanced /dta/eval {print "hi"}
    [4] DC3 .clp drive probe  the zero-cost drive-spelling check

Every failure is reported as one of:
    UNREACHABLE   no route / no host answering at all
    CLOSED        host answered but refused the port (RST)
    FILTERED      host swallowed the SYN (timeout) -- firewall or nothing bound
    NO-RESPONSE   TCP connected, then the peer hung up or said nothing
    ERROR         the service answered and reported a failure (message included)

Exit status: 0 = every applicable stage passed, 1 = something failed.

See docs/native/CONSOLE_HW_FINDINGS.md for the hardware ground truth this
encodes, and docs/native/CONSOLE_DTA_EVAL.md for the protocol design.
"""

import argparse
import errno
import os
import socket
import struct
import sys

XBDM_PORT = 730
RB3E_PORT = 21070
FTP_PORT = 21

GREEN, RED, YELLOW, DIM, RESET = (
    ("\033[32m", "\033[31m", "\033[33m", "\033[2m", "\033[0m")
    if sys.stdout.isatty() else ("", "", "", "", "")
)

_fail_count = 0


def ok(stage, msg):
    print("  %sPASS%s %-14s %s" % (GREEN, RESET, stage, msg))


def bad(stage, kind, msg, fix=None):
    global _fail_count
    _fail_count += 1
    print("  %sFAIL%s %-14s %s: %s" % (RED, RESET, stage, kind, msg))
    if fix:
        for line in fix.strip("\n").split("\n"):
            print("       %s%s%s" % (DIM, line, RESET))


def skip(stage, msg):
    print("  %sSKIP%s %-14s %s" % (YELLOW, RESET, stage, msg))


# ---------------------------------------------------------------------------
# stage 0 -- TCP reachability, with the four failure modes kept distinct
# ---------------------------------------------------------------------------

def probe_port(host, port, timeout=3.0):
    """Return (state, detail) where state is open/CLOSED/FILTERED/UNREACHABLE."""
    s = socket.socket()
    s.settimeout(timeout)
    try:
        s.connect((host, port))
        return "open", ""
    except socket.timeout:
        return "FILTERED", "no SYN-ACK within %.0fs" % timeout
    except OSError as exc:
        if exc.errno == errno.ECONNREFUSED:
            return "CLOSED", "connection refused (host is up, nothing listening)"
        if exc.errno in (errno.EHOSTUNREACH, errno.ENETUNREACH):
            return "UNREACHABLE", os.strerror(exc.errno)
        return "UNREACHABLE", str(exc)
    finally:
        s.close()


# ---------------------------------------------------------------------------
# XBDM
# ---------------------------------------------------------------------------

class XbdmError(Exception):
    pass


class Xbdm(object):
    """Minimal XBDM client. Survives a title launch, unlike FTP."""

    def __init__(self, host, port=XBDM_PORT, timeout=8):
        self.s = socket.create_connection((host, port), timeout)
        self.s.settimeout(timeout)
        self.f = self.s.makefile("rwb")
        banner = self.f.readline()
        if not banner.startswith(b"201"):
            raise XbdmError("unexpected banner %r" % banner)

    def cmd(self, c):
        """Send a command. Returns (status_line, [lines]) for 200/202."""
        self.f.write(c.encode("latin-1") + b"\r\n")
        self.f.flush()
        status = self.f.readline().decode("latin-1").strip()
        if not status:
            raise XbdmError("peer hung up during %r" % c)
        if status.startswith("202"):
            lines = []
            while True:
                ln = self.f.readline()
                if not ln:
                    raise XbdmError("truncated multiline response")
                ln = ln.decode("latin-1").rstrip("\r\n")
                if ln == ".":
                    break
                lines.append(ln)
            return status, lines
        return status, []

    def getfile(self, path):
        """Download. The 203 payload carries its own u32-LE length prefix."""
        self.f.write(b'getfile name="' + path.encode("latin-1") + b'"\r\n')
        self.f.flush()
        status = self.f.readline().decode("latin-1").strip()
        if not status.startswith("203"):
            raise XbdmError(status)
        raw = self.f.read(4)
        if len(raw) != 4:
            raise XbdmError("short length prefix")
        (n,) = struct.unpack("<I", raw)
        data = self.f.read(n)
        if len(data) != n:
            raise XbdmError("short read: %d of %d" % (len(data), n))
        return data

    def sendfile(self, path, data):
        """Upload. STATE-CHANGING."""
        self.f.write(b'sendfile name="' + path.encode("latin-1")
                     + b'" length=%d\r\n' % len(data))
        self.f.flush()
        status = self.f.readline().decode("latin-1").strip()
        if not status.startswith("204"):
            raise XbdmError(status)
        self.f.write(data)
        self.f.flush()
        status = self.f.readline().decode("latin-1").strip()
        if not status.startswith("200"):
            raise XbdmError(status)

    def exists(self, path):
        status, _ = self.cmd('getfileattributes name="%s"' % path)
        return status.startswith("202")

    def close(self):
        try:
            self.f.write(b"bye\r\n")
            self.f.flush()
        except Exception:
            pass
        try:
            self.s.close()
        except Exception:
            pass


# ---------------------------------------------------------------------------
# RB3Enhanced HTTP
# ---------------------------------------------------------------------------

def http(host, port, method, path, body=None, timeout=8):
    """Tiny HTTP/1.0 client. Returns (code, headers_text, body_bytes)."""
    req = "%s %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n" % (method, path, host)
    if body is not None:
        req += "Content-Length: %d\r\nContent-Type: text/plain\r\n" % len(body)
    req += "\r\n"
    s = socket.create_connection((host, port), timeout)
    s.settimeout(timeout)
    try:
        s.sendall(req.encode() + (body or b""))
        chunks = []
        while True:
            b = s.recv(65536)
            if not b:
                break
            chunks.append(b)
    finally:
        s.close()
    raw = b"".join(chunks)
    if not raw:
        raise IOError("connected, but the peer closed without sending anything")
    head, _, payload = raw.partition(b"\r\n\r\n")
    head = head.decode("latin-1", "replace")
    try:
        code = int(head.split(" ", 2)[1])
    except (IndexError, ValueError):
        raise IOError("malformed response head: %r" % head[:80])
    return code, head, payload


# ---------------------------------------------------------------------------

DLL = ("/home/free/code/milohax/RB3Enhanced/tools/oss-xbox-build/out/"
       "RB3Enhanced.dll")

CLP_PROBE = ('{new SkeletonClip $probe}'
             '{$probe xbox_start_record "d:\\probe.clp"}'
             '{$probe stop_recording}')


def main():
    ap = argparse.ArgumentParser(
        description="Hardware smoke test for the console DTA-eval chain.")
    ap.add_argument("host", nargs="?", default=os.environ.get("DC3_XBOX"),
                    help="console IP (default: $DC3_XBOX)")
    ap.add_argument("--write", action="store_true",
                    help="also round-trip an XBDM upload (writes then deletes "
                         "a temp file on the console)")
    ap.add_argument("--clp-path", default="d:\\probe.clp",
                    help="where the DC3 .clp drive probe should land")
    args = ap.parse_args()

    if not args.host:
        ap.error("no host: pass one, or export DC3_XBOX=<console ip>")
    host = args.host

    print("== console smoke test: %s ==" % host)

    # ---- [0] reachability -------------------------------------------------
    print("\n[0] TCP reachability")
    states = {}
    for name, port in (("xbdm", XBDM_PORT), ("rb3e", RB3E_PORT), ("ftp", FTP_PORT)):
        st, detail = probe_port(host, port)
        states[name] = st
        if st == "open":
            ok("port %d" % port, "%s: open" % name)
        elif name == "ftp":
            # Expected while a title is running: Aurora (and its FTP) is gone.
            skip("port %d" % port, "ftp: %s -- normal while a title is running "
                                   "(Aurora owns FTP)" % st)
        else:
            bad("port %d" % port, st, detail,
                fix="Is the console powered on and on this LAN?\n"
                    "  nmap -n -Pn -p 21,730,21070 --open %s/24"
                    % host.rsplit(".", 1)[0] + ".0")

    if states["xbdm"] != "open":
        print("\n%sXBDM is down -- nothing further can be checked.%s" % (RED, RESET))
        return 1

    # ---- [1] identity -----------------------------------------------------
    print("\n[1] XBDM identity")
    running = ""
    try:
        x = Xbdm(host)
    except (OSError, XbdmError) as exc:
        bad("connect", "NO-RESPONSE", str(exc))
        return 1
    try:
        _, info = x.cmd("systeminfo")
        st, name = x.cmd("dbgname")
        ok("dbgname", st.split("- ", 1)[-1])
        ok("systeminfo", "; ".join(info))
        st, xbe = x.cmd("xbeinfo running")
        running = next((l.split('name="')[1].rstrip('"')
                        for l in xbe if 'name="' in l), "")
        ok("running title", running or "(none reported)")
        st, drives = x.cmd("drivelist")
        names = [l.split('drivename="')[1].rstrip('"')
                 for l in drives if 'drivename="' in l]
        ok("drivelist", " ".join(names))
        if "D" not in names:
            bad("drive D:", "ERROR",
                "no drive named 'D' -- the .clp probe path d:\\ will not resolve",
                fix="Pick another non-'game' drive from the list above.")
    except XbdmError as exc:
        bad("xbdm", "ERROR", str(exc))

    # ---- [2] binary channel ----------------------------------------------
    print("\n[2] XBDM file channel")
    try:
        data = x.getfile("Hdd:\\name.txt")
        ok("getfile", "downloaded Hdd:\\name.txt (%d bytes)" % len(data))
    except (XbdmError, OSError) as exc:
        bad("getfile", "ERROR", str(exc))

    if args.write:
        tmp = "Hdd:\\dc3_smoke.tmp"
        payload = b"dc3-smoke\n"
        try:
            x.sendfile(tmp, payload)
            back = x.getfile(tmp)
            if back == payload:
                ok("sendfile", "upload/download round-trip byte-identical")
            else:
                bad("sendfile", "ERROR", "round-trip mismatch: %r" % back[:32])
            x.cmd('delete name="%s"' % tmp)
        except (XbdmError, OSError) as exc:
            bad("sendfile", "ERROR", str(exc))
    else:
        skip("sendfile", "upload not tested (pass --write to include it)")

    is_dc3 = "dance central 3" in running.lower() or "\\dc3\\" in running.lower()
    is_rb3 = "rb3" in running.lower() or "rock band" in running.lower()

    # ---- [3] RB3Enhanced /dta/eval ---------------------------------------
    print("\n[3] RB3Enhanced /dta/eval")
    if states["rb3e"] != "open":
        skip("dta/eval", "port %d %s (RB3Enhanced not loaded / not in RB3)"
             % (RB3E_PORT, states["rb3e"]))
    else:
        version = "?"
        try:
            code, head, _ = http(host, RB3E_PORT, "GET", "/")
            version = next((l.split(":", 1)[1].strip()
                            for l in head.split("\r\n")
                            if l.lower().startswith("server:")), "?")
            ok("http server", "%s (HTTP %d)" % (version, code))
        except (IOError, OSError) as exc:
            bad("http server", "NO-RESPONSE", str(exc))

        try:
            code, _, payload = http(host, RB3E_PORT, "POST", "/dta/eval",
                                    b'{print "hi"}')
            text = payload.decode("latin-1", "replace").strip()
            if code == 200 and not text.startswith("!!"):
                ok("dta/eval", "%r" % text[:120])
            elif code == 404:
                bad("dta/eval", "ERROR",
                    "404 -- this DLL has no /dta/eval endpoint (version %s)"
                    % version, fix=_install_hint())
            else:
                bad("dta/eval", "ERROR", "HTTP %d: %s" % (code, text[:200]))
        except IOError as exc:
            # The pre-DTA-eval DLL closes the socket on an unknown route.
            bad("dta/eval", "NO-RESPONSE",
                "%s (version %s)" % (exc, version), fix=_install_hint())
        except OSError as exc:
            bad("dta/eval", "NO-RESPONSE", str(exc))

    # ---- [4] DC3 .clp drive probe ----------------------------------------
    print("\n[4] DC3 .clp drive probe")
    if not is_dc3:
        skip("clp probe", "running title is not DC3 (%s)"
             % (running or "unknown"))
        print("       %sTo run it: boot DC3's debug XEX, then at the RndConsole "
              "(Esc) type:%s" % (DIM, RESET))
        print("       %s  %s%s" % (DIM, CLP_PROBE, RESET))
        print("       %sthen re-run this script -- stage 4 verifies the file "
              "landed.%s" % (DIM, RESET))
    else:
        print("       At the DC3 RndConsole (Esc), type:")
        print("         %s" % CLP_PROBE)
        try:
            input("       press Enter here once you have run it... ")
        except EOFError:
            pass
        probe_on_disk = args.clp_path.replace("d:\\", "D:\\")
        try:
            if x.exists(probe_on_disk):
                ok("clp probe", "%s exists -- drive spelling is correct"
                   % args.clp_path)
            else:
                bad("clp probe", "ERROR", "%s was not created" % args.clp_path,
                    fix="If the title DIED, you used a forbidden drive: "
                        "FileIsLocal()\nMILO_ASSERTs on drive 'game' and "
                        "Debug::Fail exits with no Continue.\nUse d:\\ -- it "
                        "names the same folder as game:\\ .")
        except (XbdmError, OSError) as exc:
            bad("clp probe", "ERROR", str(exc))

    x.close()

    print("\n== %s ==" % ("all applicable stages passed" if not _fail_count
                          else "%d stage(s) FAILED" % _fail_count))
    return 1 if _fail_count else 0


def _install_hint():
    return (
        "Build and install the DTA-eval DLL:\n"
        "  cd ../RB3Enhanced && git checkout feature/dta-eval-channel\n"
        "  python3 -m venv /tmp/bvenv && /tmp/bvenv/bin/pip install capstone\n"
        "  PATH=/tmp/bvenv/bin:$PATH ./tools/oss-xbox-build/build-dll.sh\n"
        "  # -> " + DLL + "\n"
        "Deploy (TOUCHES THE CONSOLE -- reboots to Aurora, then relaunches):\n"
        "  ../xex-patcher/tools/xbox.sh redeploy " + DLL + "\n"
        "Note: deploy needs FTP, which only exists while Aurora is running;\n"
        "'redeploy' boots Aurora for you first."
    )


if __name__ == "__main__":
    sys.exit(main())
