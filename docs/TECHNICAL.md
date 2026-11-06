> Developer notes from the project's earlier versions (when it was called SkyPortal360 / skydrawn). Folder and file names here use the old names: `SkyPortal` is now `The Static Rift`, `skyportal.ini` is now `staticrift.ini` and `skydrawn16.xex` is now `KaosByte.xex` (Peer Pressure; the Dashlaunch build is `KaosByteDSL.xex`).

# Project Kaos-Byte4.1.0v — The Static Rift

(Formerly SkyPortal360.) The menu, the data folder (`The Static Rift`), `staticrift.ini` and the
plugin (`KaosByte.xex`) carry the new name; a folder still called `SkyPortal` and a
`skyportal.ini` keep working.

A virtual **Portal of Power** for the Xbox 360 Skylanders games on a modded console. No portal
hardware: figures are 1 KB dump files on the hard drive or a USB stick, and you put them on /
take them off the portal with a controller combo while you play. Progress the game saves to a
figure is written back into its file.

It comes in two builds from the same source:

| Your console | Build | Loaded by |
|---|---|---|
| RGH / JTAG, or BadUpdate + XeUnshackle (Dashlaunch) | `make -C xrt VARIANT=rgh` → `KaosByteDSL.xex` (or MSVC `Release`) | Dashlaunch `launch.ini` plugin |
| **Peer Pressure** softmod | `Release_PeerPressure` → `SkyPortal360.xex` | Peer Pressure DLL sideloading |

Peer Pressure doesn't load Dashlaunch plugins. It loads per-game title DLLs instead, which
suits SkyPortal: it only ever patches the game's own import table, never the kernel or XAM.

Supported titles (built in):

| Game | Title ID |
|---|---|
| Skylanders: Spyro's Adventure | 415608AB |
| Skylanders: Giants | 415608DA |
| Skylanders: Swap Force | 415608E9 |
| Skylanders: Trap Team | 41560911 |
| Skylanders: SuperChargers | 4156091F |
| Skylanders: Imaginators | 41560930 |

## How it works

The Xbox 360 games don't talk to the portal over plain USB HID like the PS3/Wii/PC versions do.
The 360 portal has a Microsoft security chip, and the games reach it only through XAM's raw
"non-controller" input API:

| XAM export | Ordinal | Use |
|---|---|---|
| `XamInputNonControllerGetRaw` | 0x4A1 | read a 32-byte portal frame |
| `XamInputNonControllerSetRaw` | 0x4A2 | send a 32-byte command frame |
| `XamInputNonControllerGetRawEx` / `SetRawEx` | 0x52A / 0x52B | same, newer dashboards |

When a Skylanders title starts, SkyPortal360 rewrites that title's imports of those four
functions so they land in a software portal instead of XAM. The Dashlaunch build does this from
a system thread that watches for Skylanders titles; the Peer Pressure build is loaded into the
game itself and does it before the game's first instruction runs.
The security chip never comes into it, because XAM (which does the chip handshake with real
hardware) is bypassed entirely. The software portal speaks the standard portal protocol
(`R`, `A`, `S`, `Q`, `W`, `C`, `J`, `M` ... wrapped in the 360's `0B 14` frame) and serves
figures out of RAM.

```
 Skylanders title ──XamInputNonController*Raw──▶ [patched import] ──▶ SkyPortal360
                                                                        ├─ XamBridge (0B 14 frames)
                                                                        ├─ VirtualPortal (16 slots)
                                                                        └─ FigureLibrary  ⇄ Hdd:\SkyPortal\Figures
```

## Install

Both builds share the same data folder. Copy `dist\SkyPortal` to the root of the console's
hard drive, so you have:

```
Hdd:\SkyPortal\skyportal.ini
Hdd:\SkyPortal\Figures\...           <- your figure dumps
```

(A `SkyPortal` folder on a USB stick works too; the hard drive is checked first.) Unplug any
real portal; the virtual one replaces it.

### Peer Pressure

1. `SkyPortal360.xex` is **already built** and sits in `dist\peerpressure\SideLoading\`. It
   was built without the Xbox 360 XDK by the open toolchain in `xrt/` (see *Building without the
   XDK*). You can also build it yourself, with the XDK (**Release_PeerPressure**) or without it
   (`make -C xrt`).
2. Copy everything in `dist\peerpressure\SideLoading` (the DLL and the six `.ini` files) into
   `Hdd1:\PeerPressure\SideLoading\`:
   ```
   Hdd1:\PeerPressure\SideLoading\SkyPortal360.xex
   Hdd1:\PeerPressure\SideLoading\415608ab.ini    Spyro's Adventure
   Hdd1:\PeerPressure\SideLoading\415608da.ini    Giants
   Hdd1:\PeerPressure\SideLoading\415608e9.ini    Swap Force
   Hdd1:\PeerPressure\SideLoading\41560911.ini    Trap Team
   Hdd1:\PeerPressure\SideLoading\4156091f.ini    SuperChargers
   Hdd1:\PeerPressure\SideLoading\41560930.ini    Imaginators
   ```
3. Start a Skylanders game. Peer Pressure shows its "dll loaded" toast, then SkyPortal shows
   `SkyPortal360: virtual portal ready`.

(`Hdd1:` is the same drive Dashlaunch-era guides call `Hdd:`. The data folder above goes at
`Hdd1:\SkyPortal\`.) Don't add anything to `launch.ini`: Peer Pressure doesn't run Dashlaunch.

The DLL loads after the game xex is in memory and before its entry point runs. It patches the
game's portal and controller imports right there, before any game code runs, then starts a
thread for the figure menu and saving, and unloads when you quit. Figure saves happen within a
quarter second of the game writing, because the DLL disappears the moment the game exits.

**If Peer Pressure shows no "dll loaded" toast:**

- Each `.ini` sets only `TitleID` and `FileName`, so it isn't tied to one disc, region or title
  update. If your Peer Pressure version wants more, open the example it installs at
  `Hdd1:\PeerPressure\SideLoading\SideLoad.ini` and add `MediaID` / `ModuleVersion` for your
  copy. Aurora's game details or `xextool -l default.xex` on a PC show them. Once SkyPortal has
  loaded once, `skyportal.log` also prints them in `.ini` syntax:
  `Game: TitleID = 0x41560911  MediaID = 0x...  ModuleVersion = "..."`.
- Alternatively, put the files next to the game instead. Peer Pressure checks the game's own
  folder first: copy `SkyPortal360.xex` and that game's `.ini` (renamed `sideload.ini`) into the
  folder that holds the game's `default.xex` (or its GOD package folder).
- If the toast appears but the game won't start, the DLL's base address clashes with the game.
  Change `baseaddr` in `xex_sideload.xml` (e.g. `0x8C000000`) and rebuild.

### Dashlaunch (RGH / JTAG / XeUnshackle)

1. Build the **Release** configuration → `bin\Release\SkyPortal360.xex`, and put it in
   `Hdd:\SkyPortal\`.
2. Add it to Dashlaunch's `launch.ini`, in `[Plugins]`, using a free slot
   (`dist\dashlaunch\launch.ini.snippet`):
   ```
   plugin1 = Hdd:\SkyPortal\SkyPortal360.xex
   ```
3. Reboot.

## Figures

Each figure is a raw **1024-byte** MIFARE 1K dump of the toy, encrypted exactly as it is on the
toy (the game decrypts it itself). Any extension works (`.bin`, `.dump`, `.sky`, ...); only the
1024-byte size is checked. Sub-folders are fine, two levels deep. Dump your own toys with a
Proxmark, ACR122U, Flipper Zero, or a real portal + SkyReader on a PC.

The file name is what the on-screen menu shows. Trap Team traps, SuperChargers vehicles and
Imaginators crystals are figures like any other.

## Controls

Hold the **modifier** (default **LB + RB**), then:

| Button | Action |
|---|---|
| D-pad Right / Left | next / previous figure (hold to scroll) |
| D-pad Down / Up | jump 10 forward / back |
| A | put the selected figure on the portal |
| B | take the selected figure off |
| Y | **swap**: take everything off, put the selected figure on |
| X | take everything off |
| Start | show what's on the portal |

A toast shows the selected figure (`Tree Rex [on portal] (12/48)`). While the modifier is held
those buttons are hidden from the game, so browsing doesn't also jump or attack. Change the
modifier in `skyportal.ini` (`Modifier = BACK+LT`, `LS+RS`, ...) if a game needs LB + RB.

The portal holds up to 16 figures at once (what the protocol allows). New files dropped into
`Figures` over FTP show up the next time you press the modifier.

## Saving and backups

When the game writes to a figure (level, gold, hats, nickname), SkyPortal360 saves the file
within a second. The first time a file changes, the untouched original is copied to
`SkyPortal\Backups\<name>.bak`, and existing backups are never overwritten. To roll back, copy a
`.bak` over the figure. Set `SaveWrites = 0` to keep figures read-only.

## Settings (`skyportal.ini`)

| Key | Default | |
|---|---|---|
| `[General] Enabled` | 1 | 0 = virtual portal off, the game talks to a real portal as normal |
| `Notifications` | 1 | toast pop-ups |
| `SaveWrites` | 1 | write game progress back into figure files |
| `RememberPortal` | 1 | keep the same figures on the portal across reboots |
| `Speaker` | 1 | report a Traptanium speaker (audio is discarded) |
| `[Controls] Modifier` | `LB+RB` | menu modifier |
| `HideComboFromGame` | 1 | hide menu buttons from the game while the modifier is held |
| `[Titles] ExtraN` | | Dashlaunch build: extra title IDs (hex) to treat as Skylanders |
| `PatchAllTitles` | 0 | Dashlaunch build: patch any title (don't: Disney Infinity uses the same API) |
| `[Startup] FigureN` | | figures placed at boot (name or path inside `Figures`) |

With Peer Pressure, which games get SkyPortal is decided by the `.ini` files in
`SideLoading`, so the `[Titles]` section isn't used.

A log is written to `SkyPortal\skyportal.log` every boot (Dashlaunch) or every game launch
(Peer Pressure). It shows which title was hooked, how
many imports were patched, every place/remove, and every save.

## Building

You need what any Dashlaunch plugin needs:

- Visual Studio 2010 with the **Xbox 360 SDK** (XDK 2.0.20871 – 2.0.21256)
- **xkelib** (kernel / XAM export headers + import libs), built once for your SDK version

Put xkelib in `.\xkelib` (or `msbuild /p:XkelibDir=C:\path\to\xkelib`), open
`SkyPortal360.sln`, and build:

- **Release | Xbox 360** → `bin\Release\SkyPortal360.xex` (Dashlaunch). If another plugin
  already uses base address `0x91C80000`, change `baseaddr` in `xex.xml`.
- **Release_PeerPressure | Xbox 360** → `bin\Release_PeerPressure\SkyPortal360.xex`
  (Peer Pressure). Same code with `SKYPORTAL_SIDELOAD` defined, built as a title DLL
  (`xex_sideload.xml`, no `<sysdll/>`).

### Building without the XDK (Peer Pressure DLL)

`xrt/` builds the Peer Pressure DLL with nothing from Microsoft: clang and ld.lld (LLVM 14 or
newer, with the PowerPC target) and Python 3. Works on Linux and WSL:

```
make -C xrt                     # -> xrt/build/SkyPortal360.xex
make -C xrt BASE=0x8C000000     # if the default base address clashes with a game
```

How it fits together:

- **`xrt/include/`**: stand-ins for `xtl.h`, `xkelib.h` and `ppcintrinsics.h`, declaring the small
  Win32 subset the plugin uses. `xrt.cpp` implements that subset directly on kernel and XAM
  exports (`NtCreateFile`, `NtQueryDirectoryFile`, `ExCreateThread`, `XamInputGetState`, and so
  on). `xrt_libc.cpp` and `xrt_printf.cpp` supply the C library pieces.
- **The calling-convention boundary.** clang emits stock 32-bit SysV code, but the console's
  code is MSVC's. The differences that matter were measured on retail consoles by the xsdk
  project, and two kinds of glue handle them:
  - `gen_imports.py` generates a wrapper per import. It zero-extends the arguments, puts a 9th
    argument in the 0x54(r1) slot, and calls inside its own 0x60-byte frame, because MSVC
    callees may spill into their caller's argument slots.
  - `entry.S` wraps everything the console calls *into* the plugin: the DLL entry point, the
    thread start, and the five game hooks. Those wrappers keep r14-r31 whole as 64-bit
    registers, because stock 32-bit code only preserves their low halves.
- **Packaging.** `dll.ld` lays the image out on 64 KB pages at `0x8A000000`. `elf2xex.py`
  (adapted from xsdk, MIT) writes an unencrypted XEX2 with module flags TITLE | DLL, the import
  table, and page hashes. Peer Pressure removes signature checks, so it loads unsigned DLLs.

The portal logic in `src/core` has no Xbox dependencies. To run its tests on a PC:

```
make -C tests run
```

The portal tests play the game's side of the handshake (ready, activate, status stream, block
reads and writes, removal, 16 slots, speaker frames, session reset, short buffers, queue
overflow) through the same `XamBridge` the hooks use. `test_patch` runs the import patcher
against a fake title import table (several import modules, duplicate imports, jump encoding,
idempotence) and checks the execution-ID reader. `test_config` checks the ini reader. It also
syntax-checks every plugin source for both builds, for 32-bit big-endian PowerPC, against stub
XDK headers (needs clang). `test_xrt` checks the open toolchain's printf and string functions
against the host C library.

`make -C tests emu` runs the built DLL itself on an emulated PowerPC 970 (Unicorn) in the
console's mode: 32-bit addressing with 64-bit registers. A fake loader, kernel, XAM,
filesystem and Skylanders game stand in for the console, and the run covers the whole flow:

- the XEX is parsed and its imports resolved the way the loader does;
- DllMain patches the game's imports;
- the worker thread reads the settings, scans the figures, writes the log and shows the toasts;
- the game does the portal handshake through its own patched import stubs, reads figure
  blocks, and writes a block that gets saved with a backup;
- the controller menu browses and places a figure, and the game sees the combo buttons hidden.

Every kernel/XAM call is checked against the console's convention, and each fake export
scribbles over its caller's argument slots, the stack below r1, and all volatile registers, as
an MSVC callee may. Every game→hook call must give back r14-r31 whole. Breaking the DLL on
purpose makes the run fail each way: a trampoline saving a register's low half only, wrappers
without zero-extension, wrappers without their frame. It needs Unicorn's Python binding
(`UNICORN_PY=... UNICORN_LIB=...`). Unicorn 2's PowerPC 64 CPU model selection has an indexing
bug: the run needs its 970 model, which takes a one-line fix in
`qemu/target/ppc/translate_init.inc.c` that adds the 32-bit model count to the requested index.

This is still an emulator, not a console. The DLL hasn't been run on real hardware yet.

## Troubleshooting

- **No "virtual portal ready" toast.** Check `skyportal.log`.
  - Dashlaunch: if it shows `Title now 4156xxxx` but never `Hooked N import(s)`, the title ID
    or XAM version differs. Add the ID under `[Titles]` and send the log lines that list the
    XAM exports.
  - Peer Pressure: if there's no log at all, the DLL wasn't loaded (no Peer Pressure toast
    either). Check the `.ini` name matches the title ID and see *If Peer Pressure shows no
    "dll loaded" toast* above. If the log says `Hooked 0 import(s) before the game started`,
    send the log: that game version reaches the portal some other way.
- **"Plug in the Portal of Power" stays up.** Unplug the real portal, make sure
  `Enabled = 1`, and check the log.
- **Figure not detected.** It must be exactly 1024 bytes. Try taking it off (B) and putting it
  back (A). Swap (Y) also forces a clean re-announce.
- **Dashlaunch plugin doesn't load.** Look for a base-address clash with another plugin, or a
  wrong path in `launch.ini`.

## Credits

- Portal protocol and Xbox 360 framing were ported from
  [TheBiemGamer/skylanders-portal](https://github.com/TheBiemGamer/skylanders-portal) (MIT),
  which was itself built from the portal emulation in Dolphin, RPCS3 and Cemu.
- [Xenia Canary](https://github.com/xenia-canary/xenia-canary) documented the XAM raw
  non-controller API (argument order, device ids 5/6, 0x20-byte buffers).
- Import patching follows the long-standing xkelib/Dashlaunch plugin approach.
- The Peer Pressure build follows
  [Peer Pressure's DLL sideloading rules](https://github.com/grimdoomer/Xbox360PeerPressure/wiki/Softmod-Features#dll-sideloading),
  with the `.ini` format taken from RB3Enhanced's sideload config.

Skylanders is a trademark of Activision. This project is unofficial, ships no game data or
figure dumps, and is for use with games and figures you own.

## Figure menus (1.4)

Three builds of the same plugin (`make -C xrt all-variants`):

| Build | File | Menu |
|---|---|---|
| classic | `SkyPortal360.xex` | hold LB+RB, toasts show the selection |
| skypop | `skypop14.xex` | tap LB+RB: the console's message box with « Previous / Put on portal / Next » |
| skydrawn | `skydrawn16.xex` | tap LB+RB: a full-screen menu (figure picture, element, game logo, series, portal state); tap again to close |

skydrawn hooks the game's `VdSwap` import (`xrt/entry.S`, `sp_vdswap_hook`). While the menu is
closed the stub only notes the game's front buffer. While it's open the stub hands VdSwap a copy of
the game's fetch constant and front-buffer address that point at one of the plugin's own frames
(`src/xbox/overlay.cpp`), so the display shows the menu and nothing is ever drawn into the game's
buffers: no flicker. Two frames are used (one shown, one being drawn); they and the pictures are
allocated on open and freed a few frames after closing. The game keeps running underneath and
sees an idle controller until the menu is closed and every button is let go.

The menu reads its pictures from the SkyPortal folder, made on the PC by `tools/mkassets.py`:
`UI\background.qoi`, `UI\elements\*.qoi`, `UI\games\1-6.qoi`, `Images\...\*.qoi` (QOI images)
and `UI\figures.idx` (name, game, element, series and picture of every dump in the Skylanders NFC
pack layout). The same script copies the pack's dumps into `Figures\` with console-safe names
(FATX: up to 42 characters, no commas). Dumps kept loose at the top of `Figures\` (your own
figures) are listed first as "My Figures" and get their details from the index by file name.

`make -C tests preview ASSETS=<SkyPortal folder>` renders the menu on the PC with the same code;
`make -C tests emu` runs all three builds on the emulated CPU (set `SKYPORTAL_ASSETS` for real
pictures), including captures of the menu frames.

## Phone remote (Peer Pressure, and the Dashlaunch build with the drawn menu)

`src/xbox/remote.cpp` is a small HTTP/1.1 server on its own game thread. It starts 8 s after the
game (so the game sets up its own networking first), then calls `NetDll_XNetStartup` and
`NetDll_WSAStartup` as the title (caller type 1; both only count up when the game has already
started them), opens a TCP socket, marks it insecure (`setsockopt(SOL_SOCKET, 0x5801)`: plain TCP,
not the console's encrypted traffic; Peer Pressure removes the xex privilege check that normally
guards this) and listens on port 80, or 8080 when 80 is taken. `NetDll_XNetGetTitleXnAddr` gives
the console's address for the toast, the log and the menu header.

Since 3.1.0v it serves several connections at once (eight in 3.2.0v) from that one thread,
non-blocking with `select()`, and keeps them open between requests (HTTP keep-alive). 3.0.0v answered one request per connection
and closed it: the console's TCP sockets come from a small pool (XNet's default is 32 stream
sockets), and a socket the Xbox closes first stays in TIME_WAIT for a while still holding its place,
so a screenful of pictures used the pool up and new connections were refused ("connection lost").
Each response now goes out in one `send()` where it can (the headers in front of the body, or of a
file's first 16 KB) with `TCP_NODELAY`, instead of a small header send followed by the body, which
waited on the phone's delayed ACK (about 200 ms) every time. The listening socket is opened again if
it fails; the server never stops while the game runs. Its thread runs at a slightly raised priority
(+2) and sleeps in `select()` when there's nothing to do.

The phone's pictures are `The Static Rift\Phone\...` (made by `tools/mkphone.py`): 160 x 160 JPG crops
of the cards' tops, about 6 KB each instead of about 110 KB for the menu's QOI pictures; element
icons and game logos as PNG; the background as JPG.

**Live updates (3.2.0v).** The page keeps one `GET /api/events` open (Server-Sent Events). The
worker thread bumps a revision counter (`remote::Changed()`, called from `SavePortalState`, so the
phone, the TV menu and a swap finishing all count); the server thread sees a stream whose revision
is behind, posts `/api/state` to the mailbox for it, and sends the answer as one `data:` event. The
mailbox keeps 512 bytes of head room in front of the answer, so HTTP headers or the `data: ` prefix
go in front without a copy. Every stream gets `retry: 1000` (browsers reconnect a dropped stream
after a second) and a ping event every 5 s. When all eight slots are taken, the connection that has
been idle longest without asking for anything (browsers open spares ahead of time) gives up its
slot to the new one.

**The page (3.2.0v)** is built to run in any phone browser and stay light however long the list:
- pictures are plain `<img>` tags the browser manages (no `blob:` URLs, no canvas copies). An
  `IntersectionObserver` gives a card its picture when it comes within 1200 px of the screen and
  takes it back (`src` removed) when it goes further away, so about 60 pictures are held at any
  time instead of one per card scrolled past. A queue loads three at a time, the ones on the
  screen first, then the nearest; a picture lost to a dropped connection is retried, and only a
  real 404 falls back to the menu's QOI picture (decoded in JavaScript, canvas freed at once);
- no `backdrop-filter`, no blurred full-screen fixed layer (expensive on WebKit at 3x scale), no
  flex `gap`, `aspect-ratio` or `inset`; the script is plain ES5 plus `fetch`/`Promise`/
  `EventSource` (iOS 10.3+, any current Chrome, Firefox, Samsung Internet, Edge);
- actions show at once (the chip and the card's ON badge appear on the tap), go to the Xbox in
  order through a queue (none dropped while one is on its way), are tried twice (doing one again
  is harmless), and are undone on the page if the Xbox says no;
- Quick tap: one tap on a card sends Swap in; holding it (450 ms, or right-click) opens its sheet;
- without `EventSource`, or when the stream keeps failing, the page asks for `/api/state` every
  2 s instead and tries the stream again every 30 s. The status dot turns yellow only when a
  reconnect takes more than 2 s or two pings went missing, and red after 16 s without a word; a
  quiet stream is started again, and coming back from the lock screen (`visibilitychange`,
  `pageshow`, `online`) catches up at once.

Measured in the emulator with an Xbox-like network (`XBOX_LIMITS=1` in `tests/emu/run_remote.py`: a
32-socket pool, 60 s TIME_WAIT, a 1.5 MB/s link, Nagle with delayed ACKs) and `remote_load.js` as the
phone: 3.0.0v never finished the first screen of pictures (7 of 8 after 60 s), every scroll step timed
out and 71 connections were refused; 3.1.0v shows the first screen in under half a second, each
scroll step in about 0.13 s, over 4 connections in all, with 0.7 MB sent instead of several MB. On a
400 KB/s link losing a connection on 15% of its sends, every screen still fills (1.5 s at worst).

3.2.0v, same set-up: the first screen in 0.3 s (pictures 0.11 s after the list), each scroll step
0.11-0.12 s or none at all (already loaded ahead), 5 connections. On the lossy link: the first
screen in 0.65-1.3 s, the slowest scroll step 0.84 s, never red and yellow in 3 of 78 samples, with
28 connections killed along the way. `remote_browser.js` measures (in the emulator, where the
console's CPU is emulated): Quick tap shows the figure the moment it's tapped and the Xbox confirms
in about 60 ms; a change made elsewhere reaches the page in 50-80 ms with no polling; after a 4 s
Wi-Fi blip (every connection reset, new ones refused, `XBOX_BLIP_FILE` in `run_remote.py`) the
stream reconnects by itself and the next change shows in about 60 ms. `remote_mem.js` (a phone-sized
Chromium slowed down 4x, the whole Trap Team list of 516 scrolled, 20 figures opened): 3.1.0v ended
up holding 506 pictures and 541 `blob:` URLs, 3.2.0v 60 and none. Chromium's own process memory
barely moves either way (it drops off-screen decoded pictures by itself); WebKit (Safari, and every
browser on iPhone) keeps far more of what the page holds, which is where the old page ran out.
WebKit itself can't run in this test set-up, so the Safari side rests on avoiding what WebKit is
known to struggle with rather than on a measurement.

| Request | Answer |
|---|---|
| `GET /` | `The Static Rift\UI\remote.html` (re-read every time, never cached) |
| `GET /UI/...`, `GET /Images/...`, `GET /Phone/...` | files from those three folders only (no `..`, no drive letters; cached a week) |
| `GET /api/events` | `text/event-stream`: the state now and on every change (`data:`), a `ping` event every 5 s |
| `GET /api/state` | the running game, what's on the rift (and a figure being swapped in), the lists (Favorites and your own included), element names, `rev`, `nrev` |
| `GET /api/meta` | nicknames (`[[id,"nick"],...]`) and every list's figures (`[[view,"name",[ids]],...]`); fetched when `nrev` changes |
| `POST /api/nick?i=<id>&n=<nickname>` | gives a figure a nickname (cleaned up as below; empty takes it away) |
| `POST /api/lists?a=add\|remove\|new\|rename\|delete&l=<view>&i=<id>&n=<name>` | Favorites (view 8) and your own lists (9-20): add / take out a figure, make one (with a figure in it), rename, delete |
| `GET /api/list?v=<list>&from=<n>` | one page (up to 24 KB) of a list: All (0), My Picks (1), a game (2-7); `next` says where the next page starts |
| `POST /api/do?a=add|swap|remove|clear&i=<id>` | the menu's A / Y / B / X for that figure, with a message |

The server thread only moves bytes. API calls go through a one-slot mailbox to the worker thread
(the one that runs the controller menu and saves), which answers them between its 20 ms ticks, so
the figure library and the rift are only ever touched from one thread. A list request builds the
whole list once (switching the library to that list for a moment and back, on the same figure the
menu shows) and hands it out page by page.

NetDll ordinals (xam.xex): socket 0x03, closesocket 0x04, shutdown 0x05, ioctlsocket 0x06, setsockopt 0x07, bind 0x0B,
listen 0x0D, accept 0x0E, select 0x0F, recv 0x12, send 0x16, WSAGetLastError 0x1B, WSAStartup 0x01,
XNetStartup 0x33, XNetGetTitleXnAddr 0x49 (Xenia's `xam_table.inc`).

### Putting figures on fast (4.1.0v)

From a tap on the phone to the figure on the virtual portal, nothing waits that doesn't have to:
- the worker naps in 2 ms steps while the phone remote is listening and wakes as soon as a request
  is in the mailbox (`remote::Waiting`), answering it before anything else in its round; the
  server waits on the answer in 1 ms selects;
- Swap in no longer saves game progress before taking the old figures off: the portal keeps what
  the game wrote to a figure that came off (`pending_`), and the worker saves it in the background
  while the new one waits. Only a figure put back on while its file is behind (`HasUnsaved`) is
  saved first;
- `rift_state.txt` is written a second after the rift settles instead of on every change;
- the swap gap, the moment between the game seeing the old figures come off and the new one going
  on, is `[General] SwapGapMs` (150 ms, at least 3 more status reports; Giants at least 400 ms and
  8 reports) instead of 750 ms and 30 reports. Removal and arrival still never share a status
  report (what left Giants on its loading screen), and while a swap waits the worker checks every
  2 ms;
- once a game runs, the worker is one step above the game's ordinary threads (on the Dashlaunch
  build it's a system thread, which otherwise queues behind the game's).
In the emulator a swap from the phone now reaches the game 11 polls after the request (the old
gap alone was over 30); on the console that's about 150-200 ms plus the game's own reading of the
figure.

### The Dashlaunch build (4.0.0v)

The Dashlaunch plugin is a system DLL whose worker is a system thread that lives across games, but
the server has to use the game's network the way the game's own code does, and go with it. So
with each Skylanders game `StartGame` asks for the server thread (`util::RequestGameThread`), and
the next game thread that passes through one of the plugin's hooks (the game polling the
controller through the `XamInputGetState` hook, or presenting a frame through the menu's `VdSwap`
hook) starts it as one of the game's own threads (`util::ServeGameThread`, `ExCreateThread`
without the system flag). From there it is the same server as on Peer Pressure (caller type 1,
the same insecure socket the freeboot / Dashlaunch patch sets allow, as Peer Pressure's do). When
the game exits, its threads, sockets and open files go with it; `remote::TitleEnded` forgets them
(never closing handles that were the game's), frees the mailbox lock in case the server held it,
and the next game starts a fresh server. The worker answers the API between its own 20 ms ticks,
as on Peer Pressure. `run_remote.py` runs the whole of its check list on `KaosByteDSL.xex` too:
boot on the dashboard, Trap Team starts, the server comes up on the game's input thread, every
HTTP and browser check, then the game exits (the remote stops and nothing answers) and starts
again (the remote comes back, with its pop-up, and works).

### Nicknames and lists (4.0.0v)

`src/core/collections.cpp` (portable) keeps nicknames and lists; `main.cpp` reads them after each
game's scan and writes them the moment something changes (`MetaChanged`, worker thread only), then
bumps `nrev` and pushes the state to the phones. Both files sit next to `staticrift.ini` and name
figures by their file (the path relative to `Figures`, stable for a figure); updates never ship them:

    nicknames.ini                              lists.ini
    [Nicknames]                                [Favorites]
    Skylanders\...\Chop Chop.dump = Choppy     Skylanders\...\Chop Chop.dump
                                               [Speedrun]
                                               ...

A line naming a figure that isn't in `Figures` right now is kept and written back (up to 16
nicknames, 32 list lines). Typed text is cleaned up the same way on the phone and the console:
printable ASCII only (what the menu's font has), single spaces, trimmed, 20 characters for a
nickname, 24 for a list's name (no `[` `]`); list names are unique ignoring case. Favorites is list
0 (view 8) and can't be renamed or deleted; up to 12 lists of your own, 160 figures each.

`FigureLibrary::SetListView` shows a list in the menu: its figures in its own order, each once,
through the same game filter as everything else. R3 goes ALL, then Favorites and each list that has
figures, then My Picks, then each game. When the phone changes the list the TV is showing, the menu
follows (`ListsChanged`), and goes back to ALL if the list was deleted or emptied.

The nickname keyboard (START on a figure, `keyboard.h`) is drawn by `overlay_render.cpp` as a panel
over the dimmed menu: 4 rows of 10 keys (a-z, `- ' . !`, 0-9) and SPACE / DEL / CAPS / DONE; the
first letter is a capital until CAPS is pressed. D-pad with auto-repeat, A types, B deletes, X
space, Y capitals, START saves, BACK cancels; the modifier closes the menu without saving. The
nickname shows under the figure's name (the name drops a font size); the hint row makes room for
START NICKNAME and the R3 list by dropping D-PAD BROWSE when it has to.

Tests: `tests/test_collections.cpp` (reading, writing, cleaning up, list editing, lines kept for
missing figures), `tests/test_figures.cpp` (the list view), `run_dll.py` (drawn and rgh rounds:
START, typing "Rexy" on the keyboard, saved to nicknames.ini, BACK cancels; frames saved as
emu_keyboard.png and emu_nickname.png) and `run_remote.py` / `remote_browser.js` (the API, the
files written, and the page: a nickname between the name and the series, the star, the Favorites
tab, a list made from a figure's Lists, renamed and deleted, `+ New list`).

Tests: `tests/test_remote.cpp` (request parsing, query values, which files are served, JSON,
headers, keep-alive) and `tests/emu/run_remote.py` (the built xex on the emulated CPU with NetDll backed by real
sockets; plain HTTP checks, then `remote_browser.js` drives a phone-sized Chromium through the page,
and the fake game finds the figure the browser put on). 3.2.0v adds Quick tap, live-update and Wi-Fi
blip checks to `remote_browser.js`; `REMOTE_MODE=load` with `remote_load.js` or `remote_mem.js` times
the page and measures what it holds.
