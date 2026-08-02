# Console DTA-eval hardware validation — live findings
(scratch checkpoint, 2026-08-02)

## 1. Console REACHABILITY: YES (contrary to the premise)

Scan: `nmap -n -Pn -p 21,21070,730,4600,9090 --open 192.168.8.0/24`
Exactly ONE host answered: **192.168.8.180**

    730/tcp   open   (XBDM debug monitor)
    21070/tcp open   (RB3Enhanced HTTP server)
    21/tcp    CLOSED (no FTP server)  <-- gates the DC3 file transport

The stale doc IP 192.168.1.60 is dead; the console is 192.168.8.180.

## 2. Console identity (XBDM, read-only)

    dbgname          -> Jtag
    systeminfo       -> HDD=Enabled  Type=DevKit
                        Platform=Waternoose System=Trinity
                        BaseKrnl=2.0.1888.0 Krnl=2.0.17559.0 XDK=2.0.21076.11
    xbeinfo running  -> name="\Device\Mass1\Games\rb3\default.xex"

So: JTAG/RGH DevKit-type console, currently running **Rock Band 3**, not DC3.

## 3. RB3Enhanced build on the console is TOO OLD for /dta/eval

    Server: RB3Enhanced 0.7-85-gaaf319d-dirty

`aaf319d` is an ANCESTOR of the DTA-eval work on feature/dta-eval-channel:
    bd6959a DTA eval: fix result mis-attribution, CLI arg order, doc size mismatch
    63c5a1a DTA eval: main-thread remote script channel with output capture
    2040f67 PS2 guitar: title-exit watcher
    ...
    aaf319d PS2 guitar: auto-mount at boot     <-- what is actually running

Confirmed empirically: POST /dta/eval on the live console gives
    "Remote end closed connection without response"
i.e. the endpoint does not exist in the running DLL. The `/` index serves fine,
so the HTTP server itself is healthy. => RB3 side CANNOT be validated until the
new DLL is installed.

## 4. DRIVE LIST — ground truth for the `d:\` vs `GAME:\` trap

    drivelist ->
      HDD, USB0, USB1, USBMU0, DVD, CdRom0,
      GAME, D, RB3HDD, RB3USB0, RB3USB1,
      cnt000000f5, cnt00000005, cnt0000000c, cnt00000011

Both `GAME` and `D` really exist as drive names on this console, which is why
the `GAME:\` mistake is so easy to make and so expensive (Debug::Fail, no
"Continue"). `D` is confirmed present -> `d:\` in the .clp probe is a real path.

## 5. XBDM CAN PUSH FILES — the FTP gap has a workaround (GATING ANSWER: YES)

Capability probe (send the command with no args and read the error code;
`400 missing name` == command exists, `407 unknown command` == it does not):

    sendfile -> 400- missing name      SUPPORTED (host -> console upload)
    getfile  -> 400- missing name      SUPPORTED (console -> host download)
    mkdir    -> 400- missing name      SUPPORTED
    delete   -> 400- missing name      SUPPORTED

So even though FTP (port 21) is closed, **there is a working bidirectional file
channel to this console over XBDM/730**. `tools/console/dc3_eval.py -T file`
currently only knows how to push over FTP; on this console that transport cannot
work as written. An `xbdm` sub-transport is the fix.

### Path syntax that actually works (learned the hard way)

    dirlist name="Hdd:\"              OK      <- ONE backslash, trailing slash
    dirlist name="Hdd:\\"             414 access denied
    dirlist name="HDD:"               414 access denied  (needs the backslash)
    dirlist name="\Device\Mass1\Games"  OK    (raw device path also works)
    dirlist name="Mass1:\Games"       414 access denied  (not a drive alias)

### `D:` and `GAME:` are the SAME mount

`dirlist name="D:\"` and `dirlist name="GAME:\"` return byte-identical listings
(currently rb3's folder: default.xex, rb3.ini, gen/, ui/, config/ ...).

This is an important nuance for the `GAME:\`-is-fatal trap: at the *filesystem*
level the two drive names alias to the running title's root. The DC3 failure is
therefore NOT a path-resolution failure -- it is purely DC3's own
`FileIsLocal()` MILO_ASSERT rejecting the literal drive string "game". So `d:\`
and `game:\` name the same directory, and only one of them survives the assert.
Use `d:\`.

### DC3 is installed on this console

    Hdd:\Games\  ->  "Dance Central 1", "Dance Central 2", "Dance Central 3"
    \Device\Mass1\Games\ -> same three, plus rb3/ and the RB track packs

so a DC3 hardware run is possible; it just requires launching DC3 (the console
is currently in RB3).

## 6. RB3Enhanced DLL built from feature/dta-eval-channel HEAD (bd6959a)

Artifact: `/home/free/code/milohax/RB3Enhanced/tools/oss-xbox-build/out/RB3Enhanced.dll`
  size   78208 bytes
  sha256 b350959da407be3a7c081f7e117e2b9cf6f50a4b7f0bc97ea7b1205a8e2440e1
  md5    a0457a2e1bd3a6929a82df73cff4f640

Build: `PATH=/tmp/xbdm/bvenv/bin:$PATH ./tools/oss-xbox-build/build-dll.sh`
One-time dep fix: the preflight hard-fails on a missing `capstone` python module.
The system `python3` here is a 3.10 venv while `pip --user` installs into 3.14,
so `pip install capstone` alone does NOT satisfy it. Make a venv and prepend it:

    python3 -m venv /tmp/bvenv && /tmp/bvenv/bin/pip install capstone
    PATH=/tmp/bvenv/bin:$PATH ./tools/oss-xbox-build/build-dll.sh

Offline gates, all green:
  LZX round-trip byte-identical; xexlint 0 reject / 0 warn (6 pass);
  devkit-signed, ExCryptBnQwBeSigVerify self-verify PASS.
NOT flashed to the console — deploying is a console state change and was left
for the user to authorise.

## 7. **THE DC3 FTP TRANSPORT CANNOT WORK AS DESIGNED** (hardware-derived)

`docs/native/CONSOLE_DTA_EVAL.md` §3 picks "RndConsole + loose `.dta` over FTP"
and §3.1 step 2 just says "make sure the console runs an FTP server (DashLaunch /
FSD / Aurora — standard on RGH)". On this console that assumption is false in a
way that is fatal to the design:

`../xex-patcher/tools/xbox.sh` header, lines 5-6:

    FTP : user/pass xboxftp / xboxftp -- only while the Aurora dashboard is
          running (launching a title unloads Aurora + FTP; cold-reboot to get
          it back).

and that matches the live observation: port 21 is CLOSED right now, while a
title (rb3) is running, and XBDM/730 is open.

The DC3 route needs to push `p.dta` **while DC3 is running**. But running a
title is exactly the condition under which Aurora -- and therefore FTP -- is
gone. So the FTP leg is unavailable precisely when it is needed. This is not
"the console is offline"; it is a design flaw, and it would have burned a
hardware session to discover.

### The fix: use XBDM as the file transport instead of FTP

XBDM survives a title launch (proven: RB3 is running now and 730 answers), and
`sendfile`/`getfile`/`mkdir`/`delete` are all supported (see §5). XBDM is
therefore the correct transport for BOTH legs of the DC3 loop:

    push   p.dta            -> XBDM sendfile
    poll   dc3_done.txt     -> XBDM getfileattributes / getfile
    fetch  dc3_out.txt      -> XBDM getfile

`tools/console/dc3_eval.py -T file` needs an `xbdm` sub-transport alongside its
FTP one. UNIMPLEMENTED as of this writing -- flagged as the top follow-up.

### Path-spelling rules (verified live, they are NOT interchangeable)

    XBDM path commands (dirlist/getfile/sendfile) -> DRIVE-LETTER form: Usb1:\Games\rb3
    magicboot                                     -> NT DEVICE form:    \Device\Mass1\Games\rb3

Verified aliases for the currently-running title's folder -- all four list
byte-identically:

    D:\  ==  GAME:\  ==  Usb1:\Games\rb3  ==  \Device\Mass1\Games\rb3

USB enumeration is not stable across cold boots (the rb3 drive was Usb0, is now
Usb1). Re-check with XBDM `drivelist` after any cold boot.

### Correct console IP already exists in a sibling repo

`../xex-patcher/tools/xbox.sh` already defaults `XBOX=192.168.8.180`. Only the
dc3-decomp docs carry the dead 192.168.1.60.

## 8. XBDM binary file wire format (verified live, read-only)

`getfile` proven working against `Hdd:\name.txt` (22 bytes):

    -> getfileattributes name="Hdd:\name.txt"
    <- 202- multiline response follows
       sizehi=0x0 sizelo=0x16 ...
       .
    -> getfile name="Hdd:\name.txt"
    <- 203- binary response follows
    <- <u32 little-endian length><length bytes of file data>

i.e. the 203 payload carries its OWN 4-byte LE length prefix; do not assume the
stream is bare file bytes (the size from `getfileattributes` is the file size,
which is the value of that prefix, NOT the number of bytes on the wire -- the
wire carries 4 + size). Getting this wrong truncates every download by 4 bytes.

`sendfile name="..." length=N` is the upload mirror (`204- send binary data`,
then write N bytes, then read the status line). Upload was NOT executed --
writing to the console is a state change and was left for the user to
authorise. Upload support is confirmed only at the command level (§5).

## 9. XBDM upload PROVEN (write direction closed, cleanup verified)

Executed against the live console with authorisation, on a collision-proof name:

    exists Hdd:\_dc3probe_smoke.tmp   -> False   (pre-check)
    sendfile ... length=10            -> wrote 10 bytes
    exists                            -> True
    getfile                           -> 10 bytes, b'dc3-smoke\n'
    ROUND-TRIP BYTE-IDENTICAL         -> True
    delete                            -> 200- OK
    exists                            -> False   (cleanup verified, no litter)

**The two directions are ASYMMETRIC — this is the easy bug to write:**

    sendfile: length=N goes in the COMMAND; then write N RAW bytes, no prefix.
    getfile : 203- response, then <u32-LE length><data>. The prefix is ON THE WIRE.

So upload takes no prefix and download emits one. Treating them symmetrically
either corrupts the upload (10 bytes of data become 6 after a bogus 4-byte
header) or truncates every download by 4.

Both directions of the XBDM transport are now hardware-proven. The only
remaining console unknown for DC3 is the RndConsole keypress leg, which needs a
USB keyboard (or `--hid-cmd`) at the console itself.

## 10. Native loopback noise floor — HOLDS EXACTLY (no regression)

Re-measured 2026-08-02 against a fresh `ninja dc3-native` build, AFTER today's
engine fixes (4e4cf851 ObjectDir::Iterate, 8c73183d DataArray::SortNodes,
23727b3c RndText marquee), 5 runs each:

    --dir main               NOISE FLOOR: 0/2421 field cells (0.0%)
    --dir panel:main_panel   NOISE FLOOR: 0/6197 field cells (0.0%)

Both the floor AND the total cell counts are IDENTICAL to the previously
recorded numbers (0/2421 and 0/6197). Every one of the 12 probes reports
unstable=0 churn=0 on both scopes. Nothing regressed.

## 11. `dc3_eval.py` xbdm sub-transport — implemented and hardware-verified

`tools/console/dc3_eval.py` now has a pluggable file channel:

    --file-channel xbdm    (DEFAULT)  debug monitor, TCP 730, survives a title launch
    --file-channel ftp                the old path, kept for a console at the dashboard

The xbdm channel needs no `--ftp-dir`: it addresses files the same way the title
does, so `--game-path` doubles as its root.

    python3 tools/console/dc3_eval.py -T file \
        --ftp-host $DC3_XBOX --game-path 'Hdd1:\Games\DanceCentral3' \
        -e '{ui current_screen}'

Verified against the live console with the SHIPPED `XbdmBackend` class (not an
ad-hoc script): mkdir + 10240-byte put + get round-trip BYTE-IDENTICAL + delete,
cleanup confirmed by re-listing.

Offline coverage: `--self-test` now stands up a real in-process fake XBDM
*socket server* and drives `FileTransport` through it -- single eval, 3-command
batch, a 10240-byte binary round-trip, path joining, and the stale-console
timeout. A socket server rather than a mock specifically because the likely bug
is the asymmetric framing (§9), which a dict-backed mock cannot catch.
Mutation-checked: flipping the client's `<I` length decode to `>I` makes the
self-test fail loudly.

### Gotcha: `delete` will not remove a directory without `dir`

    delete name="Hdd:\_dc3probe"        -> 200- OK   but the directory REMAINS
    delete name="Hdd:\_dc3probe" dir    -> 200- OK   actually removes it

The plain form reports success on a directory and silently leaves it. Anything
that cleans up after itself must pass `dir` and then re-list to confirm --
"200- OK" is not evidence the thing is gone. (Found by re-listing after a probe
run and finding the directory still there.)

Related: a `getfile` of an absent path can come back `414- access denied`
rather than `404- file not found`, so absence must not be detected by matching
on 404 alone.

---

## 12. What is validated, and what ONLY hardware can still close

### Validated against real hardware (2026-08-02)
- Console reachable, identified, title enumerated (XBDM/730).
- XBDM file channel **both directions**, byte-exact, incl. a 10240-byte payload,
  using the shipped `XbdmBackend` class rather than a throwaway script.
- Cleanup/delete semantics, including the directory-delete gotcha (§11).
- `hw_smoke.py` end-to-end, correctly isolating the one genuine failure.
- RB3Enhanced HTTP server alive; `/dta/eval` absent on the installed build.

### Validated offline only (mock / self-test)
- `dc3_eval.py --self-test` — parser, validator, wire encoder, batch splitting,
  truncation-vs-count-mismatch, FTP round-trip, XBDM round-trip.
  The XBDM half runs against a real socket server and is mutation-checked, but
  a fake console is still a fake console.
- `tools/state_diff/tests/` — 66 tests, no engine required.

### Validated against the native port only
- Full state_diff pipeline + a 0/2421 and 0/6197 noise floor (§10). Native is
  not hardware; it shares the probe library, not the runtime.

### STILL UNVALIDATED — needs hardware, and nothing here substitutes
1. **RB3 `/dta/eval` against a console.** Blocked on installing the
   `bd6959a` DLL (§6). Until then the entire RB3E DTA path -- batching,
   the 16KB/32KB caps, the truncation banner, result attribution -- has never
   executed on a console. The PC-side client is exercised; the DLL side is not.
2. **The DC3 RndConsole keypress leg.** Needs DC3 booted from its debug XEX and
   a USB keyboard (or `--hid-cmd`) at the console. XBDM moves the files, but
   nothing has yet made DC3 *run* `p.dta`.
3. **The `.clp` drive probe actually writing.** `d:\` is confirmed to exist and
   to alias the title root, but no DC3 build has been made to write there. The
   `game:\` fatality remains inferred from the decompiled assert, not observed.
4. **`--game-path` spelling for DC3**, called out as "the #1 hardware unknown"
   in CONSOLE_DTA_EVAL.md §3.1. Narrowed but not closed: the *form*
   (`Usb1:\Games\<title>`) is now verified for the rb3 install, and DC3 is
   present at `Hdd:\Games\Dance Central 3` / `\Device\Mass1\Games\Dance Central 3`.
5. **End-to-end console-vs-native state diff.** Everything upstream is ready on
   both sides; it has never been run across the two.
