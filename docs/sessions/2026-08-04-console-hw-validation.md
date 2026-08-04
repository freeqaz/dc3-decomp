# 2026-08-04 — Console hardware validation of the RB3Enhanced DTA-eval path

Console: `192.168.8.180` (Jtag / DevKit / Waternoose-Trinity, Krnl 2.0.17559.0, XDK 2.0.21076.11).
Written incrementally — a previous agent in this lane died mid-run and lost its state.

## Status legend
`VERIFIED` = observed on hardware. `ASSUMED` = reasoned, not observed. `SKIPPED` = not run, with reason.

---

## Step 0 — pre-flight (VERIFIED)

`python3 tools/console/hw_smoke.py 192.168.8.180`:

- PASS TCP 730 (XBDM), PASS TCP 21 (FTP)
- FAIL TCP 21070 — expected: no title running, so RB3Enhanced is not loaded
- Running title: `\Device\Harddisk0\Partition1\Apps\Aurora\Aurora\Aurora.xex` (Aurora dashboard)
- drivelist: `HDD USB0 USB1 USBMU0 DVD CdRom0 GAME Hdd1 D USBMUCache0 System`

**The console is in the SAFE state**: Aurora is up, no title, no RB3E, no live kernel patches.

Drive check: `/Usb0/Games/rb3` does not exist; `/Usb1/Games/rb3` does. `xbox.sh` defaults
(`RB3_FTP=/Usb1/Games/rb3`, `RB3_DEV=\Device\Mass1\Games\rb3`) are correct for this boot.

---

## Step 1 — `xbox.sh` step-by-step safety map (VERIFIED by reading)

The script the briefing calls `xbox.sh` is **not** in RB3Enhanced. It lives at
`/home/free/code/milohax/xex-patcher/tools/xbox.sh` (152 lines).

`redeploy <dll>` is four steps:

| Step | Command | Safe? |
|---|---|---|
| [1/4] `aurora` | `magicboot title=\Device\Harddisk0\...\Aurora.xex directory=...` | **DANGEROUS — this is the step that wedged the console.** |
| [2/4] `wait-ftp` | polls FTP up to 120s | Safe (read-only poll, bounded) |
| [3/4] `deploy` | FTP `put` + sha verify | Safe (needs Aurora/FTP up, which it already is) |
| [4/4] `launch` | `magicboot title=\Device\Mass1\Games\rb3\default.xex` | Conditionally safe — see below |

### Root cause of the wedge — found in-tree, better than the briefing's hypothesis

`RB3Enhanced/source/xbox360_vcontroller.c:1064-1090` documents it. It is **not** DashLaunch:

> Our far-detours patch kernel .text (in xboxkrnl.exe/xam.xex, modules that stay resident across
> title switches) to branch INTO this DLL, which is loaded at 0x84000000 as part of RB3's OWN
> process. If RB3 exits or relaunches while those branches are still live [...] the FIRST call into
> a hooked kernel function after our DLL's memory is reclaimed jumps into freed/repurposed memory.
> That is a KERNEL-MODE fault, which is why it wedges the WHOLE console's network stack rather than
> just crashing our title, and why only a power cycle recovers it.
>
> Reproduced + isolated 2026-07-19: magicboot RB3->Aurora and RB3->RB3 both wedged the network while
> RB3E (with these detours/patches installed) was loaded; the SAME magicboot RB3->Aurora succeeded
> immediately the one time RB3 booted vanilla (RB3E not loaded, no patches).

So the hazard is **magicbooting OUT of a running RB3 that has RB3E loaded**, in either direction.
Magicbooting **INTO** RB3 from Aurora (no patches live yet) is the direction that has always worked.

Consequences for this run:
- Steps [1/4] and [2/4] are **unnecessary** — Aurora is already up and FTP already answers. Skipping
  them removes the wedging step from the procedure entirely.
- Step [3/4] deploy: run directly.
- Step [4/4] launch (Aurora -> RB3): the safe direction; run it.
- To leave RB3 afterwards: `magicboot` soft/cold (full console reboot) per the user's instruction,
  never `magicboot title=`.

---

## Step 2 — artifact identity (DISCREPANCY, resolved)

The briefing describes the artifact as **78208 bytes, sha256 `b350959d...`, from commit `bd6959a`**.
What is actually on disk at
`/home/free/code/milohax/RB3Enhanced/tools/oss-xbox-build/out/RB3Enhanced.dll`:

| | briefing | on disk |
|---|---|---|
| size | 78208 | **78210** |
| sha256 | `b350959d...` | **`068f3867 55297295 22835184 2aa639c0 fbd6449a 362d7ba9 1d8d7c0f 93f46989`** |
| mtime | — | Aug 3 00:15 |

It is a **rebuild**, not the exact binary the briefing measured. Content was verified instead of
trusting the hash — the packed DLL is LZX-compressed so `strings` finds nothing; checked the
unpacked link output `tools/oss-xbox-build/K-link/RB3Enhanced.exe` (same Aug 3 00:15 mtime):

```
AllowScripts
!! output truncated, raise RB3E_DTA_OUTPUT_MAX or split the script
=> !! refused: bad command pointer
scripts are disabled (set AllowScripts=true under [HTTP])
413 Payload Too Large
409 Conflict
/dta/eval
```

`=> !! refused: bad command pointer` is the string introduced by `bd6959a` (defect 1, result
mis-attribution), so **the build does contain the commit under test**. Repo HEAD is `23a8737`
(docs-only, on top of `bd6959a`). Treating the on-disk DLL as the artifact.

---

## Step 3 — backup (VERIFIED)

Pulled the currently-installed DLL off the console over FTP **before** touching anything:

```
/home/free/console-backups/RB3Enhanced.dll.console-backup-20260804
75776 bytes  sha256 d30285679adda187aca26a11d0574d8a092e21b896d1ce58af7fcca365b8f1f5
```

(on-console mtime was Aug 2 20:18). Rollback = FTP `put` this file back to
`/Usb1/Games/rb3/RB3Enhanced.dll`. The drive also already holds 9 older `RB3Enhanced_*.dll`
snapshots.

`rb3.ini` on the console (`/Usb1/Games/rb3/rb3.ini`) already has the two required flags —
**no ini edit needed**:

```ini
[HTTP]
EnableHTTPServer=true
AllowScripts=true
AllowCORS=true
```

---

## Test contract being validated

From `RB3Enhanced/docs/DTA-EVAL.md` + `source/DTAEval.c` + `include/DTAEval.h`:

- `RB3E_DTA_SCRIPT_MAX = 16384`; `net_http_server.c:346` uses `content_length >= MAX` -> `413`, so
  16384 is rejected and 16383 accepted.
- `RB3E_DTA_OUTPUT_MAX = 32768`; on overrun the body is rewound and ends with
  `!! output truncated, raise RB3E_DTA_OUTPUT_MAX or split the script`.
- Batch = a top level that is *all* COMMAND nodes; exactly one `=> ` marker per command, in order;
  a refused command still emits `=> !! refused: bad command pointer`; truncation is the sole case
  where markers < commands.

---

## Step 4 — install (VERIFIED)

Ran **only** step [3/4] of `redeploy` (`xbox.sh deploy`), skipping [1/4] `aurora` and [2/4]
`wait-ftp` because Aurora/FTP were already up. The dangerous magicboot-to-Aurora step was never
issued.

```
$ xbox.sh deploy .../out/RB3Enhanced.dll
deployed; on-drive:
068f386755297295
78210
local:
068f386755297295
78210
```

On-drive bytes match local bytes. Install confirmed by read-back, not by status code.

## Step 5 — reboot: SKIPPED, deliberately

No reboot was needed. Aurora was already running with FTP up, so the DLL could be deployed in place;
RB3ELoader reads the DLL at title launch, so `magicboot` INTO RB3 is sufficient to pick up a new
build. Rebooting would have added the risky transition back for no benefit. The plan's step 5 exists
to recover FTP after a title launch — not applicable here.

## Step 6 — launch: title boots, but **the DLL does not load** (BLOCKER)

```
$ xbox.sh launch
>>> magicboot title=\Device\Mass1\Games\rb3\default.xex directory=\Device\Mass1\Games\rb3
200- OK
```

Console came back cleanly — **no wedge**. XBDM was answering again at t=0s on the first probe.

- `xbeinfo running` -> `name="\Device\Mass1\Games\rb3\default.xex"` — RB3 IS running.
- `getexecstate` -> `200- start`
- **TCP 21070: TIMEOUT after a bounded 5-minute poll (60 x 5s).** Never opened.
- `modules` does **not** list `RB3Enhanced.dll` after a further bounded 300s wait.
  `RB3ELoader.xex` (base=0x91c60000) IS loaded, as are `xbdm.xex` and `JRPC2.xex`.
- `xbox.sh launch-watch 180` -> `no ALIVE within 180s`.

### Root cause, from the boot log

`RB3ELoader` prints its search and then gives up silently — captured across the magicboot with
`xbdm_notify.py --follow`:

```
[05:23:32] debugstr [RB3ELoader] Title terminated!
[05:23:33] modload  name="default.xex" base=0x82000000
[05:23:33] debugstr Hooked: 'XexLoadImage'
[05:23:34] execution started
[05:23:34] debugstr [RB3ELoader] Checking GAME:\RB3Enhanced.dll...
[05:23:34] debugstr [RB3ELoader] Checking RB3HDD:\RB3Enhanced.dll...
[05:23:34] debugstr [RB3ELoader] Checking RB3USB0:\RB3Enhanced.dll...
[05:23:34] debugstr [RB3ELoader] Checking RB3USB1:\RB3Enhanced.dll...
[05:23:34] debugstr [RB3ELoader] Checking RB3USB2:\RB3Enhanced.dll...
```

All five checks fail; there is no "loaded" line and nothing further from RB3E in the whole 79-line
log. The rest is ordinary thread create/terminate traffic from vanilla RB3.

Ruled out so far:
- **Not a missing/misplaced file.** XBDM `dirlist name="Usb1:\Games\rb3"` shows
  `name="RB3Enhanced.dll" sizelo=0x13182` (= 78210, our build). `GAME:\` is the running title's own
  directory, `\Device\Mass1\Games\rb3`, and it is checked **first**.
- **Not an invalid XEX.** `xex-patcher/tools/xexlint.py` on the exact deployed file:
  `PASS: 0 reject, 0 warn (6 pass, 0 skip)`.
- **Not a stale plugin.** `Hdd:\launch.ini` `[Plugins]` has `plugin1 = Usb:\RB3ELoader.xex`
  (present at `Usb1:\RB3ELoader.xex`) and the module list proves it ran.
- **Not a wrong drive.** `/Usb0/Games/rb3` does not exist; only `Usb1` has the install.

Incidentally this confirms the briefing's DashLaunch note: `launch.ini` really does load XBDM as
`plugin2 = Hdd:\xbdm.xex`. XBDM nonetheless survived this title launch, so plugins did load.

### Decisive A/B — old DLL loads, new DLL does not

Restored the backup over **XBDM `sendfile`** (works while the title is running — no Aurora
transition, no FTP, no wedge risk) and relaunched:

```
$ xbdm_sendfile.py 192.168.8.180 <backup> 'Usb1:\Games\rb3\RB3Enhanced.dll'
uploaded 75776 bytes -> Usb1:\Games\rb3\RB3Enhanced.dll
```

```
[05:27:51] [RB3ELoader] Checking GAME:\RB3Enhanced.dll...
[05:27:51] modload name="RB3Enhanced.dll" base=0x84000000 size=0x00858800
[05:27:51] [RB3ELoader] Loaded GAME:\RB3Enhanced.dll!
[05:27:51] [RB3E:MSG] Loaded! Version 0.7-85-gaaf319d-dirty (master-aaf319d-dirty)
[05:27:51] [RB3E:MSG] Loading config from GAME:\rb3.ini... Successfully loaded config!
[05:28:19] [RB3E:MSG] HTTP server running!
[05:28:19] 192.168.8.180 RB3E v0 ALIVE (Xbox360) payload=b'0.7-85-gaaf319d-dirty\x00'
== ALIVE — DLL loaded on console (T5 PASS) ==
```

Same loader, same path, same boot — **the old binary loads on the first `GAME:\` probe, the new one
falls through all five paths.** The rejection is a property of the new file, not the environment.

## ROOT CAUSE — the new DLL is packed by an unproven packer, and this run IS its hardware gate

`RB3Enhanced/tools/oss-xbox-build/pack-dll.sh:17-24` says so in its own header comment:

> *** The compressed Format=2 container is what LOADS ***: the console's XexLoadImage rejects the
> uncompressed (Format=1) xex2pack container at image-map time. Producing a loadable Format=2 has
> historically meant `wine xextool -m d -c c`; the native LZX writer here (xex-patcher --compress,
> XEX PATCH #6 sliding window) produces an OFFLINE-EQUIVALENT compressed container — decoded image +
> ImageHash + every per-page digest are byte-identical to xextool's output. **But HW load of this
> native container is still PENDING (no native-compressed DLL has returned ALIVE on real hardware
> yet)**: until that HW gate passes, the wine `pack-si-dll.sh` path (`wine xextool -m d -c c`) is the
> proven ship path.

The artifact in `out/` was produced by the **wine-free native LZX packer**, whose hardware load was
explicitly still pending. **This session is that pending gate, and it FAILS.**

> **NEW HARDWARE RESULT:** the xex-patcher native-LZX Format=2 container does **not** load on real
> hardware. RB3ELoader's `XexLoadImage` rejects it silently. Offline equivalence (byte-identical
> decoded image, ImageHash, per-page digests) and `xexlint` PASS are **not** sufficient to predict
> HW load.

Corroborating signal — **file-size alignment**:

| file | size | mod 2048 |
|---|---|---|
| new (native LZX) | 78210 | **386** |
| backup, loads | 75776 | 0 |
| all 9 other `RB3Enhanced_*.dll` on the drive | 61440 / 69632 / 57344 / 266240 / 8724480 / 67584 | 0 |

Every DLL known to load is 2048-byte aligned; the native packer's output is not. The briefing's
figure (78208) is also unaligned, so this is a property of the packer, not of this one rebuild.

### The currently-loaded build cannot test the feature

The DLL now running is `0.7-85-gaaf319d-dirty`. `aaf319d` **is an ancestor of `bd6959a`** and
predates the DTA-eval channel entirely. Confirmed live on hardware:

- `GET /` -> `200 OK`, `Server: RB3Enhanced 0.7-85-gaaf319d-dirty` (HTTP server healthy)
- `POST /dta/eval` -> **connection closed, empty response** (route does not exist)

So **no part of the DTA-eval contract can be exercised until a loadable build of `bd6959a` exists.**

### Safety state right now

RB3 is running with the old RB3E loaded, so its far-detour kernel patches ARE live, and `aaf319d`
**predates the title-exit watcher** (`2040f67`) that reverts them. Per
`xbox360_vcontroller.c:1064-1090` this is exactly the configuration in which `magicboot` out of RB3
wedges the console. Any further title transition must go via a soft/cold `magicboot` (full console
reboot), never `magicboot title=`.

## Step 6b — repack via the proven wine/xextool path

Rebuilt the SAME `bd6959a` PE (`K-link/RB3Enhanced.exe`, untouched) through the packer that
`pack-dll.sh` itself names as the proven ship path, substituting only step [3]:

```
[1] xex2pack --compress none   (RB3Enhanced's own PE + ordinal-map)   entry=0x84024380
[2] xex-patcher/tools/pack-loadable.sh boot.xex raw.dll   -> raw.dll 8855552 bytes, xexlint PASS
[3] wine xextool -m d -c c -o RB3Enhanced.wine.dll raw.dll
    XexTool v6.3 ... "is devkit unencrypted compressed."
[3b] xexlint -> PASS: 0 reject, 0 warn (6 pass, 0 skip)
```

Result: `out/RB3Enhanced.wine.dll`, **77824 bytes (mod 2048 == 0)**,
sha256 `a725424e8e3667fa478a5daef91de66fe2a39553fe3aaf70cbe3476bc0e2393c`.

Same source, same PE, different compressor — and the size is now 2048-aligned like every DLL that
loads. Uploaded over XBDM `sendfile`; `dirlist` confirms `sizelo=0x13000` (= 77824) on the drive.

## Step 6c — cold reboot + launch: **the wine-packed build LOADS** (VERIFIED)

RB3E's kernel patches were live, so the exit was a **cold reboot, never `magicboot title=`**:

```
>>> magicboot cold
200- OK
```
```
XBDM 730 back after 16.0s     (bounded poll, 60 x 5s ceiling)
FTP 21: up                    (Aurora restored)
```

**No wedge.** The user's soft/cold-reboot procedure is confirmed working on this console.

Then `magicboot` INTO RB3 (the safe direction) via `xbox.sh launch-watch 150`:

```
[05:32:37] [RB3E:MSG] HTTP server running!
[05:32:37] 192.168.8.180 RB3E v0 ALIVE (Xbox360)
[05:32:37] *** DLL IS LOADED ON CONSOLE (T5 PASS) ***
```

> **This closes the pending hardware gate from `pack-dll.sh`, negatively for the native writer:**
> same PE, native-LZX container = **rejected**; wine/xextool container = **loads**. The native
> LZX-normal writer is not yet shippable, and offline byte-equivalence + xexlint do not predict it.

**Caveat on the version banner:** the module reports `0.7-85-gaaf319d-dirty`. That string is stale —
it is baked into `K-link/RB3Enhanced.exe` from a cached `version.sh` output, and the same stale
string is in the PE on disk. It is **not** evidence of an old build: the PE contains the
`bd6959a`-only strings, and the decisive proof is that `/dta/eval` answers at all (the `aaf319d`
build closes that route). Worth fixing separately — the version banner is the documented "new-build
marker" and it is lying.

## FIRST-EVER HARDWARE EXECUTION OF THE DTA-EVAL PATH

```
$ tools/rb3e_dta.py 192.168.8.180 '{print "hi"}'
"hi"
=> 0
```

**PASS** — script parsed by `DataReadString`, executed on the main thread, print hook captured,
result serialised and returned. Every link in the chain works on hardware.

Note a **doc/impl discrepancy**: `docs/DTA-EVAL.md` shows this exact example returning `hi`, but
hardware returns `"hi"` (quoted). Correct per the implementation — the print wrapper *serialises*
its arguments rather than chaining to stock `print` (documented in "Output capture"), and a string
node serialises quoted. The doc example is wrong, not the code.

## HARDWARE VALIDATION RESULTS — per item

Client used: raw sockets (`POST /dta/eval`) for exact control of `Content-Length`; cross-checked
against `tools/rb3e_dta.py`.

### 1. Basic eval — **PASS**
```
{print "hi"}   ->  200 OK    "hi"
                             => 0
```

### 2. Batching — **PASS**
```
{print "a"}{print "b"}{print "c"}   ->  200 OK
"a"
=> 0
"b"
=> 0
"c"
=> 0
```
Exactly 3 `=> ` markers for 3 commands, interleaved with each command's own printed output in the
right place. The batch loop steps the commands itself, as designed.

### 3. Result attribution — **PASS** (this is the `bd6959a` fix, now proven on HW)
```
{sprint "one"}{sprint "two"}{sprint "three"}   ->  200 OK
=> "one"
=> "two"
=> "three"
```
Distinct values, so an off-by-one or a shifted marker would be visible. Nth marker == Nth command,
in order. 3 markers for 3 commands.

### 4. 16384-byte request cap — **PASS, exactly at the boundary**

`net_http_server.c:346` is `content_length >= RB3E_DTA_SCRIPT_MAX`, so 16384 must fail and 16383
must pass. Both confirmed on hardware:

| body length | status | body |
|---|---|---|
| 16384 | `413 Payload Too Large` | `script too long` |
| 16383 | `200 OK` | `"x"\n=> 0\n` |

The doc's corrected 16384 figure (defect 3 of `bd6959a`) is the one the hardware enforces.

### 5. Pre-parse validation — **PASS**
```
{print "x"     ->  400 Bad Request
                   malformed script (unbalanced braces or unterminated string)
```

### 6. 32768-byte output cap + truncation banner — **NOT REACHED** (see below)

### 7. `=> !! refused: bad command pointer` — **NOT TESTED.** Requires a top-level COMMAND node whose
`dataArray` pointer fails `DTAEval_PlausiblePointer`, i.e. a corrupt parse. There is no safe way to
manufacture that from the client side; it is a defensive path.

---

## INCIDENT — a probe script crashed the title (documented risk #1, hit live)

Reaching the 32 KB output cap needs >32768 bytes of output from <16384 bytes of input, i.e. >2x
amplification. `{print "<literal>"}` is ~1:1, so amplification needs a variable that can be printed
many times. The probe for that mechanism was:

```
{set $rb3e_probe "ZZZZ"}{print $rb3e_probe}
```

Result: **no HTTP response, and the title stopped.**

```
>>> getexecstate
200- stop
```

XBDM is fine; the *title* faulted. This is exactly known risk #1 from `DTA-EVAL.md`:

> `DataReadString` does *not* return null on bad input — it goes through `MILO_FAIL`. [...] A script
> with balanced braces but a bad token (`{print #bogus}`) reaches `DataReadString` and may fault.

The script was brace-balanced, so `RB3E_DTAEval_Validate` passed it, and it still took the title
down. **First hardware confirmation that the validator is not sufficient protection** — the doc says
this in theory; it is now observed.

Not yet isolated whether the fault is `set` on an undefined `$`-variable, the `$`-variable
dereference, or the parse of `$rb3e_probe`. Only that a balanced, innocuous-looking script killed
the title on the first try.

**The already-completed tests above are unaffected** — they all returned before this probe.

---

## Crash isolated: `{set $var "..."}` alone kills the title

Second run, the `set` clause **on its own**, nothing else in the request:

```
{set $rb3e_probe "ZZZZ"}   ->  <no HTTP response>
>>> getexecstate            ->  200- stop
```

So the fault is in the **`set` / `$`-variable** path, not in `print $var`. Reproduced twice,
recovered twice. A brace-balanced, entirely ordinary Milo DTA idiom — arguably *the* most common one
— reliably takes down the retail title through this channel. `RB3E_DTAEval_Validate` passes it, as
designed (it is a balance checker, not a parser).

Recovery both times: `magicboot cold` -> XBDM back in **24s** -> `magicboot` into RB3 -> ALIVE.
**Zero physical power-cycles this session.**

## Final consolidated battery (single clean run, after the last recovery)

| # | Item | Result |
|---|---|---|
| 1 | basic eval `{print "hi"}` | **PASS** `"hi"\n=> 0\n` |
| 2 | batching, 3 commands | **PASS** 3 markers, output interleaved correctly |
| 3 | attribution, 3 distinct values | **PASS** `=> "one" / "two" / "three"` in order |
| 3b | attribution, 10 distinct values | **PASS** `=> "0"` … `=> "9"`, 10 markers, exact order |
| 4 | request cap, 16384 bytes | **PASS** `413 Payload Too Large` / `script too long` |
| 4b | request cap, 16383 bytes | **PASS** `200 OK` |
| 5 | unbalanced braces | **PASS** `400 Bad Request` |
| 5b | empty body | **PASS** `400 Bad Request` |
| 6 | engine-state read `{rb3e_get_song_count}` | **PASS** `=> 4419` |
| 7 | **32768-byte output cap + truncation banner** | **NOT REACHABLE — see below** |
| 8 | `=> !! refused: bad command pointer` | **NOT TESTED** (defensive path, cannot be provoked from a client) |
| 9 | `409 Conflict` (concurrent claim) | **NOT TESTED** (ran out of safe budget after 2 title crashes) |
| 10 | `504` main-thread timeout | **NOT TESTED** (requires a slow script; the only slow-script lever available crashes the title) |

(3b was initially scored FAIL by my own assertion, which wrongly expected bare `0`; `sprint` returns
a *string*, so `"0"` quoted is correct. The attribution ordering it was actually testing is a PASS.)

## The 32 KB output cap is NOT REACHABLE from a remote client — with proof

This is a real finding, not a skipped test. Measured on hardware:

```
input  16383 bytes  ({print "AAAA…"}, the maximum accepted script)
output 16381 bytes  200 OK, no banner
ratio  1.000
```

The output/input ratio of the channel's only client-controllable primitive is **exactly 1.0**:
`{print "<N chars>"}` costs `N+10` in and yields `N+8` out. The request cap is 16383, so the maximum
output a client can provoke is ~16 KB — **precisely half** of `RB3E_DTA_OUTPUT_MAX`. Reaching 32768
requires >2x amplification, which needs one of:

- **DTA variables / loops** (`{set $b "…"}` + N x `{print $b}`) — **crashes the title on this build**
  (isolated above). Dead end.
- **A large engine-state dump.** Attempted: 40 x `{rb3e_get_song_name i}` gave ratio **0.92** —
  every call returned the symbol `rb3e_no_song_name`, because the song list is not populated on the
  boot/intro screen the console sits on unattended (documented risk #4, screen-dependent scripts).
  `{rb3e_get_song_count}` does answer (`4419`), so the funcs themselves work; the *data* is not there
  yet. Reaching it means driving the game to a screen where the library is loaded — a controller-in-
  hand session, out of scope for this unattended run.

**Consequence worth acting on:** `DTA-EVAL.md` instructs clients to *"check for that banner before
concluding anything from a short marker count"* — yet the banner is on a path a client cannot
self-test, and it has now been shown to be unreachable in the cheap unattended configuration. The
truncation path (`OutputLen` rewind + `TruncationNotice` append, `DTAEval.c:469-478`) remains
**entirely unexecuted on hardware**. Suggested follow-up: a temporary debug build with
`RB3E_DTA_OUTPUT_MAX` lowered to e.g. 4096 would make the banner reachable with a ~5 KB script and
would exercise the exact same code path.

---

## Console left in this state

- RB3 running, wine-packed `bd6959a` DLL loaded, `/dta/eval` answering.
- `/Usb1/Games/rb3/RB3Enhanced.dll` = the **wine-packed** build (77824 bytes,
  sha256 `a725424e…`), NOT the native-packed one, which does not load.
- Backup of the original preserved at
  `/home/free/console-backups/RB3Enhanced.dll.console-backup-20260804` (75776 bytes, sha256
  `d3028567…`), plus 9 pre-existing `RB3Enhanced_*.dll` snapshots on the drive.
- No physical power-cycle needed at any point.

## Actionable follow-ups

1. **`pack-dll.sh`'s native LZX path must not be used to ship** — it produces a container that does
   not load. Either fix the native writer or make `pack-dll.sh` default to the wine step. (A separate
   agent is isolating the packer defect; the 2048-alignment delta is the lead.)
2. **`{set $var …}` crashes the retail title through this channel.** Needs root-causing; at minimum
   `DTA-EVAL.md`'s risk #1 should name `set`/`$`-variables explicitly as a known-fatal construct,
   since it is not an obviously "bad token".
3. **The version banner lies** — the build reports `0.7-85-gaaf319d-dirty` while containing
   `bd6959a` code. `version.sh` output is being cached into the PE. This defeats the documented
   "psize + boot version string = new-build marker" check in `xbox.sh verify`.
4. **`DTA-EVAL.md` example is wrong**: `{print "hi"}` returns `"hi"` (quoted), not `hi`.
5. Untested on hardware still: truncation banner, `409`, `504`, refusal marker.
