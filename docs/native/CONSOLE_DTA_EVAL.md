# Evaluating DTA on a real Xbox 360 and getting the answer back on the PC

**Goal:** run arbitrary DTA against the *hardware* engine and read the result on a PC, so live
console state can be diffed against the native port (which already answers the same scripts on
`POST /api/dta/eval` — [HTTP_DEBUG_SERVER.md](../tools/HTTP_DEBUG_SERVER.md)).

Client: [`tools/console/dc3_eval.py`](../../tools/console/dc3_eval.py) (stdlib only, `--self-test`
runs offline).

Its CLI and importable API are deliberately **the same shape as RB3Enhanced's
`tools/rb3e_dta.py`** (`feature/dta-eval-channel`), so the state-differ can drive an RB3 console and
a DC3 console with the same code — see [§8](#8-interface-parity-with-rb3enhanced).

---

## 0. Verdict

| # | Surface | Reachable **today**? | Result comes back? | Verdict |
| --- | --- | --- | --- | --- |
| **1** | **RndConsole + loose `.dta` over FTP** | **yes, zero binary edits** | **yes** (script writes a file, PC pulls it) | **CHOSEN.** 3 keystrokes per iteration; payloads and results are 100 % PC-driven. |
| 2 | DC3Enhanced DLL (RB3Enhanced pattern) | needs a DLL + loader | yes, HTTP request/response | **Best long-term.** Design + addresses in §6. Not implemented here (sibling agent owns the RB3E side). |
| 3 | AppChild, TCP 4543 | needs **3 byte-patches** to `debug.xex` | **no** — wire protocol is fire-and-forget | Real, shipped, protocol recovered (§5). Command injector, not an eval channel. Free to test on Xenia. |
| 4 | Holmes, TCP 4544 | debug XEX only, needs a PC-side server nobody has written | partially (`MILO_LOG` tee) | Not worth it on its own; only interesting as the thing that unlocks #3 without patches. |
| 5 | OSC, UDP 12346 | no | n/a | Confirmed dead end. |
| 6 | xbdm | no | n/a | Confirmed dead end. |

The decisive constraint is §4: **an RGH console cannot pass launch arguments to a title.** That
kills #3 and #4 outright unless you edit the executable, and once you are willing to edit the
executable, #2 is strictly better than #3.

---

## 1. Surface-by-surface evidence

Symbol addresses are debug-XEX VAs from `config/373307D9/symbols.txt`. "retail" means
`orig-assets/default.xex`; both XEXs were decrypted with
`idaxex/xex1tool -b` and searched directly, so *shipped* presence is asserted from the binary,
not from `src/`.

### 1a. AppChild — TCP 4543, **console dials out**

`src/system/os/AppChild.cpp:67-77`:

```cpp
void AppChild::Poll() {
    if (mStream) {
        while (mEnabled && !mSync) {
            DataArrayPtr cmd;
            *mStream >> cmd;
            cmd->Execute(true);
        }
        mSync = false;
    }
}
```

Called unconditionally every frame from `SystemPoll` (`src/system/os/System.cpp:238-240`).
The console is the TCP **client** — `AppChild::AppChild` (`AppChild.cpp:14-20`) does
`NetStream::ClientConnect(NetAddress(HolmesResolveIP().mIP, 0x11BF))`, i.e. it connects *out* to
port 4543 on the Holmes host. **The PC listens.** That is much better than needing the console to
accept connections.

Shipped in **both** builds. The five `DataRegisterFunc`/option string literals are code-referenced
in retail as well as debug:

| symbol / literal | debug | retail |
| --- | --- | --- |
| `?Init@AppChild@@SAXXZ` | `0x825F3D18` | ≈`0x824F0678` |
| `?Poll@AppChild@@QAAXXZ` | `0x825F3F28` | linked |
| `?TheAppChild@@3PAVAppChild@@A` | `.data 0x82F695B4` | linked |
| `"app_child"` | `0x82081C94` (ref `0x825F3D30`) | `0x820592FC` (ref `0x824F0690`) |
| `"pipe_name"` | `0x82081C68` | `0x820592F0` |
| `"enable_app_child"` / `"disable_app_child"` / `"sync_app_child"` | `0x82081C54/40/30` | `0x820592DC/C8/B8` |
| `"AppChild::Connect\n"`, `"!TheAppChild"`, `"AppChild.cpp"` | present | stripped (debug literals only) |

Socket stack is fully imported in both: `NetDll_socket/bind/listen/accept/connect/recv/send/select`
from `xam.xex`. `WinSockSocket` (`??_7WinSockSocket@@6B@ = 0x8208053C`) is the concrete impl.

**Activation chain — this is where it dies.** `AppChild::Init` (`AppChild.cpp:53-63`) is gated on
`OptionBool("app_child", false)`, i.e. on `-app_child` in the process command line, and there is
**no DTA or config path** to construct `TheAppChild` (`Enable/Disable/SyncAppChild` all early-out
when it is null). Then the constructor needs `HolmesResolveIP()` to return non-zero, which chains
through `CanUseHolmes(3)` (`HolmesClient.cpp:750-761`) and `HolmesClient::PlatformResolveIP`
(`HolmesClient_NetSocket.cpp:11-19`) into `gMachineName`, which is only ever written by
`HolmesSetFileShare` inside `PlatformCreateServerStream` — i.e. **only if Holmes itself connected.**
With an unresolved host, `PlatformResolveIP` hits
`MILO_FAIL("Couldn't resolve holmes_host: %s")`.

Ordering note: `FileInit()` (which calls `HolmesClientInit`, `File.cpp:382`) runs at
`System.cpp:629`, one line *before* `AppChild::Init()` at `:630`, so Holmes really does get first
crack at `gMachineName`.

**No return path.** The only thing the console ever writes to this socket is
`AppChild::Sync()` → one little-endian `u16` = 1 (`AppChild.cpp:22-27`), triggered by the PC
sending `{sync_app_child}`. It is a per-frame ack, not a result.

### 1b. Holmes — TCP 4544

`HolmesClientInit` (`HolmesClient.cpp:398-445`) only runs when `!UsingCD() || gHostConfig ||
gHostLogging`, all three of which are command-line-only. It then needs `-holmes_host <ip>` (else
`MILO_FAIL("NO HOSTNAME PROVIDED, ADD \"-holmes_host <hostname>\" to your args")`) and a PC-side
server that answers the `Holmes::kVersion` handshake (`HolmesClientInitOpcode`, `:334-396`) — no
such server exists publicly.

**Holmes is completely absent from retail**: zero raw occurrences of `holmes`/`Holmes` in
`default.xex` (any case), no `.?AVHolmesInput@@` / `.?AVAsyncFileHolmes@@` RTTI, and the debug XEX's
DNS imports (`NetDll_XNetDnsLookup` #67, `XNetDnsRelease` #68, `inet_addr` #26, `WSACreateEvent` #29)
are **not imported by retail at all** — so `NetworkSocket::ResolveHostName`/`IPStringToInt` are dead
there.

It does inject remote keystrokes (`HolmesInput::SendKeyboardMessages`,
`src/system/os/HolmesKeyboard.cpp:50-65` → `KeyboardSendMsg`), which would drive the RndConsole
remotely — the one genuinely attractive property. But writing the server is a bigger job than
route #2, and it still needs launch args.

### 1c. RndConsole — the one that works

`RndConsole::ExecuteLine` (`src/system/rndobj/Console.cpp:416-450`) is a full REPL:

```cpp
n40 = DataReadString(line_txt.c_str());
...
n48 = n40.Array()->Command(0)->Execute();   // or ->Execute() for a bare array
...
output << "Evaluates to " << n48 << "\n";   // printed to the on-screen overlay + MILO_LOG
```

Opened by `{rnd show_console}`, bound to `KB_ESCAPE` in
`orig-assets/extracted/(..)/(..)/system/run/config/default.dta:226-228`, which
`config/ham_keep.dta:158` `#merge`s in at runtime.

Shipped in **both** builds — `.?AVRndConsole@@` RTTI at `0x82F15C10` (debug) / `0x82D1160C`
(retail), `"show_console"` code-referenced at `0x826653F4` (debug) / `0x82554F40` (retail).
Keyboard input, however, is debug-only: `XamInputGetKeystrokeEx` is in the debug XEX's import
table and **not** in retail's. So: **boot `debug.xex`.**

Editor conveniences that make it usable as an automation trigger (`Console.cpp:452-547`):

| key | code | effect |
| --- | --- | --- |
| `Esc` | `0x12E` | toggle console |
| `Up` / `Down` | `0x142` / `0x143` | command history |
| `Tab` | `9` | prefix-complete from history |
| `Enter` | `10` | `ExecuteLine()` |
| trailing `/` on the line | — | `Console.cpp:421-424`: runs the line **and auto-closes the console** |

The trailing-`/` trick matters: history stores the line *with* the `/` (`mBuffer.push_front` at
`:420` happens before the `/` is erased at `:422`), so re-running is **`Esc`, `Up`, `Enter`** —
three keystrokes, console closes itself.

### 1d. OSC — confirmed dead end

`OSCMessenger::Connect` (`src/system/utl/OSCMessenger.cpp:15-26`) is gated on `!UsingCD()` *and* a
Holmes IP, uses `NetworkSocket::Create(false)` (datagram), and `Poll` (`:28-79`) only files
`float`/`int`/`string`/`vector` values into a name→value map read by `PoseFatalities` and
`FreestyleMoveRecorder`. It cannot call a `DataFunc`.

### 1e. xbdm — confirmed dead end

Debug XEX imports exactly 7 xbdm symbols (`DmMapDevkitDrive`, `DmGetXboxName`,
`DmCaptureStackBackTrace`, `DmGetSystemInfo`, `DmIsDebuggerPresent`, `__CAP_Enter_Function`,
`__CAP_Exit_Function`). No `DmRegisterCommandProcessor`. Retail has **no xbdm import block at all**.

---

## 2. The two file-system rules that shape everything

Both come from `src/system/os/File_Win.cpp:9-13` and `src/system/obj/DataFile.cpp:584-598`:

```cpp
bool FileIsLocal(const char *file) {
    const char *drive = FileGetDrive(file);
    MILO_ASSERT(!strieq(drive, "game"), 0x24);
    return strlen(drive) > 1;
}

const char *CachedDataFile(const char *file, bool &b) {
    bool isLocal = FileIsLocal(file);
    if (strstr(file, ".dtb")) { b = true; return file; }
    if (UsingCD() && !isLocal) {            // -> read from the ark, as gen/<base>.dtb
        b = true;
        return MakeString("%s/gen/%s.dtb", FileGetPath(file), FileGetBase(file));
    }
    b = false; return file;
}
```

1. **`game:\` is illegal — for reads *and* writes, and fatally so.** `strieq` is `stricmp`-based,
   so `GAME:`, `Game:` and `game:` all assert. This is not a debug-only nicety: `File_Win.obj` is
   linked into the shipped Xbox image (`orig/373307D9/ham_xbox_r.map` puts the assert-expression
   string at `.rdata:0x820807C4`, owner `os:File_Win.obj`), `?FileIsLocal@@YA_NPBD@Z` sits at
   `.text:0x825EEEB8` and the decomp matches it 100 %, and `Debug::Fail` → `Debug::Modal` ends in
   `Exit(1, true)` (`Debug.cpp:439-444`) — the title dies.

   `NewFile` (`File.cpp:610`) calls `FileIsLocal` **unconditionally**, before the read/write
   split, so writes reach the assert too. *(This invalidated the `GAME:\%s` string patch that
   [KINECT_CAPTURE.md](KINECT_CAPTURE.md) used to suggest; that doc is now corrected and carries
   a ranked drive ladder. Note the fix there is **not** "use a multi-character drive" — see
   rule 3 — it is simply "use a drive that is not `game`", and `d:\` is the right answer because
   it names the same directory.)*
2. **Reads need a multi-character drive.** A relative path, or a single-character drive like
   `d:\`, has `strlen(drive) <= 1` → `FileIsLocal` false → with `UsingCD()` true the request is
   rewritten to `<path>/gen/<base>.dtb` and served **from the ark**, never from disk.
   `NewFile` (`File.cpp:619-622`) makes the same split: non-local reads go to `ArkFile`.
3. **Writes are unaffected *by rule 2*** — `kWrite`/`kAppend` never sets the read bit, so neither
   the `CachedDataFile` `.dtb` rewrite nor the `ArkFile` branch can catch them; a *relative* write
   goes straight through `AsyncFile::New` to the title's working directory, i.e. the folder the
   XEX was launched from. That is the return path. Writes are **not** exempt from rule 1.

   One caveat that only applies to writes: `AsyncFile::New` (`AsyncFile.cpp:64`) routes
   `UsingHolmes(1) && (mode & FILE_OPEN_WRITE) && !FileIsLocal(path)` to `AsyncFileHolmes`, i.e.
   to a connected dev PC rather than the console. With no Holmes host `gHolmesStream` is null and
   the branch is dead, so this is only a concern if you are also running `--host`.

So: **push the probe with a drive-qualified path, get the answer back on a relative path.**

---

## 3. CHOSEN ROUTE — RndConsole + loose `.dta` over FTP

### 3.1 One-time console setup

1. Boot `orig-assets/debug.xex` from an **extracted** install (see
   [KINECT_CAPTURE.md §Route A](KINECT_CAPTURE.md) for the debug-XEX boot procedure; xbdm must be
   available). Plug in a USB keyboard.
2. Make sure the console runs an FTP server (DashLaunch / FSD / Aurora — standard on RGH).
3. Establish two names for **the same folder** (the one containing `default.xex`):
   * `--ftp-dir` — as FTP sees it, e.g. `/Hdd1/Games/DanceCentral3`
   * `--game-path` — as the *title* sees it, e.g. `Hdd1:\Games\DanceCentral3` (multi-character
     drive, not `game:`). **This spelling is the #1 hardware unknown — see §7.**
4. Press `Esc`, then type once (`--print-bootstrap` emits it for you):

   ```
   {run "Hdd1:\Games\DanceCentral3\dc3\p.dta"}/
   ```

   Keep the trailing `/` — it makes `Enter` also close the console.

### 3.2 Per-iteration loop

```
PC:      dc3_eval.py -t file -e '<script>'
           -> FTP: delete dc3_out.txt, dc3_done.txt
           -> FTP: PUT <ftp-dir>/dc3/p.dta
Console: Esc, Up, Enter          <- 3 keystrokes (or --hid-cmd)
PC:      poll <ftp-dir>/dc3_done.txt for this run's token
           -> GET <ftp-dir>/dc3_out.txt, print it
```

### 3.3 What the generated probe looks like

`dc3_eval.py` wraps your commands so their values land in a file, one record per command:

```
{do
   {set $dc3_r ""}
   {strcat $dc3_r {sprint {do <COMMAND 1>}} "~~DC3REC:dc3-1a2b3c4d~~"}
   {strcat $dc3_r {sprint {do <COMMAND 2>}} "~~DC3REC:dc3-1a2b3c4d~~"}
   {write_string_to_file "dc3_out.txt" $dc3_r 0}
   {write_string_to_file "dc3_done.txt" "dc3-1a2b3c4d" 0}
}
```

* `{run}` = `DataRun`, `DataFunc.cpp:1060-1069`. `FileMakePath(FileExecRoot(), path)` preserves a
  drive-qualified path verbatim (`File.cpp:472-486`).
* `{sprint …}` = `DataSprint`, `DataFunc.cpp:72-79` — evaluates each argument and renders it into a
  `String`. Nested `{do …}` (`DataFunc.cpp:303-328`) lets a multi-statement command still return its
  last expression.
* `{strcat $v a b}` = `DataStrCat`, `DataFunc.cpp:1217-1225`. Note it appends **in place** and
  takes `array->Var(1)`, so argument 1 must be a literal `$var` — `{strcat {sprint …} "x"}` would
  fault. The remaining arguments go through `DataArray::Str` → `DataNode::Str`, which *evaluates*
  commands first (`DataNode.cpp:423-424`), which is why the nested `{sprint}` works inline.
* `{write_string_to_file p s 0}` = `OnWriteStringToFile`, `DataFunc.cpp:1088-1094`. **The 4th
  argument must be `0`** — `array->Size() > 3 ? array->Int(3) : true` defaults to *append*.
* Accumulating into `$dc3_r` and writing once means the whole batch needs exactly **one** successful
  truncating write, and never depends on `kAppend` working.
* Records are separated by a per-run marker rather than a newline because `TextFileStream::Print`
  rewrites `\n` to CRLF (`TextFileStream.cpp:10-19`) and DTA string literals have no escape syntax.
* The `dc3_done.txt` token is a sentinel so the PC never reads a half-written `dc3_out.txt`.
* `--raw` skips the wrapper if you want to push a script that manages its own output.

### 3.3a `Debug::Print` is live in DC3's debug XEX — but this path does not depend on it

On retail RB3 TU5, `Debug::Print` compiled to a bare `blr` and was ICF-folded onto an empty stub, so
`TheDebug << …` is a no-op and the stock `print` DataFunc emits nothing. **DC3's debug XEX does not
have this problem**, verified in the shipped image:

| symbol | VA | first bytes | verdict |
| --- | --- | --- | --- |
| `?Print@Debug@@UAAXPBD@Z` | `0x825CC5D8` (size `0xC4`) | `7d 88 02 a6 91 81 ff f8 …` | real prologue, not a stub |
| `??_7Debug@@6B@` slot 1 | `.rdata 0x82079AB0` | → `0x825CC5D8` | vtable points at it, no ICF fold |

More importantly, **the chosen return path never touches `Debug::Print`.** `{sprint}` renders into a
`String` and `{write_string_to_file}` goes `TextFileStream::Print` (`0x827EFA18`, also a real
function, and slot 1 of `??_7TextFileStream@@6B@` at `0x820DCB68`) → `FileStream::Write`. So even on
a build where `Debug::Print` *is* stubbed, records still come back. `{print …}` / `{printf …}` are
the verbs that would silently vanish — the client never uses them, and you should not either.

Not verified: whether `Debug::Print` is stubbed in DC3 **retail**. It does not matter for the plan
of record (boot `debug.xex`), and it cannot affect this return path either way.

`{run}` re-reads the file every time: `DataReadFile` (`DataFile.cpp:500-540`) only consults the
`gReadFiles` memo while `gReadingFile` is set (nested reads), and opens a fresh `FileStream`
otherwise. So overwriting `p.dta` between iterations is enough — no cache busting needed.

### 3.4 Client usage

```bash
# offline sanity check (validator + parser + wire encoder + probe + FTP round-trip)
python3 tools/console/dc3_eval.py --self-test

# print the one-time console line
python3 tools/console/dc3_eval.py --print-bootstrap \
    --ftp-host 192.168.1.60 --ftp-dir /Hdd1/Games/DanceCentral3 \
    --game-path 'Hdd1:\Games\DanceCentral3'

# evaluate on hardware
export DC3_XBOX=192.168.1.60
export DC3_FTP_DIR=/Hdd1/Games/DanceCentral3
export DC3_GAME_PATH='Hdd1:\Games\DanceCentral3'
python3 tools/console/dc3_eval.py -T file '{ui current_screen}'
python3 tools/console/dc3_eval.py -T file -f probes/dump_venue.dta --timeout 60

# BATCH: one FTP round-trip and ONE keypress for the whole set,
# one result line per command, in order
python3 tools/console/dc3_eval.py -T file \
    -b '{ui current_screen}' -b '{rnd frame}' -b '{$venue get_showing}'

# a DC3Enhanced DLL (route 2) -- same flags, no keypress
python3 tools/console/dc3_eval.py 192.168.1.60 '{ui current_screen}'

# the native port
python3 tools/console/dc3_eval.py localhost -p 9090 '{ui current_screen}'

# console vs native, unified diff (exit 1 if they differ)
python3 tools/console/dc3_eval.py -T file --diff -b '{ui current_screen}' -b '{rnd frame}'

# poke around interactively
python3 tools/console/dc3_eval.py 192.168.1.60 --repl
```

**Batch before you loop.** Per-round-trip cost dominates on every transport — an HTTP request on
the DLL route, and an entire FTP push plus a human keypress on the file route. `-b` sends the whole
set at once and returns one result per command, so a 20-probe state dump is one keypress rather
than twenty.

`--hid-cmd '<shell command>'` replaces the manual keypress with anything you like — the intended
upgrade is a $5 RP2040 board enumerating as a USB HID keyboard, driven over serial, which makes the
whole loop unattended:

```bash
--hid-cmd "printf 'ESC\nUP\nENTER\n' > /dev/ttyACM0"
```

### 3.5 Writing large state dumps

The payload is a file, so it can be arbitrarily large — nothing is typed by hand. Useful verbs
(all core `DataInitFuncs` registrations, so present in every build):

| verb | source | use |
| --- | --- | --- |
| `{sprint …}` / `{array_to_string arr}` | `DataFunc.cpp:72`, `:851` | stringify a value / a whole `DataArray` |
| `{write_file path arr}` | `DataFunc.cpp:1082` | dump a `DataArray` as text `.dta` |
| `{object_list}`, `{find …}`, `{file_list}` | `:1755`, `:1717`, `:1756` | enumerate |
| `{printf fmt …}` | `:1602` | goes to `TheDebug` (on-screen + `-log` file), not to the return file |
| `{file_exists p}` / `{file_mkdir p}` | `:1707`, `:1708` | drive probing (see §7) |

---

## 4. Launch arguments on an RGH console — the blocker

**They cannot be supplied.** Evidence:

`src/system/os/System_Xbox.cpp:282-284` → `SystemPreInit(GetCommandLineA(), cfg)` →
`SetSystemArgs` (`System.cpp:520-561`) → `TheSystemArgs` → `OptionBool`/`OptionStr`
(`src/system/utl/Option.cpp:6-36`). `kCommandLineSz` is 512 (`cmplwi cr6,r11,512` at
`0x825CB880`).

`GetCommandLineA` is **not an import** — it is a locally-linked XAPILIB shim at
`.text:0x82336798` (size `0x2C`), and it is a one-line accessor for a kernel *data* export:

```
82336798  mflr r12 / stw r12,-8(r1) / stwu r1,-96(r1) / mr r13,r13 / mr r14,r14
823367ac  lis  r11,0x8200
823367b0  lwz  r3,0x908(r11)     ; __imp_ExLoadedCommandLine  (.rdata:0x82000908)
823367b4  addi r1,r1,96 / lwz r12,-8(r1) / mtlr r12 / blr
```

`ExLoadedCommandLine` is xboxkrnl ordinal **430**, imported by both debug and retail. On retail
hardware the loader puts only the **module path** there — corroborated by the engine's own
assumption that `TheSystemArgs.front()` is a path (`System_Xbox.cpp:272`
`FileGetName(TheSystemArgs.front())`; `Rnd.cpp:1431` `XLaunchNewImage(TheSystemArgs.front(), 0)`)
and by Xenia's comment on the same export ("The name of the xex").

* **XEX headers cannot carry a command line.** `debug.xex` has 18 optional headers
  (`SECTION_TABLE`, `FILE_DATA_DESC`, `ENTRY_POINT`, `PE_BASE`, `IMPORTS`, `VITAL_STATS`,
  `CALLCAP_IMPORTS`, `PE_MODULE_NAME`, `BUILD_VERSIONS`, `TLS_DATA`, `STACK_SIZE`, `PRIVILEGES`,
  `PRIVILEGES_32`, `EXECUTION_ID`, `WORKSPACE_SIZE`, `GAME_RATINGS`, `LAN_KEY`,
  `ALTERNATE_TITLE_IDS`). No such ID exists in the full list either. `xex1tool` is read-only apart
  from `-b`/`-d`; XexTool 6.3 has no command-line option.
* **No launcher passes arguments.** DashLaunch `launch.ini`, FSD, Aurora and XeXMenu are all
  path-only. `XLaunchNewImage(path, flags)` has no argument parameter. The XAM launch-data blob
  (`XamLoaderSetLaunchData`, ordinals 422-424) *is* a real channel, but it is a **different pipe** —
  DC3 does not import those exports and the XDK CRT does not surface launch data as
  `GetCommandLineA()`.

### 4a. Free lane: Xenia

Xenia implements `ExLoadedCommandLine` with a passthrough
(`xenia/src/xenia/kernel/xboxkrnl/xboxkrnl_module.cc:209-226`, cvar `--cl`), so **every
argument-gated surface can be exercised in the emulator, today, with no patching**:

```bash
xenia --cl "-app_child -host_logging -holmes_host 192.168.1.50" default.xex
```

Use this to validate the AppChild wire encoder (§5) before touching hardware — see the
`xenia-gameplay` skill.

### 4b. If you do want args on hardware: an 8-byte patch

`debug.xex` is `encryption=0, compression=1` ("basic" zero-run) so it can be edited in place; no
repack, no re-sign. The VA→file-offset map is **piecewise** (this supersedes the "unverified"
note in KINECT_CAPTURE.md §Option 5):

| block | VA range | file offset |
| --- | --- | --- |
| 0 | `0x82000000`–`0x82328000` | `VA − 0x82000000 + 0x3000` |
| 1 | `0x82330000`–`0x82F60000` | `VA − 0x82000000 − 0x5000` |
| 2 | `0x83188000`–`0x83248000` | `VA − 0x82000000 − 0x22D000` |

(`xex1tool -a <VA>` computes it; `xex1tool -b` dumps a flat image where `VA − 0x82000000` is a
plain index.)

Patch `GetCommandLineA` and both callers (the CRT's `argv` *and* `SetSystemArgs`) are fixed at
once. **All current bytes below were read out of `orig-assets/debug.xex` at those offsets and
match.**

| what | VA | file offset in `debug.xex` | current bytes |
| --- | --- | --- | --- |
| `GetCommandLineA` body | `0x82336798` | **`0x00331798`** | `7d 88 02 a6 91 81 ff f8 94 21 ff a0 7d ad 6b 78 7d ce 73 78 3d 60 82 00 80 6b 09 08 38 21 00 60 81 81 ff f8 7d 88 03 a6 4e 80 00 20` |

Two shapes:

* **Minimal (8 bytes).** Replace `3d 60 82 00 | 80 6b 09 08` (`lis r11,0x8200` /
  `lwz r3,0x908(r11)`) at file `0x003317AC` with `lis r11,HI` / `addi r3,r11,LO` pointing at a
  string you stamp over an unused `.rdata` literal (block 0, `+0x3000`).
* **Self-contained.** The function is a leaf; collapse the whole body at `0x82336798` to
  `lis r3,0x8233` / `addi r3,r3,0x67A4` / `blr` = `3C 60 82 33  38 63 67 A4  4E 80 00 20`, which
  frees the 32 bytes at `0x823367A4`–`0x823367C3` (file `0x003317A4`) for the string itself. This
  drops the two `mr r13,r13` callcap NOPs — harmless unless a callcap profiler patches them.

Token 0 must look like a path (`FileGetName` runs on it); `FindOption` then scans every token for
a leading `-`.

**Unverified:** that a byte-patched `debug.xex` still boots on RGH. Dev-signed XEXs are not hash-
checked on RGH (that is why the scene runs modified game XEXs), and the per-page `ImageHash` chain
is HV-side and neutered by xeBuild patch sets — but this was not confirmed on hardware. Note that
`xex1tool -l` reports `Invalid image hash!` on the **pristine** file, so that message is not
evidence of tampering.

---

## 5. AppChild — activation recipe and recovered wire protocol

### 5.1 Wire protocol

`operator>>(BinStream&, DataArrayPtr&)` (`src/system/obj/Data.h:751-754`) calls
`DataArray::Load` **directly** — there is *no* leading "non-null" bool, unlike
`operator>>(BinStream&, DataArray*&)` at `DataArray.cpp:1011`. `NetStream` is constructed
`BinStream(true)` (`NetStream.cpp:8`), and `ReadEndian` byte-swaps when `mLittleEndian` on the
big-endian console (`BinStream.cpp:183-195`), so **everything on the wire is little-endian**.

`DataArray::Load` (`DataArray.cpp:488-...`):

```
s16  size            number of nodes
s16  line
s16  deprecated
     size × DataNode
```

`DataNode::Load` (`DataNode.cpp:727-792`):

| type | tag | payload |
| --- | --- | --- |
| `kDataInt`, `kDataUnhandled`, `kDataElse`, `kDataEndif`, `kDataAutorun` | 0, 6, 8, 9, 36 | `s32` |
| `kDataFloat` | 1 | `f32` |
| `kDataVar`, `kDataFunc`, `kDataObject`, `kDataSymbol`, `kDataIfdef`, `kDataDefine`, `kDataInclude`, `kDataMerge`, `kDataIfndef`, `kDataUndef` | 2,3,4,5,7,32,33,34,35,37 | `u32` length + raw bytes, **no NUL** (`BinStream::ReadString`, `:198-205`) |
| `kDataString` | 18 | `s32` length + raw bytes (`LoadGlob(bs, true)`, `DataArray.cpp:626-639`) |
| `kDataGlob` | 20 | `s16` (negative `mSize`) + `-mSize` bytes |
| `kDataArray`, `kDataCommand`, `kDataProperty` | 16, 17, 19 | nested `DataArray::Load` |

The type tag itself is `s32` (`d >> (int&)mType`).

Two traps:

* **Emit bare identifiers as `kDataSymbol` (5), not `kDataFunc` (3).** `DataArray::Execute`
  (`DataArray.cpp:898-912`) resolves a `kDataSymbol` head against `gDataDir` objects then
  `gDataFuncs`, and caches the result. A `kDataFunc` node is looked up at *load* time and
  `MILO_FAIL("Couldn't bind %s")`s on a miss (`DataNode.cpp:731-739`).
* **`DataArray::Load` performs macro substitution** on `kDataSymbol` nodes
  (`DataArray.cpp:497-506`): any symbol in the macro table is replaced by its expansion. The only
  macros in a stock boot are `HX_XBOX`, `HX_WIN`, `HX_NG` (`System.cpp:433-435`) plus `REGION_*`
  (`PlatformMgr.cpp:137`) and anything from `-define`. `dc3_eval.py` rejects those names.

The framing is a **blocking per-frame RPC**: `AppChild::Poll` reads and executes DataArrays in a
tight loop and does not return until one of them calls `{sync_app_child}`, which writes a single
little-endian `u16` = 1 and releases the frame. So the PC drives the frame rate: send N commands,
then `{sync_app_child}`, read the 2-byte ack, repeat.

`dc3_eval.py --self-test` asserts this encoding byte-for-byte (`{sync_app_child}`, `{print "x"}`,
`{set $x {+ 1 2}}`).

### 5.2 Making it fire without a Holmes server

Three patches, all verified against `orig-assets/debug.xex`. They avoid `-host_config`
(which forces `gUsingCD = false` + `TheArchive = nullptr` during config reads, `System.cpp:407-410`
and `:428-431`, so the whole DTA tree would have to be served over Holmes) and `-host_logging`
(which makes `PlatformCreateServerStream(false, …)` retry-loop forever until a Holmes server
answers, `HolmesClient_NetSocket.cpp:55-90`).

| # | symbol | VA | file offset | current bytes | replace with |
| --- | --- | --- | --- | --- | --- |
| 1 | `GetCommandLineA` | `0x82336798` | `0x00331798` | see §4b | return `"d.xex -app_child"` |
| 2 | `?CanUseHolmes@@YA_NH@Z` | `0x825EF600` | `0x005EA600` | `7d 88 02 a6 91 81 ff f8 fb e1 ff f0 94 21 ff a0 7c 7f 1b 78` | `38 60 00 01 4e 80 00 20` (`li r3,1` / `blr`) |
| 3 | `?HolmesFileHostName@@YAPBDXZ` | `0x825EF6F0` | `0x005EA6F0` | `3d 60 82 f7 38 6b 8c 28 4e 80 00 20` (`lis r11,0x82F7` / `addi r3,r11,-0x73D8` → `gMachineName` = `0x82F68C28`) | `lis r11,HI` / `addi r3,r11,LO` pointing at a `"192.168.1.50"` string you stamp into spare `.rdata` |

Patch 2 is safe: the only other caller is `UsingHolmes` (`HolmesClient.cpp:283-288`), which
short-circuits on `!gHolmesStream` (still null), and `OSCMessenger::Connect` is gated on
`!UsingCD()` independently. `SetIPPortFromHostPort` (`NetworkSocket.cpp:16-40`) parses a dotted
quad through `IPStringToInt` before falling back to DNS, so no name resolution is needed — and it
also accepts `"ip:port"`, though AppChild overrides the port to `0x11BF` anyway.

Then:

```bash
python3 tools/console/dc3_eval.py -t appchild -e '{print "hello from the console"}'
# listens on 0.0.0.0:4543, waits for the console to dial in,
# sends the command, then {sync_app_child}, reads the u16 ack
```

Because there is no return path, pair it with the §3 file trick:

```bash
python3 tools/console/dc3_eval.py -t appchild \
  -e '{write_string_to_file "dc3_out.txt" {sprint {ui current_screen}} 0}'
# then fetch dc3_out.txt over FTP
```

---

## 6. Route 2 (recommended long-term) — a DC3Enhanced DLL

RB3Enhanced (`/home/free/code/milohax/RB3Enhanced`) already ships exactly the channel we want for
retail RB3: `GET /execute?script=<url-encoded DTA>` on **TCP 21070**, answered on the main thread,
returning the serialized `DataNode`. The DTA executor was factored out into a standalone
`source/net_dta_exec.c` (119 lines) on the sibling agent's `feature/civetweb-http` branch; **port
that file, not the HTTP transport** (the transport is the part currently churning).

`ExecuteDTAWithResult` needs only three game functions plus a main-thread tick. DC3 equivalents,
from `config/373307D9/symbols.txt` (**debug XEX — retail addresses must be re-derived from
`orig-assets/default.xex`**):

| purpose | DC3 symbol | VA | size |
| --- | --- | --- | --- |
| parse DTA text | `?DataReadString@@YAPAVDataArray@@PBD@Z` | `0x825C12E8` | `0x78` |
| execute | `?Execute@DataArray@@QAA?AVDataNode@@_N@Z` | `0x825A1528` | `0x434` |
| release | `?Release@DataArray@@QAAXXZ` | `0x823317E8` | `0x90` |
| evaluate a node (args) | `?Evaluate@DataNode@@QBAABV1@XZ` | `0x8259D518` | `0xDC` |
| register new verbs | `?DataRegisterFunc@@YAXVSymbol@@P6A?AVDataNode@@PAVDataArray@@@Z@Z` | `0x825BA348` | `0x90` |
| registration hook point (tail of) | `?DataInitFuncs@@YAXXZ` | `0x825BA558` | `0x157C` |
| bootstrap | `??0App@@QAA@HPAPAD@Z` (`App::App`) | `0x82333798` | `0x890` |
| main-thread tick candidate | `?Run@App@@QAAXXZ` | `0x82334248` | `0x4` |
| read a file as DTA | `?DataReadFile@@YAPAVDataArray@@PBD_N@Z` | `0x825C1AD0` | `0x1B4` |
| log to a file | `?StartLog@Debug@@QAAXPBD_N@Z` | `0x825CE670` | `0xE4` |

Two DC3-specific notes versus RB3:

* **`DataArray::Execute` takes a `bool`** in DC3 (`Execute(bool fail)`), so the PPC signature is
  `(DataNode* sret, DataArray* this, bool fail)` — RB3's is `(DataNode* sret, DataArray* this)`.
  Pass `true`.
* `?Run@App@@QAAXXZ` is only 4 bytes here (a single instruction), so it is *not* RB3's
  "spare `blr` inside `App::Run`". A per-frame main-thread callback has to be found elsewhere —
  `SystemPoll` (`System.cpp:214-247`) is the natural place, and the `TheAppChild != nullptr` test
  at `:238` is a ready-made, normally-dead branch to hijack.

Everything else transfers verbatim: the naked-stub + `POKE_B` idiom for calling into game code, the
range-checked `HookFunction` trampoline (`source/utilities.c:15-52`, ±32 MB), the
`ObCreateSymbolicLink` drive mapping (`source/xbox360_files.c:29-38` — this is how RB3E gets
writable `RB3HDD:`/`RB3USB0:` drives, and it is *also* the clean fix for the §7 drive question),
and the DashLaunch-plugin loader. Two traps carried over: `RB3E_FlushCache` is a **no-op on 360**
(fine for boot-time patching only), and `DataReadString` faults on malformed DTA on the game
thread — brace-balance before parsing.

**Endpoint parity is the point.** Expose `POST /dta/eval` on port 21070 with the raw script as the
body, exactly as RB3Enhanced now does — *not* `GET /execute?script=`, whose script is capped by the
server's `request_path[250]` buffer to roughly 200 bytes and is therefore useless for real probes.
Keep `/execute` only as back-compat. Then

```bash
dc3_eval.py <console-ip> '{ui current_screen}'      # DC3Enhanced
dc3_eval.py localhost -p 9090 '{ui current_screen}' # native port
dc3_eval.py <console-ip> --diff -b '…' -b '…'       # compare them
```

all answer the same script. Port `source/net_dta_exec.c` verbatim, including the batching loop, the
`=> ` result markers, the 16 KB/32 KB caps and the exact truncation notice — the client already
parses that format, and §8 documents it.

---

## 7. What must be tested on hardware first, and what failure looks like

Ordered by how much they block.

1. **Does `debug.xex` boot at all on your RGH console with the retail ark?** Untested. Build dates
   are one day apart (debug map 2012-09-15, retail 2012-09-16). *Failure:* assert/modal during
   load, or a hang before the attract screen. `xbdm.xex` must be available — the debug XEX imports
   7 xbdm symbols and will not resolve its imports without it.

2. **What is the title-side spelling of the game folder's drive?** This is the single biggest
   unknown for the chosen route. Xbox 360 titles do **not** get `HDD:`/`USB0:` mounted by default —
   that is precisely why RB3Enhanced calls `ObCreateSymbolicLink("\\??\\RB3HDD:",
   "\\Device\\Harddisk0\\Partition1")` (`source/xbox360_files.c:17-38`). Candidates worth trying,
   in order: `Hdd1:`, `hdd:`, `Usb0:`, `Uda:`, `devkit:` (the debug XEX imports `DmMapDevkitDrive`
   and `HolmesXboxPath` uses `devkit:\`, `src/system/os/HolmesUtl.cpp:6-30`), `cache:`.
   *Failure:* `{run "X:\..."}` pops `MILO_WARN("DataReadFile: Can't open %s")`
   (`DataFile.cpp:520-521`) — a loud on-screen notify. Try the next spelling.
   **Do not try `game:`/`GAME:` — it trips `MILO_ASSERT` in `FileIsLocal`.**
   Discovery one-liner (type once, then fetch `dc3_drives.txt` over FTP from the game folder):

   ```
   {write_string_to_file "dc3_drives.txt" {sprint {file_exists "Hdd1:\"} {file_exists "hdd:\"} {file_exists "Usb0:\"} {file_exists "Uda:\"} {file_exists "devkit:\"} {file_exists "cache:\"}} 0}/
   ```

3. **Does a relative write land in the game folder?** `{write_string_to_file "dc3_out.txt" "x" 0}`
   should produce `dc3_out.txt` next to `default.xex`, visible over FTP. This assumes the XDK CRT
   sets the process working directory to the XEX's folder. *Failure:* the file never appears over
   FTP (or `FileStream` fails silently — `TextFileStream` does not warn). Fallback: use the same
   drive-qualified path you found in step 2 for the write as well.

4. **Does the RndConsole actually open, and does the keyboard reach it?** `Esc` should bring up the
   two overlays. *Failure:* nothing happens → either the runtime `#merge` of
   `system/run/config/default.dta` did not bring in the `KB_ESCAPE` binding
   (`config/ham_keep.dta:158`), or `RndOverlay::Find("output"/"input")`
   (`Console.cpp:115-116`) found no overlays in the loaded data. Cross-check by opening the pad
   cheat menu (**LT+LB+L3**) and looking for `esc  Toggle Console` in the KEYBOARD CHEATS section —
   `CheatProvider` lists keyboard cheats and `CallQuickCheat` invokes them from the pad
   (see [KINECT_CAPTURE.md §2b](KINECT_CAPTURE.md)), so that is also a keyboard-free way to *open*
   the console (you still need the keyboard to type into it).

5. **FTP concurrency.** Some console FTP servers accept only one session at a time and some lock a
   file that the title has open. `dc3_eval.py` opens a fresh connection per poll and tolerates
   connect failures, but if pushes intermittently fail, raise `--timeout` and check whether the
   FTP daemon is single-session.

6. **Only if you go down the AppChild road:** does the patched `debug.xex` still launch (§4b), and
   does `AppChild::Poll`'s blocking read behave when the PC is slow? Note the failure mode is
   ugly — `NetStream::ReadImpl` (`NetStream.cpp:24-44`) `memset`s the buffer to `0xEA` on failure
   and `DataArray::Load` will then deserialize garbage. Validate the encoder on **Xenia** first
   (§4a), where `--cl` needs no patching at all.

---

## 8. Interface parity with RB3Enhanced

The differ must be able to swap consoles without caring about the wire, so `dc3_eval.py` mirrors
`RB3Enhanced/tools/rb3e_dta.py` (branch `feature/dta-eval-channel`, commit `63c5a1a`).

**CLI** — same positionals, same flags, same exit codes:

| | RB3Enhanced | DC3 |
| --- | --- | --- |
| invocation | `rb3e_dta.py <host> '<script>'` | `dc3_eval.py <host> '<script>'` |
| script from file / stdin / prompt | `-f`, `-`, `--repl` | same |
| port / timeout | `-p` (21070), `-t` (10.0) | same defaults |
| exit codes | 0 ok, 1 unreachable/usage, 2 console refused | same |
| extra here | — | `-T/--transport`, `--api`, `-b/--batch`, `--diff`, `--print-bootstrap` |

**Importable API** — `evaluate(host, script, port=DEFAULT_PORT, timeout=10.0) -> str`,
`class ConsoleError(status, reason, body)` with `.status`/`.body`, and `DEFAULT_PORT = 21070`, all
signature-compatible. Additionally every transport object exposes `eval(script) -> str` and
`eval_batch(scripts) -> [str]`, which `rb3e_dta.py` does not have — that is the layer the differ
should target, because it is the only one that works for the file transport too.

**Wire contract implemented for `--api eval`** (`POST /dta/eval`, port 21070, raw body):

* Request body is the **raw script**, not URL-encoded, not JSON. `Content-Length` must be exact —
  the console hard-400s a short body.
* A **batch is just concatenated top-level commands** in one body; the console detects it by proving
  every top-level node is a `COMMAND`. `dc3_eval.py` joins with newlines rather than the bare
  concatenation in RB3E's doc example, because a command ending in a `;` line comment would
  otherwise swallow the next one.
* The response is **not** one plain line per command. It is captured `print` output interleaved with
  one `=> <value>` line per command; the Nth `=> ` line is the Nth command's return value and
  everything since the previous marker is that command's printed output. `split_results()`
  implements this.
* **Exactly one marker per top-level command, in order, including refused ones.** A command the
  console declines emits `=> !! refused: bad command pointer`, so markers stay 1:1 and positional
  attribution holds. `is_refusal(result)` identifies those slots.
* **One exception: truncation stops the batch early**, so a truncated body legitimately carries
  *fewer* markers than commands sent. The two shortfall cases need opposite handling and the client
  distinguishes them:

  | body | meaning | client behaviour |
  | --- | --- | --- |
  | fewer markers **and** the truncation banner | legitimate partial result — the markers received are correctly attributed to the first N commands; the rest never ran | keep the good prefix, then **re-issue the un-run tail automatically** until everything has an answer (`--no-auto-page` to opt out, which marks the remainder `<not executed: …>` instead) |
  | any count mismatch **without** the banner | genuine protocol inconsistency | raise `ConsoleError` and refuse to attribute at all, rather than risk pairing command *i*'s script with command *j*'s answer |

  Auto-paging is what a paged state dump wants: a 200-probe dump that overflows 32 KB just costs
  extra round trips instead of failing. It always terminates — each pass consumes at least one
  command, and a lone command whose *own* output overflows is recorded as
  `<truncated: this command's own output exceeds the 32768-byte cap; narrow it>` and stepped over.
* **Parse errors, and truncation of a single-command request, are never returned as data.** A body
  equal to `!! parse error`, or a one-command response carrying the truncation banner (where there
  is no salvageable prefix), raises `ConsoleError`. The banner's full text is
  `\n!! output truncated, raise RB3E_DTA_OUTPUT_MAX or split the script\n`.
* Caps: **`RB3E_DTA_SCRIPT_MAX` = 16384** bytes in (`>=` is a 413), **`RB3E_DTA_OUTPUT_MAX` = 32768**
  out. Both sides now cite the symbol names rather than the literals so they cannot drift apart
  again.
* In `--api auto`, only a **404** falls through to the next endpoint. A 403/409/413/504 is a real
  refusal and is surfaced immediately rather than masked by trying another path.

**Client-side validation.** `rb3e_dta.py` performs none — its only check is non-empty. All the
structural checking lives console-side in `RB3E_DTAEval_Validate` (`source/DTAEval.c:272-334`).
`dc3_eval.py` **ports that function to Python and runs it before sending**, because the console-side
failure modes are expensive: a crash costs the user a reboot mid-session. Semantics are matched
deliberately — `;` line comments, `"` toggles string mode with **no backslash escapes**, nesting
past 64 rejected, an embedded NUL ends the scan — plus two safe additions (`/* */` block comments,
which can only prevent false rejects, and the 16 KB size cap).

**What validation cannot catch.** A *balanced* script naming a nonexistent object still reaches the
parser and faults via `MILO_FAIL`, which is a C++ throw that neither a C DLL nor the AppChild path
can catch. Treat script text as trusted. This is why the file transport is the safest of the three
for exploratory probing: it runs through the game's own `RndConsole`, which wraps `ExecuteLine` in
`MILO_TRY`/`MILO_CATCH` (`src/system/rndobj/Console.cpp:434-444,522-527`) and merely prints
`Script error: …` — so on the chosen route a bad probe costs you a line of red text, not a reboot.

## 9. References

* `src/system/os/AppChild.cpp` — the 4543 channel
* `src/system/os/HolmesClient.cpp`, `HolmesClient_NetSocket.cpp`, `HolmesKeyboard.cpp` — Holmes
* `src/system/rndobj/Console.cpp` — the REPL, history, trailing-`/` auto-close
* `src/system/obj/DataArray.cpp`, `DataNode.cpp`, `Data.h:751` — the AppChild wire format
* `src/system/obj/DataFunc.cpp:1060-1098` — `run`, `read_file`, `write_file`,
  `write_string_to_file`, `file_exists`, `file_mkdir`
* `src/system/obj/DataFile.cpp:500-598` — `DataReadFile`, `CachedDataFile` (the ark redirect)
* `src/system/os/File_Win.cpp:9-13`, `src/system/os/File.cpp:460-540,600-640` — `FileIsLocal`,
  `FileMakePath`, `NewFile`
* `src/system/os/System.cpp:214-247,403-464,520-575` — `SystemPoll`, option parsing
* `src/system/os/System_Xbox.cpp:282-284` — `GetCommandLineA` entry
* `config/373307D9/symbols.txt`, `orig/373307D9/ham_xbox_r.map` — debug-XEX addresses
* [`docs/tools/HTTP_DEBUG_SERVER.md`](../tools/HTTP_DEBUG_SERVER.md) — the native-port endpoint this
  mirrors
* [`docs/native/KINECT_CAPTURE.md`](KINECT_CAPTURE.md) — debug-XEX boot procedure, pad cheat menu
* `/home/free/code/milohax/RB3Enhanced` — the DLL pattern for route 2
